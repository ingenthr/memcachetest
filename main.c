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
 * Portions Copyright 2009 Matt Ingenthron
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/resource.h>
#include <assert.h>
#include <string.h>

#ifdef HAVE_LIBMEMCACHED
#include "libmemcached/memcached.h"
#endif

#include "libmemc.h"
#include "metrics.h"
#include "boxmuller.h"
#include "vbucket.h"

#ifndef MAXINT
/* I couldn't find MAXINT on my MacOSX box.. I should update this... */
#define MAXINT (int)(unsigned int)-1
#endif

struct host {
    const char *hostname;
    in_port_t port;
    struct host *next;
} *hosts = NULL;

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
 * Each item represented in the cache
 */
struct item {
    /**
     * The item's key
     */
    const char *key;
    /**
     * The length of the key to avoid calling strlen all the time
     */
    size_t keylen;
    /**
     * The size of the data stored in the cache (all data stored in memcached
     * will start from datablock.data)
     */
    size_t size;
};

/**
 * The set of data to operate on
 */
struct item *dataset;

static void *keyarray;
static int keylength;
static long totalkeys;
static long long globalSetCount=0;
static long long globalGetCount=0;
/**
 * The datablock to operate with
 */
struct datablock {
    /**
     * Pointer to the datablock
     */
    void *data;
    /**
     * Minimum size generated for any given data block.
     */
    size_t min_size;
    /**
     * The size of the datablock
     */
    size_t size;
    /**
     * The average size of all the blocks
     */
    size_t avg;
} datablock = {.data = NULL, .size = 4096, .avg = 0};

/**
 * Set to one if you would like fixed block sizes
 */
int use_fixed_block_size = 0;

/**
 * Set to 1 if you would like the memcached client to connect to multiple
 * servers.
 */
int use_multiple_servers = 1;

/** The number of items to operate on (may be overridden with -i */
long no_items = 10000;
/** The number of operations (pr thread) to execute (may be overridden with -c */
long long no_iterations = 10000;
/** If we should verify the data received. May be overridden with -V */
int verify_data = 0;

/** The probaility for a set operation */
int setprc = 33;

/** what kind of benchmark are we running
    TODO: this shouldn't be a global, or if so shouldn't be done this way */
int set_bench = 0; /* whether or not this is a simple dataset bench */

double stddev = 0;
int verbose = 0;

/** TODO: get rid of these after testing */
double max_result, min_result;

/**
 * The different client libraries we have support for
 */
enum Libraries {
    LIBMEMC_TEXTUAL = 1,
    LIBMEMC_BINARY,
#ifdef HAVE_LIBMEMCACHED
    LIBMEMCACHED_TEXTUAL,
    LIBMEMCACHED_BINARY,
#endif
    INVALID_LIBRARY
};

struct memcachelib {
    int type;
    void *handle;
};

/**
 * The current library in use (all threads must use the same library)
 */
int current_memcached_library = LIBMEMC_TEXTUAL;

/**
 * Print progress information during the test..
 */
static int progress = 0;

struct connection {
    pthread_mutex_t mutex;
    void *handle;
};

/**
 * Create a handle to a memcached library
 */
static void *create_memcached_handle(void) {
    struct memcachelib* ret = malloc(sizeof (*ret));
    ret->type = current_memcached_library;

    switch (current_memcached_library) {
#ifdef HAVE_LIBMEMCACHED
    case LIBMEMCACHED_TEXTUAL:
        {
            memcached_st *memc = memcached_create(NULL);
            for (struct host *host = hosts; host != NULL; host = host->next) {
                memcached_server_add(memc, host->hostname, host->port);
                if (!use_multiple_servers) {
                    break;
                }
            }
            ret->handle = memc;
        }
        break;
    case LIBMEMCACHED_BINARY:
        {
            memcached_st *memc = memcached_create(NULL);
            memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
            for (struct host *host = hosts; host != NULL; host = host->next) {
                memcached_server_add(memc, host->hostname, host->port);
                if (!use_multiple_servers) {
                    break;
                }
            }
            ret->handle = memc;
        }
        break;
#endif
    case LIBMEMC_TEXTUAL:
        {
            struct Memcache* memcache = libmemc_create(Textual);
            for (struct host *host = hosts; host != NULL; host = host->next) {
                libmemc_add_server(memcache, host->hostname, host->port);
                if (!use_multiple_servers) {
                    break;
                }
            }
            ret->handle = memcache;
        }
        break;
    case LIBMEMC_BINARY:
        {
            struct Memcache* memcache = libmemc_create(Binary);
            for (struct host *host = hosts; host != NULL; host = host->next) {
                libmemc_add_server(memcache, host->hostname, host->port);
                if (!use_multiple_servers) {
                    break;
                }
            }
            ret->handle = memcache;
        }
        break;
    default:
        abort();
    }

    return ret;
}

/**
 * Release a handle to a memcached library
 */
static void release_memcached_handle(void *handle) {
    struct memcachelib* lib = (struct memcachelib*) handle;
    switch (lib->type) {
#ifdef HAVE_LIBMEMCACHED
    case LIBMEMCACHED_BINARY: /* FALLTHROUGH */
    case LIBMEMCACHED_TEXTUAL:
        {
            memcached_st *memc = lib->handle;
            memcached_free(memc);
        }
        break;
#endif

    case LIBMEMC_BINARY:
    case LIBMEMC_TEXTUAL:
        libmemc_destroy(lib->handle);
        break;

    default:
        abort();
    }
}

/**
 * Set a key / value pair on the memcached server
 * @param handle Thandle to the memcached library to use
 * @param key The items key
 * @param nkey The length of the key
 * @param data The data to set
 * @param The size of the data to set
 * @return 0 on success -1 otherwise
 */
static inline int memcached_set_wrapper(struct connection *connection,
                                        const char *key, int nkey,
                                        const void *data, int size) {
    struct memcachelib* lib = (struct memcachelib*) connection->handle;
    switch (lib->type) {
#ifdef HAVE_LIBMEMCACHED
    case LIBMEMCACHED_BINARY: /* FALLTHROUGH */
    case LIBMEMCACHED_TEXTUAL:
        {
            int rc = memcached_set(lib->handle, key, nkey, data, size, 0, 0);
            if (rc != MEMCACHED_SUCCESS) {
                return -1;
            }
        }
        break;
#endif
    case LIBMEMC_BINARY:
    case LIBMEMC_TEXTUAL:
        {
            struct Item mitem = {
                .key = key,
                .keylen = nkey,
                /* Set will not modify data */
                .data = (void*)data,
                .size = size
            };
            if (libmemc_set(lib->handle, &mitem) != 0) {
                return -1;
            }
        }
        break;

    default:
        abort();
    }
    return 0;
}

/**
 * Get the value for a key from the memcached server
 * @param connection the connection to use
 * @param key The items key
 * @param nkey The length of the key
 * @param The size of the data
 * @return pointer to the data on success, -1 otherwise
 * TODO: the return of -1 isn't really true
 */
static inline void *memcached_get_wrapper(struct connection* connection,
                                          const char *key, int nkey, size_t *size) {
    struct memcachelib* lib = (struct memcachelib*) connection->handle;
    void *ret = NULL;
    switch (lib->type) {
#ifdef HAVE_LIBMEMCACHED
    case LIBMEMCACHED_BINARY: /* FALLTHROUGH */
    case LIBMEMCACHED_TEXTUAL:
        {
            memcached_return rc;
            uint32_t flags;
            ret = memcached_get(lib->handle, key, nkey, size, &flags, &rc);
            if (rc != MEMCACHED_SUCCESS) {
                return NULL;
            }
        }
        break;
#endif
    case LIBMEMC_BINARY:
    case LIBMEMC_TEXTUAL:
        {
            struct Item mitem = {
                .key = key,
                .keylen = nkey
            };

            if (libmemc_get(lib->handle, &mitem) != 0) {
                return NULL;
            }
            *size = mitem.size;
            ret = mitem.data;
        }
        break;

    default:
        abort();
    }

    return ret;
}



static struct connection* connectionpool;
static size_t connection_pool_size = 1;
static int thread_bind_connection = 0;

static int create_connection_pool(void) {
    connectionpool = calloc(connection_pool_size, sizeof (struct connection));
    if (connectionpool == NULL) {
        return -1;
    }

    for (size_t ii = 0; ii < connection_pool_size; ++ii) {
        if (pthread_mutex_init(&connectionpool[ii].mutex, NULL) != 0) {
            abort();
        }
        if ((connectionpool[ii].handle = create_memcached_handle()) == NULL) {
            abort();
        }
    }
    return 0;
}

static void destroy_connection_pool(void) {
    for (size_t ii = 0; ii < connection_pool_size; ++ii) {
        pthread_mutex_destroy(&connectionpool[ii].mutex);
        release_memcached_handle(connectionpool[ii].handle);
    }

    free(connectionpool);
    connectionpool = NULL;
}

static struct connection *get_connection(void) {
    if (thread_bind_connection) {
#ifdef __sun
        return &connectionpool[pthread_self()];
#else
        /* @FIXME!!!! */
        return &connectionpool[0];
#endif
    } else {
        int idx;
        do {
            idx = random() % connection_pool_size;
        } while (pthread_mutex_trylock(&connectionpool[idx].mutex) != 0);

        return &connectionpool[idx];
    }
}

static void release_connection(struct connection *connection) {
    pthread_mutex_unlock(&connection->mutex);
}

/**
 * Convert a time (in ns) to a human readable form...
 * @param time the time in nanoseconds
 * @param buffer where to store the result
 * @param size the size of the buffer
 * @return buffer
 */
static const char* hrtime2text(hrtime_t t, char *buffer, size_t size) {
    static const char * const extensions[] = {"ns", "us", "ms", "s" }; //TODO: get a greek Mu in here correctly
    int id = 0;

    while (t > 9999) {
        ++id;
        t /= 1000;
        if (id > 3) {
            break;
        }
    }

    snprintf(buffer, size, "%d %s", (int) t, extensions[id]);
    buffer[size - 1] = '\0';
    return buffer;
}

/**
 * Convert a timeval structure to human readable form..
 * @param val the value to convert
 * @param buffer where to store the result
 * @param size the size of the buffer
 * @return buffer
 */
static const char* timeval2text(struct timeval* val, char *buffer, size_t size) {
    snprintf(buffer, size, "%2ld.%06lu", (long) val->tv_sec,
             (long) val->tv_usec);

    return buffer;
}

/**
 * Initialize the dataset to work on
 * @return 0 if success, -1 if memory allocation fails
 */
static int initialize_dataset(void) {
    uint64_t total = 0;

    if (datablock.data != NULL) {
        free(datablock.data);
    }

    datablock.data = malloc(datablock.size);
    if (datablock.data == NULL) {
        fprintf(stderr, "Failed to allocate memory for the datablock\n");
        return -1;
    }

    memset(datablock.data, 0xff, datablock.size);

    if (dataset != NULL) {
        free(dataset);
    }

    dataset = calloc(no_items, sizeof (struct item));
    if (dataset == NULL) {
        fprintf(stderr, "Failed to allocate memory for the dataset\n");
        return -1;
    }

    for (long ii = 0; ii < no_items; ++ii) {
        char buffer[128];
        sprintf(buffer, "%08ld", ii);

        dataset[ii].key = strdup(buffer);
        dataset[ii].keylen = strlen(buffer);
        if (use_fixed_block_size) {
            dataset[ii].size = datablock.size;
            total += dataset[ii].size;
        } else {
            dataset[ii].size = datablock.min_size
                + (random() % (datablock.size - datablock.min_size));
            if (dataset[ii].size == 0) {
                dataset[ii].size = 1024;
            }
            assert(dataset[ii].size >= datablock.min_size);
            assert(dataset[ii].size <= datablock.size);
            total += dataset[ii].size;
        }
    }

    datablock.avg = (size_t) (total / no_items);
    return 0;
}

/**
 * Populate the dataset to the server
 * @return 0 if success, -1 if an error occurs
 */
static int populate_dataset(struct report *rep) {
    struct connection* connection = get_connection();
    int end = rep->offset + rep->total;
    for (int ii = rep->offset; ii < end; ++ii) {
        if (memcached_set_wrapper(connection,
                                  dataset[ii].key, dataset[ii].keylen,
                                  datablock.data, dataset[ii].size) != 0) {
            fprintf(stderr, "Failed to set data!\n");
            release_connection(connection);
            return -1;
        }
        globalSetCount++;
        assert((verify_data) ? dataset[ii].keylen == strlen(dataset[ii].key) : 1);
    }

    release_connection(connection);
    return 0;
}

/**
 * The threads entry function
 * @param arg this should be a pointer to where this thread should report
 *            the result
 * @return arg
 */
static void *populate_thread_main(void* arg) {
    if (populate_dataset((struct report*) arg) == 0) {
        return arg;
    } else {
        return NULL;
    }
}

/**
 * Populate the data on the servers
 * @param no_threads the number of theads to use
 * @return 0 if success, -1 otherwise
 */
static int populate_data(int no_threads) {
    int ret = 0;
    if (no_threads > 1) {
        pthread_t *threads = calloc(sizeof (pthread_t), no_threads);
        struct report *reports = calloc(sizeof (struct report), no_threads);
        int perThread = no_items / no_threads;
        int rest = no_items % no_threads;
        size_t offset = 0;
        int ii;

        if (threads == NULL || reports == NULL) {
            fprintf(stderr, "Failed to allocate memory\n");
            free(threads);
            free(reports);
            return -1;
        }

        for (ii = 0; ii < no_threads; ++ii) {
            reports[ii].offset = offset;
            reports[ii].total = perThread;
            offset += perThread;
            if (rest > 0) {
                --rest;
                ++reports[ii].total;
                ++offset;
            }
            pthread_create(&threads[ii], 0, populate_thread_main,
                           &reports[ii]);
        }

        for (ii = 0; ii < no_threads; ++ii) {
            void *threadret;
            pthread_join(threads[ii], &threadret);
            if (threadret == NULL) {
                ret = -1;
            }
        }
        free(threads);
        free(reports);
    } else {
        struct report report;
        report.offset = 0;
        report.total = no_items;
        ret = populate_dataset(&report);
    }

    return ret;
}


static struct item get_setval(void) {
    struct item ret = { .key = NULL, .keylen = 0, .size = 0 };

    if (keyarray != 0) {
        // Use keys loaded from disk..
        ret.keylen = keylength;
        ret.size = 1024; // add random value;

        /* test the box_muller */
        double m = 6.0; /* this is the mean; stddev set via option -y */
        assert(totalkeys > 0);

        int median_keys = totalkeys;
        if (totalkeys % 2 == 1) {
            median_keys = (totalkeys - 1) / 2;
        } else {
            median_keys = totalkeys / 2;
        }

        double result = box_muller(m, stddev);

        /* the following and setting the mean around 6 and later dividing
         * by 6 is a hackish way to deal with the outliers
         */
        if (result < 0) {
            if (verbose) {
                fprintf(stderr, "WARNING: result was %f, result set to 0\n",
                        result);
            }
            result = 0;
        }

        if (result > 12) {
            if (verbose) {
                fprintf(stderr, "WARNING: result was %f, set_result set to 12\n",
                        result);
            }
            result = 12.0;
        }

        /* TODO: make sure we don't go above or below the bottom */
        unsigned int offset = (int)(median_keys * result/6);
        offset %= totalkeys;

        ret.key = (const char*)keyarray + ((keylength + 1) * (offset));
    } else {
        /* try use the ring... */
        int offset = random() % no_items;
        ret.key = dataset[offset].key;
        ret.keylen = dataset[offset].keylen;
        ret.size = dataset[offset].size;
    }

    return ret;
}

/**
 * Test the library (perform a number of operations on the server).
 * @param rep Where to store the result of the test
 * @return 0 on success, -1 otherwise
 */
static int test(struct report *rep) {
    int ret = 0;
    struct connection* connection;
    rep->bestGet = rep->bestSet = 99999999;
    rep->worstGet = rep->worstSet = 0;

    for (size_t ii = 0; ii < rep->total; ++ii) {
        connection = get_connection();

        struct item item = get_setval();

        // Ensure that the item is paged in..
        for (size_t jj = 0; jj < item.keylen; ++jj) {
            if (item.key[jj] == '\0' || item.key[jj] == '\n') {
                abort();
            }
        }

        if (keyarray == NULL && setprc > 0 && (random() % 100) < setprc) { // @todo fixme!!! (we don't do set right now...)
            hrtime_t delta;
            hrtime_t start = gethrtime();
            memcached_set_wrapper(connection, item.key, item.keylen,
                                  datablock.data, item.size);
            globalSetCount++;
            delta = gethrtime() - start;
            if (delta < rep->bestSet) {
                rep->bestSet = delta;
            }
            if (delta > rep->worstSet) {
                rep->worstSet = delta;
            }
            rep->setDelta += delta;
            // record_tx(TX_SET, delta);  Need to add sets!
            ++rep->set;
        } else {
            /* go set it from random data */
            if (verbose) {
                char buffer[260] = {0};
                snprintf(buffer, item.keylen+1, "%s", item.key);
                fprintf(stderr, "CMD: get %s\n", buffer);
                fflush(stderr);
            }
            hrtime_t delta;
            size_t size = 0;
            hrtime_t start = gethrtime();
            void *data = memcached_get_wrapper(connection, item.key,
                                               item.keylen, &size);

            globalGetCount++;
            delta = gethrtime() - start;
            if (delta < rep->bestGet) {
                rep->bestGet = delta;
            }
            if (delta > rep->worstGet) {
                rep->worstGet = delta;
            }
            rep->getDelta += delta;
            if (data != NULL) {
                if (size != item.size && keyarray == NULL) {
                    fprintf(stderr,
                            "Incorrect length returned for <%s>. "
                            "Stored %ld got %ld\n",
                            item.key, (long)item.keylen, (long)size);
                } else if (verify_data && keyarray == NULL &&
                           memcmp(datablock.data, data, item.size) != 0) {
                    fprintf(stderr, "Garbled data for <%s>\n",
                            item.key);
                }
                // record_tx(TX_GET, delta);
                free(data);
            } else {
                fprintf(stderr, "missing data for <");
                for (size_t jj = 0; jj < item.keylen; ++jj) {
                    fprintf(stderr, "%c", item.key[jj]);
                }
                fprintf(stderr, ">\n");
                // record_error(TX_GET, delta);
            }
            ++rep->get;
        }
        release_connection(connection);
    }

    return ret;
}

/**
 * The threads entry function
 * @param arg this should be a pointer to where this thread should report
 *            the result
 * @return arg
 */
static void *test_thread_main(void* arg) {
    test((struct report*) arg);
    return arg;
}

/**
 * Add a host into the list of memcached servers to use
 * @param hostname the hostname:port to connect to
 */
static void add_host(const char *hostname) {
    struct host *entry = malloc(sizeof (struct host));
    if (entry == 0) {
        fprintf(stderr, "Failed to allocate memory for <%s>. Host ignored\n",
                hostname);
        fflush(stderr);
        return;
    }
    entry->next = hosts;
    hosts = entry;
    entry->hostname = strdup(hostname);
    char *ptr = strchr(entry->hostname, ':');
    if (ptr != NULL) {
        *ptr = '\0';
        entry->port = atoi(ptr + 1);
    } else {
        entry->port = 11211;
    }
}

static struct addrinfo *lookuphost(const char *hostname, in_port_t port) {
    struct addrinfo *ai = 0;
    struct addrinfo hints = {
        .ai_flags = AI_PASSIVE|AI_ADDRCONFIG,
        .ai_family = AF_UNSPEC,
        .ai_protocol = IPPROTO_TCP,
        .ai_socktype = SOCK_STREAM };
    char service[NI_MAXSERV];
    int error;

    (void) snprintf(service, NI_MAXSERV, "%d", port);
    if ((error = getaddrinfo(hostname, service, &hints, &ai)) != 0) {
        if (error != EAI_SYSTEM) {
            fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(error));
        } else {
            perror("getaddrinfo()");
        }
    }

    return ai;
}

static int get_server_rusage(const struct host *entry, struct rusage *rusage) {
    int sock;
    int ret = -1;
    char buffer[8192];

    struct addrinfo* addrinfo = lookuphost(entry->hostname, entry->port);
    if (addrinfo == NULL) {
        return -1;
    }

    memset(rusage, 0, sizeof (*rusage));

    if ((sock = socket(addrinfo->ai_family,
                       addrinfo->ai_socktype,
                       addrinfo->ai_protocol)) != -1) {
        if (connect(sock, addrinfo->ai_addr, addrinfo->ai_addrlen) != -1) {
            if (send(sock, "stats\r\n", 7, 0) > 0) {
                if (recv(sock, buffer, sizeof (buffer), 0) > 0) {
                    char *ptr = strstr(buffer, "rusage_user");
                    if (ptr != NULL) {
                        rusage->ru_utime.tv_sec = atoi(ptr + 12);
                        ptr = strchr(ptr, '.');
                        if (ptr != NULL) {
                            rusage->ru_utime.tv_usec = atoi(ptr + 1);
                        }
                    }

                    ptr = strstr(buffer, "rusage_system");
                    if (ptr != NULL) {
                        rusage->ru_stime.tv_sec = atoi(ptr + 14);

                        ptr = strchr(ptr, '.');
                        if (ptr != NULL) {
                            rusage->ru_stime.tv_usec = atoi(ptr + 1);
                        }
                    }
                    ret = 0;
                } else {
                    fprintf(stderr, "Failed to read data: %s\n", strerror(errno));
                }
            } else {
                fprintf(stderr, "Failed to send data: %s\n", strerror(errno));
            }
        } else {
            fprintf(stderr, "Failed to connect socket: %s\n", strerror(errno));
        }

        close(sock);
    } else {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
    }

    freeaddrinfo(addrinfo);
    return ret;
}

static int load_keys(const char *fname) {
    struct stat st;
    if (stat(fname, &st) == -1) {
        perror("Failed to stat(3c) keyfile");
        return -1;
    }
    FILE *fp = fopen(fname, "r");
    if (fp == NULL) {
        perror("Failed to open keyfile");
        return -1;
    }
#ifdef HAVE_MMAP
    keyarray = mmap((caddr_t) 0, st.st_size, PROT_READ, MAP_PRIVATE, fileno(fp), 0);
    if (keyarray == NULL) {
        perror("Failed to memorymap key file");
        fclose(fp);
        return -1;
    }
#ifdef HAVE_POSIX_MADVISE
    (void) posix_madvise(keyarray, st.st_size, POSIX_MADV_RANDOM);
#endif

#else
    keyarray = malloc(st.st_size);
    size_t offset = 0;
    ssize_t nr;
    do {
        nr = read(fileno(fp), keyarray + offset, st.st_size - offset);
        if (nr > 0) {
            offset += nr;
        }
    } while (offset < st.st_size);
#endif

    char *ptr = keyarray;
    keylength = 0;
    while (*ptr != '\0' && *ptr != '\n') {
        ++ptr;
        ++keylength;
        if (ptr == ((const char*)keyarray + st.st_size)) {
            break;
        }
    }
    totalkeys = st.st_size / (keylength + 1);
    return 0;
}

/**
 * Program entry point
 * @param argc argument count
 * @param argv argument vector
 * @return 0 on success, 1 otherwise
 */
int main(int argc, char **argv) {
    int cmd;
    int no_threads = 1;
    int populate = 1;
    int loop = 0;
    struct rusage rusage;
    struct rusage server_start;
    struct timeval starttime = {.tv_sec = 0};
    int size;
    gettimeofday(&starttime, NULL);

    while ((cmd = getopt(argc, argv, "QW:M:pL:P:Fm:t:h:i:s:c:VlSvy:xk:C:")) != EOF) {
        switch (cmd) {
        case 'p':
            progress = 1;
            break;
        case 'P':
            setprc = atoi(optarg);
            if (setprc > 100) {
                setprc = 100;
            } else if (setprc < 0) {
                setprc = 0;
            }
            break;
        case 't':
            no_threads = atoi(optarg);
            break;
        case 'L':
            current_memcached_library = atoi(optarg);
            break;
        case 'M':
            size = atoi(optarg);
            if (size > 1024 * 1024 *20) {
                fprintf(stderr, "WARNING: Too big block size %d\n", size);
            } else {
                datablock.size = size;
            }
            break;
        case 'F': use_fixed_block_size = 1;
            break;
        case 'h': add_host(optarg);
            break;
        case 'i': no_items = atoi(optarg);
            break;
        case 's': srand(atoi(optarg));
            break;
        case 'c': no_iterations = atoll(optarg);
            break;
        case 'V': verify_data = 1;
            break;
        case 'l': loop = 1;
            break;
        case 'S': populate = 0;
            break;
        case 'v': verbose = 1;
            break;
        case 'W': connection_pool_size = atoi(optarg);
            break;
        case 'Q': thread_bind_connection = 1;
            break;
        case 'm':
            {
                size = atoi(optarg);
                if (size > 1024 * 1024) {
                    fprintf(stderr, "WARNING: Too big block size %d\n", size);
                } else {
                    datablock.min_size = size;
                }
                break;
            }
        case 'k':
            if (load_keys(optarg) == -1) {
                return 1;
            }
            break;
        case 'y':
            stddev = atof(optarg);
            break;
        case 'x':
            set_bench = 1;
            populate = 0;
            break;
        case 'C':
#ifndef HAVE_LIBVBUCKET
            fprintf(stderr, "You need to rebuild memcachetest with libvbucket\n");
            return 1;
#else
            if (!initialize_vbuckets(optarg)) {
                return -1;
            }
#endif
        default:
            fprintf(stderr, "Usage: test [-h host[:port]] [-t #threads]");
            fprintf(stderr, " [-T] [-i #items] [-c #iterations] [-v] ");
            fprintf(stderr, "[-V] [-f dir] [-s seed] [-W size] [-x] [-y stddev] [-k keyfile] [-C vbucketconfig]\n");
            fprintf(stderr, "\t-h The hostname:port where the memcached server is running\n");
            fprintf(stderr, "\t   (use mulitple -h args for multiple servers)");
            fprintf(stderr, "\t-t The number of threads to use\n");
            fprintf(stderr, "\t-m The minimum object size to use during testing\n");
            fprintf(stderr, "\t-M The maximum object size to use during testing\n");
            fprintf(stderr, "\t-F Use fixed message size\n");
            fprintf(stderr, "\t-i The number of items to operate with\n");
            fprintf(stderr, "\t-c The number of iteratons each thread should do\n");
            fprintf(stderr, "\t-l Loop and repeat the test, but print out information for each run\n");
            fprintf(stderr, "\t-V Verify the retrieved data\n");
            fprintf(stderr, "\t-v Verbose output\n");
            fprintf(stderr, "\t-L Use the specified memcached client library\n");
            fprintf(stderr, "\t-W connection pool size\n");
            fprintf(stderr, "\t-s Use the specified seed to initialize the random generator\n");
            fprintf(stderr, "\t-S Skip the populate of the data\n");
            fprintf(stderr, "\t-P The probability for a set operation\n");
            fprintf(stderr, "\t-y Specify standard deviation for -x option test\n");
            fprintf(stderr, "\t-k The file with keys to be retrieved\n");
            fprintf(stderr, "\t-x randomly request from a set in a supplied file\n");
            fprintf(stderr, "\t\t(implies -S, requires -k)\n");
            fprintf(stderr, "\t-C Read vbucket data\n");
            return 1;
        }
    }

    if (set_bench == 1 && keyarray==NULL) {
        fprintf(stderr,"-x option requires keyfile (-k)\n");
        exit(-1);
    }

    if (connection_pool_size < (size_t)no_threads) {
        connection_pool_size = no_threads;
    }

    {
        size_t maxthreads = no_threads;
        struct rlimit rlim;

        if (maxthreads < connection_pool_size) {
            maxthreads = connection_pool_size;
        }

        if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
            if (rlim.rlim_cur < (maxthreads + 10)) {
                rlim.rlim_cur = maxthreads + 10;
                rlim.rlim_max = maxthreads + 10;
                if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
                    fprintf(stderr, "Failed to set file limit: %s\n",
                            strerror(errno));
                    return 1;
                }
            }
        } else {
            fprintf(stderr, "Failed to get file limit: %s\n", strerror(errno));
            return 1;
        }
    }

    if (hosts == NULL) {
        add_host("localhost");
    }

    if (keyarray == NULL && initialize_dataset() == -1 && !set_bench) {
        return 1;
    }

    if (create_connection_pool() == -1) {
        return 1;
    }

    if (keyarray != NULL) {
        populate = 0;
    }

    if (populate && populate_data(no_threads) == -1) {
        return 1;
    }

    if (get_server_rusage(hosts, &server_start) == -1) {
        fprintf(stderr, "Failed to get server stats\n");
    }

    do {
        pthread_t *threads = calloc(sizeof (pthread_t), no_threads);
        struct report *reports = calloc(sizeof (struct report), no_threads);
        int ii;
        size_t set = 0;
        size_t get = 0;
        hrtime_t setDelta = 0;
        hrtime_t getDelta = 0;
        hrtime_t bestSet = MAXINT;
        hrtime_t bestGet = MAXINT;
        hrtime_t worstSet = 0;
        hrtime_t worstGet = 0;
        int bestGetTid = 0;
        int worstGetTid = 0;
        int bestSetTid = 0;
        int worstSetTid = 0;


        if (no_threads > 1 && no_iterations > 0) {
            int perThread = no_iterations / no_threads;
            int rest = no_iterations % no_threads;
            int current = 0;
            int shift = 0;

            for (ii = 0; ii < no_threads; ++ii) {
                reports[ii].total = perThread;
                if (rest > 0) {
                    --rest;
                    ++reports[ii].total;
                }
                pthread_create(&threads[ii], 0, test_thread_main, &reports[ii]);
            }

            while (current < no_iterations) {
                struct report temp = { .offset = 0 };
                char buff[40];
                sleep(5);
                /* print average */


                for (ii = 0; ii < no_threads; ++ii) {
                    struct report *rep = &reports[ii];

                    temp.set += rep->set;
                    temp.get += rep->get;
                    temp.setDelta += rep->setDelta;
                    temp.getDelta += rep->getDelta;
                }

                if (progress) {
                    fprintf(stdout, "\rAvg: ");
                    if (temp.set > 0) {
                        fprintf(stdout, "set: %s (%ld) ",
                                hrtime2text(temp.setDelta / temp.set,
                                            buff, sizeof (buff)), (long)temp.set);
                    }

                    if (temp.get > 0) {
                        fprintf(stdout, "get: %s (%ld) ",
                                hrtime2text(temp.getDelta / temp.get,
                                            buff, sizeof (buff)), (long)temp.get);
                    }
                    ++shift;
                    if (shift % 10 == 0) {
                        fprintf(stdout, "\n");
                    }
                    fflush(stdout);
                }
                current = temp.set + temp.get;
            }

            if (progress) {
                fprintf(stdout, "\n");
            }


            for (ii = 0; ii < no_threads; ++ii) {
                void *ret;
                pthread_join(threads[ii], &ret);
                struct report *rep = ret;

                set += rep->set;
                get += rep->get;
                setDelta += rep->setDelta;
                getDelta += rep->getDelta;
                if (rep->bestSet < bestSet) {
                    bestSet = rep->bestSet;
                    bestSetTid = ii;
                }
                if (rep->worstSet > worstSet) {
                    worstSet = rep->worstSet;
                    worstSetTid = ii;
                }
                if (rep->bestGet < bestGet) {
                    bestGet = rep->bestGet;
                    bestGetTid = ii;
                }
                if (rep->worstGet > worstGet) {
                    worstGet = rep->worstGet;
                    worstGetTid = ii;
                }

                if (verbose) {
                    char setTime[80];
                    char getTime[80];
                    char bestSetTime[80];
                    char bestGetTime[80];
                    char worstSetTime[80];
                    char worstGetTime[80];

                    printf("Thread: %d\n", ii);
                    if (rep->set > 0) {
                        printf("  Avg set: %s (%ld) min: %s max: %s\n",
                               hrtime2text(rep->setDelta / rep->set,
                                           setTime, sizeof (setTime)),
                               (long)rep->set,
                               hrtime2text(rep->bestSet,
                                           bestSetTime, sizeof (bestSetTime)),
                               hrtime2text(rep->worstSet,
                                           worstSetTime, sizeof (worstSetTime)));

                    }
                    if (rep->get > 0) {
                        printf("  Avg get: %s (%ld) min: %s max: %s\n",
                               hrtime2text(rep->getDelta / rep->get,
                                           getTime, sizeof (getTime)),
                               (long)rep->get,
                               hrtime2text(rep->bestGet,
                                           bestGetTime, sizeof (bestGetTime)),
                               hrtime2text(rep->worstGet,
                                           worstGetTime, sizeof (worstGetTime)));

                    }
                }
            }
        } else if (no_iterations > 0) {
            reports[0].total = no_iterations;
            test(&reports[0]);
            set = reports[0].set;
            get = reports[0].get;
            setDelta = reports[0].setDelta;
            getDelta = reports[0].getDelta;
            bestSet = reports[0].bestSet;
            worstSet = reports[0].worstSet;
            bestGet = reports[0].bestGet;
            worstGet = reports[0].worstGet;

        }

        // struct ResultMetrics *getResults = calc_metrics(TX_GET); // this does only gets at the moment need a smarter calc_metrics

        /* print out the results */

        /*
          char tavg[80];
          char tmin[80];
          char tmax[80];
          char tmax90[80];
          char tmax95[80];

          printf("Get operations:\n");
          printf("     #of ops.       min       max        avg      max90th    max95th\n");
          printf("%13ld", getResults->success_count);
          printf("%11.11s", hrtime2text(getResults->min_result, tmin, sizeof (tmin)));
          printf("%11.11s", hrtime2text(getResults->max_result, tmax, sizeof (tmax)));
          printf("%11.11s", hrtime2text(getResults->average, tavg, sizeof(tavg)));
          printf("%13.13s", hrtime2text(getResults->max90th_result, tmax90, sizeof(tmax90)));
          printf("%12.12s", hrtime2text(getResults->max95th_result, tmax95, sizeof(tmax95)));

          printf("\n\n");
        */

        printf("Average with %d threads:\n", no_threads);
        if (set > 0) {
            char avg[80];
            char best[80];
            char worst[80];
            hrtime2text(setDelta / ((set > 0) ? set : 1), avg, sizeof (avg));
            hrtime2text(bestSet, best, sizeof (best));
            hrtime2text(worstSet, worst, sizeof (worst));

            printf("  Avg set: %s (%ld) min: %s (%d) max: %s (%d)\n",
                   avg, (long)set, best, bestSetTid, worst, worstSetTid);
        }
        if (get > 0) {
            char avg[80];
            char best[80];
            char worst[80];
            hrtime2text(getDelta / ((get > 0) ? get : 1), avg, sizeof (avg));
            hrtime2text(bestGet, best, sizeof (best));
            hrtime2text(worstGet, worst, sizeof (worst));

            printf("  Avg get: %s (%ld) min: %s (%d) max: %s (%d)\n",
                   avg, (long)get, best, bestGetTid, worst, worstGetTid);
        }

        free(reports);
        free(threads);
    } while (loop);

    if (getrusage(RUSAGE_SELF, &rusage) == -1) {
        fprintf(stderr, "Failed to get resource usage: %s\n",
                strerror(errno));
    } else {
        struct timeval endtime = { .tv_sec = 0};
        char buffer[128];

        gettimeofday(&endtime, NULL);
        fprintf(stdout, "Usr: %s\n", timeval2text(&rusage.ru_utime,
                                                  buffer, sizeof (buffer)));
        fprintf(stdout, "Sys: %s\n", timeval2text(&rusage.ru_stime,
                                                  buffer, sizeof (buffer)));

        if (starttime.tv_sec != 0 && endtime.tv_sec != 0) {
            endtime.tv_sec -= starttime.tv_sec;
            endtime.tv_usec -= starttime.tv_usec;
            fprintf(stdout, "Tot: %s\n", timeval2text(&endtime,
                                                      buffer,
                                                      sizeof (buffer)));
        }

        if (get_server_rusage(hosts, &rusage) != -1) {
            rusage.ru_utime.tv_sec -= server_start.ru_utime.tv_sec;
            rusage.ru_utime.tv_usec = 0;
            rusage.ru_stime.tv_sec -= server_start.ru_stime.tv_sec;
            rusage.ru_stime.tv_usec = 0;

            fprintf(stdout, "Server time:\n");
            fprintf(stdout, "Usr: %s\n", timeval2text(&rusage.ru_utime,
                                                      buffer, sizeof (buffer)));
            fprintf(stdout, "Sys: %s\n", timeval2text(&rusage.ru_stime,
                                                      buffer, sizeof (buffer)));
        }
    }

    fprintf(stdout,"Total gets: %lld\n",globalGetCount);
    fprintf(stdout,"Total sets: %lld\n",globalSetCount);
    destroy_connection_pool();

    return 0;
}
