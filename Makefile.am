ACLOCAL_AMFLAGS = -I m4 --force
AUTOMAKE_OPTIONS = foreign
bin_PROGRAMS = memcachetest

memcachetest_SOURCES = \
                       boxmuller.c boxmuller.h \
                       libmemc.c libmemc.h \
                       main.c \
                       memcachetest.h \
                       metrics.c metrics.h \
                       timer.c \
                       vbucket.c vbucket.h
memcachetest_LDADD = $(LIBMEMCACHED) $(LIBVBUCKET)

