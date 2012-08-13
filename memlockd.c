/**
 * Copyright (c) 2010,
 *     tonglulin@gmail.com All rights reserved.
 *
 * Use, modification and distribution are subject to the "New BSD License"
 * as listed at <url: http://www.opensource.org/licenses/bsd-license.php >.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/uio.h>

#include "event.h"

#include "list.h"
#include "daemon.h"
#include "socket.h"
#include "item.h"
#include "common.h"

#define PACKAGE "memlockd"

#define SERVER_PORT 9970

/* defaults */
static void settings_init(void);
static void signals_init(void);

/* event handling, network IO */

/** exported globals **/
struct stats stats;
struct settings_t settings;

volatile sig_atomic_t daemon_quit = 0;

/** file scope variables **/
struct list_head listen_conn;
struct event_base *main_base = NULL;

static void usage(void)
{
    printf(PACKAGE " " LOCKD_VERSION "\n");
    printf("-p <num>      TCP port number to listen on (default: %d)\n"
           "-s <file>     unix socket path to listen on (disables network support)\n"
           "-a <mask>     access mask for unix socket, in octal (default 0700)\n"
           "-l <ip_addr>  interface to listen on, default is INDRR_ANY\n"
           "-d            run as a daemon\n"
           "-u <username> assume identity of <username> (only when run as root)\n"
           "-c <num>      max simultaneous connections, default is 1024\n"
           "-v            verbose (print errors/warnings while in event loop)\n"
           "-vv           very verbose (also print client commands/reponses)\n"
           "-h            print this help and exit\n"
           "-i            print license info\n"
           "-P <file>     save PID in <file>, only used with -d option\n",
          SERVER_PORT);
#ifdef USE_THREADS
    printf("-t <num>      number of threads to use, default 4\n");
#endif

    return;
}

static void usage_license(void)
{
    printf(PACKAGE " " LOCKD_VERSION "\n");
    printf("Copyright (c) 2012,\n"
           "    tonglulin@gmail.com All rights reserved.\n"
           "\n"
           "Use, modification and distribution are subject to the \"New BSD License\"\n"
           "as listed at <url: http://www.opensource.org/licenses/bsd-license.php >.\n"
          );

    return;
}

int main(int argc, char *argv[])
{
    int c = 0;
    int maxcore = 0;
    bool do_daemonize = false;

    char *pid_file = "/tmp/memlockd.pid";
    char *username = NULL;

    struct passwd *pw = NULL;
    struct rlimit rlim;

    /* register signal callback */
    signals_init();

    /* init settings */
    settings_init();

    /* set stderr non-buffering (for running under, say, daemontools) */
    setbuf(stderr, NULL);

    /* process arguments */
    while ((c = getopt(argc, argv, "a:U:p:s:c:hivl:dru:P:t")) != -1) {
        switch (c) {
            case 'a':
                /* access for unix domain socket, as octal mask (like chmod)*/
                settings.access = strtol(optarg, NULL, 8);
                break;
            case 'p':
                settings.port = atoi(optarg);
                break;
            case 's':
                settings.socketpath = optarg;
                break;
            case 'c':
                settings.maxconns = atoi(optarg);
                break;
            case 'h':
                usage();
                exit(EXIT_SUCCESS);
            case 'i':
                usage_license();
                exit(EXIT_SUCCESS);
            case 'v':
                settings.verbose++;
                break;
            case 'l':
                settings.inter = strdup(optarg);
                break;
            case 'd':
                do_daemonize = true;
                break;
            case 'r':
                maxcore = 1;
                break;
            case 'u':
                username = optarg;
                break;
            case 'P':
                pid_file = optarg;
                break;
#ifdef USE_THREADS
            case 't':
                settings.num_threads = atoi(optarg);
                if (0 == settings.num_threads) {
                    fprintf(stderr, "Number of threads must be greater than 0\n");
                    exit(EXIT_FAILURE);
                }
                break;
#endif
            default:
                fprintf(stderr, "Illegal argument \"%c\"\n", c);
                exit(EXIT_FAILURE);
        }
    }

    /*
     * If needed, increase rlimits to allow as many connections
     * as needed.
     */
    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        fprintf(stderr, "failed to getrlimit number of files\n");
        exit(EXIT_FAILURE);
    }
    else {
        int maxfiles = settings.maxconns;
        if (rlim.rlim_cur < maxfiles) {
            rlim.rlim_cur = maxfiles + 3;
        }
        if (rlim.rlim_max < rlim.rlim_cur) {
            rlim.rlim_max = rlim.rlim_cur;
        }
        if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
            fprintf(stderr, "failed to set rlimit for open files. Try "
                    "running as root or requesting smaller maxconns value.\n");
            exit(EXIT_FAILURE);
        }
    }

    /* do_daemonize if requested */
    /* if we want to ensure our ability to dump core, don't chdir to / */
    if (do_daemonize) {
        int res = 0;
        res = daemon_init();
        if (res != 0) {
            fprintf(stderr, "failed to daemon() in order to do_daemonize\n");
            return -1;
        }
    }

    /* lose root privileges if we have them */
    if (0 == getuid() || 0 == geteuid()) {
        if (NULL == username || '\0' == *username) {
            fprintf(stderr, "can't run as root without the -u switch\n");
            return -1;
        }

        if ((pw = getpwnam(username)) == NULL) {
            fprintf(stderr, "can't find the user %s to switch to\n", username);
            return 1;
        }

        if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
            fprintf(stderr, "failed to assume identity of user %s\n", username);
            return 1;
        }
    }

    /* initialize main thread libevent instance */
    main_base = event_init();

    /* initialize other stuff */
    hashlist_init();
    conn_init();

    stats.started = time(NULL);

    /* start up worker threads if MT mode */
    //thread_init(settings.num_threads, main_base);

    if (do_daemonize) {
        if (daemon_already_running(pid_file) < 0) {
            fprintf(stderr, "server is already running.\n");
            exit(EXIT_FAILURE);
        }
    }

    INIT_LIST_HEAD(&listen_conn);

    /* create unix mode sockets after dropping privileges */
    if (settings.socketpath != NULL) {
        if (socket_unix_init(settings.socketpath, settings.access)) {
            fprintf(stderr, "failed to listen\n");
            exit(EXIT_FAILURE);
        }
    }

    /* create the listening socket, bind it, and init */
    if (settings.socketpath == NULL) {
        if (socket_init(settings.port) < 0) {
            fprintf(stderr, "failed to listen\n");
            exit(EXIT_FAILURE);
        }
    }

    /* start checkpoint and deadlock detect thread */

    /* enter the event loop */
    event_base_loop(main_base, 0);
    
    hashlist_close();

    if (do_daemonize) {
        unlink(pid_file);
    }

    return 0;
}

static void settings_init(void)
{
    settings.maxconns = 1024;  /* to limit connections-related memory to about 5MB */
    settings.port = SERVER_PORT;
    settings.verbose = 0;
#ifdef USE_THREADS
    settings.num_threads = 4;
#else
    settings.num_threads = 1;
#endif
    settings.access = 0700;
    settings.inter = NULL;  /* By default this string should be NULL for getaddrinfo() */
    settings.socketpath = NULL;  /* by default, not using a unix socket */
}

static void signal_handler(int sgi)
{
    int ret = 0;

    if (daemon_quit == 1) {
        return;
    }

    daemon_quit = 1;

    /* exit event loop first */
    fprintf(stderr, "exit event base...");
    ret = event_base_loopexit(main_base, 0);
    if (ret == 0) {
        fprintf(stderr, "done.\n");
    }
    else {
        fprintf(stderr, "error\n");
    }

    /* make sure deadlock detect loop is quit*/
    sleep(2);
}

static void signals_init(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGABRT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);

    return;
}

