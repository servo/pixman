TESTPROGRAMS = \
	composite-test		\
	gradient-test

noinst_PROGRAMS = $(TESTPROGRAMS)

INCLUDES = -I$(top_srcdir)/pixman `pkg-config --cflags gtk+-2.0`

GTKLIBS = -lgtk-x11-2.0 -lgdk-x11-2.0 -latk-1.0 -lgdk_pixbuf-2.0 -lpng12 -lm -lpangocairo-1.0 -lpango-1.0 -lcairo -lgobject-2.0 -lgmodule-2.0 -ldl -lglib-2.0

composite_test_LDADD =	$(top_builddir)/pixman/libpixman.la $(GTKLIBS)
gradient_test_LDADD = $(top_builddir)/pixman/libpixman.la $(GTKLIBS)


