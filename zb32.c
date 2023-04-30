/* zb32.c - z-base-32 encoder
 * Copyright (C) 2014, 2015  Werner Koch
 *
 * This file is part of GnuPG.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* The code is based on GnuPG's common/zb32.c  */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PGM "zb32"


/* Zooko's base32 variant. See RFC-6189 and
   http://philzimmermann.com/docs/human-oriented-base-32-encoding.txt
   Caller must xfree the returned string.  Returns NULL and sets ERRNO
   on error.  To avoid integer overflow DATALEN is limited to 2^16
   bytes.  Note, that DATABITS is measured in bits!.  */
static char *
zb32_encode (const void *data, unsigned int databits)
{
  static char const zb32asc[32] = {'y','b','n','d','r','f','g','8',
                                   'e','j','k','m','c','p','q','x',
                                   'o','t','1','u','w','i','s','z',
                                   'a','3','4','5','h','7','6','9' };
  const unsigned char *s;
  char *output, *d;
  size_t datalen;

  datalen = (databits + 7) / 8;
  if (datalen > (1 << 16))
    {
      errno = EINVAL;
      return NULL;
    }

  d = output = malloc (8 * (datalen / 5)
                           + 2 * (datalen % 5)
                           - ((datalen%5)>2)
                           + 1);
  if (!output)
    return NULL;

  /* I use straightforward code.  The compiler should be able to do a
     better job on optimization than me and it is easier to read.  */
  for (s = data; datalen >= 5; s += 5, datalen -= 5)
    {
      *d++ = zb32asc[((s[0]      ) >> 3)               ];
      *d++ = zb32asc[((s[0] &   7) << 2) | (s[1] >> 6) ];
      *d++ = zb32asc[((s[1] &  63) >> 1)               ];
      *d++ = zb32asc[((s[1] &   1) << 4) | (s[2] >> 4) ];
      *d++ = zb32asc[((s[2] &  15) << 1) | (s[3] >> 7) ];
      *d++ = zb32asc[((s[3] & 127) >> 2)               ];
      *d++ = zb32asc[((s[3] &   3) << 3) | (s[4] >> 5) ];
      *d++ = zb32asc[((s[4] &  31)     )               ];
    }

  switch (datalen)
    {
    case 4:
      *d++ = zb32asc[((s[0]      ) >> 3)               ];
      *d++ = zb32asc[((s[0] &   7) << 2) | (s[1] >> 6) ];
      *d++ = zb32asc[((s[1] &  63) >> 1)               ];
      *d++ = zb32asc[((s[1] &   1) << 4) | (s[2] >> 4) ];
      *d++ = zb32asc[((s[2] &  15) << 1) | (s[3] >> 7) ];
      *d++ = zb32asc[((s[3] & 127) >> 2)               ];
      *d++ = zb32asc[((s[3] &   3) << 3)               ];
      break;
    case 3:
      *d++ = zb32asc[((s[0]      ) >> 3)               ];
      *d++ = zb32asc[((s[0] &   7) << 2) | (s[1] >> 6) ];
      *d++ = zb32asc[((s[1] &  63) >> 1)               ];
      *d++ = zb32asc[((s[1] &   1) << 4) | (s[2] >> 4) ];
      *d++ = zb32asc[((s[2] &  15) << 1)               ];
      break;
    case 2:
      *d++ = zb32asc[((s[0]      ) >> 3)               ];
      *d++ = zb32asc[((s[0] &   7) << 2) | (s[1] >> 6) ];
      *d++ = zb32asc[((s[1] &  63) >> 1)               ];
      *d++ = zb32asc[((s[1] &   1) << 4)               ];
      break;
    case 1:
      *d++ = zb32asc[((s[0]      ) >> 3)               ];
      *d++ = zb32asc[((s[0] &   7) << 2)               ];
      break;
    default:
      break;
    }
  *d = 0;

  /* Need to strip some bytes if not a multiple of 40.  */
  output[(databits + 5 - 1) / 5] = 0;
  return output;
}




int
main (int argc, char **argv )
{
  int c, i;
  char buffer[500]; /* Needs to be a multiple of 5! */
  char *output;

  if ( argc > 1 )
    {
      fprintf (stderr, "usage: " PGM " < input\n");
      return 1;
    }

  i = 0;
  while ((c = getchar ()) != EOF)
    {
      buffer[i++] = c;
      if (i == sizeof buffer)
        {
          output = zb32_encode (buffer, 8 * sizeof buffer);
          if (!output)
            {
              fprintf (stderr, PGM ": error converting data: %s\n",
                       strerror (errno));
              return 1;
            }
          fputs (output, stdout);
          free (output);
          i = 0;
        }
    }
  if (ferror (stdin))
    {
      fprintf (stderr, PGM ": read error: %s\n", strerror (errno));
      return 1;
    }

  if (i)
    {
      output = zb32_encode (buffer, 8 * i - 2);
      if (!output)
        {
          fprintf (stderr, PGM ": error converting data: %s\n",
                   strerror (errno));
          return 1;
        }
      fputs (output, stdout);
    }

  putchar ('\n');

  if (fflush (stdout) || ferror (stdout))
    {
      fprintf (stderr, PGM ": write error: %s\n", strerror (errno));
      return 1;
    }

  return 0;
}
