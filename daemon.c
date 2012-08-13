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
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#define write_lock(fd, offset, whence, len) \
    lock_reg(fd, F_SETLK, F_WRLCK, offset, whence, len)

int lock_reg(int fd, int cmd, int type, off_t offset, int whence, off_t len)
{
    struct flock lock;
    lock.l_type = type;
    lock.l_start = offset;
    lock.l_whence = whence;
    lock.l_len = len;
    return (fcntl(fd, cmd, &lock));
}

int daemon_init(void)
{
    int i, fd0, fd1, fd2;
    pid_t pid;
    struct sigaction sa;

    umask(0);

    if (0 > (pid = fork())) {
        return -1;
    }
    else if (0 != pid) {
        exit(0);
    }

    setsid();

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        exit(0);
    }

    if (0 > (pid = fork())) {
        exit(0);
    }
    else if (0 != pid) {
        exit(0);
    }

    if (chdir("/") < 0) {
        exit(0);
    }

    for (i = 0; i < 3; i++) {
        close(i);
    }

    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(0);
    fd2 = dup(0);

    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        exit(1);
    }

    return 0;
}

int daemon_already_running(const char *pid_file)
{
    pid_t pid;

    int   fd  = 0;
    int   val = 0;
    char  buf[32] = {0};

    pid = getpid();
    snprintf(buf, sizeof(buf), "%d", (int)pid & 0xFFFF);

    fd = open(pid_file, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        return -1;
    }

    if (write_lock(fd, 0, SEEK_SET, 0) < 0) {
        return -2;
    }

    if (ftruncate(fd, 0) == -1) {
        return -3;
    }

    write(fd, buf, strlen(buf));

    if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
        return -4;
    }

    val |= FD_CLOEXEC;

    if (fcntl(fd, F_SETFD, val) < 0) {
        return -5;
    }

    return 0;
}

