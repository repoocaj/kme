#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([kme], [2.1.0], [repoocaj@gmail.com])
AC_CONFIG_SRCDIR([demo_mkd.c])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([elf], [nlist])
AC_CHECK_LIB([socket], [socket])
AC_CHECK_LIB([nsl], [gethostbyname])
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_LIB([ncurses], [initscr])

# Checks for header files.
AC_CHECK_HEADERS([libelf/nlist.h nlist.h elf.h getopt.h string.h strings.h fcntl.h stropts.h sys/ksym.h sys/ptrace.h sys/time.h sys/wait.h stdlib.h termio.h termios.h unistd.h wait.h inttypes.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_HEADER_TIME
AC_MSG_CHECKING(for long long support in compiler)

AC_CACHE_VAL(kme_cv_c_long_long,
[AC_TRY_COMPILE(, [
  extern long long foo;
  switch (foo & 2) { case 0: return 1; }
],
kme_cv_c_long_long=yes, kme_cv_c_long_long=no)])
AC_MSG_RESULT($kme_cv_c_long_long)
if test $kme_cv_c_long_long = yes; then
  AC_DEFINE(CC_HAS_LONG_LONG, 1, [Description])
fi

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([alarm gethostbyname getksym gettimeofday isascii memset munmap nlist ptrace select socket strchr strstr strtoul])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
