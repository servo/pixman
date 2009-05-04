/* -*- Mode: c; c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t; -*- */
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
 *
 * Author:  Keith Packard, SuSE, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pixman-private.h"
#include "pixman-mmx.h"
#include "pixman-vmx.h"
#include "pixman-sse2.h"
#include "pixman-arm-simd.h"
#include "pixman-arm-neon.h"
#include "pixman-combine32.h"

static void
fbCompositeSrcScaleNearest (pixman_op_t     op,
			    pixman_image_t *pSrc,
			    pixman_image_t *pMask,
			    pixman_image_t *pDst,
			    int16_t         xSrc,
			    int16_t         ySrc,
			    int16_t         xMask,
			    int16_t         yMask,
			    int16_t         xDst,
			    int16_t         yDst,
			    uint16_t        width,
			    uint16_t        height)
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
pixman_walk_composite_region (pixman_op_t op,
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
			      CompositeFunc compositeRect)
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
		(*compositeRect) (op, pSrc, pMask, pDst,
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
pixman_image_composite_rect  (pixman_op_t                   op,
			      pixman_image_t               *src,
			      pixman_image_t               *mask,
			      pixman_image_t               *dest,
			      int16_t                       src_x,
			      int16_t                       src_y,
			      int16_t                       mask_x,
			      int16_t                       mask_y,
			      int16_t                       dest_x,
			      int16_t                       dest_y,
			      uint16_t                      width,
			      uint16_t                      height)
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

    pixman_composite_rect_general (&compose_data);
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

/*
 * Operator optimizations based on source or destination opacity
 */
typedef struct
{
    pixman_op_t			op;
    pixman_op_t			opSrcDstOpaque;
    pixman_op_t			opSrcOpaque;
    pixman_op_t			opDstOpaque;
} OptimizedOperatorInfo;

static const OptimizedOperatorInfo optimized_operators[] =
{
    /* Input Operator           SRC&DST Opaque          SRC Opaque              DST Opaque      */
    { PIXMAN_OP_OVER,           PIXMAN_OP_SRC,          PIXMAN_OP_SRC,          PIXMAN_OP_OVER },
    { PIXMAN_OP_OVER_REVERSE,   PIXMAN_OP_DST,          PIXMAN_OP_OVER_REVERSE, PIXMAN_OP_DST },
    { PIXMAN_OP_IN,             PIXMAN_OP_SRC,          PIXMAN_OP_IN,           PIXMAN_OP_SRC },
    { PIXMAN_OP_IN_REVERSE,     PIXMAN_OP_DST,          PIXMAN_OP_DST,          PIXMAN_OP_IN_REVERSE },
    { PIXMAN_OP_OUT,            PIXMAN_OP_CLEAR,        PIXMAN_OP_OUT,          PIXMAN_OP_CLEAR },
    { PIXMAN_OP_OUT_REVERSE,    PIXMAN_OP_CLEAR,        PIXMAN_OP_CLEAR,        PIXMAN_OP_OUT_REVERSE },
    { PIXMAN_OP_ATOP,           PIXMAN_OP_SRC,          PIXMAN_OP_IN,           PIXMAN_OP_OVER },
    { PIXMAN_OP_ATOP_REVERSE,   PIXMAN_OP_DST,          PIXMAN_OP_OVER_REVERSE, PIXMAN_OP_IN_REVERSE },
    { PIXMAN_OP_XOR,            PIXMAN_OP_CLEAR,        PIXMAN_OP_OUT,          PIXMAN_OP_OUT_REVERSE },
    { PIXMAN_OP_SATURATE,       PIXMAN_OP_DST,          PIXMAN_OP_OVER_REVERSE, PIXMAN_OP_DST },
    { PIXMAN_OP_NONE }
};

/*
 * Check if the current operator could be optimized
 */
static const OptimizedOperatorInfo*
pixman_operator_can_be_optimized(pixman_op_t op)
{
    const OptimizedOperatorInfo *info;

    for (info = optimized_operators; info->op != PIXMAN_OP_NONE; info++)
    {
        if(info->op == op)
            return info;
    }
    return NULL;
}

/*
 * Optimize the current operator based on opacity of source or destination
 * The output operator should be mathematically equivalent to the source.
 */
static pixman_op_t
pixman_optimize_operator(pixman_op_t op, pixman_image_t *pSrc, pixman_image_t *pMask, pixman_image_t *pDst )
{
    pixman_bool_t is_source_opaque;
    pixman_bool_t is_dest_opaque;
    const OptimizedOperatorInfo *info = pixman_operator_can_be_optimized(op);

    if(!info || pMask)
        return op;

    is_source_opaque = pixman_image_is_opaque(pSrc);
    is_dest_opaque = pixman_image_is_opaque(pDst);

    if(is_source_opaque == FALSE && is_dest_opaque == FALSE)
        return op;

    if(is_source_opaque && is_dest_opaque)
        return info->opSrcDstOpaque;
    else if(is_source_opaque)
        return info->opSrcOpaque;
    else if(is_dest_opaque)
        return info->opDstOpaque;

    return op;

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
PIXMAN_EXPORT void
pixman_image_composite (pixman_op_t      op,
			pixman_image_t * pSrc,
			pixman_image_t * pMask,
			pixman_image_t * pDst,
			int16_t      xSrc,
			int16_t      ySrc,
			int16_t      xMask,
			int16_t      yMask,
			int16_t      xDst,
			int16_t      yDst,
			uint16_t     width,
			uint16_t     height)
{
    pixman_bool_t srcRepeat = pSrc->type == BITS && pSrc->common.repeat == PIXMAN_REPEAT_NORMAL;
    pixman_bool_t maskRepeat = FALSE;
    pixman_bool_t srcTransform = pSrc->common.transform != NULL;
    pixman_bool_t maskTransform = FALSE;
    pixman_bool_t srcAlphaMap = pSrc->common.alpha_map != NULL;
    pixman_bool_t maskAlphaMap = FALSE;
    pixman_bool_t dstAlphaMap = pDst->common.alpha_map != NULL;
    CompositeFunc func = NULL;

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
	pSrc->bits.width == 1 &&
	pSrc->bits.height == 1)
    {
	srcTransform = FALSE;
    }

    if (pMask && pMask->type == BITS)
    {
	maskRepeat = pMask->common.repeat == PIXMAN_REPEAT_NORMAL;

	maskTransform = pMask->common.transform != 0;
	if (pMask->common.filter == PIXMAN_FILTER_CONVOLUTION)
	    maskTransform = TRUE;

	maskAlphaMap = pMask->common.alpha_map != 0;

	if (maskRepeat && maskTransform &&
	    pMask->bits.width == 1 &&
	    pMask->bits.height == 1)
	{
	    maskTransform = FALSE;
	}
    }

    /*
    * Check if we can replace our operator by a simpler one if the src or dest are opaque
    * The output operator should be mathematically equivalent to the source.
    */
    op = pixman_optimize_operator(op, pSrc, pMask, pDst);
    if(op == PIXMAN_OP_DST)
        return;

    if (pSrc->type == BITS
        && srcTransform
        && !pMask
        && op == PIXMAN_OP_SRC
        && !maskAlphaMap && !srcAlphaMap && !dstAlphaMap
        && (pSrc->common.filter == PIXMAN_FILTER_NEAREST)
        && PIXMAN_FORMAT_BPP(pDst->bits.format) == 32
        && pSrc->bits.format == pDst->bits.format
        && pSrc->common.src_clip == &(pSrc->common.full_region)
        && !pSrc->common.read_func && !pSrc->common.write_func
        && !pDst->common.read_func && !pDst->common.write_func)
    {
        /* ensure that the transform matrix only has a scale */
        if (pSrc->common.transform->matrix[0][1] == 0 &&
            pSrc->common.transform->matrix[1][0] == 0 &&
            pSrc->common.transform->matrix[2][0] == 0 &&
            pSrc->common.transform->matrix[2][1] == 0 &&
            pSrc->common.transform->matrix[2][2] == pixman_fixed_1) {
            func = fbCompositeSrcScaleNearest;
            /* Like the general path, fbCompositeSrcScaleNearest handles all the repeat types itself */
            srcRepeat = FALSE;
        }
    } else if ((pSrc->type == BITS || pixman_image_can_get_solid (pSrc)) && (!pMask || pMask->type == BITS)
        && !srcTransform && !maskTransform
        && !maskAlphaMap && !srcAlphaMap && !dstAlphaMap
        && (pSrc->common.filter != PIXMAN_FILTER_CONVOLUTION)
        && (pSrc->common.repeat != PIXMAN_REPEAT_PAD)
        && (pSrc->common.repeat != PIXMAN_REPEAT_REFLECT)
        && (!pMask || (pMask->common.filter != PIXMAN_FILTER_CONVOLUTION &&
		pMask->common.repeat != PIXMAN_REPEAT_PAD && pMask->common.repeat != PIXMAN_REPEAT_REFLECT))
	&& !pSrc->common.read_func && !pSrc->common.write_func
	&& !(pMask && pMask->common.read_func) && !(pMask && pMask->common.write_func)
	&& !pDst->common.read_func && !pDst->common.write_func)
    {
	const FastPathInfo *info;
	pixman_bool_t pixbuf;

	pixbuf =
	    pSrc && pSrc->type == BITS		&&
	    pMask && pMask->type == BITS	&&
	    pSrc->bits.bits == pMask->bits.bits &&
	    xSrc == xMask			&&
	    ySrc == yMask			&&
	    !pMask->common.component_alpha	&&
	    !maskRepeat;
	info = NULL;
	
#ifdef USE_SSE2
	if (pixman_have_sse2 ())
	    info = get_fast_path (sse2_fast_paths, op, pSrc, pMask, pDst, pixbuf);
#endif

#ifdef USE_MMX
	if (!info && pixman_have_mmx())
	    info = get_fast_path (mmx_fast_paths, op, pSrc, pMask, pDst, pixbuf);
#endif

#ifdef USE_VMX

	if (!info && pixman_have_vmx())
	    info = get_fast_path (vmx_fast_paths, op, pSrc, pMask, pDst, pixbuf);
#endif

#ifdef USE_ARM_NEON
        if (!info && pixman_have_arm_neon())
            info = get_fast_path (arm_neon_fast_paths, op, pSrc, pMask, pDst, pixbuf);
#endif

#ifdef USE_ARM_SIMD
	if (!info && pixman_have_arm_simd())
	    info = get_fast_path (arm_simd_fast_paths, op, pSrc, pMask, pDst, pixbuf);
#endif

        if (!info)
	    info = get_fast_path (c_fast_paths, op, pSrc, pMask, pDst, pixbuf);

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
	 pSrc->bits.width == 1		&&
	 pSrc->bits.height == 1)	||
	(maskRepeat			&&
	 pMask->bits.width == 1		&&
	 pMask->bits.height == 1))
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
	if (pSrc->type == BITS &&
	    pSrc->bits.width == 1 && pSrc->bits.height == 1)
	{
	    srcRepeat = FALSE;
	}

	if (pMask && pMask->type == BITS &&
	    pMask->bits.width == 1 && pMask->bits.height == 1)
	{
	    maskRepeat = FALSE;
	}

	/* if we are transforming, repeats are handled in fbFetchTransformed */
	if (srcTransform)
	    srcRepeat = FALSE;

	if (maskTransform)
	    maskRepeat = FALSE;
    }

    pixman_walk_composite_region (op, pSrc, pMask, pDst, xSrc, ySrc,
				  xMask, yMask, xDst, yDst, width, height,
				  srcRepeat, maskRepeat, func);
}


