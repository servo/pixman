/*
 * Copyright © 2009 Red Hat, Inc.
 * Copyright © 2000 SuSE, Inc.
 * Copyright © 2007 Red Hat, Inc.
 * Copyright © 2000 Keith Packard, member of The XFree86 Project, Inc.
 *             2005 Lars Knoll & Zack Rusin, Trolltech
 *             2008 Aaron Plattner, NVIDIA Corporation
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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pixman-private.h"
#include "pixman-vmx.h"
#include "pixman-arm-simd.h"
#include "pixman-combine32.h"
#include "pixman-private.h"


static void
general_combine_32 (pixman_implementation_t *imp, pixman_op_t op,
		    uint32_t *dest, const uint32_t *src, const uint32_t *mask,
		    int width)
{
    CombineFunc32 f = pixman_composeFunctions.combineU[op];

    f (dest, src, mask, width);
}

static void
general_combine_32_ca (pixman_implementation_t *imp, pixman_op_t op,
		       uint32_t *dest, const uint32_t *src, const uint32_t *mask,
		       int width)
{
    CombineFunc32 f = pixman_composeFunctions.combineC[op];

    f (dest, src, mask, width);
}

static void
general_combine_64 (pixman_implementation_t *imp, pixman_op_t op,
		    uint64_t *dest, const uint64_t *src, const uint64_t *mask,
		    int width)
{
    CombineFunc64 f = pixman_composeFunctions64.combineU[op];

    f (dest, src, mask, width);
}

static void
general_combine_64_ca (pixman_implementation_t *imp, pixman_op_t op,
		       uint64_t *dest, const uint64_t *src, const uint64_t *mask,
		       int width)
{
    CombineFunc64 f = pixman_composeFunctions64.combineC[op];

    f (dest, src, mask, width);
}

static void
pixman_composite_rect_general_internal (pixman_implementation_t *imp,
					const FbComposeData *data,
					void *src_buffer, void *mask_buffer, 
					void *dest_buffer, const int wide)
{
    int i;
    scanStoreProc store;
    scanFetchProc fetchSrc = NULL, fetchMask = NULL, fetchDest = NULL;
    uint32_t *bits;
    int32_t stride;
    source_pict_class_t srcClass, maskClass;
    pixman_bool_t component_alpha;

    srcClass = _pixman_image_classify (data->src,
				       data->xSrc, data->ySrc,
				       data->width, data->height);

    maskClass = SOURCE_IMAGE_CLASS_UNKNOWN;
    if (data->mask)
    {
	maskClass = _pixman_image_classify (data->mask,
					    data->xSrc, data->ySrc,
					    data->width, data->height);
    }
    
    if (data->op == PIXMAN_OP_CLEAR)
        fetchSrc = NULL;
    else if (wide)
	fetchSrc = _pixman_image_get_scanline_64;
    else
	fetchSrc = _pixman_image_get_scanline_32;

    if (!data->mask || data->op == PIXMAN_OP_CLEAR)
	fetchMask = NULL;
    else if (wide)
	fetchMask = _pixman_image_get_scanline_64;
    else
	fetchMask = _pixman_image_get_scanline_32;

    if (data->op == PIXMAN_OP_CLEAR || data->op == PIXMAN_OP_SRC)
	fetchDest = NULL;
    else if (wide)
	fetchDest = _pixman_image_get_scanline_64;
    else
	fetchDest = _pixman_image_get_scanline_32;

    if (wide)
	store = _pixman_image_store_scanline_64;
    else
	store = _pixman_image_store_scanline_32;

    // Skip the store step and composite directly into the
    // destination if the output format of the compose func matches
    // the destination format.
    if (!wide &&
	!data->dest->common.alpha_map &&
	!data->dest->common.write_func && 
	(data->op == PIXMAN_OP_ADD || data->op == PIXMAN_OP_OVER) &&
	(data->dest->bits.format == PIXMAN_a8r8g8b8 ||
	 data->dest->bits.format == PIXMAN_x8r8g8b8))
    {
	store = NULL;
    }

    if (!store)
    {
	bits = data->dest->bits.bits;
	stride = data->dest->bits.rowstride;
    }
    else
    {
	bits = NULL;
	stride = 0;
    }

    component_alpha =
	fetchSrc		   &&
	fetchMask		   &&
	data->mask		   &&
	data->mask->common.type == BITS &&
	data->mask->common.component_alpha &&
	PIXMAN_FORMAT_RGB (data->mask->bits.format);

    {
	pixman_combine_32_func_t compose;

	if (wide)
	{
	    if (component_alpha)
		compose = (pixman_combine_32_func_t)_pixman_implementation_combine_64_ca;
	    else
		compose = (pixman_combine_32_func_t)_pixman_implementation_combine_64;
	}
	else
	{
	    if (component_alpha)
		compose = _pixman_implementation_combine_32_ca;
	    else
		compose = _pixman_implementation_combine_32;
	}

	if (!compose)
	    return;

	if (!fetchMask)
	    mask_buffer = NULL;
	
	for (i = 0; i < data->height; ++i)
	{
	    /* fill first half of scanline with source */
	    if (fetchSrc)
	    {
		if (fetchMask)
		{
		    /* fetch mask before source so that fetching of
		       source can be optimized */
		    fetchMask (data->mask, data->xMask, data->yMask + i,
			       data->width, mask_buffer, 0, 0);

		    if (maskClass == SOURCE_IMAGE_CLASS_HORIZONTAL)
			fetchMask = NULL;
		}

		if (srcClass == SOURCE_IMAGE_CLASS_HORIZONTAL)
		{
		    fetchSrc (data->src, data->xSrc, data->ySrc + i,
			      data->width, src_buffer, 0, 0);
		    fetchSrc = NULL;
		}
		else
		{
		    fetchSrc (data->src, data->xSrc, data->ySrc + i,
			      data->width, src_buffer, mask_buffer,
			      0xffffffff);
		}
	    }
	    else if (fetchMask)
	    {
		fetchMask (data->mask, data->xMask, data->yMask + i,
			   data->width, mask_buffer, 0, 0);
	    }

	    if (store)
	    {
		/* fill dest into second half of scanline */
		if (fetchDest)
		    fetchDest (data->dest, data->xDest, data->yDest + i,
			       data->width, dest_buffer, 0, 0);

		/* blend */
		compose (imp, data->op, dest_buffer, src_buffer, mask_buffer, data->width);

		/* write back */
		store (&(data->dest->bits), data->xDest, data->yDest + i, data->width,
		       dest_buffer);
	    }
	    else
	    {
		/* blend */
		compose (imp, data->op, bits + (data->yDest + i) * stride +
			 data->xDest,
			 src_buffer, mask_buffer, data->width);
	    }
	}
    }
}

#define SCANLINE_BUFFER_LENGTH 8192

static void
general_composite_rect (pixman_implementation_t *imp,
			const FbComposeData *data)
{
    uint8_t stack_scanline_buffer[SCANLINE_BUFFER_LENGTH * 3];
    const pixman_format_code_t srcFormat =
	data->src->type == BITS ? data->src->bits.format : 0;
    const pixman_format_code_t maskFormat =
	data->mask && data->mask->type == BITS ? data->mask->bits.format : 0;
    const pixman_format_code_t destFormat = data->dest->type == BITS ? data->dest->bits.format : 0;
    const int srcWide = PIXMAN_FORMAT_16BPC(srcFormat);
    const int maskWide = data->mask && PIXMAN_FORMAT_16BPC(maskFormat);
    const int destWide = PIXMAN_FORMAT_16BPC(destFormat);
    const int wide = srcWide || maskWide || destWide;
    const int Bpp = wide ? 8 : 4;
    uint8_t *scanline_buffer = stack_scanline_buffer;
    uint8_t *src_buffer, *mask_buffer, *dest_buffer;
    
    if (data->width * Bpp > SCANLINE_BUFFER_LENGTH)
    {
	scanline_buffer = pixman_malloc_abc (data->width, 3, Bpp);

	if (!scanline_buffer)
	    return;
    }

    src_buffer = scanline_buffer;
    mask_buffer = src_buffer + data->width * Bpp;
    dest_buffer = mask_buffer + data->width * Bpp;

    pixman_composite_rect_general_internal (imp, data, src_buffer,
					    mask_buffer, dest_buffer,
					    wide);

    if (scanline_buffer != stack_scanline_buffer)
	free (scanline_buffer);
}

static void
pixman_image_composite_rect  (pixman_implementation_t *imp,
			      pixman_op_t                   op,
			      pixman_image_t               *src,
			      pixman_image_t               *mask,
			      pixman_image_t               *dest,
			      int32_t                       src_x,
			      int32_t                       src_y,
			      int32_t                       mask_x,
			      int32_t                       mask_y,
			      int32_t                       dest_x,
			      int32_t                       dest_y,
			      int32_t                      width,
			      int32_t                      height)
{
    FbComposeData compose_data;

    return_if_fail (src != NULL);
    return_if_fail (dest != NULL);

    compose_data.op = op;
    compose_data.src = src;
    compose_data.mask = mask;
    compose_data.dest = dest;
    compose_data.xSrc = src_x;
    compose_data.ySrc = src_y;
    compose_data.xMask = mask_x;
    compose_data.yMask = mask_y;
    compose_data.xDest = dest_x;
    compose_data.yDest = dest_y;
    compose_data.width = width;
    compose_data.height = height;

    general_composite_rect (imp, &compose_data);
}

static void
general_composite (pixman_implementation_t *	imp,
		   pixman_op_t			op,
		   pixman_image_t *		src,
		   pixman_image_t *		mask,
		   pixman_image_t *		dest,
		   int32_t			src_x,
		   int32_t			src_y,
		   int32_t			mask_x,
		   int32_t			mask_y,
		   int32_t			dest_x,
		   int32_t			dest_y,
		   int32_t			width,
		   int32_t			height)
{
    pixman_bool_t srcRepeat = src->type == BITS && src->common.repeat == PIXMAN_REPEAT_NORMAL;
    pixman_bool_t maskRepeat = FALSE;
    pixman_bool_t srcTransform = src->common.transform != NULL;
    pixman_bool_t maskTransform = FALSE;

#ifdef USE_VMX
    fbComposeSetupVMX();
#endif

    if (srcRepeat && srcTransform &&
	src->bits.width == 1 &&
	src->bits.height == 1)
    {
	srcTransform = FALSE;
    }

    if (mask && mask->type == BITS)
    {
	maskRepeat = mask->common.repeat == PIXMAN_REPEAT_NORMAL;

	maskTransform = mask->common.transform != 0;
	if (mask->common.filter == PIXMAN_FILTER_CONVOLUTION)
	    maskTransform = TRUE;

	if (maskRepeat && maskTransform &&
	    mask->bits.width == 1 &&
	    mask->bits.height == 1)
	{
	    maskTransform = FALSE;
	}
    }
    
#ifdef USE_VMX
    if (_pixman_run_fast_path (vmx_fast_paths, imp,
			       op, src, mask, dest,
			       src_x, src_y,
			       mask_x, mask_y,
			       dest_x, dest_y,
			       width, height))
	return;
#endif

#ifdef USE_ARM_NEON
    if (pixman_have_arm_neon() && _pixman_run_fast_path (arm_neon_fast_paths, imp,
							 op, src, mask, dest,
							 src_x, src_y,
							 mask_x, mask_y,
							 dest_x, dest_y,
							 width, height))
	return;
#endif

#ifdef USE_ARM_SIMD
    if (pixman_have_arm_simd() && _pixman_run_fast_path (arm_simd_fast_paths, imp,
							 op, src, mask, dest,
							 src_x, src_y,
							 mask_x, mask_y,
							 dest_x, dest_y,
							 width, height))
	return;
#endif

    /* CompositeGeneral optimizes 1x1 repeating images itself */
    if (src->type == BITS &&
	src->bits.width == 1 && src->bits.height == 1)
    {
	srcRepeat = FALSE;
    }
    
    if (mask && mask->type == BITS &&
	mask->bits.width == 1 && mask->bits.height == 1)
    {
	maskRepeat = FALSE;
    }
    
    /* if we are transforming, repeats are handled in fbFetchTransformed */
    if (srcTransform)
	srcRepeat = FALSE;
    
    if (maskTransform)
	maskRepeat = FALSE;

    _pixman_walk_composite_region (imp, op, src, mask, dest, src_x, src_y,
				   mask_x, mask_y, dest_x, dest_y, width, height,
				   srcRepeat, maskRepeat, pixman_image_composite_rect);
}

static pixman_bool_t
general_blt (pixman_implementation_t *imp,
	     uint32_t *src_bits,
	     uint32_t *dst_bits,
	     int src_stride,
	     int dst_stride,
	     int src_bpp,
	     int dst_bpp,
	     int src_x, int src_y,
	     int dst_x, int dst_y,
	     int width, int height)
{
    /* We can't blit unless we have sse2 or mmx */
    
    return FALSE;
}

static pixman_bool_t
general_fill (pixman_implementation_t *imp,
	      uint32_t *bits,
	      int stride,
	      int bpp,
	      int x,
	      int y,
	      int width,
	      int height,
	      uint32_t xor)
{
    return FALSE;
}

pixman_implementation_t *
_pixman_implementation_create_general (pixman_implementation_t *toplevel)
{
    pixman_implementation_t *imp = _pixman_implementation_create (toplevel, NULL);
    int i;

    for (i = 0; i < PIXMAN_OP_LAST; ++i)
    {
	imp->combine_32[i] = general_combine_32;
	imp->combine_32_ca[i] = general_combine_32_ca;
	imp->combine_64[i] = general_combine_64;
	imp->combine_64_ca[i] = general_combine_64_ca;
    }
    imp->composite = general_composite;
    imp->blt = general_blt;
    imp->fill = general_fill;
    
    return imp;
}
