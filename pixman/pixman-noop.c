/* -*- Mode: c; c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t; -*- */
/*
 * Copyright © 2011 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>
#include <stdlib.h>
#include "pixman-private.h"
#include "pixman-combine32.h"
#include "pixman-fast-path.h"

static void
noop_composite (pixman_implementation_t *imp,
		pixman_op_t              op,
		pixman_image_t *         src,
		pixman_image_t *         mask,
		pixman_image_t *         dest,
		int32_t                  src_x,
		int32_t                  src_y,
		int32_t                  mask_x,
		int32_t                  mask_y,
		int32_t                  dest_x,
		int32_t                  dest_y,
		int32_t                  width,
		int32_t                  height)
{
    return;
}

static const pixman_fast_path_t noop_fast_paths[] =
{
    { PIXMAN_OP_DST, PIXMAN_any, 0, PIXMAN_any, 0, PIXMAN_any, 0, noop_composite },
    { PIXMAN_OP_NONE },
};

pixman_implementation_t *
_pixman_implementation_create_noop (pixman_implementation_t *fallback)
{
    pixman_implementation_t *imp =
	_pixman_implementation_create (fallback, noop_fast_paths);

    return imp;
}
