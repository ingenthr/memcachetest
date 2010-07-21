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
 * Copyright 2010 Trond Norbye
 */
#include "config.h"

#include <unistd.h>
#include <sys/fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vbucket.h"

#ifdef HAVE_LIBVBUCKET

VBUCKET_CONFIG_HANDLE vbucket_handle;

bool initialize_vbuckets(const char *location)
{
    if (access(location, F_OK) == 0) {
        vbucket_handle = vbucket_config_parse_file(location);
        if (vbucket_handle == NULL) {
            fprintf(stderr, "Failed to parse vbucket config: %s\n",
                    vbucket_get_error());
        }
        return vbucket_handle != NULL;
    }

    const char *p = location;
    while (isalpha(*p) || isdigit(*p) || *p == '.' || *p == ':') {
        ++p;
    }

    if (*p) {
        fprintf(stderr, "%s is not a file, and doesn't look like a URL to me\n",
                location);
        return false;
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "wget -O - http://%s/pools/default/buckets/default", location);

    fprintf(stdout, "Downloading vbucket config from %s\n",
            location);
    fflush(stdout);

    FILE *in = popen(cmd, "r");
    char *config = malloc(8196);
    *config = '\0';

    while (fgets(cmd, sizeof(cmd), in) != NULL) {
        strcat(config, cmd);
    }

    pclose(in);

    if (strlen(config) == 0) {
        return false;
    }

    vbucket_handle = vbucket_config_parse_string(config);
    if (vbucket_handle == NULL) {
        fprintf(stderr, "Failed to parse vbucket config: %s\n",
                vbucket_get_error());
        fprintf(stderr, "Config: [%s]", config);
    }
    return vbucket_handle != NULL;
}

uint16_t get_vbucket(const char *key, size_t nkey) {
    if (vbucket_handle) {
        return vbucket_get_vbucket_by_key(vbucket_handle, key, nkey);
    }
    return 0;
}

#else
bool initialize_vbuckets(const char *location)
{
    (void)location;
    return false;
}

uint16_t get_vbucket(const char *key, size_t nkey) {
    (void)key;
    (void)nkey;
    return 0;
}

#endif
