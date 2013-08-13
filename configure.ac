AC_PREREQ([2.63])
AC_INIT([slurm_showq], [0.10.0], [karl@tacc.utexas.edu])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CXX

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lslurm':
#AC_CHECK_LIB([slurm], [main])

AC_ARG_WITH(slurm,
   AS_HELP_STRING([--with-slurm], [path to the slurm package]),
   [SLURM_LDFLAGS="-L${with_slurm}/lib" SLURM_CPPFLAGS="-I${with_slurm}/include"],[])

LDFLAGS="$SLURM_LDFLAGS $LDFLAGS"
CPPFLAGS="$SLURM_CPPFLAGS $CPPFLAGS"

AC_CHECK_HEADERS([slurm/slurm.h],,AC_MSG_ERROR([cannot find SLURM header: consider using --with-slurm option (and verify that the SLURM devel headers are installed locally)]))

# Checks for header files.
AC_CHECK_HEADERS([unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_UID_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_CHECK_FUNCS([strdup strstr])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
