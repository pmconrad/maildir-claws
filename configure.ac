AC_INIT(src/plugin.c)
AC_CONFIG_AUX_DIR(config)
AM_MAINTAINER_MODE

PACKAGE=maildir

dnl plugin version
MAJOR_VERSION=0
MINOR_VERSION=24
MICRO_VERSION=4
EXTRA_VERSION=18

CFLAGS="$CFLAGS -Wall"
if test $USE_MAINTAINER_MODE = yes; then
	CFLAGS="$CFLAGS -DGTK_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DG_DISABLE_DEPRECATED"
fi

if test \( $EXTRA_VERSION -eq 0 \); then
    if test \( $MICRO_VERSION -eq 0 \); then
        VERSION=${MAJOR_VERSION}.${MINOR_VERSION} 
    else
		VERSION=${MAJOR_VERSION}.${MINOR_VERSION}.${MICRO_VERSION}
    fi
else
    if test \( $MICRO_VERSION -eq 0 \); then
        VERSION=${MAJOR_VERSION}.${MINOR_VERSION}cvs${EXTRA_VERSION}
    else
		VERSION=${MAJOR_VERSION}.${MINOR_VERSION}.${MICRO_VERSION}cvs${EXTRA_VERSION}
    fi
fi

AM_INIT_AUTOMAKE($PACKAGE, $VERSION, no-define)
AM_CONFIG_HEADER(pluginconfig.h)
AC_DEFINE_UNQUOTED(PLUGINVERSION, "$VERSION", [plugin version])

AC_PROG_CC
AC_ISC_POSIX
AM_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_CPP
AM_PROG_LIBTOOL
AC_SYS_LARGEFILE


#
# Find pkg-config
#
AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
if test x$PKG_CONFIG = xno ; then
  AC_MSG_ERROR([*** pkg-config not found. See http://www.freedesktop.org/software/pkgconfig/])
fi

#
# Check for claws-mail
#
PKG_CHECK_MODULES(CLAWS_MAIL, claws-mail >= 3.7.6.53)
if test -z $prefix || test "${prefix}" = "NONE" ; then
  prefix=$( $PKG_CONFIG --variable=prefix claws-mail )
  CLAWS_MAIL_PLUGINDIR=$( $PKG_CONFIG --variable=plugindir claws-mail )
else
  CLAWS_MAIL_PLUGINDIR='${libdir}/claws-mail/plugins'
fi
AC_SUBST(CLAWS_MAIL_CFLAGS)
AC_SUBST(CLAWS_MAIL_LIBS)
AC_SUBST(CLAWS_MAIL_PLUGINDIR)

AM_PATH_GLIB_2_0(2.6.0,,
        AC_MSG_ERROR(Test for GLIB failed. See the file 'INSTALL' for help.),
        gthread)

AM_PATH_GTK_2_0(2.6.0,,
	AC_MSG_ERROR(Test for GTK failed. See the file 'INSTALL' for help.))

dnl Berkeley DB

DBVERS=0
DBLINKED=0
DBLIB=""

enable_db="yes"
dnl Check for a presence of Berkeley DB header
if test "x$enable_db" != "xyes"; then
   AC_MSG_WARN(cache support disabled)
else
   AC_CHECK_HEADER(db.h, [DB_HEADER=1], [DB_HEADER=0])
fi

if test "x$DB_HEADER" = "x1"; then
dnl Check for version of Berkeley DB
  AC_MSG_CHECKING([version of Berkeley DB])

dnl check for version 3 and above
  AC_TRY_COMPILE(
    [#include <db.h>],
    [#if DB_VERSION_MAJOR < 3
    #error DB version 3 or above needed
    #endif],
    [DBVERS=3]
    [])

dnl check for version 2
  if test "x$DBVERS" = "x0"; then
     AC_TRY_COMPILE([#include <db.h>],
       [#if DB_VERSION_MAJOR != 2
       #error DB version 2 needed
       #endif],
       [DBVERS=2]
       [])
  fi

  if test "x$DBVERS" = "x0"; then
dnl assume version 1
    DBVERS=1
  fi

dnl test linkage with Berkeley DB

dnl Look for db3 or superior with db_create call
  case "$DBVERS" in
    3)
    AC_MSG_RESULT([version 3.x or above])
    for lib in db-4.4 db-4.3 db-4.2 db-4.1 db-4.0 db-4 db4 db-3.2 db-3 db3 db; do
      if test "x$DBLINKED" = "x0"; then
dnl        AC_CHECK_LIB($lib, db_create, [DBLIB="-l$lib"; DBLINKED=1], [])
dnl installations of libdb4 function names are defined in db.h
dnl to other symbols
        AC_MSG_CHECKING([for db_create() in -l$lib])
	LIBS=-l$lib
	AC_TRY_LINK(
	   [#include <db.h>],
	   [db_create(0,0,0)],
	   [DBLIB="-l$lib"; DBLINKED=1; AC_MSG_RESULT([found])],
	   [AC_MSG_RESULT([not found])])
      fi
    done
    LIBS=""
    ;;

dnl Look for db2 with db_open call
    2)
    AC_MSG_RESULT([version 2.x])
    for lib in db2 db; do
      if test "x$DBLINKED" = "x0"; then 
        AC_CHECK_LIB($lib, db_open, [DBLIB="-l$lib"; DBLINKED=1], [])
      fi
    done
    ;;

    *)
dnl Look for db1 with dbopen call in -ldb or in libc (bsds)
    AC_MSG_RESULT([version 1.x])
    AC_CHECK_FUNC(dbopen, [DBLINKED=1], [])
    for lib in db-1 db1 db; do
      if test "x$DBLINKED" = "x0"; then
        AC_CHECK_LIB($lib, dbopen, [DBLIB="-l$lib"; DBLINKED=1], [])
      fi
    done
    ;;
dnl fi
  esac
fi

if test "x$DBLINKED" = "x0"; then
  DBVERS=0
  if test "x$enable_db" = "xyes"; then
    AC_MSG_ERROR(libdb missing.)
  fi
fi

AC_DEFINE_UNQUOTED(DBVERS, $DBVERS,
  [Define to detected Berkeley DB major version number])
AC_SUBST(DBLIB)

AC_SUBST(VERSION)
AC_SUBST(PLUGINVERSION)
AC_SUBST(MAJOR_VERSION)
AC_SUBST(MINOR_VERSION)
AC_SUBST(MICRO_VERSION)
AC_SUBST(EXTRA_VERSION)

AC_OUTPUT([
Makefile
src/Makefile
config/Makefile
doc/Makefile
])
