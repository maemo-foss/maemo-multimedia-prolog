EXTRA_DIST = autogen.sh build-aux/git-version-gen
SUBDIRS    = src include extensions ohm-plugin tests

if PD_SUPPORT
SUBDIRS += doc
endif

pkgconfigdir   = ${libdir}/pkgconfig
pkgconfig_DATA = libprolog.pc

MAINTAINERCLEANFILES = Makefile.in

# see build-aux/git-version-gen
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version


clean-local:
	rm -f *~
