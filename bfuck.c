/* bfuck.c - brainfunck interpreter
 * Copyright (C) 2022 Werner Koch
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define PGM "bfuck"
#define DATASIZE 1000
#define STACKSIZE 10

static int *code;
static size_t codesize, codelen;

static int data[DATASIZE];

static int *stack[STACKSIZE];
static int stackidx;


static int
read_file (const char *fname)
{
  FILE *fp;
  int c;

  fp = fopen (fname, "r");
  if (!fp)
    {
      fprintf (stderr, PGM": error opening '%s': %s\n",
               fname, strerror (errno));
      return -1;
    }

  do
    {
      c = getc (fp);
      if (c == EOF && ferror (fp))
        {
          fprintf (stderr, PGM": error reading '%s': %s\n",
                   fname, strerror (errno));
          fclose (fp);
          return -1;
        }
      if (!code || codelen >= codesize)
        {
          codesize += 1024;
          code = realloc (code, codesize * sizeof *code);
          if (!code)
            {
              fprintf (stderr, PGM": out of code reading '%s'\n", fname);
              fclose (fp);
              return -1;
            }
        }
      code[codelen++] = c;
    }
  while (c != EOF);

  fclose (fp);
  return 0;
}



int
main (int argc, char **argv)
{
  int *ip, *ipend;
  int *dp, *dpend;
  int lnr;

  if (argc)
    {
      argc--;
      argv++;
    }

  while (argc--)
    if (read_file (*argv++))
      return 2;

  if (!code || !codelen)
    {
      fprintf (stderr, PGM ": no program to execute\n");
      return 1;
    }

  setbuf (stdin, NULL);
  setbuf (stdout, NULL);

  ip = code;
  ipend = code + codelen - 1;
  dp = data;
  dpend = data + DATASIZE - 1;
  lnr = 1;
  for ( ; ip <= ipend; ip++)
    {
      switch (*ip)
        {
        case EOF:  /* Prepare for a next file.  */
          putchar ('\n');
          memset (data, 0, DATASIZE * sizeof *data);
          dp = data;
          dpend = data + DATASIZE - 1;
          stackidx = 0;
          break;

        case '>':
          if (dp < dpend)
            dp++;
          else
            {
              fprintf (stderr, PGM ": pointer overflow at line %d\n", lnr);
              return 2;
            }
          break;

        case '<':
          if (dp > data)
            dp--;
          else
            {
              fprintf (stderr, PGM ": pointer underflow at line %d\n", lnr);
              return 2;
            }
          break;

        case '+': ++*dp; break;
        case '-': --*dp; break;
        case '.': putchar (*dp); break;
        case ',': *dp = getchar (); break;

        case '[':
          if (stackidx+1 >= STACKSIZE)
            {
              fprintf (stderr, PGM ": loop too deeply nested at line %d\n",lnr);
              return 2;
            }
          else
            {
              stack[stackidx] = ip;
              stackidx++;
            }
          break;

        case ']':
          if (!stackidx)
            {
              fprintf (stderr, PGM ": no open loop at line %d\n", lnr);
              return 2;
            }
          else
            {
              stackidx--;
              if (*dp)
                {
                  ip = stack[stackidx];
                  ip--; /* fix the bump in the for().  */
                }
            }
          break;

        case '\n': lnr++; break;
        default: break;
        }
    }

  return 0;
}
