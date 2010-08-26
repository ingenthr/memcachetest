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
#include "config.h"
#include <sys/time.h>
#include <time.h>

#ifndef HAVE_GETHRTIME
hrtime_t gethrtime() {
    hrtime_t ret;

#ifdef HAVE_CLOCK_GETTIME
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        return (-1ULL);
    }

    ret = (hrtime_t)ts.tv_sec * 1000000000;
    ret += ts.tv_nsec;
#elif HAVE_GETTIMEOFDAY
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        return (-1ULL);
    }

    ret = (hrtime_t)tv.tv_sec * 1000000000;
    ret += tv.tv_usec * 1000;
#endif

    return ret;
}

#endif
