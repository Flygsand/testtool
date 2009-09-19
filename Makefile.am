EXTRA_DIST = autogen.sh

bin_PROGRAMS = testtool

testtool_SOURCES = main.c

# noinst_HEADERS =

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in $(srcdir)/configure $(srcdir)/stamp-h.in $(srcdir)/aclocal.m4 $(srcdir)/config.h.in $(srcdir)/config.h.in~

maintainer-clean-local:
	-rm -rf $(srcdir)/ac

distclean-local:
	-rm -rf autom4te.cache
