AC_PREREQ(2.59)
m4_include([m4/version.m4])
AC_INIT(memcachetest, VERSION_NUMBER, ingenthr@cep.net)
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_DISABLE_STATIC

PANDORA_CANONICAL_TARGET(less-warnings, warnings-always-on)
AM_CPPFLAGS="$AM_CPPFLAGS $NO_WERROR"
AM_CFLAGS="$AM_CFLAGS $NO_WERROR"
PANDORA_HAVE_LIBMEMCACHED
PANDORA_HAVE_LIBVBUCKET

AH_TOP([
#ifndef CONFIG_H
#define CONFIG_H

#ifdef _SYS_FEATURE_TESTS_H
#error "You should include config.h as your first include file"
#endif
])

AC_SEARCH_LIBS(socket, socket)
AC_SEARCH_LIBS(gethostbyname, nsl)
AC_SEARCH_LIBS(log, m)
AC_SEARCH_LIBS(sqrt, m)
AC_SEARCH_LIBS(pthread_create, pthread)
AC_SEARCH_LIBS(clock_gettime, rt)

AC_CHECK_HEADERS_ONCE(mach/mach_time.h)
AC_CHECK_HEADERS_ONCE(memcached/protocol_binary.h)
AC_CHECK_FUNCS_ONCE(gethrtime clock_gettime gettimeofday)

AH_BOTTOM(
#ifndef HAVE_GETHRTIME
#include <stdint.h>
typedef uint64_t hrtime_t;
extern hrtime_t gethrtime(void);
#endif
#endif
)

dnl ----------------------------------------------------------------------------

AC_CONFIG_FILES(Makefile)
AC_OUTPUT
echo "---"
echo "Configuration summary for $PACKAGE_NAME version $VERSION"
echo ""
echo "   * Installation prefix:        $prefix"
echo "   * System type:                $host_vendor-$host_os"
echo "   * Host CPU:                   $host_cpu"
echo "   * C Compiler:                 $CC_VERSION"
echo "   * C++ Compiler:               $CXX_VERSION"
echo "   * Assertions enabled:         $ac_cv_assert"
echo "   * Debug enabled:              $with_debug"
echo "   * Warnings as failure:        $ac_cv_warnings_as_errors"
echo "   * Support for binary protocol $ac_cv_header_memcached_protocol_binary_h"
echo "   * Support for libmemcached    $ac_cv_libmemcached"
echo "   * Support for libvbucket      $ac_cv_libvbucket"
echo ""
echo "---"
