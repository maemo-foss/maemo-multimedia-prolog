if HAVE_CHECK
TESTS = check-libprolog
noinst_PROGRAMS = check-libprolog
else
TESTS =
endif

check_libprolog_SOURCES = check-libprolog.c           \
			  check-libprolog-init.c      \
			  check-libprolog-predicate.c \
			  check-libprolog-memleak.c
check_libprolog_CFLAGS  = -I$(top_builddir)/include \
			  @PROLOG_CFLAGS@ \
			  @CHECK_CFLAGS@
check_libprolog_LDADD   = $(top_builddir)/src/libprolog.la \
			  @PROLOG_LIBS@ \
			  @CHECK_LIBS@

