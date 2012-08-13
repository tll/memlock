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
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "event.h"
#include "common.h"

#define COMMAND_TOKEN 0
#define SUBCOMMAND_TOKEN 1
#define KEY_TOKEN 1
#define KEY_MAX_LENGTH 64
#define MAX_TOKENS 4

struct token_t {
    char *value;
    size_t length;
};

void notify_block_conns(struct conn *c)
{
    int ret = 0;
    struct conn *nc = NULL;
    
    nc = conn_find_blocklist(c);
    if (nc == NULL) {
        return;
    }

    ret = hashlist_setlock(nc->lock_key, nc->lock_cmd);
    if (ret < 0) {
        out_string(nc, "-ERR, lock failed");

        stats.lock_hits++;

        if (!update_event(nc, EV_WRITE | EV_PERSIST)) {
            if (settings.verbose > 0) {
                fprintf(stderr, "notify_block_conns(): Couldn't update event\n");
            }
            conn_set_state(nc, conn_closing);
        }
    }
    else if (ret > 0) {
        conn_set_state(nc, conn_wait);

        stats.lock_blks++;
    }
    else {
        out_string(nc, "+OK, lock success");
        nc->flags = sess_lock;

        stats.lock_cmds++;

        if (!update_event(nc, EV_WRITE | EV_PERSIST)) {
            if (settings.verbose > 0) {
                fprintf(stderr, "notify_block_conns(): Couldn't update event\n");
            }
            conn_set_state(nc, conn_closing);
        }
    }

    return;
}

static void process_lock_command(struct conn *c, struct token_t *tokens, const int ntokens)
{
    int  i = 0;
    int  val = 0;
    int  ret = 0;
    int  len = 0;
    int  nkey = 0;
    char *key = NULL;
    char *ptr = NULL;
    char flags[4] = {0};

    assert(c != NULL);

    if (tokens[KEY_TOKEN].length > KEY_MAX_LENGTH) {
        out_string(c, "-ERR, bad command line format");
        return;
    }

    key = tokens[KEY_TOKEN].value;
    nkey = tokens[KEY_TOKEN].length;

    if (c->flags == sess_block) {
        out_string(c, "-ERR, waiting for have lock");
        return;
    }
    else if (c->flags == sess_lock) {
        out_string(c, "-ERR, have locked one key");
        return;
    }
    
    snprintf(c->lock_key, sizeof(c->lock_key), "%s", key);

    snprintf(flags, sizeof(flags), "%s", tokens[2].value);

    len = strlen(flags);
    if (len > 2) {
        out_string(c, "-ERR, bad command flags parameter");
        return;
    }

    for (i = 0; i < len; i++) {
        ptr = strchr("rwnb", flags[i]);
        if (NULL == ptr) {
            out_string(c, "-ERR, illegal flags parameter");
            return;
        }
    }

    if (strchr(flags , 'w')) {
        val |= EM_WRITE;
    }

    if (strchr(flags, 'n')) {
        val |= EM_NONBLOCK;
    }

    c->lock_cmd = val;

    ret = hashlist_setlock(key, val);
    if (ret < 0) {
        out_string(c, "-ERR, lock failed");
        c->flags = sess_init;

        stats.lock_hits++;
    }
    else if (ret > 0) {
        conn_set_state(c, conn_wait);
        c->flags = sess_block;

        stats.lock_blks++;
    }
    else {
        out_string(c, "+OK, lock success");
        c->flags = sess_lock;

        stats.lock_cmds++;
    }

    return;
}

static void process_unlock_command(struct conn *c, struct token_t *tokens, const int ntokens)
{
    assert(c != NULL);

    if (c->flags != sess_lock || c->lock_key[0] == '\0') {
        out_string(c, "-ERR, sequence error");
        return;
    }

    if (c->lock_key[0] != '\0') {
        hashlist_setunlock(c->lock_key);
        c->lock_key[0] = '\0';
    }

    c->flags = sess_init;

    out_string(c, "+OK, unlock success");

    stats.unlock_cmds++;

    notify_block_conns(c);

    return;
}

static void process_stats_command(struct conn *c, struct token_t *tokens, const int ntokens)
{
    char buf[1024] = {0};

    assert(c != NULL);

    snprintf(buf, sizeof(buf), \
            "+OK, server stats:\r\nserver started: %ld\r\n"
            "current conns: %llu\r\ntotal conns: %llu\r\n"
            "locked cmds: %llu\r\nlocked hits: %llu\r\n"
            "locked blks: %llu\r\nunlock cmds: %llu", \
            stats.started, stats.curr_conns, stats.total_conns, \
            stats.lock_cmds, stats.lock_hits, stats.lock_blks,
            stats.unlock_cmds);

    out_string(c, buf);

    return;
}

static void process_help_command(struct conn *c, struct token_t *tokens, const int ntokens)
{
    char buf[1024] = {0};

    assert(c != NULL);

    snprintf(buf, sizeof(buf), \
            "+OK, lock server command usage (V%s):\r\n"
            "lock key_string {n | w/r}\r\nunlock\r\n"
            "quit\r\nfind key_string\r\nstats\r\nhelp", LOCKD_VERSION);

    out_string(c, buf);

    return;
}

static void process_find_command(struct conn *c, struct token_t *tokens, const int ntokens)
{
    struct item *it = NULL;

    int  nkey = 0;
    char *key = NULL;
    char buf[512] = {0};

    assert(c != NULL);

    key = tokens[KEY_TOKEN].value;
    nkey = tokens[KEY_TOKEN].length;

    it = hashlist_findlock(key);
    if (it == NULL) {
        out_string(c, "+OK, the key is not exist");
        return;
    }

    snprintf(buf, sizeof(buf), "+OK, the key %d locked ref %d at %ld", \
            it->val, it->ref, it->exp);

    out_string(c, buf);

    return;
}

static int tokenize_command(char *command, struct token_t *tokens, 
        const int max_tokens)
{
    int  ntokens = 0;
    char *s = NULL, *e = NULL;

    assert(command != NULL && tokens != NULL && max_tokens > 1);

    for (s = e = command; ntokens < max_tokens - 1; ++e) {
        if (*e == ' ') {
            if (s != e) {
                tokens[ntokens].value = s;
                tokens[ntokens].length = e - s;
                ntokens++;
                *e = '\0';
            }
            s = e + 1;
        }
        else if (*e == '\0') {
            if (s != e) {
                tokens[ntokens].value = s;
                tokens[ntokens].length = e - s;
                ntokens++;
            }

            break; /* string end */
        }
    }

    /*
     * If we scanned the whole string, the terminal value pointer is null,
     * otherwise it is the first unprocessed character.
     */
    tokens[ntokens].value = *e == '\0' ? NULL : e;
    tokens[ntokens].length = 0;
    ntokens++;

    return ntokens;
}

static void process_command(struct conn *c, char *command)
{
    struct token_t tokens[MAX_TOKENS];
    int    ntokens = 0;
    
    assert(c != NULL);

    if (settings.verbose > 0) {
        fprintf(stderr, "<<<. %d input cmd:[%s]\n", c->sfd, command);
    }

    ntokens = tokenize_command(command, tokens, MAX_TOKENS);
    if (ntokens >= 4
            && (strcmp(tokens[COMMAND_TOKEN].value, "lock") == 0)) {
        process_lock_command(c, tokens, ntokens);
    }
    else if (ntokens == 2
            && (strcmp(tokens[COMMAND_TOKEN].value, "unlock") == 0)) {
        process_unlock_command(c, tokens, ntokens);
    }
    else if (ntokens == 2
            && (strcmp(tokens[COMMAND_TOKEN].value, "quit") == 0)) {
        conn_set_state(c, conn_closing);
    }
    else if (ntokens == 2
            && (strcmp(tokens[COMMAND_TOKEN].value, "stats") == 0)) {
        process_stats_command(c, tokens, ntokens);
    }
    else if (ntokens == 2
            && (strcmp(tokens[COMMAND_TOKEN].value, "help") == 0)) {
        process_help_command(c, tokens, ntokens);
    }
    else if (ntokens == 3
            && (strcmp(tokens[COMMAND_TOKEN].value, "find") == 0)) {
        process_find_command(c, tokens, ntokens);
    }
    else {
        out_string(c, "-ERR, unimplemented");
    }

    return;
}

/*
 * if we have a complete line in the buffer, process it.
 */
static int try_read_command(struct conn *c)
{
    char *el, *cont;

    assert(c != NULL);
    assert(c->rcurr <= (c->rbuf + c->rsize));

    if (c->rbytes == 0) {
        return 0;
    }

    el = memchr(c->rcurr, '\n', c->rbytes);
    if (!el) {
        return 0;
    }

    cont = el + 1;
    if ((el - c->rcurr) > 1 && *(el - 1) == '\r') {
        el--;
    }
    *el = '\0';

    assert(cont <= (c->rcurr + c->rbytes));

    process_command(c, c->rcurr);

    c->rbytes -= (cont - c->rcurr);
    c->rcurr = cont;

    assert(c->rcurr <= (c->rbuf + c->rsize));

    return 1;
}

/*
 * read from network as much as we can, handle buffer overflow and connection
 * close.
 * before reading, move the remaining incomplete fragment of a command
 * (if any) to the beginning of the buffer.
 * return 0 if there's nothing to read on the first read.
 */
static int try_read_network(struct conn *c)
{
    int res = 0;
    int gotdata = 0;

    assert(c != NULL);

    if (c->rcurr != c->rbuf) {
        if (c->rbytes != 0) /* otherwise there's nothing to copy */
            memmove(c->rbuf, c->rcurr, c->rbytes);
        c->rcurr = c->rbuf;
    }

    while (1) {
        if (c->rbytes >= c->rsize) {
            char *new_rbuf = realloc(c->rbuf, c->rsize * 2);
            if (!new_rbuf) {
                if (settings.verbose > 0) {
                    fprintf(stderr, "Couldn't realloc input buffer\n");
                }

                c->rbytes = 0; /* ignore what we read */
                out_string(c, "-ERR, out of memory reading request");
                return 1;
            }
            c->rcurr = c->rbuf = new_rbuf;
            c->rsize *= 2;
        }

        int avail = c->rsize - c->rbytes;
        res = read(c->sfd, c->rbuf + c->rbytes, avail);
        if (res > 0) {
            gotdata = 1;
            c->rbytes += res;
            if (res == avail) {
                continue;
            } else {
                break;
            }
        }
        if (res == 0) {
            /* connection closed */
            conn_set_state(c, conn_closing);
            return 1;
        }
        if (res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            /* Should close on unhandled errors. */
            conn_set_state(c, conn_closing);
            return 1;
        }
    }

    return gotdata;
}

static int try_write_network(struct conn *c)
{
    int n = 0;

    assert(c != NULL);

    n = write(c->sfd, c->wcurr, c->wbytes);
    if (n == -1) {
        return -1;
    }
    else if (n == 0) {
        return 0;
    }

    /* */
    if (n >= c->wbytes) {
        c->wbytes = 0;
        c->wcurr = c->wbuf;
        goto done;
    }

    c->wcurr += n;
    c->wbytes -= n;

done:
    return n;
}

static void drive_machine(struct conn *c)
{
    int  ret = 0;
    int  sfd = -1;
    int  flags = 1;
    bool stop = false;
    struct conn *nc = NULL;
    struct sockaddr_storage addr;
    socklen_t addrlen;

    assert(c != NULL);

    while (!stop) {
        switch (c->state) {
            case conn_listening:
                addrlen = sizeof(addr);
                if ((sfd = accept(c->sfd, (struct sockaddr *)&addr, &addrlen)) == -1) {
                    /* these are transient, so don't log anything */
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        stop = true;
                    }
                    else if (errno == EMFILE) {
                        if (settings.verbose > 0) {
                            fprintf(stderr, "Too many open connections\n");
                            stop = true;
                        }
                    }
                    else {
                        fprintf(stderr, "accept(): fatal error\n");
                        stop = true;
                    }
                    break;
                }

                if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
                        || fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    fprintf(stderr, "fcntl(): setting O_NONBLOCK\n");
                    close(sfd);
                    break;
                }

                nc = conn_new(sfd, conn_read, EV_READ | EV_PERSIST,
                        DATA_BUFFER_SIZE, main_base);
                if (NULL == nc) {
                    fprintf(stderr, "conn_new(): fatal error\n");
                    close(sfd);
                    break;
                }

                stats.curr_conns++;
                stats.total_conns++;

                conn_add_to_connslist(nc);

                stop = true;
                break;
                
            case conn_read:
                if (0 != try_read_command(c)) {
                    continue;
                }

                if (0 != try_read_network(c)) {
                    continue;
                }

                /* we have no command line and no data to read from network */
                if (!update_event(c, EV_READ | EV_PERSIST)) {
                    if (settings.verbose > 0) {
                        fprintf(stderr, "Couldn't update event\n");
                    }
                    conn_set_state(c, conn_closing);
                    break;
                }

                stop = true;
                break;

            case conn_write:
                ret = try_write_network(c);
                if (ret < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        if (!update_event(c, EV_WRITE | EV_PERSIST)) {
                            if (settings.verbose > 0) {
                                fprintf(stderr, "Couldn't update event\n");
                            }
                            conn_set_state(c, conn_closing);
                            break;
                        }
                        stop = true;
                        break;
                    }
                    else {
                        conn_set_state(c, conn_closing);
                        break;
                    }
                }
                else if (ret == 0) {
                    if (c->state == conn_write) {
                        conn_set_state(c, conn_read);
                    }
                    else {
                        conn_set_state(c, conn_closing);
                    }
                }
                else {
                    conn_set_state(c, conn_write);
                }
                break;

            case conn_wait:
                if (!update_event(c, EV_READ | EV_PERSIST)) {
                    if (settings.verbose > 0) {
                        fprintf(stderr, "Couldn't update event\n");
                    }
                    conn_set_state(c, conn_closing);
                    break;
                }

                conn_set_state(c, conn_read);
                stop = true;
                break;

            case conn_closing:
                conn_close(c);
                stop = true;
                break;
        }
    }

    return;
}

void event_handler(const int fd, const short which, void *arg)
{
    struct conn *c = NULL;

    c = (struct conn *)arg;
    assert(c != NULL);

    c->which = which;

    /* sanity */
    if (fd != c->sfd) {
        if (settings.verbose > 0) {
            fprintf(stderr, "Catastrophic: event fd doesn't match conn fd!\n");
        }
        conn_close(c);
        return;
    }

    drive_machine(c);

    /* wait for next event */
    return;
}

void out_string(struct conn *c, const char *str)
{
    size_t len;

    assert(c != NULL);

    if (settings.verbose > 0) {
        fprintf(stderr, ">>>. %d output cmd:[%s]\n", c->sfd, str);
    }

    len = strlen(str);
    if ((len + 2) > c->wsize) {
        /* ought to be always enough. just fail for simplicity */
        str = "-ERR, server output line too long";
        len = strlen(str);
    }

    memcpy(c->wbuf, str, len);
    memcpy(c->wbuf + len, "\r\n", 2);
    c->wbytes = len + 2;
    c->wcurr = c->wbuf;

    conn_set_state(c, conn_write);

    return;
}

bool update_event(struct conn *c, const int new_flags)
{
    assert(c != NULL);

    struct event_base *base = c->event.ev_base;

    if (c->ev_flags == new_flags) {
        return true;
    }

    if (event_del(&c->event) == -1) {
        return false;
    }

    event_set(&c->event, c->sfd, new_flags, event_handler, (void *)c);
    event_base_set(base, &c->event);
    c->ev_flags = new_flags;

    if (event_add(&c->event, 0) == -1) {
        return false;
    }

    return true;
}

