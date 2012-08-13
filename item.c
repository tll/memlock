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
#include <assert.h>
#include <time.h>

#include "hash.h"
#include "hashtable.h"
#include "common.h"
#include "item.h"

struct hashtable *g_hashlist = NULL;

static struct item *item_init(const char *key, int flags)
{
    struct item *it = NULL;

    it = (struct item *)calloc(1, sizeof(struct item));
    if (it == NULL) {
        return NULL;
    }

    snprintf(it->key, sizeof(it->key), "%s", key);
    it->val = flags;
    it->ref = 1;
    it->exp = time(NULL);

    return it;
}

static unsigned int hashfromkey(void *k)
{
    return em_hash(k, strlen(k), 0);
}

static int equalkeys(void *k1, void *k2)
{
    return (strcmp(k1, k2) == 0);
}

void hashlist_init(void)
{
    g_hashlist = create_hashtable(65535, hashfromkey, equalkeys);
    if (g_hashlist == NULL) {
        fprintf(stderr, "create_hashtable(): init fatal error\n");
        exit(EXIT_FAILURE);
    }

    return;
}

void hashlist_close(void)
{
    hashtable_destroy(g_hashlist, 1);
}

/*
 * return:
 *        -1  failed
 *         0  success
 *         1  wait
 */
int hashlist_setlock(const char *key, int flags)
{
    int ret = 0;
    char *k = NULL;
    struct item *it = NULL;

    assert(key != NULL);

    if (settings.verbose > 1) {
        fprintf(stderr, ">>>. hashlist_setlock(): set lock key:[%s] flags:[%d]\n", key, flags);
    }
    
    it = (struct item *)hashtable_search(g_hashlist, (void *)key);
    if (it == NULL) {
        it = item_init(key, flags);
        if (it == NULL) {
            fprintf(stderr, "hash_init(): out of memory\n");
            return -1;
        }

        k = strdup(key);
        ret = hashtable_insert(g_hashlist, (void *)k, (void *)it);
        assert(ret != 0);
        if (settings.verbose > 1) {
            fprintf(stderr, ">>>. hashlist_setlock(): insert key:[%s]\n", key);
        }

        return 0;
    }

    if (settings.verbose > 1) {
        fprintf(stderr, ">>>. hashlist_setlock(): find key:[%s] flags:[%d]\n", key, it->val);
    }

    if (EM_NONBLOCK & flags) {
        if (EM_WRITE & flags) {
            return -1;
        }

        if (EM_WRITE & it->val) {
            return -1;
        }

        it->ref++;
        return 0;
    }

    if (EM_WRITE & flags) {
        return 1;
    }

    if (EM_WRITE & it->val) {
        return 1;
    }

    it->ref++;
    return 0;
}

int hashlist_setunlock(const char *key)
{
    struct item *it = NULL;
    struct item *itm = NULL;

    assert(key != NULL);

    if (settings.verbose > 1) {
        fprintf(stderr, ">>>. hashlist_setunlock(): set unlock key:[%s]\n", key);
    }

    it = (struct item *)hashtable_search(g_hashlist, (void *)key);
    if (it == NULL) {
        return -1;
    }

    if (settings.verbose > 1) {
        fprintf(stderr, ">>>. hashlist_setunlock(): find key:[%s]\n", key);
    }

    it->ref--;

    if (it->ref <= 0) {
        itm = hashtable_remove(g_hashlist, (void *)key);
        free(itm);
        if (settings.verbose > 1) {
            fprintf(stderr, ">>>. hashlist_setunlock(): remove key:[%s]\n", key);
        }
    }

    return 0;
}

struct item *hashlist_findlock(const char *key)
{
    struct item *it = NULL;

    assert(key != NULL);

    if (settings.verbose > 1) {
        fprintf(stderr, ">>>. hashlist_findlock(): find lock key:[%s]\n", key);
    }

    it = (struct item *)hashtable_search(g_hashlist, (void *)key);
    if (it == NULL) {
        return NULL;
    }

    return it;
}

