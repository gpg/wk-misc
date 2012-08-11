#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PGM "epoch2iso"


int
main (int argc, char **argv)
{
  long value;
  struct tm *tp;
  time_t atime;

  if (argc != 2)
    {
      fprintf (stderr, "usage: " PGM " seconds_since_Epoch\n");
      return 1;
    }

  value = strtol (argv[1], NULL, 0);
  if (value < 0)
    {
      fprintf (stderr, PGM ": invalid time given\n");
      return 1;
    }

  atime = value;
  tp = gmtime (&atime);

  printf("%04d-%02d-%02d %02d:%02d:%02d\n",
         1900+tp->tm_year, tp->tm_mon+1, tp->tm_mday,
         tp->tm_hour, tp->tm_min, tp->tm_sec);
  return 0;
}

/*
Local Variables:
compile-command: "cc -Wall -o epoch2iso epoch2iso.c"
End:
*/
