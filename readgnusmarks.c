/* readgnusmarks.c - Helper to convert nnml to Maildir
 *	Copyright (C) 2009 Werner Koch
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
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>

#define PGM           "readgnusmarks"
#define PGM_VERSION   "0.0"
#define PGM_BUGREPORT "wk@gnupg.org"

/* Option flags. */
static int verbose;
static int debug;
static const char *outputdir;

/* Error counter.  */
static int any_error;


typedef struct
{
  unsigned int in_use:1;
  unsigned int seen:1;
  unsigned int replied:1;
  unsigned int passed:1;
  unsigned int flagged:1;
} flags_t;

/* Number of flags we currently support.  If one ever needs to process
   messages with nnnml file number higher an additional data structure
   is required.  */
#define MAX_FLAGS 300000

/* An array of flags and a value indicating the allocated numer of
   elements.  */
static flags_t *flagarray;
static size_t flagarraysize;



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

/* Print an info message. */
static void
inf (const char *format, ...)
{
  va_list arg_ptr;

  if (!verbose)
    return;

  fflush (stdout);
  fprintf (stderr, "%s: ", PGM);

  va_start (arg_ptr, format);
  vfprintf (stderr, format, arg_ptr);
  va_end (arg_ptr);
  putc ('\n', stderr);
}



static void
set_mark (int num, int action)
{
  if (!action)
    return;

  if (num < 0 || (size_t)num >= flagarraysize)
    die ("nnml file number %d too high; maximum is %lu", 
         num, (unsigned long)flagarraysize);

  flagarray[num].in_use = 1;
  switch (action)
    {
    case 'S': flagarray[num].seen = 1; break;
    case 'R': flagarray[num].replied = 1; break;
    case 'P': flagarray[num].passed = 1; break;
    case 'F': flagarray[num].flagged = 1; break;
    }
}





static void
read_marks (const char *fname)
{
  FILE *fp;
  int c;
  int level = 0;
  int wait_for_action = 0;
  char token[50];
  size_t tokenidx = 0;
  int action = 0;
  int num, n_from, n_to, cons_state;

  fp = fopen (fname, "r");
  if (!fp)
    die ("failed to open `%s': %s", fname, strerror (errno));

  while ((c = getc (fp)) != EOF)
    {
      if (!isascii (c))
        die ("non ascii character found in `%s' - can't proceed", fname);
      if (isspace (c))
        ;
      else if (level < 0)
        die ("garbage at end of `%s' - can't proceed", fname);
      else if (c == ')')
        ;
      else if (c == '(')
        {
          level++;
          if (level > 3)
            die ("nesting too deep in `%s' - can't proceed", fname);
          if (wait_for_action && level == 3)
            die ("action missing in `%s' - can't proceed", fname);
          wait_for_action = (level == 2);
          if (level == 3)
            cons_state = n_from = n_to = 0;
          tokenidx = 0;
        }
      else
        {
          if (tokenidx+1 >= sizeof token)
            die ("token too long in `%s' - can't proceed", fname);
          token[tokenidx++] = c;
          continue;
        }
      
      token[tokenidx] = 0;
      if (tokenidx)
        {
          tokenidx = 0;
          if (wait_for_action)
            {
              wait_for_action = 0;
              inf ("got action token `%s'", token);
              if (!strcmp (token, "read"))
                action = 'S';
              else if (!strcmp (token, "reply"))
                action = 'R';
              else if (!strcmp (token, "forward"))
                action = 'P';
              else if (!strcmp (token, "tick"))
                action = 'F';
              else if (!strcmp (token, "save")
                       ||!strcmp (token, "dormant")
                       ||!strcmp (token, "killed"))
                {
                  inf ("action '%s' in `%s' - ignored", token, fname);
                  action = 0;
                }
              else 
                {
                  err ("unknown action '%s' in `%s' - skipped", token, fname);
                  action = 0;
                }
            }
          else if (level == 2)
            {
              num = atoi (token);
              if (num < 1)
                err ("bad number `%s' in `%s' - skipped", token, fname);
              else
                set_mark (num, action);
            }
          else if (level == 3)
            {
              if (*token == '.' && !token[1] && cons_state == 1)
                cons_state++;
              else
                {
                  num = atoi (token);
                  if (num < 1)
                    err ("bad number `%s' in `%s' - skipped", token, fname);
                  else if (!cons_state)
                    {
                      cons_state++;
                      n_from = num;
                    }
                  else if (cons_state == 2)
                    {
                      cons_state++;
                      n_to = num;
                      if (n_to < n_from)
                        err ("invalid range in `%s'", fname);
                      for (num = n_from; num <= n_to; num++)
                        set_mark (num, action);
                    }
                  else
                    err ("too many numbers in cons `%s' - skipped", fname);
                }
            }
        }
      if (c == ')')
        level--;
    }
  if (ferror (fp))
    die ("failed to read `%s': %s", fname, strerror (errno));

  fclose (fp);
}


static void
process_input (void)
{
  char oldname[1024];
  char newname[1024+50];
  size_t n;
  const char *s;
  char *endp;
  int num;
  int counter = 0;

  while (fgets (oldname, sizeof oldname, stdin))
    {
      n = strlen (oldname);
      if (n && oldname[n-1] == '\n')
        oldname[--n] = 0;
      s = oldname;
      num = (int)strtol (s, &endp, 10);
      if (num < 1 || *endp != '\0')
        {
          err ("bad file name structure '%s' - skipped", oldname);
          continue;
        }
      if (num < 0 || (size_t)num >= flagarraysize)
        {
          err ("nnml file number %d in `%s' too high - skipped", num, oldname);
          continue;
        }

      snprintf (newname, sizeof newname-50, "%s/cur/%lu.%d-%d", 
                outputdir,
                (unsigned long)time (NULL), num, ++counter);

      strcat (newname, ":2,");
      if (flagarray[num].in_use)
        {
           if (flagarray[num].flagged)
             strcat (newname, "F");
           if (flagarray[num].passed)
             strcat (newname, "P");
           if (flagarray[num].replied)
             strcat (newname, "R");
           if (flagarray[num].seen)
             strcat (newname, "S");
        }
      if ( !strcmp (oldname, newname) )
        {
          inf ("file `%s' not moved", oldname);
          continue;
        }
      printf ("mv '%s' '%s'\n", oldname, newname); 
    }
  if (ferror (stdin))
    die ("error reading from stdin: %s", strerror (errno));
}




static int
show_usage (int ex)
{
  fputs ("Usage: " PGM " <MARKSFILE> [OUTDIR]\n"
         "Read an nnml .marks file and rename Maildir files from stdin\n\n"
         "  --verbose      enable extra informational output\n"
         "  --debug        enable additional debug output\n"
         "  --help         display this help and exit\n\n"
         "This tool is used with the mv-nnml2maildir script.\n"
         "Report bugs to " PGM_BUGREPORT ".\n",
         ex? stderr:stdout);
  exit (ex);
}


int 
main (int argc, char **argv)
{
  int last_argc = -1;

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

  if (argc < 1 || argc > 2 )
    show_usage (1);
  if (argc == 2)
    outputdir = argv[1];
  else
    outputdir = ".";

  flagarraysize = MAX_FLAGS;
  flagarray = calloc (flagarraysize, sizeof *flagarray);
  if (!flagarray)
    die ("out of core: %s", strerror (errno));

  read_marks (*argv);
  
  process_input ();

  free (flagarray);

  return any_error? 1:0;
}


/*
Local Variables:
compile-command: "gcc -Wall -W -O2 -g -o readgnusmarks readgnusmarks.c"
End:
*/
