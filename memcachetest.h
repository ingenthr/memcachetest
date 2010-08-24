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
 * File:   memcachetest.h
 * Author: ingenthr
 *
 * Created on March 19, 2010, 3:01 PM
 */
/*
 * Portions Copyright 2010 Trond Norbye
 * Simplified the logic to fit our needs.
 */

#ifndef MEMCACHETEST_H
#define	MEMCACHETEST_H

#include <stdbool.h>
#include "metrics.h"

#ifdef	__cplusplus
extern "C" {
#endif

    struct samples {
        int current;
        hrtime_t *set;
    };

    /**
     * A struct for the info on the thread
     */
    struct thread_context {
        int offset;
        size_t total;
        struct samples tx[TX_CAS - TX_GET];
        /* struct report thr_summary; */
    };

    bool initialize_thread_ctx(struct thread_context *ctx,
                               int offset,
                               size_t total);

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMCACHETEST_H */

