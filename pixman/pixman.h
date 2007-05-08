/***********************************************************

Copyright 1987, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

Copyright 1987 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/
/*
 * Copyright © 1998 Keith Packard
 * Copyright   2007 Red Hat, Inc.
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
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef PIXMAN_H__
#define PIXMAN_H__

/* FIXME: bad hack to avoid including config.h in
 * an installed header. This should probably be donw
 * with an installed pixman-config.h header
 */
#define HAVE_STDINT_H 1

/*
 * Standard integers
 */
#if   HAVE_STDINT_H
# include <stdint.h>
#elif HAVE_INTTYPES_H
# include <inttypes.h>
#elif HAVE_SYS_INT_TYPES_H
# include <sys/int_types.h>
#elif defined(_MSC_VER)
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#  error Cannot find definitions for fixed-width integral types (uint8_t, uint32_t, etc.)
#endif

/*
 * Boolean
 */
typedef int pixman_bool_t;

/*
 * Fixpoint numbers
 */
typedef uint64_t		pixman_fixed_32_32_t;
typedef pixman_fixed_32_32_t	pixman_fixed_48_16_t;
typedef uint32_t		pixman_fixed_1_31_t;
typedef uint32_t		pixman_fixed_1_16_t;
typedef int32_t			pixman_fixed_16_16_t;
typedef pixman_fixed_16_16_t	pixman_fixed_t;

#define pixman_fixed_e			((pixman_fixed) 1)
#define pixman_fixed_1			(pixman_int_to_fixed(1))
#define pixman_fixed_1_minus_e		(pixman_fixed_1 - pixman_fixed_e)
#define pixman_fixed_to_int(f)		((int) ((f) >> 16))
#define pixman_int_to_fixed(i)		((pixman_fixed_t) ((i) << 16))
#define pixman_fixed_to_double(f)	(double) ((f) / (double) pixman_fixed_1)
#define pixman_fixed_frac(f)		((f) & pixman_fixed_1_minus_e)
#define pixman_fixed_floor(f)		((f) & ~pixman_fixed_1_minus_e)
#define pixman_fixed_ceil(f)		pixman_fixed_floor ((f) + pixman_fixed_1_minus_e)
#define pixman_fixed_fraction(f)	((f) & pixman_fixed_1_minus_e)
#define pixman_fixed_mod_2(f)		((f) & (pixman_fixed1 | pixman_fixed_1_minus_e))
#define pixman_max_fixed_48_16		((pixman_fixed_48_16_t) 0x7fffffff)
#define pixman_min_fixed_48_16		(-((pixman_fixed_48_16_t) 1 << 31))

/*
 * Misc structs
 */
typedef struct pixman_color pixman_color_t;
typedef struct pixman_point_fixed pixman_point_fixed_t;
typedef struct pixman_line_fixed pixman_line_fixed_t;
typedef struct pixman_vector pixman_vector_t;
typedef struct pixman_transform pixman_transform_t;

struct pixman_color
{
    uint16_t	red;
    uint16_t    green;
    uint16_t    blue;
    uint16_t    alpha;
};

struct pixman_point_fixed
{
    pixman_fixed_t	x;
    pixman_fixed_t	y;
};

struct pixman_line_fixed
{
    pixman_point_fixed_t	p1, p2;
};

struct pixman_vector
{
    pixman_fixed_t	vector[3];
};

struct pixman_transform
{
    pixman_fixed_t	matrix[3][3];
};

/* Don't blame me, blame XRender */
typedef enum
{
    PIXMAN_REPEAT_NONE,
    PIXMAN_REPEAT_NORMAL,
    PIXMAN_REPEAT_PAD,
    PIXMAN_REPEAT_REFLECT
} pixman_repeat_t;

typedef enum
{
    PIXMAN_FILTER_FAST,
    PIXMAN_FILTER_GOOD,
    PIXMAN_FILTER_BEST,
    PIXMAN_FILTER_NEAREST,
    PIXMAN_FILTER_BILINEAR,
    PIXMAN_FILTER_CONVOLUTION
} pixman_filter_t;

typedef enum
{
    PIXMAN_OP_CLEAR,
    PIXMAN_OP_SRC,
    PIXMAN_OP_DST,
    PIXMAN_OP_OVER,
    PIXMAN_OP_OVER_REVERSE,
    PIXMAN_OP_IN,
    PIXMAN_OP_IN_REVERSE,
    PIXMAN_OP_OUT,
    PIXMAN_OP_OUT_REVERSE,
    PIXMAN_OP_ATOP,
    PIXMAN_OP_ATOP_REVERSE,
    PIXMAN_OP_XOR,
    PIXMAN_OP_ADD,
    PIXMAN_OP_SATURATE
} pixman_op_t;

/*
 * Regions
 */
typedef struct pixman_region16_data	pixman_region16_data_t;
typedef struct pixman_box16		pixman_box16_t;
typedef struct pixman_region16		pixman_region16_t;

struct pixman_box16
{
    int16_t x1, y1, x2, y2;
};

struct pixman_region16
{
    pixman_box16_t          extents;
    pixman_region16_data_t  *data;
};

typedef enum
{
    PIXMAN_REGION_OUT,
    PIXMAN_REGION_IN,
    PIXMAN_REGION_PART
} pixman_region_overlap_t;

/* creation/destruction */
void                    pixman_region_init              (pixman_region16_t *region);
void                    pixman_region_init_rect         (pixman_region16_t *region,
							 int                x,
							 int                y,
							 unsigned int       width,
							 unsigned int       height);
void                    pixman_region_init_with_extents (pixman_region16_t *region,
							 pixman_box16_t    *extents);
void                    pixman_region_fini              (pixman_region16_t *region);

/* manipulation */
void                    pixman_region_translate  (pixman_region16_t *region,
						  int                x,
						  int                y);
pixman_bool_t           pixman_region_copy       (pixman_region16_t *dest,
						  pixman_region16_t *source);
pixman_bool_t           pixman_region_intersect  (pixman_region16_t *newReg,
						  pixman_region16_t *reg1,
						  pixman_region16_t *reg2);
pixman_bool_t           pixman_region_union      (pixman_region16_t *newReg,
						  pixman_region16_t *reg1,
						  pixman_region16_t *reg2);
pixman_bool_t           pixman_region_union_rect (pixman_region16_t *dest,
						  pixman_region16_t *source,
						  int                x,
						  int                y,
						  unsigned int       width,
						  unsigned int       height);
pixman_bool_t           pixman_region_subtract   (pixman_region16_t *regD,
						  pixman_region16_t *regM,
						  pixman_region16_t *regS);
pixman_bool_t           pixman_region_inverse    (pixman_region16_t *newReg,
						  pixman_region16_t *reg1,
						  pixman_box16_t    *invRect);
pixman_bool_t           pixman_region_contains_point (pixman_region16_t *region, int x, int y, pixman_box16_t *box);
pixman_region_overlap_t pixman_region_contains_rectangle (pixman_region16_t *pixman_region16_t, pixman_box16_t *prect);
pixman_bool_t           pixman_region_not_empty (pixman_region16_t *region);
pixman_box16_t *        pixman_region_extents (pixman_region16_t *region);
int                     pixman_region_n_rects (pixman_region16_t *region);
const pixman_box16_t *  pixman_region_rectangles (pixman_region16_t *region,
						  int		    *n_rects);

/*
 * Images
 */
typedef struct pixman_indexed	pixman_indexed_t;

#define PIXMAN_MAX_INDEXED  256 /* XXX depth must be <= 8 */

#if PIXMAN_MAX_INDEXED <= 256
typedef uint8_t pixman_index_type;
#endif

struct pixman_indexed
{
    pixman_bool_t       color;
    uint32_t		rgba[PIXMAN_MAX_INDEXED];
    pixman_index_type	ent[32768];
};

/*
 * While the protocol is generous in format support, the
 * sample implementation allows only packed RGB and GBR
 * representations for data to simplify software rendering,
 */
#define PIXMAN_FORMAT(bpp,type,a,r,g,b)	(((bpp) << 24) |  \
					 ((type) << 16) | \
					 ((a) << 12) |	  \
					 ((r) << 8) |	  \
					 ((g) << 4) |	  \
					 ((b)))

#define PIXMAN_FORMAT_BPP(f)	(((f) >> 24)       )
#define PIXMAN_FORMAT_TYPE(f)	(((f) >> 16) & 0xff)
#define PIXMAN_FORMAT_A(f)	(((f) >> 12) & 0x0f)
#define PIXMAN_FORMAT_R(f)	(((f) >>  8) & 0x0f)
#define PIXMAN_FORMAT_G(f)	(((f) >>  4) & 0x0f)
#define PIXMAN_FORMAT_B(f)	(((f)      ) & 0x0f)
#define PIXMAN_FORMAT_RGB(f)	(((f)      ) & 0xfff)
#define PIXMAN_FORMAT_VIS(f)	(((f)      ) & 0xffff)

#define PIXMAN_TYPE_OTHER	0
#define PIXMAN_TYPE_A	1
#define PIXMAN_TYPE_ARGB	2
#define PIXMAN_TYPE_ABGR	3
#define PIXMAN_TYPE_COLOR	4
#define PIXMAN_TYPE_GRAY	5

#define PIXMAN_FORMAT_COLOR(f)	(PIXMAN_FORMAT_TYPE(f) & 2)

/* 32bpp formats */
typedef enum {
    PIXMAN_a8r8g8b8 =	PIXMAN_FORMAT(32,PIXMAN_TYPE_ARGB,8,8,8,8),
    PIXMAN_x8r8g8b8 =	PIXMAN_FORMAT(32,PIXMAN_TYPE_ARGB,0,8,8,8),
    PIXMAN_a8b8g8r8 =	PIXMAN_FORMAT(32,PIXMAN_TYPE_ABGR,8,8,8,8),
    PIXMAN_x8b8g8r8 =	PIXMAN_FORMAT(32,PIXMAN_TYPE_ABGR,0,8,8,8),
    
/* 24bpp formats */
    PIXMAN_r8g8b8 =	PIXMAN_FORMAT(24,PIXMAN_TYPE_ARGB,0,8,8,8),
    PIXMAN_b8g8r8 =	PIXMAN_FORMAT(24,PIXMAN_TYPE_ABGR,0,8,8,8),
    
/* 16bpp formats */
    PIXMAN_r5g6b5 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ARGB,0,5,6,5),
    PIXMAN_b5g6r5 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ABGR,0,5,6,5),
    
    PIXMAN_a1r5g5b5 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ARGB,1,5,5,5),
    PIXMAN_x1r5g5b5 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ARGB,0,5,5,5),
    PIXMAN_a1b5g5r5 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ABGR,1,5,5,5),
    PIXMAN_x1b5g5r5 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ABGR,0,5,5,5),
    PIXMAN_a4r4g4b4 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ARGB,4,4,4,4),
    PIXMAN_x4r4g4b4 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ARGB,0,4,4,4),
    PIXMAN_a4b4g4r4 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ABGR,4,4,4,4),
    PIXMAN_x4b4g4r4 =	PIXMAN_FORMAT(16,PIXMAN_TYPE_ABGR,0,4,4,4),
    
/* 8bpp formats */
    PIXMAN_a8 =		PIXMAN_FORMAT(8,PIXMAN_TYPE_A,8,0,0,0),
    PIXMAN_r3g3b2 =	PIXMAN_FORMAT(8,PIXMAN_TYPE_ARGB,0,3,3,2),
    PIXMAN_b2g3r3 =	PIXMAN_FORMAT(8,PIXMAN_TYPE_ABGR,0,3,3,2),
    PIXMAN_a2r2g2b2 =	PIXMAN_FORMAT(8,PIXMAN_TYPE_ARGB,2,2,2,2),
    PIXMAN_a2b2g2r2 =	PIXMAN_FORMAT(8,PIXMAN_TYPE_ABGR,2,2,2,2),
    
    PIXMAN_c8 =		PIXMAN_FORMAT(8,PIXMAN_TYPE_COLOR,0,0,0,0),
    PIXMAN_g8 =		PIXMAN_FORMAT(8,PIXMAN_TYPE_GRAY,0,0,0,0),
    
    PIXMAN_x4a4 =	PIXMAN_FORMAT(8,PIXMAN_TYPE_A,4,0,0,0),
    
    PIXMAN_x4c4 =	PIXMAN_FORMAT(8,PIXMAN_TYPE_COLOR,0,0,0,0),
    PIXMAN_x4g4 =	PIXMAN_FORMAT(8,PIXMAN_TYPE_GRAY,0,0,0,0),
    
/* 4bpp formats */
    PIXMAN_a4 =		PIXMAN_FORMAT(4,PIXMAN_TYPE_A,4,0,0,0),
    PIXMAN_r1g2b1 =	PIXMAN_FORMAT(4,PIXMAN_TYPE_ARGB,0,1,2,1),
    PIXMAN_b1g2r1 =	PIXMAN_FORMAT(4,PIXMAN_TYPE_ABGR,0,1,2,1),
    PIXMAN_a1r1g1b1 =	PIXMAN_FORMAT(4,PIXMAN_TYPE_ARGB,1,1,1,1),
    PIXMAN_a1b1g1r1 =	PIXMAN_FORMAT(4,PIXMAN_TYPE_ABGR,1,1,1,1),
    
    PIXMAN_c4 =		PIXMAN_FORMAT(4,PIXMAN_TYPE_COLOR,0,0,0,0),
    PIXMAN_g4 =		PIXMAN_FORMAT(4,PIXMAN_TYPE_GRAY,0,0,0,0),
    
/* 1bpp formats */
    PIXMAN_a1 =		PIXMAN_FORMAT(1,PIXMAN_TYPE_A,1,0,0,0),
    
    PIXMAN_g1 =		PIXMAN_FORMAT(1,PIXMAN_TYPE_GRAY,0,0,0,0),
} pixman_format_code_t;

typedef struct
{
    /* All of this struct is considered private to libcomp */
    unsigned char	reserved [128];
} pixman_image_t;

/* Initialize */
void pixman_image_init_solid_fill       (pixman_image_t       *image,
					 pixman_color_t       *color,
					 int                  *error);
void pixman_image_init_linear_gradient  (pixman_image_t       *image,
					 pixman_point_fixed_t *p1,
					 pixman_point_fixed_t *p2,
					 int                   n_stops,
					 pixman_fixed_t       *stops,
					 pixman_color_t       *colors,
					 int                  *error);
void pixman_image_init_radial_gradient  (pixman_image_t       *image,
					 pixman_point_fixed_t *inner,
					 pixman_point_fixed_t *outer,
					 pixman_fixed_t        inner_radius,
					 pixman_fixed_t        outer_radius,
					 int                   n_stops,
					 pixman_fixed_t       *stops,
					 pixman_color_t       *colors,
					 int                  *error);
void pixman_image_init_conical_gradient (pixman_image_t       *image,
					 pixman_point_fixed_t *center,
					 pixman_fixed_t        angle,
					 int                   n_stops,
					 pixman_fixed_t       *stops,
					 pixman_color_t       *colors,
					 int                  *error);
void pixman_image_init_bits             (pixman_image_t       *image,
					 pixman_format_code_t  format,
					 int                   width,
					 int                   height,
					 uint32_t             *bits,
					 int                   rowstride_bytes);
/* Set properties */
void pixman_image_set_clip_region       (pixman_image_t       *image,
					 pixman_region16_t    *region);
void pixman_image_set_transform         (pixman_image_t       *image,
					 pixman_transform_t   *transform);
void pixman_image_set_repeat            (pixman_image_t       *image,
					 pixman_repeat_t       repeat);
void pixman_image_set_filter            (pixman_image_t       *image,
					 pixman_filter_t       filter);
void pixman_image_set_filter_params     (pixman_image_t       *image,
					 pixman_fixed_t       *params,
					 int                   n_params);
void pixman_image_set_alpha_map         (pixman_image_t       *image,
					 pixman_image_t       *alpha_map,
					 int16_t               x,
					 int16_t               y);
void pixman_image_set_component_alpha   (pixman_image_t       *image,
					 pixman_bool_t         component_alpha);

/* Composite */
void pixman_image_composite		(pixman_op_t	       op,
					 pixman_image_t	      *src,
					 pixman_image_t	      *mask,
					 pixman_image_t	      *dest,
					 int		       src_x,
					 int		       src_y,
					 int		       mask_x,
					 int		       mask_y,
					 int			dest_x,
					 int			dest_y,
					 int			width,
					 int			height);



#endif /* PIXMAN_H__ */
