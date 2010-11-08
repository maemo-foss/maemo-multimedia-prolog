pkglib_LTLIBRARIES = libohm_rule_engine.la
pkglibdir          = @OHM_PLUGIN_DIR@
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = rule_engine.ini

#noinst_PROGRAMS    = test
#
#test_CFLAGS  = @PROLOG_CFLAGS@ \
#	        -DDEFAULT_PROLOG_DIR=\"${pkglibdir}/prolog\"
#test_SOURCES = test.c
#test_LDADD   = @PROLOG_LIBS@ ../src/libprolog.la

libohm_rule_engine_la_SOURCES = rule-engine.c
libohm_rule_engine_la_CFLAGS  = @OHM_PLUGIN_CFLAGS@ @PROLOG_CFLAGS@ \
			        -DDEFAULT_PROLOG_DIR=\"${pkglibdir}/prolog\"
libohm_rule_engine_la_LIBADD  = @OHM_PLUGIN_LIBS@ @PROLOG_LIBS@ \
                                ../src/libprolog.la
libohm_rule_engine_la_LDFLAGS = -module -avoid-version

INCLUDES = -I$(top_builddir)/include

MAINTAINERCLEANFILES = Makefile.in

clean-local:
	rm -f *~
