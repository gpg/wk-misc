/* vegetarise.c - A spam filter based on Paul Graham's idea
 *    Copyright (C) 2002  Werner Koch
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
 *
 * $Id: vegetarise.c,v 1.6 2004/09/11 14:51:51 werner Exp $
 */

/* How to use:
 *
 *  # Put this at the top of your ~/.procmailrc:
 *  #
 *  VEGETARISE_SOCKET=$HOME/.vegetarise-socket
 *  # After basic filtering, e.g. throwing away all chinese stuff and the
 *  # worm of the day, add a rule like:
 *  :0
 *  * ? vegetarise -s $HOME/Mail/words
 *  spamfolder2/
 *
 *
 *  To intialize vegetarise you need to have collections of spam and
 *  vegetarian mails.  For example, if you have sorted them into two
 *  mbox files:
 *
 *     vegetarise -l veg.mbox spam.mbox >words
 *
 *  To add new stuff to an esisting word list, use this:
 *
 *     vegetarise -l veg.mbox spam.mbox oldwords >words
 *
 *  If you don't have mbox files but two files each with a list of mails
 *  (MH or Maildir format), you can use this:
 *
 *     vegetarise -L veg-files.list spam-files.list oldwords >words
 *
 *  It can either be run standalone (usually slow) or in auto server
 *  mode (using option -s).
 **/



#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#ifdef HAVE_PTH /* In theory we could use sockets without Pth but it
                   does not make much sense to we require it. */
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pth.h>
#endif /*HAVE_PTH*/

#define PGMNAME "vegetarise"

#define MAX_WORDLENGTH 50 /* max. length of a word */
#define MAX_WORDS 15      /* max. number of words to look at. */


/* A list of token characters.  There is explicit code for 8bit
   characters. */
#define TOKENCHARS "abcdefghijklmnopqrstuvwxyz" \
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                   "0123456789-_'$"

#ifdef __GNUC__
#define inline __inline__
#else
#define inline
#endif
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 5)
#define ATTR_PRINTF(a,b)    __attribute__ ((format (printf,a,b)))
#define ATTR_NR_PRINTF(a,b) __attribute__ ((noreturn,format (printf,a,b)))
#else
#define ATTR_PRINTF(a,b)    
#define ATTR_NR_PRINTF(a,b)
#endif

#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))
#define DIMof(type,member)   DIM(((type *)0)->member)



#ifdef HAVE_PTH
# define my_read(a,b,c) pth_read ((a),(b),(c))
# define my_write(a,b,c) pth_write ((a),(b),(c))
# define YIELD  do { if (running_as_server) pth_yield (NULL); } while (0)
#else
# define my_read(a,b,c) read ((a),(b),(c))
# define my_write(a,b,c) write ((a),(b),(c))
# define YIELD  do { ; } while (0)
#endif

#define xtoi_1(a)   ((a) <= '9'? ((a)- '0'): \
                     (a) <= 'F'? ((a)-'A'+10):((a)-'a'+10))
#define xtoi_2(a,b) ((xtoi_1(a) * 16) + xtoi_1(b))


struct pushback_s {
  int buflen;
  char buf[100];
  int nl_seen;
  int state;
  int base64_nl;
  int base64_val;
  int qp1;
};
typedef struct pushback_s PUSHBACK;


struct hash_entry_s {
  struct hash_entry_s *next;
  unsigned int veg_count; 
  unsigned int spam_count; 
  unsigned int hit_ref; /* reference to the hit table. */
  char prob; /* range is 1 to 99 or 0 for not calculated */
  char word [1];
};
typedef struct hash_entry_s *HASH_ENTRY;

struct hit_array_s {
  size_t size; /* Allocated size. */
  unsigned int *hits;  
};
typedef struct hit_array_s *HIT_ARRAY;


/* Option flags. */
static int verbose;
static int name_only;

/* Flag to indicate that we are running as a server. */
static int running_as_server;

/* Keep track of memory used for debugging. */
static size_t total_memory_used;


/* The global table with the words and its size. */ 
static int hash_table_size; 
static HASH_ENTRY *word_table;

/* When storing a new word, we assign a hit reference id to it, so
   that we can index a hit table.  The variable keeps tracks of the
   used reference numbers. */
static unsigned int next_hit_ref;

/* Number of good and bad messages the word table is made up. */
static unsigned int srvr_veg_count, srvr_spam_count;



/* Base64 conversion tables. */
static unsigned char bintoasc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz"
			          "0123456789+/";
static unsigned char asctobin[256]; /* runtime initialized */



/* Prototypes. */

static void die (const char *format, ...)   ATTR_NR_PRINTF(1,2);
static void error (const char *format, ...) ATTR_PRINTF(1,2);
static void info (const char *format, ...)  ATTR_PRINTF(1,2);



/* 
    Helper

*/

static void
die (const char *fmt, ...)
{
  va_list arg_ptr;

  va_start (arg_ptr, fmt);
  fputs (PGMNAME": fatal error: ", stderr);
  vfprintf (stderr, fmt, arg_ptr);
  va_end (arg_ptr);
  exit (1);
}

static void
error (const char *fmt, ...)
{
  va_list arg_ptr;

  va_start (arg_ptr, fmt);
  fputs (PGMNAME": error: ", stderr);
  vfprintf (stderr, fmt, arg_ptr);
  va_end (arg_ptr);
}

static void
info (const char *fmt, ...)
{
  va_list arg_ptr;

  va_start (arg_ptr, fmt);
  fputs (PGMNAME": ", stderr);
  vfprintf (stderr, fmt, arg_ptr);
  va_end (arg_ptr);
}

static void *
xmalloc (size_t n)
{
  void *p = malloc (n);
  if (!p)
    die ("out of core\n");
  total_memory_used += n;
  return p;
}

static void *
xcalloc (size_t n, size_t k)
{
  void *p = calloc (n, k);
  if (!p)
    die ("out of core\n");
  total_memory_used += n * k;
  return p;
}


static inline unsigned int
hash_string (const char *s)
{
  unsigned int h = 0;
  unsigned int g;
  
  while (*s)
    {
      h = (h << 4) + *s++;
      if ((g = (h & (unsigned int) 0xf0000000)))
        h = (h ^ (g >> 24)) ^ g;
    }

  return (h % hash_table_size);
}


static void
pushback (PUSHBACK *pb, int c)
{
  if (pb->buflen < sizeof pb->buf)
      pb->buf[pb->buflen++] = c;
  else
    error ("comment parsing problem\n");
}


static inline int 
basic_next_char (FILE *fp, PUSHBACK *pb)
{
  int c;

  if (pb->buflen)
    {
      c = *pb->buf;
      if (--pb->buflen)
        memmove (pb->buf, pb->buf+1, pb->buflen);
    }
  else
    {
      c = getc (fp);
      if (c == EOF)
        return c;
    }

  /* check for HTML comment - we only use the limited syntax:
     "<!--" ... "-->" */
  while (c == '<')
    {
      if ((c=getc (fp)) == EOF)
          return c; 
      pushback (pb, c);
      if ( c != '!' )
        return '<';
      if ((c=getc (fp)) == EOF)
        {
          pb->buflen = 0;;
          return EOF; /* This misses the last chars but who cares. */
        }
      pushback (pb, c);
      if ( c != '-' )
        return '<';
      if ((c=getc (fp)) == EOF)
        {
          pb->buflen = 0;
          return EOF; /* This misses the last chars but who cares. */
        }
      pushback (pb, c);
      if ( c != '-' )
        return '<';
      pb->buflen = 0;
      /* found html comment - skip to end */
      do
        {
          while ( (c = getc (fp)) != '-')
            {
              if (c == EOF)
                return EOF;
            }
          if ( (c=getc (fp)) == EOF)
            return EOF;
        }
      while ( c != '-' ); 
      
      while ( (c = getc (fp)) != '>')
        {
          if (c == EOF)
            return EOF;
        }
      c = getc (fp);
    }
  return c;
}


static inline int 
next_char (FILE *fp, PUSHBACK *pb)
{
  int c, c2;

 next:
  if ((c=basic_next_char (fp, pb)) == EOF)
    return c;
  switch (pb->state)
    {
    case 0: break;
    case 1: 
      if (pb->nl_seen && (c == '\r' || c == '\n'))
        {
          pb->state = 2;
          goto next;
        }
      break;
    case 2:
      if (!strchr (bintoasc, c))
        break;
      pb->nl_seen = 0;
      pb->base64_nl = 0;
      pb->state = 3;
      /* fall through */
    case 3: /* 1. base64 byte */
    case 4: /* 2. base64 byte */
    case 5: /* 3. base64 byte */
    case 6: /* 4. base64 byte */
      if (c == '\n')
        {
          pb->base64_nl = 1;
          goto next;
        }
      if (pb->base64_nl && c == '-')
        {
          pb->state = 0; /* end of mime part */
          c = ' '; /* make sure that last token gets processed. */
          break;
        }
      pb->base64_nl = 0;
      if (c == ' ' || c == '\r' || c == '\t')
        goto next; /* skip all other kind of white space */
      if (c == '=' ) 
        goto next;   /* we ignore the stop character and rely on
                        the MIME boundary */
      if ((c = asctobin[c]) == 255 )
        goto next;  /* invalid base64 character */
        
      switch (pb->state)
        {
        case 3: 
          pb->base64_val = c << 2;
          pb->state = 4;
          goto next;
        case 4:
          pb->base64_val |= (c>>4)&3;
          c2 = pb->base64_val;
          pb->base64_val = (c<<4)&0xf0;
          c = c2;
          pb->state = 5;
          break; /* deliver C */
        case 5:
          pb->base64_val |= (c>>2)&15;
          c2 = pb->base64_val;
          pb->base64_val = (c<<6)&0xc0;
          c = c2;
          pb->state = 6;
          break; /* deliver C */
        case 6:
          pb->base64_val |= c&0x3f;
          c = pb->base64_val;
          pb->state = 3;
          break; /* deliver C */
        }
      break;
    case 101: /* quoted-printable */
      if (pb->nl_seen && (c == '\r' || c == '\n'))
        {
          pb->state = 102;
          goto next;
        }
      break;
    case 102:
      if (pb->nl_seen && (c == '-'))
        {
          pb->state = 105;
          goto next;
        }
      else if ( c == '=' )
        {
          pb->state = 103;
          goto next;
        }
      break;
    case 103:
      if ( isxdigit (c) )
        {
          pb->qp1 = c;
          pb->state = 104;
          goto next;
        }
      pb->state = 102;
      break;
    case 104:
      if ( isxdigit (c) )
        c = xtoi_2 (pb->qp1, c);
      pb->state = 102;
      break;
    case 105:
      if (c == '-')
        {
          pb->state = 106;
          goto next;
        }
      pb->state = 102;
      break; /* we drop one, but that's okat for this application */
    case 106:
      if ( !isspace (c) == ' ')
        {
          pb->state = 0; /* assume end of mime part */
          c = ' '; /* make sure that last token gets processed. */
        }
      else
        pb->state = 102; 
      break;
      
    }
  return c;
}

static void
enlarge_hit_array (HIT_ARRAY ha)
{
  unsigned int *arr;
  size_t i, n;

  n = ha->size + 100;
  arr = xmalloc (n * sizeof *arr);
  for (i=0; i < ha->size; i++)
    arr[i] = ha->hits[i];
  for (; i < n; i++)
    arr[i] = 0;
  free (ha->hits);
  ha->hits = arr;
  ha->size = n;
}

static HIT_ARRAY
new_hit_array (void)
{
  HIT_ARRAY ha = xmalloc (sizeof *ha);
  /* Create the array with space for extra 1000 words */
  ha->size = next_hit_ref + 1000;
  ha->hits = xcalloc (ha->size, sizeof *ha->hits);
  return ha;
}

static void
release_hit_array (HIT_ARRAY ha)
{
  if (ha)
    {
      free (ha->hits);
      free (ha);
    }
}


/* 
   real processing stuff
*/

static HASH_ENTRY
store_word (const char *word, int *is_new)
{
  unsigned int hash = hash_string (word);
  HASH_ENTRY entry;

  if (is_new)
    *is_new = 0;
  for (entry = word_table[hash];
       entry && strcmp (entry->word, word); entry = entry->next)
    ;
  if (!entry)
    {
      size_t n = sizeof *entry + strlen (word);
      entry = xmalloc (n);

      strcpy (entry->word, word);
      entry->veg_count = 0;
      entry->spam_count = 0;
      entry->hit_ref = next_hit_ref++;
      entry->prob = 0;
      entry->next = word_table[hash];
      word_table[hash] = entry;
      if (is_new)
        *is_new = 1;
    }
  return entry;
}


static void
check_one_word ( const char *word, int left_anchored, int is_spam,
                 HIT_ARRAY ha)
{
  size_t wordlen = strlen (word);
  const char *p;
  int n0, n1, n2, n3, n4, n5;

/*    fprintf (stderr, "token `%s'\n", word); */

  for (p=word; isdigit (*p); p++)
    ;
  if (!*p || wordlen < 3) 
    return; /* only digits or less than 3 chars */
  if (wordlen == 16 && word[6] == '-' && word[13] == '-')
    return; /* very likely a message-id formatted like 16cpeB-0004HM-00 */

  if (wordlen > 25 )
    return; /* words longer than that are rare */ 

  for (n0=n1=n2=n3=n4=n5=0, p=word; *p; p++)
    {
      if ( *p & 0x80)
        n0++;
      else if ( isupper (*p) )
        n1++;
      else if ( islower (*p) )
        n2++;
      else if ( isdigit (*p) )
        n3++;
      else if ( *p == '-' )
        n4++;
      else if ( *p == '.' )
        n5++;
    }
  if ( n4 == wordlen)
    return; /* Only dashes. */

  /* try to figure meaningless identifiers */
  if (n0)
    ; /* 8bit chars in name are okay */
  else if ( n3 && n3 + n5 == wordlen && n5 == 3 )
    ; /* allow IP addresses */
  else if ( !n5 && n1 > 3 && (n2 > 3 || n3 > 3) )
    return; /* No dots, mixed uppercase with digits or lowercase. */
  else if ( !n5 && n2 > 3 && (n1 > 3 || n3 > 3) )
    return; /* No dots, mixed lowercase with digits or uppercase. */
  else if ( wordlen > 8 && (3*n3) > (n1+n2))
    return; /* long word with 3 times more digits than letters. */

  if (ha)
    { /* we are in checking mode */
      int is_new;
      HASH_ENTRY e = store_word (word, &is_new);
      if (ha->size <= e->hit_ref)
        {
          assert (e->hit_ref < next_hit_ref);
          enlarge_hit_array (ha);
        }
      ha->hits[e->hit_ref]++;
    }
  else if (is_spam)
    store_word (word, NULL)->spam_count++;
  else
    store_word (word, NULL)->veg_count++;
}

/* Parse a message and return the number of messages in case it is an
   mbox message as indicated by IS_MBOX passed as true. */
static unsigned int
parse_message (const char *fname, FILE *fp, int is_spam, int is_mbox,
               HIT_ARRAY ha)
{
  int c;
  char aword[MAX_WORDLENGTH+1];
  int idx = 0;
  int in_token = 0;
  int left_anchored = 0;
  int maybe_base64 = 0;
  PUSHBACK pbbuf;
  unsigned int msgcount = 0;
  int count = 0;

  memset (&pbbuf, 0, sizeof pbbuf);
  while ( (c=next_char (fp, &pbbuf)) != EOF)
    {
      if ( ++count > 20000 )
        {
          count = 0;
          YIELD;
        }
    again:
      if (in_token)
        {
          if ((c & 0x80) || strchr (TOKENCHARS, c))
            {
              if (idx < MAX_WORDLENGTH)
                aword[idx++] = c;
              /* truncate a word and ignore truncated characters */
            }
          else
            { /* got a delimiter */
              in_token = 0;
              aword[idx] = 0;
              if (maybe_base64)
                {
                  if ( !strcasecmp (aword, "base64") )
                    pbbuf.state = 1;
                  else if ( !strcasecmp (aword, "quoted-printable") )
                    pbbuf.state = 101;
                  else
                    pbbuf.state = 0;
                  maybe_base64 = 0; 
                }
              else if (is_mbox && left_anchored
                       && !pbbuf.state && !strcmp (aword, "From"))
                {
                  if (c != ' ')
                    {
                      pbbuf.nl_seen = (c == '\n');
                      goto again;
                    }
                  msgcount++;
                }
              else if (left_anchored
                  && (!strcasecmp (aword, "Received")
                      || !strcasecmp (aword, "Date")))
                {
                  if (c != ':')
                    {
                      pbbuf.nl_seen = (c == '\n');
                      goto again;
                    }

                  do
                    {
                      while ( (c = next_char (fp, &pbbuf)) != '\n')
                        {
                          if (c == EOF)
                            goto leave;
                        }
                    }
                  while ( c == ' ' || c == '\t');
                  pbbuf.nl_seen = 1;
                  goto again;
                }
              else if (left_anchored
                       && !strcasecmp (aword, "Content-Transfer-Encoding"))
                {
                  if (c != ':')
                    {
                      pbbuf.nl_seen = (c == '\n');
                      goto again;
                    }
                  maybe_base64 = 1;
                }
              else if (c == '.' && idx && !(aword[idx-1] & 0x80)
                       && isalnum (aword[idx-1]) )
                {
                  /* Assume an IP address or a hostname if a dot is
                     followed by a letter or digit. */
                  c = next_char (fp, &pbbuf);
                  if ( !(c & 0x80) && isalnum (c) && idx < MAX_WORDLENGTH)
                    {
                      aword[idx++] = '.';
                      in_token = 1;
                    }
                  else
                    check_one_word (aword, left_anchored, is_spam, ha);
                  pbbuf.nl_seen = (c == '\n');
                  goto again;
                }
#if 0
              else if (c == '=')
                {
                  /* Assume an QP encoded character if followed by an
                     hexdigit */
                  c = next_char (fp, &pbbuf);
                  if ( !(c & 0x80) && (isxdigit (c)) && idx < MAX_WORDLENGTH)
                    {
                      aword[idx++] = '=';
                      in_token = 1;
                    }
                  else
                    check_one_word (aword, left_anchored, is_spam, ha);
                  pbbuf.nl_seen = (c == '\n');
                  goto again;
                }
#endif
              else
                check_one_word (aword, left_anchored, is_spam, ha);
            }
        }
      else if ( (c & 0x80) || strchr (TOKENCHARS, c))
        {
          in_token = 1;
          idx = 0;
          aword[idx++] = c;
          left_anchored = pbbuf.nl_seen;
        }
      pbbuf.nl_seen = (c == '\n');
    }
 leave:
  if (ferror (fp))
      die ("error reading `%s': %s\n", fname, strerror (errno));

  msgcount++;
  return msgcount;
}


static unsigned int
calc_prob (unsigned int g, unsigned int b,
           unsigned int ngood, unsigned int nbad)
{
   double prob_g, prob_b, prob;

/*      (max .01 */
/*            (min .99 (float (/ (min 1 (/ b nbad)) */
/*                               (+ (min 1 (/ g ngood)) */
/*                                  (min 1 (/ b nbad))))))))) */

   prob_g = (double)g / ngood;
   if (prob_g > 1)
     prob_g = 1;
   prob_b = (double)b / ngood;
   if (prob_b > 1)
     prob_b = 1;

   prob = prob_b / (prob_g + prob_b);
   if (prob < .01)
     prob = .01;
   else if (prob > .99)
     prob = .99;
  
   return (unsigned int) (prob * 100);
}


static void
calc_probability (unsigned int ngood, unsigned int nbad)
{
  int n;
  HASH_ENTRY entry;
  unsigned int g, b; 

  if (!ngood)
    die ("no vegetarian mails available - stop\n");
  if (!nbad)
    die ("no spam mails available - stop\n");

  for (n=0; n < hash_table_size; n++)
    {
      for (entry = word_table[n]; entry; entry = entry->next)
        {
          g = entry->veg_count * 2;
          b = entry->spam_count;
          if (g + b >= 5)
            entry->prob = calc_prob (g, b, ngood, nbad);
        }
    }
}


static unsigned int
check_spam (unsigned int ngood, unsigned int nbad, HIT_ARRAY ha)
{
  unsigned int n;
  HASH_ENTRY entry;
  unsigned int dist, min_dist;
  struct {
    HASH_ENTRY e;
    unsigned int d;
    double prob;
  } st[MAX_WORDS];
  int nst = 0;
  int i;
  double prod, inv_prod, taste;

  for (i=0; i < MAX_WORDS; i++)
    {
      st[i].e = NULL;
      st[i].d = 0;
    }

  min_dist = 100;
  for (n=0; n < hash_table_size; n++)
    {
      for (entry = word_table[n]; entry; entry = entry->next)
        {
          if (entry->hit_ref && ha->hits[entry->hit_ref])
            {
              if (!entry->prob)
                dist = 10; /* 50 - 40 */
              else
                dist = entry->prob < 50? (50 - entry->prob):(entry->prob - 50);
              if (nst < MAX_WORDS)
                {
                  st[nst].e = entry;
                  st[nst].d = dist;
                  st[nst].prob = entry->prob? (double)entry->prob/100 : 0.4;
                  if (dist < min_dist)
                    min_dist = dist;
                  nst++;
                }
              else if (dist > min_dist)
                { 
                  unsigned int tmp = 100;
                  int tmpidx = -1;
                  
                  for (i=0; i < MAX_WORDS; i++)
                    {
                      if (st[i].d < tmp)
                        {
                          tmp = st[i].d;
                          tmpidx = i;
                        }
                    }
                  assert (tmpidx != -1);
                  st[tmpidx].e = entry;
                  st[tmpidx].d = dist;
                  st[tmpidx].prob = entry->prob? (double)entry->prob/100 : 0.4;
                  
                  min_dist = 100;
                  for (i=0; i < MAX_WORDS; i++)
                    {
                      if (st[i].d < min_dist)
                        min_dist = dist;
                    }
                }
            }
        }
    }

  /* ST has now the NST most intersting words */
  if (!nst)
    {
      info ("not enough words - assuming goodness\n");
      return 100;
    }

  if (verbose > 1)
    {
      for (i=0; i < nst; i++)
        info ("prob %3.2f dist %3d for `%s'\n",
              st[i].prob, st[i].d, st[i].e->word);
    }

  prod = 1;
  for (i=0; i < nst; i++)
    prod *= st[i].prob;

  inv_prod = 1;
  for (i=0; i < nst; i++)
    inv_prod *= 1.0 - st[i].prob;

  taste = prod / (prod + inv_prod);
  if (verbose > 1)
    info ("taste -> %u\n\n", (unsigned int)(taste * 100));
  return (unsigned int)(taste * 100);
}



static void
reset_hits (HIT_ARRAY ha)
{
  int i;

  for (i=0; i < ha->size; i++)
    ha->hits[i] = 0;
}

static void
write_table (unsigned int ngood, unsigned int nbad)
{
  int n;
  HASH_ENTRY entry;

  printf ("#\t0\t0\t0\t%u\t%u\n", ngood, nbad);
  for (n=0; n < hash_table_size; n++)
    {
      for (entry = word_table[n]; entry; entry = entry->next)
        if (entry->prob)
          printf ("%s\t%d\t%u\t%u\n", entry->word, entry->prob,
                  entry->veg_count, entry->spam_count);
    }
}

static void
read_table (const char *fname,
            unsigned int *ngood, unsigned int *nbad, unsigned int *nwords)
{
  FILE *fp;
  char line[MAX_WORDLENGTH + 100]; 
  unsigned int lineno = 0;
  char *p;

  *nwords = 0;
  fp = fopen (fname, "r");
  if (!fp)
    die ("can't open wordlist `%s': %s\n", fname, strerror (errno));

  while ( fgets (line, sizeof line, fp) )
    {
      lineno++;
      if (!*line) /* last line w/o LF? */
        die ("incomplete line %u in `%s'\n", lineno, fname);

      if (line[strlen (line)-1] != '\n')
        die ("line %u in `%s' too long\n", lineno, fname);

      line[strlen (line)-1] = 0;
      if (!*line)
        goto invalid_line;
      p = strchr (line, '\t');
      if (!p || p == line || (p-line) > MAX_WORDLENGTH )
        goto invalid_line;
      *p++ = 0;
      if (lineno == 1)
        {
          if (sscanf (p, "%*u %*u %*u %u %u", ngood, nbad) < 2)
            goto invalid_line;
        }
      else
        {
          HASH_ENTRY e;
          unsigned int prob, g, b;
          int is_new;

          if (sscanf (p, "%u %u %u", &prob, &g, &b) < 3)
            goto invalid_line;
          if (prob > 99)
            goto invalid_line;
          e = store_word (line, &is_new);
          if (!is_new)
            die ("duplicate entry at line %u in `%s'\n", lineno, fname);

          e->prob = prob? prob : 1;
          e->veg_count = g;
          e->spam_count = b;
          ++*nwords;
        }

    }
  if (ferror (fp))
    die ("error reading wordlist `%s' at line %u: %s\n",
         fname, lineno, strerror (errno));
  fclose (fp);
  return;

 invalid_line:
  die ("invalid line %u in `%s'\n", lineno, fname);
}




static FILE *
open_next_file (FILE *listfp, char *fname, size_t fnamelen)
{
  FILE *fp;
  char line[2000];

  while ( fgets (line, sizeof line, listfp) )
    {
      if (!*line) 
        { /* last line w/o LF? */
          if (fgetc (listfp) != EOF)
            error ("weird problem reading file list\n");
          break;
        }
      if (line[strlen (line)-1] != '\n')
        {
          error ("filename too long - skipping\n");
          while (getc (listfp) != '\n')
            ;
          continue;
        }
      line[strlen (line)-1] = 0;
      if (!*line)
        continue; /* skip empty lines */

      fp = fopen (line, "rb");
      if (fp)
        {
          if (fname && fnamelen > 1)
            {
              strncpy (fname, line, fnamelen-1);
              fname[fnamelen-1] = 0;
            }
          return fp;
        }
      error ("can't open `%s': %s - skipped\n", line, strerror (errno));
    }
  if (ferror (listfp))
    error ("error reading file list: %s\n",  strerror (errno));
  return NULL;
}


static void
check_and_print (unsigned int veg_count, unsigned int spam_count,
                 const char *filename, HIT_ARRAY ha)
{
  int spamicity = check_spam (veg_count, spam_count, ha);
  if (name_only > 0 && spamicity > 90)
    puts (filename); /* contains spam */
  else if (name_only < 0 && spamicity <= 90)
    puts (filename); /* contains valuable blurbs */
  else if (!name_only)
    printf ("%s: %2u\n", filename, spamicity);
  reset_hits (ha);
}



/*
   Server code and startup 
*/

#ifdef HAVE_PTH

/* Write NBYTES of BUF to file descriptor FD. */
static int
writen (int fd, const void *buf, size_t nbytes)
{
  size_t nleft = nbytes;
  int nwritten;
  
  while (nleft > 0)
    {
      nwritten = my_write( fd, buf, nleft );
      if (nwritten < 0)
        {
          if (errno == EINTR)
            nwritten = 0;
          else
            {
              error ("write failed: %s\n", strerror (errno));
              return -1;
            }
        }
      nleft -= nwritten;
      buf = (const char*)buf + nwritten;
    }
  
  return 0;
}


/* Read an entire line and return number of bytes read. */
static int
readline (int fd, char *buf, size_t buflen)
{
  size_t nleft = buflen;
  char *p;
  int nread = 0;

  while (nleft > 0)
    {
      int n = my_read (fd, buf, nleft);
      if (n < 0)
        {
          if (errno == EINTR)
            continue;
          return -1;
        }
      else if (!n)
        return -1; /* incomplete line */

      p = buf;
      nleft -= n;
      buf += n;
      nread += n;
      
      for (; n && *p != '\n'; n--, p++)
        ;
      if (n)
        {
          break; /* at least one full line available - that's enough.
                    This function is just a simple implementation, so
                    it is okay to forget about pending bytes */
        }
    }
  
  return nread; 
}



static int
create_socket (const char *name, struct sockaddr_un *addr, size_t *len)
{
  int fd;

  if (strlen (name)+1 >= sizeof addr->sun_path) 
    die ("oops\n");

  if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) 
    die ("can't create socket: %s\n", strerror(errno));
    
  memset (addr, 0, sizeof *addr);
  addr->sun_family = AF_UNIX;
  strcpy (addr->sun_path, name);
  *len = (offsetof (struct sockaddr_un, sun_path)
          + strlen (addr->sun_path) + 1);
  return fd;
}
    

/* Try to connect to the socket NAME and return the file descriptor. */
static int
find_socket (const char *name)
{
  int fd;
  struct sockaddr_un addr;
  size_t len;

  fd = create_socket (name, &addr, &len);
  if (connect (fd, (struct sockaddr*)&addr, len) == -1)
    {
      if (errno != ECONNREFUSED)
        error ("can't connect to `%s': %s\n", name, strerror (errno));
      close (fd);
      return -1;
    }

  return fd;
}

static void
handle_signal (int signo)
{
  switch (signo)
    {
    case SIGHUP:
      info ("SIGHUP received - re-reading configuration\n");
      break;
      
    case SIGUSR1:
      if (verbose < 5)
        verbose++;
      info ("SIGUSR1 received - verbosity set to %d\n", verbose);
      break;

    case SIGUSR2:
      if (verbose)
        verbose--;
      info ("SIGUSR2 received - verbosity set to %d\n", verbose );
      break;

    case SIGTERM:
      info ("SIGTERM received - shutting down ...\n");
      exit (0);
      break;
        
    case SIGINT:
      info ("SIGINT received - immediate shutdown\n");
      exit (0);
      break;

    default:
      info ("signal %d received - no action defined\n", signo);
    }
}


/* Handle a request */
static void *
handle_request (void *arg)
{
  static HIT_ARRAY hit_arrays[10];
  HIT_ARRAY ha;
  int fd = (int)arg;
  int i;
  FILE *fp;
  char *p, buf[100];
  
  if (verbose > 1)
    info ("handler for fd %d started\n", fd);
  /* See whether we can use a hit_array from our attic */
  for (ha=NULL, i=0; i < DIM (hit_arrays); i++ )
    {
      if (hit_arrays[i])
        {
          ha = hit_arrays[i];
          hit_arrays[i] = NULL;
          break;
        }
    }
  if (!ha) /* no - create a new one */
     ha = new_hit_array ();

  fp = fdopen (fd, "rw");
  if (!fp)
    {
      p = "0 fd_open_failed\n";
      writen (fd, p, strlen (p));
    }
  else
    {
      parse_message ("[net]", fp, -1, 0, ha);
      sprintf (buf, "%u\n", check_spam (srvr_veg_count, srvr_spam_count, ha));
      p = buf;
    }
  writen (fd, p, strlen (p));

  if (fp)
    fclose (fp);
  else
    close (fd);
  reset_hits (ha);
  /* See whether we can put the array back into our attic */
  for (i=0; i < DIM (hit_arrays); i++ )
    {
      if (!hit_arrays[i])
        {
          hit_arrays[i] = ha;
          ha = NULL;
          break;
        }
    }
  if (ha)
    release_hit_array (ha);

  if (verbose > 1)
    info ("handler for fd %d terminated\n", fd);
  return NULL;
}

/* Send a request to check for spam to the server process.  Return the
   spam level. */
static unsigned int
transact_request (int fd, const char *fname, FILE *fp)
{
  char buf[4096];
  size_t n;

  do
    {
      n = fread (buf, 1, sizeof buf, fp);
      writen (fd, buf, n);
    }
  while ( n == sizeof buf);
  if (ferror (fp))
    die ("input read error\n");
  shutdown (fd, 1);
  if (readline (fd, buf, sizeof buf -1) == -1)
    die ("error reading from server: %s\n", strerror (errno));
  return atoi (buf);
}


/* Start a server process to listen on socket NAME. */
static void
start_server (const char *name)
{
  int srvr_fd;
  struct sockaddr_un srvr_addr;
  size_t len;
  struct sigaction sa;
  pid_t pid;
  pth_attr_t tattr;
  pth_event_t ev;
  sigset_t sigs;
  int signo;

  fflush (NULL);
  pid = fork ();
  if (pid == (pid_t)-1) 
    die ("fork failed: %s\n", strerror (errno));

  if (pid) 
    {
      /* FIXME: we should use a pipe to wait forthe server to get ready */
      return; /* we are the parent */
    }
  /* this is the child */
  sa.sa_handler = SIG_IGN;
  sigemptyset (&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction (SIGPIPE, &sa, NULL);

  srvr_fd = create_socket (name, &srvr_addr, &len);
  if (bind (srvr_fd, (struct sockaddr*)&srvr_addr, len) == -1)
    {
      /* This might happen when server has been started in the meantime. */
      die ("error binding socket to `%s': %s\n", name, strerror (errno));
    }
  
  if (listen (srvr_fd, 5 ) == -1)
    die ("listen on `%s' failed: %s\n", name, strerror (errno));
  if (verbose)
    info ("listening on socket `%s'\n", name );

  if (!pth_init ())
    die ("failed to initialize the Pth library\n");

  tattr = pth_attr_new ();
  pth_attr_set (tattr, PTH_ATTR_JOINABLE, 0);
  pth_attr_set (tattr, PTH_ATTR_STACK_SIZE, 32*1024);
  pth_attr_set (tattr, PTH_ATTR_NAME, PGMNAME);

  sigemptyset (&sigs );
  sigaddset (&sigs, SIGHUP);
  sigaddset (&sigs, SIGUSR1);
  sigaddset (&sigs, SIGUSR2);
  sigaddset (&sigs, SIGINT);
  sigaddset (&sigs, SIGTERM);
  ev = pth_event (PTH_EVENT_SIGS, &sigs, &signo);

  running_as_server = 1; /* enable pth_yield(). */
  for (;;)
    {
      int fd;
      struct sockaddr_un paddr;
      socklen_t plen = sizeof (paddr);

      fd = pth_accept_ev (srvr_fd, (struct sockaddr *)&paddr, &plen, ev);
      if (fd == -1)
        {
          if (pth_event_occurred (ev))
            {
              handle_signal (signo);
              continue;
	    }
          error ("accept failed: %s - waiting 1s\n", strerror (errno));
          pth_sleep(1);
          continue;
	}

      if (!pth_spawn (tattr, handle_request, (void*)fd))
        {
          error ("error spawning connection handler: %s\n", strerror (errno));
          close (fd);
	}
    }
  /*NOTREACHED*/
}

#endif /*HAVE_PTH*/

static void
usage (void)
{
  fputs (
   "usage: " PGMNAME " [-t] wordlist [messages]\n"
   "       " PGMNAME "  -T  wordlist [messages-file-list]\n"
   "       " PGMNAME "  -s  wordlist [message]\n"
   "       " PGMNAME "  -l  veg.mbox spam.mbox [initial-wordlist]\n"
   "       " PGMNAME "  -L  veg-file-list spam-file-list [initial-wordlist]\n"
         "\n"
   "  -v      be more verbose\n"
   "  -l      learn mode (mbox)\n"
   "  -L      learn mode (one file per message)\n"
   "  -n      print only the names of spam files\n"
   "  -N      print only the names of vegetarian files\n"
   "  -s      auto server mode\n"
   , stderr );
  exit (1);
}


int
main (int argc, char **argv)
{
  int i;
  unsigned char *s;
  int skip = 0;
  int learn = 0;
  int indirect = 0;
  int server = 0;
  unsigned int veg_count=0, spam_count=0;
  FILE *fp;
  char fnamebuf[1000];
  int server_fd = -1;
  HIT_ARRAY ha = NULL;

  /* Build the helptable for radix64 to bin conversion. */
  for (i=0; i < 256; i++ )
    asctobin[i] = 255; /* used to detect invalid characters */
  for (s=bintoasc, i=0; *s; s++, i++)
    asctobin[*s] = i;

  if (argc < 1)
    usage ();  /* Hey, read how to use exec*(2) */
  argv++; argc--;
  for (; argc; argc--, argv++) 
    {
      const char *s = *argv;
      if (!skip && *s == '-')
        {
          s++;
          if( *s == '-' && !s[1] ) {
            skip = 1;
            continue;
          }
          if (*s == '-' || !*s) 
            usage();

          while (*s)
            {
              if (*s=='v')
                {
                  verbose++;
                  s++;
                }
              else if (*s=='t')
                {
                  learn = 0;
                  indirect = 0;
                  s++;
                }
              else if (*s=='T')
                {
                  learn = 0;
                  indirect = 1;
                  s++;
                }
              else if (*s=='l')
                {
                  learn = 1;
                  indirect = 0;
                  s++;
                }
              else if (*s=='L')
                {
                  learn = 1;
                  indirect = 1;
                  s++;
                }
              else if (*s=='n')
                {
                  name_only = 1;
                  s++;
                }
              else if (*s=='N')
                {
                  name_only = -1;
                  s++;
                }
              else if (*s=='s')
                {
                  server = 1;
                  s++;
                }
              else if (*s)
                usage();
            }
          continue;
        }
      else 
        break;
    }

  if (server)
    {
      char namebuf[80];
      const char *name;

      if (learn)
        die ("learn mode can't be combined with server mode\n");
#ifndef HAVE_PTH
      die ("not compiled with GNU Pth support - can't run in server mode\n");
#else

      if (argc < 1)
        usage ();

      /* Well, what name should we use format_ the socket.  The best
         thing would be to create it in the home directory, but this
         will be problematic for NFS mounted homes.  So for now we use
         a constant name under tmp.  Check whether we can use
         something under /var/run */
      name = getenv ("VEGETARISE_SOCKET");
      if (!name || !*name)
        {
          sprintf (namebuf, "/tmp/vegetarise-%lu/VEG_SOCK",
                   (unsigned long)getuid ());
          name = namebuf;
        }

      server_fd = find_socket (name);
      if (server_fd == -1)
        {
          int tries;
          unsigned int nwords;

          hash_table_size = 4999;
          word_table = xcalloc (hash_table_size, sizeof *word_table);

          read_table (argv[0], &veg_count, &spam_count, &nwords);
          info ("starting server with "
                "%u vegetarian, %u spam, %u words, %lu kb memory\n",
                veg_count, spam_count, nwords,
                (unsigned long int)total_memory_used/1024);
          srvr_veg_count = veg_count;
          srvr_spam_count = spam_count;

          /* fixme: don't use sleep */
          start_server (name);
          for (tries=0; (tries < 10
                         && (server_fd=find_socket (name))==-1); tries++)
            sleep (1);
        }
      if (server_fd == -1)
        {
          error ("failed to start server - disabling server mode\n");
          server = 0;
        }
#endif /*HAVE_PTH*/
    }

  
  if (learn && !server)
    {
      FILE *veg_fp = NULL, *spam_fp = NULL;
      unsigned int nwords;

      if (argc != 2 && argc != 3)
        usage ();

      hash_table_size = 4999;
      word_table = xcalloc (hash_table_size, sizeof *word_table);

      if ( strcmp (argv[0], "-") )
        {
          veg_fp = fopen (argv[0], "r");
          if (!veg_fp)
            die ("can't open `%s': %s\n", argv[0], strerror (errno));
        }
      if ( strcmp (argv[1], "-") )
        {
          spam_fp = fopen (argv[1], "r");
          if (!spam_fp)
            die ("can't open `%s': %s\n", argv[1], strerror (errno));
        }

      if (argc == 3)
        { 
          info ("loading initial wordlist\n");
          read_table (argv[2], &veg_count, &spam_count, &nwords);
          info ("%u vegetarian, %u spam, %u words, %lu kb memory used\n",
                veg_count, spam_count, nwords,
                (unsigned long int)total_memory_used/1024);
        }

      if (veg_fp)
        {
          info ("scanning vegetarian mail\n");
          if (indirect)
            {
              while ((fp = open_next_file (veg_fp, fnamebuf, sizeof fnamebuf)))
                {
                  veg_count += parse_message (fnamebuf, fp, 0, 0, ha);
                  fclose (fp);
                }
            }
          else
            veg_count += parse_message (argv[0], veg_fp, 0, 1, ha);
          fclose (veg_fp);
        }

      if (spam_fp)
        {
          info ("scanning spam mail\n");
          if (indirect)
            {
              while ((fp = open_next_file (spam_fp, fnamebuf, sizeof fnamebuf)))
                {
                  spam_count += parse_message (fnamebuf, fp, 1, 0, ha);
                  fclose (fp);
                }
            }
          else
            spam_count += parse_message (argv[1], spam_fp, 1, 1, ha);
          fclose (spam_fp);
        }
      info ("computing probabilities\n");
      calc_probability (veg_count, spam_count);
      
      write_table (veg_count, spam_count);

      info ("%u vegetarian, %u spam, %lu kb memory used\n",
            veg_count, spam_count,
            (unsigned long int)total_memory_used/1024);
    }
#ifdef HAVE_PTH
  else if (server_fd != -1)
    { /* server mode */

      argc--; argv++; /* ignore the wordlist */
      
      if (argc > 1)
        usage ();
                    
      fp = argc? fopen (argv[0], "r") : stdin;
      if (!fp)
        die ("can't open `%s': %s\n", argv[0], strerror (errno));
      if (transact_request (server_fd, argc? argv[0]:"-", fp) > 90)
        {
          close (server_fd);
          if (verbose)
            puts ("spam\n");
          exit (0); 
        }
      close (server_fd);
      exit (1); /* Non-Spam but we use false so that a
                   system error does not lead false
                   positives */
    }
#endif /*HAVE_PTH*/
  else
    {
      unsigned int nwords;

      if (argc < 1)
        usage ();

      hash_table_size = 4999;
      word_table = xcalloc (hash_table_size, sizeof *word_table);

      read_table (argv[0], &veg_count, &spam_count, &nwords);
      argc--; argv++;
      if (verbose)
        info ("%u vegetarian, %u spam, %u words, %lu kb memory used\n",
              veg_count, spam_count, nwords,
              (unsigned long int)total_memory_used/1024);
      
      ha = new_hit_array ();

      if (!argc)
        {
          if (indirect)
            {
              while ((fp = open_next_file (stdin, fnamebuf, sizeof fnamebuf)))
                {
                  parse_message (fnamebuf, fp, 0, 0, ha);
                  fclose (fp);
                  check_and_print (veg_count, spam_count, fnamebuf, ha);
                }
            }
          else
            {
              parse_message ("-", stdin, -1, 0, ha);
              if ( check_spam (veg_count, spam_count, ha) > 90)
                {
                  if (verbose)
                    puts ("spam\n");
                  exit (0);
                }
              else
                exit (1); /* Non-Spam but we use false so that a
                             system error does not lead false
                             positives */
            }
        }
      else
        {
          for (; argc; argc--, argv++)
            {
              FILE *fp = fopen (argv[0], "r");
              if (!fp)
                {
                  error ("can't open `%s': %s\n", argv[0], strerror (errno));
                  continue;
                }
              if (indirect)
                {
                  FILE *fp2;
                  while ((fp2 = open_next_file (fp,fnamebuf, sizeof fnamebuf)))
                    {
                      parse_message (fnamebuf, fp2, 0, 0, ha);
                      fclose (fp2);
                      check_and_print (veg_count, spam_count, fnamebuf, ha);
                    }
                }
              else
                {
                  parse_message (argv[0], fp, -1, 0, ha);
                  check_and_print (veg_count, spam_count, argv[0], ha);
                }
              fclose (fp);
            }
        }
    }

  return 0;
}

/*
Local Variables:
compile-command: "gcc -Wall -g -DHAVE_PTH -o vegetarise vegetarise.c -lpth"
End:
*/
