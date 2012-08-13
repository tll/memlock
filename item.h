/**
 * Copyright (c) 2010,
 *     tonglulin@gmail.com All rights reserved.
 *
 * Use, modification and distribution are subject to the "New BSD License"
 * as listed at <url: http://www.opensource.org/licenses/bsd-license.php >.
 */

#ifndef _ITEM_H_
#define _ITEM_H_

#include "hashtable.h"

struct item {
    char   key[64];
    int    val;
    int    ref;
    time_t exp;
    unsigned int cip;
};

#define EM_READ     0x00
#define EM_WRITE    0x01
#define EM_NONBLOCK 0x10

extern struct hashtable *g_hashlist;

void hashlist_init(void);

void hashlist_close(void);

int hashlist_setlock(const char *key, int flags);

int hashlist_setunlock(const char *key);

struct item *hashlist_findlock(const char *key);

#endif
