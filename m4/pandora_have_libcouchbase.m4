dnl  Copyright (C) 2011 Couchbase, Inc
dnl This file is free software; Couchbase, Inc
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

AC_DEFUN([_PANDORA_SEARCH_LIBCOUCHBASE],[
  AC_REQUIRE([AC_LIB_PREFIX])

  dnl --------------------------------------------------------------------
  dnl  Check for libcouchbase
  dnl --------------------------------------------------------------------

  AC_ARG_ENABLE([libcouchbase],
    [AS_HELP_STRING([--disable-libcouchbase],
      [Build with libcouchbase support @<:@default=on@:>@])],
    [ac_enable_libcouchbase="$enableval"],
    [ac_enable_libcouchbase="yes"])

  AS_IF([test "x$ac_enable_libcouchbase" = "xyes"],[
    AC_LIB_HAVE_LINKFLAGS(couchbase,,[
      #include <sys/types.h>
      #include <libcouchbase/couchbase.h>
    ],[
      libcouchbase_destroy(NULL);
    ])
  ],[
    ac_cv_libcouchbase="no"
  ])

  AM_CONDITIONAL(HAVE_LIBCOUCHBASE, [test "x${ac_cv_libcouchbase}" = "xyes"])
])

AC_DEFUN([PANDORA_HAVE_LIBCOUCHBASE],[
  AC_REQUIRE([_PANDORA_SEARCH_LIBCOUCHBASE])
])

AC_DEFUN([PANDORA_REQUIRE_LIBCOUCHBASE],[
  AC_REQUIRE([PANDORA_HAVE_LIBCOUCHBASE])
  AS_IF([test x$ac_cv_libcouchbase = xno],
      AC_MSG_ERROR([libcouchbase is required for ${PACKAGE}]))
])
