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

    ret = ts.tv_sec * 1000000000;
    ret += ts.tv_nsec;
#elif HAVE_GETTIMEOFDAY
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == -1) {
      return (-1ULL);
    }

    ret = tv.tv_sec * 1000000000;
    ret += tv.tv_usec * 1000;
#endif

    return ret;
}

#endif
