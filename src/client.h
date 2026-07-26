extern int connected;
extern char server[128];

/* inmsgs are messages coming from the server */

enum inmsg_type {
    IN_UNUSED,
    IN_CONNECT, IN_DISCONNECT, IN_CONNECTERROR,
    IN_PLAYERNUM, IN_PLAYERJOIN, IN_PLAYERLEAVE, IN_KICK,
    IN_TEAM,
    IN_PLINE, IN_PLINEACT,
    IN_PLAYERLOST, IN_PLAYERWON,
    IN_NEWGAME, IN_INGAME, IN_PAUSE, IN_ENDGAME,
    IN_F, IN_SB, IN_LVL, IN_GMSG,
    IN_WINLIST,
    IN_SPECJOIN, IN_SPECLEAVE, IN_SPECLIST, IN_SMSG, IN_SACT,
    IN_BTRIXNEWGAME
};

/* outmsgs are messages going out to the server */

enum outmsg_type {
    OUT_UNUSED, OUT_DISCONNECT, OUT_CONNECTED,
    OUT_TEAM,
    OUT_PLINE, OUT_PLINEACT,
    OUT_PLAYERLOST,
    OUT_F, OUT_SB, OUT_LVL, OUT_GMSG,
    OUT_STARTGAME, OUT_PAUSE,
    OUT_VERSION,
    OUT_CLIENTINFO
};

/* functions for connecting and disconnecting */
extern void client_init (const char *server, const char *nick);
extern void client_disconnect (void);

/* Set to 1 by the app's main loop once it actually starts running (see
   client.c for why -- stands in for GTK's gtk_main_level() check). */
extern int app_mainloop_running;

/* Call both of these once per frame from the app's main loop; both are
   safe to call unconditionally (no-ops when nothing is pending/connected).
   client_poll_connect() drives the non-blocking hostname-resolve/connect
   sequence client_init() kicks off; client_poll_socket() replaces the
   old GIOChannel watch for incoming server data once connected. */
extern void client_poll_connect (void);
extern void client_poll_socket (void);

/* for sending stuff back and forth */
extern void client_outmessage (enum outmsg_type msgtype, char *str);
extern void client_inmessage (char *str);
