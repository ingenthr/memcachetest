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
#ifndef LIBMEMC_LIBMEMC_H
#define LIBMEMC_LIBMEMC_H

#include <sys/types.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C"  {
#endif

    struct Item {
        uint64_t cas_id;
        const char *key;
        int keylen;
        void *data;
        size_t size;
        size_t exptime;
    };

    enum Protocol { Binary = 1, Textual = 2 };

    struct Memcache* libmemc_create(enum Protocol protocol);
    void libmemc_destroy(struct Memcache* handle);
    int libmemc_add_server(struct Memcache *handle, const char *host,
                           in_port_t port);
    int libmemc_add(struct Memcache *handle, const struct Item *item);
    int libmemc_set(struct Memcache *handle, const struct Item *item);
    int libmemc_replace(struct Memcache *handle, const struct Item *item);
    int libmemc_get(struct Memcache *handle, struct Item *item);
    int libmemc_connect_server(const char *hostname, in_port_t port);
    char *libmemc_get_error(struct Memcache *handle);

#ifdef __cplusplus
}
#endif

#endif  /* LIBMEMC_LIBMEMC_H */
