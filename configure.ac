AC_INIT([mod_sqs], [1.0.0], bugs@dexo.tech)

AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIRS([m4])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
m4_pattern_allow([AC_PACKAGE_VERSION])

AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_LIBTOOL

m4_ifndef([PKG_CHECK_VAR], [
	AC_DEFUN([PKG_CHECK_VAR], [
		AC_REQUIRE([PKG_PROG_PKG_CONFIG])dnl
		AC_ARG_VAR([$1], [value of $3 for $2, overriding pkg-config])dnl

		_PKG_CONFIG([$1], [variable="][$3]["], [$2])
		AS_VAR_COPY([$1], [pkg_cv_][$1])

		AS_VAR_IF([$1], [""], [$5], [$4])dnl
	])
])

PKG_CHECK_MODULES([AWS_SDK_CORE],[aws-cpp-sdk-core], [], [AC_ERROR([please install aws-cpp-sdk-core-devel to continue.])])
CXXLAGS+=" `pkg-config aws-cpp-sdk-core --cflags`"
LDFLAGS+=" `pkg-config aws-cpp-sdk-core --libs`"

PKG_CHECK_MODULES([AWS_SDK_SQS],[aws-cpp-sdk-sqs], [], [AC_ERROR([please install aws-cpp-sdk-sqs-devel to continue.])])
CXXLAGS+=" `pkg-config aws-cpp-sdk-sqs --cflags`"
LDFLAGS+=" `pkg-config aws-cpp-sdk-sqs --libs`"

PKG_CHECK_MODULES([FREESWITCH],[freeswitch], [], [AC_ERROR([please install freeswitch-devel to continue.])])
PKG_CHECK_VAR([moddir],[freeswitch],[modulesdir])
PKG_CHECK_VAR([confdir],[freeswitch],[confdir])
AC_SUBST([moddir])
AC_SUBST([confdir])
CFLAGS+=" `pkg-config freeswitch --cflags`"
LDFLAGS+=" `pkg-config freeswitch --libs`"

AC_ARG_WITH([modver], [AS_HELP_STRING([--with-modver=VER], [Specify the module version (default: AC_PACKAGE_VERSION)])], [modver="$withval"], [modver=AC_PACKAGE_VERSION])
AC_SUBST([MOD_VERSION], [$modver])

AC_ARG_ENABLE(assertions, [AC_HELP_STRING([--disable-assertions],[build without assert function])],[CFLAGS+=" -DNDEBUG"; AM_CONDITIONAL([NO_ASSERT], [true])],[AM_CONDITIONAL([NO_ASSERT], [false])])
ac_configure_args="${ac_configure_args} --disable-assertions"

AC_CONFIG_FILES([Makefile])

AC_OUTPUT

