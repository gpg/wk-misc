/* proto-dbgmsg.h - Definition of the debug message protocol.
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

#ifndef PROTO_DBGMSG_H
#define PROTO_DBGMSG_H

/* Description of the Debug Message protocol.

   This protocol is used to send debug strings to the bus.

   byte  0 - protocol PROTOCOL_EBUS_DBGMSG
   byte  1 - sender   node-id high
   byte  2 - sender   node-id low
   byte  3...15 - string

*/

#include "protocol.h"


#endif /*PROTO_DBGMSG_H*/
