/**
 * Copyright (c) 2012,
 *     tonglulin@gmail.com All rights reserved.
 *
 * Use, modification and distribution are subject to the "New BSD License"
 * as listed at <url: http://www.opensource.org/licenses/bsd-license.php >.
 */

#ifndef _COMMON_H_
#define _COMMON_H_

/* Get a consistent bool type */
#if HAVE_STDBOOL_H
# include <stdbool.h>
#else
typedef enum {false = 0, true = 1} bool;
#endif

#define LOCKD_VERSION "0.0.1"

#define DATA_BUFFER_SIZE 2048

struct settings_t {
    int  maxconns;
    int  port;
    int  verbose;  /* debug model */
    int  num_threads;  /* number of libevent threads to run */
    int  access;  /* access mask (a la chmod) for unix domain socket */
    char *inter;
    char *socketpath;  /* path to unix socket if using local socket */
};

struct stats {
    time_t             started;  /* when the process was started */
    unsigned long long curr_conns;
    unsigned long long total_conns;
    unsigned long long lock_cmds;
    unsigned long long lock_hits;
    unsigned long long lock_blks;
    unsigned long long unlock_cmds;
    unsigned long long unlock_hits;
    unsigned long long items;
};

extern struct stats stats;
extern struct settings_t settings;
extern struct event_base *main_base;
extern struct list_head listen_conn;

#include "conn.h"
#include "item.h"

void event_handler(const int fd, const short which, void *arg);
void out_string(struct conn *c, const char *str);
bool update_event(struct conn *c, const int new_flags);
void notify_block_conns(struct conn *c);

#endif
