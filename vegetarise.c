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
 * $Id: vegetarise.c,v 1.1 2002-10-13 21:22:05 werner Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#ifdef __GNUC__
#define inline __inline__
#else
#define inline
#endif

#define min(a,b) ((a) < (b)? (a): (b))
#define max(a,b) ((a) > (b)? (a): (b))

#define PGMNAME "vegetarise"

#define MAX_WORDLENGTH 50 /* max. length of a word */
#define MAX_WORDS 15      /* max. number of words to look at. */

/* A list of token characters.  There is explicit code for 8bit
   characters. */
#define TOKENCHARS "abcdefghijklmnopqrstuvwxyz" \
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                   "0123456789-_'$"

struct pushback_s {
  int buflen;
  char buf[100];
  int nl_seen;
  int base64_state;
  int base64_nl;
  int base64_val;
};
typedef struct pushback_s PUSHBACK;


struct hash_entry_s {
  struct hash_entry_s *next;
  unsigned int veg_count; 
  unsigned int spam_count; 
  unsigned int hits;
  char prob; /* range is 1 to 99 or 0 for not calculated */
  char word [1];
};
typedef struct hash_entry_s *HASH_ENTRY;

static int silent;
static int verbose;
static int learning;

static size_t total_memory_used;

static int hash_table_size; 
static HASH_ENTRY *word_table;

/* Base64 conversion tables. */
static unsigned char bintoasc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz"
			          "0123456789+/";
static unsigned char asctobin[256]; /* runtime initialized */


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
    fprintf (stderr, PGMNAME": comment parsing problem\n");
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
  switch (pb->base64_state)
    {
    case 0: break;
    case 1: 
      if (pb->nl_seen && (c == '\r' || c == '\n'))
        {
          pb->base64_state = 2;
          goto next;
        }
      break;
    case 2:
      if (!strchr (bintoasc, c))
        break;
      pb->nl_seen = 0;
      pb->base64_nl = 0;
      pb->base64_state = 3;
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
          pb->base64_state = 0; /* end of mime part */
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
        
      switch (pb->base64_state)
        {
        case 3: 
          pb->base64_val = c << 2;
          pb->base64_state = 4;
          goto next;
        case 4:
          pb->base64_val |= (c>>4)&3;
          c2 = pb->base64_val;
          pb->base64_val = (c<<4)&0xf0;
          c = c2;
          pb->base64_state = 5;
          break; /* deliver C */
        case 5:
          pb->base64_val |= (c>>2)&15;
          c2 = pb->base64_val;
          pb->base64_val = (c<<6)&0xc0;
          c = c2;
          pb->base64_state = 6;
          break; /* deliver C */
        case 6:
          pb->base64_val |= c&0x3f;
          c = pb->base64_val;
          pb->base64_state = 3;
          break; /* deliver C */
        }
    }
  return c;
}


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
      entry = malloc (n);
      if (!entry)
        {
          fprintf (stderr, PGMNAME": out of core\n");
          exit (1);
        }
      total_memory_used += n;

      strcpy (entry->word, word);
      entry->veg_count = 0;
      entry->spam_count = 0;
      entry->hits = 0;
      entry->prob = 0;
      entry->next = word_table[hash];
      word_table[hash] = entry;
      if (is_new)
        *is_new = 1;
    }
  return entry;
}


static void
check_one_word ( const char *word, int left_anchored, int is_spam)
{
  size_t wordlen = strlen (word);
  const char *p;
  int n0, n1, n2, n3, n4, n5;

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

  if (!learning)
    {
      int is_new;
      HASH_ENTRY e = store_word (word, &is_new);
      e->hits++;
    }
  else if (is_spam)
    store_word (word, NULL)->spam_count++;
  else
    store_word (word, NULL)->veg_count++;
}


static void
parse_words (const char *fname, FILE *fp, int is_spam)
{
  int c;
  char aword[MAX_WORDLENGTH+1];
  int idx = 0;
  int in_token = 0;
  int left_anchored = 0;
  int maybe_base64 = 0;
  PUSHBACK pbbuf;

  memset (&pbbuf, 0, sizeof pbbuf);
  while ( (c=next_char (fp, &pbbuf)) != EOF)
    {
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
                  pbbuf.base64_state = !strcasecmp (aword, "base64");
                  maybe_base64 = 0; 
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
                    check_one_word (aword, left_anchored, is_spam);
                  pbbuf.nl_seen = (c == '\n');
                  goto again;
                }
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
                    check_one_word (aword, left_anchored, is_spam);
                  pbbuf.nl_seen = (c == '\n');
                  goto again;
                }
              else
                check_one_word (aword, left_anchored, is_spam);
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
    {
      fprintf (stderr, PGMNAME": error reading `%s': %s\n",
               fname, strerror (errno));
      exit (1);
    }
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
    {
      fprintf (stderr, PGMNAME": no vegetarian mails available - stop\n");
      exit (1);
    }
  if (!nbad)
    {
      fprintf (stderr, PGMNAME": no spam mails available - stop\n");
      exit (1);
    }
    

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
check_spam (unsigned int ngood, unsigned int nbad)
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
          if (entry->hits)
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
      fprintf (stderr, PGMNAME": not enough words - assuming good\n");
      return 100;
    }

  if (verbose > 1)
    {
      for (i=0; i < nst; i++)
        fprintf (stderr, PGMNAME": prob %3.2f dist %3d for `%s'\n",
                 st[i].prob, st[i].d, st[i].e->word);
    }

  prod = 1;
  for (i=0; i < nst; i++)
    prod *= st[i].prob;

  inv_prod = 1;
  for (i=0; i < nst; i++)
    inv_prod *= 1.0 - st[i].prob;

  taste = prod / (prod + inv_prod);
  return (unsigned int)(taste * 100);
}



static void
reset_hits (void)
{
  int n;
  HASH_ENTRY entry;

  for (n=0; n < hash_table_size; n++)
    {
      for (entry = word_table[n]; entry; entry = entry->next)
        entry->hits = 0;
    }
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
    {
      fprintf (stderr, PGMNAME": can't open wordlist `%s': %s\n",
               fname, strerror (errno));
      exit (1);
    }
  while ( fgets (line, sizeof line, fp) )
    {
      lineno++;
      if (!*line)
        { /* last line w/o LF? */
          fprintf (stderr, PGMNAME": incomplete line %u in `%s'\n",
                   lineno, fname);
          exit (1);
        }
      if (line[strlen (line)-1] != '\n')
        {
          fprintf (stderr, PGMNAME": line %u in `%s' too long\n",
                   lineno, fname);
          exit (1);
        }
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
            {
              fprintf (stderr, PGMNAME": duplicate entry at line %u in `%s'\n",
                       lineno, fname);
              exit (1);
            }
          e->prob = prob? prob : 1;
          e->veg_count = g;
          e->spam_count = b;
          ++*nwords;
        }

    }
  if (ferror (fp))
    {
      fprintf (stderr, PGMNAME": error reading wordlist `%s' at line %u: %s\n",
               fname, lineno, strerror (errno));
      exit (1);
    }
  fclose (fp);
  return;

 invalid_line:
  fprintf (stderr, PGMNAME": invalid line %u in `%s'\n", lineno, fname);
  exit (1);
}




static FILE *
open_next_file (FILE *listfp)
{
  FILE *fp;
  char line[2000];

  while ( fgets (line, sizeof line, listfp) )
    {
      if (!*line)
        { /* last line w/o LF? */
          if (fgetc (listfp) != EOF)
            fprintf (stderr, PGMNAME": weird problem reading file list\n");
          break;
        }
      if (line[strlen (line)-1] != '\n')
        {
          fprintf (stderr, PGMNAME": filename too long - skipping\n");
          while (getc (listfp) != '\n')
            ;
          continue;
        }
      line[strlen (line)-1] = 0;
      if (!*line)
        continue; /* skip empty lines */

      fp = fopen (line, "rb");
      if (fp)
        return fp;
      fprintf (stderr, PGMNAME": skipping `%s': %s\n",
               line, strerror (errno));
    }
  if (ferror (listfp))
    fprintf (stderr, PGMNAME": error reading file list: %s\n",
             strerror (errno));
  return NULL;
}

static void
usage (void)
{
  fputs ("usage: " PGMNAME " [options] [--] [veg-file-list spam-file-list]\n"
         "\n"
         "  -q   be silent\n"
         "  -v   be more verbose\n"
         "  -L   learn mode\n"
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
  unsigned int veg_count=0, spam_count=0;

  /* Build the helptable for radix64 to bin conversion. */
  for (i=0; i < 256; i++ )
    asctobin[i] = 255; /* used to detect invalid characters */
  for (s=bintoasc, i=0; *s; s++, i++)
    asctobin[*s] = i;

  /* Option parsing. */
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
              if (*s=='q')
                {
                  silent=1;
                  s++;
                }
              else if (*s=='v')
                {
                  verbose++;
                  s++;
                }
              else if (*s=='L')
                {
                  learn = 1;
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

  hash_table_size = 4999;
  word_table = calloc (hash_table_size, sizeof *word_table);
  if (!word_table)
    {
      fprintf (stderr, PGMNAME": out of core\n");
      exit (1);
    }
  total_memory_used = hash_table_size * sizeof *word_table;
  
  if (learn)
    {
      FILE *veg_fp, *spam_fp, *fp;

      if (argc != 2)
        usage ();

      learning = 1;
      veg_fp = fopen (argv[0], "r");
      if (!veg_fp)
        {
          fprintf (stderr, PGMNAME": can't open `%s': %s\n",
                   argv[0], strerror (errno));
          exit (1);
        }
      spam_fp = fopen (argv[1], "r");
      if (!spam_fp)
        {
          fprintf (stderr, PGMNAME": can't open `%s': %s\n",
                   argv[1], strerror (errno));
          exit (1);
        }
      
      if (verbose)
        fprintf (stderr, PGMNAME": scanning vegetarian mail\n");
      while ((fp = open_next_file (veg_fp)))
        {
          parse_words (*argv, fp, 0);
          veg_count++;
          fclose (fp);
        }
      fclose (veg_fp);

      if (verbose)
        fprintf (stderr, PGMNAME": scanning spam mail\n");
      while ((fp = open_next_file (spam_fp)))
        {
          parse_words (*argv, fp, 1);
          spam_count++;
          fclose (fp);
        }
      fclose (spam_fp);

      if (verbose)
        fprintf (stderr, PGMNAME": computing probabilities\n");
      calc_probability (veg_count, spam_count);
      
      if (verbose)
        fprintf (stderr, PGMNAME": writing table\n");
      write_table (veg_count, spam_count);

      if (verbose)
        fprintf (stderr, PGMNAME
                 ": %u vegetarian, %u spam, %lu kb memory used\n",
                 veg_count, spam_count,
                 (unsigned long int)total_memory_used/1024);
    }
  else
    {
      unsigned int nwords;
      unsigned int spamicity;

      if (argc < 1)
        usage ();
      read_table (argv[0], &veg_count, &spam_count, &nwords);
      argc--; argv++;
      if (verbose)
        fprintf (stderr, PGMNAME
                 ": %u vegetarian, %u spam, %u words, %lu kb memory used\n",
                 veg_count, spam_count, nwords,
                 (unsigned long int)total_memory_used/1024);
      if (!argc)
        {
          parse_words ("-", stdin, -1);
          spamicity = check_spam (veg_count, spam_count);
          printf ("%2u\n", spamicity);
        }
      else
        {
          for (; argc; argc--, argv++)
            {
              FILE *fp = fopen (argv[0], "r");
              if (!fp)
                {
                  fprintf (stderr, PGMNAME": can't open `%s': %s\n",
                           argv[0], strerror (errno));
                  continue;
                }
              parse_words (argv[0], fp, -1);
              fclose (fp);
              spamicity = check_spam (veg_count, spam_count);
              printf ("%s: %2u\n", argv[0], spamicity);
              reset_hits ();
            }
        }
    }

  return 0;
}

/*
Local Variables:
compile-command: "gcc -Wall -g -o vegetarise vegetarise.c"
End:
*/
