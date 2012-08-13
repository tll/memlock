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
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "common.h"
#include "conn.h"

#define LISTENQ 1024 

static int socket_new(struct addrinfo *ai);
static int socket_unix_new(void);

int socket_init(const int port)
{
    int sfd = -1;
    int flags = 0;
    int error = 0;
    int success = 0;

    struct linger ling = {0, 0};
    struct addrinfo *ai;
    struct addrinfo *next;
    struct addrinfo hints;

    char port_buf[NI_MAXSERV] = {0};

    /*
     * the memset call clears nonstandard fields in some impementations
     * that otherwise mess things up.
     */
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    snprintf(port_buf, sizeof(port_buf), "%d", port);

    error = getaddrinfo(settings.inter, port_buf, &hints, &ai);
    if (error != 0) {
        if (error != EAI_SYSTEM) {
            fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
        }
        else {
            fprintf(stderr, "getaddrinfo(): fatal error\n");
        }
        return -1;
    }

    for (next = ai; next != NULL; next = next->ai_next) {
        struct conn *listen_conn_add = NULL;

        if ((sfd = socket_new(next)) == -1) {
            freeaddrinfo(ai);
            return -1;
        }

        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
        setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
        setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
        setsockopt(sfd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));

        if (bind(sfd, next->ai_addr, next->ai_addrlen) == -1) {
            if (errno != EADDRINUSE) {
                fprintf(stderr, "bind(): fatal error\n");
                close(sfd);
                freeaddrinfo(ai);
                return -1;
            }
            close(sfd);
            continue;
        }
        else {
            success++;
            if (listen(sfd, LISTENQ) == -1) {
                fprintf(stderr, "listen(): fatal error\n");
                close(sfd);
                freeaddrinfo(ai);
                return -1;
            }
        }

        if ((listen_conn_add = conn_new(sfd, conn_listening, \
                        EV_READ | EV_PERSIST, 1, main_base)) == NULL) {
            fprintf(stderr, "failed to create listening connection\n");
            exit(EXIT_FAILURE);
        }

        list_add(&listen_conn_add->cnode, &listen_conn);
    }

    freeaddrinfo(ai);

    /* Return zero iff we detected no errors in starting up connections */
    return 0 == success;
}

int socket_unix_init(const char *path, int access_mask)
{
    int sfd = -1;
    int flags = 1;
    int old_umask = 0;
    struct linger ling = {0, 0};
    struct sockaddr_un addr;
    struct stat tstat;
    struct conn *listen_conn_add = NULL;

    if (path == NULL) {
        return -1;
    }

    if ((sfd = socket_unix_new()) == -1) {
        return -1;
    }

    /*
     * Clean up a previous socket file if we left it around
     */
    if (lstat(path, &tstat) == 0) {
        if (S_ISSOCK(tstat.st_mode))
            unlink(path);
    }

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
    setsockopt(sfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
    setsockopt(sfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));

    /*
     * the memset call clears nonstandard fields in some impementations
     * that otherwise mess things up.
     */
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);

    old_umask = umask(~(access_mask & 0777));

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        fprintf(stderr, "bind(): fatal error\n");
        close(sfd);
        umask(old_umask);
        return -1;
    }

    umask(old_umask);

    if (listen(sfd, LISTENQ) == -1) {
        fprintf(stderr, "listen(): fatal error\n");
        close(sfd);
        return -1;
    }

    if (!(listen_conn_add = conn_new(sfd, conn_listening,
                    EV_READ | EV_PERSIST, 1, main_base))) {
        fprintf(stderr, "failed to create listening connection\n");
        exit(EXIT_FAILURE);
    }

    list_add(&listen_conn_add->cnode, &listen_conn);

    return 0;
}

static int socket_new(struct addrinfo *ai)
{
    int sfd = 0;
    int flags = 0;

    if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
        fprintf(stderr, "socket(): fatal error\n");
        return -1;
    }

    if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
            || fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf(stderr, "setting O_NONBLOCK socket error\n");
        close(sfd);
        return -1;
    }

    return sfd;
}

static int socket_unix_new(void)
{
    int sfd = -1;
    int flags = 0;

    if ((sfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "socket(): fatal error\n");
        return -1;
    }

    if ((flags = fcntl(sfd, F_GETFL, 0)) < 0
            || fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf(stderr, "setting O_NONBLOCK");
        close(sfd);
        return -1;
    }

    return sfd;
}

