/* webbsh.c - Webbuilder Shell, a chroot command for the webbuilder account.
 * Copyright (C) 2002 g10 Code GmbH
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#define ACCOUNT "webbuilder"

#ifdef _POSIX_OPEN_MAX
#define MAX_OPEN_FDS _POSIX_OPEN_MAX
#else
#define MAX_OPEN_FDS 256
#endif


static void 
usage (void)
{
  fprintf (stderr, "usage: webbsh directory commands\n");
  exit (1);
}

static void *
xmalloc (size_t n)
{
  void *p = malloc (n);
  if (!n)
    {
      fprintf (stderr, "webbsh: out of core\n");
      exit (1);
    }
  return p;
}

static void *
xcalloc (size_t n, size_t m)
{
  void *p = xmalloc (n*m);
  memset (p, 0, n*m);
  return p;
}


/*  static char * */
/*  xstrdup (const char *s) */
/*  { */
/*    char *p = xmalloc (strlen (s)+1); */
/*    strcpy (p, s); */
/*    return p; */
/*  } */


static char **
build_env (void)
{
  int n = 0;
  char **envp = xcalloc (10, sizeof *envp);
  
  envp[n++] = "HOME=/";
  envp[n++] = "SHELL=/bin/sh";  
  envp[n++] = "LOGNAME=" ACCOUNT;
  envp[n++] = "PATH=/bin:/usr/bin";

  return envp;
}



int 
main (int argc, char **argv)
{
  const char *directory;
  struct passwd *pw;
  int i, n;
  gid_t dummy_grplist[1];
  char **envp;

  if (argc < 2)
    usage ();
  argc--; argv++;
  if ( !strcmp (*argv, "--help") )
    usage ();
  directory = *argv;
  argc--; argv++;
  if (*directory != '/')
    {
      fprintf (stderr, "webbsh: directory must be given as absolute path\n");
      exit (1);
    }

  pw = getpwnam (ACCOUNT);
  if (!pw)
    { 
      fprintf (stderr, "webbsh: no user `%s'\n", ACCOUNT);
      exit (1);
    }
  if ( getuid () != pw->pw_uid || getgid () != pw->pw_gid)
    { 
      fprintf (stderr, "webbsh: not run as user `%s'\n", ACCOUNT);
      exit (1);
    }

  if (getgroups (0, dummy_grplist) )
    { 
      fprintf (stderr, "webbsh: user `%s' must not"
                       " have any supplementary groups\n", ACCOUNT);
      exit (1);
    }
  if ( getegid () != getgid () )
    { 
      fprintf (stderr, "webbsh: must not be run sgid\n");
      exit (1);
    }


  /* Close all files except for the standard ones. */
  n = sysconf (_SC_OPEN_MAX);
  if (n < 0)
    n = MAX_OPEN_FDS;
  for (i=0; i < n; i++)
    {
      if (i == STDIN_FILENO || i == STDOUT_FILENO || i == STDERR_FILENO)
        continue;
      close(i);
    }
  errno = 0;
    
  if (chdir (directory))
    { 
      fprintf (stderr, "webbsh: chdir `%s' failed: %s\n", directory, 
               strerror (errno));
      exit (1);
    }

  if (chroot (directory))
    { 
      fprintf (stderr, "webbsh: chroot `%s' failed: %s\n", directory, 
               strerror (errno));
      exit (1);
    }

  /* Give up privileges. */
  if (setuid ( getuid () ) || getuid () != geteuid() || !setuid(0) )
    { 
      fprintf (stderr, "webbsh: failed to give up permissions: %s\n",  
               strerror (errno));
      exit (1);
    }

  if (!geteuid () || !getuid ())
    abort (); /* we are pretty paranoid. */

  if (argv[argc])
    abort (); /* should never happen. */

  envp = build_env ();

  if (!argc)
    {
      fputs ("webbsh: no command given - would use this environment:\n", stderr);
      for (i=0; envp[i]; i++)
        fprintf (stderr, "%s\n", envp[i]);
      exit (0);
    }


  if (execve (argv[0], argv, envp))
    {
      fprintf (stderr, "webbsh: failed to run `%s': %s\n", argv[0],  
               strerror (errno));
      exit (1);
    }
  abort (); /*NOTREACHED*/
  return 0;
}

/*
Local Variables:
compile-command: "cc -Wall -o webbsh webbsh.c"
End:
*/

