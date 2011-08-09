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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <termios.h>
#include <unistd.h>

#define PGM           "heating-daemon"
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


static void
run_loop (FILE *fp)
{
  char line[1024];
  size_t len;
  int c;

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
          fputs (line, stdout);
          putchar ('\n');
        }
      else
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
