lib_LTLIBRARIES = libpixman.la

libpixman_la_LIBADD = @DEP_LIBS@ -lm

libpixman_la_SOURCES =    \
	 pixman.h	  \
	 pixman-region.c  \
	 pixman-private.h \
	 pixman-image.c   \
	 pixman-compose.c \
	 pixman-pict.c	  \
	 pixman-utils.c


noinst_LTLIBRARIES = libpixman-mmx.la
libpixman_mmx_la_SOURCES = \
	pixman-mmx.c \
	pixman-mmx.h
libpixman_mmx_la_CFLAGS = $(DEP_CFLAGS) -mmmx
libpixman_mmx_la_LIBADD = $(DEP_LIBS)
libpixman_la_LIBADD += libpixman-mmx.la

libpixmanincludedir = $(includedir)/pixman
libpixmaninclude_HEADERS = pixman.h
