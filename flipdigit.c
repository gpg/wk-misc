/* flipdigit.c - Driver for 7seg flip digits display with 4 rows 7cols.
 * Copyright (C) 2023 Werner Koch (dd9jn)
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
/*
 * Build with
 * - ENABLE_JABBER to enable XMPP support
 * - ENABLE_GPIO   to enable using GPIOs to enable transmit mode
 *                 (Default is to assume auto transmit/receive switching)
 * - ENABLE_CUSTOM to compile with flipdigit-custom.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <assert.h>
#ifdef ENABLE_GPIO
# include <linux/gpio.h>
#endif
#ifdef ENABLE_JABBER
# include <strophe.h>  /* The low-level xmpp library.  */
#endif


#define PGM           "flipdigit"
#define PGM_VERSION   "0.1"
#define PGM_BUGREPORT "wk@gnupg.org"

#define DIM(v)		     (sizeof(v)/sizeof((v)[0]))
#define DIMof(type,member)   DIM(((type *)0)->member)
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 5)
#define ATTR_PRINTF(a,b)    __attribute__ ((format (printf,a,b)))
#define ATTR_NR_PRINTF(a,b) __attribute__ ((noreturn,format (printf,a,b)))
#else
#define ATTR_PRINTF(a,b)
#define ATTR_NR_PRINTF(a,b)
#endif
#if __GNUC__ >= 4
# define ATTR_SENTINEL(a) __attribute__ ((sentinel(a)))
#else
# define ATTR_SENTINEL(a)
#endif


/* Option flags. */
static int verbose;
static int debug;
static int line_speed = 19200;
static const char *opt_user;
static const char *opt_pass;
static const char *opt_resource;
static const char *opt_muc;

#ifdef ENABLE_GPIO
static const char *gpio_device = "/dev/gpiochip0";
static int gpio_recv_enable = 18;  /* (negated) */
#endif

/* The file descriptor of our motion sensor.  */
static int motion_fd= -1;

/* True if connected to a jabber server and a global contect.  */
#ifdef ENABLE_JABBER
static int jabber_connected;
static xmpp_ctx_t *jabber_ctx;
static xmpp_conn_t *jabber_conn;
#endif


/* Error counter.  */
static int any_error;

/* We use a global for the RS485 fd.  */
static int rs485_fd;

/* We use a global for the GPIO fd.  */
#ifdef ENABLE_GPIO
static int gpio_fd;
#endif

/* if true the daemon will exit. */
static int shutdown_pending;

/* If set to true, the default clock will be displayed.  */
static int enable_clock;
static int suppress_seconds;

/* If set to true everything received will be displayed.  */
static int talk_mode;

/* The timeout after which the clock will be re-enabled.  */
static unsigned int auto_clock = 60;

/* A linked list of recent senders.  */
struct sender_list_item_s
{
  struct sender_list_item_s *next;
  unsigned int count;  /* How often seen.  */
  char excerpt[20];    /* The last seen command from this jid.  */
  char jid[1];
};
typedef struct sender_list_item_s *sender_list_item_t;
static sender_list_item_t sender_list;


/* The last string shown on the display - basically the current
 * thing.  (malloced) */
static char *last_displayed_string;
/* The current glyphs in 7segment coding.  */
static unsigned char current_glyphs[7*4];


static unsigned char digit_map[10] =
  {
    /* 0 */ 0b01111110,
    /* 1 */ 0b00110000,
    /* 2 */ 0b01101101,
    /* 3 */ 0b01111001,
    /* 4 */ 0b00110011,
    /* 5 */ 0b01011011,
    /* 6 */ 0b01011111,
    /* 7 */ 0b01110000,
    /* 8 */ 0b01111111,
    /* 9 */ 0b01111011
  };

static unsigned char letter_map[26] =
  {
   /* A */ 0b01110111,
   /* b */ 0b00011111,
   /* C */ 0b01001110,
   /* d */ 0b00111101,
   /* E */ 0b01001111,
   /* F */ 0b01000111,
   /* G */ 0b01011110,
   /* H */ 0b00110111,
   /* I */ 0b00000110,
   /* J */ 0b00111100,
   /* K */ 0b01010111,
   /* L */ 0b00001110,
   /* M */ 0b01101010,
   /* n */ 0b00010101,
   /* o */ 0b00011101,
   /* P */ 0b01100111,
   /* q */ 0b01110011,
   /* r */ 0b00000101,
   /* S */ 0b00011011,
   /* t */ 0b00001111,
   /* U */ 0b00111110,
   /* V */ 0b00101010,
   /* W */ 0b00111111,
   /* X */ 0b01001001,
   /* Y */ 0b00111011,
   /* Z */ 0b01101101   /* Same as '2' */
  };


static struct {
  char ascii;
  unsigned char value;
} special_map[] =
  {
   { '+', 0b00000111 },
   { '-', 0b00000001 },
   { '/', 0b00100101 },
   { '@', 0b01111101 },
   { '\\',0b00010011 },
   { 0, 0 }
  };

struct private_membuf_s
{
  size_t len;
  size_t size;
  char *buf;
  int out_of_core;
};
typedef struct private_membuf_s membuf_t;

/*-- Hooks which may be initialized by flipdigit-custom.c --*/

/* The whoop notification is emitted if this hook is NULL or if the
 * hook function returns true.  */
static int (*hook_allow_whoop)(time_t t);

/* Hook used by the is_admin function. */
static int (*hook_is_admin)(const char *name);



/*-- Prototypes --*/
static void die (const char *format, ...) ATTR_NR_PRINTF(1,2);
static void err (const char *format, ...) ATTR_PRINTF(1,2);
static void inf (const char *format, ...) ATTR_PRINTF(1,2);
static void dbg (const char *format, ...) ATTR_PRINTF(1,2);
static char *strconcat (const char *s1, ...) ATTR_SENTINEL(0);
static char *xstrconcat (const char *s1, ...) ATTR_SENTINEL(0);
static void add_to_sender_list (const char *jid, const char *body);
static char *cmd_scroll (char *args);
static char *handle_cmd (char *cmd, const char *sender);
#ifdef ENABLE_CUSTOM
static void custom_main (void);
#endif /*ENABLE_CUSTOM*/



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
  if (!*format || format[strlen(format)-1] != '\n')
    putc ('\n', stderr);

  exit (1);
}


/* Print diagnostic message. */
static void
err (const char *format, ...)
{
  va_list arg_ptr;

  any_error = 1;

  fflush (stdout);
  fprintf (stderr, "%s: ", PGM);

  va_start (arg_ptr, format);
  vfprintf (stderr, format, arg_ptr);
  va_end (arg_ptr);
  if (!*format || format[strlen(format)-1] != '\n')
    putc ('\n', stderr);
}


/* Print an info message. */
static void
inf (const char *format, ...)
{
  va_list arg_ptr;

  if (verbose)
    {
      fprintf (stderr, "%s: ", PGM);

      va_start (arg_ptr, format);
      vfprintf (stderr, format, arg_ptr);
      va_end (arg_ptr);
      if (!*format || format[strlen(format)-1] != '\n')
        putc ('\n', stderr);
    }
}


/* Print a debug message. */
static void
dbg (const char *format, ...)
{
  va_list arg_ptr;

  if (debug)
    {
      fprintf (stderr, "%s: DBG: ", PGM);

      va_start (arg_ptr, format);
      vfprintf (stderr, format, arg_ptr);
      va_end (arg_ptr);
      if (!*format || format[strlen(format)-1] != '\n')
        putc ('\n', stderr);
    }
}

static void *
xmalloc (size_t n)
{
  void *p = malloc (n);
  if (!p)
    die ("out of core\n");
  return p;
}


/* static void * */
/* xrealloc (void *a, size_t newsize) */
/* { */
/*   void *p = realloc (a, newsize); */
/*   if (!p && newsize) */
/*     die ("out of core\n"); */
/*   return p; */
/* } */


static char *
xstrdup (const char *string)
{
  char *buf = xmalloc (strlen (string) + 1);
  strcpy (buf, string);
  return buf;
}


/* Helper for xstrconcat and strconcat.  */
static char *
do_strconcat (int xmode, const char *s1, va_list arg_ptr)
{
  const char *argv[48];
  size_t argc;
  size_t needed;
  char *buffer, *p;

  argc = 0;
  argv[argc++] = s1;
  needed = strlen (s1);
  while (((argv[argc] = va_arg (arg_ptr, const char *))))
    {
      needed += strlen (argv[argc]);
      if (argc >= DIM (argv)-1)
        die ("too may args for strconcat\n");
      argc++;
    }
  needed++;
  buffer = xmode? xmalloc (needed) : malloc (needed);
  for (p = buffer, argc=0; argv[argc]; argc++)
    p = stpcpy (p, argv[argc]);

  return buffer;
}


/* Concatenate the string S1 with all the following strings up to a
   NULL.  Returns a malloced buffer with the new string or NULL on a
   malloc error or if too many arguments are given.  */
static char *
strconcat (const char *s1, ...)
{
  va_list arg_ptr;
  char *result;

  if (!s1)
    result = strdup ("");
  else
    {
      va_start (arg_ptr, s1);
      result = do_strconcat (0, s1, arg_ptr);
      va_end (arg_ptr);
    }
  return result;
}

static char *
xstrconcat (const char *s1, ...)
{
  va_list arg_ptr;
  char *result;

  if (!s1)
    result = xstrdup ("");
  else
    {
      va_start (arg_ptr, s1);
      result = do_strconcat (1, s1, arg_ptr);
      va_end (arg_ptr);
    }
  return result;
}

/* A simple implementation of a dynamic buffer.  Use init_membuf() to
 * create a buffer, put_membuf to append bytes and get_membuf to
 * release and return the buffer.  Allocation errors are detected but
 * only returned at the final get_membuf(), this helps not to clutter
 * the code with out of core checks.  */
void
init_membuf (membuf_t *mb, int initiallen)
{
  mb->len = 0;
  mb->size = initiallen;
  mb->out_of_core = 0;
  mb->buf = malloc (initiallen);
  if (!mb->buf)
    mb->out_of_core = errno;
}

void
put_membuf (membuf_t *mb, const void *buf, size_t len)
{
  if (mb->out_of_core || !len)
    return;

  if (mb->len + len >= mb->size)
    {
      char *p;

      mb->size += len + 1024;
      p = realloc (mb->buf, mb->size);
      if (!p)
        {
          mb->out_of_core = errno ? errno : ENOMEM;
          return;
        }
      mb->buf = p;
    }
  if (buf)
    memcpy (mb->buf + mb->len, buf, len);
  else
    memset (mb->buf + mb->len, 0, len);
  mb->len += len;
}


void
put_membuf_str (membuf_t *mb, const char *string)
{
  if (!string)
    string= "";
  put_membuf (mb, string, strlen (string));
}


void *
get_membuf (membuf_t *mb, size_t *len)
{
  char *p;

  if (mb->out_of_core)
    {
      if (mb->buf)
        {
          free (mb->buf);
          mb->buf = NULL;
        }
      errno = mb->out_of_core;
      return NULL;
    }

  p = mb->buf;
  if (len)
    *len = mb->len;
  mb->buf = NULL;
  mb->out_of_core = ENOMEM; /* hack to make sure it won't get reused. */
  return p;
}


/* Remove leading and trailing white space from STR.  */
static char *
trim_spaces (char *string)
{
  unsigned char *s, *p, *mark;

  p = s = (unsigned char *)string;
  for (; *p && isspace (*p); p++)
    ;
  for (mark=NULL; (*s = *p); s++, p++)
    {
      if (isspace (*p))
        {
          if (!mark)
            mark = s;
        }
      else
        mark = NULL;
    }
  if (mark)
    *mark = 0;

  return string;
}



/* Return true if SENDER (a jid) is allowed to issue admin commands.  */
static int
is_admin (const char *sender)
{
  char *shortjid, *p;
  int result;

  if (!sender || !*sender)
    return 0;
  if (!hook_is_admin)
    return 0;
  shortjid = strdup (sender);
  if (!shortjid)
    {
      err ("out of core in is_admin()");
      return 0;
    }
  p = strchr (shortjid, '/');
  if (p)
    *p = 0;
  result = hook_is_admin (shortjid);
  free (shortjid);
  return result;
}



#ifdef ENABLE_JABBER

static xmpp_stanza_t *
new_name_stanza (xmpp_ctx_t *ctx, const char *name)
{
  xmpp_stanza_t *stanza;
  int rc;

  stanza = xmpp_stanza_new (ctx);
  if (!stanza)
    die ("xmpp_stanza_new failed\n");
  rc = xmpp_stanza_set_name (stanza, name);
  if (rc)
    die ("xmpp_stanza_set_name failed: rc=%d\n", rc);
  return stanza;
}

static xmpp_stanza_t *
new_text_stanza (xmpp_ctx_t *ctx, const char *text)
{
  xmpp_stanza_t *stanza;
  int rc;

  stanza = xmpp_stanza_new (ctx);
  if (!stanza)
    die ("xmpp_stanza_new failed\n");
  rc = xmpp_stanza_set_text (stanza, text);
  if (rc)
    die ("xmpp_stanza_set_text failed: rc=%d\n", rc);
  return stanza;
}

const char *
get_bound_jid (const xmpp_conn_t * const conn)
{
  const char *s = xmpp_conn_get_bound_jid (conn);
  if (!s)
    die ("xmpp_conn_get_bound_jid failed\n");
  return s;
}


/* Send STRING as a MUC message to RECIPIENT.  A resource has already
 * been stripped from RECIPIENT; the recipient itself should have a
 * valid syntax.  */
static void
jabber_send_muc (xmpp_ctx_t *ctx, xmpp_conn_t *const conn,
                 const char *recipient, const char *string)
{
  int rc;
  char *p;
  char *nickbuf = NULL;
  char *recpbuf = NULL;
  const char *nick;
  const char *recp;
  xmpp_stanza_t *stanza = NULL;
  xmpp_stanza_t *stanza2= NULL;
  xmpp_stanza_t *stanza3= NULL;

  nickbuf = strdup (get_bound_jid (conn));
  if (!nickbuf || !(p = strchr (nickbuf, '@')))
    {
      err ("invalid own jid at %d\n", __LINE__);
      goto leave;
    }
  *p = 0;
  nick = nickbuf;

  inf ("sending MUC message to '%s' nick '%s'\n", recipient, nick);

  recp = recpbuf = strconcat (recipient, "/", nick, NULL);
  if (!recp)
    {
      err ("out of core at %d\n", __LINE__);
      goto leave;
    }

  dbg ("sending presence to the room\n");
  stanza = xmpp_presence_new (ctx);
  if (!stanza)
    {
      err ("xmpp_presence_new failed for MUC\n");
      goto leave;
    }
  rc = xmpp_stanza_set_from (stanza, get_bound_jid (conn));
  if (rc)
    {
      err ("xmpp_stanza_set_from failed: rc=%d\n", rc);
      goto leave;
    }
  rc = xmpp_stanza_set_to (stanza, recp);
  if (rc)
    {
      err ("xmpp_stanza_set_to failed: rc=%d\n", rc);
      goto leave;
    }
  rc = xmpp_stanza_set_id (stanza, "pres1");
  if (rc)
    {
      err ("xmpp_stanza_set_id failed: rc=%d\n", rc);
      goto leave;
    }

  /* Tell server that we support the Basic MUC protocol and that we
   * don't want any history.  */
  stanza2 = new_name_stanza (ctx, "x");
  rc = xmpp_stanza_set_ns (stanza2, "http://jabber.org/protocol/muc");
  if (rc)
    {
      err ("xmpp_stanza_set_ns failed: rc=%d\n", rc);
      goto leave;
    }
  stanza3 = new_name_stanza (ctx, "history");
  rc = xmpp_stanza_set_attribute (stanza3, "maxchars", "0");
  if (rc)
    {
      err ("xmpp_stanza_set_attribute failed: rc=%d\n", rc);
      goto leave;
    }
  rc = xmpp_stanza_add_child (stanza2, stanza3);
  if (rc)
    {
      err ("xmpp_stanza_add_child failed: rc=%d\n", rc);
      goto leave;
    }
  xmpp_stanza_release (stanza3);
  stanza3 = NULL;
  rc = xmpp_stanza_add_child (stanza, stanza2);
  if (rc)
    {
      err ("xmpp_stanza_add_child failed: rc=%d\n", rc);
      goto leave;
    }
  xmpp_stanza_release (stanza2);
  stanza2 = NULL;

  xmpp_send (conn, stanza);
  xmpp_stanza_release (stanza);
  stanza = NULL;

  stanza = xmpp_message_new (ctx, "groupchat", recipient, "chat1");
  if (!stanza)
    {
      err ("xmpp_message_new failed for '%s'\n", recipient);
      goto leave;
    }
  rc = xmpp_message_set_body (stanza, string);
  if (rc)
    {
      err ("xmpp_message_set_body failed: rc=%d\n", rc);
      goto leave;
    }
  xmpp_send (conn, stanza);

 leave:
  if (stanza3)
    xmpp_stanza_release (stanza3);
  if (stanza2)
    xmpp_stanza_release (stanza2);
  if (stanza)
    xmpp_stanza_release (stanza);
  free (nickbuf);
  free (recpbuf);
}


/* Handle iq:version stanzas.  */
static int
jabber_version_handler (xmpp_conn_t * const conn, xmpp_stanza_t * const stanza,
                        void * const opaque)
{
  xmpp_ctx_t *ctx = opaque;
  int rc;
  xmpp_stanza_t *reply, *query, *name, *version, *value;
  const char *s;

  inf ("received version request from %s\n", xmpp_stanza_get_from (stanza));

  reply = xmpp_stanza_reply (stanza);
  if (!reply)
    die ("xmpp_stanza_reply failed\n");
  xmpp_stanza_set_type (reply, "result");

  query = new_name_stanza (ctx, "query");
  s = xmpp_stanza_get_ns (xmpp_stanza_get_children (stanza));
  if (s)
    xmpp_stanza_set_ns (query, s);

  name = new_name_stanza (ctx, "name");
  rc = xmpp_stanza_add_child (query, name);
  if (rc)
    die ("xmpp_stanza_add_child failed: rc=%d\n", rc);
  xmpp_stanza_release (name);
  value = new_text_stanza (ctx, PGM);
  rc = xmpp_stanza_add_child (name, value);
  if (rc)
    die ("xmpp_stanza_add_child failed: rc=%d\n", rc);
  xmpp_stanza_release (value);

  version = new_name_stanza (ctx, "version");
  rc = xmpp_stanza_add_child (query, version);
  if (rc)
    die ("xmpp_stanza_add_child failed: rc=%d\n", rc);
  xmpp_stanza_release (version);
  value = new_text_stanza (ctx, PGM_VERSION);
  rc = xmpp_stanza_add_child (version, value);
  if (rc)
    die ("xmpp_stanza_add_child failed: rc=%d\n", rc);
  xmpp_stanza_release (value);

  rc = xmpp_stanza_add_child (reply, query);
  if (rc)
    die ("xmpp_stanza_add_child failed: rc=%d\n", rc);
  xmpp_stanza_release (query);

  xmpp_send (conn, reply);
  xmpp_stanza_release (reply);

  return 1;  /* Keep this handler.  */
}


/* Handle message stanzas.  */
static int
jabber_message_handler (xmpp_conn_t * const conn, xmpp_stanza_t * const stanza,
                        void * const opaque)
{
  xmpp_ctx_t *ctx = opaque;
  const char *type;
  xmpp_stanza_t *child, *achild;
  char *subject, *body;
  const char *code, *errtype;

  type = xmpp_stanza_get_type (stanza);
  dbg ("msg: type='%s'", type);
  if (type && !strcmp (type, "error"))
    {
      child = xmpp_stanza_get_child_by_name (stanza, "error");
      errtype = child? xmpp_stanza_get_attribute (child, "type") : NULL;
      code = child? xmpp_stanza_get_attribute (child, "code") : NULL;
      err ("received error from <%s>: %s=%s\n",
           xmpp_stanza_get_from (stanza),
           code? "code":"type",
           code?  code : errtype);
      achild = xmpp_stanza_get_child_by_name (child, "text");
      body = achild? xmpp_stanza_get_text (achild) : NULL;
      if (body)
        inf ("->%s<-\n", body);
      xmpp_free (ctx, body);
    }
  else if (xmpp_stanza_get_child_by_name (stanza, "body"))
    {
      /* No type but has a body.  */
      child = xmpp_stanza_get_child_by_name (stanza, "subject");
      subject = child? xmpp_stanza_get_text (child) : NULL;

      child = xmpp_stanza_get_child_by_name (stanza, "body");
      body = child? xmpp_stanza_get_text (child) : NULL;

      inf ("received message from <%s> %s%s%s\n", xmpp_stanza_get_from (stanza),
           subject? "(subject: ":"",
           subject? subject:"",
           subject? ")":"");
      if (body)
        {
          char *replystr;
          xmpp_stanza_t *reply;
          const char *sender;

          trim_spaces (body);
          inf ("->%s<-\n", body);
          sender = xmpp_stanza_get_from (stanza);
          add_to_sender_list (sender, body);
          if (*body == '!' && opt_muc && is_admin (sender))
            {
              jabber_send_muc (ctx, conn, opt_muc, body+1);
              replystr = NULL;
            }
          else
            replystr = handle_cmd (body, sender);  /* (modifies BODY) */
          if (replystr)
            {
              reply = xmpp_stanza_reply (stanza);
              if (!xmpp_stanza_get_type (reply))
                xmpp_stanza_set_type (reply, "chat");

              xmpp_message_set_body (reply, replystr);

              xmpp_send (conn, reply);
              xmpp_stanza_release (reply);
              free (replystr);
            }
        }

      xmpp_free (ctx, body);
      xmpp_free (ctx, subject);
      if (shutdown_pending)
        {
          inf ("requesting disconnect\n");
          xmpp_disconnect (conn);
        }
    }

  return 1; /* Keep this handler.  */
}


/* Handle connection events.  */
static void
jabber_conn_handler (xmpp_conn_t * const conn, const xmpp_conn_event_t status,
                     const int error, xmpp_stream_error_t * const stream_error,
                     void * const userdata)
{
  xmpp_ctx_t *ctx = (xmpp_ctx_t *)userdata;
  xmpp_stanza_t* pres;

  if (status == XMPP_CONN_CONNECT)
    {
      inf ("connected\n");

      xmpp_handler_add (conn, jabber_version_handler,
                        "jabber:iq:version", "iq", NULL, ctx);

      xmpp_handler_add (conn, jabber_message_handler,
                        NULL, "message", NULL, ctx);

      /* Send initial presence so that we appear online to contacts */
      pres = xmpp_presence_new (ctx);
      xmpp_send (conn, pres);
      xmpp_stanza_release (pres);
    }
  else
    {
      inf ("disconnected\n");
      xmpp_stop (ctx);
      shutdown_pending = 2;
    }
}
#endif /*ENABLE_JABBER*/




/* Open GPIO device and prepare pins.  Returns the file descriptor to
 * change the GPIO data.  */
#ifdef ENABLE_GPIO
static int
gpio_open (void)
{
  struct gpiohandle_request greq;
  int fd;
  int ret;

  fd = open (gpio_device, O_RDONLY);
  if (fd == -1)
    die ("can't open `%s': %s", gpio_device, strerror (errno));
  /* greq.lineoffsets[0] = gpio_recv_enable; */
  greq.lineoffsets[0] = gpio_data_enable;
  greq.lines = 1;
  greq.flags = GPIOHANDLE_REQUEST_OUTPUT;

  ret = ioctl (fd, GPIO_GET_LINEHANDLE_IOCTL, &greq);
  close (fd);
  if (ret == -1)
    die ("GPIO_GET_LINEHANDLE_IOCTL for '%s' failed: %s\n",
         gpio_device, strerror (errno));

  return greq.fd;
}

static void
gpio_close (int fd)
{
  if (fd != -1)
    close (fd);
}


static void
gpio_write (int fd, int recv_enable, int data_enable)
{
  struct gpiohandle_data gdata;
  int ret;

  /* gdata.values[0] = !!recv_enable; */
  gdata.values[0] = !!data_enable;
  ret = ioctl (fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &gdata);
  if (ret == -1)
    err ("error setting GPIOs: %s\n", strerror (errno));
}
#endif /*ENABLE_GPIO*/

static void
dump_mcbits (int fd)
{
  int mcbits;

  if (ioctl (fd, TIOCMGET, &mcbits))
    err ("TIOCMGET failed: %s\n", strerror (errno));
  else
    inf ("mc: %3s %3s %3s %3s %3s %3s %3s %3s %3s",
         (mcbits & TIOCM_LE )? "LE":"",
         (mcbits & TIOCM_DTR)? "DTR":"",
         (mcbits & TIOCM_DSR)? "DSR":"",
         (mcbits & TIOCM_CAR)? "DCD":"",
         (mcbits & TIOCM_RNG)? "RI":"",
         (mcbits & TIOCM_RTS)? "RTS":"",
         (mcbits & TIOCM_CTS)? "CTS":"",
         (mcbits & TIOCM_ST )? "TX2":"",
         (mcbits & TIOCM_SR )? "RX2":"");
}


static int
open_line (const char *fname)
{
  int fd;
  struct termios term;
  speed_t speed;

  switch (line_speed)
    {
    case 300   : speed = B300   ; break;
    case 600   : speed = B600   ; break;
    case 1200  : speed = B1200  ; break;
    case 2400  : speed = B2400  ; break;
    case 4800  : speed = B4800  ; break;
    case 9600  : speed = B9600  ; break;
    case 19200 : speed = B19200 ; break;
    case 38400 : speed = B38400 ; break;
    case 57600 : speed = B57600 ; break;
    case 115200: speed = B115200; break;
    default:
      die ("unsupported line speed %d given", line_speed);
    }

  fd = open (fname, O_WRONLY);
  if (fd ==  -1)
    die ("can't open '%s': %s", fname, strerror (errno));

  if (tcgetattr (fd, &term))
    die ("tcgetattr(%d) failed: %s", fd, strerror (errno));

  term.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                    | INLCR | IGNCR | ICRNL | IXON);
  term.c_oflag &= ~OPOST;
  term.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  term.c_cflag &= ~(CSIZE | PARENB);
  term.c_cflag |= CS8;

  if (cfsetospeed (&term, speed) || cfsetispeed (&term, speed))
    die ("setting terminal speed to %d failed: %s",
         line_speed, strerror (errno));

  if (tcsetattr (fd, TCSANOW, &term ) )
    die ("tcsetattr(%d) failed: %s", fd, strerror (errno));

  inf ("connected to '%s' at %dbps", fname, line_speed);
  dump_mcbits (fd);

  return fd;
}


/* Return the next mask for the display.  This uses a statically
 * allocated array.  If RESET is true the sequence of masks returned
 * starts again.  Returns NULL to indicate that all masks have been
 * produced.  */
static const unsigned char *
prepare_mask (int reset)
{
  static unsigned char mask[4*7];
  static unsigned int seq;
  static char southeast[9][7] =
    { { 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      { 0x25, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00 },
      { 0x18, 0x25, 0x42, 0x00, 0x00, 0x00, 0x00 },
      { 0x00, 0x18, 0x25, 0x42, 0x00, 0x00, 0x00 },
      { 0x00, 0x00, 0x18, 0x25, 0x42, 0x00, 0x00 },
      { 0x00, 0x00, 0x00, 0x18, 0x25, 0x42, 0x00 },
      { 0x00, 0x00, 0x00, 0x00, 0x18, 0x25, 0x42 },
      { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x25 },
      { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18 } };
  int idx;

  if (reset)
    {
      seq = 0;
      memset (mask, 0, sizeof mask);
    }
  else if (seq >= 22)
    return NULL;

  for (idx=0; idx < 7; idx++)
    mask[idx] |= (seq < 9)? southeast[seq][idx] : 0;
  for (; idx < 14; idx++)
    mask[idx] |= (seq > 2 && seq < 12)? southeast[seq-3][idx] : 0;
  for (; idx < 21; idx++)
    mask[idx] |= (seq > 5 && seq < 15)? southeast[seq-6][idx] : 0;
  for (; idx < 28; idx++)
    mask[idx] |= (seq > 8 && seq < 18)? southeast[seq-9][idx] : 0;

  seq++;
  return mask;
}




/* Send the data to the display.  */
static int
send_data (int cmd, const unsigned char *data, unsigned int datalen)
{
  unsigned char buf[32];
  unsigned char *bufptr;
  unsigned int buflen;
  int i;
  int nwritten;
  int ret = 0;

  assert (datalen <= 28);

  buflen = 0;
  buf[buflen++] = 0x80; /* Start of frame marker.   */
  buf[buflen++] = cmd;
  if (cmd != 0x82)
    buf[buflen++] = 0xff; /* Our device address.      */
  for (i=0; i < datalen; i++)
    buf[buflen++] = data[i];
  buf[buflen++] = 0x8f; /* End of frame marker.     */

#ifdef ENABLE_GPIO
  gpio_write (gpio_fd, 0, 1);  /* Enable transmitter.  */
#endif

  bufptr = buf;
  while (buflen > 0)
    {
      nwritten = write (rs485_fd, bufptr, buflen);

      if (nwritten < 0 && errno == EINTR)
        continue;
      if (nwritten < 0)
        {
          ret = -1;  /* Error */
          break;
        }
      buflen -= nwritten;
      bufptr += nwritten;
    }
  if (!buflen)
    ret = 0;

  {
    int save_errno = errno;
    if (tcdrain (rs485_fd))
      err ("tcdrain failed: %s\n", strerror (errno));
#ifdef ENABLE_GPIO
    gpio_write (gpio_fd, 0, 0); /* Disable transmitter.  */
#endif
    errno = save_errno;
  }

  if (ret)
    err ("send_data failed: %s\n", strerror (errno));
  return ret;
}


/* Map C to the 7 segment code.  Returns 0x80 for no mapping. */
static unsigned char
map_ascii (int c)
{
  if (c >= '0' && c <= '9')
    return digit_map[c - '0'];
  else if (c >= 'A' && c <= 'Z')
    return letter_map[c - 'A'];
  else if (c >= 'a' && c <= 'z')
    return letter_map[c - 'a'];
  else if (c == ' ' || c == '\t')
    return 0;
  else
    {
      int i;

      for (i=0; special_map[i].ascii; i++)
        if (special_map[i].ascii == c)
          return special_map[i].value;

      return 0x80;
    }
}


static void
map_string_to_data (const char *s, unsigned char *data, size_t datalen)
{
  int idx;

  for (idx=0; idx < datalen; idx++)
    {
      if (*s)
        {
          data[idx] = map_ascii (*s);
          if ((data[idx] & 0x80))
            {
              data[idx] = 0;
              if (*s == '\n')
                {
                  if (idx % 7)
                    idx += 7-(idx%7);
                  idx--;
                }
            }
          s++;
        }
    }
}


/* Show STRING on the display.  */
static void
show_string (const char *string)
{
  unsigned char data[28];

  if (last_displayed_string && string
      && !strcmp (last_displayed_string, string))
    return;

  memset (data, 0, sizeof data);
  map_string_to_data (string, data, sizeof data);
  send_data (0x83, data, sizeof data);
  /* Now keep a copy.  */
  assert (sizeof current_glyphs == sizeof data);
  memcpy (current_glyphs, data, sizeof data);
  dbg ("displaying '%s'", string);
  free (last_displayed_string);
  if (string)
    last_displayed_string = strdup (string);
}


/* Show STRING on the display using the given mask. */
static void
show_with_mask (const char *string, const unsigned char *mask, int fill)
{
  unsigned char data[28];
  int i;

  memset (data, 0, sizeof data);
  map_string_to_data (string, data, sizeof data);
  for (i=0; i < 28; i++)
    if (fill)
      data[i] = mask[i];
    else
      data[i] &= ~mask[i];
  send_data (0x83, data, sizeof data);
}



static const char *
shortmonthstr (int idx)
{
  switch (idx)
    {
    case  0: return "Jan";
    case  1: return "Feb";
    case  2: return "Mar";
    case  3: return "Apr";
    case  4: return "Mai";
    case  5: return "Jun";
    case  6: return "Jul";
    case  7: return "Aug";
    case  8: return "Sep";
    case  9: return "Oct";
    case 10: return "Nov";
    case 11: return "Dez";
    default: return "???";
    }
}


/* Run the loop  */
static void
run_loop (void)
{
  static time_t nextwhoop;
  static time_t lastatime;
  static time_t lastnoclock;
  static int mtrigger;
  struct tm *tp;
  time_t atime;
  int nread;
  char textbuf[100];

  while (shutdown_pending != 2)
    {
      atime = time (NULL);

      if (motion_fd != -1 && atime > nextwhoop)
        {
          char line[32];

          lseek (motion_fd, 0, SEEK_SET);
          nread = read (motion_fd, line, sizeof line - 1);
          if (nread > 0)
            {
              line[nread] = 0;
              if (*line && atoi (line) < 1000)
                mtrigger = 1;
            }
        }

      /* Auto-re-enable clock after a minute or whatever auto_clock gives.  */
      if (!enable_clock && atime > (lastnoclock + auto_clock))
        {
          enable_clock = 1;
          lastnoclock = 0;
        }
      else if (!enable_clock && !lastnoclock)
        lastnoclock = atime;

      if (atime > lastatime && enable_clock)
        {
          tp = localtime (&atime);
          if (!tp->tm_sec && !(tp->tm_min % 15))
            cmd_scroll (NULL);
          snprintf (textbuf, sizeof textbuf,
                    "%s\n"
                    " %2d %s\n"
                    "%02duhr%02d%c"
                    "%02d sec%c",
                    tp->tm_wday == 1? "Montag":
                    tp->tm_wday == 2? "Dinstag":
                    tp->tm_wday == 3? "Mittwch":
                    tp->tm_wday == 4? "Donntag":
                    tp->tm_wday == 5? "Freitag":
                    tp->tm_wday == 6? "Samstag": "Sonntag",
                    tp->tm_mday, shortmonthstr (tp->tm_mon),
                    tp->tm_hour, tp->tm_min,
                    suppress_seconds? 0:'\n', tp->tm_sec,
                    mtrigger ? '+':' ');
          show_string (textbuf);

        }
      lastatime = atime;
      if (mtrigger && jabber_ctx && opt_muc &&  atime > nextwhoop)
        {
          mtrigger = 0;
          nextwhoop = atime + 10*60;
          if (!hook_allow_whoop || hook_allow_whoop (atime))
            jabber_send_muc (jabber_ctx, jabber_conn, opt_muc, "/me whoop");
        }

#ifdef ENABLE_JABBER
      if (jabber_ctx && jabber_connected)
        {
          xmpp_run_once (jabber_ctx, 250);
        }
      else
#endif /*ENABLE_JABBER*/
        sleep (1);
    }
}



/* Add the sender JID to the list of senders along with the begin of
 * body.  */
static void
add_to_sender_list (const char *jid, const char *body)
{
  sender_list_item_t item;
  int i;

  for(item=sender_list; item; item = item->next)
    if (!strcmp (item->jid, jid))
      break;
  if (!item)
    {
      item = calloc (1, sizeof *item + strlen (jid));
      if (!item)
        {
          err ("%s: malloc failed: %s\n", __func__, strerror (errno));
          return;
        }
      strcpy (item->jid, jid);
      item->next = sender_list;
      sender_list = item;
    }
  item->count++;
  if (body && strncmp (body, "/last", 5))
    {
      /* Note that we do not any command starting with "/last".  */
      strncpy (item->excerpt, body, sizeof item->excerpt - 1);
      item->excerpt[sizeof item->excerpt - 1] = 0;
      for (i=0; item->excerpt[i]; i++)
        if (item->excerpt[i] < 32 || item->excerpt[i] > 126)
          item->excerpt[i] = '.';
    }

  /* FIXME: Limit the size of the list.  */
}



static char *
cmd_read (char *args)
{
  (void)args;
  return strdup (last_displayed_string?last_displayed_string:"[empty]");
}


static char *
cmd_write (char *args)
{
  enable_clock = 0;
  show_string (args);
  return NULL;
}


static char *
cmd_talk (char *args)
{
  char *res = NULL;

  if (!*args)
    talk_mode = !talk_mode;
  else if (!strcasecmp (args, "on") || !strcmp (args, "1"))
    talk_mode = 1;
  else if (!strcasecmp (args, "off") || !strcmp (args, "0"))
    talk_mode = 0;
  else
    res = strdup ("error: invalid arg - use on, off, or no arg to toggle");
  if (!res)
    res = strdup (talk_mode? "Talk mode enabled":"Talk mode disabled");
  return res;
}


static char *
cmd_echo (char *args)
{
  return strdup (args);
}


static char *
cmd_clock (char *args)
{
  char *res = NULL;

  if (!*args)
    enable_clock = !enable_clock;
  else if (!strcasecmp (args, "on") || !strcmp (args, "1"))
    enable_clock = 1;
  else if (!strcasecmp (args, "off") || !strcmp (args, "0"))
    enable_clock = 0;
  else if (!strcasecmp (args, "nosec") || !strcasecmp (args, "sec"))
    suppress_seconds = (*args == 'n' || *args == 'N');
  else if (atoi (args) > 0)
    auto_clock = atoi (args);
  else
    res = strdup ("error: invalid arg - use on, off, or no arg to toggle");
  if (!res)
    res = strdup (enable_clock? "Clock shown":"Clock not shown");
  return res;
}


/* Show the flip digit display as ascii art.
 * col:  0     1     2     3     4     5     6
 * idx:  012345012345012345012345012345012345012345
 *     0  --    --    --    --    --    --    --
 *     1 |  |  |  |  |  |  |  |  |  |  |  |  |  |
 *     2  --    --    --    --    --    --    --
 *     3 |  |  |  |  |  |  |  |  |  |  |  |  |  |
 *     4  --    --    --    --    --    --    --
 * followed  by empty line and the repeated 3 times.
 */
static char *
cmd_show (char *args)
{
  char lines[5][7*6];  /* We need 5 lines per row.  */
  int row, col, i;
  unsigned char glyph;
  membuf_t mb;

  (void)args;

  init_membuf (&mb, 0);
  put_membuf (&mb, "```\n", 4);
  for (row=0; row < 4; row++)
    {
      if (row)
        put_membuf (&mb, "\n", 1);  /* An empty line.  */
      memset (lines, ' ', sizeof lines);
      for (col=0; col < 7; col++)
        {
          glyph = current_glyphs[7*row + col];
          if ((glyph & 0b01000000))
            lines[0][col*6+1] = '-', lines[0][col*6+2] = '-';
          if ((glyph & 0b00100000))
            lines[1][col*6+3] = '|';
          if ((glyph & 0b00010000))
            lines[3][col*6+3] = '|';
          if ((glyph & 0b00001000))
            lines[4][col*6+1] = '-', lines[4][col*6+2] = '-';
          if ((glyph & 0b00000100))
            lines[3][col*6+0] = '|';
          if ((glyph & 0b00000010))
            lines[1][col*6+0] = '|';
          if ((glyph & 0b00000001))
            lines[2][col*6+1] = '-', lines[2][col*6+2] = '-';
        }
      for (i=0; i < 5; i++)
        {
          put_membuf (&mb, lines[i], 7*6);
          put_membuf (&mb, "\n", 1);
        }
    }
  put_membuf (&mb, "```", 4);

  return get_membuf (&mb, NULL);
}


/* A simple last command listing the last seen new senders. */
static char *
cmd_last (char *args)
{
  membuf_t mb;
  sender_list_item_t item;
  int nitems;
  char numbuf[20];

  (void)args;

  init_membuf (&mb, 0);
  put_membuf_str (&mb, "```");
  for (item=sender_list,nitems=0; item && nitems < 20; item=item->next,nitems++)
    {
      snprintf (numbuf, sizeof numbuf, "%5u ", item->count);
      put_membuf_str (&mb, numbuf);
      put_membuf_str (&mb, item->jid);
      put_membuf_str (&mb, " (");
      put_membuf_str (&mb, item->excerpt);
      put_membuf_str (&mb, " )\n");
    }
  put_membuf_str (&mb, "```");
  put_membuf (&mb, "", 1);

  return get_membuf (&mb, NULL);
}


static char *
cmd_scroll (char *args)
{
  const unsigned char *mask;
  const char *string;

  (void)args;

  string = last_displayed_string;
  if (!string)
    string = "";

  for (mask = prepare_mask (1); mask; mask = prepare_mask (0))
    {
      show_with_mask (string, mask, 1);
      usleep (100*1000);
    }
  for (mask = prepare_mask (1); mask; mask = prepare_mask (0))
    {
      show_with_mask (string, mask, 0);
      usleep (100*1000);
    }
  for (mask = prepare_mask (1); mask; mask = prepare_mask (0))
    {
      show_with_mask (string, mask, 1);
      usleep (100*1000);
    }
  for (mask = prepare_mask (1); mask; mask = prepare_mask (0))
    {
      show_with_mask (string, mask, 0);
      usleep (100*1000);
    }
  return NULL;
}


static char *
cmd_shutdown (char *args)
{
  (void)args;
  shutdown_pending = 1;
  return strdup (PGM " shutdown in progress");
}


static char *
cmd_help (char *args)
{
  (void)args;
  return strconcat ("```"
                    "/read          Show the current display\n"
                    "/write STRING  Show STRING on the display and\n"
                    "               disable the clock\n"
                    "/clock [OPT]   Control the clock display:\n"
                    "               OPT is \"on\"    - enable clock\n"
                    "               OPT is \"off\"   - disable clock\n"
                    "               OPT is \"sec\"   - enable seconds\n"
                    "               OPT is \"nosec\" - disable seconds\n"
                    "               No OPT toggles the clock display\n"
                    "/show          Show the display in ascii art\n"
                    "/talk [on|off] Control talk mode\n"
                    "/last          Show the last users\n",
                    "```", NULL
                    );
}


/* Handle a command.  The function may modify CMD.  The returned value
 * must be free'd by the caller.  SENDER is the sender of the command. */
static char *
handle_cmd (char *cmd, const char *sender)
{
  static struct
  {
    const char *name;
    char *(*func)(char*args);
    int adminonly;
  } cmdtbl[] = {
    { "read",   cmd_read },
    { "write",  cmd_write },
    { "talk",   cmd_talk, 1 },
    { "echo",   cmd_echo },
    { "clock",  cmd_clock },
    { "show",   cmd_show },
    { "last",   cmd_last },
    { "scroll", cmd_scroll },
    { "shutdown", cmd_shutdown, 1 },
    { "help",     cmd_help },
    { NULL, NULL }
  };
  char dummyargs[1] = { 0 };
  char *args;
  int i;
  char *res = NULL;

  if (!cmd || cmd[0] != '/' || !cmd[1])
    {
      if (cmd && talk_mode)
        {
          enable_clock = 0;
          show_string (cmd);
          return NULL;
        }
      return strdup ("info: commands must start with a slash");
    }
  cmd++;
  args = strpbrk (cmd, " \t\n\v");
  if (args)
    {
      *args++ = 0;
      trim_spaces (args);
    }
  else
    args = dummyargs;

  for (i=0; cmdtbl[i].name; i++)
    if (!strcasecmp (cmdtbl[i].name, cmd))
      break;
  if (!cmdtbl[i].name)
    res = strdup ("error: No such command; try \"/help\"");
  else if (cmdtbl[i].adminonly && !is_admin (sender))
    res = strdup ("error: Please go away; you are not an admin");
  else if (cmdtbl[i].func)
    res = cmdtbl[i].func (args);

  return res;
}



/* Read our config file.  */
static void
read_config (void)
{
  char *fname;
  const char *s;
  FILE *fp;
  char line[512];
  int c;
  char *user, *pass;

  s = getenv ("HOME");
  if (!s)
    s = "";
  fname = xstrconcat (s, "/.txxmpprc", NULL);
  fp = fopen (fname, "r");
  if (!fp)
    {
      free (fname);
      return;
    }

  user = pass = NULL;
  while (fgets (line, sizeof line, fp))
    {
      if (line[strlen (line)-1] != '\n')
        {
          while ((c = getc (fp)) != EOF && c != '\n')
            ;
          err ("warning: ignoring rest of overlong line in '%s'\n", fname);
        }
      if (*line == '#')
        continue;
      trim_spaces (line);
      if (!*line)
        continue;
      user = strtok (line, " \t");
      if (user)
        pass = strtok (NULL, " \t");
      else
        pass = NULL;

      if (!opt_user) /* Take the first line and we are done.  */
        {
          opt_user = xstrdup (user);
          if (!opt_pass && pass)
            opt_pass = xstrdup (pass);
          break;
        }

      if (!strcmp (opt_user, user) && !opt_pass)  /* Password found.  */
        {
          opt_pass = xstrdup (pass);
          break;
        }
    }

  fclose (fp);
  free (fname);
}


static int
show_usage (int ex)
{
  fputs ("Usage: " PGM " DEVICE\n"
         "Drive a flip digits display via RS485\n\n"
         "  --version      Print program version\n"
         "  --speed N      Use given speed\n"
         "  --user JID     Connect as JID\n"
         "  --pass PASS    Override password with PASS\n"
         "  --resource RES Override default resource with RES\n"
         "  --muc JID      Also connect to thge MUC JID\n"
         "  --verbose      Enable extra informational output\n"
         "  --debug        Enable additional debug output\n"
         "  --help         Display this help and exit\n\n"
         "The password is taken from the ~/.txmpprc file where the\n"
         "first non-comment line specifies the default user:\n"
         "  ----- 8< ----- 8< ----- 8< -----\n"
         "  # Example config for txxmppp\n"
         "  foo@jabber.example.org PASSWORD\n"
         "  bar@example.net PASSWORD\n"
         "  ----- >8 ----- >8 ----- >8 -----\n"
         "Report bugs to " PGM_BUGREPORT ".\n",
         ex? stderr:stdout);
  exit (ex);
}


int
main (int argc, char **argv )
{
  int last_argc = -1;
  int rc;

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
      else if (!strcmp (*argv, "--version"))
        {
          fputs (PGM " " PGM_VERSION "\n", stdout);
          exit (0);
        }
      else if (!strcmp (*argv, "--help"))
        {
          show_usage (0);
        }
      else if (!strcmp (*argv, "--speed"))
        {
          argc--; argv++;
          if (argc)
            {
              line_speed = atoi (*argv);
              argc--; argv++;
            }
        }
      else if (!strcmp (*argv, "--user"))
        {
          argc--; argv++;
          if (!argc || !**argv || !strcmp (*argv, "--"))
            die ("argument missing for option '%s'\n", argv[-1]);
          opt_user = *argv;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--pass"))
        {
          argc--; argv++;
          if (!argc || !**argv || !strcmp (*argv, "--"))
            die ("argument missing for option '%s'\n", argv[-1]);
          opt_pass = *argv;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--resource"))
        {
          argc--; argv++;
          if (!argc || !**argv || !strcmp (*argv, "--"))
            die ("argument missing for option '%s'\n", argv[-1]);
          opt_resource = *argv;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--muc"))
        {
          argc--; argv++;
          if (!argc || !**argv || !strcmp (*argv, "--"))
            die ("argument missing for option '%s'\n", argv[-1]);
          opt_muc = *argv;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--verbose"))
        {
          verbose = 1;
          argc--; argv++;
        }
      else if (!strcmp (*argv, "--debug"))
        {
          verbose = debug = 1;
          argc--; argv++;
        }
      else if (!strncmp (*argv, "--", 2))
        show_usage (1);
    }

  if (argc != 1)
    show_usage (1);

  enable_clock = 1;      /* Start up with the clock.     */
  suppress_seconds = 0;  /* Display the seconds.  */

  setvbuf (stdout, NULL, _IOLBF, 0);

  read_config ();

  rs485_fd = open_line (*argv);
  assert (rs485_fd != -1);

#ifdef ENABLE_GPIO
  gpio_fd = gpio_open ();
  if (gpio_fd == -1)
    die ("failed to open GPIO device");
#endif

  motion_fd = open ("/etc/motion/sensor-1" , O_RDONLY);
  if (motion_fd == -1)
    err ("running w/o motion sensor support: %s", strerror (errno));

#ifdef ENABLE_JABBER
  if (!opt_user || !*opt_user || !opt_pass || !*opt_pass)
    {
      if (!opt_user || !*opt_user)
        inf ("error: no user given\n");
      if (!opt_pass || !*opt_pass)
        inf ("error: no password given\n");
      inf ("running without XMPP support");
      opt_user = NULL;
    }
  if (opt_user)
    {
      xmpp_conn_t *conn;

      xmpp_initialize ();
      jabber_ctx = xmpp_ctx_new (NULL,
                   (debug? xmpp_get_default_logger (XMPP_LEVEL_DEBUG)
                    /* */    : NULL));
      if (!jabber_ctx)
        die ("xmpp_ctx_new failed\n");

      conn = xmpp_conn_new (jabber_ctx);
      if (!conn)
        die ("xmpp_conn_new failed\n");

      xmpp_conn_set_jid (conn, opt_user);
      xmpp_conn_set_pass (conn, opt_pass);
      rc = xmpp_connect_client (conn, NULL, 0, jabber_conn_handler, jabber_ctx);
      if (rc)
        die ("xmpp_connect_client failed: rc=%d\n", rc);
      else
        {
          jabber_connected = 1;
          jabber_conn = conn;
        }
    }
  else
    {
      jabber_ctx = NULL;
      jabber_conn = NULL;
    }
#endif /*ENABLE_JABBER*/

#ifdef ENABLE_CUSTOM
  custom_main ();
#endif

  run_loop ();

#ifdef ENABLE_JABBER
  if (jabber_conn)
    {
      xmpp_conn_release (jabber_conn);
      xmpp_ctx_free (jabber_ctx);
      xmpp_shutdown ();
    }
#endif /*ENABLE_JABBER*/

  if (motion_fd != -1)
    {
      close (motion_fd);
      motion_fd = -1;
    }

#ifdef ENABLE_GPIO
  gpio_close (gpio_fd);
  gpio_fd = -1;
#endif

  close (rs485_fd);
  rs485_fd = -1;

  return any_error? 1:0;
}

#ifdef ENABLE_CUSTOM
# include "flipdigit-custom.c"
#endif

/*
Local Variables:
compile-command: "gcc -Wall -DENABLE_JABBER -lstrophe -o flipdigit flipdigit.c"
End:
*/
