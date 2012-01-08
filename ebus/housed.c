/* housed.c - A house control daemon
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


#define PGM           "housed"
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


/* Log a decoded protocol message. */
static void
logmsg_start (const char *text)
{
  struct tm *tp;
  time_t atime = time (NULL);

  tp = localtime (&atime);
  fprintf (stdout, "%s_%02d:%02d:%02d [%s]",
           tp->tm_wday == 1? "Mon":
           tp->tm_wday == 2? "Tue":
           tp->tm_wday == 3? "Wed":
           tp->tm_wday == 4? "Thu":
           tp->tm_wday == 5? "Fri":
           tp->tm_wday == 6? "Sat": "Sun",
           tp->tm_hour, tp->tm_min, tp->tm_sec, text);
}

static void
logmsg_end ()
{
  fprintf (stdout, "\n");
  fflush (stdout);
}

static void
logmsg_addr (byte *msg, size_t msglen)
{
  fprintf (stdout, " %02x:%02x->%02x:%02x",
           msg[3], msg[4], msg[1], msg[2]);

  if (msg[3] == 0xff || msg[4] == 0xff)
    fputs ("[bad_sender_addr]", stdout);
}

static void
logmsg_fmt (const char *format, ...)
{
  va_list arg_ptr;

  va_start (arg_ptr, format);
  putc (' ', stdout);
  vfprintf (stdout, format, arg_ptr);
  va_end (arg_ptr);
}

static void
logmsg_time (unsigned int value, unsigned int decile)
{
  unsigned int day, hour, min, sec;

  day = (value/6/60/24);
  hour= (value/6/60 % 24);
  min = (value/6 % 60);
  sec = (value % 6) * 10;
  sec += decile/10;

  logmsg_fmt ("%s %02u:%02u:%02u.%u",
              day == 0? "Mon" :
              day == 1? "Tue" :
              day == 2? "Wed" :
              day == 3? "Thu" :
              day == 4? "Fri" :
              day == 5? "Sat" :
              day == 6? "Sun" : "[?]",
              hour, min, sec, decile%10);
}




/* Process test messages.  */
static void
process_ebus_test (byte *msg, size_t msglen)
{
  logmsg_start ("test");
  logmsg_fmt ("from %02x.%02x mode %02x", msg[1], msg[2], msg[3]);
  logmsg_time ((msg[4] << 8)|msg[5], 0);
  logmsg_fmt ("nrx %u", ((msg[6] << 8)|msg[7]));
  logmsg_fmt ("ntx %u", ((msg[8] << 8)|msg[9]));
  logmsg_fmt ("col %u", ((msg[10] << 8)|msg[11]));
  logmsg_fmt ("ovf %u", ((msg[12] << 8)|msg[13]));
  logmsg_fmt ("int %u", ((msg[14] << 8)|msg[15]));
  logmsg_end ();
}


/* Process debug messages.  */
static void
process_ebus_dbgmsg (byte *msg, size_t msglen)
{
  logmsg_start ("dbg");
  logmsg_fmt ("%02x:%02x->ff:ff \"%.13s\"", msg[1], msg[2], msg+3);
  logmsg_end ();
}


static void
p_busctl_time (byte *msg, size_t msglen, int have_decile)
{
  unsigned int value, decile;

  value = (msg[7] << 8 | msg[8]);
  decile = (have_decile || (msg[6] & 0x02))? msg[9]: 0;

  logmsg_time (value, decile);
  logmsg_fmt ("(%lu %u%s%s%s)",
              value, decile,
              (msg[6] & 0x04)? " dst":"",
              (msg[6] & 0x02)? " decile":"",
              (msg[6] & 0x01)? " exact":"");
}


static void
p_busctl_version (byte *msg, size_t msglen)
{
  logmsg_fmt ("nodetype=%u rev=\"%.7s\"%s", msg[6], msg+8,
              (msg[7] || msg[15])? "[reserved octets are not 0]":"");
}


static void
p_busctl_set_debug (byte *msg, size_t msglen)
{
  logmsg_fmt ("flags=%02x", msg[6]);
}


static void
p_busctl_qry_debug (byte *msg, size_t msglen)
{
  logmsg_fmt ("flags=%02x", msg[6]);
}


/* Process busctl messages.  */
static void
process_ebus_busctl (byte *msg, size_t msglen)
{
  char is_response = !!(msg[5] & P_BUSCTL_RESPMASK);

  logmsg_start ("bus");
  logmsg_addr (msg, msglen);

  switch ((msg[5] & ~P_BUSCTL_RESPMASK))
    {
    case P_BUSCTL_TIME:
      logmsg_fmt ("%s:TimeBroadcast", is_response?"Rsp":"Cmd");
      if (is_response)
        logmsg_fmt ("[invalid:response_flag_set]");
      else
        p_busctl_time (msg, msglen, 0);
      break;

    case P_BUSCTL_QRY_TIME:
      logmsg_fmt ("%s:QueryTime", is_response?"Rsp":"Cmd");
      if (is_response)
        p_busctl_time (msg, msglen, 1);
      break;

    case P_BUSCTL_QRY_VERSION:
      logmsg_fmt ("%s:QueryVersion", is_response?"Rsp":"Cmd");
      if (is_response)
        p_busctl_version (msg, msglen);
      break;

    case P_BUSCTL_SET_DEBUG:
      logmsg_fmt ("%s:SetDebug", is_response?"Rsp":"Cmd");
      if (!is_response)
        p_busctl_set_debug (msg, msglen);
      break;

    case P_BUSCTL_QRY_DEBUG:
      logmsg_fmt ("%s:QueryDebug", is_response?"Rsp":"Cmd");
      if (is_response)
        p_busctl_qry_debug (msg, msglen);
      break;

    default:
      logmsg_fmt ("%s:%02x", is_response?"Rsp":"Cmd", msg[5]);
      break;
    }
  logmsg_end ();
}


static void
p_h61_sensor_cmd (byte *msg, size_t msglen)
{
  switch (msg[6])
    {
    case P_H61_SENSOR_TEMPERATURE:
      logmsg_fmt ("Temperature(%u)", msg[7]);
      break;
    default:
      logmsg_fmt ("Type_%u", msg[6]);
      break;
    }
}


static void
p_h61_sensor_rsp (byte *msg, size_t msglen)
{
  int i;
  unsigned short val;

  switch (msg[6])
    {
    case P_H61_SENSOR_TEMPERATURE:
      logmsg_fmt ("Temperature: Group %u[%u]", (msg[7]&0x0f), (msg[7]>>4));
      for (i=8; i < 16; i += 2)
        {
          val = ((msg[i] << 8) | msg[i+1]);
          if (val == 0x8000)
            logmsg_fmt ("      -");
          else if (val == 0x7fff)
            logmsg_fmt ("  *err*");
          else
            logmsg_fmt (" %6.1f", val/10.0);
        }
      break;

    default:
      logmsg_fmt ("Type_%u", msg[6]);
      break;
    }
}


static void
p_h61_shutter_cmd (byte *msg, size_t msglen)
{
  switch (msg[6])
    {
    case P_H61_SHUTTER_QRY_SCHEDULE:
      logmsg_fmt ("QrySchedule(%u)", msg[7]);
      break;
    case P_H61_SHUTTER_UPD_SCHEDULE:
      logmsg_fmt ("UpdSchedule(%u): %u[%u] action 0x%02x ",
                  msg[7], msg[10], msg[9], msg[13]);
      logmsg_time ((msg[11] << 8)|msg[12], 0);
      break;
    default:
      logmsg_fmt ("Subcommand_%u", msg[6]);
      break;
    }
}

static void
p_h61_shutter_rsp (byte *msg, size_t msglen)
{
  switch (msg[6])
    {
    case P_H61_SHUTTER_QUERY:
      logmsg_fmt ("QryState: err=%d state=%02x",
                  msg[7], msg[8]);
      if ((msg[8] & 0xc0) == 0xc0)
        logmsg_fmt (" upwards");
      if ((msg[8] & 0xc0) == 0x80)
        logmsg_fmt (" downwards");
      if ((msg[8] & 0x20))
        logmsg_fmt (" %d%% closed", (msg[8] & 0x0f)*100/15);
      break;
    case P_H61_SHUTTER_QRY_SCHEDULE:
      logmsg_fmt ("QrySchedule(%u): %u[%u] action 0x%02x ",
                  msg[7], msg[10], msg[9], msg[13]);
      logmsg_time ((msg[11] << 8)|msg[12], 0);
      break;
    default:
      logmsg_fmt ("Subcommand_%u", msg[6]);
      break;
    }
}


/* Process H/61 messages.  */
static void
process_ebus_h61 (byte *msg, size_t msglen)
{
  char is_response = !!(msg[5] & P_H61_RESPMASK);

  logmsg_start ("h61");
  logmsg_addr (msg, msglen);

  switch ((msg[5] & ~P_H61_RESPMASK))
    {
    case P_H61_SHUTTER:
      logmsg_fmt ("%s:Shutter", is_response?"Rsp":"Cmd");
      if (is_response)
        p_h61_shutter_rsp (msg, msglen);
      else
        p_h61_shutter_cmd (msg, msglen);
       break;

    case P_H61_SENSOR:
      logmsg_fmt ("%s:Sensor", is_response?"Rsp":"Cmd");
      if (is_response)
        p_h61_sensor_rsp (msg, msglen);
      else
        p_h61_sensor_cmd (msg, msglen);
       break;

    default:
      logmsg_fmt ("%s:%02x", is_response?"Rsp":"Cmd", msg[5]);
      break;
    }
  logmsg_end ();
}


static void
process (FILE *fp)
{
  unsigned char buffer[48+2];
  int idx, synced, esc;
  int c, i;
  int msglen = 0;

  esc = synced = idx = 0;
  while ((c=getc (fp)) != EOF)
    {
      if (c == FRAMESYNCBYTE)
        {
          esc = 0;
          synced = 1;
          idx = 0;
        }
      else if (c == FRAMEESCBYTE && !esc)
        esc = 1;
      else if (synced)
        {
          if (esc)
            {
              esc = 0;
              c ^= FRAMEESCMASK;
            }

          if (!idx)
            {
              switch ((c & PROTOCOL_MSGLEN_MASK))
                {
                case PROTOCOL_MSGLEN_48: msglen = 48; break;
                case PROTOCOL_MSGLEN_32: msglen = 32; break;
                case PROTOCOL_MSGLEN_16: msglen = 16; break;
                default:
                  err ("reserved message length value encountered");
                  synced = 0;
                  continue;
                }
              buffer[idx++] = c;
            }
          else if (idx < msglen + 2)
            {
              buffer[idx++] = c;
              if (idx == msglen + 2)
                {
                  unsigned int crc;
                  int crcok;

                  crc = compute_crc (buffer, msglen);
                  crcok = ((crc >> 8) == buffer[msglen]
                           && (crc&0xff) == buffer[msglen+1]);

                  if (debug)
                    {
                      for (i=0; i < msglen + 2; i++)
                        printf ("%s%02x", i? " ":"", buffer[i]);
                      fputs (crcok? " ok":" bad", stdout);
                      putchar ('\n');
                      fflush (stdout);
                    }
                  if (crcok)
                    {
                      switch ((buffer[0] & 0xff))
                        {
                        case PROTOCOL_EBUS_BUSCTL:
                          process_ebus_busctl (buffer, msglen);
                          break;
                        case PROTOCOL_EBUS_H61:
                          process_ebus_h61 (buffer, msglen);
                          break;
                        case PROTOCOL_EBUS_DBGMSG:
                          process_ebus_dbgmsg (buffer, msglen);
                          break;
                        case PROTOCOL_EBUS_TEST:
                          process_ebus_test (buffer, msglen);
                          break;
                        default:
                          /* Ignore all other protocols.  */
                          break;
                        }
                    }
                }
            }
        }
    }
}



static int
show_usage (int ex)
{
  fputs ("Usage: " PGM " DEVICE\n"
         "Control an attached ebus\n\n"
         "  --speed N      Use given speed\n"
         "  --verbose      Enable extra informational output\n"
         "  --debug        Enable additional debug output\n"
         "  --help         Display this help and exit\n\n"
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
