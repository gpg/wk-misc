/* scrutmime.c - Look at MIME mails.
 *	Copyright (C) 2004 g10 Code GmbH
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


/* Utility to help filter out spam.  This one identifies attachments. */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "rfc822parse.h"


#define PGM "scrutmime"
#define VERSION "1.0"

/* Option flags. */
static int verbose;
static int quiet;
static int debug;
static int opt_match_zip;
static int opt_match_exe;
static int opt_match_html;


enum mime_types 
  {
    MT_NONE = 0,
    MT_OCTET_STREAM,
    MT_AUDIO,
    MT_IMAGE,
    MT_TEXT_HTML
  };

enum transfer_encodings
  {
    TE_NONE = 0,
    TE_BASE64
  };


/* Structure used to communicate with the parser callback. */
struct parse_info_s {
  enum mime_types mime_type;
  enum transfer_encodings transfer_encoding;
  int test_base64; /* Set if we should decode and test base64 data. */
  int got_probe;
};


/* Base64 conversion tables. */
static unsigned char bintoasc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz"
			          "0123456789+/";
static unsigned char asctobin[256]; /* runtime initialized */



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

  fflush (stdout);
  fprintf (stderr, "%s: ", PGM);

  va_start (arg_ptr, format);
  vfprintf (stderr, format, arg_ptr);
  va_end (arg_ptr);
  putc ('\n', stderr);
}

/* static void * */
/* xmalloc (size_t n) */
/* { */
/*   void *p = malloc (n); */
/*   if (!p) */
/*     die ("out of core: %s", strerror (errno)); */
/*   return p; */
/* } */

/* static void * */
/* xcalloc (size_t n, size_t m) */
/* { */
/*   void *p = calloc (n, m); */
/*   if (!p) */
/*     die ("out of core: %s", strerror (errno)); */
/*   return p; */
/* } */

/* static void * */
/* xrealloc (void *old, size_t n) */
/* { */
/*   void *p = realloc (old, n); */
/*   if (!p) */
/*     die ("out of core: %s", strerror (errno)); */
/*   return p; */
/* } */

/* static char * */
/* xstrdup (const char *string) */
/* { */
/*   void *p = malloc (strlen (string)+1); */
/*   if (!p) */
/*     die ("out of core: %s", strerror (errno)); */
/*   strcpy (p, string); */
/*   return p; */
/* } */

/* static char * */
/* stpcpy (char *a,const char *b) */
/* { */
/*   while (*b) */
/*     *a++ = *b++; */
/*   *a = 0; */
  
/*   return (char*)a; */
/* } */


/* Simple but sufficient and locale independend lowercase function. */
static void
lowercase_string (unsigned char *string)
{
  for (; *string; string++)
    if (*string >= 'A' && *string <= 'Z')
      *string = *string - 'A' + 'a';
}


/* Inplace Base64 decoder. Returns the length of the valid databytes
   in BUFFER. Note, that BUFFER should initially be a C-string. */
static size_t
decode_base64 (unsigned char *buffer)
{
  int state, c, value=0;
  unsigned char *d, *s;
  
  for (state=0, d=s=buffer; *s; s++)
    {
      if ((c = asctobin[*s]) == 255 )
        continue;  /* Simply skip invalid base64 characters. */
      
      switch (state)
        {
        case 0: 
          value = c << 2;
          break;
        case 1:
          value |= (c>>4)&3;
          *d++ = value;
          value = (c<<4)&0xf0;
          break; 
        case 2:
          value |= (c>>2)&15;
          *d++ = value;
          value = (c<<6)&0xc0;
          break; 
        case 3:
          value |= c&0x3f;
          *d++ = value;
          break; 
        }
      state++;
      state = state & 3;
    }

  return d - buffer;
}


/* Given a Buffer starting with the magic MZ, check whethere thsi is a
   Windows PE executable. */
static int
is_windows_pe (const unsigned char *buffer, size_t buflen)
{
  unsigned long off;

  if ( buflen < 0x3c + 4 )
    return 0;
  /* The offset is little endian. */
  off = ((buffer[0x3c]) | (buffer[0x3d] << 8)
         | (buffer[0x3e] << 16) | (buffer[0x3f] << 24));
  return (off < buflen - 4 && !memcmp (buffer+off, "PE\0", 4));
}


/* See whether we can identify the binary data in BUFFER. */
static void
identify_binary (const unsigned char *buffer, size_t buflen)
{
  if (buflen > 5 && !memcmp (buffer, "PK\x03\x04", 4))
    {
      if (!quiet)
        fputs ("ZIP\n", stdout);
      if (opt_match_zip)
        exit (0);
    }
  else if (buflen > 132 && buffer[0] == 'M' && buffer[1] == 'Z' 
           && is_windows_pe (buffer, buflen))
    {
      if (!quiet)
        fputs ("EXE (Windows PE)\n", stdout);
      if (opt_match_exe)
        exit (0);
    }
}



/* Print the event received by the parser for debugging as comment
   line. */
static void
show_event (rfc822parse_event_t event)
{
  const char *s;

  switch (event)
    {
    case RFC822PARSE_OPEN: s= "Open"; break;
    case RFC822PARSE_CLOSE: s= "Close"; break;
    case RFC822PARSE_CANCEL: s= "Cancel"; break;
    case RFC822PARSE_T2BODY: s= "T2Body"; break;
    case RFC822PARSE_FINISH: s= "Finish"; break;
    case RFC822PARSE_RCVD_SEEN: s= "Rcvd_Seen"; break;
    case RFC822PARSE_LEVEL_DOWN: s= "Level_Down"; break;
    case RFC822PARSE_LEVEL_UP: s= "Level_Up"; break;
    case RFC822PARSE_BOUNDARY: s= "Boundary"; break;
    case RFC822PARSE_LAST_BOUNDARY: s= "Last_Boundary"; break;
    case RFC822PARSE_BEGIN_HEADER: s= "Begin_Header"; break;
    case RFC822PARSE_PREAMBLE: s= "Preamble"; break;
    case RFC822PARSE_EPILOGUE: s= "Epilogue"; break;
    default: s= "[unknown event]"; break;
    }
  printf ("# *** got RFC822 event %s\n", s);
}

/* This function is called by the parser to communicate events.  This
   callback communicates with the main program using a structure
   passed in OPAQUE. Should retrun 0 or set errno and return -1. */
static int
message_cb (void *opaque, rfc822parse_event_t event, rfc822parse_t msg)
{
  struct parse_info_s *info = opaque;

  if (debug)
    show_event (event);
  if (event == RFC822PARSE_T2BODY)
    {
      rfc822parse_field_t ctx;
      size_t off;
      char *p;

      info->mime_type = MT_NONE;
      info->transfer_encoding = TE_NONE;
      info->test_base64 = 0;
      info->got_probe = 0;
      ctx = rfc822parse_parse_field (msg, "Content-Type", -1);
      if (ctx)
        {
          const char *s1, *s2;
          s1 = rfc822parse_query_media_type (ctx, &s2);
          if (!s1)
            ;
          else if (!strcmp (s1, "application") 
                   && !strcmp (s2, "octet-stream"))
            info->mime_type = MT_OCTET_STREAM;
          else if (!strcmp (s1, "text") 
                   && !strcmp (s2, "html"))
            info->mime_type = MT_TEXT_HTML;
          else if (!strcmp (s1, "audio"))
            info->mime_type = MT_AUDIO;
          else if (!strcmp (s1, "image"))
            info->mime_type = MT_IMAGE;

          if (verbose)
            printf ("# Content-Type: %s/%s\n", s1?s1:"", s2?s2:"");

          rfc822parse_release_field (ctx);
        }

      p = rfc822parse_get_field (msg, "Content-Transfer-Encoding", -1, &off);
      if (p)
        {
          lowercase_string (p+off);
          if (!strcmp (p+off, "base64"))
            info->transfer_encoding = TE_BASE64;
          free (p);
        }

      if ((info->mime_type == MT_OCTET_STREAM
           || info->mime_type == MT_AUDIO
           || info->mime_type == MT_IMAGE)
          && info->transfer_encoding == TE_BASE64)
        info->test_base64 = 1;
      else if (info->mime_type == MT_TEXT_HTML)
        {
          if (!quiet)
            fputs ("HTML\n", stdout);
          if (opt_match_html)
            exit (0);
        }

    }
  else if (event == RFC822PARSE_PREAMBLE)
    ;
  else if (event == RFC822PARSE_BOUNDARY || event == RFC822PARSE_LAST_BOUNDARY)
    {
      if (info->test_base64)
        info->got_probe = 1;
      info->test_base64 = 0;
    }
  else if (event == RFC822PARSE_BEGIN_HEADER)
    {
    }

  return 0;
}


/* Read a message from FP and process it according to the global
   options. */
static void
parse_message (FILE *fp)
{
  char line[2000];
  unsigned char buffer[1000]; 
  size_t buflen = 0;   
  size_t length;
  rfc822parse_t msg;
  unsigned int lineno = 0;
  int no_cr_reported = 0;
  struct parse_info_s info;

  memset (&info, 0, sizeof info);

  msg = rfc822parse_open (message_cb, &info);
  if (!msg)
    die ("can't open parser: %s", strerror (errno));

  /* Fixme: We should not use fgets because it can't cope with
     embedded nul characters. */
  while (fgets (line, sizeof (line), fp))
    {
      lineno++;
      if (lineno == 1 && !strncmp (line, "From ", 5))
        continue;  /* We better ignore a leading From line. */

      length = strlen (line);
      if (length && line[length - 1] == '\n')
	line[--length] = 0;
      else if (verbose)
        err ("line number %u too long or last line not terminated", lineno);
      if (length && line[length - 1] == '\r')
	line[--length] = 0;
      else if (verbose && !no_cr_reported)
        {
          err ("non canonical ended line detected (line %u)", lineno);
          no_cr_reported = 1;
        }

      if (rfc822parse_insert (msg, line, length))
	die ("parser failed: %s", strerror (errno));
      
      if (info.got_probe && buflen)
        {
          info.got_probe = 0;
          buffer[buflen] = 0;
          buflen = decode_base64 (buffer);
          if (debug)
            {
              int i;
              
              printf ("# %4d bytes base64:", (int)buflen);
              for (i=0; i < buflen; i++)
                {
                  if (i && !(i % 16))
                    printf ("\n#            0x%04X:", i);
                  printf (" %02X", buffer[i]);
                }
              putchar ('\n'); 
            }
          identify_binary (buffer, buflen);
        }

      if (info.test_base64)
        {
          if (info.test_base64 == 1)
            { 
              /* This is the empty marker line. */
              buflen = 0;
            }
          else
            {
              if (length > sizeof buffer - 1 - buflen)
                {
                  length = sizeof buffer - 1 - buflen;
                  info.got_probe = 1;  /* We got enough. */
                }
              if (length)
                {
                  memcpy (buffer+buflen, line, length);
                  buflen += length;
                }
            }
          if (info.got_probe)
            info.test_base64 = 0;
          else
            info.test_base64++;
        }
    }

  rfc822parse_close (msg);
}



int 
main (int argc, char **argv)
{
  int last_argc = -1;
  int any_match = 0;
 
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
      else if (!strcmp (*argv, "--help"))
        {
          puts (
                "Usage: " PGM " [OPTION] [FILE]\n"
                "Scrutinize a mail message.\n\n"
                "  --match-zip  return true if a ZIP body was found\n"
                "  --match-exe  return true if an EXE body was found\n"
                "  --match-html return true if a HTML body was found\n"
                "  --verbose    enable extra informational output\n"
                "  --debug      enable additional debug output\n"
                "  --help       display this help and exit\n\n"
                "With no FILE, or when FILE is -, read standard input.\n\n"
                "Report bugs to <bugs@g10code.com>.");
          exit (0);
        }
      else if (!strcmp (*argv, "--version"))
        {
          puts (PGM " " VERSION "\n"
               "Copyright (C) 2004 g10 Code GmbH\n"
               "This program comes with ABSOLUTELY NO WARRANTY.\n"
               "This is free software, and you are welcome to redistribute it\n"
                "under certain conditions. See the file COPYING for details.");
          exit (0);
        }
      else if (!strcmp (*argv, "--verbose"))
        {
          verbose = 1;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--quiet"))
        {
          quiet = 1;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--debug"))
        {
          verbose = debug = 1;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--match-zip"))
        {
          opt_match_zip = 1;
          any_match = 1;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--match-exe"))
        {
          opt_match_exe = 1;
          any_match = 1;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--match-html"))
        {
          opt_match_html = 1;
          any_match = 1;
          argc--; argv++;
        }
    }          
 
  if (argc > 1)
    die ("usage: " PGM " [OPTION] [FILE] (try --help for more information)\n");

  signal (SIGPIPE, SIG_IGN);

  /* Build the helptable for radix64 to bin conversion. */
  {
    int i;
    unsigned char *s;

    for (i=0; i < 256; i++ )
      asctobin[i] = 255; /* Used to detect invalid characters. */
    for (s=bintoasc, i=0; *s; s++, i++)
      asctobin[*s] = i;
  }

  /* Start processing. */
  if (argc && strcmp (*argv, "-"))
    {
      FILE *fp = fopen (*argv, "rb");
      if (!fp)
        die ("can't open `%s': %s", *argv, strerror (errno));
      parse_message (fp);
      fclose (fp);
    }
  else
    parse_message (stdin);

  /* If any match option was used and we reach this here we return
     false.  True is returned immediately on a match. */
  return any_match? 1:0;
}


/*
Local Variables:
compile-command: "gcc -Wall -g -o scrutmime rfc822parse.c scrutmime.c"
End:
*/
