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

#include "hsd-misc.h"
#include "hsd-time.h"


/* Take a time string and convert it into an ebus style time.  Ebus
   time is the number of 10 second intervals since Monday 0:00 local
   time. (uint16_t)(-1) is returned on error.

   Supported formats are:

     hh:mm[:sx]    - Hour, minute and 10 seconds on Monday
     w hh:mm[:sx]  - Ditto with weekday specified
     ndhh:mm[:sx]  - Ditto with weekday give as Monday (0) to  Sunday (6)

   with

     hh = Hour
     mm = Minutes
     sx = Seconds with the lower digit ignored
     w  = Weekday name (mo,tu,we,th,fr,sa,su)
                    or (mon,tue,wed,thu,fri,sat,sun)
          in any capitalization.
 */
uint16_t
timestr_to_ebustime (char const *string, char **r_endp)
{
  int i, n;
  int weekday = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  char const *s;
  char *endp;

  if (!string)
    return INVALID_TIME;

  for (; *string && ascii_isspace (*string); string++)
    ;

  if (*string >= '0' && *string <= '6' && string[1] == 'd')
    {
      weekday = *string - '0';
      string += 2;
    }
  else if ((*string >= 'a' && *string < 'z')
           || (*string >= 'A' && *string < 'Z'))
    {
      static struct { char no; char const * const name; } tbl[] = {
        { 0, "mo" }, { 0, "mon" }, { 0, "monday" },
        { 1, "tu" }, { 1, "tue" }, { 1, "tuesday" },
        { 2, "we" }, { 2, "wed" }, { 2, "wednesday" },
        { 3, "th" }, { 3, "thu" }, { 3, "thursday" },
        { 4, "fr" }, { 4, "fri" }, { 4, "friday" },
        { 5, "sa" }, { 5, "sat" }, { 5, "saturday" },
        { 6, "su" }, { 6, "sun" }, { 6, "sunday" }};

      for (s=string+1; *s; s++)
        if (ascii_isspace (*s))
          break;
      n = s - string;
      for (i=0; i < DIM (tbl); i++)
        if (strlen (tbl[i].name) == n
            && !ascii_strncasecmp (string, tbl[i].name, n))
          break;
      if (!(i < DIM (tbl)))
        return INVALID_TIME;
      weekday = tbl[i].no;
      for (; *s && ascii_isspace (*s); s++)
        ;
      string = s;
    }

  if (!digitp (string))
    return INVALID_TIME;
  hour = strtol (string, &endp, 10);
  if (hour < 0 || hour > 23 || *endp != ':')
    return INVALID_TIME;
  string = endp + 1;
  minute = strtol (string, &endp, 10);
  if (!digitp (string) || !digitp (string+1) || minute < 0 || minute > 59)
    return INVALID_TIME;
  if (*endp == ':')
    {
      string = endp + 1;
      second = strtol (string, &endp, 10);
      /* Note: We don't care about leap seconds.  */
      if (!digitp (string) || !digitp (string+1) || second < 0 || second > 59)
        return INVALID_TIME;
    }

  if (!*endp)
    string = endp;
  else if (ascii_isspace (*endp))
    string = endp + 1;
  else
    return INVALID_TIME;

  if (r_endp)
    *r_endp = (char *)string;

  return (weekday * 24 * 60 * 6
          + hour * 60 * 6
          + minute * 6
          + second / 10);
}



char *
ebustime_to_timestr (uint16_t ebustime)
{
  unsigned int day, hour, min, sec;
  char *result;

  if (ebustime == INVALID_TIME)
    {
      if (asprintf (&result, "[invalid time]") == -1)
        result = NULL;
    }
  else
    {
      day = (ebustime/6/60/24);
      hour= (ebustime/6/60 % 24);
      min = (ebustime/6 % 60);
      sec = (ebustime % 6) * 10;

      if (asprintf (&result, "%s %u:%02u:%02u",
                    day == 0? "Mon" :
                    day == 1? "Tue" :
                    day == 2? "Wed" :
                    day == 3? "Thu" :
                    day == 4? "Fri" :
                    day == 5? "Sat" :
                    day == 6? "Sun" : "[?]",
                    hour, min, sec) == -1)
        result = NULL;
    }
  return result;
}

