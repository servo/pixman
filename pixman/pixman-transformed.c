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
fbFetchTransformed (bits_image_t * pict, int x, int y, int width,
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
		    coords[0] = 0xffffffff; /* FIXME: the intention is that this should be fetched as 0 */
		else
		    coords[0] = div >> 16;

		div = ((pixman_fixed_48_16_t)v.vector[1] << 16)/v.vector[2];
		if ((div >> 16) >= 0xffff)
		    coords[1] = 0xffffffff; /* FIXME: the intention is that this should be fetched as 0 */
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
	    fetch_nearest_pixels (pict, tmp_buffer, n_pixels);
	    break;

	case PIXMAN_FILTER_BILINEAR:
	case PIXMAN_FILTER_GOOD:
	case PIXMAN_FILTER_BEST:
	    fetch_bilinear_pixels (pict, tmp_buffer, n_pixels);
	    break;
	case PIXMAN_FILTER_CONVOLUTION:
	    fetch_convolution_pixels (pict, tmp_buffer, n_pixels);
	    break;
	}

	for (j = 0; j < n_pixels; ++j)
	    buffer[i++] = tmp_buffer[j];
    }
}
