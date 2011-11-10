/* i2c.c - I2C implementation for AVR
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

/*
   Note that Atmel's term for I2C is TWI which stands for Two-Wire
   interface.
 */

#include "hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#include "ebus.h"

/* Send the START condition.  Returns 0 on success.  */
static byte
send_start (void)
{
 retry:
  /* Clear the interrupt flag, set send_staart and enable TWI operation.  */
  TWCR = _BV (TWINT) | _BV (TWSTA) | _BV (TWEN);
  /* Wait for completion.  */
  while (!(TWCR & _BV (TWINT)))
    ;
  /* Check the status value.  */
  switch (TWSR & 0xF8)
    {
    case 0x08:  /* Start condition has been transmitted.  */
    case 0x10:  /* Re-start condition has been transmitted.  */
      break;
    case 0x38:  /* Arbitration lost.  */
      goto retry;
    default:    /* Error or unexpected status code.  */
      return 1;
    }
  return 0;
}


/* Send the STOP condition.  */
static void
send_stop (void)
{
  /* Clear the interrupt flag, set send_stop and enable TWI operation.  */
  TWCR = _BV (TWINT) | _BV (TWSTO) | _BV (TWEN);
  /* The MCU won't set TWINT after after sending the stop; thus we may
     not wait for it.  */
}


/* Send the slave ADDRESS.  Returns 0 on success, 1 on general error,
   2 to request a restart.  */
static byte
send_sla (byte address)
{
  /* Load address into the data register.  */
  TWDR = address;
  /* Clear the interrupt flag and enable TWI operation.  */
  TWCR = _BV (TWINT) | _BV (TWEN);
  /* Wait for completion.  */
  while (!(TWCR & _BV (TWINT)))
    ;
  /* Check the status value.  */
  switch (TWSR & 0xF8)
    {
    case 0x18:  /* SLA+W has been transmitted.  */
      break;
    case 0x20:  /* NACK received.  */
      LED_Collision |= _BV(LED_Collision_BIT);
      return 2;
    case 0x38:  /* Arbitration lost.  */
      return 2;
    default:    /* Unexpected status code.  */
      return 1;
    }
  return 0;
}


/* Send a data byte.  Returns 0 on success, 1 on general error, 2 on
   requesting a restart, 3 on receiving a NACK.  */
static byte
send_data (byte data)
{
  /* Load data into the data register.  */
  TWDR = data;
  /* Clear the interrupt flag and enable TWI operation.  */
  TWCR = _BV (TWINT) | _BV (TWEN);
  /* Wait for completion.  */
  while (!(TWCR & _BV (TWINT)))
    ;
  /* Check the status value.  */
  switch (TWSR & 0xF8)
    {
    case 0x28:  /* Data has been transmitted.  */
      break;
    case 0x30:  /* NACK received.  */
      return 3;
    case 0x38:  /* Arbitration lost.  */
      return 2;
    default:    /* Unexpected status code.  */
      return 1;
    }
  return 0;
}


/* Start in master transmitter mode.  ADDRESS is the slave address in
   the range 0 to 254; the low bit is ignored but should be passed as
   zero.  Returns 0 on success, 1 on general error, 2 for a bad
   address.  */
byte
i2c_start_mt (byte address)
{
  /* We need to send an SLA+W; thus clear the LSB.  */
  address &= 0xf7;

  if (send_start ())
    {
      LED_Transmit |= _BV(LED_Transmit_BIT);
      return 1;
    }
  switch (send_sla (address))
    {
    case 0:
      break;
    case 2:
      return 2; /* NACK on SLA - assume bad address.  */
    default:
      return 1;
    }
  return 0;
}

/* Send byte VALUE.  Returns 1 if successfully sent; 0 on error.  */
byte
i2c_send_byte (byte value)
{
  if (send_data (value))
    return 0;
  return 1;
}


/* Send DATALEN bytes of DATA.  Returns number of bytes successfully
   sent.  */
byte
i2c_send_data (byte *data, byte datalen)
{
  byte idx;

  for (idx=0; idx < datalen; idx++)
    {
      if (send_data (data[idx]))
        return idx;
    }
  return idx;
}



void
i2c_stop (void)
{
  send_stop ();
}



/* Initialize the I2C code.  This must be done after the
   initialization of the general hardware code.  Note that using I2C
   switches port C pins 4 and 5 to their alternate functions: PC5 is
   SCL and PC4 is SCD.  */
void
i2c_setup (void)
{
  /* We use external pull-ups thus disable the internal pull-ups. */
  PORTC &= ~(_BV(4) | _BV(5));

  /* Set the clock close to 100kHz:
       F_scl = F_cpu / (16 + 2 * TWBR * Prescaler)
       |    F_cpu | pre | TWBR | F_scl |
       |----------+-----+------+-------|
       | 16000000 |   1 |   73 | 98765 |
       #+TBLFM: $4=$1 / (16 + (2 * $3 * $2));%.0f

     Note that the prescaler is controlled by bits 1 and 0 of the
     TWSR; bits 7 to 3 make up the TWI status register. */
#if F_CPU != 16000000ul
# error Please adjust the bit rate register.
#endif
  TWSR = 0;  /* Prescaler to 1.  */
  TWBR = 73; /* Bit rate register to 73.  */

}
