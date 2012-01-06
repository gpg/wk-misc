/* xor - Extremly sophisticated encryption system
 * Copyright (C) 2011 Werner Koch (dd9jn)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main (int argc, char **argv )
{
  const unsigned char *key;
  int c, keyidx, keylen;
  unsigned char val;

  if ( argc != 2 )
    {
      fputs ("usage: xor KEYSTRING < input\n", stderr);
      return 1;
    }

  key = (const unsigned char *)argv[1];
  keylen = strlen ((const char*)key);
  keyidx = 0;

  while ( (c=getchar ()) != EOF )
    {
      val = (c & 0xff);
      if (keylen)
        {
          val ^= key[keyidx++];
          if (keyidx >= keylen)
            keyidx = 0;
        }
      putchar (val);
    }
  if (ferror (stdin))
    {
      fputs ("xor: read error\n", stderr);
      return 1;
    }
  if (fflush (stderr) || ferror (stdout))
    {
      fputs ("xor: write error\n", stderr);
      return 1;
    }

  return 0;
}

/*
Local Variables:
compile-command: "cc -Wall -o xor xor.c"
End:
*/
