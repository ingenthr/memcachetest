#ifndef VBUCKET_H
#define VBUCKET_H 1

#include <stdbool.h>

#ifdef HAVE_LIBVBUCKET
#include "libvbucket/vbucket.h"
#endif

extern bool initialize_vbuckets(const char *location);
extern uint16_t get_vbucket(const char *key, size_t nkey);

#endif
