/* onewire.c - 1-Wire (iButton) master implementation for AVR
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
#include <string.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/crc16.h>

#include "ebus.h"

#define DBG_ONEWIRE 1

static int
write_reset (void)
{
  /* Drive low for 480us.  */
  OW_Bus_PORT &= ~_BV(OW_Bus_BIT);
  _delay_us (480);
  /* Drive high via pull-up for 480us.  */
  OW_Bus_PORT |= _BV(OW_Bus_BIT);  /* Enable pull-up.  */
  OW_Bus_DDR  &= ~_BV(OW_Bus_BIT); /* Configure as input.  */
  _delay_us (480);
  OW_Bus_DDR  |= _BV(OW_Bus_BIT);  /* Configure as output.  */
  return 0;
}


static void
write_one (void)
{
  /* Drive low for 10us (1 <= T_low1 < 15).  */
  OW_Bus_PORT &= ~_BV(OW_Bus_BIT);
  _delay_us (10);
  /* Drive high for 80us (60us <= T_slot < 120us) 60+(120-60)/2-T_low1.  */
  OW_Bus_PORT |= _BV(OW_Bus_BIT);
  _delay_us (80);
  /* Note that this includes enough time for recovery.  */
}

static void
write_zero (void)
{
  /* Drive low for 90us (60 <= T_low0 < 120).  */
  OW_Bus_PORT &= ~_BV(OW_Bus_BIT);
  _delay_us (90);
  /* Drive high again.  */
  OW_Bus_PORT |= _BV(OW_Bus_BIT);
}


static uint8_t
read_bit (void)
{
  uint8_t c;

  /* Drive low for at least 1us; we use 2us.  */
  OW_Bus_PORT &= ~_BV(OW_Bus_BIT);
  _delay_us (2);
  /* Drive high via pull-up to that the slave may pull it down.  */
  OW_Bus_PORT |= _BV(OW_Bus_BIT);  /* Enable pull-up.  */
  OW_Bus_DDR  &= ~_BV(OW_Bus_BIT); /* Configure as input.  */
  _delay_us (10);
  c = !!bit_is_set (OW_Bus_PIN, OW_Bus_BIT);
  _delay_us (78);  /* This makes the slot 90us.  */
  OW_Bus_DDR  |= _BV(OW_Bus_BIT);  /* Configure as output.  */

  return c;
}


/* Power up the bus if not done and reset the bus.  */
void
onewire_enable (void)
{
  OW_Bus_PORT |= _BV(OW_Bus_BIT);  /* Set high.  */
  OW_Bus_DDR  |= _BV(OW_Bus_BIT);  /* Configure as output.  */
  write_reset ();
}


/* Power down the bus etc.  */
void
onewire_disable (void)
{


}


void
onewire_write_byte (uint8_t c)
{
  uint8_t i;
  uint8_t mask;

#ifdef DBG_ONEWIRE
  OW_Bus_PORT |= _BV(1);
#endif
  for (mask=1, i=0; i < 8; mask <<= 1, i++)
    if ((c & mask))
      write_one ();
    else
      write_zero ();
  _delay_us (20);
#ifdef DBG_ONEWIRE
  OW_Bus_PORT &= ~_BV(1);
#endif
}


uint8_t
onewire_read_byte (void)
{
  uint8_t c, i;
  uint8_t mask;

#ifdef DBG_ONEWIRE
  OW_Bus_PORT |= _BV(1);
#endif
  c = 0;
  for (mask=1, i=0; i < 8; mask <<= 1, i++)
    if (read_bit ())
      c |= mask;

#ifdef DBG_ONEWIRE
  OW_Bus_PORT &= ~_BV(1);
#endif

  return c;
}


void
onewire_wait_for_one (void)
{
  while (!read_bit ())
    ;
}




/* Initialize the 1-Wire code.  This must be done after the
   initialization of the general hardware code.  Note that this will
   only prepare the machinery, for real use onewire_enable needs to be
   called before a real action.  */
void
onewire_setup (void)
{
  /* Internal pullup needs to be enabled.  Startup as output.  */
  OW_Bus_PORT |= _BV(OW_Bus_BIT);  /* Enable pull-up.  */
  OW_Bus_DDR  |= _BV(OW_Bus_BIT);

#ifdef DBG_ONEWIRE
  OW_Bus_PORT &= ~_BV(1);
  OW_Bus_DDR  |= _BV(1);
#endif
}
