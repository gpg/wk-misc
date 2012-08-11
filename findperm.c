#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void *
xmalloc (size_t n)
{
  void *p = malloc (n);
  if (!p)
    abort();
  return p;
}

char *
strlwr (char *s)
{
    char *p;
    for(p=s; *p; p++ )
	*p = tolower(*(unsigned char *)p);
    return s;
}


int
main (int argc, char**argv)
{
  const unsigned char *s;
  char line [1024];
  char *target;
  char *flags1, *flags2;
  size_t targetlen;

  if (argc != 2)
    return 1;

  targetlen = strlen (argv[1]);
  target = xmalloc (targetlen+1);
  strcpy (target, argv[1]);
  strlwr (target);
  flags1 = xmalloc (256);
  flags2 = xmalloc (256);
  memset (flags1, 0, 256);
  for (s=target; *s; s++)
    flags1[*s]++;

  while ( fgets (line, sizeof line , stdin) )
    {
      if (!*line || strlen (line) != targetlen + 1)
        continue;
      line [targetlen] = 0;
      strlwr (line);
      memset (flags2, 0, 256);
      for (s=line; *s; s++)
        flags2[*s]++;
      if (!memcmp (flags1, flags2, 256))
          printf ("%s\n", line);

    }

  return 0;
}
