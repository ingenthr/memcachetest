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
 * Provide gethrtime() for systems that don't have it natively.
 */

#include "config.h"
#ifndef HAVE_GETHRTIME

#include <sys/time.h>
#include <time.h>
#include <stdio.h>

/* Mac OS X implementation using mach_time.h */
#if defined(HAVE_MACH_MACH_TIME_H)
#include <mach/mach_time.h>

hrtime_t gethrtime() {
  static mach_timebase_info_data_t info = {0,0};

  /* Initialize once */
  if (info.denom == 0) {
      mach_timebase_info(&info);
  }

  return mach_absolute_time() * info.numer / info.denom;
}

/* Implementation using clock_gettime() with CLOCK_MONOTONIC */
#elif defined(HAVE_CLOCK_GETTIME)

hrtime_t gethrtime() {
  hrtime_t ret;
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
    perror("clock_gettime");
    return (-1ULL);
  }

  ret = ts.tv_sec * 1000000000;
  ret += ts.tv_nsec;

  return ret;
}

#else
/* Unreliable fallback implementation using gettimeofday() */
hrtime_t gethrtime() {
    hrtime_t ret;

    struct timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
        perror("gettimeofday");
        return (-1ULL);
    }

    ret = (hrtime_t)tv.tv_sec * 1000000000;
    ret += tv.tv_usec * 1000;

    return ret;
}

#endif /* #if defined(HAVE_MACH_MACH_TIME_H) */

#endif /* #ifndef HAVE_GETHRTIME */
