/* rot13 - very sophisticated encryption system
 *	Copyright (C) 2000 Werner Koch (dd9jn)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include <stdio.h>
#include <stdlib.h>

static void
rot13 ( char *block )
{
    int i, c; 

    for (i = 0; i < 5; i++ ) {
        c = (block[i] + 13) % 26;
        putchar ( 'A'+c );
    }
}


int
main (int argc, char **argv )
{
    char block[5];
    int n=0, c, idx=0;

    if ( argc > 1 ) {
        fprintf (stderr, "usage: rot13 < input\n");
        return 1;
    }
    

    while ( (c=getchar ()) != EOF ) {
        if ( c >= 'a' && c <= 'z' )
            block[idx++] = c - 'a';
        else if ( c >= 'A' && c <= 'Z' ) 
            block[idx++] = c - 'A';
        else if ( c != ' ' && c != '\t' && c != '\n' )
            fprintf (stderr, "rot13: ignoring character 0x%02X\n", c );
        if ( idx >= 5 ) {
            if ( n )
                putchar (' ');
            rot13 (block);
            idx = 0;
            if ( ++n == 10 ) {
                putchar ('\n');
                n = 0;
            }
        }
    }
    if ( idx ) {
        while ( idx < 5 )
            block[idx++] = 'X' - 'A';
        if ( n )
            putchar (' ');
        rot13 (block);
        putchar ('\n');
    }

    return 0;
}



