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
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
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
#include "memcachetest.h"
#include "boxmuller.h"
#include "vbucket.h"

#ifndef MAXINT
/* MAXINT doesn't seem to exist on MacOS */
#define MAXINT (int)(unsigned int)-1
#endif

struct host {
    const char *hostname;
    in_port_t port;
    struct host *next;
} *hosts = NULL;


/**
 * The set of data to operate on
 */
size_t *dataset;

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
} datablock = {.data = NULL, .size = 4096, .avg = 0, .min_size = 1};

const char *prefix = "";

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
    struct memcachelib* ret = malloc(sizeof(*ret));
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
    struct memcachelib* lib = (struct memcachelib*)handle;
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
    struct memcachelib* lib = (struct memcachelib*)connection->handle;
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
static inline bool memcached_get_wrapper(struct connection* connection,
                                          const char *key, int nkey,
                                          size_t *size, void **data) {
    struct memcachelib* lib = (struct memcachelib*)connection->handle;
    switch (lib->type) {
#ifdef HAVE_LIBMEMCACHED
    case LIBMEMCACHED_BINARY: /* FALLTHROUGH */
    case LIBMEMCACHED_TEXTUAL:
        {
            memcached_return rc;
            uint32_t flags;
            *data = memcached_get(lib->handle, key, nkey, size, &flags, &rc);
            if (rc != MEMCACHED_SUCCESS) {
                return false;
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
                return false;
            }
            *size = mitem.size;
            *data = mitem.data;
        }
        break;

    default:
        abort();
    }

    return true;
}

static struct connection* connectionpool;
static size_t connection_pool_size = 1;
static int thread_bind_connection = 0;

static int create_connection_pool(void) {
    connectionpool = calloc(connection_pool_size, sizeof(struct connection));
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
 * Convert a timeval structure to human readable form..
 * @param val the value to convert
 * @param buffer where to store the result
 * @param size the size of the buffer
 * @return buffer
 */
static const char* timeval2text(struct timeval* val, char *buffer, size_t size) {
    snprintf(buffer, size, "%2ld.%06lu", (long)val->tv_sec,
             (long)val->tv_usec);

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

    dataset = calloc(no_items, sizeof(size_t));
    if (dataset == NULL) {
        fprintf(stderr, "Failed to allocate memory for the dataset\n");
        return -1;
    }

    for (long ii = 0; ii < no_items; ++ii) {
        if (use_fixed_block_size) {
            dataset[ii] = datablock.size;
        } else {
            dataset[ii] = datablock.min_size +
                (random() % (datablock.size - datablock.min_size));
            assert(dataset[ii] >= datablock.min_size);
            assert(dataset[ii] <= datablock.size);
        }

        total += dataset[ii];
    }

    datablock.avg = (size_t)(total / no_items);
    return 0;
}

/**
 * Populate the dataset to the server
 * @return 0 if success, -1 if an error occurs
 */
static int populate_dataset(struct thread_context *ctx) {
    struct connection* connection = get_connection();
    int end = ctx->offset + ctx->total;
    char key[256];
    size_t nkey;
    int sres = -1;

    assert(end > ctx->offset);
    if (verbose) {
        fprintf(stderr, "Populating from %d to %d\n", ctx->offset, end);
    }
    for (int ii = ctx->offset; ii < end; ++ii) {
        nkey = snprintf(key, sizeof(key), "%s%d", prefix, ii);
        sres = memcached_set_wrapper(connection, key, nkey,
                                     datablock.data, dataset[ii]);
        if (sres != 0) {
            fprintf(stderr, "Failed to set [%s]: during populate.\n", key);
            release_connection(connection);
            return -1;
        }
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
    if (populate_dataset((struct thread_context*)arg) == 0) {
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
    pthread_t *threads = calloc(sizeof(pthread_t), no_threads);
    struct thread_context *ctx = calloc(sizeof(struct thread_context), no_threads);
    int perThread = no_items / no_threads;
    int rest = no_items % no_threads;
    size_t offset = 0;
    int ii;

    if (threads == NULL || ctx == NULL) {
        fprintf(stderr, "Failed to allocate memory\n");
        free(threads);
        free(ctx);
        return -1;
    }

    for (ii = 0; ii < no_threads; ++ii) {
        struct thread_context *ctxi = &ctx[ii];
        if (!initialize_thread_ctx(ctxi, offset,
                                   (rest > 0) ? perThread + 1 : perThread)) {
            abort();
        }
        offset += perThread;
        if (rest > 0) {
            --rest;
            ++offset;
        }

        pthread_create(&threads[ii], 0, populate_thread_main,
                       &ctx[ii]);
    }

    for (ii = 0; ii < no_threads; ++ii) {
        void *threadret;
        pthread_join(threads[ii], &threadret);
        if (threadret == NULL) {
            ret = -1;
        }
    }
    free(threads);
    free(ctx);

    return ret;
}


static int get_setval(void) {
    return random() % no_items;
}

/**
 * Test the server and library
 * @param rep Where to store the result of the test
 * @return 0 on success, -1 otherwise
 */
static int test(struct thread_context *ctx) {
    int ret = 0;
    struct connection* connection;
    char key[256];
    size_t nkey;
    for (size_t ii = 0; ii < ctx->total; ++ii) {
        connection = get_connection();
        int idx = get_setval();
        nkey = snprintf(key, sizeof(key), "%s%d", prefix, idx);

        if (setprc > 0 && (random() % 100) < setprc) {
            if (verbose) {
                fprintf(stderr, "CMD: set %s\n", key);
            }
            hrtime_t delta;
            hrtime_t start = gethrtime();
            memcached_set_wrapper(connection, key, nkey,
                                  datablock.data, dataset[idx]);
            delta = gethrtime() - start;
            record_tx(TX_SET, delta, ctx);
        } else {
            /* go set it from random data */
            if (verbose) {
                fprintf(stderr, "CMD: get %s\n", key);
            }
            hrtime_t delta;
            size_t size = 0;
            hrtime_t start = gethrtime();
            void *data;
            bool found = memcached_get_wrapper(connection, key, nkey, &size,
                                               &data);

            delta = gethrtime() - start;
            if (found) {
                if (size != dataset[idx]) {
                    fprintf(stderr,
                            "Incorrect length returned for <%s>. "
                            "Stored %zu got %zu\n",
                            key, dataset[idx], (long)size);
                } else if (verify_data &&
                           memcmp(datablock.data, data, size) != 0) {
                    fprintf(stderr, "Garbled data for <%s>\n", key);
                }
                record_tx(TX_GET, delta, ctx);
                free(data);
            } else {
                fprintf(stderr, "<%s> isn't there anymore\n", key);
            }
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
    test((struct thread_context*)arg);
    return arg;
}

/**
 * Add a host into the list of memcached servers to use
 * @param hostname the hostname:port to connect to
 */
static void add_host(const char *hostname) {
    struct host *entry = malloc(sizeof(struct host));
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

    (void)snprintf(service, NI_MAXSERV, "%d", port);
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
    int ret = -1;
    switch (current_memcached_library) {
    case LIBMEMC_TEXTUAL:
        {
            int sock;
            char buffer[8192];
            struct addrinfo* addrinfo = lookuphost(entry->hostname, entry->port);
            if (addrinfo == NULL) {
                return -1;
            }

            memset(rusage, 0, sizeof(*rusage));

            if ((sock = socket(addrinfo->ai_family,
                               addrinfo->ai_socktype,
                               addrinfo->ai_protocol)) != -1) {
                if (connect(sock, addrinfo->ai_addr, addrinfo->ai_addrlen) != -1) {
                    if (send(sock, "stats\r\n", 7, 0) > 0) {
                        if (recv(sock, buffer, sizeof(buffer), 0) > 0) {
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
        }
        break;
    default:
        break;
    }
    return ret;
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

    while ((cmd = getopt(argc, argv, "K:QW:M:pL:P:Fm:t:h:i:s:c:VlSvC:")) != EOF) {
        switch (cmd) {
        case 'K':
            if (strlen(prefix) > 240) {
                fprintf(stderr, "Prefix too long\n");
                return 1;
            }
            prefix = optarg;
            break;
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
            }
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
            break;
        default:
            fprintf(stderr, "Usage: test [-h host[:port]] [-t #threads]");
            fprintf(stderr, " [-T] [-i #items] [-c #iterations]\n");
            fprintf(stderr, "            [-v] [-V] [-f dir] [-s seed] [-W size] [-C vbucketconfig]\n");
            fprintf(stderr, "\t-h The hostname:port where the memcached server is running\n");
            fprintf(stderr, "\t   (use mulitple -h args for multiple servers)\n");
            fprintf(stderr, "\t-t The number of threads to use\n");
            fprintf(stderr, "\t-i The number of items to operate with\n");
            fprintf(stderr, "\t-c The number of iteratons each thread should do\n");
            fprintf(stderr, "\t-l Loop and repeat the test, but print out information for each run\n");
            fprintf(stderr, "\t-m The minimum object size to use during testing\n");
            fprintf(stderr, "\t-M The maximum object size to use during testing\n");
            fprintf(stderr, "\t-F Use fixed message size, specified by -M\n");
            fprintf(stderr, "\t-V Verify the retrieved data\n");
            fprintf(stderr, "\t-v Verbose output\n");
            fprintf(stderr, "\t-L Use the specified memcached client library\n");
            fprintf(stderr, "\t-W connection pool size\n");
            fprintf(stderr, "\t-s Use the specified seed to initialize the random generator\n");
            fprintf(stderr, "\t-S Skip the populate of the data\n");
            fprintf(stderr, "\t-P The probability for a set operation\n");
            fprintf(stderr, "\t   (default: 33 meaning set 33%% of the time)\n");
            fprintf(stderr, "\t-K specify a prefix that is added to all of the keys\n");
            fprintf(stderr, "\t-C Read vbucket data from host:port specified\n");
            fprintf(stderr, "\nVersion: %s\n\n", VERSION);
            return 1;
        }
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

    if (initialize_dataset() == -1) {
        return 1;
    }

    if (create_connection_pool() == -1) {
        return 1;
    }

    if (populate && populate_data(no_threads) != 0) {
        return 1;
    }

    if (get_server_rusage(hosts, &server_start) == -1) {
        fprintf(stderr, "Failed to get server stats\n");
    }


    size_t nget = 0;
    size_t nset = populate ? no_items : 0;
    do {
        pthread_t *threads = calloc(sizeof(pthread_t), no_threads);
        struct thread_context *ctx = calloc(sizeof(struct thread_context), no_threads);
        int ii;

        if (no_iterations > 0) {
            int perThread = no_iterations / no_threads;
            int rest = no_iterations % no_threads;

            for (ii = 0; ii < no_threads; ++ii) {
                struct thread_context *ctxi = &ctx[ii];
                if (!initialize_thread_ctx(ctxi, 0,
                                           (rest > 0) ? perThread + 1 : perThread)) {
                    abort();
                }

                if (rest > 0) {
                    --rest;
                }
                pthread_create(&threads[ii], 0, test_thread_main, &ctx[ii]);
            }

            for (ii = 0; ii < no_threads; ++ii) {
                void *ret;
                pthread_join(threads[ii], &ret);
                assert(ret == (void*)&ctx[ii]);
                if (verbose) {
                    fprintf(stdout, "Details from thread %d\n", ii);
                    print_metrics(&ctx[ii]);
                }
            }
        }

        fprintf(stdout, "Average with %d threads\n", no_threads);
        print_aggregated_metrics(ctx, no_threads);
        free(threads);
        free(ctx);
    } while (loop);

    if (getrusage(RUSAGE_SELF, &rusage) == -1) {
        fprintf(stderr, "Failed to get resource usage: %s\n",
                strerror(errno));
    } else {
        struct timeval endtime = { .tv_sec = 0};
        char buffer[128];

        gettimeofday(&endtime, NULL);
        fprintf(stdout, "Usr: %s\n", timeval2text(&rusage.ru_utime,
                                                  buffer, sizeof(buffer)));
        fprintf(stdout, "Sys: %s\n", timeval2text(&rusage.ru_stime,
                                                  buffer, sizeof(buffer)));

        if (starttime.tv_sec != 0 && endtime.tv_sec != 0) {
            endtime.tv_sec -= starttime.tv_sec;
            endtime.tv_usec -= starttime.tv_usec;
            fprintf(stdout, "Tot: %s\n", timeval2text(&endtime,
                                                      buffer,
                                                      sizeof(buffer)));
        }

        if (get_server_rusage(hosts, &rusage) != -1) {
            rusage.ru_utime.tv_sec -= server_start.ru_utime.tv_sec;
            rusage.ru_utime.tv_usec = 0;
            rusage.ru_stime.tv_sec -= server_start.ru_stime.tv_sec;
            rusage.ru_stime.tv_usec = 0;

            fprintf(stdout, "Server time:\n");
            fprintf(stdout, "Usr: %s\n", timeval2text(&rusage.ru_utime,
                                                      buffer, sizeof(buffer)));
            fprintf(stdout, "Sys: %s\n", timeval2text(&rusage.ru_stime,
                                                      buffer, sizeof(buffer)));
        }
    }

    fprintf(stdout,"Total gets: %zu\n", nget);
    fprintf(stdout,"Total sets: %zu\n", nset);
    destroy_connection_pool();

    return 0;
}
