/* 8bit-in-header.c
 * A tool to check for non ASCII characters in RFC822 messages header.
 *    Copyright (C) 2000  Werner Koch
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
 *
 *
 * 2000-01-06 wk
 * Written as an attempt to get rid of strange chinese mails
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PGMNAME "8bit-in-header"

/* bail out after more than BAD_LIMIT 8/bit chars in one logical line
 * We allow for a few bad characters due to transmission errors or
 * something like this when option -s is used.
 */
#define BAD_LIMIT 3

#define MAX_NAMES 200

static int silent;
static int sloppy;

static void
usage(void)
{
    fputs("usage: " PGMNAME " [-q] [-s] [--] [headernames]\n"
          "\n"
          "  -q   be silent\n"
          "  -s   sloppy match: accept up to 2 non ascii characters\n"
                      , stderr );
    exit(2);
}

static int
name_cmp( const char *a, const char *b )
{
    for( ; *a && *b; a++, b++ ) {
        if( *a != *b
            && toupper(*(unsigned char *)a) != toupper(*(unsigned char *)b) )
            return 1;
    }
    return *a != *b;
}

static int
headerp( char *p, char **names )
{
    int i, c;
    char *p2;

    p2 = strchr(p, ':');
    if( !p2 || p == p2 ) {
        if( !silent )
            fputs( "header line without colon or name\n", stdout );
        exit(1);
    }
    if( p2[-1] == ' ' || p2[-1] == '\t' ) {
        if( !silent )
            fputs( "invalid header field\n", stdout );
        exit(1);
    }

    if( !names[0] )
        return 1;  /* check all fields */
    c = *p2;
    *p2 = 0;
    for(i=0 ; names[i]; i++ ) {
        if( !name_cmp( names[i], p ) )
            break;
    }
    *p2 = c;
    return !!names[i];
}


static int
count_bad( const char *s )
{
    int bad=0;

    for(; *s; s++ ) {
        if( *s & 0x80 )
            bad++;
    }
    return bad;
}

int
main( int argc, char **argv )
{
    int skip = 0;
    char *names[MAX_NAMES+1];
    int thisone, bad, i;
    char line[2000];

    if( argc < 1)
        usage();  /* Hey, read how to uses exec*(2) */
    argv++; argc--;

    for( i=0; argc; argc--, argv++ ) {
        const char *s = *argv;
        if( !skip && *s == '-' ) {
            s++;
            if( *s == '-' && !s[1] ) {
                skip = 1;
                continue;
            }
            if( *s == '-' || !*s ) {
                usage();
            }
            while( *s ) {
                if( *s=='q' ) {
                    silent=1;
                    s++;
                }
                else if( *s=='s' ) {
                    sloppy=1;
                    s++;
                }
                else if( *s )
                    usage();
            }
            continue;
        }
        if( i >= MAX_NAMES ) {
            fputs(PGMNAME ": too many names given\n", stderr );
            exit(2);
        }
        names[i++] = *argv;
    }
    names[i] = NULL;


    /* now get the lines */
    bad = thisone = 0;
    while( fgets( line, sizeof line, stdin ) ) {
        int n = strlen(line);
        if( !n || line[n-1] != '\n' ) {
            /* n == 0 should never happen */
            if( !silent )
                fputs( "line too long - see RFC822\n", stdout );
            exit(1);  /* maybe someone wants to circument this */
        }
        line[--n] = 0;
        if( n && line[n-1] == '\r' )
            line[--n] = 0;
        if( !n ) {
            exit(0); /* Here is the body - stop */
        }
        if( *line == ' ' || *line == '\t' ) {
            /* we don't care when the first line is an invalid cont.-line */
            if( thisone )
                bad += count_bad( line );
        }
        else if( headerp( line, names ) ) {
            thisone = 1;
            bad = count_bad( line );
        }
        else {
            thisone = 0;
            bad = 0;
        }
        if( (bad && !sloppy) || bad >= BAD_LIMIT ) {
            if( !silent ) {
                if( sloppy )
                    fputs( "too many 8-bit characters in a header\n", stdout );
                else
                    fputs( "8-bit character in a header\n", stdout );
            }
            exit(1);
        }
    }
    if( ferror(stdin) ) {
        fputs( PGMNAME ": read error\n", stderr );
        exit(1);
    }
    return 0;
}

/*
Local Variables:
compile-command: "gcc -Wall -g -o 8bit-inheader.o 8bit-in-header.c"
End:
*/
