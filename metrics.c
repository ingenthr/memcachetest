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
 * Portions Copyright 2010 Trond Norbye
 * Simplified the logic to fit our needs.
 */

#include "config.h"
#include <sys/time.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "metrics.h"
#include "memcachetest.h"

bool initialize_thread_ctx(struct thread_context *ctx, int offset, size_t total)
{
    ctx->offset = offset;
    ctx->total = total;

    for (int ii = 0; ii < TX_CAS - TX_GET; ++ii) {
        ctx->tx[ii].set = calloc(ctx->total, sizeof(hrtime_t));
        if (ctx->tx[ii].set == NULL) {
            for (int jj = 0; jj < ii; ++jj) {
                free(ctx->tx[jj].set);
            }
            return false;
        }
    }

    return true;
}

/**
 * A comparison-function used by qsort.
 * @param p1 element 1 to compare
 * @param p2 element 2 to compare
 * @return -1, 0 or 1
 */
static int compare(const void *p1, const void *p2)
{
   hrtime_t a = *((hrtime_t *)p1);
   hrtime_t b = *((hrtime_t *)p2);

   if (a < b) {
      return -1;
   } else if (a > b) {
      return 1;
   } else {
      return 0;
   }
}

/**
 * External interface
 */
void record_tx(enum TxnType tx_type, hrtime_t tx_time, struct thread_context *ctx) {
    assert(tx_type < (TX_CAS - TX_GET));
    ctx->tx[tx_type].set[ctx->tx[tx_type].current++] = tx_time;
}

struct ResultMetrics *calc_metrics(enum TxnType tx_type,
                                   struct thread_context *ctx)
{
    struct ResultMetrics *ret = calloc(1, sizeof(*ret));
    if (ret == NULL) {
        return NULL;
    }
    struct samples *sample = &ctx->tx[tx_type];
    if (sample->current == 0) {
        return ret;
    }

    // sort the result
    qsort(sample->set, sample->current, sizeof(hrtime_t), compare);

    ret->success_count = sample->current ;
    long percentile90 = (0.9) * (float) (ret->success_count - 1) + 1.0;
    long percentile95 = (0.95) * (float) (ret->success_count - 1) + 1.0;
    long percentile99 = (0.99) * (float) (ret->success_count - 1) + 1.0;

    ret->max90th_result = sample->set[percentile90];
    ret->max95th_result = sample->set[percentile95];
    ret->max99th_result = sample->set[percentile99];
    ret->min_result = sample->set[0];
    ret->max_result = sample->set[sample->current - 1];

    hrtime_t total = 0;
    for (int ii = 0; ii < sample->current; ++ii) {
        total += sample->set[ii];
    }

    ret->average = total / sample->current;

    return ret;
}

/**
 * Convert a time (in ns) to a human readable form...
 * @param time the time in nanoseconds
 * @param buffer where to store the result
 * @param size the size of the buffer
 * @return buffer
 */
static const char* hrtime2text(hrtime_t t, char *buffer, size_t size) {
    static const char * const extensions[] = {"s", "ms", "us", "ns"}; //TODO: get a greek Mu in here correctly
    int id = sizeof(extensions)/sizeof(extensions[0]) - 1;

    while (t > 9999 && id > 0) {
        id--;
        t /= 1000;
    }

    snprintf(buffer, size, "%d %s", (int) t, extensions[id]);
    return buffer;
}

static void print_details(enum TxnType tx_type, struct ResultMetrics *r) {
    static const char * txt[] = { [TX_GET] = "Get",
                                   [TX_SET] = "Set",
                                   [TX_ADD] = "Add",
                                   [TX_REPLACE] = "Replace",
                                   [TX_APPEND] = "Append",
                                   [TX_PREPEND] = "Prepend",
                                   [TX_CAS] = "Cas" };


    printf("%s operations:\n", txt[tx_type]);
    char tavg[80];
    char tmin[80];
    char tmax[80];
    char tmax90[80];
    char tmax95[80];
    char tmax99[80];
    printf("     #of ops.       min       max       avg   max90th   max95th   max99th\n");
    printf("%13ld%10.10s%10.10s%10.10s%10.10s%10.10s%10.10s\n\n", r->success_count,
           hrtime2text(r->min_result, tmin, sizeof (tmin)),
           hrtime2text(r->max_result, tmax, sizeof (tmax)),
           hrtime2text(r->average, tavg, sizeof(tavg)),
           hrtime2text(r->max90th_result, tmax90, sizeof(tmax90)),
           hrtime2text(r->max95th_result, tmax95, sizeof(tmax95)),
           hrtime2text(r->max99th_result, tmax99, sizeof(tmax99)));
}

void print_metrics(struct thread_context *ctx) {
    for (int ii = 0; ii < TX_CAS - TX_GET; ++ii) {
        if (ctx->tx[ii].current > 0) {
            struct ResultMetrics *r = calc_metrics(ii, ctx);
            if (r) {
                print_details(ii, r);
                free(r);
            }
        }
    }
}

void print_aggregated_metrics(struct thread_context *ctx, int num)
{
    int total = 0;
    for (int ii = 0; ii < num; ++ii) {
        total += ctx[ii].total;
    }

    struct thread_context context;
    memset(&context, 0, sizeof(context));
    initialize_thread_ctx(&context, 0, total);

    for (int ii = 0; ii < num; ++ii) {
        for (int jj = 0; jj < TX_CAS - TX_GET; ++jj) {
            memcpy(context.tx[jj].set + context.tx[jj].current,
                   ctx[ii].tx[jj].set, ctx[ii].tx[jj].current * sizeof(hrtime_t));
            context.tx[jj].current += ctx[ii].tx[jj].current;
        }
    }

    print_metrics(&context);
}
