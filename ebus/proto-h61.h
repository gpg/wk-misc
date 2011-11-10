/* proto-h61.h - Definition of the H61 protocol.
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

#ifndef PROTO_H61_H
#define PROTO_H61_H

/* Description of the H/61 protocol.

   This protocol is used for domotik control of a detached house.  The
   message is similar to the

   byte  0 - protocol PROTOCOL_EBUS_H61 (0x86)
   byte  1 - receiver node-id high
   byte  2 - receiver node-id low
   byte  3 - sender   node-id high
   byte  4 - sender   node-id low
   byte  5 - command, see below
   byte  6...15 - command dependent

   List of Commands.  Bit 7 is the RESPONSE bit and used for
   corrsponding response messages.  The command itself is given by
   bits 6 to 0:

* 0x10 := Shutter control:

  byte 6 - Subcommand for the shutter control:

** 0x00 - Query shutter state

   byte 7..15 - rfu must be 0.

*** Response message

   byte  7 - error flag (0 for okay).  Only set on response from the
             drive shutter subcommand.
   byte  8 - Shutter 1 state:
             bit 7 - Currently driving
             bit 6 - Currently pulling up
             bit 5 - State in bits 3...0 is valid.
             bit 4 - rfu
             bit 3..0 - Percentage closed
                       (0 = open, 31 = 100% closed)
   byte  9 - Shutter 2 state
   byte 10 - Shutter 3 state
   byte 11 - Shutter 4 state
   byte 12 - Shutter 5 state
   byte 13 - Shutter 6 state
   byte 14 - Shutter 7 state
   byte 15 - Shutter 8 state

** 0x01 - Drive shutter

   byte 7 - Shutter number to drive
            (0 = all shutters controlled by node)
   byte 8 - Shutter control byte
            bit 7 - Drive
            bit 6 - Direction is "up".
            bit 5 - Drive to percentage given in bits 3..0.
                    (Bit 6 must be zero.)
            bit 4 - rfu
            bit 3..0 = Drive to this closed state
                       (0 = open, 15 = 100% closed)

            Undefined values will do nothing.
   byte  9..15 - rfu


*** Response message

    The response is the same as the response from the query shutter
    state subcommand.  If there was an error in the command block the
    error flag in the response block will be set.  However the
    subcommand in the response message is 0x01 and not 0x00.

** 0x02 - Query shutter timings

   The time required to pull the drive up or down is stored in the
   EEPROM.  This command allows to read out the value.

   byte 7 - Shutter number
   byte 8..15 - rfu, must be 0.

*** Response message

   byte 7 - Shutter number
   byte 8 - Time for a complete pull up in seconds.
   byte 9 - Time for a complete pull down in seconds.
   byte 10..15 - rfu

** 0x03 - Update shutter timings

   The time required to pull the drive up or down is stored in th
   EEPROM.  This command allows an easy adjustment.

   byte 7 - Shutter number
   byte 8 - Time for a complete pull up in seconds.
   byte 9 - Time for a complete pull down in seconds.
   byte 10..15 - rfu, must be 0

*** Response message

    The response message is the same as Query Shutter timings.

*** 0x04 - Query Shutter Schedule

    byte 7 - Shutter number

*** Response message

   The device answers with multiple response messages returning all
   stored schedules.

   byte 7 - Shutter number
   byte 8 - Error flag (only used with ChangeShutterSchedule)
   byte 9 - Number of items
   byte 10 - Item number
   byte 11,12 - Time
   byte 13    - New state (cf. Drive Shutter)
   byte 14..15 - rfu, must be 0

** 0x05 - Update Shutter Schedule

   byte 7 - Shutter number
   byte 8 - rfu, must be 0
   byte 9 - constant 1.
   byte 10 - Item number
   byte 11,12 - Time
   byte 13    - Action (cf. Drive Shutter)

   byte 14..15 - rfu, must be 0

   After one message old messages are destroyed.  On error the device
   may or may not use the old values.  Using an undefined action may
   be used to delete an entry.

   A factory reset of the schedule may be done using these parameters:
   Shutter number: 0xf0
   Byte 9: 16
   Item number: 0xf0
   time = 0xf0f0
   action = 0xf0

*** Response message

   There is no response message.  It is suggested to use Query Shutter
   State to check that the settings are correct.

*/

#include "protocol.h"

#define P_H61_RESPMASK 0x80  /* The response mask for the commands.  */
#define P_H61_SHUTTER  0x10  /* ShutterControl.  */
#define P_H61_ADCREAD  0x21  /* Read analog sensor.  */


/* Subcommands of P_H61_SHUTTER.  */
#define P_H61_SHUTTER_QUERY        0x00
#define P_H61_SHUTTER_DRIVE        0x01
#define P_H61_SHUTTER_QRY_TIMINGS  0x02
#define P_H61_SHUTTER_UPD_TIMINGS  0x03
#define P_H61_SHUTTER_QRY_SCHEDULE 0x04
#define P_H61_SHUTTER_UPD_SCHEDULE 0x05



#endif /*PROTO_H61_H*/
