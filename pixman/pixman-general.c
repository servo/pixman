/*
 * Copyright © 2009 Red Hat, Inc.
 * Copyright © 2000 SuSE, Inc.
 * Copyright © 2007 Red Hat, Inc.
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

/*
 *
 * Copyright © 2000 Keith Packard, member of The XFree86 Project, Inc.
 *             2005 Lars Knoll & Zack Rusin, Trolltech
 *             2008 Aaron Plattner, NVIDIA Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

static void
pixman_composite_rect_general_internal (const FbComposeData *data,
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
	CombineFunc32 compose;

	if (wide)
	{
	    if (component_alpha)
		compose = (CombineFunc32)pixman_composeFunctions64.combineC[data->op];
	    else
		compose = (CombineFunc32)pixman_composeFunctions64.combineU[data->op];
	}
	else
	{
	    if (component_alpha)
		compose = pixman_composeFunctions.combineC[data->op];
	    else
		compose = pixman_composeFunctions.combineU[data->op];
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
		compose (dest_buffer, src_buffer, mask_buffer, data->width);

		/* write back */
		store (&(data->dest->bits), data->xDest, data->yDest + i, data->width,
		       dest_buffer);
	    }
	    else
	    {
		/* blend */
		compose (bits + (data->yDest + i) * stride +
			 data->xDest,
			 src_buffer, mask_buffer, data->width);
	    }
	}
    }
}

#define SCANLINE_BUFFER_LENGTH 8192

static void
general_composite_rect (const FbComposeData *data)
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

    pixman_composite_rect_general_internal (data, src_buffer,
					    mask_buffer, dest_buffer,
					    wide);

    if (scanline_buffer != stack_scanline_buffer)
	free (scanline_buffer);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pixman-private.h"
#include "pixman-mmx.h"
#include "pixman-vmx.h"
#include "pixman-sse2.h"
#include "pixman-arm-simd.h"
#include "pixman-combine32.h"

static void
fbCompositeSrcScaleNearest (pixman_implementation_t *imp,
			    pixman_op_t     op,
			    pixman_image_t *pSrc,
			    pixman_image_t *pMask,
			    pixman_image_t *pDst,
			    int32_t         xSrc,
			    int32_t         ySrc,
			    int32_t         xMask,
			    int32_t         yMask,
			    int32_t         xDst,
			    int32_t         yDst,
			    int32_t        width,
			    int32_t        height)
{
    uint32_t       *dst;
    uint32_t       *src;
    int             dstStride, srcStride;
    int             i, j;
    pixman_vector_t v;

    fbComposeGetStart (pDst, xDst, yDst, uint32_t, dstStride, dst, 1);
    /* pass in 0 instead of xSrc and ySrc because xSrc and ySrc need to be
     * transformed from destination space to source space */
    fbComposeGetStart (pSrc, 0, 0, uint32_t, srcStride, src, 1);

    /* reference point is the center of the pixel */
    v.vector[0] = pixman_int_to_fixed(xSrc) + pixman_fixed_1 / 2;
    v.vector[1] = pixman_int_to_fixed(ySrc) + pixman_fixed_1 / 2;
    v.vector[2] = pixman_fixed_1;

    if (!pixman_transform_point_3d (pSrc->common.transform, &v))
        return;

    /* Round down to closest integer, ensuring that 0.5 rounds to 0, not 1 */
    v.vector[0] -= pixman_fixed_e;
    v.vector[1] -= pixman_fixed_e;

    for (j = 0; j < height; j++) {
        pixman_fixed_t vx = v.vector[0];
        pixman_fixed_t vy = v.vector[1];
        for (i = 0; i < width; ++i) {
            pixman_bool_t inside_bounds;
            uint32_t result;
            int x, y;
            x = vx >> 16;
            y = vy >> 16;

            /* apply the repeat function */
            switch (pSrc->common.repeat) {
                case PIXMAN_REPEAT_NORMAL:
                    x = MOD (x, pSrc->bits.width);
                    y = MOD (y, pSrc->bits.height);
                    inside_bounds = TRUE;
                    break;

                case PIXMAN_REPEAT_PAD:
                    x = CLIP (x, 0, pSrc->bits.width-1);
                    y = CLIP (y, 0, pSrc->bits.height-1);
                    inside_bounds = TRUE;
                    break;

                case PIXMAN_REPEAT_REFLECT:
		    x = MOD (x, pSrc->bits.width * 2);
		    if (x >= pSrc->bits.width)
			    x = pSrc->bits.width * 2 - x - 1;
		    y = MOD (y, pSrc->bits.height * 2);
		    if (y >= pSrc->bits.height)
			    y = pSrc->bits.height * 2 - y - 1;
		    inside_bounds = TRUE;
		    break;

                case PIXMAN_REPEAT_NONE:
                default:
                    inside_bounds = (x >= 0 && x < pSrc->bits.width && y >= 0 && y < pSrc->bits.height);
                    break;
            }

            if (inside_bounds) {
                //XXX: we should move this multiplication out of the loop
                result = READ(pSrc, src + y * srcStride + x);
            } else {
                result = 0;
            }
            WRITE(pDst, dst + i, result);

            /* adjust the x location by a unit vector in the x direction:
             * this is equivalent to transforming x+1 of the destination point to source space */
            vx += pSrc->common.transform->matrix[0][0];
        }
        /* adjust the y location by a unit vector in the y direction
         * this is equivalent to transforming y+1 of the destination point to source space */
        v.vector[1] += pSrc->common.transform->matrix[1][1];
        dst += dstStride;
    }
}

static void
pixman_walk_composite_region (pixman_implementation_t *imp,
			      pixman_op_t op,
			      pixman_image_t * pSrc,
			      pixman_image_t * pMask,
			      pixman_image_t * pDst,
			      int16_t xSrc,
			      int16_t ySrc,
			      int16_t xMask,
			      int16_t yMask,
			      int16_t xDst,
			      int16_t yDst,
			      uint16_t width,
			      uint16_t height,
			      pixman_bool_t srcRepeat,
			      pixman_bool_t maskRepeat,
			      pixman_composite_func_t compositeRect)
{
    int		    n;
    const pixman_box32_t *pbox;
    int		    w, h, w_this, h_this;
    int		    x_msk, y_msk, x_src, y_src, x_dst, y_dst;
    pixman_region32_t reg;
    pixman_region32_t *region;

    pixman_region32_init (&reg);
    if (!pixman_compute_composite_region32 (&reg, pSrc, pMask, pDst,
					    xSrc, ySrc, xMask, yMask, xDst, yDst, width, height))
    {
	return;
    }

    region = &reg;

    pbox = pixman_region32_rectangles (region, &n);
    while (n--)
    {
	h = pbox->y2 - pbox->y1;
	y_src = pbox->y1 - yDst + ySrc;
	y_msk = pbox->y1 - yDst + yMask;
	y_dst = pbox->y1;
	while (h)
	{
	    h_this = h;
	    w = pbox->x2 - pbox->x1;
	    x_src = pbox->x1 - xDst + xSrc;
	    x_msk = pbox->x1 - xDst + xMask;
	    x_dst = pbox->x1;
	    if (maskRepeat)
	    {
		y_msk = MOD (y_msk, pMask->bits.height);
		if (h_this > pMask->bits.height - y_msk)
		    h_this = pMask->bits.height - y_msk;
	    }
	    if (srcRepeat)
	    {
		y_src = MOD (y_src, pSrc->bits.height);
		if (h_this > pSrc->bits.height - y_src)
		    h_this = pSrc->bits.height - y_src;
	    }
	    while (w)
	    {
		w_this = w;
		if (maskRepeat)
		{
		    x_msk = MOD (x_msk, pMask->bits.width);
		    if (w_this > pMask->bits.width - x_msk)
			w_this = pMask->bits.width - x_msk;
		}
		if (srcRepeat)
		{
		    x_src = MOD (x_src, pSrc->bits.width);
		    if (w_this > pSrc->bits.width - x_src)
			w_this = pSrc->bits.width - x_src;
		}
		(*compositeRect) (imp,
				  op, pSrc, pMask, pDst,
				  x_src, y_src, x_msk, y_msk, x_dst, y_dst,
				  w_this, h_this);
		w -= w_this;
		x_src += w_this;
		x_msk += w_this;
		x_dst += w_this;
	    }
	    h -= h_this;
	    y_src += h_this;
	    y_msk += h_this;
	    y_dst += h_this;
	}
	pbox++;
    }
    pixman_region32_fini (&reg);
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

    general_composite_rect (&compose_data);
}

static pixman_bool_t
mask_is_solid (pixman_image_t *mask)
{
    if (mask->type == SOLID)
	return TRUE;

    if (mask->type == BITS &&
	mask->common.repeat == PIXMAN_REPEAT_NORMAL &&
	mask->bits.width == 1 &&
	mask->bits.height == 1)
    {
	return TRUE;
    }

    return FALSE;
}

static const FastPathInfo *
get_fast_path (const FastPathInfo *fast_paths,
	       pixman_op_t         op,
	       pixman_image_t     *pSrc,
	       pixman_image_t     *pMask,
	       pixman_image_t     *pDst,
	       pixman_bool_t       is_pixbuf)
{
    const FastPathInfo *info;

    for (info = fast_paths; info->op != PIXMAN_OP_NONE; info++)
    {
	pixman_bool_t valid_src		= FALSE;
	pixman_bool_t valid_mask	= FALSE;

	if (info->op != op)
	    continue;

	if ((info->src_format == PIXMAN_solid && pixman_image_can_get_solid (pSrc))		||
	    (pSrc->type == BITS && info->src_format == pSrc->bits.format))
	{
	    valid_src = TRUE;
	}

	if (!valid_src)
	    continue;

	if ((info->mask_format == PIXMAN_null && !pMask)			||
	    (pMask && pMask->type == BITS && info->mask_format == pMask->bits.format))
	{
	    valid_mask = TRUE;

	    if (info->flags & NEED_SOLID_MASK)
	    {
		if (!pMask || !mask_is_solid (pMask))
		    valid_mask = FALSE;
	    }

	    if (info->flags & NEED_COMPONENT_ALPHA)
	    {
		if (!pMask || !pMask->common.component_alpha)
		    valid_mask = FALSE;
	    }
	}

	if (!valid_mask)
	    continue;
	
	if (info->dest_format != pDst->bits.format)
	    continue;

	if ((info->flags & NEED_PIXBUF) && !is_pixbuf)
	    continue;

	return info;
    }

    return NULL;
}

#if defined(USE_SSE2) && defined(__GNUC__) && !defined(__x86_64__) && !defined(__amd64__)

/*
 * Work around GCC bug causing crashes in Mozilla with SSE2
 * 
 * When using SSE2 intrinsics, gcc assumes that the stack is 16 byte
 * aligned. Unfortunately some code, such as Mozilla and Mono contain
 * code that aligns the stack to 4 bytes.
 *
 * The __force_align_arg_pointer__ makes gcc generate a prologue that
 * realigns the stack pointer to 16 bytes.
 *
 * On x86-64 this is not necessary because the standard ABI already
 * calls for a 16 byte aligned stack.
 *
 * See https://bugs.freedesktop.org/show_bug.cgi?id=15693
 */

__attribute__((__force_align_arg_pointer__))
#endif
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
    pixman_bool_t srcAlphaMap = src->common.alpha_map != NULL;
    pixman_bool_t maskAlphaMap = FALSE;
    pixman_bool_t dstAlphaMap = dest->common.alpha_map != NULL;
    pixman_composite_func_t func = NULL;

#ifdef USE_MMX
    fbComposeSetupMMX();
#endif

#ifdef USE_VMX
    fbComposeSetupVMX();
#endif

#ifdef USE_SSE2
    fbComposeSetupSSE2();
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

	maskAlphaMap = mask->common.alpha_map != 0;

	if (maskRepeat && maskTransform &&
	    mask->bits.width == 1 &&
	    mask->bits.height == 1)
	{
	    maskTransform = FALSE;
	}
    }

    if (src->type == BITS
        && srcTransform
        && !mask
        && op == PIXMAN_OP_SRC
        && !maskAlphaMap && !srcAlphaMap && !dstAlphaMap
        && (src->common.filter == PIXMAN_FILTER_NEAREST)
        && PIXMAN_FORMAT_BPP(dest->bits.format) == 32
        && src->bits.format == dest->bits.format
        && src->common.src_clip == &(src->common.full_region)
        && !src->common.read_func && !src->common.write_func
        && !dest->common.read_func && !dest->common.write_func)
    {
        /* ensure that the transform matrix only has a scale */
        if (src->common.transform->matrix[0][1] == 0 &&
            src->common.transform->matrix[1][0] == 0 &&
            src->common.transform->matrix[2][0] == 0 &&
            src->common.transform->matrix[2][1] == 0 &&
            src->common.transform->matrix[2][2] == pixman_fixed_1) {
            func = fbCompositeSrcScaleNearest;
            /* Like the general path, fbCompositeSrcScaleNearest handles all the repeat types itself */
            srcRepeat = FALSE;
        }
    } else if ((src->type == BITS || pixman_image_can_get_solid (src)) && (!mask || mask->type == BITS)
        && !srcTransform && !maskTransform
        && !maskAlphaMap && !srcAlphaMap && !dstAlphaMap
        && (src->common.filter != PIXMAN_FILTER_CONVOLUTION)
        && (src->common.repeat != PIXMAN_REPEAT_PAD)
        && (src->common.repeat != PIXMAN_REPEAT_REFLECT)
        && (!mask || (mask->common.filter != PIXMAN_FILTER_CONVOLUTION &&
		mask->common.repeat != PIXMAN_REPEAT_PAD && mask->common.repeat != PIXMAN_REPEAT_REFLECT))
	&& !src->common.read_func && !src->common.write_func
	&& !(mask && mask->common.read_func) && !(mask && mask->common.write_func)
	&& !dest->common.read_func && !dest->common.write_func)
    {
	const FastPathInfo *info;
	pixman_bool_t pixbuf;

	pixbuf =
	    src && src->type == BITS		&&
	    mask && mask->type == BITS	&&
	    src->bits.bits == mask->bits.bits &&
	    src_x == mask_x			&&
	    src_y == mask_y			&&
	    !mask->common.component_alpha	&&
	    !maskRepeat;
	info = NULL;
	
#ifdef USE_SSE2
	if (pixman_have_sse2 ())
	    info = get_fast_path (sse2_fast_paths, op, src, mask, dest, pixbuf);
#endif

#ifdef USE_MMX
	if (!info && pixman_have_mmx())
	    info = get_fast_path (mmx_fast_paths, op, src, mask, dest, pixbuf);
#endif

#ifdef USE_VMX

	if (!info && pixman_have_vmx())
	    info = get_fast_path (vmx_fast_paths, op, src, mask, dest, pixbuf);
#endif

#ifdef USE_ARM_NEON
        if (!info && pixman_have_arm_neon())
            info = get_fast_path (arm_neon_fast_paths, op, src, mask, dest, pixbuf);
#endif

#ifdef USE_ARM_SIMD
	if (!info && pixman_have_arm_simd())
	    info = get_fast_path (arm_simd_fast_paths, op, src, mask, dest, pixbuf);
#endif

        if (!info)
	    info = get_fast_path (c_fast_paths, op, src, mask, dest, pixbuf);

	if (info)
	{
	    func = info->func;

	    if (info->src_format == PIXMAN_solid)
		srcRepeat = FALSE;

	    if (info->mask_format == PIXMAN_solid	||
		info->flags & NEED_SOLID_MASK)
	    {
		maskRepeat = FALSE;
	    }
	}
    }
    
    if ((srcRepeat			&&
	 src->bits.width == 1		&&
	 src->bits.height == 1)	||
	(maskRepeat			&&
	 mask->bits.width == 1		&&
	 mask->bits.height == 1))
    {
	/* If src or mask are repeating 1x1 images and srcRepeat or
	 * maskRepeat are still TRUE, it means the fast path we
	 * selected does not actually handle repeating images.
	 *
	 * So rather than call the "fast path" with a zillion
	 * 1x1 requests, we just use the general code (which does
	 * do something sensible with 1x1 repeating images).
	 */
	func = NULL;
    }

    if (!func)
    {
	func = pixman_image_composite_rect;

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
    }

    pixman_walk_composite_region (imp, op, src, mask, dest, src_x, src_y,
				  mask_x, mask_y, dest_x, dest_y, width, height,
				  srcRepeat, maskRepeat, func);
}

pixman_implementation_t *
_pixman_implementation_create_general (pixman_implementation_t *toplevel)
{
    pixman_implementation_t *imp = _pixman_implementation_create (toplevel, NULL);
    int i;

    imp->composite = general_composite;
    
    for (i = 0; i < PIXMAN_OP_LAST; ++i)
    {
	imp->combine_32[i] = general_combine_32;
	imp->combine_32_ca[i] = general_combine_32_ca;
	imp->combine_64[i] = general_combine_64;
	imp->combine_64_ca[i] = general_combine_64_ca;
    }

    return imp;
}
