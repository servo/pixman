/*
 * Copyright © 2009 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Red Hat not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Red Hat makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */
#include <config.h>
#include "pixman-private.h"

static void
combine_32 (pixman_implementation_t *imp, pixman_op_t op,
	    uint32_t *dest, const uint32_t *src, const uint32_t *mask,
	    int width)
{
    CombineFunc32 f = pixman_composeFunctions.combineU[op];

    f (dest, src, mask, width);
}

static void
combine_64 (pixman_implementation_t *imp, pixman_op_t op,
	    uint64_t *dest, const uint64_t *src, const uint64_t *mask,
	    int width)
{
    CombineFunc64 f = pixman_composeFunctions64.combineU[op];

    f (dest, src, mask, width);
}

static void
combine_32_ca (pixman_implementation_t *imp, pixman_op_t op,
	       uint32_t *dest, const uint32_t *src, const uint32_t *mask,
	       int width)
{
    CombineFunc32 f = pixman_composeFunctions.combineC[op];

    f (dest, src, mask, width);
}

static void
combine_64_ca (pixman_implementation_t *imp, pixman_op_t op,
	    uint64_t *dest, const uint64_t *src, const uint64_t *mask,
	    int width)
{
    CombineFunc64 f = pixman_composeFunctions64.combineC[op];

    f (dest, src, mask, width);
}

pixman_implementation_t *
_pixman_implementation_create_general (pixman_implementation_t *toplevel)
{
    pixman_implementation_t *imp = _pixman_implementation_create (toplevel, NULL);
    int i;

    for (i = 0; i < PIXMAN_OP_LAST; ++i)
    {
	imp->combine_32[i] = combine_32;
	imp->combine_64[i] = combine_64;
	imp->combine_32_ca[i] = combine_32_ca;
	imp->combine_64_ca[i] = combine_64_ca;
    }

    return imp;
}
