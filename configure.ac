AC_PREREQ([2.61])
AC_INIT([raptorial], [0.3.2], [sprezzos-dev@sprezzatech.com], [raptorial], [https://www.sprezzatech.com/wiki/index.php/Raptorial])
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_HEADERS([src/lib/config.h:config.in])
# FIXME no love with -Werror under automake 1.11; needs 1.13...
AM_INIT_AUTOMAKE([-Wall foreign dist-xz std-options subdir-objects nostdinc color-tests])
AC_CONFIG_SRCDIR([src/lib/raptorial.h])

## compiler check
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CC_C99
AC_C_INLINE
AC_C_RESTRICT

AM_PROG_AR
AM_PROG_CC_C_O

AX_PTHREAD

AX_CHECK_ZLIB

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES(LIBBLOSSOM, libblossom >= 1.3.0, [have_libblossom=yes])
CFLAGS+=" $LIBBLOSSOM_LIBS"
LIBS+=" $LIBBLOSSOM_LIBS"

AX_PROG_XSLTPROC
if test "x$XSLTPROC" = "x" ; then
	AC_MSG_ERROR([Cannot find xsltproc.])
fi
AX_CHECK_DOCBOOK_XSLT

AC_PROG_LIBTOOL

#AC_CONFIG_MACRO_DIR([m4])

SRCDIR=${srcdir}

## output
AC_SUBST([CFLAGS])
AC_SUBST([SRCDIR])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
