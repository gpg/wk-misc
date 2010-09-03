/* undump - Hex undump tool
 * Copyright (C) 2000, 2010 Werner Koch (dd9jn)
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
 * 2010-09-02 wk  Changed to GPLv3.
 *                Fixed detection of write errors.  Reported by Marcus
 *                Brinkmann
 */

#include <stdio.h>
#include <stdlib.h>

#define digitp(p)   ((p) >= '0' && (p) <= '9')
#define hexdigitp(a) (digitp (a)                     \
                      || ((a) >= 'A' && (a) <= 'F')  \
                      || ((a) >= 'a' && (a) <= 'f'))
#define ascii_isspace(a) ((a)==' ' || (a)=='\n' || (a)=='\r' || (a)=='\t')
#define xtoi_1(p)   ((p) <= '9'? ((p)- '0'): \
                     (p) <= 'F'? ((p)-'A'+10):((p)-'a'+10))




int
main (int argc, char **argv )
{
  int c1, c2;
  unsigned int value;

  if ( argc > 1 ) 
    {
      fprintf (stderr, "usage: undump < input\n");
      return 1;
    }
    

  while ( (c1=getchar ()) != EOF )
    {
      if (ascii_isspace (c1))
        continue;
      if (!hexdigitp (c1))
        {
          fprintf (stderr, "undump: non hex-digit encountered\n");
          return 1;
        }
      if ( (c2=getchar ()) == EOF )
        {
          fprintf (stderr, "undump: error reading second nibble\n");
          return 1;
        }
      if (!hexdigitp (c2))
        {
          fprintf (stderr, "undump: second nibble is not a hex-digit\n");
          return 1;
        }
      value = xtoi_1 (c1) * 16 + xtoi_1 (c2);
      putchar (value);
    }
  if (ferror (stdin))
    {
      fprintf (stderr, "undump: read error\n");
      return 1;
    }
  if (ferror (stdout))
    {
      fprintf (stderr, "undump: write error\n");
      return 1;
    }

  return 0;
}

/*
Local Variables:
compile-command: "cc -Wall -o undump undump.c"
End:
*/


