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

#include <stdlib.h>

#include "pixman-private.h"

#define Alpha(x) ((x) >> 24)
#define Red(x) (((x) >> 16) & 0xff)
#define Green(x) (((x) >> 8) & 0xff)
#define Blue(x) ((x) & 0xff)

#define Alpha64(x) ((x) >> 48)
#define Red64(x) (((x) >> 32) & 0xffff)
#define Green64(x) (((x) >> 16) & 0xffff)
#define Blue64(x) ((x) & 0xffff)

/*
 * Fetch from region strategies
 */
typedef FASTCALL uint32_t (*fetchFromRegionProc)(bits_image_t *pict, int x, int y, uint32_t *buffer, fetchPixelProc32 fetch, pixman_box32_t *box);

/*
 * There are two properties we can make use of when fetching pixels
 *
 * (a) Is the source clip just the image itself?
 *
 * (b) Do we know the coordinates of the pixel to fetch are
 *     within the image boundaries;
 *
 * Source clips are almost never used, so the important case to optimize
 * for is when src_clip is false. Since inside_bounds is statically known,
 * the last part of the if statement will normally be optimized away.
 */
static force_inline uint32_t
do_fetch (bits_image_t *pict, int x, int y, fetchPixelProc32 fetch,
	  pixman_bool_t src_clip,
	  pixman_bool_t inside_bounds)
{
    if (src_clip)
    {
	if (pixman_region32_contains_point (pict->common.src_clip, x, y,NULL))
	    return fetch (pict, x, y);
	else
	    return 0;
    }
    else if (inside_bounds)
    {
	return fetch (pict, x, y);
    }
    else
    {
	if (x >= 0 && x < pict->width && y >= 0 && y < pict->height)
	    return fetch (pict, x, y);
	else
	    return 0;
    }
}

static void
fetch_pixels_src_clip (bits_image_t *image, uint32_t *buffer, int n_pixels)
{
    if (image->common.src_clip != &(image->common.full_region) &&
	!pixman_region32_equal (image->common.src_clip, &(image->common.full_region)))
    {
	int32_t *coords = (int32_t *)buffer;
	int i;

	for (i = 0; i < n_pixels; ++i)
	{
	    int32_t x = coords[0];
	    int32_t y = coords[1];

	    if (!pixman_region32_contains_point (image->common.src_clip, x, y, NULL))
	    {
		coords[0] = 0xffffffff;
		coords[1] = 0xffffffff;
	    }

	    coords += 2;
	}
    }

    _pixman_image_fetch_pixels (image, buffer, n_pixels);
}

/* Buffer contains list of integers on input, list of pixels on output */
static void
fetch_extended (bits_image_t *image, uint32_t *buffer, int n_pixels)
{
    int32_t *coords, x, y, width, height;
    int i;

    width = image->width;
    height = image->height;
    
    coords = (int32_t *)buffer;
    
    switch (image->common.repeat)
    {
    case PIXMAN_REPEAT_NORMAL:
	for (i = 0; i < n_pixels; ++i)
	{
	    coords[0] = MOD (coords[0], width);
	    coords[1] = MOD (coords[1], height);

	    coords += 2;
	}
	break;

    case PIXMAN_REPEAT_PAD:
	for (i = 0; i < n_pixels; ++i)
	{
	    coords[0] = CLIP (coords[0], 0, width - 1);
	    coords[1] = CLIP (coords[1], 0, height - 1);

	    coords += 2;
	}
	break;
	
    case PIXMAN_REPEAT_REFLECT:
	for (i = 0; i < n_pixels; ++i)
	{
	    x = MOD (coords[0], width * 2);
	    y = MOD (coords[1], height * 2);

	    if (x >= width)
		x = width * 2 - x - 1;

	    if (y >= height)
		y = height * 2 - y - 1;

	    coords[0] = x;
	    coords[1] = y;

	    coords += 2;
	}
	break;

    case PIXMAN_REPEAT_NONE:
	for (i = 0; i < n_pixels; ++i)
	{
	    x = coords[0];
	    y = coords[1];

	    if (x < 0 || x >= width)
		coords[0] = 0xffffffff;
	    
	    if (y < 0 || y >= height)
		coords[1] = 0xffffffff;

	    coords += 2;
	}
	break;
    }

    fetch_pixels_src_clip (image, buffer, n_pixels);
}

/* Buffer contains list of fixed-point coordinates on input,
 * a list of pixels on output
 */
static void
fetch_nearest_pixels (bits_image_t *image, uint32_t *buffer, int n_pixels)
{
    int i;

    for (i = 0; i < 2 * n_pixels; ++i)
    {
	int32_t *coords = (int32_t *)buffer;

	coords[i] >>= 16;
    }

    return fetch_extended (image, buffer, n_pixels);
}

/*
 * Fetching Algorithms
 */
static inline uint32_t
fetch_nearest (bits_image_t		*pict,
	       fetchPixelProc32		 fetch,
	       pixman_bool_t		 affine,
	       pixman_repeat_t		 repeat,
	       pixman_bool_t             has_src_clip,
	       const pixman_vector_t    *v)
{
    if (!v->vector[2])
    {
	return 0;
    }
    else
    {
	int x, y;
	pixman_bool_t inside_bounds;

	if (!affine)
	{
	    x = DIV(v->vector[0], v->vector[2]);
	    y = DIV(v->vector[1], v->vector[2]);
	}
	else
	{
	    x = v->vector[0]>>16;
	    y = v->vector[1]>>16;
	}

	switch (repeat)
	{
	case PIXMAN_REPEAT_NORMAL:
	    x = MOD (x, pict->width);
	    y = MOD (y, pict->height);
	    inside_bounds = TRUE;
	    break;
	    
	case PIXMAN_REPEAT_PAD:
	    x = CLIP (x, 0, pict->width-1);
	    y = CLIP (y, 0, pict->height-1);
	    inside_bounds = TRUE;
	    break;
	    
	case PIXMAN_REPEAT_REFLECT:
	    x = MOD (x, pict->width * 2);
	    if (x >= pict->width)
		x = pict->width * 2 - x - 1;
	    y = MOD (y, pict->height * 2);
	    if (y >= pict->height)
		y = pict->height * 2 - y - 1;
	    inside_bounds = TRUE;
	    break;

	case PIXMAN_REPEAT_NONE:
	    inside_bounds = FALSE;
	    break;

	default:
	    return 0;
	}

	return do_fetch (pict, x, y, fetch, has_src_clip, inside_bounds);
    }
}

/* Buffer contains list of fixed-point coordinates on input,
 * a list of pixels on output
 */
static void
fetch_bilinear_pixels (bits_image_t *image, uint32_t *buffer, int n_pixels)
{
/* (Four pixels * two coordinates) per pixel */
#define TMP_N_PIXELS	(256)
#define N_TEMPS		(TMP_N_PIXELS * 8)
#define N_DISTS		(TMP_N_PIXELS * 2)
    
    uint32_t temps[N_TEMPS];
    int32_t  dists[N_DISTS];
    int32_t *coords;
    int i;

    i = 0;
    coords = (int32_t *)buffer;
    while (i < n_pixels)
    {
	int tmp_n_pixels = MIN(TMP_N_PIXELS, n_pixels - i);
	int32_t distx, disty;
	uint32_t *u;
	int32_t *t, *d;
	int j;
	
	t = (int32_t *)temps;
	d = dists;
	for (j = 0; j < tmp_n_pixels; ++j)
	{
	    int32_t x1, y1, x2, y2;
	    x1 = coords[0];
	    y1 = coords[1];
	    distx = (x1 >> 8) & 0xff;
	    disty = (y1 >> 8) & 0xff;
	    x1 >>= 16;
	    y1 >>= 16;
	    x2 = x1 + 1;
	    y2 = y1 + 1;

	    *t++ = x1;
	    *t++ = y1;
	    *t++ = x2;
	    *t++ = y1;
	    *t++ = x1;
	    *t++ = y2;
	    *t++ = x2;
	    *t++ = y2;

	    *d++ = distx;
	    *d++ = disty;

	    coords += 2;
	}

	fetch_extended (image, temps, tmp_n_pixels * 4);

	u = (uint32_t *)temps;
	d = dists;
	for (j = 0; j < tmp_n_pixels; ++j)
	{
	    uint32_t tl, tr, bl, br, r;
	    int32_t idistx, idisty;
	    uint32_t ft, fb;
	    
	    tl = *u++;
	    tr = *u++;
	    bl = *u++;
	    br = *u++;

	    distx = *d++;
	    disty = *d++;

	    idistx = 256 - distx;
	    idisty = 256 - disty;
	    
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

	    buffer[i++] = r;
	}
    }
}

static inline uint32_t
fetch_bilinear (bits_image_t		*pict,
		fetchPixelProc32	 fetch,
		pixman_bool_t		 affine,
		pixman_repeat_t		 repeat,
		pixman_bool_t		 has_src_clip,
		const pixman_vector_t   *v)
{
    if (!v->vector[2])
    {
	return 0;
    }
    else
    {
	int x1, x2, y1, y2, distx, idistx, disty, idisty;
	uint32_t tl, tr, bl, br, r;
	uint32_t ft, fb;
	pixman_bool_t inside_bounds;
	
	if (!affine)
	{
	    pixman_fixed_48_16_t div;
	    div = ((pixman_fixed_48_16_t)v->vector[0] << 16)/v->vector[2];
	    x1 = div >> 16;
	    distx = ((pixman_fixed_t)div >> 8) & 0xff;
	    div = ((pixman_fixed_48_16_t)v->vector[1] << 16)/v->vector[2];
	    y1 = div >> 16;
	    disty = ((pixman_fixed_t)div >> 8) & 0xff;
	}
	else
	{
	    x1 = v->vector[0] >> 16;
	    distx = (v->vector[0] >> 8) & 0xff;
	    y1 = v->vector[1] >> 16;
	    disty = (v->vector[1] >> 8) & 0xff;
	}
	x2 = x1 + 1;
	y2 = y1 + 1;
	
	idistx = 256 - distx;
	idisty = 256 - disty;

	switch (repeat)
	{
	case PIXMAN_REPEAT_NORMAL:
	    x1 = MOD (x1, pict->width);
	    x2 = MOD (x2, pict->width);
	    y1 = MOD (y1, pict->height);
	    y2 = MOD (y2, pict->height);
	    inside_bounds = TRUE;
	    break;
	    
	case PIXMAN_REPEAT_PAD:
	    x1 = CLIP (x1, 0, pict->width-1);
	    x2 = CLIP (x2, 0, pict->width-1);
	    y1 = CLIP (y1, 0, pict->height-1);
	    y2 = CLIP (y2, 0, pict->height-1);
	    inside_bounds = TRUE;
	    break;
	    
	case PIXMAN_REPEAT_REFLECT:
	    x1 = MOD (x1, pict->width * 2);
	    if (x1 >= pict->width)
		x1 = pict->width * 2 - x1 - 1;
	    x2 = MOD (x2, pict->width * 2);
	    if (x2 >= pict->width)
		x2 = pict->width * 2 - x2 - 1;
	    y1 = MOD (y1, pict->height * 2);
	    if (y1 >= pict->height)
		y1 = pict->height * 2 - y1 - 1;
	    y2 = MOD (y2, pict->height * 2);
	    if (y2 >= pict->height)
		y2 = pict->height * 2 - y2 - 1;
	    inside_bounds = TRUE;
	    break;

	case PIXMAN_REPEAT_NONE:
	    inside_bounds = FALSE;
	    break;

	default:
	    return 0;
	}
	
	tl = do_fetch(pict, x1, y1, fetch, has_src_clip, inside_bounds);
	tr = do_fetch(pict, x2, y1, fetch, has_src_clip, inside_bounds);
	bl = do_fetch(pict, x1, y2, fetch, has_src_clip, inside_bounds);
	br = do_fetch(pict, x2, y2, fetch, has_src_clip, inside_bounds);
	
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

	return r;
    }
}

/* Buffer contains list of fixed-point coordinates on input,
 * a list of pixels on output
 */
static void
fetch_convolution_pixels (bits_image_t *image, uint32_t *buffer, int n_pixels)
{
#define N_TMP_PIXELS 8192
    uint32_t tmp_pixels_stack[N_TMP_PIXELS * 2]; /* Two coordinates per pixel */
    uint32_t *tmp_pixels = tmp_pixels_stack;
    pixman_fixed_t *params = image->common.filter_params;
    int x_off = (params[0] - pixman_fixed_1) >> 1;
    int y_off = (params[0] - pixman_fixed_1) >> 1;
    int n_tmp_pixels;
    int32_t *coords;
    int32_t *t;
    uint32_t *u;
    int i;
    int max_n_kernels;

    int32_t cwidth = pixman_fixed_to_int (params[0]);
    int32_t cheight = pixman_fixed_to_int (params[1]);
    int kernel_size = cwidth * cheight;

    params += 2;

    n_tmp_pixels = N_TMP_PIXELS;
    if (kernel_size > n_tmp_pixels)
    {
	/* Two coordinates per pixel */
	tmp_pixels = malloc (kernel_size * 2 * sizeof (uint32_t));
	n_tmp_pixels = kernel_size;

	if (!tmp_pixels)
	{
	    /* We ignore out-of-memory during rendering */
	    return;
	}
    }

    max_n_kernels = n_tmp_pixels / kernel_size;
    
    i = 0;
    coords = (int32_t *)buffer;
    while (i < n_pixels)
    {
	int n_kernels = MIN (max_n_kernels, (n_pixels - i));
	int j;
	
	t = (int32_t *)tmp_pixels;
	for (j = 0; j < n_kernels; ++j)
	{
	    int32_t x, y, x1, x2, y1, y2;
	    
	    x1 = pixman_fixed_to_int (coords[0]) - x_off;
	    y1 = pixman_fixed_to_int (coords[1]) - y_off;
	    x2 = x1 + cwidth;
	    y2 = y1 + cheight;

	    for (y = y1; y < y2; ++y)
	    {
		for (x = x1; x < x2; ++x)
		{
		    *t++ = x;
		    *t++ = y;
		}
	    }

	    coords += 2;
	}

	fetch_extended (image, tmp_pixels, n_kernels * kernel_size);

	u = tmp_pixels;
	for (j = 0; j < n_kernels; ++j)
	{
	    int32_t srtot, sgtot, sbtot, satot;
	    pixman_fixed_t *p = params;
	    int k;

	    srtot = sgtot = sbtot = satot = 0;
		
	    for (k = 0; k < kernel_size; ++k)
	    {
		pixman_fixed_t f = *p++;
		if (f)
		{
		    uint32_t c = *u++;

		    srtot += Red(c) * f;
		    sgtot += Green(c) * f;
		    sbtot += Blue(c) * f;
		    satot += Alpha(c) * f;
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

	    buffer[i++] = ((satot << 24) |
			   (srtot << 16) |
			   (sgtot <<  8) |
			   (sbtot       ));
	}
    }
    
    if (tmp_pixels != tmp_pixels_stack)
	free (tmp_pixels);
}

static void
fbFetchTransformed_Convolution(bits_image_t * pict, int width, uint32_t *buffer,
			       uint32_t *mask, uint32_t maskBits,
			       pixman_bool_t affine, pixman_vector_t v, pixman_vector_t unit)
{
    fetchPixelProc32 fetch;
    int i;

    pixman_fixed_t *params = pict->common.filter_params;
    int32_t cwidth = pixman_fixed_to_int(params[0]);
    int32_t cheight = pixman_fixed_to_int(params[1]);
    int xoff = (params[0] - pixman_fixed_1) >> 1;
    int yoff = (params[1] - pixman_fixed_1) >> 1;
    fetch = ACCESS(pixman_fetchPixelProcForPicture32)(pict);

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
			case PIXMAN_REPEAT_REFLECT:
			    ty = MOD (y, pict->height * 2);
			    if (ty >= pict->height)
				ty = pict->height * 2 - ty - 1;
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
				case PIXMAN_REPEAT_REFLECT:
				    tx = MOD (x, pict->width * 2);
				    if (tx >= pict->width)
					tx = pict->width * 2 - tx - 1;
				    break;
                                default:
                                    tx = x;
                            }
                            if (pixman_region32_contains_point (pict->common.src_clip, tx, ty, NULL)) {
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

void
ACCESS(fbFetchTransformed)(bits_image_t * pict, int x, int y, int width,
                           uint32_t *buffer, uint32_t *mask, uint32_t maskBits)
{
#define N_TMP_PIXELS 1024

    uint32_t     *bits;
    int32_t    stride;
    pixman_vector_t v;
    pixman_vector_t unit;
    pixman_bool_t affine = TRUE;
    uint32_t tmp_buffer[2 * N_TMP_PIXELS];
    int32_t *coords;
    int i;

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

        affine = (v.vector[2] == pixman_fixed_1 && unit.vector[2] == 0);
    }
    else
    {
        unit.vector[0] = pixman_fixed_1;
        unit.vector[1] = 0;
        unit.vector[2] = 0;
    }

    /* These adjustments should probably be moved into the filter code */
    if (pict->common.filter == PIXMAN_FILTER_NEAREST ||
	pict->common.filter == PIXMAN_FILTER_FAST)
    {
	/* Round down to closest integer, ensuring that 0.5 rounds to 0, not 1 */
	adjust (&v, &unit, - pixman_fixed_e);
    }
    else if (pict->common.filter == PIXMAN_FILTER_BILINEAR	||
	     pict->common.filter == PIXMAN_FILTER_GOOD	||
	     pict->common.filter == PIXMAN_FILTER_BEST)
    {
	/* Let the bilinear code pretend that pixels fall on integer coordinaters */
	adjust (&v, &unit, -(pixman_fixed_1 / 2));
    }
    else if (pict->common.filter == PIXMAN_FILTER_CONVOLUTION)
    {
	/* Round to closest integer, ensuring that 0.5 rounds to 0, not 1 */
	adjust (&v, &unit, - pixman_fixed_e);
    }
    
    i = 0;
    while (i < width)
    {
	int j;
	int n_pixels = MIN (N_TMP_PIXELS, width - i);
	
	coords = (int32_t *)tmp_buffer;

	for (j = 0; j < n_pixels; ++j)
	{
	    if (affine)
	    {
		coords[0] = v.vector[0];
		coords[1] = v.vector[1];
	    }
	    else
	    {
		pixman_fixed_48_16_t div;
		
		div = ((pixman_fixed_48_16_t)v.vector[0] << 16)/v.vector[2];
		if ((div >> 16) >= 0xffff)
		    coords[0] = 0xffffffff;
		else
		    coords[0] = div >> 16;

		div = ((pixman_fixed_48_16_t)v.vector[1] << 16)/v.vector[2];
		if ((div >> 16) >= 0xffff)
		    coords[1] = 0xffffffff;
		else
		    coords[1] = div >> 16;
	    }

	    coords += 2;

	    v.vector[0] += unit.vector[0];
	    v.vector[1] += unit.vector[1];
	    v.vector[2] += unit.vector[2];
	}

	switch (pict->common.filter)
	{
	case PIXMAN_FILTER_NEAREST:
	case PIXMAN_FILTER_FAST:
	{
#if 0
	    int k;
	    for (k = 0; k < n_pixels; ++k)
	    {
		int32_t x, y;
		uint32_t r1;
		pixman_vector_t vv;

		x = tmp_buffer[2 * k];
		y = tmp_buffer[2 * k + 1];

		vv.vector[0] = x;
		vv.vector[1] = y;
		vv.vector[2] = 1 << 16;
		
		r1 = fetch_nearest (pict, affine, pict->common.repeat, FALSE, &vv);
		
		fetch_nearest_pixels (pict, (uint32_t *)&(vv.vector), 1);
		
		if (r1 != (uint32_t)vv.vector[0])
		    assert (r1 == (uint32_t) (vv.vector[0]));
	    }
#endif

	    fetch_nearest_pixels (pict, tmp_buffer, n_pixels);
	    break;
	}

	case PIXMAN_FILTER_BILINEAR:
	case PIXMAN_FILTER_GOOD:
	case PIXMAN_FILTER_BEST:
	{
#if 0
	    int k;
	    for (k = 0; k < n_pixels; ++k)
	    {
		int32_t x, y;
		uint32_t r1;
		pixman_vector_t vv;

		x = tmp_buffer[2 * k];
		y = tmp_buffer[2 * k + 1];

		vv.vector[0] = x;
		vv.vector[1] = y;
		vv.vector[2] = 1 << 16;
		
		r1 = fetch_bilinear (pict, affine, pict->common.repeat, FALSE, &vv);
		
		fetch_bilinear_pixels (pict, (uint32_t *)&(vv.vector), 1);

		if (r1 != (uint32_t)vv.vector[0])
		    assert (r1 == (uint32_t) (vv.vector[0]));
	    }
#endif
		
	    fetch_bilinear_pixels (pict, tmp_buffer, n_pixels);
	    break;
	}
	case PIXMAN_FILTER_CONVOLUTION:
	    fetch_convolution_pixels (pict, tmp_buffer, n_pixels);
	    break;
	}

	for (j = 0; j < n_pixels; ++j)
	    buffer[i++] = tmp_buffer[j];
    }
}

#define SCANLINE_BUFFER_LENGTH 2048

void
ACCESS(fbFetchExternalAlpha)(bits_image_t * pict, int x, int y, int width,
                             uint32_t *buffer, uint32_t *mask,
                             uint32_t maskBits)
{
    int i;
    uint32_t _alpha_buffer[SCANLINE_BUFFER_LENGTH];
    uint32_t *alpha_buffer = _alpha_buffer;

    if (!pict->common.alpha_map) {
        ACCESS(fbFetchTransformed) (pict, x, y, width, buffer, mask, maskBits);
	return;
    }
    if (width > SCANLINE_BUFFER_LENGTH)
        alpha_buffer = (uint32_t *) pixman_malloc_ab (width, sizeof(uint32_t));

    ACCESS(fbFetchTransformed)(pict, x, y, width, buffer, mask, maskBits);
    ACCESS(fbFetchTransformed)((bits_image_t *)pict->common.alpha_map, x - pict->common.alpha_origin.x,
                               y - pict->common.alpha_origin.y, width,
                               alpha_buffer, mask, maskBits);
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
