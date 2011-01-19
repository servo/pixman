AM_CFLAGS = @OPENMP_CFLAGS@
AM_LDFLAGS = @OPENMP_CFLAGS@ @TESTPROGS_EXTRA_LDFLAGS@

TEST_LDADD = $(top_builddir)/pixman/libpixman-1.la -lm
INCLUDES = -I$(top_srcdir)/pixman -I$(top_builddir)/pixman

TESTPROGRAMS =			\
	a1-trap-test		\
	pdf-op-test		\
	region-test		\
	region-translate-test	\
	fetch-test		\
	oob-test		\
	trap-crasher		\
	alpha-loop		\
	scaling-crash-test	\
	gradient-crash-test	\
	alphamap		\
	stress-test		\
	blitters-test		\
	scaling-test		\
	affine-test		\
	composite

a1_trap_test_LDADD = $(TEST_LDADD)

fetch_test_LDADD = $(TEST_LDADD)

trap_crasher_LDADD = $(TEST_LDADD)

oob_test_LDADD = $(TEST_LDADD)

scaling_crash_test_LDADD = $(TEST_LDADD)

region_translate_test_LDADD = $(TEST_LDADD)

pdf_op_test_LDADD = $(TEST_LDADD)
pdf_op_test_SOURCES = pdf-op-test.c utils.c utils.h

region_test_LDADD = $(TEST_LDADD)
region_test_SOURCES = region-test.c utils.c utils.h

blitters_test_LDADD = $(TEST_LDADD)
blitters_test_SOURCES = blitters-test.c utils.c utils.h

scaling_test_LDADD = $(TEST_LDADD)
scaling_test_SOURCES = scaling-test.c utils.c utils.h

affine_test_LDADD = $(TEST_LDADD)
affine_test_SOURCES = affine-test.c utils.c utils.h

alphamap_LDADD = $(TEST_LDADD)
alphamap_SOURCES = alphamap.c utils.c utils.h

alpha_loop_LDADD = $(TEST_LDADD)
alpha_loop_SOURCES = alpha-loop.c utils.c utils.h

composite_LDADD = $(TEST_LDADD)
composite_SOURCES = composite.c utils.c utils.h

gradient_crash_test_LDADD = $(TEST_LDADD)
gradient_crash_test_SOURCES = gradient-crash-test.c utils.c utils.h

stress_test_LDADD = $(TEST_LDADD)
stress_test_SOURCES = stress-test.c utils.c utils.h

# Benchmarks

BENCHMARKS =			\
	lowlevel-blt-bench

lowlevel_blt_bench_SOURCES = lowlevel-blt-bench.c utils.c utils.h
lowlevel_blt_bench_LDADD = $(TEST_LDADD)

noinst_PROGRAMS = $(TESTPROGRAMS) $(BENCHMARKS)

TESTS = $(TESTPROGRAMS)
