/* testnode.c - Elektor Bus test Node
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

/* This node continuously sends messages with status information.  S2
   and S3 are used to change the send interval.  */

#include "hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

#include <util/crc16.h>

#include "ebus.h"
#include "proto-dbgmsg.h"


static volatile unsigned short send_interval;

static volatile byte send_flag;



/* This code is called by the 1ms ticker interrupt service routine
   with the current clock value given in milliseconds from 0..999. */
void
ticker_bottom (unsigned int clock)
{

  /* Run the main loop every N ms.  */
  if (send_interval && !(clock % send_interval))
    {
      send_flag = 1;
      wakeup_main = 1;
    }

  if (read_key_s2 ())
    {
      switch (send_interval)
        {
        case 1000:
          /* Disable sending but send one more frame to show the
             new sending interval.  */
          send_interval = 0;
          send_flag = 1;
          wakeup_main = 1;
          break;
        case  500: send_interval = 1000; break;
        case  250: send_interval =  500; break;
        case  125: send_interval =  250; break;
        case  100: send_interval =  125; break;
        case   50: send_interval =  100; break;
        case   25: send_interval =   50; break;
        }
    }
  if (read_key_s3 ())
    {
      switch (send_interval)
        {
        case 0:    send_interval = 1000; break;
        case 1000: send_interval = 500; break;
        case  500: send_interval = 250; break;
        case  250: send_interval = 125; break;
        case  125: send_interval = 100; break;
        case  100: send_interval =  50; break;
        case   50: send_interval =  25; break;
        }
    }
}


static void
send_msg (void)
{
  byte msg[MSGSIZE];
  int idx = 0;
  unsigned int val;

  msg[idx++] = PROTOCOL_EBUS_TEST;
  msg[idx++] = 0;
  msg[idx++] = config.nodeid_lo;
  msg[idx++] = 0;
  msg[idx++] = (val = get_current_time ()) >> 8;
  msg[idx++] = val;
  msg[idx++] = (val = csma_get_stats (1)) >> 8;
  msg[idx++] = val;
  msg[idx++] = (val = csma_get_stats (2)) >> 8;
  msg[idx++] = val;
  msg[idx++] = (val = csma_get_stats (3)) >> 8;
  msg[idx++] = val;
  msg[idx++] = (val = csma_get_stats (4)) >> 8;
  msg[idx++] = val;
  msg[idx++] = send_interval >> 8;
  msg[idx++] = send_interval;

  csma_send_message (msg, MSGSIZE);
}


static void
send_dbgmsg (const char *string)
{
  byte msg[16];

  msg[0] = PROTOCOL_EBUS_DBGMSG;
  msg[1] = config.nodeid_hi;
  msg[2] = config.nodeid_lo;
  memset (msg+3, 0, 13);
  strncpy (msg+3, string, 13);
  csma_send_message (msg, 16);
}

static void
send_dbgmsg_fmt (const char *format, ...)
{
  va_list arg_ptr;
  char buffer[16];

  va_start (arg_ptr, format);
  vsnprintf (buffer, 16, format, arg_ptr);
  va_end (arg_ptr);
  send_dbgmsg (buffer);
}



/* A new message has been received and we must now parse the message
   quickly and see what to do.  We need to return as soon as possible,
   so that the caller may re-enable the receiver.  */
static void
process_ebus_test (void)
{
  /* Is this a broadcast or is it directed to us.  If not we don't
     need to care about it.  */

}



/*
    Entry point
 */
int
main (void)
{
  byte *msg;
  hardware_setup (NODETYPE_UNDEFINED);
  csma_setup ();
  onewire_setup ();

  send_interval = 1000;

  sei (); /* Enable interrupts.  */


  /* for (;;) */
  /*   { */
  /*     byte i, crc, buf[9]; */

  /*     send_dbgmsg_fmt ("serial..."); */
  /*     onewire_enable (); */
  /*     onewire_write_byte (0x33); /\* Read ROM.  *\/ */
  /*     _delay_ms (50); */
  /*     onewire_enable (); */
  /*     for (i=0; i < 8; i++) */
  /*       buf[i] = onewire_read_byte (); */
  /*     _delay_ms (1); */
  /*     crc = 0; */
  /*     for (i=0; i < 7; i++) */
  /*       crc = _crc_ibutton_update (crc, buf[i]); */

  /*     send_dbgmsg_fmt ("%02x %02x%02x%02x %02x", */
  /*                      buf[0], buf[1], buf[2], buf[3], buf[7]); */
  /*     send_dbgmsg_fmt ("%02x%02x%02x %s", */
  /*                      buf[4], buf[5], buf[6], */
  /*                      buf[7] == crc? "ok":"bad"); */

  /*     _delay_ms (2000); */
  /*   } */

  for (;;)
    {
      byte i, crc, buf[9];

      onewire_enable ();
      onewire_write_byte (0xcc); /* Skip ROM.  */
      onewire_write_byte (0x44); /* Convert T.  */
      /* onewire_wait_for_one (); */
      _delay_ms (900);
      onewire_enable ();         /* Reset */
      onewire_write_byte (0xcc); /* Skip ROM.  */
      onewire_write_byte (0xbe); /* Read scratchpad.  */
      for (i=0; i < 9; i++)
        buf[i] = onewire_read_byte ();

      crc = 0;
      for (i=0; i < 8; i++)
        crc = _crc_ibutton_update (crc, buf[i]);

      if (buf[8] == crc)
        {
          int16_t tread = (buf[1] << 8) | buf[0];
          int16_t t;

          t = (tread*100 - 25 + ((16 - buf[6])*100 / 16)) / 20;

          send_dbgmsg_fmt ("t=%d (%d)", t, tread);
        }
      else
        send_dbgmsg_fmt ("badcrc");

      onewire_disable ();
      _delay_ms (1000);
    }



  for (;;)
    {
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
          switch ((msg[0] & PROTOCOL_TYPE_MASK))
            {
            case PROTOCOL_EBUS_TEST:
              process_ebus_test ();
              break;
            default:
              /* Ignore all other protocols.  */
              break;
            }
          /* Re-enable the receiver.  */
          csma_message_done ();
        }

      if (send_flag)
        {
          send_msg ();
          send_flag = 0;
        }
    }
}
