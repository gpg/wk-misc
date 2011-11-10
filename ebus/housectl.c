/* housectl.c - A house control utility
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
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "protocol.h"
#include "proto-busctl.h"
#include "proto-h61.h"

#include "hsd-misc.h"
#include "hsd-time.h"


#define PGM           "housectl"
#define PGM_VERSION   "0.0"
#define PGM_BUGREPORT "wk@gnupg.org"

typedef unsigned char byte;

/* Option flags. */
static int verbose;
static int debug;
static int line_speed = 19200;

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
  speed_t speed;

  switch (line_speed)
    {
    case 300   : speed = B300   ; break;
    case 600   : speed = B600   ; break;
    case 1200  : speed = B1200  ; break;
    case 2400  : speed = B2400  ; break;
    case 4800  : speed = B4800  ; break;
    case 9600  : speed = B9600  ; break;
    case 19200 : speed = B19200 ; break;
    case 38400 : speed = B38400 ; break;
    case 57600 : speed = B57600 ; break;
    case 115200: speed = B115200; break;
    default:
      die ("unsupported line speed %d given", line_speed);
    }

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

  if (cfsetospeed (&term, speed) || cfsetispeed (&term, speed))
    die ("setting terminal speed to %d failed: %s",
         line_speed, strerror (errno));

  if (tcsetattr (fd, TCSANOW, &term ) )
    die ("tcsetattr(%d) failed: %s", fd, strerror (errno));

  inf ("connected to '%s' at %dbps", fname, line_speed);
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


/* Compute the CRC for MSG.  MSG must be of MSGLEN.  The CRC used is
   possible not the optimal CRC for our message length.  However on
   the AVR we have a convenient inline function for it.  */
static uint16_t
compute_crc (const unsigned char *msg, size_t msglen)
{
  int idx;
  uint16_t crc = 0xffff;

  for (idx=0; idx < msglen; idx++)
    crc = crc_ccitt_update (crc, msg[idx]);

  return crc;
}


/* Return the current ebus time for broadcasting.  The time is defined
   as number of 10 second periods passed since Monday 0:00.  */
static unsigned int
mk_ebus_time (unsigned int *r_decile, unsigned int *r_dst)
{
  struct tm *tp;
  time_t atime = time (NULL);
  unsigned int result;

  /* Get the local time and convert it to a Monday...Sunday week.  */
  /* Fixme: We can't return fractions of a second.  Need to use
     clock_gettime or wait for the full second.  */
  tp = localtime (&atime);
  if (!tp->tm_wday)
    tp->tm_wday = 6;
  else
    tp->tm_wday--;

  result = (tp->tm_wday * 24 * 60 * 6
            + tp->tm_hour * 60 * 6
            + tp->tm_min * 6 + tp->tm_sec/10);
  if (r_decile)
    *r_decile = (tp->tm_sec % 10) * 10;
  if (r_dst)
    *r_dst = !!tp->tm_isdst;
  return result;
}




/* Send out the raw byte C.  */
static void
send_byte_raw (FILE *fp, byte c)
{
  putc (c, fp);
}


/* Send byte C with byte stuffing.  */
static void
send_byte (FILE *fp, byte c)
{
  if (c == FRAMESYNCBYTE || c == FRAMEESCBYTE)
    {
      send_byte_raw (fp, FRAMEESCBYTE);
      send_byte_raw (fp, (c ^ FRAMEESCMASK));
    }
  else
    send_byte_raw (fp, c);
}


static void
cmd_query_time (FILE *fp)
{
  byte msg[16];
  unsigned int crc;
  int idx;

  msg[0] = PROTOCOL_EBUS_BUSCTL;
  msg[1] = 0xff;
  msg[2] = 0xff;
  msg[3] = 0x01;
  msg[4] = 0x01;
  msg[5] = P_BUSCTL_QRY_TIME;
  memset (msg+6, 0, 10);
  crc = compute_crc (msg, 16);

  send_byte_raw (fp, FRAMESYNCBYTE);
  for (idx=0; idx < 16; idx++)
    send_byte (fp, msg[idx]);
  send_byte (fp, crc >> 8);
  send_byte (fp, crc);
  fflush (fp);
}


static void
cmd_query_version (FILE *fp)
{
  byte msg[16];
  unsigned int crc;
  int idx;

  msg[0] = PROTOCOL_EBUS_BUSCTL;
  msg[1] = 0xff;
  msg[2] = 0xff;
  msg[3] = 0x01;
  msg[4] = 0x01;
  msg[5] = P_BUSCTL_QRY_VERSION;
  memset (msg+6, 0, 10);
  crc = compute_crc (msg, 16);

  send_byte_raw (fp, FRAMESYNCBYTE);
  for (idx=0; idx < 16; idx++)
    send_byte (fp, msg[idx]);
  send_byte (fp, crc >> 8);
  send_byte (fp, crc);
  fflush (fp);
}



static void
cmd_query_shutter_schedule (FILE *fp)
{
  byte msg[16];
  unsigned int crc;
  int idx;

  msg[0] = PROTOCOL_EBUS_H61;
  msg[1] = 0x10;
  msg[2] = 0x05;
  msg[3] = 0x01;
  msg[4] = 0x01;
  msg[5] = P_H61_SHUTTER;
  msg[6] = P_H61_SHUTTER_QRY_SCHEDULE;
  msg[7] = 1;
  memset (msg+8, 0, 8);
  crc = compute_crc (msg, 16);

  send_byte_raw (fp, FRAMESYNCBYTE);
  for (idx=0; idx < 16; idx++)
    send_byte (fp, msg[idx]);
  send_byte (fp, crc >> 8);
  send_byte (fp, crc);
  fflush (fp);
}

static void
cmd_broadcast_time (FILE *fp)
{
  byte msg[16];
  unsigned int crc;
  int idx;
  unsigned int tim, dec, dst;

  tim = mk_ebus_time (&dec, &dst);
  msg[0] = PROTOCOL_EBUS_BUSCTL;
  msg[1] = 0xff;
  msg[2] = 0xff;
  msg[3] = 0x01;
  msg[4] = 0x01;
  msg[5] = P_BUSCTL_TIME;
  msg[6] = 0x03;  /* Decile given, exact time */
  if (dst)
    msg[6] |= 0x04;
  msg[7] = tim >> 8;
  msg[8] = tim;
  msg[9] = dec;
  memset (msg+10, 0, 6);
  crc = compute_crc (msg, 16);

  send_byte_raw (fp, FRAMESYNCBYTE);
  for (idx=0; idx < 16; idx++)
    send_byte (fp, msg[idx]);
  send_byte (fp, crc >> 8);
  send_byte (fp, crc);
  fflush (fp);
}


/* static void */
/* footest_4 (FILE *fp) */
/* { */
/*   byte msg[16]; */
/*   unsigned int crc; */
/*   int idx; */
/*   unsigned char   item = 2; */
/*   unsigned int     tim = 3 * 60 * 6; */
/*   unsigned char action = 0x80; */

/*   msg[0] = PROTOCOL_EBUS_H61; */
/*   msg[1] = 0x10; */
/*   msg[2] = 0x05; */
/*   msg[3] = 0x01; */
/*   msg[4] = 0x01; */
/*   msg[5] = P_H61_SHUTTER; */
/*   msg[6] = P_H61_SHUTTER_UPD_SCHEDULE; */
/*   msg[7] = 0; */
/*   msg[8] = 0; */
/*   msg[9] = 1; */
/*   msg[10] = item; */
/*   msg[11] = tim >> 8; */
/*   msg[12] = tim; */
/*   msg[13] = action; */
/*   msg[14] = 0; */
/*   msg[15] = 0; */
/*   crc = compute_crc (msg, 16); */

/*   send_byte_raw (fp, FRAMESYNCBYTE); */
/*   for (idx=0; idx < 16; idx++) */
/*     send_byte (fp, msg[idx]); */
/*   send_byte (fp, crc >> 8); */
/*   send_byte (fp, crc); */
/*   fflush (fp); */
/* } */

static void
cmd_query_shutter_state (FILE *fp)
{
  byte msg[16];
  unsigned int crc;
  int idx;

  msg[0] = PROTOCOL_EBUS_H61;
  msg[1] = 0x10;
  msg[2] = 0x05;
  msg[3] = 0x01;
  msg[4] = 0x01;
  msg[5] = P_H61_SHUTTER;
  msg[6] = P_H61_SHUTTER_QUERY;
  msg[7] = 0;
  msg[8] = 0;
  msg[9] = 0;
  msg[10] = 0;
  msg[11] = 0;
  msg[12] = 0;
  msg[13] = 0;
  msg[14] = 0;
  msg[15] = 0;
  crc = compute_crc (msg, 16);

  send_byte_raw (fp, FRAMESYNCBYTE);
  for (idx=0; idx < 16; idx++)
    send_byte (fp, msg[idx]);
  send_byte (fp, crc >> 8);
  send_byte (fp, crc);
  fflush (fp);
}


static void
cmd_drive_shutter (FILE *fp, const char *subcmd)
{
  byte msg[16];
  unsigned int crc;
  int idx;

  msg[0] = PROTOCOL_EBUS_H61;
  msg[1] = 0x10;
  msg[2] = 0x05;
  msg[3] = 0x01;
  msg[4] = 0x01;
  msg[5] = P_H61_SHUTTER;
  msg[6] = P_H61_SHUTTER_DRIVE;
  msg[7] = 0;

  if (!strcmp (subcmd, "up"))
    msg[8] = 0xc0;
  else if (!strcmp (subcmd, "down"))
    msg[8] = 0x80;
  else
    {
      err ("invalid sub-command.  Use \"up\" or \"down\"");
      return;
    }
  msg[9] = 0;
  msg[10] = 0;
  msg[11] = 0;
  msg[12] = 0;
  msg[13] = 0;
  msg[14] = 0;
  msg[15] = 0;
  crc = compute_crc (msg, 16);

  send_byte_raw (fp, FRAMESYNCBYTE);
  for (idx=0; idx < 16; idx++)
    send_byte (fp, msg[idx]);
  send_byte (fp, crc >> 8);
  send_byte (fp, crc);
  fflush (fp);
}


static void
cmd_reset_shutter_eeprom (FILE *fp)
{
  byte msg[16];
  unsigned int crc;
  int idx;

  msg[0] = PROTOCOL_EBUS_H61;
  msg[1] = 0x10;
  msg[2] = 0x05;
  msg[3] = 0x01;
  msg[4] = 0x01;
  msg[5] = P_H61_SHUTTER;
  msg[6] = P_H61_SHUTTER_UPD_SCHEDULE;
  msg[7] = 0xf0;
  msg[8] = 0;
  msg[9] = 16;
  msg[10] = 0xf0;
  msg[11] = 0xf0;
  msg[12] = 0xf0;
  msg[13] = 0xf0;
  msg[14] = 0;
  msg[15] = 0;
  crc = compute_crc (msg, 16);

  send_byte_raw (fp, FRAMESYNCBYTE);
  for (idx=0; idx < 16; idx++)
    send_byte (fp, msg[idx]);
  send_byte (fp, crc >> 8);
  send_byte (fp, crc);
  fflush (fp);
}




static void
show_usage (const char *errtext)
{
  if (errtext)
    {
      err ("command line error: %s\n", errtext);
      exit (1);
    }
  fputs ("Usage: " PGM " DEVICE COMMAND\n"
         "Send message to housed\n\n"
         "  --speed N      Use given speed\n"
         "  --verbose      Enable extra informational output\n"
         "  --debug        Enable additional debug output\n"
         "  --help         Display this help and exit\n\n"
         "Report bugs to " PGM_BUGREPORT ".\n",
         stdout);
  exit (0);
}


int
main (int argc, char **argv )
{
  int last_argc = -1;
  FILE *fp;
  const char *cmd, *cmdarg1;

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
          show_usage (NULL);
        }
      else if (!strcmp (*argv, "--speed"))
        {
          argc--; argv++;
          if (argc)
            {
              line_speed = atoi (*argv);
              argc--; argv++;
            }
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
      else if (!strcmp (*argv, "--node"))
        {
          argc--; argv++;
          if (argc)
            {
              line_speed = atoi (*argv);
              argc--; argv++;
            }
          else
            show_usage ("argument missing");
        }
      else if (!strncmp (*argv, "--", 2))
        show_usage ("invalid option");
    }

  if (argc < 2)
    show_usage ("device or command missing");

  setvbuf (stdout, NULL, _IOLBF, 0);

  fp = open_line (*argv);
  cmd = argv[1];
  cmdarg1 = argc < 3? "": argv[2];
  if (!strcmp (cmd, "help"))
    {
      fputs ("help                     This help\n"
             "broadcast-time\n"
             "query-time\n"
             "query-version\n"
             "query-shutter-state\n"
             "query-shutter-schedule\n"
             "set-shutter-schedule SLOT TIMESPEC\n"
             "reset-shutter-eeprom\n"
             "drive-shutter up|down\n"
             ,stdout);
    }
  else  if (!strcmp (cmd, "broadcast-time"))
    cmd_broadcast_time (fp);
  else if (!strcmp (cmd, "query-time"))
    cmd_query_time (fp);
  else if (!strcmp (cmd, "query-version"))
    cmd_query_version (fp);
  else if (!strcmp (cmd, "query-shutter-state"))
    cmd_query_shutter_state (fp);
  else if (!strcmp (cmd, "query-shutter-schedule"))
    cmd_query_shutter_schedule (fp);
  else if (!strcmp (cmd, "set-shutter-schedule"))
    cmd_set_shutter_schedule (fp);
  else if (!strcmp (cmd, "reset-shutter-eeprom"))
    cmd_reset_shutter_eeprom (fp);
  else if (!strcmp (cmd, "drive-shutter"))
    cmd_drive_shutter (fp, cmdarg1);
  else
    err ("invalid command `%s'", cmd);


  fclose (fp);
  return any_error? 1:0;
}
