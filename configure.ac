#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([tinehttpd], [1.0], [2360537343@qq.com])
AC_CONFIG_SRCDIR([src/httpd/httpd.cc])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE(tinehttpd, 1.0)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lc':
AC_CHECK_LIB([c], [main])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netinet/in.h stdlib.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_PID_T
AC_CHECK_HEADER_STDBOOL

# Checks for library functions.
AC_FUNC_FORK
AC_CHECK_FUNCS([dup2 inet_ntoa memset putenv socket])

AC_OUTPUT([Makefile
	   src/htdocs/Makefile
	   src/httpd/Makefile])
