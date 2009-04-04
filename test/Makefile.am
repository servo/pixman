TEST_LDADD = $(top_builddir)/pixman/libpixman-1.la
INCLUDES = -I$(top_srcdir)/pixman -I$(top_builddir)/pixman 

TESTPROGRAMS =			\
	region-test		\
	scaling-test		\
	fetch-test

fetch_test_LDADD = $(TEST_LDADD)
region_test_LDADD = $(TEST_LDADD)
scaling_test_LDADD = $(TEST_LDADD)

# GTK using test programs

if HAVE_GTK

GTK_LDADD = $(TEST_LDADD) $(GTK_LIBS)

TESTPROGRAMS +=			\
	clip-test		\
	composite-test		\
	gradient-test		\
	trap-test

noinst_PROGRAMS = $(TESTPROGRAMS)

INCLUDES += $(GTK_CFLAGS)

composite_test_LDADD = $(GTK_LDADD)
gradient_test_LDADD = $(GTK_LDADD)
clip_test_LDADD = $(GTK_LDADD)
trap_test_LDADD = $(GTK_LDADD)

endif

