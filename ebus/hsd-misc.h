/* hsd-misc.c - Miscellaneous macros for housed.c and housectl.c
 * Copyright (C) 2011 g10 Code GmbH
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
 */

#ifndef HSD_MISC_H
#define HSD_MISC_H


void *xmalloc (size_t n);
void *xstrdup (char const *string);

static inline int
ascii_isspace (int a)
{
  switch (a)
    {
    case ' ': case '\n': case '\r':
    case '\t': case '\f': case '\v':
      return 1;
    default:
      return 0;
    }
}

static inline int
ascii_isupper (int c)
{
  return c >= 'A' && c <= 'Z';
}

static inline int
ascii_islower (int c)
{
  return c >= 'a' && c <= 'z';
}

static inline int
ascii_toupper (int c)
{
  if (c >= 'a' && c <= 'z')
    c &= ~0x20;
  return c;
}

static inline int
ascii_tolower (int c)
{
  if (c >= 'A' && c <= 'Z')
    c |= 0x20;
  return c;
}


int ascii_strcasecmp (char const *a, char const *b);
int ascii_strncasecmp (char const *a, char const *b, size_t n);


/*-- Macros to replace ctype ones to avoid locale problems. --*/
#define spacep(p)   (*(p) == ' ' || *(p) == '\t')
#define digitp(p)   (*(p) >= '0' && *(p) <= '9')
#define hexdigitp(a) (digitp (a)                     \
                      || (*(a) >= 'A' && *(a) <= 'F')  \
                      || (*(a) >= 'a' && *(a) <= 'f'))


/* The atoi macros assume that the buffer has only valid digits. */
#define atoi_1(p)   (*(p) - '0' )
#define atoi_2(p)   ((atoi_1(p) * 10) + atoi_1((p)+1))
#define atoi_4(p)   ((atoi_2(p) * 100) + atoi_2((p)+2))
#define xtoi_1(p)   (*(p) <= '9'? (*(p)- '0'): \
                     *(p) <= 'F'? (*(p)-'A'+10):(*(p)-'a'+10))
#define xtoi_2(p)   ((xtoi_1(p) * 16) + xtoi_1((p)+1))
#define xtoi_4(p)   ((xtoi_2(p) * 256) + xtoi_2((p)+2))

/* Helper macros.  */
#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))

#endif /*HSD_MISC_H*/
