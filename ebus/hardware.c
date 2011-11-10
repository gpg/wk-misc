/* hardware.c - Hardware related code for an Elektor Bus Node
 * Copyright (C) 2011 g10 Code GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/crc16.h>

#include "ebus.h"

/* UART defs.  */
#define UBRR_VAL   ((F_CPU+8*BAUD)/(16*BAUD)-1)
#define BAUD_REAL  (F_CPU/(16*(UBRR_VAL+1)))
#define BAUD_ERROR ((1000*BAUD_REAL)/BAUD)
#if (BAUD_ERROR < 990 || BAUD_ERROR > 1010)
# error computed baud rate out of range
#endif




/* EEPROM layout.  We keep configuration data at the start of the
   eeprom but copy it on startup into the RAM for easier access.  */
struct __attribute__ ((packed)) ee_config_s
{
  uint16_t  reserved;
  byte      nodeid_hi;
  byte      nodeid_lo;
  byte      reserved2[12];
};

struct ee_config_s ee_config EEMEM = {0};
struct ee_data_s ee_data EEMEM = {NODETYPE_UNDEFINED, 0, {{0}}};

/* End EEPROM.  */


/* The current time measured in quantities of 10 seconds with
   explicit roll over after 7 days.  */
static volatile uint16_t current_time;

/* Milliseconds in the 10 second period.  */
static volatile uint16_t current_clock;




/* Read key S2.  Return true once at the first debounced leading edge
   of the depressed key.  For correct operation this function needs to
   be called at fixed intervals. */
byte
read_key_s2 (void)
{
  static uint16_t state;

  state <<= 1;
  state |= !KEY_S2;
  state |= 0xf800; /* To work on 10 continuous depressed readings.  */
  return (state == 0xfc00); /* Only the transition shall return true.  */
}

byte
read_key_s3 (void)
{
  static uint16_t state;

  state <<= 1;
  state |= !KEY_S3;
  state |= 0xf800; /* To work on 10 continuous depressed readings.  */
  return (state == 0xfc00); /* Only the transition shall return true.  */
}


/* Return the current time.  Time is measured as the count of 10
   second periods passed in a 7 day period.  */
uint16_t
get_current_time (void)
{
  return current_time;
}


uint16_t
get_current_fulltime (byte *r_deci)
{
  uint16_t hi, lo;

  cli ();
  hi = current_time;
  lo = current_clock;
  sei ();
  *r_deci = lo/100;
  return hi;
}


void
set_current_fulltime (uint16_t tim, byte deci)
{
  cli ();
  current_time = tim;
  current_clock = deci * 100;
  sei ();
}



/*
   Interrupt service routines
 */


/* 1ms ticker interrupt service routine. */
ISR (TIMER2_COMPA_vect)
{
  uint16_t clockval;

  current_clock++;

  if (current_clock >= 10000)
    {
      /* 10 seconds passed.  Bump the current time.  */
      current_time++;
      if (current_time == (uint16_t)1440 * 6 * 7 )
        current_time = 0; /* Weekly roll-over.  */
      current_clock = 0;
    }

  clockval = current_clock;
  ticker_bottom (clockval);
}


/* Setup for some parts of the hardware.  The caller needs to pass the
   node type so that the EEPROM will be erased if it does not match
   the node type. */
void
hardware_setup (byte nodetype)
{
  byte value;

  /* Port D configuration:
     PIND.7 = Inp: KEY_S3
     PIND.6 = Out: LED_Transmit
     PIND.5 = Inp: KEY_S2
     PIND.4 = Out: LED_Collision
     PIND.3 = Out: LT1785-pin2 = !RE
     PIND.2 = Out: LT1785-pin3 = DE
     PIND.1 = TXD: LT1785-pin4 = DI
     PIND.0 = RXD: LT1785-pin1 = RO
  */
  PORTD = _BV(7) | _BV(5);  /* Enable pull-ups.  */
  DDRD  = (_BV(6) | _BV(4) | _BV(3) | _BV(2) | _BV(1));

#if (KEY_S3_BIT != 7 || KEY_S2_BIT != 5                 \
     || LED_Transmit_BIT != 6 || LED_Collision_BIT != 4)
# error mismatch with hardware.h
#endif

  /* Set the UART: 8n1, async, rx and tx on, rx int enabled.
     Baud rate set to the value computed from BAUD.  */
  UCSR0A = 0x00;
  UCSR0B = _BV(RXCIE0) | _BV(RXEN0) | _BV(TXEN0);
  UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
  UBRR0H = (UBRR_VAL >> 8) & 0x0f;
  UBRR0L = (UBRR_VAL & 0xff);

  /* Timer 2: 1ms ticker.  */
  TCCR2A = 0x02;   /* Select CTC mode.  */
  TCCR2B = 0x04;   /* Set prescaler to 64.  */
  TCNT2 = 0x00;    /* Clear timer/counter register.  */
  OCR2A  = 249;    /* Compare value for 1ms.  Note: This is one less
                      than the naively computed value 250.  */
  TIMSK2 = 0x02;   /* Set OCIE2A.  */

  /* Copy some configuration data into the RAM.  */
  config.nodeid_hi = eeprom_read_byte (&ee_config.nodeid_hi);
  config.nodeid_lo = eeprom_read_byte (&ee_config.nodeid_lo);


  /* Lacking any better way to seed rand we use the node id.  A better
     way would be to use the low bit of an ADC to gather some entropy.
     However the node id is supposed to be unique and should thus be
     sufficient.  */
  srand (config.nodeid_lo);

  /* Clear the node specific eeprom if the node type changed.  */
  value = eeprom_read_byte (&ee_data.nodetype);
  if (value != nodetype)
    {
      int i;
      uint32_t dw = 0;

      for (i=0; i < sizeof ee_data; i += sizeof dw)
        eeprom_write_dword ((uint32_t*)(&ee_data.nodetype+i), dw);
      eeprom_write_byte (&ee_data.nodetype, nodetype);
    }

}
