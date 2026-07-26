/*
 *  GTetrinet
 *  Copyright (C) 1999, 2000, 2001, 2002, 2003  Ka-shu Wong (kswong@zip.com.au)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <glib/gi18n.h>

#include "client.h"
#include "tetrinet.h"
#include "partyline.h"
#include "dialogs.h"
#include "misc.h"
#include "gtetrinet.h"
#include "sched.h"

/* IRIX 6.5's own <stdio.h> predates C99 and never declares snprintf at
   all (not even behind a feature-test macro) despite libc actually
   providing a working symbol for it -- confirmed by grepping the real
   system header on the O2. __sgi is GCC's standard predefined macro for
   this target. */
#if defined(__sgi)
extern int snprintf (char *str, size_t size, const char *format, ...);
#endif

#define PORT 31457
#define SPECPORT 31458

int connected;
char server[128];

/* Set once the SDL app's own per-frame event loop has actually started
   (mirrors GTK's gtk_main_level() > 0, which client_disconnect() used to
   check to decide whether emitting a synthetic "disconnect" inmessage
   made sense yet). Owned and set by the app's main loop startup code. */
int app_mainloop_running = 0;

static int sock;
/* connect_pending: client_process() kicks off the background resolve
   thread and returns immediately instead of blocking (there is no GTK
   main loop to spin via gtk_main_iteration() while waiting) -- 1 while a
   resolve+connect is in flight, checked by client_poll_connect(), which
   the app calls once per frame to pick up where client_process() left
   off once `resolved` is set by the background thread. */
static int connect_pending;
static int resolved;

/* structures and arrays for message translation */

struct inmsgt {
    enum inmsg_type num;
    char *str;
};

struct outmsgt {
    enum outmsg_type num;
    char *str;
};

/* some of these strings change depending on the game mode selected */
/* these changes are put into effect through the function inmsg_change */
struct inmsgt inmsgtable[] = {
    {IN_CONNECT, "connect"},
    {IN_DISCONNECT, "disconnect"},

    {IN_CONNECTERROR, "noconnecting"},
    {IN_PLAYERNUM, "playernum"},
    {IN_PLAYERJOIN, "playerjoin"},
    {IN_PLAYERLEAVE, "playerleave"},
    {IN_KICK, "kick"},
    {IN_TEAM, "team"},
    {IN_PLINE, "pline"},
    {IN_PLINEACT, "plineact"},
    {IN_PLAYERLOST, "playerlost"},
    {IN_PLAYERWON, "playerwon"},
    {IN_NEWGAME, "newgame"},
    {IN_INGAME, "ingame"},
    {IN_PAUSE, "pause"},
    {IN_ENDGAME, "endgame"},
    {IN_F, "f"},
    {IN_SB, "sb"},
    {IN_LVL, "lvl"},
    {IN_GMSG, "gmsg"},
    {IN_WINLIST, "winlist"},

    {IN_SPECJOIN, "specjoin"},
    {IN_SPECLEAVE, "specleave"},
    {IN_SPECLIST, "speclist"},
    {IN_SMSG, "smsg"},
    {IN_SACT, "sact"},

    {IN_BTRIXNEWGAME, "btrixnewgame"},

    {0, 0}
};

static struct inmsgt *get_inmsg_entry(enum inmsg_type num)
{
    int i;
    for (i = 0; inmsgtable[i].num && inmsgtable[i].num != num; i ++);
    return &inmsgtable[i];
}

static void inmsg_change()
{
    switch (gamemode) {
    case ORIGINAL:
        get_inmsg_entry(IN_PLAYERNUM)->str = "playernum";
        get_inmsg_entry(IN_NEWGAME)->str = "newgame";
        break;
    case TETRIFAST:
        get_inmsg_entry(IN_PLAYERNUM)->str = ")#)(!@(*3";
        get_inmsg_entry(IN_NEWGAME)->str = "*******";
        break;
    }
}

struct outmsgt outmsgtable[] = {
    {OUT_DISCONNECT, "disconnect"},
    {OUT_CONNECTED, "connected"},

    {OUT_TEAM, "team"},
    {OUT_PLINE, "pline"},
    {OUT_PLINEACT, "plineact"},
    {OUT_PLAYERLOST, "playerlost"},
    {OUT_F, "f"},
    {OUT_SB, "sb"},
    {OUT_LVL, "lvl"},
    {OUT_STARTGAME, "startgame"},
    {OUT_PAUSE, "pause"},
    {OUT_GMSG, "gmsg"},

    {OUT_VERSION, "version"},

    {OUT_CLIENTINFO, "clientinfo"},

    {0, 0}
};

/* functions which set up the connection */
static void client_process (void);
static gpointer client_resolv_hostname (void);
static void client_connected (void);
static void client_finish_connecting (void);

/* some other useful functions */
static int client_sendmsg (char *str);
static int client_readmsg (gchar **str);
static void server_ip (unsigned char buf[4]);

enum inmsg_type inmsg_translate (char *str);
char *outmsg_translate (enum outmsg_type);

void client_init (const char *s, const char *n)
{
    int i;
    GTET_O_STRCPY(server, s);
    GTET_O_STRCPY(nick, n);

    connectingdialog_new ();

    /* wipe spaces off the nick */
    for (i = 0; nick[i]; i ++)
      if (isspace (nick[i]))
        nick[i] = 0;

    /* set the game mode */
    inmsg_change();
    
    client_process ();
}

void client_outmessage (enum outmsg_type msgtype, char *str)
{
    char buf[1024];
    GTET_O_STRCPY(buf, outmsg_translate (msgtype));
    if (str) {
        GTET_O_STRCAT(buf, " ");
        GTET_O_STRCAT(buf, str);
    }
    switch (msgtype)
    {
      case OUT_DISCONNECT : client_disconnect (); break;
      case OUT_CONNECTED : client_connected (); break;
      default : client_sendmsg (buf);
    }
}

void client_inmessage (char *str)
{
    enum inmsg_type msgtype;
    gchar **tokens, *final;

    /* split the message */
    tokens = g_strsplit (str, " ", 256);
    msgtype = inmsg_translate (tokens[0]);

    /* process it */
    final = g_strjoinv (" ", &tokens[1]);
    tetrinet_inmessage (msgtype, final);
    g_strfreev (tokens);
    g_free (final);
}

/* these functions set up the connection */

/* GThread handle for the in-flight resolve/connect, joined once
   `resolved` is set. Previously a local in client_process() -- now needs
   to outlive that function since it returns immediately instead of
   blocking until the thread finishes. */
static GThread *resolve_thread;

void client_process (void)
{
  errno = 0;
  resolved = 0;
  connect_pending = 1;

  resolve_thread = g_thread_new ("resolve", (GThreadFunc) client_resolv_hostname, NULL);

  /* Previously this function blocked here, spinning gtk_main_iteration()
     until `resolved` was set by the background thread. There is no GTK
     main loop in the SDL port to spin -- client_poll_connect() (called
     once per frame by the app's main loop) picks up where this left off:
     it checks `resolved` each frame and calls client_finish_connecting()
     once the background thread is done, exactly the same work this
     function used to do synchronously below this point. */
}

/* Called once per frame by the app's main loop while a connection
   attempt is in flight (i.e. after client_init(), until this stops
   being needed). No-op once nothing is pending, so it's always safe to
   call unconditionally from the main loop. */
void client_poll_connect (void)
{
  if (!connect_pending)
    return;
  if (resolved == 0)
    return; /* still waiting on the background thread */

  g_thread_join (resolve_thread);
  connect_pending = 0;

  if (resolved == -1) {
    char errmsg[1024];

    GTET_O_STRCPY(errmsg, "noconnecting ");

    if (errno)        GTET_O_STRCAT(errmsg, strerror (errno));
    else if (h_errno) GTET_O_STRCAT(errmsg, _("Couldn't resolve hostname."));

    client_inmessage (errmsg);

    return;
  }

  client_finish_connecting ();
}

/* The part of the old client_process() that ran after the hostname was
   resolved and the socket connected: send the handshake. Split out into
   its own function so client_poll_connect() can call it once resolved,
   without duplicating the encoding logic. */
static void client_finish_connecting (void)
{
  GString *s1 = g_string_sized_new(80);
  GString *s2 = g_string_sized_new(80);
  unsigned char ip[4];
  GString *iphashbuf = g_string_sized_new(11);
  unsigned int i, len;
  int l;

  /* The socket is a plain BSD socket (still blocking-mode, as
     client_resolv_hostname() left it) -- client_poll_socket() (called
     once per frame) polls it for readability with a zero-timeout
     select() rather than relying on a GIOChannel watch tied to a GLib
     main loop. */

  /* construct message */
  if (gamemode == TETRIFAST)
    g_string_printf (s1, "tetrifaster %s 1.13", nick);
  else
    g_string_printf (s1, "tetrisstart %s 1.13", nick);

  /* do that encoding thingy */
  server_ip (ip);
  g_string_printf (iphashbuf, "%d",
                   ip[0]*54 + ip[1]*41 + ip[2]*29 + ip[3]*17);
  l = iphashbuf->len;

  g_string_append_c(s2, 0);
  for (i = 0; s1->str[i]; i ++)
    g_string_append_c(s2, ((((s2->str[i] & 0xFF) +
                     (s1->str[i] & 0xFF)) % 255) ^
                      iphashbuf->str[i % l]));
  g_assert(s1->len == i);
  g_assert(s2->len == (i + 1));
  len = i + 1;

  g_string_truncate(s1, 0);
  for (i = 0; i < len; i ++)
    g_string_append_printf(s1, "%02X", s2->str[i] & 0xFF);

  /* now send to server */
  client_sendmsg (s1->str);

  g_string_free(s1, TRUE);
  g_string_free(s2, TRUE);
  g_string_free(iphashbuf, TRUE);
}


gpointer client_resolv_hostname (void)
{
    char hbuf[NI_MAXHOST];
    struct addrinfo hints, *res, *res0;
    char service[10];

    /* set up the connection */
    snprintf(service, 9, "%d", spectating?SPECPORT:PORT);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(server, service, &hints, &res0)) {
        /* set errno = 0 so that we know it's a getaddrinfo error */
        errno = 0;
        resolved = -1;
        g_thread_exit (GINT_TO_POINTER (-1));
    }
    for (res = res0; res; res = res->ai_next) {
        sock = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock < 0) {
            if (res->ai_next)
                continue;
            else {
                freeaddrinfo(res0);
                resolved = -1;
                g_thread_exit (GINT_TO_POINTER (-1));
            }
        }
        getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof(hbuf), NULL, 0, 0);
        if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
            if (res->ai_next) {
                close(sock);
                continue;
            } else {
                close(sock);
                freeaddrinfo(res0);
                resolved = -1;
                g_thread_exit (GINT_TO_POINTER (-1));
            }
        }
        break;
    }
    freeaddrinfo(res0);

    resolved = 1;
    return (GINT_TO_POINTER (1));
}

void client_connected (void)
{
    connected = 1;
    client_inmessage ("connect");
}

void client_disconnect (void)
{
    if (connected)
    {
      /* app_mainloop_running stands in for GTK's gtk_main_level() check:
         both exist for the same reason -- don't synthesize a
         "disconnect" inmessage if the rest of the app (UI, state
         machine) isn't actually up and running yet to receive it (e.g.
         during early startup/teardown paths outside the normal frame
         loop). */
      if (app_mainloop_running)
        client_inmessage ("disconnect");
      sched_remove_all ();
      shutdown (sock, 2);
      close (sock);
      connected = 0;
      connect_pending = 0;

      // Allow for sending the blocktrix init on reconnect.
      pnumrec = 0;
    }
}


/* some other useful functions */

/* Called once per frame by the app's main loop while connected. Replaces
   the old GIOChannel + g_io_add_watch(G_IO_IN, io_channel_cb) setup,
   which relied on a live GLib main loop to invoke io_channel_cb()
   whenever the socket had data ready. Here the app's own frame loop is
   the "watch": a zero-timeout select() checks readability, and if ready,
   this does exactly what io_channel_cb() used to do. Safe to call
   unconditionally every frame -- it's a no-op while not connected. */
void client_poll_socket (void)
{
  fd_set readfds;
  struct timeval tv;
  gchar *buf;

  if (!connected)
    return;

  FD_ZERO (&readfds);
  FD_SET (sock, &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  if (select (sock + 1, &readfds, NULL, NULL, &tv) <= 0)
    return;
  if (!FD_ISSET (sock, &readfds))
    return;

  if (client_readmsg (&buf) < 0)
  {
    g_warning ("client_readmsg failed, aborting connection\n");
    client_disconnect ();
  }
  else
  {
    if (strlen (buf)) client_inmessage (buf);

    if (strncmp ("noconnecting", buf, 12) == 0)
    {
      connected = 1; /* so we can disconnect :) */
      client_disconnect ();
    }
    g_free (buf);
  }
}

int client_sendmsg (char *str)
{
    gchar *buf;
    size_t len;
    ssize_t sent;

    len = strlen (str) + 1;
    buf = g_strdup (str);
    buf[len - 1] = (gchar) 0xFF;

    /* Loop until the whole message is sent: unlike g_io_channel_write_chars
       (which buffered/looped internally), a raw send() on a blocking
       socket can still return a short count if interrupted by a signal
       (EINTR) or, in principle, a partial kernel-buffer write. */
    sent = 0;
    while ((size_t) sent < len) {
        ssize_t n = send (sock, buf + sent, len - (size_t) sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            g_warning ("client_sendmsg: send() failed: %s", strerror (errno));
            break;
        }
        sent += n;
    }
    g_free (buf);

#ifdef DEBUG
    printf ("> %s\n", str);
#endif

    return 0;
}

int client_readmsg (gchar **str)
{
    gchar buf[1024];
    gint i = 0;

    for (;;)
    {
      ssize_t n = recv (sock, &buf[i], 1, 0);

      if (n < 0 && errno == EINTR)
        continue; /* retry the same byte, i unchanged -- not a do/while,
                     so this correctly re-issues recv() rather than
                     jumping to a loop condition on stale data */
      if (n == 0)
      {
        g_warning ("End of file (server closed connection).");
        return -1;
      }
      if (n < 0)
      {
        g_warning ("client_readmsg: recv() failed: %s", strerror (errno));
        return -1;
      }

      i++;
      if (buf[i-1] == (gchar)0xFF || i >= 1024)
        break;
    }
    buf[i-1] = 0;

#ifdef DEBUG
    printf ("< %s\n", buf);
#endif
    
    /** Convert all incoming data to utf-8 (or fall back to locale, and then
     * iso8859-1. - vidar */
    *str = ensure_utf8 (buf); 

    return 0;
}

void server_ip (unsigned char buf[4])
{
    struct sockaddr_in6 sin;
    struct sockaddr_in *sin4;
    socklen_t len = sizeof(sin);

    getpeername (sock, (struct sockaddr *)&sin, &len);
    if (sin.sin6_family == AF_INET6) {
	memcpy (buf, ((char *) &sin.sin6_addr) + 12, 4);
    } else {
	sin4 = (struct sockaddr_in *) &sin;
	memcpy (buf, &sin4->sin_addr, 4);
   }
}

enum inmsg_type inmsg_translate (char *str)
{
    int i;
    for (i = 0; inmsgtable[i].str; i++) {
        if (strcmp (inmsgtable[i].str, str) == 0)
            return inmsgtable[i].num;
    }
    return 0;
}

char *outmsg_translate (enum outmsg_type num)
{
    int i;
    for (i = 0; outmsgtable[i].num; i++) {
        if (outmsgtable[i].num==num) return outmsgtable[i].str;
    }
    return NULL;
}
