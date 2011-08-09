/* hkpstats.c - Report stats about an HPK keyserver pool
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <adns.h>


#define PGM           "hkpstats"
#define PGM_VERSION   "0.0"
#define PGM_BUGREPORT "wk@gnupg.org"

/* Option flags. */
static int verbose;
static int debug;

/* Error counter.  */
static int any_error;

/* An object to keep track of each host. */
struct host_s
{
  struct host_s *next;

  const char *poolname;  /* Original poolname (e.g. "keys.gnupg.net") */
  struct in_addr addr;   /* Its IP address.  */
  char *addr_str;        /* Ditto but in human readabale format.  */

  struct {
    adns_query query;    /* Active query.  */
  } help;
};
typedef struct host_s *host_t;



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
process_one_pool (adns_state adns_ctx, const char *poolname)
{
  adns_answer *answer = NULL;
  int rridx;
  host_t host;
  host_t hostlist = NULL;
  in rc;

  inf ("collecting data for `%s'", poolname);
  
  if (adns_synchronous (adns_ctx, poolname,
                        adns_r_a, adns_qf_quoteok_query,
                        &answer) )
    {
      err ("DNS query for `%s' failed: %s", poolname, strerror (errno));
      return;
    }

  if (answer->status != adns_s_ok) 
    {
      err ("DNS query for `%s' failed: %s (%s)", poolname, 
           adns_strerror (answer->status),
           adns_errabbrev (answer->status)); 
      free (answer); 
      return;
    }
  assert (answer->type == adns_r_a);

  for (rridx=0; rridx < answer->nrrs; rridx++)
    {
      struct sockaddr_in sockaddr;
      struct in_addr addr = answer->rrs.inaddr[rridx];
      const char *s;

      host = calloc (1, sizeof *host);
      if (!host)
        die ("out of core: %s", strerror (errno));
      host->poolname = poolname;
      host->addr = addr;
      s = inet_ntoa (addr);
      host->addr_str = strdup (s?s:"[none]");
      if (!host->addr_str)
        die ("out of core: %s", strerror (errno));
        
      inf ("IP=%s", host->addr_str);

      memset (&sockaddr, 0,sizeof sockaddr);
      sockaddr.sin_family = AF_INET; 
      memcpy (&sockaddr.sin_addr, &addr, sizeof addr);
      if (adns_submit_reverse (adns_ctx, (struct sockaddr *)&sockaddr,
                               adns_r_ptr, 
                               adns_qf_quoteok_cname | adns_qf_cname_loose,
                               host, &host->help.query))
        {
          err ("DNS reverse lookup of `%s', %s failed: %s (%s)",
               poolname, host->addr_str,
               adns_strerror (answer->status),
               adns_errabbrev (answer->status)); 
          /*fixme: release_host (host);*/
        }
      else
        {
          host->next = hostlist;
          hostlist = host;
        }
    }
  free (answer);

  if (!hostlist)
    return;
    
  /* Wait until all hosts are resolved. */
  for (;;)
    {
      adns_query query = NULL;  

      rc = adns_check(adns_ctx, &query, &answer, &host);
      if (!rc)
        {


        }
      else if (err == EAGAIN) break;
      if (err) {
	fprintf(stderr, "%s: adns_wait/check: %s", progname, strerror(err));
	exit(1);
      }


      for (host = hostlist; host; host = host->next)
    {
      if (!host->help.query)
        continue;
    }
}


static int
show_usage (int ex)
{
  fputs ("Usage: " PGM " {pool}\n"
         "Generate a report for all keyservers in the POOLs.\n\n"
         "  --verbose      enable extra informational output\n"
         "  --debug        enable additional debug output\n"
         "  --help         display this help and exit\n\n"
         "Example:  \"" PGM " keys.gnupg.net http-keys.gnupg.net\"\n\n"
         "Report bugs to " PGM_BUGREPORT ".\n",
         ex? stderr:stdout);
  exit (ex);
}


int 
main (int argc, char **argv)
{
  int last_argc = -1;
  adns_state adns_ctx = NULL;

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

  if (argc < 1)
    show_usage (1);

  if (adns_init (&adns_ctx, adns_if_none, stderr))
    die ("error initializing ADNS: %s", strerror (errno));

  /* Note: Firther own we keep shallow copies of argv; thus don't
     modify them. */
  for (; argc; argc--, argv++)
    process_one_pool (adns_ctx, *argv);
  
  adns_finish (adns_ctx);


  return any_error? 1:0;
}


/*
Local Variables:
compile-command: "gcc -Wall -W -O2 -g -o hkpstats -ladns hkpstats.c"
End:
*/
