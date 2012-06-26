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

#include <string.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include "TargetConditionals.h"
#endif

#include "pixman-private.h"

#ifdef USE_VMX

/* The CPU detection code needs to be in a file not compiled with
 * "-maltivec -mabi=altivec", as gcc would try to save vector register
 * across function calls causing SIGILL on cpus without Altivec/vmx.
 */
static pixman_bool_t initialized = FALSE;
static volatile pixman_bool_t have_vmx = TRUE;

#ifdef __APPLE__
#include <sys/sysctl.h>

static pixman_bool_t
pixman_have_vmx (void)
{
    if (!initialized)
    {
	size_t length = sizeof(have_vmx);
	int error =
	    sysctlbyname ("hw.optional.altivec", &have_vmx, &length, NULL, 0);

	if (error)
	    have_vmx = FALSE;

	initialized = TRUE;
    }
    return have_vmx;
}

#elif defined (__OpenBSD__)
#include <sys/param.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>

static pixman_bool_t
pixman_have_vmx (void)
{
    if (!initialized)
    {
	int mib[2] = { CTL_MACHDEP, CPU_ALTIVEC };
	size_t length = sizeof(have_vmx);
	int error =
	    sysctl (mib, 2, &have_vmx, &length, NULL, 0);

	if (error != 0)
	    have_vmx = FALSE;

	initialized = TRUE;
    }
    return have_vmx;
}

#elif defined (__linux__)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/auxvec.h>
#include <asm/cputable.h>

static pixman_bool_t
pixman_have_vmx (void)
{
    if (!initialized)
    {
	char fname[64];
	unsigned long buf[64];
	ssize_t count = 0;
	pid_t pid;
	int fd, i;

	pid = getpid ();
	snprintf (fname, sizeof(fname) - 1, "/proc/%d/auxv", pid);

	fd = open (fname, O_RDONLY);
	if (fd >= 0)
	{
	    for (i = 0; i <= (count / sizeof(unsigned long)); i += 2)
	    {
		/* Read more if buf is empty... */
		if (i == (count / sizeof(unsigned long)))
		{
		    count = read (fd, buf, sizeof(buf));
		    if (count <= 0)
			break;
		    i = 0;
		}

		if (buf[i] == AT_HWCAP)
		{
		    have_vmx = !!(buf[i + 1] & PPC_FEATURE_HAS_ALTIVEC);
		    initialized = TRUE;
		    break;
		}
		else if (buf[i] == AT_NULL)
		{
		    break;
		}
	    }
	    close (fd);
	}
    }
    if (!initialized)
    {
	/* Something went wrong. Assume 'no' rather than playing
	   fragile tricks with catching SIGILL. */
	have_vmx = FALSE;
	initialized = TRUE;
    }

    return have_vmx;
}

#else /* !__APPLE__ && !__OpenBSD__ && !__linux__ */
#include <signal.h>
#include <setjmp.h>

static jmp_buf jump_env;

static void
vmx_test (int        sig,
	  siginfo_t *si,
	  void *     unused)
{
    longjmp (jump_env, 1);
}

static pixman_bool_t
pixman_have_vmx (void)
{
    struct sigaction sa, osa;
    int jmp_result;

    if (!initialized)
    {
	sa.sa_flags = SA_SIGINFO;
	sigemptyset (&sa.sa_mask);
	sa.sa_sigaction = vmx_test;
	sigaction (SIGILL, &sa, &osa);
	jmp_result = setjmp (jump_env);
	if (jmp_result == 0)
	{
	    asm volatile ( "vor 0, 0, 0" );
	}
	sigaction (SIGILL, &osa, NULL);
	have_vmx = (jmp_result == 0);
	initialized = TRUE;
    }
    return have_vmx;
}

#endif /* __APPLE__ */
#endif /* USE_VMX */

#if defined(USE_MIPS_DSPR2) || defined(USE_LOONGSON_MMI)

#if defined (__linux__) /* linux ELF */

static pixman_bool_t
pixman_have_mips_feature (const char *search_string)
{
    const char *file_name = "/proc/cpuinfo";
    /* Simple detection of MIPS features at runtime for Linux.
     * It is based on /proc/cpuinfo, which reveals hardware configuration
     * to user-space applications.  According to MIPS (early 2010), no similar
     * facility is universally available on the MIPS architectures, so it's up
     * to individual OSes to provide such.
     */

    char cpuinfo_line[256];

    FILE *f = NULL;

    if ((f = fopen (file_name, "r")) == NULL)
        return FALSE;

    while (fgets (cpuinfo_line, sizeof (cpuinfo_line), f) != NULL)
    {
        if (strstr (cpuinfo_line, search_string) != NULL)
        {
            fclose (f);
            return TRUE;
        }
    }

    fclose (f);

    /* Did not find string in the proc file. */
    return FALSE;
}

#if defined(USE_MIPS_DSPR2)
pixman_bool_t
pixman_have_mips_dspr2 (void)
{
     /* Only currently available MIPS core that supports DSPr2 is 74K. */
    return pixman_have_mips_feature ("MIPS 74K");
}
#endif

#if defined(USE_LOONGSON_MMI)
pixman_bool_t
pixman_have_loongson_mmi (void)
{
    /* I really don't know if some Loongson CPUs don't have MMI. */
    return pixman_have_mips_feature ("Loongson");
}
#endif

#else /* linux ELF */

#define pixman_have_mips_dspr2() FALSE
#define pixman_have_loongson_mmi() FALSE

#endif /* linux ELF */

#endif /* USE_MIPS_DSPR2 || USE_LOONGSON_MMI */

pixman_bool_t
_pixman_disabled (const char *name)
{
    const char *env;

    if ((env = getenv ("PIXMAN_DISABLE")))
    {
	do
	{
	    const char *end;
	    int len;

	    if ((end = strchr (env, ' ')))
		len = end - env;
	    else
		len = strlen (env);

	    if (strlen (name) == len && strncmp (name, env, len) == 0)
	    {
		printf ("pixman: Disabled %s implementation\n", name);
		return TRUE;
	    }

	    env += len;
	}
	while (*env++);
    }

    return FALSE;
}

pixman_implementation_t *
_pixman_choose_implementation (void)
{
    pixman_implementation_t *imp;

    imp = _pixman_implementation_create_general();

    if (!_pixman_disabled ("fast"))
	imp = _pixman_implementation_create_fast_path (imp);

    imp = _pixman_x86_get_implementations (imp);
    imp = _pixman_arm_get_implementations (imp);
    
#ifdef USE_LOONGSON_MMI
    if (!_pixman_disabled ("loongson-mmi") && pixman_have_loongson_mmi ())
	imp = _pixman_implementation_create_mmx (imp);
#endif

#ifdef USE_MIPS_DSPR2
    if (!_pixman_disabled ("mips-dspr2") && pixman_have_mips_dspr2 ())
	imp = _pixman_implementation_create_mips_dspr2 (imp);
#endif

#ifdef USE_VMX
    if (!_pixman_disabled ("vmx") && pixman_have_vmx ())
	imp = _pixman_implementation_create_vmx (imp);
#endif

    imp = _pixman_implementation_create_noop (imp);

    return imp;
}

