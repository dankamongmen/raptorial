AC_PREREQ([2.61])
AC_INIT([raptorial], [0.0.1-pre], [sprezzos-dev@sprezzatech.com], [raptorial], [https://www.sprezzatech.com/wiki/index.php/Raptorial])
AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([-Wall -Werror foreign dist-xz std-options subdir-objects nostdinc color-tests])
AC_CONFIG_SRCDIR([src/lib/raptorial.h])

## compiler check
CXXFLAGS="-O2 -Wall -W"
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_CC_C99
AC_C_INLINE
AC_C_RESTRICT

AX_PTHREAD


AX_PROG_XSLTPROC
if test "$XSLTPROC" = no ; then
	AC_MSG_ERROR([Cannot find xsltproc.])
fi
AX_CHECK_DOCBOOK_XSLT

AC_PROG_LIBTOOL

AC_CONFIG_MACRO_DIR([m4])

SRCDIR=${srcdir}

## output
AC_SUBST([CFLAGS])
AC_SUBST([SRCDIR])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
