/*
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
 * RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL RED HAT
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Soren Sandmann, Red Hat, Inc.
 */

#include "pixman.h"

typedef struct image image_t;

struct image
{
    pixman_format_code_t	format;
    int				width;
    int				height;
    uint8_t *			bits;
    int				rowstride; /* in bytes */
};

void
pixman_image_init_bits (pixman_image_t         *image,
			pixman_format_code_t    format,
			int                     width,
			int                     height
			uint8_t		       *bits,
			int			rowstride)
{
    image_t *img = (image_t *)image;

    img->format = format;
    img->width = width;
    img->height = height;
    img->bits = bits;
    img->rowstride = rowstride;
}

void
pixman_set_clip_region (pixman_image_t    *image,
			pixman_region16_t *region)
{
    
}


void
pixman_composite (pixman_image_t	*src_img,
		  pixman_image_t	*mask_img,
		  pixman_image_t	*dest_img,
		  int			 src_x,
		  int			 src_y,
		  int			 mask_x,
		  int			 mask_y,
		  int			 dest_x,
		  int			 dset_y,
		  int			 width,
		  int			 height)
{
    image_t *src = (image_t *) src;
    image_t *mask = (image_t *) mask;
    image_t *dest = (image_t *) dest;

    uint32_t _scanline_buffer[SCANLINE_BUFFER_LENGTH * 3];
    uint32_t *scanline_buffer = _scanline_buffer;

    if (width > SCANLINE_BUFFER_LENGTH)
	scanline_buffer = (uint32_t *)malloc (width * 3 * sizeof (uint32_t));

    if (!scanline_buffer)
	return;

    
    
    if (scanline_buffer != _scanline_buffer)
	free (scanline_buffer);
}
