/* encrypt-to-p.c - Do we have an encrypt-to in gpg.conf?
 * Copyright (C) 2008 g10 Code GmbH
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
#include <locale.h>
#include <gpgme.h>

#define fail_if_err(err)					\
  do								\
    {								\
      if (err)							\
        {							\
          fprintf (stderr, "%s:%d: %s: %s\n",			\
                   __FILE__, __LINE__, gpgme_strsource (err),	\
		   gpgme_strerror (err));			\
          exit (1);						\
        }							\
    }								\
  while (0)


int 
main (int argc, char **argv)
{
  gpg_error_t err;
  gpgme_ctx_t ctx;
  gpgme_conf_comp_t conf;
  gpgme_conf_comp_t comp;
  gpgme_conf_opt_t opt;
  int result = 1;

  gpgme_check_version (NULL);
  setlocale (LC_ALL, "");
  gpgme_set_locale (NULL, LC_CTYPE, setlocale (LC_CTYPE, NULL));
  gpgme_set_locale (NULL, LC_MESSAGES, setlocale (LC_MESSAGES, NULL));

  err = gpgme_engine_check_version (GPGME_PROTOCOL_OpenPGP);
  fail_if_err (err);

  err = gpgme_new (&ctx);
  fail_if_err (err);

  err = gpgme_op_conf_load (ctx, &conf);
  fail_if_err (err);

  for (comp = conf; comp;  comp = comp->next)
    {
      if (!strcmp (comp->name, "gpg"))
        {
          for (opt = comp->options; opt; opt = opt->next)
            if ( !(opt->flags & GPGME_CONF_GROUP)
                 && !strcmp (opt->name, "encrypt-to"))
              {
                if (opt->value && opt->alt_type == GPGME_CONF_STRING)
                  {
                    printf ("%s\n", opt->value->value.string);
                    result = 0;
                  }
                break;
              }
          break;
        }
    }

  gpgme_conf_release (conf);
  gpgme_release (ctx);

  return result;
}

/*
Local Variables:
compile-command: "gcc -Wall -g -lgpgme -o encrypt-to-p encrypt-to-p.c"
End:
*/
