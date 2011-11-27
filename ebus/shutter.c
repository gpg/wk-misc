/* shutter.c - Elektor Bus node to control a shutter
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

/* This node is used to control the shutter in a living room.  The
   shutter is operator by a motor with two coils and limit switches
   connected to a solid state relays which is controlled by this node.
   A future version of this controller will also support some sensor
   to allow pulling down the shutter only to some percentage.  In any
   case the limit switches of the motor control the endpoints.  The
   control logic takes down the relays after the time required to pull
   the shutters up or down plus some extra time.  The S2 and S3
   switches are used for manually controlling the shutter.  They are
   interlocked and operate in a toggle on/off way.  The hardware
   itself is also interlocked so that it is not possible to drive both
   motors at the same time.

   The used relays are FINDER 34.51.7.012.0010 which feature a high
   sensitive coil (~18mA) and are able to switch 6A (the datasheet
   even exlicitly allows motors of up to 185W).  The first relay is
   used for direction selection with NC connected to the pull-up motor
   and NO to the pull-down motor.  Our control logic makes sure that
   this relay is not switched under load.  The second one connects its
   NO to the first relay and a snubber circuit is used to protect its
   contacts.  They are both connected to the 12V rail and flyback
   diodes are used for each.  Two BC547 are used to drive them.

   Historical note: A first version of the shutter rig and this
   software used two (not interlocked) solid state relays made up from
   S202S02 opto-triacs along with snubbers and varistors to cope with
   the voltage peaks induced by the other motor.  However the spikes
   were obviously too high and after some weeks the triacs bricked
   themself.  A second try using S202S01s had the same effect.  Better
   save your money and use mechanical relays.
 */

#include "hardware.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>

#include "ebus.h"
#include "proto-busctl.h"
#include "proto-h61.h"


#define MOTOR_on       (PORTC)  /* Switch motor on.  */
#define MOTOR_on_BIT   (PC2)
#define MOTOR_down     (PORTC)  /* Switch direction relay to down.  */
#define MOTOR_down_BIT (PC3)


#define SCHEDULE_ACTION_NOP   0 /* No operation.  */
#define SCHEDULE_ACTION_UP    1 /* Minute + 10s := pull up  */
#define SCHEDULE_ACTION_DOWN  5 /* Minute + 50s := pull down  */


/* Allowed action values.  Note that the hardware interlocks both
   motors.  However to avoid switch the direction under load we first
   set the direction, wait a moment and then switch the motor on.  */
enum __attribute__ ((packed)) action_values {
  action_none = 0,
  action_up,
  action_down
};

enum __attribute__ ((packed)) motor_state_values {
  motor_state_off = 0,
  motor_state_pre_off,
  motor_state_pre_off2,
  motor_state_pre_up,
  motor_state_pre_up2,
  motor_state_up,
  motor_state_up_ready,
  motor_state_pre_down,
  motor_state_pre_down2,
  motor_state_down,
  motor_state_down_ready
};

/* Remember the last action time.  */
static uint16_t schedule_last_tfound;

/* The next action to do.  It's value is changed by the commands
   and a ticker is switching the motor depending on these values.  */
static volatile enum action_values next_action;


/* The shutter state byte.  This is directly used for the
   QueryShutterState response message.  */
static volatile byte shutter_state;

/* Event flag, triggred (surprise) every 10 seconds.  */
static volatile byte ten_seconds_event;



/* Local prototypes.  */
static void init_eeprom (byte force);




/* This code is called by the 1ms ticker interrupt service routine
   with the current clock value given in milliseconds from 0..9999. */
void
ticker_bottom (unsigned int clock)
{
  static volatile enum action_values current_action;
  static volatile uint16_t action_delay;
  static volatile enum motor_state_values state = motor_state_off;
  enum action_values save_action;

  /* Blink the activity LED if the motor is not in off state.  */
  if (state != motor_state_off)
    {
      if (!(clock % 1000))
        {
          if ((LED_Collision & _BV(LED_Collision_BIT)))
            LED_Collision &= ~_BV (LED_Collision_BIT);
          else
            LED_Collision |= _BV(LED_Collision_BIT);
        }
    }

  /* Get next action.  */
  save_action = current_action;
  if (read_key_s2 ())
    current_action = current_action != action_down ? action_down : action_none;
  if (read_key_s3 ())
    current_action = current_action != action_up? action_up : action_none;
  if (current_action != save_action || next_action)
    {
      if (next_action)
        {
          current_action = next_action;
          next_action = 0;
        }
      switch (current_action)
        {
        case action_none: state = motor_state_pre_off; break;
        case action_up:   state = motor_state_pre_up; break;
        case action_down: state = motor_state_pre_down; break;
        }
      /* When a new action has been selected we need to limit the
         action delay to the minimum value so that we don't stick in
         the long down or up states.  We also need to make sure that
         there is an action delay so that we enter the state
         transition switch. */
      if (!action_delay)
        action_delay = 1;
      else if (action_delay > 200)
        action_delay = 200;
    }

  if (action_delay && !--action_delay)
    {
      switch (state)
        {
        case motor_state_off:
          LED_Collision &= ~_BV (LED_Collision_BIT); /* Clear LED.  */
          /* Make sure the motor flags are cleared in the state info.  */
          shutter_state &= 0b00111111;
          break;

        label_pre_off:
        case motor_state_pre_off:
          MOTOR_on &= ~_BV(MOTOR_on_BIT);    /* Switch motor off. */
          action_delay = 200; /*ms*/
          state = motor_state_pre_off2;
          break;
        case motor_state_pre_off2:
          MOTOR_down &= ~_BV(MOTOR_down_BIT);/* Switch direction relay off. */
          action_delay = 200; /*ms*/
          state = motor_state_off;
          break;

        case motor_state_pre_up:
          MOTOR_on &= ~_BV(MOTOR_on_BIT);    /* Switch motor off. */
          action_delay = 200; /*ms*/
          state = motor_state_pre_up2;
          break;
        case motor_state_pre_up2:
          MOTOR_down &= ~_BV(MOTOR_down_BIT);/* Switch direction relay off. */
          action_delay = 200; /*ms*/
          state = motor_state_up;
          break;
        case motor_state_up:
          MOTOR_on |= _BV(MOTOR_on_BIT);     /* Switch motor on. */
          shutter_state = 0b11000000;
          /*                |!------- Direction set to up
           *                +-------- Motor running.        */
          action_delay = 25000; /*ms*/
          state = motor_state_up_ready;
          break;
        case motor_state_up_ready:
          shutter_state = 0b00100000;
          /*                  | !~~!- Value: 0 = 0% closed
           *                  +------ State in bits 3..0 is valid.  */
          goto label_pre_off;

        case motor_state_pre_down:
          MOTOR_on &= ~_BV(MOTOR_on_BIT);    /* Switch motor off. */
          action_delay = 200; /*ms*/
          state = motor_state_pre_down2;
          break;
        case motor_state_pre_down2:
          MOTOR_down |= _BV(MOTOR_down_BIT); /* Switch direction relay on. */
          action_delay = 200; /*ms*/
          state = motor_state_down;
          break;
        case motor_state_down:
          MOTOR_on |= _BV(MOTOR_on_BIT);     /* Switch motor on. */
          shutter_state = 0b10000000;
          /*                |!------- Direction set to down
           *                +-------- Motor running.        */
          action_delay = 25000; /*ms*/
          state = motor_state_down_ready;
          break;
        case motor_state_down_ready:
          shutter_state = 0b00101111;
          /*                  | !~~!--- Value: 15 = 100% closed
           *                  +-------- State in bits 3..0 is valid.  */
          goto label_pre_off;
        }
    }

  if (!clock)
    {
      ten_seconds_event = 1;
      wakeup_main = 1;
    }
}


/* static void */
/* send_dbgmsg (const char *string) */
/* { */
/*   byte msg[16]; */

/*   msg[0] = PROTOCOL_EBUS_DBGMSG; */
/*   msg[1] = config.nodeid_hi; */
/*   msg[2] = config.nodeid_lo; */
/*   memset (msg+3, 0, 13); */
/*   strncpy (msg+3, string, 13); */
/*   csma_send_message (msg, 16); */
/* } */

/* static void */
/* send_dbgmsg_fmt (const char *format, ...) */
/* { */
/*   va_list arg_ptr; */
/*   char buffer[16]; */

/*   va_start (arg_ptr, format); */
/*   vsnprintf (buffer, 16, format, arg_ptr); */
/*   va_end (arg_ptr); */
/*   send_dbgmsg (buffer); */
/* } */


/* Process scheduled actions.  TIME is the current time.  If
   FORCED_TLOW is not 0 the scheduler will run the last action between
   FORCED_TLOW and TIME regardless on whether it has already been run.
   This feature is required to cope with a changed system time:
   Consider the shutter shall be closed at 19:00, the current system
   time is 18:59 and the system time is updated to 19:10 - without
   that feature the closing at 19:00 would get lost.  */
static void
process_schedule (uint16_t time, uint16_t forced_tlow)
{
  uint16_t tlow, thigh, t, tfound;
  byte i;

  if (!time_has_been_set)
    return; /* Don't schedule while running without a valid clock.  */

  if (schedule_last_tfound > time || forced_tlow)
    schedule_last_tfound = 0;  /* Time wrapped to the next week or forced
                                  schedule action.  */

  /* We look up to 5 minutes back into the past to cope with lost events.  */
  time /= 6;
  time *= 6;
  tlow = forced_tlow? forced_tlow : time;
  if (tlow >= 5 * 6)
    tlow -= 5 * 6;
  else
    tlow = 0;
  if (schedule_last_tfound && schedule_last_tfound > tlow)
    tlow = schedule_last_tfound;
  thigh = time + 5;

  /* send_dbgmsg_fmt ("time=%u", time); */
  /* send_dbgmsg_fmt ("lst=%u", schedule_last_tfound); */
  /* send_dbgmsg_fmt ("low=%u", tlow); */
  /* send_dbgmsg_fmt ("hig=%u", thigh); */

  /* Walk the schedule and find the last entry. */
  for (tfound=0, i=0; i < DIM (ee_data.u.shutterctl.schedule); i++)
    {
      t = eeprom_read_word (&ee_data.u.shutterctl.schedule[i]);
      if (!t)
        break;
      if (t > tlow && t <= thigh)
        tfound = t;
    }
  if (tfound)
    {
      schedule_last_tfound = tfound;
      /* send_dbgmsg_fmt ("fnd=%u", ((tfound/6)*6)); */
      tfound %= 6;
      /* send_dbgmsg_fmt ("act=%u", tfound); */
      if (tfound == SCHEDULE_ACTION_UP)
        next_action = action_up;
      else if (tfound == SCHEDULE_ACTION_DOWN)
        next_action = action_down;
    }
}


/* Process a shutter command.  */
static void
process_shutter_cmd (byte *msg)
{
  uint16_t val16;
  byte err = 0;

  switch (msg[6])
    {
    case P_H61_SHUTTER_QUERY:
      {
        msg[1] = msg[3];
        msg[2] = msg[4];
        msg[3] = config.nodeid_hi;
        msg[4] = config.nodeid_lo;
        msg[5] |= P_BUSCTL_RESPMASK;

        msg[7] = 0; /* No error.  */
        msg[8] = shutter_state;
        memset (msg+9, 0, 7);
        csma_send_message (msg, MSGSIZE);
      }
      break;

    case P_H61_SHUTTER_DRIVE:
      {
        if (msg[7] > 1 /* Only all shutters or shutter 1 are allowed.  */
            || msg[9] || msg[10] || msg[11] || msg[12] || msg[13]
            || msg[14] || msg[15] /* Reserved bytes are not zero.  */
            || (msg[8] & 0x10)    /* Reserved bit is set.  */
            || (msg[8] & 0x20)    /* Not yet supported.  */ )
          err = 1;
        else if ((msg[8] & 0xc0) == 0xc0)
          next_action = action_up;
        else if ((msg[8] & 0xc0) == 0x80)
          next_action = action_down;
        else
          err = 1;

        msg[1] = msg[3];
        msg[2] = msg[4];
        msg[3] = config.nodeid_hi;
        msg[4] = config.nodeid_lo;
        msg[5] |= P_BUSCTL_RESPMASK;
        msg[7] = err;
        msg[8] = shutter_state;
        memset (msg+9, 0, 7);
        csma_send_message (msg, MSGSIZE);
      }
      break;

    case P_H61_SHUTTER_QRY_TIMINGS:
      break;

    case P_H61_SHUTTER_UPD_TIMINGS:
      break;

    case P_H61_SHUTTER_QRY_SCHEDULE:
      {
        byte i;

        msg[1] = msg[3];
        msg[2] = msg[4];
        msg[3] = config.nodeid_hi;
        msg[4] = config.nodeid_lo;
        msg[5] |= P_BUSCTL_RESPMASK;
        msg[7] = 0; /* We only have a global schedule for now.  */
        msg[8] = 0; /* No error.  */
        for (i=0; i < DIM (ee_data.u.shutterctl.schedule); i++)
          {
            val16 = eeprom_read_word (&ee_data.u.shutterctl.schedule[i]);
            switch ((val16 % 6))
              {
              case SCHEDULE_ACTION_UP:   msg[13] = 0b11000000; break;
              case SCHEDULE_ACTION_DOWN: msg[13] = 0b10000000; break;
              default: msg[13] = 0;  break; /* Undefined.  */
              }
            val16 /= 6;
            val16 *= 6;
            msg[9] = DIM (ee_data.u.shutterctl.schedule);
            msg[10] = i;
            msg[11] = val16 >> 8;
            msg[12] = val16;
            /* msg[13] already set.  */
            msg[14] = 0;
            msg[15] = 0;
            csma_send_message (msg, MSGSIZE);
          }
      }
      break;

    case P_H61_SHUTTER_UPD_SCHEDULE:
      if (msg[8] || msg[14] || msg[15] || msg[9] != 1
          || msg[10] >= DIM (ee_data.u.shutterctl.schedule))
        {
          /* Bad message or eeprom reset  */
          if (msg[7] == 0xf0 && msg[9] == 16 && msg[10] == 0xf0
              && msg[11] == 0xf0 && msg[12] == 0xf0 && msg[13] == 0xf0)
            {
              init_eeprom (1);
            }
        }
      else
        {
          /* Get time and round down to the full minute.  */
          val16 = (msg[11] << 8) | msg[12];
          val16 /= 6;
          val16 *= 6;
          /* Include the action.  Note that SCHEDULE_ACTION_NOP is the
             default.  */
          if (msg[13] == 0b11000000)
            val16 += SCHEDULE_ACTION_UP;
          else if (msg[13] == 0b10000000)
            val16 += SCHEDULE_ACTION_DOWN;

          eeprom_write_word (&ee_data.u.shutterctl.schedule[msg[10]], val16);
        }
      break;

    default:
      break;
    }
}



/* Process a sensor command.  */
static void
process_sensor_cmd (byte *msg)
{
  uint16_t val16;
  byte err = 0;

  switch (msg[6])
    {
    case P_H61_SENSOR_TEMPERATURE:
      {
        msg[1] = msg[3];
        msg[2] = msg[4];
        msg[3] = config.nodeid_hi;
        msg[4] = config.nodeid_lo;
        msg[5] |= P_BUSCTL_RESPMASK;
        msg[7] = (1 << 4 | 1); /* Group 1 of 1.  */
        msg[8] = 0;
        msg[9] = 0;
        msg[10] = 0x80; /* No sensor.  */
        msg[11] = 0;
        msg[12] = 0x80;
        msg[13] = 0;
        msg[14] = 0x80;
        msg[15] = 0;
        memset (msg+10, 0, 6);
        csma_send_message (msg, MSGSIZE);
      }
      break;

    default:
      break;
    }
}


/* A new message has been received and we must now parse the message
   quickly and see what to do.  We need to return as soon as possible,
   so that the caller may re-enable the receiver.  */
static void
process_ebus_h61 (byte *msg)
{
  char is_response = !!(msg[5] & P_H61_RESPMASK);

  if (!(msg[1] == config.nodeid_hi && msg[2] == config.nodeid_lo))
    return; /* Not addressed to us.  */

  switch ((msg[5] & ~P_H61_RESPMASK))
    {
    case P_H61_SHUTTER:
      if (!is_response)
        process_shutter_cmd (msg);
      break;

    case P_H61_SENSOR:
      if (!is_response)
        process_sensor_cmd (msg);
      break;

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
      {
        uint16_t t;

        t = get_current_time ();
        val16 = (msg[7] << 8) | msg[8];
        val8  = (msg[6] & 0x02)? msg[9] : 0;
        set_current_fulltime (val16, val8);
        if (val16 > t)
          process_schedule (val16, t);
      }
      break;

    case P_BUSCTL_QRY_TIME:
      msg[1] = msg[3];
      msg[2] = msg[4];
      msg[3] = config.nodeid_hi;
      msg[4] = config.nodeid_lo;
      msg[5] |= P_BUSCTL_RESPMASK;
      msg[6] = 0; /* fixme: return an error for unknown shutter numbers.  */
      val16 = get_current_fulltime (&val8);
      msg[7] = val16 >> 8;
      msg[8] = val16;
      msg[9] = val8;
      memset (msg+10, 0, 6);
      csma_send_message (msg, MSGSIZE);
      break;

    case P_BUSCTL_QRY_VERSION:
      msg[1] = msg[3];
      msg[2] = msg[4];
      msg[3] = config.nodeid_hi;
      msg[4] = config.nodeid_lo;
      msg[5] |= P_BUSCTL_RESPMASK;
      msg[6] = eeprom_read_byte (&ee_data.nodetype);
      msg[7] = 0;
      memcpy_P (msg+8, PSTR (GIT_REVISION), 7);
      msg[15] = 0;
      csma_send_message (msg, MSGSIZE);
      break;

    default:
      break;
    }
}



/* Init our eeprom data if needed. */
static void
init_eeprom (byte force)
{
  uint16_t uptime, downtime;
  byte i;

  if (force || !eeprom_read_word (&ee_data.u.shutterctl.schedule[0]))
    {
      /* The schedule is empty - set up reasonable values for every
         day of the week.  */
      uptime = (7 * 60 + 30) * 6 + SCHEDULE_ACTION_UP;
      downtime = (18 * 60 + 15) * 6 + SCHEDULE_ACTION_DOWN;
      for (i=0; i < 7*2 && i < DIM (ee_data.u.shutterctl.schedule); i++)
        {
          if (i==6*2) /* Pull up on Sundays one hour later.  */
            uptime += 60 * 6;
          eeprom_write_word (&ee_data.u.shutterctl.schedule[i], uptime);
          i++;
          eeprom_write_word (&ee_data.u.shutterctl.schedule[i], downtime);
          uptime += 24 * 60 * 6;
          downtime += 24 * 60 * 6;
        }
      for (; i < DIM (ee_data.u.shutterctl.schedule); i++)
        eeprom_write_word (&ee_data.u.shutterctl.schedule[i], 0);
    }
}


/*
    Entry point
 */
int
main (void)
{
  byte *msg;

  hardware_setup (NODETYPE_SHUTTER);
  init_eeprom (0);

  /* Port C configuration changes.  Configure motor ports for output
     and switch them off. */
  PORTC &= ~(_BV(MOTOR_down_BIT) | _BV(MOTOR_on_BIT));
  DDRC  |= _BV(MOTOR_down_BIT) | _BV(MOTOR_on_BIT);

  csma_setup ();
  onewire_setup ();

  sei (); /* Enable interrupts.  */

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

      if (ten_seconds_event)
        {
          uint16_t t;

          ten_seconds_event = 0;

          t = get_current_time ();
          if (!(t % 6))
            {
              /* Code to run every minute.  */
              process_schedule (t, 0);
            }
        }


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
