/*
 * Copyright © 2000 Keith Packard, member of The XFree86 Project, Inc.
 *             2005 Lars Knoll & Zack Rusin, Trolltech
 *             2008 Aaron Plattner, NVIDIA Corporation
 * Copyright © 2000 SuSE, Inc.
 * Copyright © 2007, 2009 Red Hat, Inc.
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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include "pixman-private.h"

#define Alpha(x) ((x) >> 24)
#define Red(x) (((x) >> 16) & 0xff)
#define Green(x) (((x) >> 8) & 0xff)
#define Blue(x) ((x) & 0xff)

#define READ_ACCESS(f) ((image->common.read_func)? f##_accessors : f)
#define WRITE_ACCESS(f) ((image->common.write_func)? f##_accessors : f)

/* Store functions */

static void
bits_image_store_scanline_32 (bits_image_t *image, int x, int y, int width, uint32_t *buffer)
{
    uint32_t *bits;
    int32_t stride;
    const pixman_indexed_t *indexed = image->indexed;

    bits = image->bits;
    stride = image->rowstride;
    bits += y*stride;

    image->store_scanline_raw_32 ((pixman_image_t *)image, bits, buffer, x, width, indexed);

    if (image->common.alpha_map)
    {
	x -= image->common.alpha_origin.x;
	y -= image->common.alpha_origin.y;

	bits_image_store_scanline_32 (image->common.alpha_map, x, y, width, buffer);
    }
}

static void
bits_image_store_scanline_64 (bits_image_t *image, int x, int y, int width, uint32_t *buffer)
{
    uint32_t *bits;
    int32_t stride;
    const pixman_indexed_t *indexed = image->indexed;

    bits = image->bits;
    stride = image->rowstride;
    bits += y*stride;

    image->store_scanline_raw_64 ((pixman_image_t *)image, bits, (uint64_t *)buffer, x, width, indexed);

    if (image->common.alpha_map)
    {
	x -= image->common.alpha_origin.x;
	y -= image->common.alpha_origin.y;

	bits_image_store_scanline_64 (image->common.alpha_map, x, y, width, buffer);
    }
}

void
_pixman_image_store_scanline_32 (bits_image_t *image, int x, int y, int width,
				 uint32_t *buffer)
{
    image->store_scanline_32 (image, x, y, width, buffer);
}

void
_pixman_image_store_scanline_64 (bits_image_t *image, int x, int y, int width,
				 uint32_t *buffer)
{
    image->store_scanline_64 (image, x, y, width, buffer);
}

/* Fetch functions */

/* On entry, @buffer should contain @n_pixels (x, y) coordinate pairs, where
 * x and y are both uint32_ts. On exit, buffer will contain the corresponding
 * pixels.
 */
static void
bits_image_fetch_raw_pixels (bits_image_t *image, uint32_t *buffer, int n_pixels)
{
    uint32_t *coords;
    int i;

    coords = buffer;
    
    for (i = 0; i < n_pixels; ++i)
    {
	uint32_t x = *coords++;
	uint32_t y = *coords++;

	if (x == 0xffffffff || y == 0xffffffff)
	    buffer[i] = 0;
	else
	    buffer[i] = image->fetch_pixel (image, x, y);
    }
}

static void
bits_image_fetch_alpha_pixels (bits_image_t *image, uint32_t *buffer, int n_pixels)
{
#define N_ALPHA_PIXELS 256
    
    uint32_t alpha_pixels[N_ALPHA_PIXELS * 2];
    int i;
    
    if (!image->common.alpha_map)
    {
	bits_image_fetch_raw_pixels (image, buffer, n_pixels);
	return;
    }

    /* Alpha map */
    i = 0;
    while (i < n_pixels)
    {
	int tmp_n_pixels = MIN (N_ALPHA_PIXELS, n_pixels - i);
	int j;
	int32_t *coords;
	
	memcpy (alpha_pixels, buffer + 2 * i, tmp_n_pixels * 2 * sizeof (int32_t));
	coords = (int32_t *)alpha_pixels;
	for (j = 0; j < tmp_n_pixels; ++j)
	{
	    int32_t x = coords[0];
	    int32_t y = coords[1];
	    
	    if (x != 0xffffffff)
	    {
		x -= image->common.alpha_origin.x;
		
		if (x < 0 || x >= image->common.alpha_map->width)
		    x = 0xffffffff;
	    }
	    
	    if (y != 0xffffffff)
	    {
		y -= image->common.alpha_origin.y;
		
		if (y < 0 || y >= image->common.alpha_map->height)
		    y = 0xffffffff;
	    }
	    
	    coords[0] = x;
	    coords[1] = y;
	    
	    coords += 2;
	}
	
	bits_image_fetch_raw_pixels (image->common.alpha_map, alpha_pixels, tmp_n_pixels);
	bits_image_fetch_raw_pixels (image, buffer + 2 * i, tmp_n_pixels);
	
	for (j = 0; j < tmp_n_pixels; ++j)
	{
	    int a = alpha_pixels[j] >> 24;
	    
	    buffer[i] =
		(a << 24)					|
		div_255 (Red (buffer[2 * i - j]) * a) << 16	|
		div_255 (Green (buffer[2 * i - j]) * a) << 8	|
		div_255 (Blue (buffer[2 * i - j]) * a);
	    
	    i++;
	}
    }
}

static void
bits_image_fetch_pixels_src_clip (bits_image_t *image, uint32_t *buffer, int n_pixels)
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

    bits_image_fetch_alpha_pixels (image, buffer, n_pixels);
}

/* Buffer contains list of integers on input, list of pixels on output */
static void
bits_image_fetch_extended (bits_image_t *image, uint32_t *buffer, int n_pixels)
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

    bits_image_fetch_pixels_src_clip (image, buffer, n_pixels);
}

/* Buffer contains list of fixed-point coordinates on input,
 * a list of pixels on output
 */
static void
bits_image_fetch_nearest_pixels (bits_image_t *image, uint32_t *buffer, int n_pixels)
{
    int i;

    for (i = 0; i < 2 * n_pixels; ++i)
    {
	int32_t *coords = (int32_t *)buffer;

	/* Subtract pixman_fixed_e to ensure that 0.5 rounds to 0, not 1 */
	coords[i] = pixman_fixed_to_int (coords[i] - pixman_fixed_e);
    }

    return bits_image_fetch_extended (image, buffer, n_pixels);
}

/* Buffer contains list of fixed-point coordinates on input,
 * a list of pixels on output
 */
static void
bits_image_fetch_bilinear_pixels (bits_image_t *image, uint32_t *buffer, int n_pixels)
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
	    
	    x1 = coords[0] - pixman_fixed_1 / 2;
	    y1 = coords[1] - pixman_fixed_1 / 2;
	    
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

	bits_image_fetch_extended (image, temps, tmp_n_pixels * 4);

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
bits_image_fetch_convolution_pixels (bits_image_t *image, uint32_t *buffer, int n_pixels)
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

	    /* Subtract pixman_fixed_e to ensure that 0.5 rounds to 0, not 1 */
	    x1 = pixman_fixed_to_int (coords[0] - pixman_fixed_e) - x_off;
	    y1 = pixman_fixed_to_int (coords[1] - pixman_fixed_e) - y_off;
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

	bits_image_fetch_extended (image, tmp_pixels, n_kernels * kernel_size);

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
bits_image_fetch_filtered (bits_image_t *pict, uint32_t *buffer, int n_pixels)
{
    switch (pict->common.filter)
    {
    case PIXMAN_FILTER_NEAREST:
    case PIXMAN_FILTER_FAST:
	bits_image_fetch_nearest_pixels (pict, buffer, n_pixels);
	break;
	
    case PIXMAN_FILTER_BILINEAR:
    case PIXMAN_FILTER_GOOD:
    case PIXMAN_FILTER_BEST:
	bits_image_fetch_bilinear_pixels (pict, buffer, n_pixels);
	break;
    case PIXMAN_FILTER_CONVOLUTION:
	bits_image_fetch_convolution_pixels (pict, buffer, n_pixels);
	break;
    }
}

static void
bits_image_fetch_transformed (bits_image_t * pict, int x, int y, int width,
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

    /* when using convolution filters or PIXMAN_REPEAT_PAD one
     * might get here without a transform */
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

    i = 0;
    while (i < width)
    {
	int n_pixels = MIN (N_TMP_PIXELS, width - i);
	int j;
	
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
		
		div = ((pixman_fixed_48_16_t)v.vector[0] << 16) / v.vector[2];

		if ((div >> 16) > 0x7fff)
		    coords[0] = 0x7fffffff; 
		else if ((div >> 16) < 0x8000)
		    coords[0] = 0x80000000;
		else
		    coords[0] = div;
		
		div = ((pixman_fixed_48_16_t)v.vector[1] << 16) / v.vector[2];

		if ((div >> 16) > 0x7fff)
		    coords[1] = 0x7fffffff;
		else if ((div >> 16) < 0x8000)
		    coords[1] = 0x8000000;
		else
		    coords[1] = div;
	    }

	    coords += 2;

	    v.vector[0] += unit.vector[0];
	    v.vector[1] += unit.vector[1];
	    v.vector[2] += unit.vector[2];
	}

	bits_image_fetch_filtered (pict, tmp_buffer, n_pixels);
	
	for (j = 0; j < n_pixels; ++j)
	    buffer[i++] = tmp_buffer[j];
    }
}

static void
bits_image_fetch_solid_32 (bits_image_t * image,
			   int x, int y, int width,
			   uint32_t *buffer,
			   uint32_t *mask, uint32_t maskBits)
{
    uint32_t color;
    uint32_t *end;
    
    color = image->fetch_pixel (image, 0, 0);
    
    end = buffer + width;
    while (buffer < end)
	*(buffer++) = color;
}

static void
bits_image_fetch_solid_64 (bits_image_t * image,
			   int x, int y, int width,
			   uint64_t *buffer, void *unused, uint32_t unused2)
{
    uint64_t color;
    uint64_t *end;
    
    color = image->fetch_pixel (image, 0, 0);
    
    end = buffer + width;
    while (buffer < end)
	*(buffer++) = color;
}

static void
bits_image_fetch_untransformed_32 (bits_image_t * image,
				   int x, int y, int width,
				   uint32_t *buffer, uint32_t *mask, uint32_t maskBits)
{
    image->fetch_scanline_raw_32 (image, x, y, width, buffer);
}

static void
bits_image_fetch_untransformed_64 (bits_image_t * image,
				   int x, int y, int width,
				   uint64_t *buffer, void *unused, uint32_t unused2)
{
    image->fetch_scanline_raw_64 (image, x, y, width, buffer);
}

static void
bits_image_property_changed (pixman_image_t *image)
{
    bits_image_t *bits = (bits_image_t *)image;
    
    if (bits->common.alpha_map)
    {
	image->common.get_scanline_64 =
	    (scanFetchProc)_pixman_image_get_scanline_64_generic;
	image->common.get_scanline_32 =
	    (scanFetchProc)bits_image_fetch_transformed;
    }
    else if ((bits->common.repeat != PIXMAN_REPEAT_NONE) &&
	    bits->width == 1 &&
	    bits->height == 1)
    {
	image->common.get_scanline_64 = (scanFetchProc)bits_image_fetch_solid_64;
	image->common.get_scanline_32 = (scanFetchProc)bits_image_fetch_solid_32;
    }
    else if (!bits->common.transform &&
	     bits->common.filter != PIXMAN_FILTER_CONVOLUTION &&
	     bits->common.repeat != PIXMAN_REPEAT_PAD &&
	     bits->common.repeat != PIXMAN_REPEAT_REFLECT)
    {
	image->common.get_scanline_64 = (scanFetchProc)bits_image_fetch_untransformed_64;
	image->common.get_scanline_32 = (scanFetchProc)bits_image_fetch_untransformed_32;
    }
    else
    {
	image->common.get_scanline_64 =
	    (scanFetchProc)_pixman_image_get_scanline_64_generic;
	image->common.get_scanline_32 =
	    (scanFetchProc)bits_image_fetch_transformed;
    }
    
    bits->store_scanline_64 = bits_image_store_scanline_64;
    bits->store_scanline_32 = bits_image_store_scanline_32;

    bits->store_scanline_raw_32 =
	WRITE_ACCESS(pixman_storeProcForPicture32)(bits);
    bits->store_scanline_raw_64 =
	WRITE_ACCESS(pixman_storeProcForPicture64)(bits);

    bits->fetch_scanline_raw_32 =
	READ_ACCESS(pixman_fetchProcForPicture32)(bits);
    bits->fetch_scanline_raw_64 =
	READ_ACCESS(pixman_fetchProcForPicture64)(bits);
    
    bits->fetch_pixel = READ_ACCESS(pixman_fetchPixelProcForPicture32)(bits);
}

static uint32_t *
create_bits (pixman_format_code_t format,
	     int		  width,
	     int		  height,
	     int		 *rowstride_bytes)
{
    int stride;
    int buf_size;
    int bpp;
    
    /* what follows is a long-winded way, avoiding any possibility of integer
     * overflows, of saying:
     * stride = ((width * bpp + FB_MASK) >> FB_SHIFT) * sizeof (uint32_t);
     */
    
    bpp = PIXMAN_FORMAT_BPP (format);
    if (pixman_multiply_overflows_int (width, bpp))
	return NULL;
    
    stride = width * bpp;
    if (pixman_addition_overflows_int (stride, FB_MASK))
	return NULL;
    
    stride += FB_MASK;
    stride >>= FB_SHIFT;
    
#if FB_SHIFT < 2
    if (pixman_multiply_overflows_int (stride, sizeof (uint32_t)))
	return NULL;
#endif
    stride *= sizeof (uint32_t);
    
    if (pixman_multiply_overflows_int (height, stride))
	return NULL;
    
    buf_size = height * stride;
    
    if (rowstride_bytes)
	*rowstride_bytes = stride;
    
    return calloc (buf_size, 1);
}

PIXMAN_EXPORT pixman_image_t *
pixman_image_create_bits (pixman_format_code_t  format,
			  int                   width,
			  int                   height,
			  uint32_t	       *bits,
			  int			rowstride_bytes)
{
    pixman_image_t *image;
    uint32_t *free_me = NULL;
    
    /* must be a whole number of uint32_t's
     */
    return_val_if_fail (bits == NULL ||
			(rowstride_bytes % sizeof (uint32_t)) == 0, NULL);
    
    if (!bits && width && height)
    {
	free_me = bits = create_bits (format, width, height, &rowstride_bytes);
	if (!bits)
	    return NULL;
    }
    
    image = _pixman_image_allocate();
    
    if (!image) {
	if (free_me)
	    free (free_me);
	return NULL;
    }
    
    image->type = BITS;
    image->bits.format = format;
    image->bits.width = width;
    image->bits.height = height;
    image->bits.bits = bits;
    image->bits.free_me = free_me;
    
    image->bits.rowstride = rowstride_bytes / (int) sizeof (uint32_t); /* we store it in number
									* of uint32_t's
									*/
    image->bits.indexed = NULL;
    
    pixman_region32_fini (&image->common.full_region);
    pixman_region32_init_rect (&image->common.full_region, 0, 0,
			       image->bits.width, image->bits.height);
    
    image->common.property_changed = bits_image_property_changed;
    
    bits_image_property_changed (image);
    
    _pixman_image_reset_clip_region (image);
    
    return image;
}
