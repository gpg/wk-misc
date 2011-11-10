/* proto-busctl.h - Definition of the BusControl protocol.
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

#ifndef PROTO_BUSCTL_H
#define PROTO_BUSCTL_H

/* Description of the BusControl protocol.

   This protocol is used on the ebus for generic tasks.  All messages
   use the common header:

   byte  0 - protocol PROTOCOL_EBUS_BUSCTL (0x81)
   byte  1 - receiver node-id high
   byte  2 - receiver node-id low
   byte  3 - sender   node-id high
   byte  4 - sender   node-id low
   byte  5 - command, see below
   byte  6...15 - command dependent

   A sender node-id high of 0 is valid and indicates that the sender
   is on the current bus segment.  A sender node-id low of 0 is
   reserved for a bus controller node (which is currently not
   defined).  A sender with node-id low or high set to 0xff is
   invalid.

   List of Commands.  Bit 7 is the RESPONSE bit and used for
   corresponding response messages.  The command itself is given by
   bits 6 to 0:

* 0x01 := Time Broadcast

  byte 6 - Control byte
          bit 2 - Daylight Saving Time active
          bit 1 - Decile seconds given.
          bit 0 - Exact time given.  If this bit is not set, the time
                  may be somewhat off due to collision retries.
  byte 7,8 - Number of 10 second periods passed this week.
  byte 9   - Decile seconds within this period.  Values are 0 to 99
             representing 0.0 to 9.9 seconds.
  byte 10..15 - rfu

  No reponses are defined or expected.

  Note that the usual operation is to broadcast the current time from
  a sender connected to an NTP server.  Thus it is common to see
  receiver node-ids of (0xff,0xff).

  The time on the bus is the local time.  This makes it easier to
  display and modify times.  This function shall be called by a master
  around the daylight switching hour to make sure the bus gets updated
  to the right time.

* 0x02 := Query Time

  byte 6..15 - rfu, must be 0.

  Response format:

  byte 6   - rfu
  byte 7,8 - Number of 10 second periods passed this week.
  byte 9   - Decile seconds within this period.  Values are 0 to 99
             representing 0.0 to 9.9 seconds.

* 0x03 := Query Version

  byte 6..15 - rfu, must be 0.

  Response format:

  byte 6   - nodetype (NODETYPE_xxxx)
  byte 7   - reserved
  byte 8..14 - GIT revision string or "unknown".
  byte 15   - reserved
*/

#include "protocol.h"

#define P_BUSCTL_RESPMASK    0x80 /* The response mask for the commands.  */
#define P_BUSCTL_TIME        0x01 /* Time Broadcast.  */
#define P_BUSCTL_QRY_TIME    0x02 /* Query Time.  */
#define P_BUSCTL_QRY_VERSION 0x03 /* Query software version.  */


#endif /*PROTO_BUSCTL_H*/
