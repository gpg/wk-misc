/* doorbell.c - Elektor Bus node to control a a doorbell
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

/* This node is used to control the doorbell and to display the
   current time and temperature.  A PCF8574 I2C bus expander is used
   to drive an ST7036 controlled LCD display in 4 bit mode.  */

#include "hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>


#include "ebus.h"
#include "proto-busctl.h"
#include "proto-h61.h"


/* This code is called by the 1ms ticker interrupt service routine
   with the current clock value given in milliseconds from 0..9999. */
void
ticker_bottom (unsigned int clock)
{
  /* Every 10 seconds send out a temperature reading.  */
  if (!clock)
    {
      wakeup_main = 1;
    }
}


/* A new message has been received and we must now parse the message
   quickly and see what to do.  We need to return as soon as possible,
   so that the caller may re-enable the receiver.  */
static void
process_ebus_h61 (byte *msg)
{
  char is_response = !!(msg[5] & P_H61_RESPMASK);

  if (!(msg[1] == config.nodeid_hi || msg[2] == config.nodeid_lo))
    return; /* Not addressed to us.  */

  switch ((msg[5] & ~P_H61_RESPMASK))
    {
    default:
      break;
    }
}


/* Process busctl messages.  */
static void
process_ebus_busctl (byte *msg)
{
  uint16_t val16;
  byte     val8;
  char is_response = !!(msg[5] & P_BUSCTL_RESPMASK);

  if (is_response)
    return;  /* Nothing to do.  */
  else if (msg[3] == 0xff || msg[4] == 0xff || msg[4] == 0)
    return ; /* Bad sender address.  */
  else if (msg[1] == config.nodeid_hi && msg[2] == config.nodeid_lo)
    ; /* Directed to us.  */
  else if ((msg[1] == config.nodeid_hi || msg[1] == 0xff) && msg[2] == 0xff)
    ; /* Broadcast. */
  else
    return; /* Not addressed to us.  */

  switch ((msg[5] & ~P_BUSCTL_RESPMASK))
    {
    case P_BUSCTL_TIME:
      /* Fixme: Implement */
      break;

    case P_BUSCTL_QRY_TIME:
      msg[1] = msg[3];
      msg[2] = msg[4];
      msg[3] = config.nodeid_hi;
      msg[4] = config.nodeid_lo;
      msg[5] |= P_BUSCTL_RESPMASK;
      msg[6] = 0;
      val16 = get_current_fulltime (&val8);
      msg[7] = val16 >> 8;
      msg[8] = val16;
      msg[9] = val8;
      memset (msg+10, 0, 6);
      csma_send_message (msg, MSGSIZE);
      break;

    default:
      break;
    }
}


/*
    Entry point
 */
int
main (void)
{
  byte *msg;

  hardware_setup (NODETYPE_DOORBELL);

  csma_setup ();
  onewire_setup ();
  i2c_setup ();
  lcd_setup ();

  sei (); /* Enable interrupts.  */

  lcd_init ();
  for (;;)
    {
      for (;;)
        {
          if (read_key_s2 ())
            lcd_backlight (1);
          else if (read_key_s3 ())
            lcd_backlight (0);
          lcd_home ();
          lcd_putc ('a');
          LED_Collision |= _BV(LED_Collision_BIT);
          _delay_ms (100);
          LED_Collision &= ~_BV(LED_Collision_BIT);
          _delay_ms (100);
          lcd_puts_P ("Hello World!");
          LED_Collision |= _BV(LED_Collision_BIT);
          _delay_ms (100);
          LED_Collision &= ~_BV(LED_Collision_BIT);
        }

      set_sleep_mode (SLEEP_MODE_IDLE);
      while (!wakeup_main)
        {
          cli();
          if (!wakeup_main)
            {
              sleep_enable ();
              sei ();
              sleep_cpu ();
              sleep_disable ();
            }
          sei ();
        }
      wakeup_main = 0;

      msg = csma_get_message ();
      if (msg)
        {
          /* Process the message.  */
          switch (msg[0])
            {
            case PROTOCOL_EBUS_BUSCTL:
              process_ebus_busctl (msg);
              break;
            case PROTOCOL_EBUS_H61:
              process_ebus_h61 (msg);
              break;
            default:
              /* Ignore all other protocols.  */
              break;
            }
          /* Re-enable the receiver.  */
          csma_message_done ();
        }

    }

}
