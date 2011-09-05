/* ebusd.c - Ebus control daemon
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
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>


#define PGM           "ebusd"
#define PGM_VERSION   "0.0"
#define PGM_BUGREPORT "wk@gnupg.org"

/* Option flags. */
static int verbose;
static int debug;

/* Error counter.  */
static int any_error;


/* Print diagnostic message and exit with failure. */
static void
die (const char *format, ...)
{
  va_list arg_ptr;

  fflush (stdout);
  fprintf (stderr, "%s: ", PGM);

  va_start (arg_ptr, format);
  vfprintf (stderr, format, arg_ptr);
  va_end (arg_ptr);
  putc ('\n', stderr);

  exit (1);
}


/* Print diagnostic message. */
static void
err (const char *format, ...)
{
  va_list arg_ptr;

  any_error = 1;

  fflush (stdout);
  fprintf (stderr, "%s: ", PGM);

  va_start (arg_ptr, format);
  vfprintf (stderr, format, arg_ptr);
  va_end (arg_ptr);
  putc ('\n', stderr);
}


/* Print a info message message. */
static void
inf (const char *format, ...)
{
  va_list arg_ptr;

  if (verbose)
    {
      fprintf (stderr, "%s: ", PGM);

      va_start (arg_ptr, format);
      vfprintf (stderr, format, arg_ptr);
      va_end (arg_ptr);
      putc ('\n', stderr);
    }
}


static void
dump_mcbits (int fd)
{
  int mcbits;

  if (ioctl (fd, TIOCMGET, &mcbits))
    err ("TIOCMGET failed: %s\n", strerror (errno));
  else
    inf ("mc: %3s %3s %3s %3s %3s %3s %3s %3s %3s",
         (mcbits & TIOCM_LE )? "LE":"",
         (mcbits & TIOCM_DTR)? "DTR":"",
         (mcbits & TIOCM_DSR)? "DSR":"",
         (mcbits & TIOCM_CAR)? "DCD":"",
         (mcbits & TIOCM_RNG)? "RI":"",
         (mcbits & TIOCM_RTS)? "RTS":"",
         (mcbits & TIOCM_CTS)? "CTS":"",
         (mcbits & TIOCM_ST )? "TX2":"",
         (mcbits & TIOCM_SR )? "RX2":"");
}


static FILE *
open_line (const char *fname)
{
  FILE *fp;
  int fd;
  struct termios term;

  fp = fopen (fname, "r+");
  if (!fp || (fd = fileno (fp)) == -1)
    die ("can't open `%s': %s", fname, strerror (errno));

  if (tcgetattr (fd, &term))
    die ("tcgetattr(%d) failed: %s", fd, strerror (errno));

  term.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                    | INLCR | IGNCR | ICRNL | IXON);
  term.c_oflag &= ~OPOST;
  term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  term.c_cflag &= ~(CSIZE | PARENB);
  term.c_cflag |= CS8;

  if (cfsetospeed (&term, B9600) || cfsetispeed (&term, B9600))
    die ("setting terminal speed to 9600 failed: %s", strerror (errno));

  if (tcsetattr (fd, TCSANOW, &term ) )
    die ("tcsetattr(%d) failed: %s", fd, strerror (errno));

  inf ("connected to '%s' at 9600bps", fname);
  dump_mcbits (fd);
  /* { */
  /*   int mcbits; */

  /*   for (;;) */
  /*     { */
  /*       mcbits = TIOCM_RTS; */
  /*       if (ioctl (fd, TIOCMBIC, &mcbits)) */
  /*         err ("TIOCMBIC(RTS) failed: %s\n", strerror (errno)); */
  /*       mcbits = TIOCM_RTS; */
  /*       if (ioctl (fd, TIOCMBIS, &mcbits)) */
  /*         err ("TIOCMBIS(RTS) failed: %s\n", strerror (errno)); */
  /*     } */
  /* } */
  /* dump_mcbits (fd); */

  return fp;
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


static void
process (FILE *fp)
{
  unsigned char buffer[16+2];
  int idx, synced, esc;
  int c, i;

  esc = synced = idx = 0;
  while ((c=getc (fp)) != EOF)
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

          if (idx < sizeof buffer)
            buffer[idx++] = c;

          if (idx == sizeof buffer)
            {
              unsigned int crc = compute_crc (buffer);
              for (i=0; i < sizeof buffer; i++)
                printf ("%s%02x", i? " ":"", buffer[i]);
              if ((crc >> 8) == buffer[16] && (crc&0xff) == buffer[17])
                fputs (" ok", stdout);
              else
                fputs (" bad", stdout);
              putchar ('\n');
              fflush (stdout);
            }
        }
    }
}



static int
show_usage (int ex)
{
  fputs ("Usage: " PGM " DEVICE\n"
         "Control an attached ebus\n\n"
         "  --verbose      enable extra informational output\n"
         "  --debug        enable additional debug output\n"
         "  --help         display this help and exit\n\n"
         "Report bugs to " PGM_BUGREPORT ".\n",
         ex? stderr:stdout);
  exit (ex);
}


int
main (int argc, char **argv )
{
  int last_argc = -1;
  FILE *fp;

  if (argc)
    {
      argc--; argv++;
    }
  while (argc && last_argc != argc )
    {
      last_argc = argc;
      if (!strcmp (*argv, "--"))
        {
          argc--; argv++;
          break;
        }
      else if (!strcmp (*argv, "--version"))
        {
          fputs (PGM " " PGM_VERSION "\n", stdout);
          exit (0);
        }
      else if (!strcmp (*argv, "--help"))
        {
          show_usage (0);
        }
      else if (!strcmp (*argv, "--verbose"))
        {
          verbose = 1;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--debug"))
        {
          verbose = debug = 1;
          argc--; argv++;
        }
      else if (!strncmp (*argv, "--", 2))
        show_usage (1);
    }

  if (argc != 1)
    show_usage (1);

  setvbuf (stdout, NULL, _IOLBF, 0);

  fp = open_line (*argv);
  process (fp);
  fclose (fp);

  return any_error? 1:0;
}

/*
Local Variables:
compile-command: "cc -Wall -o ebusd ebusd.c"
End:
*/
