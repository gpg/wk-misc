/* hsd-time.c - Timee functions for housed and housectl
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>

#include "hsd-time.h"

/* Take a time string and convert it into an ebus style time.  Ebus
   time is the number of 10 second intervals since Monday 0:00 local
   time. (uint16_t)(-1) is returned on error.

   Supported formats are:

     hh:mm:sx    - Hour, minute and 10 seconds on Monday
     w hh:mm:sx  - Ditto with weekday specified
     ndhh:mm:sx  - Ditto with weekday give as Monday (0) to  Sunday (6)

   with

     hh = Hour
     mm = Minutes
     sx = Seconds with the lower digit ignored
     w  = Weekday name (mo,tu,we,th,fr,sa,su)
                    or (mon,tue,wed,thu,fri,sat,sun)
          in any capitalization.
 */
uint16_t
timestr_to_ebustime (const char *string)
{




}
