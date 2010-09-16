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

#ifndef PIXMAN_FAST_PATH_H__
#define PIXMAN_FAST_PATH_H__

#include "pixman-private.h"

static force_inline pixman_bool_t
repeat (pixman_repeat_t repeat, int *c, int size)
{
    if (repeat == PIXMAN_REPEAT_NONE)
    {
	if (*c < 0 || *c >= size)
	    return FALSE;
    }
    else if (repeat == PIXMAN_REPEAT_NORMAL)
    {
	while (*c >= size)
	    *c -= size;
	while (*c < 0)
	    *c += size;
    }
    else if (repeat == PIXMAN_REPEAT_PAD)
    {
	*c = CLIP (*c, 0, size - 1);
    }
    else /* REFLECT */
    {
	*c = MOD (*c, size * 2);
	if (*c >= size)
	    *c = size * 2 - *c - 1;
    }
    return TRUE;
}

/* A macroified version of specialized nearest scalers for some
 * common 8888 and 565 formats. It supports SRC and OVER ops.
 *
 * There are two repeat versions, one that handles repeat normal,
 * and one without repeat handling that only works if the src region
 * used is completely covered by the pre-repeated source samples.
 *
 * The loops are unrolled to process two pixels per iteration for better
 * performance on most CPU architectures (superscalar processors
 * can issue several operations simultaneously, other processors can hide
 * instructions latencies by pipelining operations). Unrolling more
 * does not make much sense because the compiler will start running out
 * of spare registers soon.
 */

#define GET_8888_ALPHA(s) ((s) >> 24)
 /* This is not actually used since we don't have an OVER with
    565 source, but it is needed to build. */
#define GET_0565_ALPHA(s) 0xff

#define FAST_NEAREST(scale_func_name, SRC_FORMAT, DST_FORMAT,					\
		     src_type_t, dst_type_t, OP, repeat_mode)					\
static void											\
fast_composite_scaled_nearest_ ## scale_func_name ## _ ## OP (pixman_implementation_t *imp,	\
							      pixman_op_t              op,      \
							      pixman_image_t *         src_image, \
							      pixman_image_t *         mask_image, \
							      pixman_image_t *         dst_image, \
							      int32_t                  src_x,   \
							      int32_t                  src_y,   \
							      int32_t                  mask_x,  \
							      int32_t                  mask_y,  \
							      int32_t                  dst_x,   \
							      int32_t                  dst_y,   \
							      int32_t                  width,   \
							      int32_t                  height)  \
{												\
    dst_type_t *dst_line;									\
    src_type_t *src_first_line;									\
    uint32_t   d;										\
    src_type_t s1, s2;										\
    uint8_t   a1, a2;										\
    int       w;										\
    int       x1, x2, y;									\
    pixman_fixed_t orig_vx;									\
    pixman_fixed_t max_vx, max_vy;								\
    pixman_vector_t v;										\
    pixman_fixed_t vx, vy;									\
    pixman_fixed_t unit_x, unit_y;								\
												\
    src_type_t *src;										\
    dst_type_t *dst;										\
    int       src_stride, dst_stride;								\
												\
    if (PIXMAN_OP_ ## OP != PIXMAN_OP_SRC && PIXMAN_OP_ ## OP != PIXMAN_OP_OVER)		\
	abort();										\
												\
    if (PIXMAN_REPEAT_ ## repeat_mode != PIXMAN_REPEAT_NORMAL		&&			\
	PIXMAN_REPEAT_ ## repeat_mode != PIXMAN_REPEAT_NONE)					\
    {												\
	abort();										\
    }												\
												\
    PIXMAN_IMAGE_GET_LINE (dst_image, dst_x, dst_y, dst_type_t, dst_stride, dst_line, 1);	\
    /* pass in 0 instead of src_x and src_y because src_x and src_y need to be			\
     * transformed from destination space to source space */					\
    PIXMAN_IMAGE_GET_LINE (src_image, 0, 0, src_type_t, src_stride, src_first_line, 1);		\
												\
    /* reference point is the center of the pixel */						\
    v.vector[0] = pixman_int_to_fixed (src_x) + pixman_fixed_1 / 2;				\
    v.vector[1] = pixman_int_to_fixed (src_y) + pixman_fixed_1 / 2;				\
    v.vector[2] = pixman_fixed_1;								\
												\
    if (!pixman_transform_point_3d (src_image->common.transform, &v))				\
	return;											\
												\
    unit_x = src_image->common.transform->matrix[0][0];						\
    unit_y = src_image->common.transform->matrix[1][1];						\
												\
    /* Round down to closest integer, ensuring that 0.5 rounds to 0, not 1 */			\
    v.vector[0] -= pixman_fixed_e;								\
    v.vector[1] -= pixman_fixed_e;								\
												\
    vx = v.vector[0];										\
    vy = v.vector[1];										\
												\
    if (PIXMAN_REPEAT_ ## repeat_mode == PIXMAN_REPEAT_NORMAL)					\
    {												\
	/* Clamp repeating positions inside the actual samples */				\
	max_vx = src_image->bits.width << 16;							\
	max_vy = src_image->bits.height << 16;							\
												\
	repeat (PIXMAN_REPEAT_NORMAL, &vx, max_vx);						\
	repeat (PIXMAN_REPEAT_NORMAL, &vy, max_vy);						\
    }												\
												\
    orig_vx = vx;										\
												\
    while (--height >= 0)									\
    {												\
	dst = dst_line;										\
	dst_line += dst_stride;									\
												\
	y = vy >> 16;										\
	vy += unit_y;										\
	if (PIXMAN_REPEAT_ ## repeat_mode == PIXMAN_REPEAT_NORMAL)				\
	    repeat (PIXMAN_REPEAT_NORMAL, &vy, max_vy);						\
												\
	src = src_first_line + src_stride * y;							\
												\
	w = width;										\
	vx = orig_vx;										\
	while ((w -= 2) >= 0)									\
	{											\
	    x1 = vx >> 16;									\
	    vx += unit_x;									\
	    if (PIXMAN_REPEAT_ ## repeat_mode == PIXMAN_REPEAT_NORMAL)				\
	    {											\
		/* This works because we know that unit_x is positive */			\
		while (vx >= max_vx)								\
		    vx -= max_vx;								\
	    }											\
	    s1 = src[x1];									\
												\
	    x2 = vx >> 16;									\
	    vx += unit_x;									\
	    if (PIXMAN_REPEAT_ ## repeat_mode == PIXMAN_REPEAT_NORMAL)				\
	    {											\
		/* This works because we know that unit_x is positive */			\
		while (vx >= max_vx)								\
		    vx -= max_vx;								\
	    }											\
	    s2 = src[x2];									\
												\
	    if (PIXMAN_OP_ ## OP == PIXMAN_OP_OVER)						\
	    {											\
		a1 = GET_ ## SRC_FORMAT ## _ALPHA(s1);						\
		a2 = GET_ ## SRC_FORMAT ## _ALPHA(s2);						\
												\
		if (a1 == 0xff)									\
		{										\
		    *dst = CONVERT_ ## SRC_FORMAT ## _TO_ ## DST_FORMAT (s1);			\
		}										\
		else if (s1)									\
		{										\
		    d = CONVERT_ ## DST_FORMAT ## _TO_8888 (*dst);				\
		    s1 = CONVERT_ ## SRC_FORMAT ## _TO_8888 (s1);				\
		    a1 ^= 0xff;									\
		    UN8x4_MUL_UN8_ADD_UN8x4 (d, a1, s1);					\
		    *dst = CONVERT_8888_TO_ ## DST_FORMAT (d);					\
		}										\
		dst++;										\
												\
		if (a2 == 0xff)									\
		{										\
		    *dst = CONVERT_ ## SRC_FORMAT ## _TO_ ## DST_FORMAT (s2);			\
		}										\
		else if (s2)									\
		{										\
		    d = CONVERT_## DST_FORMAT ## _TO_8888 (*dst);				\
		    s2 = CONVERT_## SRC_FORMAT ## _TO_8888 (s2);				\
		    a2 ^= 0xff;									\
		    UN8x4_MUL_UN8_ADD_UN8x4 (d, a2, s2);					\
		    *dst = CONVERT_8888_TO_ ## DST_FORMAT (d);					\
		}										\
		dst++;										\
	    }											\
	    else /* PIXMAN_OP_SRC */								\
	    {											\
		*dst++ = CONVERT_ ## SRC_FORMAT ## _TO_ ## DST_FORMAT (s1);			\
		*dst++ = CONVERT_ ## SRC_FORMAT ## _TO_ ## DST_FORMAT (s2);			\
	    }											\
	}											\
												\
	if (w & 1)										\
	{											\
	    x1 = vx >> 16;									\
	    s1 = src[x1];									\
												\
	    if (PIXMAN_OP_ ## OP == PIXMAN_OP_OVER)						\
	    {											\
		a1 = GET_ ## SRC_FORMAT ## _ALPHA(s1);						\
												\
		if (a1 == 0xff)									\
		{										\
		    *dst = CONVERT_ ## SRC_FORMAT ## _TO_ ## DST_FORMAT (s1);			\
		}										\
		else if (s1)									\
		{										\
		    d = CONVERT_## DST_FORMAT ## _TO_8888 (*dst);				\
		    s1 = CONVERT_ ## SRC_FORMAT ## _TO_8888 (s1);				\
		    a1 ^= 0xff;									\
		    UN8x4_MUL_UN8_ADD_UN8x4 (d, a1, s1);					\
		    *dst = CONVERT_8888_TO_ ## DST_FORMAT (d);					\
		}										\
		dst++;										\
	    }											\
	    else /* PIXMAN_OP_SRC */								\
	    {											\
		*dst++ = CONVERT_ ## SRC_FORMAT ## _TO_ ## DST_FORMAT (s1);			\
	    }											\
	}											\
    }												\
}

#define SCALED_NEAREST_FLAGS						\
    (FAST_PATH_SCALE_TRANSFORM	|					\
     FAST_PATH_NO_ALPHA_MAP	|					\
     FAST_PATH_NEAREST_FILTER	|					\
     FAST_PATH_NO_ACCESSORS	|					\
     FAST_PATH_NO_WIDE_FORMAT)

#define SIMPLE_NEAREST_FAST_PATH(op,s,d,func)				\
    {   PIXMAN_OP_ ## op,						\
	PIXMAN_ ## s,							\
	(SCALED_NEAREST_FLAGS		|				\
	 FAST_PATH_NORMAL_REPEAT	|				\
	 FAST_PATH_X_UNIT_POSITIVE),					\
	PIXMAN_null, 0,							\
	PIXMAN_ ## d, FAST_PATH_STD_DEST_FLAGS,				\
	fast_composite_scaled_nearest_ ## func ## _normal ## _ ## op,	\
    },									\
    {   PIXMAN_OP_ ## op,						\
	PIXMAN_ ## s,							\
	SCALED_NEAREST_FLAGS | FAST_PATH_SAMPLES_COVER_CLIP,		\
	PIXMAN_null, 0,							\
	PIXMAN_ ## d, FAST_PATH_STD_DEST_FLAGS,				\
	fast_composite_scaled_nearest_ ## func ## _none ## _ ## op,	\
    }

#endif
