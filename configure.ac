AC_PREREQ(2.52)
AC_INIT(memcachetest, 0.5.0, ingenthr@cep.net)
AC_CONFIG_SRCDIR(main.c)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

AH_TOP([#ifndef CONFIG_H
#define CONFIG_H
])

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_C_CONST

AC_ARG_ENABLE(64bit,
  [AS_HELP_STRING([--enable-64bit],[build 64bit version])])
if test "x$enable_64bit" == "xyes"
then
    org_cflags=$CFLAGS
    CFLAGS=-m64
    AC_RUN_IFELSE(
      [AC_LANG_PROGRAM([], [dnl
return sizeof(void*) == 8 ? 0 : 1;
      ])
    ],[
      CFLAGS="-m64 $org_cflags"
    ],[
    AC_MSG_ERROR([Don't know how to build a 64-bit object.])
    ])
fi

AC_ARG_WITH(libmemcached,
    AS_HELP_STRING(--with-libmemcached,[Add support for libmemcached]),
    [
        if test "x$withval" = "xno"
        then
            without_libmemcached=true
        else
            if test "x$withval" != "x"
            then
               CFLAGS="-I$withval/include $CFLAGS"
	       CPPFLAGS="-I$withval/include $CPPFLAGS"
               # TODO: linux should have --rpath instead of -R
	       # TODO: MacOS has -rpath
               #LDFLAGS="-L$withval/lib -rpath $withval/lib"
	       LDFLAGS="-L$withval/lib -R$withval/lib"
            fi
        fi
    ]
)

AC_ARG_WITH(memcached,
    AS_HELP_STRING(--with-memcached,[Specify location of memcached installation]),
    [
        if test "x$withval" = "xno"
        then
            without_memcached=true
        else
            if test "x$withval" != "x"
            then
               CFLAGS="-I$withval/include $CFLAGS"
	       CPPFLAGS="-I$withval/include $CPPFLAGS"
            fi
        fi
    ]
)

# Currently I only support GCC and Sun Studio
if test "$GCC" = "yes"
then
  CFLAGS="-Wall -O3 $CFLAGS"
else
  CFLAGS="-Xa -xstrconst -mt -xunroll=2 -xprefetch=auto -xprefetch_level=3 $CFLAGS"
fi

AC_SEARCH_LIBS(socket, socket)
AC_SEARCH_LIBS(gethostbyname, nsl)
AC_SEARCH_LIBS(log, m)
AC_SEARCH_LIBS(sqrt, m)
AC_SEARCH_LIBS(pthread_create, pthread)

AC_CHECK_HEADER(sys/mman.h, AC_DEFINE(HAVE_MMAN_H, 1, [Define if you have sys/mman.h]))
AC_CHECK_FUNC(mmap, AC_DEFINE(HAVE_MMAP, 1, [Define if you have mmap]))
AC_CHECK_FUNC(madvise, AC_DEFINE(HAVE_MADVISE, 1, [Define if you have madvise]))
AC_CHECK_FUNC(gethrtime, AC_DEFINE(HAVE_GETHRTIME, 1, [Define if you have gethrtime]))
AS_IF(test "x${ac_cv_func_gethrtime}" = "xno",
      [AC_CHECK_FUNC(clock_gettime,  AC_DEFINE(HAVE_CLOCK_GETTIME, 1, [Define if you have clock_gettime]))
       AS_IF(test "x${ac_cv_func_clock_gettime}" = "xno",
                   [AC_CHECK_FUNC(gettimeofday,  AC_DEFINE(HAVE_GETTIMEOFDAY, 1, [Define if you have gettimeofday]))])
      ])

AH_BOTTOM(
#ifndef HAVE_GETHRTIME
#include <stdint.h>
typedef uint64_t hrtime_t;
extern hrtime_t gethrtime();
#endif
)

if test "$without_libmemcaced" != "true"
then
   AC_CHECK_HEADER(libmemcached/memcached.h, 
       AC_SEARCH_LIBS(memcached_create, memcached, 
           AC_DEFINE(HAVE_LIBMEMCACHED, 1, [Define if you have libmemcached/memcached.h])))
fi

if test "$without_memcaced" != "true"
then
   AC_CHECK_HEADER(memcached/protocol_binary.h, 
       AC_DEFINE(HAVE_PROTOCOL_BINARY, 1, [Define if you have memcached/protocol_binary.h]))
fi

AH_BOTTOM([#endif])
AC_CONFIG_FILES(Makefile)
AC_OUTPUT
