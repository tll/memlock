/**
 * Copyright (c) 2012,
 *     tonglulin@gmail.com All rights reserved.
 *
 * Use, modification and distribution are subject to the "New BSD License"
 * as listed at <url: http://www.opensource.org/licenses/bsd-license.php >.
 */

#ifndef _DEAMON_H_
#define _DAEMON_H_

int daemon_init(void);

int daemon_already_running(const char *pid_file);

#endif
