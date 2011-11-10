/* protocol.h - Protocol definitons for an Elektor Bus Node
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

#ifndef PROTOCOL_H
#define PROTOCOL_H

/* The sync byte which indicates the start of a new message.  If this
   byte is used inside the message it needs to be escaped and XORed
   with the escape mask.  This ensures that a sync byte will always be
   the start of a new frame.  Note that this is similar to the PPP
   (rfc-1662) framing format.  */
#define FRAMESYNCBYTE 0x7e
#define FRAMEESCBYTE  0x7d
#define FRAMEESCMASK  0x20

/* The protocol id describes the format of the payload; ie. the actual
   message.  It is used instead of the 0xaa byte of the original (May
   2011) ElektorBus protocol.  Defined values:

   | Bit 7 .. 6 | Bit 5 .. 0   |
   |------------+--------------|
   | Msglen     | ProtocolType |
   |------------+--------------|

   | Msglen | Description          |
   |--------+----------------------|
   |      0 | 48 byte + 2 byte CRC |
   |      1 | 32 byte + 2 byte CRC |
   |      2 | 16 byte + 2 byte CRC |
   |      3 | RFU                  |
   |        |                      |

   | ProtocolType  | Description            | Allowed Msglen codes  |
   |---------------+------------------------+-----------------------|
   | 0x00          | Not used               | -                     |
   | 0x01 ... 0x2f | Assigned values        |                       |
   | 0x01          | BusControl             | 2,1,0                 |
   | 0x06          | H/61 protocol          | 2                     |
   | 0x1f          | DebugMessage           | 2,1,0                 |
   | 0x2a          | ElektorBus Application | 2                     |
   | 0x30 ... 0x37 | Experimental protocols |                       |
   | 0x38 ... 0x3e | RFU                    |                       |
   | 0x3f          | Not used               |                       |
 */
#define PROTOCOL_MSGLEN_MASK  0xc0
#define PROTOCOL_TYPE_MASK    0x3f
#define PROTOCOL_MSGLEN_48    0x00
#define PROTOCOL_MSGLEN_32    0x40
#define PROTOCOL_MSGLEN_16    0x80

#define PROTOCOL_EBUS_BUSCTL (PROTOCOL_MSGLEN_16 | 0x01)
#define PROTOCOL_EBUS_H61    (PROTOCOL_MSGLEN_16 | 0x06)
#define PROTOCOL_EBUS_DBGMSG (PROTOCOL_MSGLEN_16 | 0x1f)
#define PROTOCOL_EBUS_TEST   (PROTOCOL_MSGLEN_16 | 0x31)

#endif /*PROTOCOL_H*/
