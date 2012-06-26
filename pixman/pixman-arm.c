/*
 * Copyright © 2000 SuSE, Inc.
 * Copyright © 2007 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of SuSE not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  SuSE makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * SuSE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL SuSE
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pixman-private.h"

#if defined(USE_ARM_SIMD) || defined(USE_ARM_NEON) || defined(USE_ARM_IWMMXT)

#include <string.h>
#include <stdlib.h>

#if defined(USE_ARM_SIMD) && defined(_MSC_VER)
/* Needed for EXCEPTION_ILLEGAL_INSTRUCTION */
#include <windows.h>
#endif

#if defined(__APPLE__)
#include "TargetConditionals.h"
#endif

#if defined(_MSC_VER)

#if defined(USE_ARM_SIMD)
extern int pixman_msvc_try_arm_simd_op ();

pixman_bool_t
pixman_have_arm_simd (void)
{
    static pixman_bool_t initialized = FALSE;
    static pixman_bool_t have_arm_simd = FALSE;

    if (!initialized)
    {
	__try {
	    pixman_msvc_try_arm_simd_op ();
	    have_arm_simd = TRUE;
	} __except (GetExceptionCode () == EXCEPTION_ILLEGAL_INSTRUCTION) {
	    have_arm_simd = FALSE;
	}
	initialized = TRUE;
    }

    return have_arm_simd;
}

#endif /* USE_ARM_SIMD */

#if defined(USE_ARM_NEON)
extern int pixman_msvc_try_arm_neon_op ();

pixman_bool_t
pixman_have_arm_neon (void)
{
    static pixman_bool_t initialized = FALSE;
    static pixman_bool_t have_arm_neon = FALSE;

    if (!initialized)
    {
	__try
	{
	    pixman_msvc_try_arm_neon_op ();
	    have_arm_neon = TRUE;
	}
	__except (GetExceptionCode () == EXCEPTION_ILLEGAL_INSTRUCTION)
	{
	    have_arm_neon = FALSE;
	}
	initialized = TRUE;
    }

    return have_arm_neon;
}

#endif /* USE_ARM_NEON */

#elif (defined (__APPLE__) && defined(TARGET_OS_IPHONE)) /* iOS (iPhone/iPad/iPod touch) */

/* Detection of ARM NEON on iOS is fairly simple because iOS binaries
 * contain separate executable images for each processor architecture.
 * So all we have to do is detect the armv7 architecture build. The
 * operating system automatically runs the armv7 binary for armv7 devices
 * and the armv6 binary for armv6 devices.
 */

pixman_bool_t
pixman_have_arm_simd (void)
{
#if defined(USE_ARM_SIMD)
    return TRUE;
#else
    return FALSE;
#endif
}

pixman_bool_t
pixman_have_arm_neon (void)
{
#if defined(USE_ARM_NEON) && defined(__ARM_NEON__)
    /* This is an armv7 cpu build */
    return TRUE;
#else
    /* This is an armv6 cpu build */
    return FALSE;
#endif
}

pixman_bool_t
pixman_have_arm_iwmmxt (void)
{
#if defined(USE_ARM_IWMMXT)
    return FALSE;
#else
    return FALSE;
#endif
}

#elif defined (__linux__) || defined(__ANDROID__) || defined(ANDROID) /* linux ELF or ANDROID */

static pixman_bool_t arm_has_v7 = FALSE;
static pixman_bool_t arm_has_v6 = FALSE;
static pixman_bool_t arm_has_vfp = FALSE;
static pixman_bool_t arm_has_neon = FALSE;
static pixman_bool_t arm_has_iwmmxt = FALSE;
static pixman_bool_t arm_tests_initialized = FALSE;

#if defined(__ANDROID__) || defined(ANDROID) /* Android device support */

#include <cpu-features.h>

static void
pixman_arm_read_auxv_or_cpu_features ()
{
    AndroidCpuFamily cpu_family;
    uint64_t cpu_features;

    cpu_family = android_getCpuFamily();
    cpu_features = android_getCpuFeatures();

    if (cpu_family == ANDROID_CPU_FAMILY_ARM)
    {
	if (cpu_features & ANDROID_CPU_ARM_FEATURE_ARMv7)
	    arm_has_v7 = TRUE;
	
	if (cpu_features & ANDROID_CPU_ARM_FEATURE_VFPv3)
	    arm_has_vfp = TRUE;
	
	if (cpu_features & ANDROID_CPU_ARM_FEATURE_NEON)
	    arm_has_neon = TRUE;
    }

    arm_tests_initialized = TRUE;
}

#elif defined (__linux__) /* linux ELF */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <elf.h>

static void
pixman_arm_read_auxv_or_cpu_features ()
{
    int fd;
    Elf32_auxv_t aux;

    fd = open ("/proc/self/auxv", O_RDONLY);
    if (fd >= 0)
    {
	while (read (fd, &aux, sizeof(Elf32_auxv_t)) == sizeof(Elf32_auxv_t))
	{
	    if (aux.a_type == AT_HWCAP)
	    {
		uint32_t hwcap = aux.a_un.a_val;
		/* hardcode these values to avoid depending on specific
		 * versions of the hwcap header, e.g. HWCAP_NEON
		 */
		arm_has_vfp = (hwcap & 64) != 0;
		arm_has_iwmmxt = (hwcap & 512) != 0;
		/* this flag is only present on kernel 2.6.29 */
		arm_has_neon = (hwcap & 4096) != 0;
	    }
	    else if (aux.a_type == AT_PLATFORM)
	    {
		const char *plat = (const char*) aux.a_un.a_val;
		if (strncmp (plat, "v7l", 3) == 0)
		{
		    arm_has_v7 = TRUE;
		    arm_has_v6 = TRUE;
		}
		else if (strncmp (plat, "v6l", 3) == 0)
		{
		    arm_has_v6 = TRUE;
		}
	    }
	}
	close (fd);
    }

    arm_tests_initialized = TRUE;
}

#endif /* Linux elf */

#if defined(USE_ARM_SIMD)
pixman_bool_t
pixman_have_arm_simd (void)
{
    if (!arm_tests_initialized)
	pixman_arm_read_auxv_or_cpu_features ();

    return arm_has_v6;
}

#endif /* USE_ARM_SIMD */

#if defined(USE_ARM_NEON)
pixman_bool_t
pixman_have_arm_neon (void)
{
    if (!arm_tests_initialized)
	pixman_arm_read_auxv_or_cpu_features ();

    return arm_has_neon;
}

#endif /* USE_ARM_NEON */

#if defined(USE_ARM_IWMMXT)
pixman_bool_t
pixman_have_arm_iwmmxt (void)
{
    if (!arm_tests_initialized)
	pixman_arm_read_auxv_or_cpu_features ();

    return arm_has_iwmmxt;
}

#endif /* USE_ARM_IWMMXT */

#else /* !_MSC_VER && !Linux elf && !Android */

#define pixman_have_arm_simd() FALSE
#define pixman_have_arm_neon() FALSE
#define pixman_have_arm_iwmmxt() FALSE

#endif

#endif /* USE_ARM_SIMD || USE_ARM_NEON || USE_ARM_IWMMXT */

pixman_implementation_t *
_pixman_arm_get_implementations (pixman_implementation_t *imp)
{
#ifdef USE_ARM_SIMD
    if (!_pixman_disabled ("arm-simd") && pixman_have_arm_simd ())
	imp = _pixman_implementation_create_arm_simd (imp);
#endif

#ifdef USE_ARM_IWMMXT
    if (!_pixman_disabled ("arm-iwmmxt") && pixman_have_arm_iwmmxt ())
	imp = _pixman_implementation_create_mmx (imp);
#endif

#ifdef USE_ARM_NEON
    if (!_pixman_disabled ("arm-neon") && pixman_have_arm_neon ())
	imp = _pixman_implementation_create_arm_neon (imp);
#endif

    return imp;
}

