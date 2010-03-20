/* 
 * File:   memcachetest.h
 * Author: ingenthr
 *
 * Created on March 19, 2010, 3:01 PM
 */

#ifndef _MEMCACHETEST_H
#define	_MEMCACHETEST_H

#include "metrics.h"

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * A struct holding the information I would like to measure for each test-run
     */
    struct report {
        /** The index in the items array to start at */
        int offset;
        /** The number of operations to execute */
        size_t total;
        /** The number of set-operation executed */
        size_t set;
        /** The total time of all of the set-operations */
        hrtime_t setDelta;
        /** The number of get-operations executed */
        size_t get;
        /** The total time of all of the get-operations */
        hrtime_t getDelta;
        /** The best set operation */
        hrtime_t bestSet;
        /** The best get operation */
        hrtime_t bestGet;
        /** The worst set operation */
        hrtime_t worstSet;
        /** The worst get operation */
        hrtime_t worstGet;
    };

    /**
     * A struct for the info on the thread
     */
    struct thread_context {
        struct TxnResult* head;
        struct report thr_summary;
    };




#ifdef	__cplusplus
}
#endif

#endif	/* _MEMCACHETEST_H */

