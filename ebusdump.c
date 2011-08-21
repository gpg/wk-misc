/* ebusdump - Hex dump tool for ebus frames
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
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Valid nodes we want to print in --top mode.  */
#define FIRST_NODE_ID 1
#define LAST_NODE_ID  5

static volatile int ctrl_c_pending;

static void
control_c_handler (int signo)
{
  (void)signo;
  ctrl_c_pending = 1;
}


int
main (int argc, char **argv )
{
  int rawmode = 0;
  int topmode = 0;
  int c;
  int any;
  int esc;
  int synced;
  int idx;
  unsigned int value;
  unsigned char buffer[5];

  if (argc)
    {
      argc--; argv++;
    }

  if (argc && !strcmp (*argv, "--raw"))
    {
      rawmode = 1;
      argc--; argv++;
    }
  else if (argc && !strcmp (*argv, "--top"))
    {
      topmode = 1;
      argc--; argv++;
    }

  if (argc)
    {
      fprintf (stderr, "usage: pppdump [--raw|--top] < input\n");
      return 1;
    }

  if (topmode)
    {
      struct sigaction nact;

      nact.sa_handler = control_c_handler;
      nact.sa_flags = 0;
      sigaction (SIGINT, &nact, NULL);
      printf ("\x1b[1;1H\x1b[J"); /* Clear screen.  */
      fflush (stdout);
    }

  any = esc = synced = 0;
  while ( (c=getchar ()) != EOF && !ctrl_c_pending)
    {
      if (c == 0x7e && any)
        {
          if (topmode)
            printf ("\x1b[K");  /* Clear to end of line.  */
          else
            putchar ('\n');
          any = 0;
        }
      if (rawmode)
        {
          printf ("%s%02x", any?" ":"", c);
          any = 1;
        }
      else
        {
          if (c == 0x7e)
            {
              esc = 0;
              synced = 1;
              idx = 0;
            }
          else if (c == 0x7d && !esc)
            esc = 1;
          else if (synced)
            {
              if (esc)
                {
                  esc = 0;
                  c ^= 0x20;
                }
              if (topmode && !idx && c != 0x41)
                {
                  printf ("\x1b[1;1H" "[bad_protocol] %02x", c);
                  synced = 0;
                  any = 1;
                }
              else if (topmode && idx < 2)
                {
                  buffer[idx] = c;
                }
              else if (topmode && idx == 2)
                {
                  if (c >= FIRST_NODE_ID && c <= LAST_NODE_ID)
                    printf ("\x1b[%d;1H", c+1 );
                  else
                    printf ("\x1b[1;1H[bad node-id]\x1b[K ");
                  printf ("%02x", buffer[0]);
                  printf (" %02x", buffer[1]);
                  printf (" %02x", c);
                  any = 1;
                }
              else if (topmode && idx > 3 && idx < 16)
                {
                  switch (idx)
                    {
                    case 4: case 6: case 8: case 10: case 12: case 14:
                      value = c << 8;
                      break;
                    case 5:
                      value |= c;
                      printf (" t:%6u", value);
                      break;
                    case 7:
                      value |= c;
                      printf (" nrx:%6u", value);
                      break;
                    case 9:
                      value |= c;
                      printf (" ntx:%6u", value);
                      break;
                    case 11:
                      value |= c;
                      printf (" col:%6u", value);
                      break;
                    case 13:
                      value |= c;
                      printf (" ovf:%6u", value);
                      break;
                    case 15:
                      value |= c;
                      printf (" int:%6u", value);
                      break;
                    }
                  any = 1;
                }
              else if (idx < 30)
                {
                  if (idx < 16)
                    putchar (' ');
                  else if (idx == 16)
                    printf ("  trash: ");
                  printf ("%02x", c);
                  any = 1;
                }
              else if (idx == 30)
                printf ("...");
              idx++;
            }
        }
    }
  if (topmode)
    printf ("\x1b[%d;1H\x1b[K\n", 7);
  if (any)
    putchar ('\n');
  fflush (stdout);
  if (ferror (stdin) && !ctrl_c_pending)
    {
      fprintf (stderr, "ebusdump: read error\n");
      return 1;
    }
  if (ferror (stdout))
    {
      fprintf (stderr, "ebusdump: write error\n");
      return 1;
    }

  return 0;
}

/*
Local Variables:
compile-command: "cc -Wall -o ebusdump ebusdump.c"
End:
*/
