/**
 * Copyright (c) 2012,
 *     tonglulin@gmail.com All rights reserved.
 *
 * Use, modification and distribution are subject to the "New BSD License"
 * as listed at <url: http://www.opensource.org/licenses/bsd-license.php >.
 */

#ifndef _CONN_H_
#define _CONN_H_

#include "event.h"
#include "list.h"

enum conn_states {
    conn_listening,  /* the socket which listens for connections */
    conn_read,       /* reading in a command line */
    conn_write,      /* writing out a simple response */
    conn_wait,       /* wait block for connection */
    conn_closing,    /* closing this connection */
};

enum conn_session {
    sess_init,   /* connection init */
    sess_block,  /* connection block, waiting notify */
    sess_lock,   /* connection locked */
};

struct conn {
    struct list_head cnode;
    int    sfd;
    int    state;  /* connection event state */
    int    flags;  /* connection session state */
    struct event event;
    short  ev_flags;
    short  which;  /* which events were just triggered */

    char   *rbuf;  /* buffer to read commands into */
    char   *rcurr; /* but if we parsed some already, this is where we stopped */
    int    rsize;  /* total allocated size of rbuf */
    int    rbytes; /* how much data, starting from rcurr, do we have unparsed */

    char   *wbuf;  /* buffer to write commands resq */
    char   *wcurr; /* */
    int    wsize;  /* total allocated size of wbuf */
    int    wbytes; /* how much data, starting from wcurr */

    unsigned int cip;  /* client ip */
    int    lock_cmd;   /* client cmd */
    char   lock_key[64]; /* client cmd key */
};

extern struct list_head connslist;

void conn_init(void);

void conn_close(struct conn *c);

void conn_add_to_connslist(struct conn *c);

void conn_set_state(struct conn *c, int state);

struct conn *conn_new(const int sfd, const int init_state,
        const int event_flags, const int read_buffer_size,
        struct event_base *base);

struct conn *conn_find_blocklist(struct conn *c);

#endif
