/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * See LICENSE.txt included in this distribution for the specific
 * language governing permissions and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at LICENSE.txt.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Portions Copyright 2009-2010 Matt Ingenthron
 */

/*
 * The metrics file is all about collecting metrics during a run and then
 * calculating key metrics after the run is complete.
 *
 * loosely reimplemented in C from Faban http://faban.sunsource.net
 *
 * @author Matt Ingenthron <ingenthr@cep.net>
 *
 */

#include "config.h"
#include <sys/time.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "metrics.h"
#include "memcachetest.h"

/* Stats for all transaction types - the first dimension of the array
 * is always the operation id. This is the index into the operations
 * array of the mix. The second dimension, existent only for histograms
 * is the bucket.
 */

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

const int RAMP_UP = 30;
static struct TxnResult * txn_list_first = NULL;


/**
 * Number of successful transactions during steady state.
 * This is used for final reporting and in-flight reporting of averages.
 */
int txCntStdy[6];


/**
 * Number of failed transactions during steady state.
 * This is used for final reporting and in-flight reporting of averages.
 */
int errCntStdy[6];

struct TxnRunStatus {
    int txnCount;
    int avgRespTime;
};


static void insert(struct thread_context *ctx, struct TxnResult* item) {
    if (ctx->head == NULL) {
        ctx->head = item;
    } else {
        struct TxnResult* ptr = ctx->head;
        int searching = 1;

        while (searching) {
            if (item->respTime <= ptr->respTime) {
                /* should be on the left */
                if (ptr->left == NULL) {
                    ptr->left = item;
                    searching = 0;
                } else {
                    ptr = ptr->left;
                }
            } else {
                /* should be on the right */
                if (ptr->right == NULL) {
                    ptr->right = item;
                    searching = 0;
                } else {
                    ptr = ptr->right;
                }
            }
        }
    }
}

static int walk(struct TxnResult* node, int (*walk_function)(void *, struct TxnResult* node), void *cookie) {
    int ret;
    if (node->left) {
        if ((ret = walk(node->left, walk_function, cookie)) != 0) {
            return ret;
        }
    }
    if ((ret = (*walk_function)(cookie, node) != 0)) {
        return ret;
    }

    if (node->right) {
        if ((ret = walk(node->right, walk_function, cookie)) != 0) {
            return ret;
        }
    }

    return 0;
}

#if 0
static struct TxnResult *create_txn(struct TxnResult *item) {
    static struct TxnResult *items;
    static int no_left = 0;
    struct TxnResult *ret;

    if (no_left == 0) {
        // Need to allocate more items!!
        items = calloc(1000, sizeof (struct TxnResult));
        assert(items != NULL);
        no_left = 1000;
    }
    ret = items;
    ++items;
    --no_left;

    ret->respTime = item->respTime;
    ret->left = ret->right = NULL;

    return ret;
}
#endif

struct walker_execution_time {
    long current;
    long elementnum;
    hrtime_t exec_time;
};

static int get_exec_time_walker(void *cookie, struct TxnResult* node) {
    struct walker_execution_time *tim = cookie;
    if (tim->current < tim->elementnum) {
        tim->exec_time = node->respTime;
        ++tim->current;
        return 0;
    } else {
        return 1;
    }
}

/*
 * Get the execution time for a given element number in the list
 * @param elementnum the element number you want the execution time for
 */
static hrtime_t get_exec_time(struct TxnResult* root, long elementnum) {
    struct walker_execution_time tim = { .current = 0, .exec_time = 0, .elementnum = elementnum };

    walk(root, get_exec_time_walker, &tim);
    return tim.exec_time;
}

struct walker_average {
    hrtime_t delta;
    long count;
};

static int get_average_walker(void *cookie, struct TxnResult* node) {
    struct walker_average *a = cookie;
    ++a->count;
    a->delta += node->respTime;
    return 0;
}

/*
 * Get the execution time for a given element number in the list
 * @param elementnum the element number you want the execution time for
 */
static hrtime_t calc_average(struct TxnResult* a) {
    struct walker_average avg = { .count = 0, .delta = 0 };
    walk(a, get_average_walker, &avg);
    return avg.delta / avg.count;
}

/**
 * External interface
 */
void record_tx(enum TxnType tx_type, hrtime_t time, struct thread_context *ctx) {
    struct TxnResult* new_txn = calloc(1, sizeof(struct TxnResult));
    new_txn->respTime = time;
    new_txn->left = new_txn->right = new_txn->next = NULL;
    insert(ctx, new_txn);
    txCntStdy[tx_type]++;
}

void record_error(enum TxnType tx_type, hrtime_t t) {
    (void)t;
    errCntStdy[tx_type]++;
}

struct ResultMetrics *calc_metrics(enum TxnType tx_type) {
    struct ResultMetrics * ret = calloc(1, sizeof (struct ResultMetrics));
    ret->success_count = txCntStdy[tx_type];

    long percentile90 = (0.9) * (float) (ret->success_count - 1) + 1.0;
    long percentile95 = (0.95) * (float) (ret->success_count - 1) + 1.0;
    assert(ret != NULL);

    ret->max90th_result = get_exec_time(txn_list_first, percentile90);
    ret->max95th_result = get_exec_time(txn_list_first, percentile95);
    ret->min_result = get_exec_time(txn_list_first, 0);
    ret->max_result = get_exec_time(txn_list_first, ret->success_count);
    ret->average = calc_average(txn_list_first);

    return ret;
}
