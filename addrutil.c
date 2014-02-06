/* [addrutil.c wk 07.03.97] Tool to mess with address lists
 *	Copyright (c) 1997, 2003, 2014 Werner Koch (dd9jn)
 *	Copyright (C) 2000 OpenIT GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

/* $Id: addrutil.c,v 1.5 2005-07-13 13:25:32 werner Exp $ */

/* How to use:

This utility works on databases using this format:  Plain text file,
a hash mark in the first column denotes a comment line.  Fieldnames must start
with a letter in the first column and be terminated by a colon.  The
value of a fields starts after the colon, leding white spaces are ignored,
the value may be continued on the next line by prepending it with at least
one white space.  The first fieldname in a file acts as the record separator.
Fieldnames are case insensitive, duplkicated fieldnames are allowed (but not
for the first field) and inetrnally index by appendig a number.
Here is an example:
== addr.db ==============
# My address database (this is a comment)

Name: Alyssa Hacker
Email: alyssa@foo.net
Street: Cambridge Road 1
City: Foovillage

Name: Ben Bitfiddle
Street: Emacs Road 20
City: 234567  Gnutown
Email: ben@bar.org
Phone: 02222-33333

===========================================

This tool may be used to insert the values into a TeX file.  Here is an
example for such an TeX template:
== letter.tex ========
\documentclass[a4paper]{letter}
\usepackage[latin1]{inputenc}
\usepackage{ifthen}
\usepackage{german}
\begin{document}

%% the next line contains a pseudo field which marks the
%% start of a block which will be repeated for each record from the
%% database.  You way want to view this as a "while (not-database); do"
% @@begin-record-block@@

\begin{letter}{
@@Name@@ \\
@@Street@@ \\
@@City@@
}

\opening{Dear @@Email@@}
%% The variable substitution above knows about a special format, e.g.:
%% @@Email:N=,@@
%% where included linefeeds are replaced by the string after the equal
%% sign.

we are glad to invite you to our annual bit party at the Bit breweries,
located in Bitburg/Eifel.

\closing{Happy hacking}

\end{letter}

%% We are ready with this one record, and start over with the next one.
%% You may want to view this next-record statement as the closing "done"
%% done statement for the above while.
% @@next-record@@

\end{document}
======================

To send this letter to all the folks from the addr.db you whould use these
commands:

  addrutil -T letter.tex addr.db >a.tex && latex a.tex


To convert regular CSV data into our record format you may use this:

  addrutil --readcsv FILENAME

Add any number of -F options to name the fields.

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

#define PGMNAME "addrutil"
#define VERSION "0.71"
#define FIELDNAMELEN 40		/* max. length of a fieldname */

#ifdef __GNUC__
#define INLINE __inline__
#else
#define INLINE
#endif


typedef enum {
  SELECT_SAME,
  SELECT_NOTSAME,
  SELECT_SUB,
  SELECT_NOTSUB,
  SELECT_EQ, /* Numerically equal.  */
  SELECT_NE, /* Numerically not equal.  */
  SELECT_LE,
  SELECT_GE,
  SELECT_LT,
  SELECT_GT
} select_op_t;


typedef struct outfield_struct
{
  struct outfield_struct *next;
  char name[1];
} *OUTFIELD;


typedef struct selectexpr_struct
{
  struct selectexpr_struct *next;
  select_op_t op;
  const char *value;  /* Points into NAME.  */
  long numvalue;
  char name[1];
} *SELECTEXPR;


static struct
{
  int verbose;
  int debug;
  int checkonly;
  int readcsv;
  int format;
  const char *texfile;
  int sortmode;
  OUTFIELD outfields;
  SELECTEXPR selectexpr;
} opt;


typedef struct data_struct
{
  struct data_struct *next;
  int activ;			/* True if slot is in use. */
  int index;			/* Index number of this item.  */
  size_t size;			/* Available length of D. */
  size_t used;			/* Used length of D. */
  char d[1];			/* (this is not a string) */
} *DATA;

static DATA unused_data;	/* LL of unused data blocks. */

typedef struct field_struct
{
  struct field_struct *nextfield;
  int valid;			/* In current record.  */
  DATA data;			/* Data storage for this field.  */
  char name[1];			/* Extended to the correct length.  */
} *FIELD;


typedef struct sort_struct
{
  struct sort_struct *next;
  long offset;			/* of the record.  */
  char d[1];			/* Concatenated data used for sort.  */
} *SORT;


typedef struct namebucket_struct
{
  struct namebucket_struct *next;
  FIELD ptr;
} *NAMEBUCKET;

#define NO_NAMEBUCKETS 51
static NAMEBUCKET namebuckets[NO_NAMEBUCKETS];


static FIELD fieldlist;		/* Description of the record. */
        			/* The first field ist the record marker.  */
static FIELD next_field;	/* Used by GetFirst/NextField(). */
static OUTFIELD next_outfield;
static SORT sortlist;		/* Used when opt.sortmode activ.  */
static ulong output_count;
static long start_of_record;	/* Fileoffset of the current record.  */
static int new_record_flag;
static struct
{
  FILE *fp;
  int in_record_block;
  long begin_block;
  long end_block;
} tex;


typedef struct
{
  int *argc;			/* Pointer to argc (value subject to change) */
  char ***argv;			/* Pointer to argv (value subject to change) */
  unsigned flags;		/* Global flags (DO NOT CHANGE) */
  int err;			/* Print error about last option */
                                /*   1 = warning, 2 = abort */
  int r_opt;			/* Return option */
  int r_type;			/* Type of return value:   */
                                /*   0 = no argument found */
  union
  {
    int ret_int;
    long ret_long;
    ulong ret_ulong;
    char *ret_str;
  } r;				/* Return values */
  struct
  {
    int index;
    int inarg;
    int stopped;
    const char *last;
  } internal;
} ARGPARSE_ARGS;

typedef struct
{
  int short_opt;
  const char *long_opt;
  unsigned flags;
  const char *description;	/* Optional option description.  */
} ARGPARSE_OPTS;


static void set_opt_arg (ARGPARSE_ARGS * arg, unsigned flags, char *s);
static void show_help (ARGPARSE_OPTS * opts, unsigned flags);
static void show_version (void);

static INLINE unsigned long hash_name (const char *s);
static void dump_hash_infos (void);
static void log_error (int rc, const char *s, ...);
static void process (const char *filename);
static void read_and_print_csv (const char *filename);
static FIELD store_field_name (const char *fname, long offset);
static DATA expand_data_slot (FIELD field, DATA data);
static void new_record (long);
static FIELD get_first_field (void);
static FIELD get_next_field (void);
static void finish_record (void);
static void print_format2 (int flush);
static void print_tex_file (int);
static int process_tex_op (const char *op);
static void do_sort (void);
static int do_sort_fnc (const void *arg_a, const void *arg_b);

static const char *get_usage_str (int level);

static void
show_usage (int level)
{
  static int sentinel = 0;

  if (sentinel)
    return;

  sentinel++;
  if (!level)
    {
      fputs (get_usage_str (level), stderr);
      putc ('\n', stderr);
      fputs (get_usage_str (31), stderr);
      fprintf (stderr, "%s (%s)\n", get_usage_str (32), get_usage_str (24));
      fflush (stderr);
    }
  else if (level == 1)
    {
      fputs (get_usage_str (level), stderr);
      putc ('\n', stderr);
      exit (1);
    }
  else if (level == 2)
    {
      puts (get_usage_str (level));
      exit (0);
    }
  sentinel--;
}


static const char *
get_usage_str (int level)
{
  const char *p;
  switch (level)
    {
    case 10:
    case 0:
      p = "addrutil - v" VERSION "; " "Copyright (C) 2003 Werner Koch";
      break;
    case 13:
      p = "addrutil";
      break;
    case 14:
      p = VERSION;
      break;
    case 1:
    case 11:
      p = "Usage: addrutil [options] [files] (-h for help)";
      break;
    case 2:
    case 12:
      p =
	"\nSyntax: addrutil [options] [files]\n"
	"Handle address database files\n";
      break;
    case 19:
      p =			/* Footer */
	"Format modes:  0   Colon delimited fields\n"
	"               1   Colon delimited name=fields pairs\n"
	"               2   `Name',`Street',`City' formatted for labels\n"
	"               3   Addrutil format\n"
	"               4   Semicolon delimited format\n" "";
      break;
    default:
      p = "";
    }
  show_usage (level);
  return p;
}


static void *
xmalloc (size_t n)
{
  void *p = malloc (n);
  if (!p)
    {
      fprintf (stderr, PGMNAME ": out of memory\n");
      exit (2);
    }
  return p;
}

static void *
xcalloc (size_t n, size_t m)
{
  void *p = calloc (n, m);
  if (!p)
    {
      fprintf (stderr, PGMNAME ": out of memory\n");
      exit (2);
    }
  return p;
}


static void
strip_trailing_ws (char *str)
{
  char *p;
  char *mark;

  /* find last non space character */
  for (mark = NULL, p = str; *p; p++)
    {
      if (isspace (*(unsigned char *) p))
	{
	  if (!mark)
	    mark = p;
	}
      else
	mark = NULL;
    }
  if (mark)
    {
      *mark = '\0';		/* remove trailing spaces */
    }
}


/* Find string SUB in (BUFFER,BUFLEN).  */
static const char *
memstr (const void *buffer, size_t buflen, const char *sub)
{
  const char *buf = buffer;
  const char *t = buf;
  const char *s = sub;
  size_t n = buflen;

  for (; n; t++, n--)
    {
      if (*t == *s)
        {
          for (buf = t++, buflen = n--, s++; n && *t == *s; t++, s++, n--)
            ;
          if (!*s)
            return buf;
          t = buf;
          s = sub ;
          n = buflen;
	}
    }
  return NULL;
}



static int
arg_parse (ARGPARSE_ARGS * arg, ARGPARSE_OPTS * opts)
{
  int index;
  int argc;
  char **argv;
  char *s, *s2;
  int i;

  if (!(arg->flags & (1 << 15)))
    {				/* initialize this instance */
      arg->internal.index = 0;
      arg->internal.last = NULL;
      arg->internal.inarg = 0;
      arg->internal.stopped = 0;
      arg->err = 0;
      arg->flags |= 1 << 15;	/* mark initialized */
      if (*arg->argc < 0)
	abort ();		/*Invalid argument for ArgParse */
    }
  argc = *arg->argc;
  argv = *arg->argv;
  index = arg->internal.index;

  if (arg->err)
    {				/* last option was erroneous */
      /* FIXME: We could give more help on the option if opts->desription
       * is used. Another possibility ist, to autogenerate the help
       * from these descriptions. */
      if (arg->r_opt == -3)
	s = PGMNAME ": missing argument for option \"%.50s\"\n";
      else
	s = PGMNAME ": invalid option \"%.50s\"\n";
      fprintf (stderr, s, arg->internal.last ? arg->internal.last : "[??]");
      if (arg->err != 1)
	exit (2);
      arg->err = 0;
    }

  if (!index && argc && !(arg->flags & (1 << 4)))
    {				/* skip the first entry */
      argc--;
      argv++;
      index++;
    }

 next_one:
  if (!argc)
    {				/* no more args */
      arg->r_opt = 0;
      goto leave;		/* ready */
    }

  s = *argv;
  arg->internal.last = s;

  if (arg->internal.stopped && (arg->flags & (1 << 1)))
    {
      arg->r_opt = -1;		/* not an option but a argument */
      arg->r_type = 2;
      arg->r.ret_str = s;
      argc--;
      argv++;
      index++;			/* set to next one */
    }
  else if (arg->internal.stopped)
    {				/* ready */
      arg->r_opt = 0;
      goto leave;
    }
  else if (*s == '-' && s[1] == '-')
    {				/* long option */
      arg->internal.inarg = 0;
      if (!s[2] && !(arg->flags & (1 << 3)))
	{			/* stop option processing */
	  arg->internal.stopped = 1;
	  argc--;
	  argv++;
	  index++;
	  goto next_one;
	}

      for (i = 0; opts[i].short_opt; i++)
	if (opts[i].long_opt && !strcmp (opts[i].long_opt, s + 2))
	  break;

      if (!opts[i].short_opt && !strcmp ("help", s + 2))
	show_help (opts, arg->flags);
      else if (!opts[i].short_opt && !strcmp ("version", s + 2))
	show_version ();
      else if (!opts[i].short_opt && !strcmp ("warranty", s + 2))
	{
	  puts (get_usage_str (10));
	  puts (get_usage_str (31));
	  exit (0);
	}

      arg->r_opt = opts[i].short_opt;
      if (!opts[i].short_opt)
	{
	  arg->r_opt = -2;	/* unknown option */
	  arg->r.ret_str = s + 2;
	}
      else if ((opts[i].flags & 7))
	{
	  s2 = argv[1];
	  if (!s2 && (opts[i].flags & 8))
	    {			/* no argument but it is okay */
	      arg->r_type = 0;	/* because it is optional */
	    }
	  else if (!s2)
	    {
	      arg->r_opt = -3;	/* missing argument */
	    }
	  else if (*s2 == '-' && (opts[i].flags & 8))
	    {
	      /* the argument is optional and the next seems to be
	       * an option. We do not check this possible option
	       * but assume no argument */
	      arg->r_type = 0;
	    }
	  else
	    {
	      set_opt_arg (arg, opts[i].flags, s2);
	      argc--;
	      argv++;
	      index++;		/* skip one */
	    }
	}
      else
	{			/* does not take an argument */
	  arg->r_type = 0;
	}
      argc--;
      argv++;
      index++;			/* set to next one */
    }
  else if ((*s == '-' && s[1]) || arg->internal.inarg)
    {				/* short option */
      int dash_kludge = 0;
      i = 0;
      if (!arg->internal.inarg)
	{
	  arg->internal.inarg++;
	  if (arg->flags & (1 << 5))
	    {
	      for (i = 0; opts[i].short_opt; i++)
		if (opts[i].long_opt && !strcmp (opts[i].long_opt, s + 1))
		  {
		    dash_kludge = 1;
		    break;
		  }
	    }
	}
      s += arg->internal.inarg;

      if (!dash_kludge)
	{
	  for (i = 0; opts[i].short_opt; i++)
	    if (opts[i].short_opt == *s)
	      break;
	}

      if (!opts[i].short_opt && *s == 'h')
	show_help (opts, arg->flags);

      arg->r_opt = opts[i].short_opt;
      if (!opts[i].short_opt)
	{
	  arg->r_opt = -2;	        /* unknown option */
	  arg->internal.inarg++;	/* point to the next arg */
	  arg->r.ret_str = s;
	}
      else if ((opts[i].flags & 7))
	{
	  if (s[1] && !dash_kludge)
	    {
	      s2 = s + 1;
	      set_opt_arg (arg, opts[i].flags, s2);
	    }
	  else
	    {
	      s2 = argv[1];
	      if (!s2 && (opts[i].flags & 8))
		{		        /* no argument but it is okay */
		  arg->r_type = 0;	/* because it is optional */
		}
	      else if (!s2)
		{
		  arg->r_opt = -3;	/* missing argument */
		}
	      else if (*s2 == '-' && s2[1] && (opts[i].flags & 8))
		{
		  /* the argument is optional and the next seems to be
		   * an option. We do not check this possible option
		   * but assume no argument */
		  arg->r_type = 0;
		}
	      else
		{
		  set_opt_arg (arg, opts[i].flags, s2);
		  argc--;
		  argv++;
		  index++;	/* skip one */
		}
	    }
	  s = "x";		/* so that !s[1] yields false */
	}
      else
	{			/* does not take an argument */
	  arg->r_type = 0;
	  arg->internal.inarg++;/* point to the next arg */
	}
      if (!s[1] || dash_kludge)
	{			/* no more concatenated short options */
	  arg->internal.inarg = 0;
	  argc--;
	  argv++;
	  index++;
	}
    }
  else if (arg->flags & (1 << 2))
    {
      arg->r_opt = -1;		/* not an option but a argument */
      arg->r_type = 2;
      arg->r.ret_str = s;
      argc--;
      argv++;
      index++;			/* set to next one */
    }
  else
    {
      arg->internal.stopped = 1;/* stop option processing */
      goto next_one;
    }

 leave:
  *arg->argc = argc;
  *arg->argv = argv;
  arg->internal.index = index;
  return arg->r_opt;
}



static void
set_opt_arg (ARGPARSE_ARGS * arg, unsigned flags, char *s)
{
  int base = (flags & 16) ? 0 : 10;

  switch (arg->r_type = (flags & 7))
    {
    case 1:			/* takes int argument */
      arg->r.ret_int = (int) strtol (s, NULL, base);
      break;
    default:
    case 2:			/* takes string argument */
      arg->r.ret_str = s;
      break;
    case 3:			/* takes long argument   */
      arg->r.ret_long = strtol (s, NULL, base);
      break;
    case 4:			/* takes ulong argument  */
      arg->r.ret_ulong = strtoul (s, NULL, base);
      break;
    }
}

static void
show_help (ARGPARSE_OPTS * opts, unsigned flags)
{
  const char *s;

  puts (get_usage_str (10));
  s = get_usage_str (12);
  if (*s == '\n')
    s++;
  puts (s);
  if (opts[0].description)
    {				/* auto format the option description */
      int i, j, indent;
      /* get max. length of long options */
      for (i = indent = 0; opts[i].short_opt; i++)
	{
	  if (opts[i].long_opt)
	    if ((j = strlen (opts[i].long_opt)) > indent && j < 35)
	      indent = j;
	}
      /* example: " -v, --verbose   Viele Sachen ausgeben" */
      indent += 10;
      puts ("Options:");
      for (i = 0; opts[i].short_opt; i++)
	{
	  if (opts[i].short_opt < 256)
	    printf (" -%c", opts[i].short_opt);
	  else
	    fputs ("   ", stdout);
	  j = 3;
	  if (opts[i].long_opt)
	    j += printf ("%c --%s   ", opts[i].short_opt < 256 ? ',' : ' ',
			 opts[i].long_opt);
	  for (; j < indent; j++)
	    putchar (' ');
	  if ((s = opts[i].description))
	    {
	      for (; *s; s++)
		{
		  if (*s == '\n')
		    {
		      if (s[1])
			{
			  putchar ('\n');
			  for (j = 0; j < indent; j++)
			    putchar (' ');
			}
		    }
		  else
		    putchar (*s);
		}
	    }
	  putchar ('\n');
	}
      if (flags & 32)
	puts ("\n(A single dash may be used instead of the double ones)");
    }
  if ((s = get_usage_str (19)))
    {				/* bug reports to ... */
      putchar ('\n');
      fputs (s, stdout);
    }
  fflush (stdout);
  exit (0);
}

static void
show_version ()
{
  const char *s;
  printf ("%s version %s (%s", get_usage_str (13), get_usage_str (14),
	  get_usage_str (45));
  if ((s = get_usage_str (24)) && *s)
    {
      printf (", %s)\n", s);
    }
  else
    {
      printf (")\n");
    }
  fflush (stdout);
  exit (0);
}

/* Supported expressions are:

   [<ws>]NAME[<ws>]<op>[<ws>]VALUE[<ws>]

   NAME and VALUE may not be the empty string. <ws> indicates white
   space.  [] indicates optional parts.  If VALUE starts with one of
   the characters used in any <op> a space after the <op> is required.
   Valid <op> are:

      =~  Substring must match
      !~  Substring must not match
      =   The full string must match
      <>  The full string must not match
      ==  The numerical value must match
      !=  The numerical value must not match
      <=  The numerical value of the field must be LE than the value.
      <   The numerical value of the field must be LT than the value.
      >=  The numerical value of the field must be GT than the value.
      >=  The numerical value of the field must be GE than the value.

      Numerical values are computed as long int.  */


static SELECTEXPR
parse_selectexpr (const char *expr)
{
  SELECTEXPR se;
  const char *s0, *s;

  while (*expr == ' ' || *expr == '\t')
    expr++;

  se = xmalloc (sizeof *se + strlen (expr));
  se->next = NULL;
  strcpy (se->name, expr);

  s = strpbrk (expr, "=<>!~");
  if (!s || s == expr )
    log_error (1, "%s: no field name given for select\n", PGMNAME);
  s0 = s;

  if (!strncmp (s, "=~", 2))
    {
      se->op = SELECT_SUB;
      s += 2;
    }
  else if (!strncmp (s, "!~", 2))
    {
      se->op = SELECT_NOTSUB;
      s += 2;
    }
  else if (!strncmp (s, "<>", 2))
    {
      se->op = SELECT_NOTSAME;
      s += 2;
    }
  else if (!strncmp (s, "==", 2))
    {
      se->op = SELECT_EQ;
      s += 2;
    }
  else if (!strncmp (s, "!=", 2))
    {
      se->op = SELECT_NE;
      s += 2;
    }
  else if (!strncmp (s, "<=", 2))
    {
      se->op = SELECT_LE;
      s += 2;
    }
  else if (!strncmp (s, ">=", 2))
    {
      se->op = SELECT_GE;
      s += 2;
    }
  else if (!strncmp (s, "<", 1))
    {
      se->op = SELECT_LT;
      s += 1;
    }
  else if (!strncmp (s, ">", 1))
    {
      se->op = SELECT_GT;
      s += 1;
    }
  else if (!strncmp (s, "=", 1))
    {
      se->op = SELECT_SAME;
      s += 1;
    }
  else
    log_error (1, "%s: invalid select operator\n", PGMNAME);

  /* We require that a space is used if the value starts with any of
     the operator characters.  */
  if (strchr ("=<>!~", *s))
    log_error (1, "%s: invalid select operator\n", PGMNAME);
  while (*s == ' ' || *s == '\t')
    s++;
  if (!*s)
    log_error (1, "%s: no value given for select\n", PGMNAME);

  se->name[s0 - expr] = 0;
  strip_trailing_ws (se->name);
  if (!se->name[0])
    log_error (1, "%s: no field name given for select\n", PGMNAME);

  strip_trailing_ws (se->name + (s - expr));
  se->value = se->name + (s - expr);
  if (!se->value[0])
    log_error (1, "%s: no value given for select\n", PGMNAME);

  se->numvalue = strtol (se->value, NULL, 10);

  return se;
}


int
main (int argc, char **argv)
{
  ARGPARSE_OPTS opts[] = {
    {'f', "format", 1, "use output format N"},
    {'s', "sort", 0, "sort the file"},
    {'S', "select", 2, "output records matching expression" },
    {'F', "field", 2, "output this field"},
    {'T', "tex-file", 2, "use TeX file as template"},
    {'c', "check-only", 0, "do only a syntax check"},
    { 501, "readcsv",   0, "read CSV data" },
    {'v', "verbose", 0, "verbose"},
    {'d', "debug", 0, "increase the debug level"},
    {0}
  };
  ARGPARSE_ARGS pargs = { &argc, &argv, 0 };
  int org_argc;
  char **org_argv;
  OUTFIELD of, of2;
  SELECTEXPR se;

  while (arg_parse (&pargs, opts))
    {
      switch (pargs.r_opt)
	{
	case 'v':
	  opt.verbose++;
	  break;
	case 'd':
	  opt.debug++;
	  break;
	case 'c':
	  opt.checkonly++;
	  break;
	case 's':
	  opt.sortmode = 1;
	  break;
	case 'f':
	  opt.format = pargs.r.ret_int;
	  break;
	case 'T':
	  opt.texfile = pargs.r.ret_str;
	  break;
	case 'F':
	  of = xmalloc (sizeof *of + strlen (pargs.r.ret_str));
	  of->next = NULL;
	  strcpy (of->name, pargs.r.ret_str);
	  if (!(of2 = opt.outfields))
	    opt.outfields = of;
	  else
	    {
	      for (; of2->next; of2 = of2->next)
		;
	      of2->next = of;
	    }
	  break;
        case 501:
          opt.readcsv = 1;
          break;
	case 'S':
          se = parse_selectexpr (pargs.r.ret_str);
          if (se)
            {
              se->next = opt.selectexpr;
              opt.selectexpr = se;
            }
	  break;
	default:
	  pargs.err = 2;
	  break;
	}
    }

  if (opt.selectexpr && opt.debug)
    {
      FILE *fp = stderr;

      fputs ("--- Begin selectors ---\n", fp);
      for (se=opt.selectexpr; se; se = se->next)
        fprintf (fp, "*(%s) %s '%s'\n",
                 se->name,
                 se->op == SELECT_SAME?    "= ":
                 se->op == SELECT_NOTSAME? "<>":
                 se->op == SELECT_SUB?     "=~":
                 se->op == SELECT_NOTSUB?  "!~":
                 se->op == SELECT_EQ?      "==":
                 se->op == SELECT_NE?      "!=":
                 se->op == SELECT_LT?      "< ":
                 se->op == SELECT_LE?      "<=":
                 se->op == SELECT_GT?      "> ":
                 se->op == SELECT_GE?      ">=":"[oops]",
                 se->value);
      fputs ("--- End selectors ---\n", fp);
    }

  if (opt.readcsv)
    {
      if (!argc)
        read_and_print_csv (NULL);
      else
        {
          for (; argc; argc--, argv++)
            read_and_print_csv (*argv);
        }
      exit (0);
    }

  if (opt.texfile)
    {
      tex.fp = fopen (opt.texfile, "r");
      if (!tex.fp)
	{
	  fprintf (stderr, PGMNAME ": failed to open `%s': %s\n",
		   opt.texfile, strerror (errno));
	  exit (1);
	}
    }

  if (opt.sortmode && argc != 1)
    {
      fprintf (stderr,
	       PGMNAME ": sorry, sorting is only available for one file\n");
      exit (1);
    }

  org_argc = argc;
  org_argv = argv;

 pass_two:
  if (!argc)
    process (NULL);
  else
    {
      for (; argc; argc--, argv++)
	process (*argv);
    }

  if (opt.texfile)
    {
      if (tex.in_record_block && opt.sortmode != 1)
	{
	  print_tex_file (1);
	}
    }
  else if (opt.format == 2 && opt.sortmode != 1)
    print_format2 (1);		/* flush */

  if (opt.sortmode == 1 && sortlist)
    {
      do_sort ();
      argc = org_argc;
      argv = org_argv;
      opt.sortmode = 2;
      goto pass_two;
    }
  else if (opt.sortmode == 2)
    {
      /* FIXME: cleanup the sort infos */
    }

  if (opt.debug)
    {
      FIELD f;
      DATA d;
      FILE *fp = stderr;
      int n;

      fputs ("--- Begin fieldlist ---\n", fp);
      for (f = fieldlist; f; f = f->nextfield)
	{
	  n = fprintf (fp, "%.20s:", f->name);
	  for (d = f->data; d; d = d->next)
	    fprintf (fp, "%*s idx=%-3d used=%-3u size=%-3u %s\n",
		     d == f->data ? 0 : n, "", d->index,
                     (unsigned int)d->used, (unsigned int)d->size,
		     d->activ ? "activ" : "not-active");
	  if (!f->data)
	    putc ('\n', fp);
	}
      fputs ("--- End fieldlist ---\n", fp);
      dump_hash_infos ();
    }
  return 0;
}


static INLINE unsigned long
hash_name (const char *s_arg)
{
  const unsigned char *s = (const unsigned char*)s_arg;
  unsigned long hashVal = 0;
  unsigned long carry;

  if (s)
    for (; *s; s++)
      {
	hashVal = (hashVal << 4) + toupper (*s);
	if ((carry = (hashVal & 0xf0000000)))
	  {
	    hashVal ^= (carry >> 24);
	    hashVal ^= carry;
	  }
      }

  return hashVal % NO_NAMEBUCKETS;
}


static void
dump_hash_infos ()
{
  int i, sum, perBucket, n;
  NAMEBUCKET r;

  perBucket = sum = 0;
  for (i = 0; i < NO_NAMEBUCKETS; i++)
    {
      for (n = 0, r = namebuckets[i]; r; r = r->next)
	n++;
      sum += n;
      if (n > perBucket)
	perBucket = n;
    }
  fprintf (stderr,
	   "%d entries in %d hash buckets; max. %d entr%s per hash bucket\n",
	   sum, NO_NAMEBUCKETS, perBucket, perBucket == 1 ? "y" : "ies");
}


static void
log_error (int rc, const char *s, ...)
{
  va_list arg_ptr;
  FILE *fp = stderr;

  va_start (arg_ptr, s);
  vfprintf (fp, s, arg_ptr);
  putc ('\n', fp);
  va_end (arg_ptr);
  if (rc)
    exit (rc);
}


static void
process (const char *filename)
{
  FILE *fp;
  int c;
  unsigned long lineno = 0;
  long lineoff = 0;		/* offset of the current line */
  int newline;
  int comment = 0;
  int linewrn = 0;
  char fname[FIELDNAMELEN + 1];
  int fnameidx = 0;
  int index;			/* current index */
  enum
  { sINIT,			/* no record yet */
    sFIELD,			/* inside a fieldname */
    sDATABEG,			/* waiting for start of value */
    sDATA			/* storing a value */
  } state = sINIT;
  FIELD f = NULL;		/* current field */
  DATA d = NULL;		/* current data slot */
  SORT sort = sortlist;
  int pending_lf = 0;
  int skip_kludge = 0;

  if (filename)
    {
      fp = fopen (filename, "r");
      if (!fp)
	{
	  fprintf (stderr, PGMNAME ": failed to open `%s': %s\n",
		   filename, strerror (errno));
	  exit (1);
	}
    }
  else
    {
      fp = stdin;
      filename = "[stdin]";
    }

  if (opt.sortmode == 2)
    {
      if (!sort)
	return;			/* nothing to sort */
    next_sortrecord:
      if (!sort)
	goto ready;
      clearerr (fp);
      if (fseek (fp, sort->offset, SEEK_SET))
	{
	  fprintf (stderr, PGMNAME ": error seekung to %ld\n", sort->offset);
	  exit (2);
	}
      sort = sort->next;
      state = sINIT;
      skip_kludge = 1;
    }

  /* Read the file byte by byte; do not impose a limit on the
   * line length. Fieldnames are up to FIELDNAMELEN bytes long.
   */
  lineno++;
  newline = 1;
  while ((c = getc (fp)) != EOF)
    {
      if (c == '\n')
	{
	  switch (state)
	    {
	    case sFIELD:
	      log_error (2, "%s:%ld: fieldname not terminated",
                         filename, lineno);
	      break;
	    case sDATA:
	      pending_lf++;
	      break;
	    default:
	      break;
	    }
	  lineno++;
	  lineoff = ftell (fp) - 1;
	  newline = 1;
	  comment = 0;
	  linewrn = 0;
	  continue;
	}
      else if (comment)
	continue;

      if (newline)
	{			/* at first column */
	  if (c == '#')
	    comment = 1;	/* bybass the entire line */
	  else if (c == ' ' || c == '\t')
	    {
	      switch (state)
		{
		case sINIT:
		  break;	/* nothing to do */
		case sFIELD:
		  abort ();
		case sDATABEG:
		  break;
		case sDATA:
		  state = sDATABEG;
		  break;
		}
	    }
	  else if (c == ':')
	    log_error (2, "%s:%ld: line starts with a colon", filename, lineno);
	  else
	    {
	      switch (state)
		{
		case sDATABEG:
		case sDATA:
		  /*FinishField(); */
		  /* fall thru */
		case sINIT:	/* start of a fieldname */
		  fnameidx = 0;
		  fname[fnameidx++] = c;
		  state = sFIELD;
		  break;
		case sFIELD:
		  abort ();
		}
	    }
	  newline = 0;
	}
      else
	{
	  switch (state)
	    {
	    case sINIT:
	      if (!linewrn)
		{
		  log_error (0, "%s:%lu: warning: garbage detected",
                             filename, lineno);
		  linewrn++;
		}
	      break;
	    case sFIELD:
	      if (c == ':')
		{
		  char *p;

		  fname[fnameidx] = 0;
		  strip_trailing_ws (fname);
		  if ((p = strrchr (fname, '.')))
		    {
		      *p++ = 0;
		      strip_trailing_ws (fname);
		      index = atoi (p);
		      if (index < 0 || index > 255)
			log_error (2, "%s:%lu: invalid index of fieldname",
                                   filename, lineno);
		    }
		  else
		    index = 0;	/* must calculate an index */
		  if (!*fname)
		    log_error (2, "%s:%lu: empty fieldname", filename, lineno);
		  new_record_flag = 0;
		  f = store_field_name (fname, lineoff);
		  if (opt.sortmode == 2 && new_record_flag && !skip_kludge)
		    goto next_sortrecord;
		  skip_kludge = 0;
		  if (!index)
		    { /* detect the index */
		      /* first a shortcut: */
		      if ((d = f->data) && d->index == 1 && !d->activ)
			index = 1;	/* that's it */
		      else
			{ /* find the highest unused index */
			  for (index = 1; d;)
			    {
			      if (d->index == index)
				{
				  if (d->activ)
				    {
				      index++;
				      d = f->data;
				    }
				  else
				    break;
				}
			      else
				d = d->next;
			    }
			}
		    }
		  else
		    { /* find a data slot for the given index. */
		      for (d = f->data; d; d = d->next)
			if (d->index == index)
			  break;
		      if (d && d->activ)
			log_error (0, "%s:%lu: warning: %s.%d redefined",
                                   filename, lineno, fname, index);
		    }
		  if (!d)
		    { /* create a new slot */
		      if ((d = unused_data))
			unused_data = d->next;
		      else
			{
			  d = xmalloc (sizeof *d + 100);
			  d->size = 100 + 1;
			}
		      d->index = index;
		      d->next = NULL;
		      if (!f->data)
			f->data = d;
		      else
			{
			  DATA d2;
			  for (d2 = f->data; d2->next; d2 = d2->next)
			    ;
			  d2->next = d;
			}
		    }
		  d->activ = 1;
		  d->used = 0;	/* used length */
		  pending_lf = 0;
		  state = sDATABEG;
		}
	      else
		{
		  if (fnameidx >= FIELDNAMELEN)
		    log_error (2, "%s:%ld: fieldname too long",
                               filename, lineno);
		  fname[fnameidx++] = c;
		}
	      break;
	    case sDATABEG:
	      if (c == ' ' || c == '\t')
		break;
	      state = sDATA;
	      /* fall thru */
	    case sDATA:
	      if (!d)
		abort ();
	      for (; pending_lf; pending_lf--)
		{
		  if (d->used >= d->size)
		    d = expand_data_slot (f, d);
		  d->d[d->used++] = '\n';
		}
	      if (d->used >= d->size)
		d = expand_data_slot (f, d);
	      d->d[d->used++] = c;
	      break;
	    } /* end switch state after first column */
	}
    }
  if (ferror (fp))
    {
      fprintf (stderr, PGMNAME ":%s:%lu: read error: %s\n",
	       filename, lineno, strerror (errno));
      exit (2);
    }
  if (!newline)
    {
      log_error (0, "%s: warning: last line not terminated by a LF", filename);
    }
  if (opt.sortmode == 2)
    goto next_sortrecord;

 ready:
  finish_record ();
  lineno--;
  if (opt.verbose)
    log_error (0, "%s: %lu line%s processed", filename, lineno,
               lineno == 1 ? "" : "s");

  if (fp != stdin)
    fclose (fp);
}


static void
read_and_print_csv (const char *filename)
{
  FILE *fp;
  int c;
  unsigned long lineno;
  int newline, newfield, in_string, any_printed;
  int fieldidx = 0;
  OUTFIELD of;

  if (filename)
    {
      fp = fopen (filename, "r");
      if (!fp)
	{
	  fprintf (stderr, PGMNAME ": failed to open `%s': %s\n",
		   filename, strerror (errno));
	  exit (1);
	}
    }
  else
    {
      fp = stdin;
      filename = "[stdin]";
    }

  /* Read the file byte by byte; do not impose a limit on the line
   * length.  Fieldnames are up to FIELDNAMELEN bytes long.  */
  lineno  = 1;
  newline = 1;
  newfield = 0;
  in_string = 0;
  any_printed = 0;
  of = NULL;
  while ((c = getc (fp)) != EOF)
    {
      if (c == '\r')
        continue;

      if (c == '\n')  /* End of line.  */
	{
          putchar ('\n');
          putchar ('\n');
	  lineno++;
	  newline = 1;
          newfield = 0;
          in_string = 0;
          fieldidx = 0;
	  continue;
	}

      if (newline) /* At a new record.  */
        {
	  newline = 0;
          newfield = 1;
          of = opt.outfields? opt.outfields : NULL;
	}

      if (newfield)
        {
          if (of)
            printf ("%s:", of->name);
          else
            printf ("Field_%d:", fieldidx);
          newfield = 0;
          any_printed = 0;
          in_string = 0;
        }

      if (in_string == 1)
        {
          if (c == '\"')
            in_string = 2; /* Possible end of string.  */
          else
            {
              if (!any_printed)
                {
                  any_printed = 1;
                  putchar (' ');
                }
              putchar (c);
            }
        }
      else if (in_string == 2)
        {
          if (c == '\"' )
            {
              putchar ('\"');
              in_string = 1;
            }
          else
            in_string = 0;
        }

      if (in_string)
        ;
      else if (c == '\"')
        {
          in_string = 1;
        }
      else if (c == ',')
        {
          putchar ('\n');
          fieldidx++;
          newfield = 1;
          if (of)
            of = of->next;
        }
      else
        {
          if (!any_printed)
            {
              any_printed = 1;
              putchar (' ');
            }
          putchar (c);
        }
    }

  if (ferror (fp))
    {
      fprintf (stderr, PGMNAME ":%s:%lu: read error: %s\n",
	       filename, lineno, strerror (errno));
      exit (2);
    }
  if (!newline)
    {
      log_error (0, "%s: warning: last line not terminated by a LF", filename);
    }

  lineno--;
  if (opt.verbose)
    log_error (0, "%s: %lu line%s processed", filename, lineno,
               lineno == 1 ? "" : "s");

  if (fp != stdin)
    fclose (fp);
}


/*
 * Handle the fieldname.
 *
 * If we already have a field with this name in the current record, we
 * append a counter to the field (e.g. "Phone.1", "Phone.2", ... )
 * where a counter of 1 is same as the filed without a
 * count. Filednames are NOT casesensitiv. index o means: calculate an
 * index if this is an unknown field.
 *
 * Returns: a pointer to the field
 */
static FIELD
store_field_name (const char *fname, long offset)
{
  unsigned long hash;
  NAMEBUCKET buck;
  FIELD fdes, f2;

  for (buck = namebuckets[hash = hash_name (fname)]; buck; buck = buck->next)
    if (!strcasecmp (buck->ptr->name, fname))
      {
	fdes = buck->ptr;
	break;
      }

  if (buck && fdes == fieldlist)
    new_record (offset);
  else if (!buck)
    { /* A new fieldname.  */
      fdes = xcalloc (1, sizeof *fdes + strlen (fname));
      strcpy (fdes->name, fname);
      /* Create a hash entry to speed up field access.  */
      buck = xcalloc (1, sizeof *buck);
      buck->ptr = fdes;
      buck->next = namebuckets[hash];
      namebuckets[hash] = buck;
      /* Link the field into the record description.  */
      if (!fieldlist)
	fieldlist = fdes;
      else
	{
	  for (f2 = fieldlist; f2->nextfield; f2 = f2->nextfield)
	    ;
	  f2->nextfield = fdes;
	}
    }
  fdes->valid = 1; /* This is in the current record.  */
  return fdes;
}



/*
 * Replace the data slot DATA by an larger one.
 */
static DATA
expand_data_slot (FIELD field, DATA data)
{
  DATA d, d2;

  for (d = unused_data; d; d = d->next)
    if (d->size > data->size)
      break;
  if (!d)
    {
      d = xmalloc (sizeof *d + data->size + 200);
      d->size = data->size + 200 + 1;
    }
  memcpy (d->d, data->d, data->used);
  d->used = data->used;
  d->index = data->index;
  d->activ = data->activ;
  d->next = data->next;
  /* Link it into the field list.   */
  if (field->data == data)
    field->data = d;
  else
    {
      for (d2 = field->data; d2; d2 = d2->next)
	if (d2->next == data)
	  break;
      if (!d2)
	abort (); /* Data not linked to field.  */
      d2->next = d;
    }
  data->next = unused_data;
  unused_data = data;
  return d;
}


/*
 * Begin a new record after closing the last one.
 */
static void
new_record (long offset)
{
  finish_record ();
  start_of_record = offset;
  new_record_flag = 1;
}


static FIELD
get_first_field ()
{
  FIELD f;
  OUTFIELD of;

  if (opt.outfields)
    {
      of = opt.outfields;
      for (f = fieldlist; f; f = f->nextfield)
	if (!strcmp (f->name, of->name))
	  {
	    next_outfield = of;
	    return f;
	  }
      next_outfield = NULL;
      return NULL;
    }
  return (next_field = fieldlist);
}

static FIELD
get_next_field ()
{
  FIELD f;
  OUTFIELD of;

  if (opt.outfields)
    {
      if (next_outfield && (of = next_outfield->next))
	{
	  for (f = fieldlist; f; f = f->nextfield)
	    if (!strcmp (f->name, of->name))
	      {
		next_outfield = of;
		return f;
	      }
	}
      next_outfield = NULL;
      return NULL;
    }
  return next_field ? (next_field = next_field->nextfield) : NULL;
}


/* Return true if the record has been selected.  Note that the
   selection currently considers only the first active line of a given
   field.  */
static int
select_record_p (void)
{
  FIELD f;
  SELECTEXPR se;
  DATA d;
  const char *value;
  size_t selen, valuelen, n;
  long numvalue;
  int result = 1;

  for (se=opt.selectexpr; se; se = se->next)
    {
      for (f = fieldlist; f; f = f->nextfield)
        if (!strcmp (f->name, se->name))
          break;
      if (!f || !f->valid)
        continue;
      for (d = f->data; d; d = d->next)
        if (d->activ)
          break;
      if (!d)
        {
          value = "";
          valuelen = 0;
          numvalue = 0;
        }
      else
        {
          char tmpbuf[25];
          value = d->d;
          valuelen = d->used;
          n = valuelen;
          if (n > sizeof tmpbuf - 1)
            n = sizeof tmpbuf -1;
          memcpy (tmpbuf, value, n);
          tmpbuf[n] = 0;
          numvalue = strtol (tmpbuf, NULL, 10);
        }
      selen = strlen (se->value);

      switch (se->op)
        {
        case SELECT_SAME:
          result = (valuelen == selen && !memcmp (value, se->value, selen));
          break;
        case SELECT_NOTSAME:
          result = !(valuelen == selen && !memcmp (value, se->value, selen));
          break;
        case SELECT_SUB:
          result = !!memstr (value, valuelen, se->value);
          break;
        case SELECT_NOTSUB:
          result = !!memstr (value, valuelen, se->value);
          break;
        case SELECT_EQ:
          result = (numvalue == se->numvalue);
          break;
        case SELECT_NE:
          result = (numvalue != se->numvalue);
          break;
        case SELECT_GT:
          result = (numvalue > se->numvalue);
          break;
        case SELECT_GE:
          result = (numvalue >= se->numvalue);
          break;
        case SELECT_LT:
          result = (numvalue < se->numvalue);
          break;
        case SELECT_LE:
          result = (numvalue <= se->numvalue);
          break;
        }
      if (!result)
        break;
    }

  return result;
}




/*
 * If we are in a record: close the current record.
 */
static void
finish_record ()
{
  FIELD f;
  DATA d = NULL;
  int any = 0;
  size_t n;
  char *p;
  int indent;

  if (!opt.checkonly && fieldlist && fieldlist->valid)
    {
      /* There is a valid record */
      if (opt.sortmode == 1)
	{ /* Store only.  */
	  SORT sort;

	  n = 0;
	  for (f = fieldlist; f; f = f->nextfield)
	    if (f->valid)
	      for (d = f->data; d; d = d->next)
		if (d->activ)
		  {
		    n = d->used;
		    goto okay;
		  }
	okay:
	  sort = xcalloc (1, sizeof *sort + n + 1);
	  sort->offset = start_of_record;
	  memcpy (sort->d, d->d, n);
	  sort->d[n] = 0; /* Make it a string.  */
	  sort->next = sortlist;
	  sortlist = sort;
	}
      else if (opt.selectexpr && !select_record_p ())
        ;
      else if (opt.texfile)
	{
	  print_tex_file (0);
	}
      else if (opt.format == 0)
	{
	  for (f = get_first_field (); f; f = get_next_field ())
	    {
	      if (f->valid)
		{
		  int need_tab = 0;
		  for (d = f->data; d; d = d->next)
		    {
		      if (d->activ)
			{
			  const char *s;
			  int i;

			  if (need_tab)
			    putchar ('\t');
			  else if (any)
			    putchar (':');
			  for (i = 0, s = d->d; i < d->used; s++, i++)
			    {
			      if (*s == '%')
				fputs ("%25", stdout);
			      else if (*s == ':')
				fputs ("%3A", stdout);
			      else if (*s == '\n')
				fputs ("%0A", stdout);
			      else if (*s == '\t')
				putchar (' ');
			      else
				putchar (*s);
			    }
			  any = 1;
			  need_tab = 1;
			}
		      else if (any && !need_tab)
			putchar (':');
		    }
		}
	      else
		{
		  if (any)
		    putchar (':');
		  else
		    any++;
		}
	    }
	  putchar ('\n');
	}
      else if (opt.format == 1)
	{
	  for (f = get_first_field (); f; f = get_next_field ())
	    {
	      if (f->valid)
		{
		  for (d = f->data; d; d = d->next)
		    if (d->activ)
		      {
			if (d->index != 1)
			  printf ("%s%s.%d='%.*s'", any ? ":" : "",
				  f->name, d->index, (int) d->used, d->d);
			else
			  printf ("%s%s='%.*s'", any ? ":" : "",
				  f->name, (int) d->used, d->d);
			any = 1;
		      }
		}
	    }
	  putchar ('\n');
	}
      else if (opt.format == 2)
	{
	  print_format2 (0);
	}
      else if (opt.format == 3)
	{
	  for (f = get_first_field (); f; f = get_next_field ())
	    {
	      if (f->valid)
		{
		  for (d = f->data; d; d = d->next)
		    if (d->activ)
		      {
			any = 1;
			indent = printf ("%s: ", f->name);
			for (n = 0, p = d->d; n < d->used; n++, p++)
			  if (*p == '\n')
			    break;
			if (n < d->used) /* Multi-line output.  */
			  {
			    for (n = 0, p = d->d; n < d->used; n++, p++)
			      {
				putchar (*p);
				if (*p == '\n')
				  printf ("%*s", indent, "");
			      }
			  }
			else /* Singe-line output.  */
			  {
			    printf ("%.*s", (int) d->used, d->d);
			  }
			putchar ('\n');
		      }
		}
	    }
	  if (any)
	    putchar ('\n');
	}
      else if (opt.format == 4) /* ';' delimited */
	{
	  for (f = get_first_field (); f; f = get_next_field ())
	    {
	      if (any)
		putchar (';');
	      if (f->valid)
		{
		  int any2 = 0;
		  for (d = f->data; d; d = d->next)
		    if (d->activ)
		      {
			if (any2)
			  putchar ('|');
			any = 1;
			any2 = 1;
			for (n = 0, p = d->d; n < d->used; n++, p++)
			  {
			    if (*p == '\n')
			      putchar (' ');
			    else if (*p == ';')
			      putchar (',');
			    else
			      putchar (*p);
			  }
		      }
		}
	    }
	  if (any)
	    putchar ('\n');
	}
    }
  output_count++;
  for (f = fieldlist; f; f = f->nextfield)
    {
      f->valid = 0;
      /* Set the data blocks inactive.  */
      for (d = f->data; d; d = d->next)
	d->activ = 0;
    }
}



static void
print_format2 (int flushit)
{
  static int pending = 0;
  static int totlines = 0;
  static char *names[] = { "Name", "Street", "City", NULL };
  NAMEBUCKET buck;
  FIELD f = NULL;
  DATA d;
  int n, len, lines = 0;
  const char *name;
  static char buffers[3][40];

  if (pending && totlines > 58)
    {
      putchar ('\f');
      totlines = 0;
    }
  if (flushit && pending)
    {
      for (n = 0; (name = names[n]); n++)
	{
	  printf ("%-40s\n", buffers[n]);
	  lines++;
	  totlines++;
	}
    }

  for (n = 0; !flushit && (name = names[n]); n++)
    {
      for (buck = namebuckets[hash_name (name)]; buck; buck = buck->next)
	if (!strcasecmp (buck->ptr->name, name))
	  {
	    f = buck->ptr;
	    break;
	  }
      if (!f)
	continue;

      for (d = f->data; d; d = d->next)
	if (d->activ && d->index == 1)
	  break;
      if (!d)
	continue;
      if ((len = (int) d->used) > 38)
	len = 38;

      if (!pending)
	sprintf (buffers[n], "%.*s", len, d->d);
      else
	{
	  printf ("%-40s%.*s\n", buffers[n], len, d->d);
	  lines++;
	  totlines++;
	}
    }
  if (pending)
    {
      for (; lines < 5; lines++, totlines++)
	putchar ('\n');
    }
  if (flushit)
    {
      pending = 0;
      totlines = 0;
    }
  else
    pending = !pending;
}


static void
print_tex_file (int flushit)
{
  char pseudo_op[200];
  int c, pseudo_op_idx = 0;
  int state = 0;

  if (flushit && tex.end_block)
    {
      if (fseek (tex.fp, tex.end_block, SEEK_SET))
	{
	  fprintf (stderr, PGMNAME ": error seeking to offset %ld\n",
		   tex.end_block);
	  exit (1);
	}
    }

  while ((c = getc (tex.fp)) != EOF)
    {
      switch (state)
	{
	case 0:
	  if (c == '@')
	    state = 1;
	  else
	    putchar (c);
	  break;
	case 1:
	  if (c == '@')
	    {
	      state = 2;
	      pseudo_op_idx = 0;
	    }
	  else
	    {
	      putchar ('@');
	      ungetc (c, tex.fp);
	      state = 0;
	    }
	  break;
	case 2:		/* pseudo-op start */
	  if (pseudo_op_idx >= sizeof (pseudo_op) - 1)
	    {
	      fprintf (stderr, PGMNAME ": pseudo-op too long\n");
	      exit (1);
	    }
	  else if (c == '\n')
	    {
	      fprintf (stderr, PGMNAME ": invalid pseudo-op - ignored\n");
	      pseudo_op[pseudo_op_idx] = 0;
	      fputs (pseudo_op, stdout);
	      putchar ('\n');
	      state = 0;
	    }
	  else if (c == '@'
		   && pseudo_op_idx && pseudo_op[pseudo_op_idx - 1] == '@')
	    {
	      pseudo_op[pseudo_op_idx - 1] = 0;
	      state = 0;
	      if (!flushit && process_tex_op (pseudo_op))
		return;
	    }
	  else
	    pseudo_op[pseudo_op_idx++] = c;
	  break;

	default:
	  abort ();
	}
    }
  if (c == EOF)
    {
      if (ferror (tex.fp))
	{
	  fprintf (stderr, PGMNAME ":%s: read error\n", opt.texfile);
	  exit (1);
	}
      else if (state)
	{
	  fprintf (stderr, PGMNAME ":%s: unclosed pseudo-op\n", opt.texfile);
	}
    }

}


static int
process_tex_op (const char *op)
{
  NAMEBUCKET buck;
  FIELD f;
  DATA d;

  if (!strcasecmp (op, "begin-record-block"))
    {
      tex.in_record_block = 1;
      tex.begin_block = ftell (tex.fp);
    }
  else if (!strcasecmp (op, "end-record-block"))
    {
      tex.in_record_block = 0;
    }
  else if (!strcasecmp (op, "next-record") && tex.in_record_block)
    {
      tex.end_block = ftell (tex.fp);
      if (fseek (tex.fp, tex.begin_block, SEEK_SET))
	{
	  fprintf (stderr, PGMNAME ": error seeking to offset %ld\n",
		   tex.begin_block);
	  exit (1);
	}
      return 1;
    }
  else if (!tex.in_record_block)
    {
      fprintf (stderr,
	       PGMNAME ": pseudo op '%s' not allowed in this context\n", op);
    }
  else /* Take it as the key to the record data. */
    {
      char *p = strchr (op, ':');

      if (p)
	*p++ = 0; /* Strip modifier. */

      f = NULL;
      for (buck = namebuckets[hash_name (op)]; buck; buck = buck->next)
	if (!strcasecmp (buck->ptr->name, op))
	  {
	    f = buck->ptr;
	    break;
	  }
      if (f) /* We have an entry with this name.  */
	{
	  for (d = f->data; d; d = d->next)
	    if (d->activ)
	      {
		printf ("%s", d->index > 1 ? "\\par " : "");
		if (p && !strncmp (p, "N=", 2))
		  {
		    size_t n;

		    for (n = 0; n < d->used; n++)
		      if (d->d[n] == '\r')
			;
		      else if (d->d[n] == '\n')
			fputs (p + 2, stdout);
		      else
			putchar (((unsigned char *) d->d)[n]);
		  }
		else
		  printf ("%.*s", (int) d->used, d->d);
	      }
	}
    }
  return 0;
}


/*
 * Sort the sortlist
 */
static void
do_sort ()
{
  size_t i, n;
  SORT s, *array;

  for (n = 0, s = sortlist; s; s = s->next)
    n++;
  if (!n)
    return;
  array = xmalloc ((n + 1) * sizeof *array);
  for (n = 0, s = sortlist; s; s = s->next)
    array[n++] = s;
  array[n] = NULL;
  qsort (array, n, sizeof *array, do_sort_fnc);
  sortlist = array[0];
  for (i = 0; i < n; i++)
    array[i]->next = array[i + 1];
}

static int
do_sort_fnc (const void *arg_a, const void *arg_b)
{
  SORT a = *(SORT *) arg_a;
  SORT b = *(SORT *) arg_b;
  return strcmp (a->d, b->d);
}

/*
Local Variables:
compile-command: "cc -Wall -O2 -o addrutil addrutil.c"
End:
*/
