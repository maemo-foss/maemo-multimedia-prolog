# This file is part of Autoconf.                       -*- Autoconf -*-

# Macro for checking GCC's visibility support.
AC_DEFUN([AC_CHECK_CCOPT_VISIBILITY],
    [AC_CACHE_CHECK([whether ${CC-cc} supports -fvisibility=hidden],
         [ac_cv_cc_visibility],
         [echo 'void f(){}' > conftest.c
          if test -z "`${CC-cc} -c -fvisibility=hidden conftest.c 2>&1`"; then
            ac_cv_cc_visibility=yes
          else
            ac_cv_cc_visibility=no
          fi])
     if test $ac_cv_cc_visibility = yes; then
       AC_DEFINE([HAVE_VISIBILITY_SUPPORT], 1,
                 [Define to 1 if your compiler supports -fvisibility=hidden.])
       CCOPT_VISIBILITY_HIDDEN="-fvisibility=hidden"
     else
       AC_DEFINE([HAVE_VISIBILITY_SUPPORT], [],
                 [Define to 1 if your compiler supports -fvisibility=hidden.])
       CCOPT_VISIBILITY_HIDDEN=""
     fi
    ])


# Macro for checking the necessary tools for policy documentation.
AC_DEFUN([DOC_SUPPORT_INIT], [

PD_DOC_SUPPORT_DEFVAL=true
PD_DOCTOOL_PATH=/usr/share/policy-doc-tools

AC_PATH_TOOL([PD_LYX], lyx)

AS_IF([test x$PD_LYX = x],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find lyx. doc-support is going to be disabled])]
)

AC_PATH_TOOL([PD_FIG2DEV], fig2dev)

AS_IF([test x$PD_FIG2DEV = x],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find fig2dev. doc-support is going to be disabled])]
)

AC_PATH_TOOL([PD_DOXYGEN], doxygen)

AS_IF([test x$PD_DOXYGEN = x],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find doxygen. doc-support is going to be disabled])]
)

AC_PATH_TOOL([PD_XSLTPROC], xsltproc)

AS_IF([test x$PD_XSLTPROC = x],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find xsltproc. doc-support is going to be disabled])]
)

AC_SUBST([PD_DOXML2DB_STY], [${PD_DOCTOOL_PATH}/xslt/doxml2db.sty])

AC_CHECK_FILE([$PD_DOXML2DB_STY], [PD_DOXML2DB_STY_OK=true])

AS_IF([test x$PD_DOXML2DB_STY_OK != xtrue],
      [PD_DOC_SUPPORT_DEFVAL=false
       AC_MSG_WARN([Can't find doxml2db.sty; doc-support disabled])])

# Macro for enabling/disabling documentation support.
AC_ARG_ENABLE(doc-support,
              [  --enable-doc-support       enable policy documentation],
              enable_doc=$enableval,enable_doc=$PD_DOC_SUPPORT_DEFVAL)
AC_MSG_CHECKING([for policy documentation support])
if test "x$enable_doc" = "xyes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi
AM_CONDITIONAL([PD_SUPPORT], [test "x$enable_doc" = "xyes"])

])