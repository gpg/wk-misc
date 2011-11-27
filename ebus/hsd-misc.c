/* hsd-misc.c - Miscellaneous functions for housed and housectl
 * Copyright (C) 1998, 1999, 2000, 2001, 2003, 2004, 2005, 2006, 2007,
 *               2008, 2009, 2010  Free Software Foundation, Inc.
 * Copyright (C) 2011 Werner Koch (dd9jn)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/* Some code has been taken from the jnlib part og gnupg.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hsd-misc.h"

static void
out_of_core (char const *string)
{
  fprintf (stderr, "out of core in %s\n", string);
  exit (2);
}


void *
xmalloc (size_t n)
{
  void *p = malloc (n);
  if (!p)
    out_of_core ("xmalloc");
  return p;
}

void *
xstrdup (char const *string)
{
  char *p;

  p = malloc (strlen (string)+1);
  if (!p)
    out_of_core ("xstrdup");
  strcpy (p, string);
  return p;
}


int
ascii_strcasecmp (char const *a, char const *b)
{
  if (a == b)
    return 0;

  for (; *a && *b; a++, b++)
    {
      if (*a != *b && ascii_toupper (*a) != ascii_toupper (*b))
        break;
    }
  return *a == *b? 0 : (ascii_toupper (*a) - ascii_toupper (*b));
}


int
ascii_strncasecmp (char const *a, char const *b, size_t n)
{
  unsigned char const *p1 = (unsigned char const *)a;
  unsigned char const *p2 = (unsigned char const *)b;
  unsigned char c1, c2;

  if (p1 == p2 || !n )
    return 0;

  do
    {
      c1 = ascii_tolower (*p1);
      c2 = ascii_tolower (*p2);

      if ( !--n || c1 == '\0')
	break;

      ++p1;
      ++p2;
    }
  while (c1 == c2);

  return c1 - c2;
}
