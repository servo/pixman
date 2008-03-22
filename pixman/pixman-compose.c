/*
 *
 * Copyright © 2000 Keith Packard, member of The XFree86 Project, Inc.
 *             2005 Lars Knoll & Zack Rusin, Trolltech
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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>

#include "pixman-private.h"

#ifdef PIXMAN_FB_ACCESSORS
#define PIXMAN_COMPOSITE_RECT_GENERAL pixman_composite_rect_general_accessors
#define PIXMAN_COMPOSE_FUNCTIONS pixman_composeFunctions_accessors
#define FETCH_PROC_FOR_PICTURE pixman_fetchProcForPicture_accessors
#define FETCH_PIXEL_PROC_FOR_PICTURE pixman_fetchPixelProcForPicture_accessors
#define STORE_PROC_FOR_PICTURE pixman_storeProcForPicture_accessors
#else
#define PIXMAN_COMPOSITE_RECT_GENERAL pixman_composite_rect_general_no_accessors
#define PIXMAN_COMPOSE_FUNCTIONS pixman_composeFunctions
#define FETCH_PROC_FOR_PICTURE pixman_fetchProcForPicture
#define FETCH_PIXEL_PROC_FOR_PICTURE pixman_fetchPixelProcForPicture
#define STORE_PROC_FOR_PICTURE pixman_storeProcForPicture
#endif

static unsigned int
SourcePictureClassify (source_image_t *pict,
		       int	       x,
		       int	       y,
		       int	       width,
		       int	       height)
{
    if (pict->common.type == SOLID)
    {
	pict->class = SOURCE_IMAGE_CLASS_HORIZONTAL;
    }
    else if (pict->common.type == LINEAR)
    {
	linear_gradient_t *linear = (linear_gradient_t *)pict;
	pixman_vector_t   v;
	pixman_fixed_32_32_t l;
	pixman_fixed_48_16_t dx, dy, a, b, off;
	pixman_fixed_48_16_t factors[4];
	int	     i;

	dx = linear->p2.x - linear->p1.x;
	dy = linear->p2.y - linear->p1.y;
	l = dx * dx + dy * dy;
	if (l)
	{
	    a = (dx << 32) / l;
	    b = (dy << 32) / l;
	}
	else
	{
	    a = b = 0;
	}

	off = (-a * linear->p1.x
	       -b * linear->p1.y) >> 16;

	for (i = 0; i < 3; i++)
	{
	    v.vector[0] = pixman_int_to_fixed ((i % 2) * (width  - 1) + x);
	    v.vector[1] = pixman_int_to_fixed ((i / 2) * (height - 1) + y);
	    v.vector[2] = pixman_fixed_1;

	    if (pict->common.transform)
	    {
		if (!pixman_transform_point_3d (pict->common.transform, &v))
		    return SOURCE_IMAGE_CLASS_UNKNOWN;
	    }

	    factors[i] = ((a * v.vector[0] + b * v.vector[1]) >> 16) + off;
	}

	if (factors[2] == factors[0])
	    pict->class = SOURCE_IMAGE_CLASS_HORIZONTAL;
	else if (factors[1] == factors[0])
	    pict->class = SOURCE_IMAGE_CLASS_VERTICAL;
    }

    return pict->class;
}

#define SCANLINE_BUFFER_LENGTH 2048

static void fbFetchSolid(bits_image_t * pict, int x, int y, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits)
{
    uint32_t color;
    uint32_t *end;
    fetchPixelProc fetch = FETCH_PIXEL_PROC_FOR_PICTURE(pict);

    color = fetch(pict, 0, 0);

    end = buffer + width;
    while (buffer < end)
	*(buffer++) = color;
}

static void fbFetch(bits_image_t * pict, int x, int y, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits)
{
    fetchProc fetch = FETCH_PROC_FOR_PICTURE(pict);

    fetch(pict, x, y, width, buffer);
}

#ifdef PIXMAN_FB_ACCESSORS	/* The accessor version can't be parameterized from outside */
static const
#endif
FbComposeFunctions PIXMAN_COMPOSE_FUNCTIONS = {
    pixman_fbCombineFuncU,
    pixman_fbCombineFuncC,
    pixman_fbCombineMaskU
};

/*
 * Fetch from region strategies
 */
typedef FASTCALL uint32_t (*fetchFromRegionProc)(bits_image_t *pict, int x, int y, uint32_t *buffer, fetchPixelProc fetch, pixman_box16_t *box);

static inline uint32_t
fbFetchFromNoRegion(bits_image_t *pict, int x, int y, uint32_t *buffer, fetchPixelProc fetch, pixman_box16_t *box)
{
    return fetch (pict, x, y);
}

static uint32_t
fbFetchFromNRectangles(bits_image_t *pict, int x, int y, uint32_t *buffer, fetchPixelProc fetch, pixman_box16_t *box)
{
    pixman_box16_t box2;
    if (pixman_region_contains_point (pict->common.src_clip, x, y, &box2))
        return fbFetchFromNoRegion(pict, x, y, buffer, fetch, box);
    else
        return 0;
}

static uint32_t
fbFetchFromOneRectangle(bits_image_t *pict, int x, int y, uint32_t *buffer, fetchPixelProc fetch, pixman_box16_t *box)
{
    pixman_box16_t box2 = *box;
    return ((x < box2.x1) | (x >= box2.x2) | (y < box2.y1) | (y >= box2.y2)) ?
        0 : fbFetchFromNoRegion(pict, x, y, buffer, fetch, box);
}

/*
 * Fetching Algorithms
 */
static void
fbFetchTransformed_Nearest_Normal(bits_image_t * pict, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits, pixman_bool_t affine, pixman_vector_t v, pixman_vector_t unit)
{
    pixman_box16_t* box = NULL;
    fetchPixelProc   fetch;
    fetchFromRegionProc fetchFromRegion;
    int x, y, i;

    /* initialize the two function pointers */
    fetch = FETCH_PIXEL_PROC_FOR_PICTURE(pict);

    if(pixman_region_n_rects (pict->common.src_clip) == 1)
        fetchFromRegion = fbFetchFromNoRegion;
    else
        fetchFromRegion = fbFetchFromNRectangles;

    for ( i = 0; i < width; ++i)
    {
        if (!mask || mask[i] & maskBits)
        {
            if (!v.vector[2])
            {
                *(buffer + i) = 0;
            }
            else
            {
                if (!affine)
                {
                    y = MOD(DIV(v.vector[1],v.vector[2]), pict->height);
                    x = MOD(DIV(v.vector[0],v.vector[2]), pict->width);
                }
                else
                {
                    y = MOD(v.vector[1]>>16, pict->height);
                    x = MOD(v.vector[0]>>16, pict->width);
                }
                *(buffer + i) = fetchFromRegion(pict, x, y, buffer, fetch, box);
            }
        }

        v.vector[0] += unit.vector[0];
        v.vector[1] += unit.vector[1];
        v.vector[2] += unit.vector[2];
    }
}

static void
fbFetchTransformed_Nearest_Pad(bits_image_t * pict, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits, pixman_bool_t affine, pixman_vector_t v, pixman_vector_t unit)
{
    pixman_box16_t *box = NULL;
    fetchPixelProc   fetch;
    fetchFromRegionProc fetchFromRegion;
    int x, y, i;

    /* initialize the two function pointers */
    fetch = FETCH_PIXEL_PROC_FOR_PICTURE(pict);

    if(pixman_region_n_rects (pict->common.src_clip) == 1)
        fetchFromRegion = fbFetchFromNoRegion;
    else
        fetchFromRegion = fbFetchFromNRectangles;

    for (i = 0; i < width; ++i)
    {
        if (!mask || mask[i] & maskBits)
        {
            if (!v.vector[2])
            {
                *(buffer + i) = 0;
            }
            else
            {
                if (!affine)
                {
                    y = CLIP(DIV(v.vector[1], v.vector[2]), 0, pict->height-1);
                    x = CLIP(DIV(v.vector[0], v.vector[2]), 0, pict->width-1);
                }
                else
                {
                    y = CLIP(v.vector[1]>>16, 0, pict->height-1);
                    x = CLIP(v.vector[0]>>16, 0, pict->width-1);
                }

                *(buffer + i) = fetchFromRegion(pict, x, y, buffer, fetch, box);
            }
        }

        v.vector[0] += unit.vector[0];
        v.vector[1] += unit.vector[1];
        v.vector[2] += unit.vector[2];
    }
}

static void
fbFetchTransformed_Nearest_General(bits_image_t * pict, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits, pixman_bool_t affine, pixman_vector_t v, pixman_vector_t unit)
{
    pixman_box16_t *box = NULL;
    fetchPixelProc   fetch;
    fetchFromRegionProc fetchFromRegion;
    int x, y, i;

    /* initialize the two function pointers */
    fetch = FETCH_PIXEL_PROC_FOR_PICTURE(pict);

    if(pixman_region_n_rects (pict->common.src_clip) == 1)
    {
        box = &(pict->common.src_clip->extents);
        fetchFromRegion = fbFetchFromOneRectangle;
    }
    else
    {
        fetchFromRegion = fbFetchFromNRectangles;
    }

    for (i = 0; i < width; ++i) {
        if (!mask || mask[i] & maskBits)
        {
            if (!v.vector[2]) {
                *(buffer + i) = 0;
            } else {
                if (!affine) {
                    y = DIV(v.vector[1],v.vector[2]);
                    x = DIV(v.vector[0],v.vector[2]);
                } else {
                    y = v.vector[1]>>16;
                    x = v.vector[0]>>16;
                }
                *(buffer + i) = fetchFromRegion(pict, x, y, buffer, fetch, box);
            }
        }
        v.vector[0] += unit.vector[0];
        v.vector[1] += unit.vector[1];
        v.vector[2] += unit.vector[2];
    }
}

static void
fbFetchTransformed_Bilinear_Normal(bits_image_t * pict, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits, pixman_bool_t affine, pixman_vector_t v, pixman_vector_t unit)
{
    pixman_box16_t *box = NULL;
    fetchPixelProc   fetch;
    fetchFromRegionProc fetchFromRegion;
    int i;

    /* initialize the two function pointers */
    fetch = FETCH_PIXEL_PROC_FOR_PICTURE(pict);

    if(pixman_region_n_rects (pict->common.src_clip) == 1)
        fetchFromRegion = fbFetchFromNoRegion;
    else
        fetchFromRegion = fbFetchFromNRectangles;

    for (i = 0; i < width; ++i) {
        if (!mask || mask[i] & maskBits)
        {
            if (!v.vector[2]) {
                *(buffer + i) = 0;
            } else {
                int x1, x2, y1, y2, distx, idistx, disty, idisty;
                uint32_t tl, tr, bl, br, r;
                uint32_t ft, fb;

                if (!affine) {
                    pixman_fixed_48_16_t div;
                    div = ((pixman_fixed_48_16_t)v.vector[0] << 16)/v.vector[2];
                    x1 = div >> 16;
                    distx = ((pixman_fixed_t)div >> 8) & 0xff;
                    div = ((pixman_fixed_48_16_t)v.vector[1] << 16)/v.vector[2];
                    y1 = div >> 16;
                    disty = ((pixman_fixed_t)div >> 8) & 0xff;
                } else {
                    x1 = v.vector[0] >> 16;
                    distx = (v.vector[0] >> 8) & 0xff;
                    y1 = v.vector[1] >> 16;
                    disty = (v.vector[1] >> 8) & 0xff;
                }
                x2 = x1 + 1;
                y2 = y1 + 1;

                idistx = 256 - distx;
                idisty = 256 - disty;

                x1 = MOD (x1, pict->width);
                x2 = MOD (x2, pict->width);
                y1 = MOD (y1, pict->height);
                y2 = MOD (y2, pict->height);

                tl = fetchFromRegion(pict, x1, y1, buffer, fetch, box);
                tr = fetchFromRegion(pict, x2, y1, buffer, fetch, box);
                bl = fetchFromRegion(pict, x1, y2, buffer, fetch, box);
                br = fetchFromRegion(pict, x2, y2, buffer, fetch, box);

                ft = FbGet8(tl,0) * idistx + FbGet8(tr,0) * distx;
                fb = FbGet8(bl,0) * idistx + FbGet8(br,0) * distx;
                r = (((ft * idisty + fb * disty) >> 16) & 0xff);
                ft = FbGet8(tl,8) * idistx + FbGet8(tr,8) * distx;
                fb = FbGet8(bl,8) * idistx + FbGet8(br,8) * distx;
                r |= (((ft * idisty + fb * disty) >> 8) & 0xff00);
                ft = FbGet8(tl,16) * idistx + FbGet8(tr,16) * distx;
                fb = FbGet8(bl,16) * idistx + FbGet8(br,16) * distx;
                r |= (((ft * idisty + fb * disty)) & 0xff0000);
                ft = FbGet8(tl,24) * idistx + FbGet8(tr,24) * distx;
                fb = FbGet8(bl,24) * idistx + FbGet8(br,24) * distx;
                r |= (((ft * idisty + fb * disty) << 8) & 0xff000000);
                *(buffer + i) = r;
            }
        }
        v.vector[0] += unit.vector[0];
        v.vector[1] += unit.vector[1];
        v.vector[2] += unit.vector[2];
    }
}

static void
fbFetchTransformed_Bilinear_Pad(bits_image_t * pict, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits, pixman_bool_t affine, pixman_vector_t v, pixman_vector_t unit)
{
    pixman_box16_t *box = NULL;
    fetchPixelProc   fetch;
    fetchFromRegionProc fetchFromRegion;
    int i;

    /* initialize the two function pointers */
    fetch = FETCH_PIXEL_PROC_FOR_PICTURE(pict);

    if(pixman_region_n_rects (pict->common.src_clip) == 1)
        fetchFromRegion = fbFetchFromNoRegion;
    else
        fetchFromRegion = fbFetchFromNRectangles;

    for (i = 0; i < width; ++i) {
        if (!mask || mask[i] & maskBits)
        {
            if (!v.vector[2]) {
                *(buffer + i) = 0;
            } else {
                int x1, x2, y1, y2, distx, idistx, disty, idisty;
                uint32_t tl, tr, bl, br, r;
                uint32_t ft, fb;

                if (!affine) {
                    pixman_fixed_48_16_t div;
                    div = ((pixman_fixed_48_16_t)v.vector[0] << 16)/v.vector[2];
                    x1 = div >> 16;
                    distx = ((pixman_fixed_t)div >> 8) & 0xff;
                    div = ((pixman_fixed_48_16_t)v.vector[1] << 16)/v.vector[2];
                    y1 = div >> 16;
                    disty = ((pixman_fixed_t)div >> 8) & 0xff;
                } else {
                    x1 = v.vector[0] >> 16;
                    distx = (v.vector[0] >> 8) & 0xff;
                    y1 = v.vector[1] >> 16;
                    disty = (v.vector[1] >> 8) & 0xff;
                }
                x2 = x1 + 1;
                y2 = y1 + 1;

                idistx = 256 - distx;
                idisty = 256 - disty;

                x1 = CLIP (x1, 0, pict->width-1);
                x2 = CLIP (x2, 0, pict->width-1);
                y1 = CLIP (y1, 0, pict->height-1);
                y2 = CLIP (y2, 0, pict->height-1);

                tl = fetchFromRegion(pict, x1, y1, buffer, fetch, box);
                tr = fetchFromRegion(pict, x2, y1, buffer, fetch, box);
                bl = fetchFromRegion(pict, x1, y2, buffer, fetch, box);
                br = fetchFromRegion(pict, x2, y2, buffer, fetch, box);

                ft = FbGet8(tl,0) * idistx + FbGet8(tr,0) * distx;
                fb = FbGet8(bl,0) * idistx + FbGet8(br,0) * distx;
                r = (((ft * idisty + fb * disty) >> 16) & 0xff);
                ft = FbGet8(tl,8) * idistx + FbGet8(tr,8) * distx;
                fb = FbGet8(bl,8) * idistx + FbGet8(br,8) * distx;
                r |= (((ft * idisty + fb * disty) >> 8) & 0xff00);
                ft = FbGet8(tl,16) * idistx + FbGet8(tr,16) * distx;
                fb = FbGet8(bl,16) * idistx + FbGet8(br,16) * distx;
                r |= (((ft * idisty + fb * disty)) & 0xff0000);
                ft = FbGet8(tl,24) * idistx + FbGet8(tr,24) * distx;
                fb = FbGet8(bl,24) * idistx + FbGet8(br,24) * distx;
                r |= (((ft * idisty + fb * disty) << 8) & 0xff000000);
                *(buffer + i) = r;
            }
        }
        v.vector[0] += unit.vector[0];
        v.vector[1] += unit.vector[1];
        v.vector[2] += unit.vector[2];
    }
}

static void
fbFetchTransformed_Bilinear_General(bits_image_t * pict, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits, pixman_bool_t affine, pixman_vector_t v, pixman_vector_t unit)
{
    pixman_box16_t *box = NULL;
    fetchPixelProc   fetch;
    fetchFromRegionProc fetchFromRegion;
    int i;

    /* initialize the two function pointers */
    fetch = FETCH_PIXEL_PROC_FOR_PICTURE(pict);

    if(pixman_region_n_rects (pict->common.src_clip) == 1)
    {
        box = &(pict->common.src_clip->extents);
        fetchFromRegion = fbFetchFromOneRectangle;
    }
    else
    {
        fetchFromRegion = fbFetchFromNRectangles;
    }

    for (i = 0; i < width; ++i)
    {
        if (!mask || mask[i] & maskBits)
        {
            if (!v.vector[2]) {
                *(buffer + i) = 0;
            } else {
                int x1, x2, y1, y2, distx, idistx, disty, idisty;
                uint32_t tl, tr, bl, br, r;
                uint32_t ft, fb;

                if (!affine) {
                    pixman_fixed_48_16_t div;
                    div = ((pixman_fixed_48_16_t)v.vector[0] << 16)/v.vector[2];
                    x1 = div >> 16;
                    distx = ((pixman_fixed_t)div >> 8) & 0xff;
                    div = ((pixman_fixed_48_16_t)v.vector[1] << 16)/v.vector[2];
                    y1 = div >> 16;
                    disty = ((pixman_fixed_t)div >> 8) & 0xff;
                } else {
                    x1 = v.vector[0] >> 16;
                    distx = (v.vector[0] >> 8) & 0xff;
                    y1 = v.vector[1] >> 16;
                    disty = (v.vector[1] >> 8) & 0xff;
                }
                x2 = x1 + 1;
                y2 = y1 + 1;

                idistx = 256 - distx;
                idisty = 256 - disty;

                tl = fetchFromRegion(pict, x1, y1, buffer, fetch, box);
                tr = fetchFromRegion(pict, x2, y1, buffer, fetch, box);
                bl = fetchFromRegion(pict, x1, y2, buffer, fetch, box);
                br = fetchFromRegion(pict, x2, y2, buffer, fetch, box);

                ft = FbGet8(tl,0) * idistx + FbGet8(tr,0) * distx;
                fb = FbGet8(bl,0) * idistx + FbGet8(br,0) * distx;
                r = (((ft * idisty + fb * disty) >> 16) & 0xff);
                ft = FbGet8(tl,8) * idistx + FbGet8(tr,8) * distx;
                fb = FbGet8(bl,8) * idistx + FbGet8(br,8) * distx;
                r |= (((ft * idisty + fb * disty) >> 8) & 0xff00);
                ft = FbGet8(tl,16) * idistx + FbGet8(tr,16) * distx;
                fb = FbGet8(bl,16) * idistx + FbGet8(br,16) * distx;
                r |= (((ft * idisty + fb * disty)) & 0xff0000);
                ft = FbGet8(tl,24) * idistx + FbGet8(tr,24) * distx;
                fb = FbGet8(bl,24) * idistx + FbGet8(br,24) * distx;
                r |= (((ft * idisty + fb * disty) << 8) & 0xff000000);
                *(buffer + i) = r;
            }
        }

        v.vector[0] += unit.vector[0];
        v.vector[1] += unit.vector[1];
        v.vector[2] += unit.vector[2];
    }
}

static void
fbFetchTransformed_Convolution(bits_image_t * pict, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits, pixman_bool_t affine, pixman_vector_t v, pixman_vector_t unit)
{
    pixman_box16_t dummy;
    fetchPixelProc fetch;
    int i;

    pixman_fixed_t *params = pict->common.filter_params;
    int32_t cwidth = pixman_fixed_to_int(params[0]);
    int32_t cheight = pixman_fixed_to_int(params[1]);
    int xoff = (params[0] - pixman_fixed_1) >> 1;
    int yoff = (params[1] - pixman_fixed_1) >> 1;
    fetch = FETCH_PIXEL_PROC_FOR_PICTURE(pict);

    params += 2;
    for (i = 0; i < width; ++i) {
        if (!mask || mask[i] & maskBits)
        {
            if (!v.vector[2]) {
                *(buffer + i) = 0;
            } else {
                int x1, x2, y1, y2, x, y;
                int32_t srtot, sgtot, sbtot, satot;
                pixman_fixed_t *p = params;

                if (!affine) {
                    pixman_fixed_48_16_t tmp;
                    tmp = ((pixman_fixed_48_16_t)v.vector[0] << 16)/v.vector[2] - xoff;
                    x1 = pixman_fixed_to_int(tmp);
                    tmp = ((pixman_fixed_48_16_t)v.vector[1] << 16)/v.vector[2] - yoff;
                    y1 = pixman_fixed_to_int(tmp);
                } else {
                    x1 = pixman_fixed_to_int(v.vector[0] - xoff);
                    y1 = pixman_fixed_to_int(v.vector[1] - yoff);
                }
                x2 = x1 + cwidth;
                y2 = y1 + cheight;

                srtot = sgtot = sbtot = satot = 0;

                for (y = y1; y < y2; y++) {
                    int ty;
                    switch (pict->common.repeat) {
                        case PIXMAN_REPEAT_NORMAL:
                            ty = MOD (y, pict->height);
                            break;
                        case PIXMAN_REPEAT_PAD:
                            ty = CLIP (y, 0, pict->height-1);
                            break;
                        default:
                            ty = y;
                    }
                    for (x = x1; x < x2; x++) {
                        if (*p) {
                            int tx;
                            switch (pict->common.repeat) {
                                case PIXMAN_REPEAT_NORMAL:
                                    tx = MOD (x, pict->width);
                                    break;
                                case PIXMAN_REPEAT_PAD:
                                    tx = CLIP (x, 0, pict->width-1);
                                    break;
                                default:
                                    tx = x;
                            }
                            if (pixman_region_contains_point (pict->common.src_clip, tx, ty, &dummy)) {
                                uint32_t c = fetch(pict, tx, ty);

                                srtot += Red(c) * *p;
                                sgtot += Green(c) * *p;
                                sbtot += Blue(c) * *p;
                                satot += Alpha(c) * *p;
                            }
                        }
                        p++;
                    }
                }

                satot >>= 16;
                srtot >>= 16;
                sgtot >>= 16;
                sbtot >>= 16;

                if (satot < 0) satot = 0; else if (satot > 0xff) satot = 0xff;
                if (srtot < 0) srtot = 0; else if (srtot > 0xff) srtot = 0xff;
                if (sgtot < 0) sgtot = 0; else if (sgtot > 0xff) sgtot = 0xff;
                if (sbtot < 0) sbtot = 0; else if (sbtot > 0xff) sbtot = 0xff;

                *(buffer + i) = ((satot << 24) |
                                 (srtot << 16) |
                                 (sgtot <<  8) |
                                 (sbtot       ));
            }
        }
        v.vector[0] += unit.vector[0];
        v.vector[1] += unit.vector[1];
        v.vector[2] += unit.vector[2];
    }
}

static void
adjust (pixman_vector_t *v, pixman_vector_t *u, pixman_fixed_t adjustment)
{
    int delta_v = (adjustment * v->vector[2]) >> 16;
    int delta_u = (adjustment * u->vector[2]) >> 16;
    
    v->vector[0] += delta_v;
    v->vector[1] += delta_v;
    
    u->vector[0] += delta_u;
    u->vector[1] += delta_u;
}

static void
fbFetchTransformed(bits_image_t * pict, int x, int y, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits)
{
    uint32_t     *bits;
    int32_t    stride;
    pixman_vector_t v;
    pixman_vector_t unit;
    pixman_bool_t affine = TRUE;

    bits = pict->bits;
    stride = pict->rowstride;

    /* reference point is the center of the pixel */
    v.vector[0] = pixman_int_to_fixed(x) + pixman_fixed_1 / 2;
    v.vector[1] = pixman_int_to_fixed(y) + pixman_fixed_1 / 2;
    v.vector[2] = pixman_fixed_1;

    /* when using convolution filters or PIXMAN_REPEAT_PAD one might get here without a transform */
    if (pict->common.transform)
    {
        if (!pixman_transform_point_3d (pict->common.transform, &v))
            return;
        unit.vector[0] = pict->common.transform->matrix[0][0];
        unit.vector[1] = pict->common.transform->matrix[1][0];
        unit.vector[2] = pict->common.transform->matrix[2][0];
        affine = v.vector[2] == pixman_fixed_1 && unit.vector[2] == 0;
    }
    else
    {
        unit.vector[0] = pixman_fixed_1;
        unit.vector[1] = 0;
        unit.vector[2] = 0;
    }

    /* This allows filtering code to pretend that pixels are located at integer coordinates */
    adjust (&v, &unit, -(pixman_fixed_1 / 2));
    
    if (pict->common.filter == PIXMAN_FILTER_NEAREST || pict->common.filter == PIXMAN_FILTER_FAST)
    {
	/* Round down to closest integer, ensuring that 0.5 rounds to 0, not 1 */
	adjust (&v, &unit, pixman_fixed_1 / 2 - pixman_fixed_e);
	
        if (pict->common.repeat == PIXMAN_REPEAT_NORMAL)
        {
            fbFetchTransformed_Nearest_Normal(pict, width, buffer, mask, maskBits, affine, v, unit);

        }
        else if (pict->common.repeat == PIXMAN_REPEAT_PAD)
        {
            fbFetchTransformed_Nearest_Pad(pict, width, buffer, mask, maskBits, affine, v, unit);
        }
        else
        {
            fbFetchTransformed_Nearest_General(pict, width, buffer, mask, maskBits, affine, v, unit);
        }
    } else if (pict->common.filter == PIXMAN_FILTER_BILINEAR	||
	       pict->common.filter == PIXMAN_FILTER_GOOD	||
	       pict->common.filter == PIXMAN_FILTER_BEST)
    {
        if (pict->common.repeat == PIXMAN_REPEAT_NORMAL)
        {
            fbFetchTransformed_Bilinear_Normal(pict, width, buffer, mask, maskBits, affine, v, unit);
        }
        else if (pict->common.repeat == PIXMAN_REPEAT_PAD)
        {
            fbFetchTransformed_Bilinear_Pad(pict, width, buffer, mask, maskBits, affine, v, unit);
        }
        else
        {
            fbFetchTransformed_Bilinear_General(pict, width, buffer, mask, maskBits, affine, v, unit);
        }
    }
    else if (pict->common.filter == PIXMAN_FILTER_CONVOLUTION)
    {
	/* Round to closest integer, ensuring that 0.5 rounds to 0, not 1 */
	adjust (&v, &unit, pixman_fixed_1 / 2 - pixman_fixed_e);
	
        fbFetchTransformed_Convolution(pict, width, buffer, mask, maskBits, affine, v, unit);
    }
}


static void
fbFetchExternalAlpha(bits_image_t * pict, int x, int y, int width, uint32_t *buffer, uint32_t *mask, uint32_t maskBits)
{
    int i;
    uint32_t _alpha_buffer[SCANLINE_BUFFER_LENGTH];
    uint32_t *alpha_buffer = _alpha_buffer;

    if (!pict->common.alpha_map) {
        fbFetchTransformed (pict, x, y, width, buffer, mask, maskBits);
	return;
    }
    if (width > SCANLINE_BUFFER_LENGTH)
        alpha_buffer = (uint32_t *) pixman_malloc_ab (width, sizeof(uint32_t));

    fbFetchTransformed(pict, x, y, width, buffer, mask, maskBits);
    fbFetchTransformed((bits_image_t *)pict->common.alpha_map, x - pict->common.alpha_origin.x,
		       y - pict->common.alpha_origin.y, width, alpha_buffer,
		       mask, maskBits);
    for (i = 0; i < width; ++i) {
        if (!mask || mask[i] & maskBits)
	{
	    int a = alpha_buffer[i]>>24;
	    *(buffer + i) = (a << 24)
		| (div_255(Red(*(buffer + i)) * a) << 16)
		| (div_255(Green(*(buffer + i)) * a) << 8)
		| (div_255(Blue(*(buffer + i)) * a));
	}
    }

    if (alpha_buffer != _alpha_buffer)
        free(alpha_buffer);
}

static void
fbStore(bits_image_t * pict, int x, int y, int width, uint32_t *buffer)
{
    uint32_t *bits;
    int32_t stride;
    storeProc store = STORE_PROC_FOR_PICTURE(pict);
    const pixman_indexed_t * indexed = pict->indexed;

    bits = pict->bits;
    stride = pict->rowstride;
    bits += y*stride;
    store((pixman_image_t *)pict, bits, buffer, x, width, indexed);
}

static void
fbStoreExternalAlpha(bits_image_t * pict, int x, int y, int width, uint32_t *buffer)
{
    uint32_t *bits, *alpha_bits;
    int32_t stride, astride;
    int ax, ay;
    storeProc store;
    storeProc astore;
    const pixman_indexed_t * indexed = pict->indexed;
    const pixman_indexed_t * aindexed;

    if (!pict->common.alpha_map) {
        fbStore(pict, x, y, width, buffer);
	return;
    }

    store = STORE_PROC_FOR_PICTURE(pict);
    astore = STORE_PROC_FOR_PICTURE(pict->common.alpha_map);
    aindexed = pict->common.alpha_map->indexed;

    ax = x;
    ay = y;

    bits = pict->bits;
    stride = pict->rowstride;

    alpha_bits = pict->common.alpha_map->bits;
    astride = pict->common.alpha_map->rowstride;

    bits       += y*stride;
    alpha_bits += (ay - pict->common.alpha_origin.y)*astride;


    store((pixman_image_t *)pict, bits, buffer, x, width, indexed);
    astore((pixman_image_t *)pict->common.alpha_map,
	   alpha_bits, buffer, ax - pict->common.alpha_origin.x, width, aindexed);
}

typedef void (*scanStoreProc)(pixman_image_t *, int, int, int, uint32_t *);
typedef void (*scanFetchProc)(pixman_image_t *, int, int, int, uint32_t *,
			      uint32_t *, uint32_t);

#ifndef PIXMAN_FB_ACCESSORS
static
#endif
void
PIXMAN_COMPOSITE_RECT_GENERAL (const FbComposeData *data,
			       uint32_t *scanline_buffer)
{
    uint32_t *src_buffer = scanline_buffer;
    uint32_t *dest_buffer = src_buffer + data->width;
    int i;
    scanStoreProc store;
    scanFetchProc fetchSrc = NULL, fetchMask = NULL, fetchDest = NULL;
    unsigned int srcClass = SOURCE_IMAGE_CLASS_UNKNOWN;
    unsigned int maskClass = SOURCE_IMAGE_CLASS_UNKNOWN;
    uint32_t *bits;
    int32_t stride;
    int xoff, yoff;

    if (data->op == PIXMAN_OP_CLEAR)
        fetchSrc = NULL;
    else if (IS_SOURCE_IMAGE (data->src))
    {
	fetchSrc = (scanFetchProc)pixmanFetchSourcePict;
	srcClass = SourcePictureClassify ((source_image_t *)data->src,
					  data->xSrc, data->ySrc,
					  data->width, data->height);
    }
    else
    {
	bits_image_t *bits = (bits_image_t *)data->src;

	if (bits->common.alpha_map)
	{
	    fetchSrc = (scanFetchProc)fbFetchExternalAlpha;
	}
	else if ((bits->common.repeat == PIXMAN_REPEAT_NORMAL || bits->common.repeat == PIXMAN_REPEAT_PAD) &&
		 bits->width == 1 &&
		 bits->height == 1)
	{
	    fetchSrc = (scanFetchProc)fbFetchSolid;
	    srcClass = SOURCE_IMAGE_CLASS_HORIZONTAL;
	}
	else if (!bits->common.transform && bits->common.filter != PIXMAN_FILTER_CONVOLUTION
                && bits->common.repeat != PIXMAN_REPEAT_PAD)
	{
	    fetchSrc = (scanFetchProc)fbFetch;
	}
	else
	{
	    fetchSrc = (scanFetchProc)fbFetchTransformed;
	}
    }

    if (!data->mask || data->op == PIXMAN_OP_CLEAR)
    {
	fetchMask = NULL;
    }
    else
    {
	if (IS_SOURCE_IMAGE (data->mask))
	{
	    fetchMask = (scanFetchProc)pixmanFetchSourcePict;
	    maskClass = SourcePictureClassify ((source_image_t *)data->mask,
					       data->xMask, data->yMask,
					       data->width, data->height);
	}
	else
	{
	    bits_image_t *bits = (bits_image_t *)data->mask;

	    if (bits->common.alpha_map)
	    {
		fetchMask = (scanFetchProc)fbFetchExternalAlpha;
	    }
	    else if ((bits->common.repeat == PIXMAN_REPEAT_NORMAL || bits->common.repeat == PIXMAN_REPEAT_PAD) &&
		     bits->width == 1 && bits->height == 1)
	    {
		fetchMask = (scanFetchProc)fbFetchSolid;
		maskClass = SOURCE_IMAGE_CLASS_HORIZONTAL;
	    }
	    else if (!bits->common.transform && bits->common.filter != PIXMAN_FILTER_CONVOLUTION
                    && bits->common.repeat != PIXMAN_REPEAT_PAD)
		fetchMask = (scanFetchProc)fbFetch;
	    else
		fetchMask = (scanFetchProc)fbFetchTransformed;
	}
    }

    if (data->dest->common.alpha_map)
    {
	fetchDest = (scanFetchProc)fbFetchExternalAlpha;
	store = (scanStoreProc)fbStoreExternalAlpha;

	if (data->op == PIXMAN_OP_CLEAR || data->op == PIXMAN_OP_SRC)
	    fetchDest = NULL;
    }
    else
    {
	fetchDest = (scanFetchProc)fbFetch;
	store = (scanStoreProc)fbStore;

	switch (data->op)
	{
	case PIXMAN_OP_CLEAR:
	case PIXMAN_OP_SRC:
	    fetchDest = NULL;
#ifndef PIXMAN_FB_ACCESSORS
	    /* fall-through */
	case PIXMAN_OP_ADD:
	case PIXMAN_OP_OVER:
	    switch (data->dest->bits.format) {
	    case PIXMAN_a8r8g8b8:
	    case PIXMAN_x8r8g8b8:
		store = NULL;
		break;
	    default:
		break;
	    }
#endif
	    break;
	}
    }

    if (!store)
    {
	bits = data->dest->bits.bits;
	stride = data->dest->bits.rowstride;
	xoff = yoff = 0;
    }
    else
    {
	bits = NULL;
	stride = 0;
	xoff = yoff = 0;
    }

    if (fetchSrc		   &&
	fetchMask		   &&
	data->mask		   &&
	data->mask->common.type == BITS &&
	data->mask->common.component_alpha &&
	PIXMAN_FORMAT_RGB (data->mask->bits.format))
    {
	uint32_t *mask_buffer = dest_buffer + data->width;
	CombineFuncC compose = PIXMAN_COMPOSE_FUNCTIONS.combineC[data->op];
	if (!compose)
	    return;

	for (i = 0; i < data->height; ++i) {
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
		store (data->dest, data->xDest, data->yDest + i, data->width,
		       dest_buffer);
	    }
	    else
	    {
		/* blend */
		compose (bits + (data->yDest + i+ yoff) * stride +
			 data->xDest + xoff,
			 src_buffer, mask_buffer, data->width);
	    }
	}
    }
    else
    {
	uint32_t *src_mask_buffer = 0, *mask_buffer = 0;
	CombineFuncU compose = PIXMAN_COMPOSE_FUNCTIONS.combineU[data->op];
	if (!compose)
	    return;

	if (fetchMask)
	    mask_buffer = dest_buffer + data->width;

	for (i = 0; i < data->height; ++i) {
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

		    if (mask_buffer)
		    {
			PIXMAN_COMPOSE_FUNCTIONS.combineU[PIXMAN_OP_IN] (mask_buffer, src_buffer, data->width);
			src_mask_buffer = mask_buffer;
		    }
		    else
			src_mask_buffer = src_buffer;

		    fetchSrc = NULL;
		}
		else
		{
		    fetchSrc (data->src, data->xSrc, data->ySrc + i,
			      data->width, src_buffer, mask_buffer,
			      0xff000000);

		    if (mask_buffer)
			PIXMAN_COMPOSE_FUNCTIONS.combineMaskU (src_buffer,
							       mask_buffer,
							       data->width);

		    src_mask_buffer = src_buffer;
		}
	    }
	    else if (fetchMask)
	    {
		fetchMask (data->mask, data->xMask, data->yMask + i,
			   data->width, mask_buffer, 0, 0);

		PIXMAN_COMPOSE_FUNCTIONS.combineU[PIXMAN_OP_IN] (mask_buffer, src_buffer, data->width);

		src_mask_buffer = mask_buffer;
	    }

	    if (store)
	    {
		/* fill dest into second half of scanline */
		if (fetchDest)
		    fetchDest (data->dest, data->xDest, data->yDest + i,
			       data->width, dest_buffer, 0, 0);

		/* blend */
		compose (dest_buffer, src_mask_buffer, data->width);

		/* write back */
		store (data->dest, data->xDest, data->yDest + i, data->width,
		       dest_buffer);
	    }
	    else
	    {
		/* blend */
		compose (bits + (data->yDest + i+ yoff) * stride +
			 data->xDest + xoff,
			 src_mask_buffer, data->width);
	    }
	}
    }
}

#ifndef PIXMAN_FB_ACCESSORS

void
pixman_composite_rect_general (const FbComposeData *data,
			       uint32_t *scanline_buffer)
{
    if (data->src->common.read_func			||
	data->src->common.write_func			||
	(data->mask && data->mask->common.read_func)	||
	(data->mask && data->mask->common.write_func)	||
	data->dest->common.read_func			||
	data->dest->common.write_func)
    {
	pixman_composite_rect_general_accessors (data, scanline_buffer);
    }
    else
    {
	pixman_composite_rect_general_no_accessors (data, scanline_buffer);
    }
}

#endif
