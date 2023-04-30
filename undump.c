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
 * 2020-05-28 wk  gpg-connect-agent dump mode detection
 */

/* Special features:
 * - \xHH is detected and converted.
 * - 0xHH is detected and converted.
 * - A trailing backslash is ignored as used by Libgcrypt logging.
 * - The line format
 *    "D[<any>]<up_to_3_spaces><hex_dump><3_spaces_or_more><rest>"
 *   is detected and only the <hex_dump> part is converted.  This
 *   is the format gpg-connect-agent uses in /hex mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  int last_lf, in_offset, dump_mode, skip_to_eol;
  unsigned int value;
  unsigned long lnr, off;
  int reformat = 0;
  unsigned long addr = 0;

  if (argc == 2 && !strcmp (argv[1], "-r"))
    reformat = 1;
  else if ( argc > 1 )
    {
      fprintf (stderr, "usage: undump [-r] < input\n");
      return 1;
    }


  last_lf = 1;
  in_offset = 0;
  dump_mode = 0;
  skip_to_eol = 0;
  lnr = 1;
  off = 0;
  while ( (c1=getchar ()) != EOF )
    {
      off++;
      if (c1 == '\n')
        {
          lnr++;
          last_lf = 1;
          in_offset = 0;
          dump_mode = 0;
          skip_to_eol = 0;
          continue;
        }
      if (skip_to_eol)
        continue;
      if (last_lf)
        {
          last_lf = 0;
          if (c1 == 'D')
            {
              c2 = getchar ();
              if (c2 == '[')
                {
                  in_offset = 1;
                  dump_mode = 1;
                  continue;
                }
              ungetc (c2, stdin);
            }
        }

      if (in_offset)
        {
          if (c1 == ']' || c1 == '\n')
            in_offset = 0;
          continue;
        }

      if (dump_mode)
        {
          if (c1 == ' ')
            dump_mode++;
          else
            dump_mode = 1;

          if (dump_mode > 3)  /* 3 spaces seen */
            skip_to_eol = 1;
        }


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
                      if (addr && reformat)
                        putchar('\n');
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
              if (addr && reformat)
                putchar('\n');
              fprintf (stderr, "undump: incomplete \\x "
                       "prefix at line %lu, off %lu\n", lnr, off);
              return 1;
            }
          c1 = c2;
        }
      if (!hexdigitp (c1))
        {
          if (addr && reformat)
            putchar('\n');
          fprintf (stderr,
                   "undump: non hex-digit encountered at line %lu, off %lu\n",
                   lnr, off);
          return 1;
        }
      if ( (c2=getchar ()) == EOF )
        {
          if (addr && reformat)
            putchar('\n');
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
                  if (addr && reformat)
                    putchar('\n');
                  fprintf (stderr, "undump: incomplete 0x "
                           "prefix at line %lu, off %lu\n", lnr, off);
                  return 1;
                }
            }
          else
            {
              if (addr && reformat)
                putchar('\n');
              fprintf (stderr, "undump: second nibble is not a hex-digit"
                       " at line %lu, off %lu\n", lnr, off);
              return 1;
            }
        }
      value = xtoi_1 (c1) * 16 + xtoi_1 (c2);
      if (reformat)
        {
          if (!(addr%16))
            {
              if (addr)
                putchar('\n');
              printf ("%08lx ", addr);
            }
          printf (" %02x", value);
          addr++;
        }
      else
        putchar (value);
    }

  if (addr && reformat)
    putchar('\n');

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
