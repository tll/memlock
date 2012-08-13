/**
 * Copyright (c) 2012,
 *     tonglulin@gmail.com All rights reserved.
 *
 * Use, modification and distribution are subject to the "New BSD License"
 * as listed at <url: http://www.opensource.org/licenses/bsd-license.php >.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "common.h"

extern struct settings_t settings;

struct list_head connslist;

void conn_init(void)
{
    INIT_LIST_HEAD(&connslist);
}

void conn_free(struct conn *c)
{
    if (c == NULL) {
        return;
    }

    if (c->rbuf != NULL) {
        free(c->rbuf);
    }

    if (c->wbuf != NULL) {
        free(c->wbuf);
    }

    free(c);

    return;
}

struct conn *conn_new(const int sfd, const int init_state, 
        const int event_flags, const int read_buffer_size, 
        struct event_base *base)
{
    struct conn *c = NULL;

    c = (struct conn *)calloc(1, sizeof(struct conn));
    if (c == NULL) {
        fprintf(stderr, "calloc(): struct conn fatal error\n");
        return NULL;
    }

    c->rsize = read_buffer_size;
    c->wsize = DATA_BUFFER_SIZE;

    c->rbuf = c->wbuf = NULL;
    c->rbuf = (char *)malloc(c->rsize);
    c->wbuf = (char *)malloc(c->wsize);

    if (c->rbuf == NULL || c->wbuf == NULL) {
        conn_free(c);
        fprintf(stderr, "malloc(): struct conn rbuf/wbuf fatal error\n");
        return NULL;
    }

    if (settings.verbose > 1) {
        if (conn_listening == init_state) {
            fprintf(stderr, "<<<. %d server listening\n", sfd);
        }
        else {
            fprintf(stderr, "<<<. %d new client connection\n", sfd);
        }
    }

    c->sfd = sfd;
    c->state = init_state;
    c->flags = sess_init;
    c->lock_key[0] = '\0';

    event_set(&c->event, sfd, event_flags, event_handler, (void *)c);
    event_base_set(base, &c->event);
    c->ev_flags = event_flags;

    if (event_add(&c->event, NULL) == -1) {
        conn_free(c);
        fprintf(stderr, "event_add(): fatal error\n");
        return NULL;
    }

    return c;
}

void conn_add_to_connslist(struct conn *c)
{
    list_add(&c->cnode, &connslist);

    return;
}

bool conn_del_from_connslist(struct conn *c)
{
    struct conn *nc = NULL;
    struct list_head *node = NULL;
    struct list_head *n    = NULL;

    if (list_empty(&connslist)) {
        return false;
    }

    list_for_each_safe (node, n, &connslist) {
        nc = list_entry(node, struct conn, cnode);
        if (nc->sfd == c->sfd) {
            list_del(node);
            break;
        }
    }

    return true;
}

struct conn *conn_find_blocklist(struct conn *c)
{
    struct conn *nc = NULL;
    struct list_head *node = NULL;

    if (list_empty(&connslist)) {
        return NULL;
    }

    list_for_each (node, &connslist) {
        nc = list_entry(node, struct conn, cnode);
        if (c->sfd != nc->sfd
                && nc->flags == sess_block
                && strncasecmp(c->lock_key, nc->lock_key, strlen(c->lock_key)) == 0) {
            return nc;
        }
    }

    return NULL;
}

/*
 * Sets a connection's current state in the state machine. Any special
 * processing that needs to happen on certain state transitions can
 * happen here.
 */
void conn_set_state(struct conn *c, int state)
{
    assert(c != NULL);

    if (state != c->state) {
        c->state = state;
    }

    return;
}

void conn_close(struct conn *c)
{
    assert(c != NULL);

    /* delete the event, the socket and the conn */
    event_del(&c->event);

    if (settings.verbose > 0) {
        fprintf(stderr, ">>>. %d connection closed.\n", c->sfd);
    }

    close(c->sfd);

    if (c->flags == sess_lock && c->lock_key[0] != '\0') {
        hashlist_setunlock(c->lock_key);
    }
    
    stats.curr_conns--;

    conn_del_from_connslist(c);

    notify_block_conns(c);

    if (settings.verbose > 0) {
        fprintf(stderr, ">>>. %d notify other %llu client.\n", c->sfd, stats.curr_conns);
    }

    if (settings.verbose > 1) {
        fprintf(stderr, ">>>. %d closed, hashlist count:[%d]\n", c->sfd, hashtable_count(g_hashlist));
    }

    conn_free(c);

    return;
}

