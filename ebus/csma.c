/* csma.c - CMSA code for an Elektor Bus Node using an ATmega88
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
 *
 * 2011-06-01 wk  Initial code.
 * 2011-08-22 wk  Changed to a CSMA/CD protocol
 * 2011-09-05 wk  Modularized code.
 */

#include "hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/crc16.h>

#include "ebus.h"




/* We use timer 0 to measure the time for one octet.  With 8n1 an
   octet is represented by 10 bits on the wire which takes the time
   Tc.

   |   Baud |    Tc |    Tc |   Tc |
   |        |  (ms) | c/256 | c/64 |
   |--------+-------+-------+------|
   |   9600 |  1.04 |    65 |      |
   |  19200 |  0.52 |    33 |      |
   |  38400 |  0.26 |       |   65 |
   |  57600 |  0.17 |       |   43 |
   |  76800 |  0.13 |       |   33 |
   | 115200 | 0.087 |       |   22 |
*/
#if BAUD == 9600
# define T_x_PSCALE 256
# define T_c_CMPVAL  65
#elif BAUD == 19200
# define T_x_PSCALE 256
# define T_c_CMPVAL  33
#elif BAUD == 38400
# define T_x_PSCALE  64
# define T_c_CMPVAL  65
#elif BAUD == 57600
# define T_x_PSCALE  64
# define T_c_CMPVAL  43
#elif BAUD == 76800
# define T_x_PSCALE  64
# define T_c_CMPVAL  33
#elif BAUD == 115200
# define T_x_PSCALE  64
# define T_c_CMPVAL  22
#else
# error Specified baud rate not supported
#endif


static volatile unsigned int frames_sent;
static volatile unsigned int frames_received;
static volatile unsigned int collision_count;
static volatile unsigned int overflow_count;

/* Flag indicating that a PCINT2 was triggered.  */
volatile byte rx_pin_level_change;

/* The buffer filled by an ISR with the message.  */
static volatile byte rx_buffer[MSGSIZE];

/* The buffer with the currently sent message.  We need to store it to
   send retries.  It is also used by the receiver to detect collisions.  */
static volatile byte tx_buffer[MSGSIZE];
static volatile uint16_t tx_buffer_crc;

/* Flag set if we do not want to receive but check our own sending for
   collisions.  */
static volatile byte check_sending;

/* If true RX_BUFFER has a new message.  This flag is set by the ISR
   and must be cleared by the main fucntion so that the ISR can
   continue to work.  */
static volatile byte rx_ready;





/* Reset the gap timer (timer 0).  Note that this resets both
   timers.  */
static void
reset_retry_timer (void)
{
  TCCR0B = 0x00;   /* Stop clock.  */
  TIFR0  = _BV(OCF0A) | _BV(OCF0B);  /* Clear the flags.  */
  TCNT0  = 0x00;   /* Clear timer/counter register.  */

  /* Start the clock.  */
#if T_x_PSCALE == 256
  TCCR0B = 0x04;   /* Set prescaler to clk/256.  */
#else
  TCCR0B = 0x03;   /* Set prescaler to clk/64.  */
#endif
}


static inline int
t_c_reached_p (void)
{
  return !!(TIFR0 & _BV(OCF0A));
}


static void
wait_t_c_reached (void)
{
  set_sleep_mode (SLEEP_MODE_IDLE);
  while (!t_c_reached_p ())
    {
      cli();
      if (!t_c_reached_p ())
        {
          sleep_enable ();
          sei ();
          sleep_cpu ();
          sleep_disable ();
        }
      sei ();
    }
}


/* Compute the CRC for MSG.  MSG must be of MSGSIZE.  The CRC used is
   possible not the optimal CRC for our message length.  However we
   have a convenient inline function for it.  */
static uint16_t
compute_crc (const volatile byte *msg)
{
  int idx;
  uint16_t crc = 0xffff;

  for (idx=0; idx < MSGSIZE; idx++)
    crc = _crc_ccitt_update (crc, msg[idx]);

  return crc;
}



/*
   Interrupt service routines
 */


/* UART tx complete interrupt service routine. */
ISR (USART_TX_vect)
{
  /* Nothing to do.  */
}


/* UART receive complete interrupt service routine.  Note that we
   always listen on the bus and thus also receive our own transmits.
   This is useful so that we can easily detect collisions.  */
ISR (USART_RX_vect)
{
  static byte sentinel;
  static byte receiving;
  static byte idx;
  static byte escape;
  static uint16_t crc;
  byte c;

  c = UDR0;

  if (sentinel)
    return;
  sentinel = 1;
  sei ();

  if (c == FRAMESYNCBYTE)
    {
      if (receiving)
        {
          /* Received sync byte while receiving - either a collision
             or the message was too short and the next frame started.  */
          LED_Collision |= _BV(LED_Collision_BIT);
          receiving = 0;
          collision_count++;
          if (check_sending)
            check_sending = 2;
        }
      else
        {
          receiving = 1;
          idx = escape = 0;
        }
    }
  else if (!receiving)
    ; /* No sync seen, thus skip this octet.  */
  else if (rx_ready && !check_sending)
    {
      /* Overflow.  The previous message has not yet been processed.  */
      receiving = 0;
      overflow_count++;
    }
  else if (c == FRAMEESCBYTE && !escape)
    escape = 1;
  else
    {
      if (escape)
        {
          escape = 0;
          c ^= FRAMEESCMASK;
        }

      if (check_sending)
        {
          if (idx < MSGSIZE)
            {
              if (tx_buffer[idx++] != c)
                {
                  LED_Collision |= _BV(LED_Collision_BIT);
                  receiving = 0;
                  collision_count++;
                  check_sending = 2;  /* Tell the tx code.  */
                  idx = 0;
                }
            }
          else if (idx == MSGSIZE)
            {
              crc = c << 8;
              idx++;
            }
          else /* idx == MSGSIZE + 1 */
            {
              crc |= c;
              if (crc != tx_buffer_crc)
                {
                  LED_Collision |= _BV(LED_Collision_BIT);
                  receiving = 0;
                  collision_count++;
                  check_sending = 2;  /* Tell the tx code.  */
                  idx = 0;
                }
              else if (check_sending == 1)
                {
                  check_sending = 0; /* All checked. */
                  receiving = 0;
                }
            }
        }
      else if (idx == 0 && (c & PROTOCOL_MSGLEN_MASK) != PROTOCOL_MSGLEN_16)
        {
          /* Protocol length mismatch - ignore this message.  */
          /* Switch a lit collision LED off. */
          LED_Collision &= ~_BV(LED_Collision_BIT);
          /* Prepare for the next frame.  */
          receiving = 0;
        }
      else if (idx < MSGSIZE)
        rx_buffer[idx++] = c;
      else if (idx == MSGSIZE)
        {
          crc = c << 8;
          idx++;
        }
      else /* idx == MSGSIZE + 1 */
        {
          crc |= c;
          if (crc != compute_crc (rx_buffer))
            {
              LED_Collision |= _BV(LED_Collision_BIT);
              collision_count++;
              if (check_sending)
                check_sending = 2;
            }
          else
            {
              frames_received++;
              /* Switch a lit collision LED off. */
              LED_Collision &= ~_BV(LED_Collision_BIT);
              /* Tell the mainloop that there is something to process.  */
              rx_ready = 1;
              wakeup_main = 1;
            }
          /* Prepare for the next frame.  */
          receiving = 0;
        }
    }

  sentinel = 0;
}


/* Pin Change Interrupt Request 2 handler.  */
ISR (PCINT2_vect)
{
  rx_pin_level_change = 1;
  reset_retry_timer ();
  rx_pin_level_change = 0;
}


/* Send out the raw byte C.  */
static void
send_byte_raw (const byte c)
{
  /* Wait until transmit buffer is empty.  */
  while (!bit_is_set (UCSR0A, UDRE0))
    ;
  /* Send the byte.  */
  UDR0 = c;
}


/* Send byte C with byte stuffing.  */
static void
send_byte (const byte c)
{
  if (c == FRAMESYNCBYTE || c == FRAMEESCBYTE)
    {
      send_byte_raw (FRAMEESCBYTE);
      send_byte_raw ((c ^ FRAMEESCMASK));
    }
  else
    send_byte_raw (c);
}


static int
bus_idle_p (void)
{
  rx_pin_level_change = 0;

  reset_retry_timer ();

  PCMSK2  = _BV(PCINT16);  /* We only want to use this pin.  */
  PCICR  |= _BV(PCIE2);

  wait_t_c_reached ();

  PCMSK2 &= ~_BV(PCINT16);
  PCICR  &= ~_BV(PCIE2);

  return !rx_pin_level_change;
}


static void
wait_bus_idle (void)
{
  while (!bus_idle_p ())
    ;
}


/* Send a message.  This function does the framing and and retries
   until the message has been sent.  */
void
csma_send_message (const byte *data, byte datalen)
{
  byte idx;
  byte resend = 0;
  byte backoff = 0;

  memcpy ((byte*)tx_buffer, data, datalen);
  if (datalen < MSGSIZE)
    memset ((byte*)tx_buffer+datalen, 0, MSGSIZE - datalen);

  tx_buffer_crc = compute_crc (tx_buffer);

  do
    {
      if (resend)
        {
          int nloops;

          /* Exponential backoff up to 32.  For the first two resents
             we randomly use one or two wait loops; the 3rd and 4th
             time 1 to 4, then 1 to 8, then 1 to 16 and finally stick
             to 1 to 32. */
          if (backoff < 8)
            backoff++;

          do
            {
              wait_bus_idle ();
              nloops = (rand () % (1 << backoff/2)) + 1;

              while (nloops--)
                {
                  reset_retry_timer ();
                  wait_t_c_reached ();
                }
            }
          while (!bus_idle_p ());

          /* Switch a lit collision LED off.  We do this here to give
             a feedback on the used delay.  */
          LED_Collision &= ~_BV(LED_Collision_BIT);

          resend = 0;
        }
      else
        wait_bus_idle ();

      check_sending = 1;

      /* Switch TX LED on.  */
      LED_Transmit |= _BV(LED_Transmit_BIT);

      /* Before sending we need to clear the TXC bit by setting it.  */
      UCSR0A |= _BV(TXC0);

      /* Enable the LT1785 driver output (DE).  */
      PORTD |= _BV(2);

      send_byte_raw (FRAMESYNCBYTE);
      for (idx=0; idx < MSGSIZE; idx++)
        {
          send_byte (tx_buffer[idx]);
          if (check_sending == 2)
            {
              /* Collision detected - stop sending as soon as
                 possible.  We even disable the driver output right
                 now so that we won't clobber the bus any further with
                 data in the tx queue.  */
              PORTD &= ~_BV(2);  /* Disable LT1785 driver output.  */
              resend++;
              break;
            }
        }

      if (!resend)
        {
          send_byte ((tx_buffer_crc >> 8));
          send_byte (tx_buffer_crc);
        }

      /* Wait until transmit is complete.  */
      while (!bit_is_set (UCSR0A, TXC0))
        ;
      UCSR0A |= _BV(TXC0);

      /* Now disable the LT1785 driver output (DE).  It is important
         to do that as soon as possible.  */
      PORTD &= ~_BV(2);

      /* Wait until the receiver received and checked all our octets.  */
      if (check_sending == 1)
        {
          byte tccount = 0;

          reset_retry_timer ();
          while (check_sending == 1)
            {
              set_sleep_mode (SLEEP_MODE_IDLE);
              cli();
              if (t_c_reached_p ())
                {
                  if (++tccount > 3)
                    {
                      /* Nothing received for 4*Tc - assume receiver
                         is clogged due to collisions.  */
                      check_sending = 2;
                    }
                  else
                    reset_retry_timer ();
                }
              else if (check_sending == 1)
                {
                  sleep_enable ();
                  sei ();
                  sleep_cpu ();
                  sleep_disable ();
                }
              sei ();
            }
        }
      if (check_sending == 2)
        resend++;

      check_sending = 0;

      /* Switch TX LED off.  */
      LED_Transmit &= ~_BV(LED_Transmit_BIT);

    }
  while (resend);

  /* Switch a lit collision LED off. */
  LED_Collision &= ~_BV(LED_Collision_BIT);

  frames_sent++;
}


/* Return a message or NULL if none available.  The caller must call
   csma_message_done after a message has been processed.  */
byte *
csma_get_message (void)
{
  if (rx_ready)
    return (byte*)rx_buffer;
  return NULL;
}


void
csma_message_done (void)
{
  rx_ready = 0;
}


unsigned int
csma_get_stats (int what)
{
  switch (what)
    {
    case 1: return frames_received;
    case 2: return frames_sent;
    case 3: return collision_count;
    case 4: return overflow_count;
    default: return 0;
    }
}



/* Initialize the CSMA code.  This must be done after the
   initialization of the general hardware code.  */
void
csma_setup (void)
{
  /* Timer 0: We use this timer to measure the time for one octet on
     the wire.  */
  TCCR0A = 0x00;   /* Select normal mode.  */
#if T_x_PSCALE == 256
  TCCR0B = 0x04;   /* Set prescaler to clk/256.  */
#else
  TCCR0B = 0x03;   /* Set prescaler to clk/64.  */
#endif
  TIMSK0 = 0x00;   /* No interrupts so that we need to clear the TIFR0
                      flags ourself.  */
  OCR0A  = T_c_CMPVAL; /* Use this for Tc.  */
  OCR0B  = 0;
}
