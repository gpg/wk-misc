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
#include <stdint.h>

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

static uint16_t
crc_ccitt_update (uint16_t crc, uint8_t data)
{
  data ^= (crc & 0xff);
  data ^= data << 4;

  return ((((uint16_t)data << 8) | ((crc >> 8)& 0xff)) ^ (uint8_t)(data >> 4)
          ^ ((uint16_t)data << 3));
}

/* Compute the CRC for MSG.  MSG must be of MSGSIZE.  The CRC used is
   possible not the optimal CRC for our message length.  However we
   have a convenient inline function for it.  */
static uint16_t
compute_crc (const unsigned char *msg)
{
  int idx;
  uint16_t crc = 0xffff;

  for (idx=0; idx < 16; idx++)
    crc = crc_ccitt_update (crc, msg[idx]);

  return crc;
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
  unsigned char buffer[18];
  unsigned int protocol;

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

  any = esc = synced = idx = 0;
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

              if (idx < sizeof (buffer))
                buffer[idx] = c;

              if (!idx)
                protocol = (c & 0x3f);

              if (topmode && !idx && (c & 0xc0) != 0x80)
                {
                  printf ("\x1b[1;1H" "[bad protocol_msglen] %02x", c);
                  synced = 0;
                  any = 1;
                }
              else if (idx < 2 && protocol == 0x31)
                ; /* Printed later.  */
              else if (topmode && idx == 2 && protocol == 0x31)
                {
                  if (c >= FIRST_NODE_ID && c <= LAST_NODE_ID)
                    printf ("\x1b[%d;1H", c+1 );
                  else
                    printf ("\x1b[1;1H[bad node-id]\x1b[K ");
                  printf ("%02x", buffer[0]);
                  printf (" %02x", buffer[1]);
                  printf (" %02x", buffer[2]);
                  any = 1;
                }
              else if (topmode && idx > 3 && idx < 16 && protocol == 0x31)
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
                  if (idx && idx < 16)
                    putchar (' ');
                  else if (idx == 16 && protocol == 0x31)
                    printf (" crc: ");
                  else if (idx == 18 && protocol == 0x31)
                    printf (" trash: ");
                  printf ("%02x", c);
                  if (idx == 17 && protocol == 0x31)
                    {
                      unsigned int crc = compute_crc (buffer);
                      if ((crc >> 8) == buffer[16] && (crc&0xff) == buffer[17])
                        printf (" ok ");
                      else
                        printf (" bad");
                    }
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
