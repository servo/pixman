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

#include <config.h>

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

typedef int pixman_bool_t;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

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
void pixman_region_init              (pixman_region16_t *region);
void pixman_region_init_rect         (pixman_region16_t *region,
				      int                x,
				      int                y,
				      unsigned int       width,
				      unsigned int       height);
void pixman_region_init_with_extents (pixman_region16_t *region,
				      pixman_box16_t    *extents);
void pixman_region_fini              (pixman_region16_t *region);

/* manipulation */
void          pixman_region_translate  (pixman_region16_t *region,
					int                x,
					int                y);
pixman_bool_t pixman_region_copy       (pixman_region16_t *dest,
					pixman_region16_t *source);
pixman_bool_t pixman_region_intersect  (pixman_region16_t *newReg,
					pixman_region16_t *reg1,
					pixman_region16_t *reg2);
pixman_bool_t pixman_region_union      (pixman_region16_t *newReg,
					pixman_region16_t *reg1,
					pixman_region16_t *reg2);
pixman_bool_t pixman_region_union_rect (pixman_region16_t *dest,
					pixman_region16_t *source,
					int                x,
					int                y,
					unsigned int       width,
					unsigned int       height);
pixman_bool_t pixman_region_subtract   (pixman_region16_t *regD,
					pixman_region16_t *regM,
					pixman_region16_t *regS);
pixman_bool_t pixman_region_inverse    (pixman_region16_t *newReg,
					pixman_region16_t *reg1,
					pixman_box16_t    *invRect);


/*
 * Images
 */
/*
 * While the protocol is generous in format support, the
 * sample implementation allows only packed RGB and GBR
 * representations for data to simplify software rendering,
 */
#define PIXMAN_FORMAT(bpp,type,a,r,g,b)	(((bpp) << 24) |  \
					 ((type) << 16) | \
					 ((a) << 12) | \
					 ((r) << 8) | \
					 ((g) << 4) | \
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
    unsigned char	reserved [64];
} pixman_image_t;

void pixman_image_init      (pixman_image_t       *image,
			     pixman_format_code_t  format,
			     int                   width,
			     int                   height,
			     uint8_t              *bits,
			     int                   rowstride); /* in bytes */
void pixman_set_clip_region (pixman_image_t       *image,
			     pixman_region16_t    *region);


#endif /* PIXMAN_H__ */
