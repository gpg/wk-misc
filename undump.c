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
 * 2011-02-24 wk  Allow for 0x and \x prefixes.  Print offset with
 *                the error messages.
 * 2019-03-20 wk  Allow for trailing backslashes.
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
  unsigned long lnr, off;

  if ( argc > 1 )
    {
      fprintf (stderr, "usage: undump < input\n");
      return 1;
    }


  lnr = 1;
  off = 0;
  while ( (c1=getchar ()) != EOF )
    {
      off++;
      if (c1 == '\n')
        lnr++;
      if (ascii_isspace (c1))
        continue;
      if (c1 == '\\')
        {
          /* Assume the hex digits are prefixed with \x.  */
          c1 = getchar ();
          off++;
          if (c1 == '\n')
            {
              /* But this is a trailing backslash - skip.  */
              lnr++;
              continue;
            }
          if (ascii_isspace (c1))
            {
              /* backslash followed by space - see whether this
               * can also be considered as a trailing backslash.  */
              while ((c1 = getchar ()) != EOF && ++off && c1 != '\n')
                {
                  if (!ascii_isspace (c1))
                    {
                      fprintf (stderr, "undump: spurious backslash "
                               "at line %lu, off %lu\n", lnr, off);
                      return 1;
                    }
                }
              if (c1 == '\n')
                {
                  lnr++;
                  continue;
                }
              /* EOF */
              break;
            }
          if (c1 != EOF)
            {
              c2 = getchar ();
              off++;
            }
          if (c1 != 'x' || c2 == EOF)
            {
              fprintf (stderr, "undump: incomplete \\x "
                       "prefix at line %lu, off %lu\n", lnr, off);
              return 1;
            }
          c1 = c2;
        }
      if (!hexdigitp (c1))
        {
          fprintf (stderr,
                   "undump: non hex-digit encountered at line %lu, off %lu\n",
                   lnr, off);
          return 1;
        }
      if ( (c2=getchar ()) == EOF )
        {
          fprintf (stderr,
                   "undump: error reading second nibble at line %lu, off %lu\n",
                   lnr, off);
          return 1;
        }
      off++;
      if (c2 == '\n')
        lnr++;
      if (!hexdigitp (c2))
        {
          if (c1 == '0' && c2 == 'x')
            {
              /* Assume the hex digits are prefixed with 0x.  */
              c1 = getchar ();
              off++;
              if (c1 != EOF)
                {
                  c2 = getchar ();
                  off++;
                }
              if (c1 == EOF || c2 == EOF || !hexdigitp (c1) || !hexdigitp (c2))
                {
                  fprintf (stderr, "undump: incomplete 0x "
                           "prefix at line %lu, off %lu\n", lnr, off);
                  return 1;
                }
            }
          else
            {
              fprintf (stderr, "undump: second nibble is not a hex-digit"
                       " at line %lu, off %lu\n", lnr, off);
              return 1;
            }
        }
      value = xtoi_1 (c1) * 16 + xtoi_1 (c2);
      putchar (value);
    }
  if (ferror (stdin))
    {
      fprintf (stderr, "undump: read error at line %lu, off %lu\n", lnr, off);
      return 1;
    }
  if (ferror (stdout))
    {
      fprintf (stderr, "undump: write error at input line %lu, off %lu\n",
               lnr, off);
      return 1;
    }

  return 0;
}

/*
Local Variables:
compile-command: "cc -Wall -o undump undump.c"
End:
*/
