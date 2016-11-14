/* heating-daemon.c - Collect data from heating-control.c
 * Copyright (C) 2010 Werner Koch
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE  /* We use asprintf */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>

#define PGM           "heating-daemon"
#define PGM_VERSION   "0.1"
#define PGM_BUGREPORT "wk@gnupg.org"

#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))
#define DIMof(type,member)   DIM(((type *)0)->member)

/* Option flags. */
static int verbose;
static int debug;

/* Error counter.  */
static int any_error;

/* Current state of the system.  */
struct state_s
{
  int day, hour, minute;
  unsigned int mode;
  unsigned int burner_on : 1;
  unsigned int pump_on : 1;
  unsigned int system_secs;
  unsigned int burner_secs;

  int target_dc;
  int boiler_dc;
  int outside_dc;
};
struct state_s last_state, current_state;

/* Name of the file with the HTML status page.  */
static char *status_page_fname;

/* The time the burner has been started or stopped.  */
static time_t burner_start_time, burner_stop_time;

/* The time the burner is on or off.  */
static unsigned int burner_on_secs, burner_off_secs;

/* The temperature or the boiler when the burner started.  */
static int boiler_dc_at_burner_start;

/* The time the burner went ito alert state.  */
static time_t burner_alert_time;


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


/* Split a string into colon delimited fields and remove leading and
 * trailing spaces from each field.  A pointer to each field is stored
 * in ARRAY.  Stop splitting at ARRAYSIZE fields.  The function
 * modifies STRING.  The number of parsed fields is returned.
 * Example:
 *
 *   char *fields[2];
 *   if (split_fields (string, fields, DIM (fields)) < 2)
 *     return ; // Not enough args.
 *   foo (fields[0]);
 *   foo (fields[1]);
 */
int
split_fields (char *string, char **array, int arraysize)
{
  int n = 0;
  char *p, *pend;

  for (p = string; *p == ' '; p++)
    ;
  do
    {
      if (n == arraysize)
        break;
      array[n++] = p;
      pend = strchr (p, ':');
      if (!pend)
        break;
      *pend++ = 0;
      for (p = pend; *p == ' '; p++)
        ;
    }
  while (*p);

  return n;
}


static FILE *
open_line (void)
{
  FILE *fp;
  int fd;
  struct termios term;

  /* FIXME get device lock.  */

  fp = fopen ("/dev/ttyS0", "r+");
  if (!fp || (fd = fileno (fp)) == -1)
    die ("can't open `%s': %s", "/dev/ttyS0", strerror (errno));

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

  inf ("connected to '%s' at 9600bps", "/dev/ttyS0");

  return fp;
}


static const char *
day_to_weekday (int d)
{
  switch (d)
    {
    case 0: return "Monday";
    case 1: return "Tuesday";
    case 2: return "Wednesday";
    case 3: return "Thursday";
    case 4: return "Firday";
    case 5: return "Saturday";
    case 6: return "Sunday";
    default: return "?";
    }
}


static void
print_html_page (void)
{
  FILE *fp;

  fp = fopen (status_page_fname, "w");
  if (!fp)
    {
      err ("can't create '%s': %s", status_page_fname, strerror (errno));
      return;
    }
  fputs ("<html>\n"
         "<head>\n"
         "<title>Heating system</title>\n"
         " <meta http-equiv=\"Content-Type\""
         " content=\"text/html;charset=utf-8\" />\n"
         " <meta http-equiv=\"refresh\" content=\"10\" />\n"
         "</head>\n"
         "<body>\n", fp);

  fprintf (fp, "<h1>%s %02d:%02d</h1>\n",
           day_to_weekday (current_state.day),
           current_state.hour,
           current_state.minute);
  fputs ("<table>\n", fp);
  fprintf (fp, "<tr><td>Outside</td><td>%5.1f°C</td></tr>\n",
           (float)current_state.outside_dc/10);
  fprintf (fp, "<tr><td>Boiler</td><td>%5.1f°C</td></tr>\n",
           (float)current_state.boiler_dc/10);
  fprintf (fp, "<tr><td>Desired</td><td>%5.1f°C</td></tr>\n",
           (float)current_state.target_dc/10);

  if (current_state.burner_on)
    fprintf (fp, "<tr><td>Burner</td><td>on (%u s)%s</td></tr>\n",
             burner_on_secs, burner_alert_time? " <strong>Alert</strong>":"");
  else
    fprintf (fp, "<tr><td>Burner</td><td>off (%u s)</td></tr>\n",
             burner_off_secs);


  fprintf (fp, "<tr><td>Pump</td><td>%s</td></tr>\n",
           current_state.pump_on? "on":"off");
  fprintf (fp, "<tr><td>Mode</td><td>%s</td></tr>\n",
           current_state.mode == 0 ? "Night" :
           current_state.mode == 1 ? "Day" :
           current_state.mode == 1 ? "Absent" : "Off");
  fputs ("</table>\n", fp);

  fputs ("<table>\n", fp);
  fprintf (fp, "<tr><td>System running</td><td>%u:%02u</td></tr>\n",
           current_state.system_secs / 3600,
           (current_state.system_secs %3600 ) / 60);
  fprintf (fp, "<tr><td>Burner firing</td><td>%u:%02u</td></tr>\n",
           current_state.burner_secs / 3600,
           (current_state.burner_secs % 3600) / 60);
  fputs ("</table>\n", fp);

  fputs ("</body>\n"
         "</html>\n", fp);

  fclose (fp);
}


/* Evaluate state and update html status page.  */
static void
evaluate_state (void)
{
  time_t now;

  if (!memcmp (&last_state, &current_state, sizeof (current_state)))
    return;  /* No change in state.  */

  now = time (NULL);

  if (!last_state.burner_on && current_state.burner_on)
    {
      burner_start_time = now;
      boiler_dc_at_burner_start = current_state.boiler_dc;
    }
  else if (last_state.burner_on && !current_state.burner_on)
    burner_stop_time = now;

  if (current_state.burner_on)
    {
      burner_on_secs  = now - burner_start_time;
      burner_off_secs = 0;
      if (current_state.boiler_dc > boiler_dc_at_burner_start)
        burner_alert_time = 0;
      else if (burner_on_secs > 60
          && current_state.boiler_dc < boiler_dc_at_burner_start)
        burner_alert_time = now;
    }
  else
    {
      burner_on_secs = 0;
      burner_off_secs = now - burner_stop_time;
      burner_alert_time = 0;
    }

  print_html_page ();

  /* Remember the state.  */
  last_state = current_state;
}



/* This is the format of a status line:
 *
 * s:6:10:51:9291:1:1:1:1:::::19535:13001:
 *   ^ ^  ^  ^    ^ ^ ^ ^     ^     ^
 *   | |  |  |    | | | |     |     +- burner running in 2sec units.
 *   | |  |  |    | | | |     +------- running time in 2 sec units.
 *   | |  |  |    | | | +------------- circulation pump on
 *   | |  |  |    | | +--------------- burner on
 *   | |  |  |    | +----------------- group 1 selected
 *   | |  |  |    +------------------- operation mode is 1
 *   | |  |  +------------------------ minute of the week
 *   | |  +--------------------------- minute
 *   | +------------------------------ hour
 *   +-------------------------------- weekday (0=monday)
 */
static void
process_s_line (char *line)
{
  char *fields[15];

  if (split_fields (line, fields, DIM (fields)) < 15)
    return; /* Not enough fields.  */

  current_state.day    = atoi (fields[1]);
  current_state.hour   = atoi (fields[2]);
  current_state.minute = atoi (fields[3]);
  /* minute of the week */
  current_state.mode   = atoi (fields[5]);
  /* group */
  current_state.burner_on   = !!atoi (fields[7]);
  current_state.pump_on     = !!atoi (fields[8]);
  current_state.system_secs = strtoul (fields[13], NULL, 10) * 2;
  current_state.burner_secs = strtoul (fields[14], NULL, 10) * 2;
}


/* This is the format of a time line (deci-Celsius).  A time line
 * follows a status line.
 *
 * t:592:625:68:::68:0:
 *   ^   ^   ^    ^  ^
 *   |   |   |    |  +--- outside temperature from sensor 1
 *   |   |   |    +------ outside temperature from sensor 0
 *   |   |   +----------- outside temperature (averaged)
 *   |   +--------------- boiler temperature
 *   +------------------- target temperature
 */
static void
process_t_line (char *line)
{
  char *fields[4];

  if (split_fields (line, fields, DIM (fields)) < 4)
    return; /* Not enough fields.  */

  current_state.target_dc = atoi (fields[1]);
  current_state.boiler_dc = atoi (fields[2]);
  current_state.outside_dc= atoi (fields[3]);
}


static void
run_loop (FILE *fp)
{
  char line[1024];
  size_t len;
  int c;
  time_t lasttime = 0;
  time_t curtime;

  fseek (fp, 0L, SEEK_CUR);
  fputs ("\r\n\r\nAT+M1\r\n", fp);
  fseek (fp, 0L, SEEK_CUR);

  while (fgets (line, sizeof line, fp))
    {
      len = strlen (line);
      if (!len || line[len-1] != '\n')
        {
          err ("line too long - skipping");
          while ((c = getc (fp)) != EOF && c != '\n')
            ;
          continue;
        }
      while (len && (line[len-1] == '\n' || line[len-1] == '\r'))
        len--;
      line[len] = 0;

      if (*line && line[1] == ':')
        {
          /* Insert a timestamp line every minute.  */
          curtime = time (NULL);
          if (curtime >= lasttime + 60)
            {
              lasttime = curtime;
              printf ("$:%lu:\n", (unsigned long)curtime);
            }

          /* Print the line top stdout.  */
          fputs (line, stdout);
          putchar ('\n');

          /* Process the line.  */
          if (*line == 's')
            process_s_line (line);
          else if (*line == 't')
            {
              process_t_line (line);
              evaluate_state ();
            }

        }
      else /* Print an arbitrary message. */
        inf ("message: %s", line);
    }

  if (ferror (fp))
    err ("processing stopped due to a read error: %s", strerror (errno));
  else
    inf ("processing stopped due to an EOF");
}



static int
show_usage (int ex)
{
  fputs ("Usage: " PGM "\n"
         "Control the heating controller and collect data.\n\n"
         "  --verbose      enable extra informational output\n"
         "  --debug        enable additional debug output\n"
         "  --help         display this help and exit\n\n"
         "Report bugs to " PGM_BUGREPORT ".\n",
         ex? stderr:stdout);
  exit (ex);
}


int
main (int argc, char **argv)
{
  int last_argc = -1;
  FILE *fp;
  const char *s;

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

  if (argc)
    show_usage (1);

  setvbuf (stdout, NULL, _IOLBF, 0);

  s = getenv ("HOME");
  if (!s)
    die ("envvar HOME not set");
  if (asprintf (&status_page_fname, "%s/public_html/heating.html", s) < 0)
    die ("asprintf failed");

  burner_start_time = burner_stop_time = time (NULL);

  fp = open_line ();
  run_loop (fp);
  fclose (fp);

  return any_error? 1:0;
}


/*
Local Variables:
compile-command: "gcc -Wall -W -O2 -g -o heating-daemon heating-daemon.c"
End:
*/
