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
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>

#include "pixman-private.h"

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

    store = _pixman_image_get_storer (data->dest, wide);

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
		store (data->dest, data->xDest, data->yDest + i, data->width,
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

void
pixman_composite_rect_general (const FbComposeData *data)
{
    uint8_t stack_scanline_buffer[SCANLINE_BUFFER_LENGTH * 3];
    const pixman_format_code_t srcFormat = data->src->type == BITS ? data->src->bits.format : 0;
    const pixman_format_code_t maskFormat = data->mask && data->mask->type == BITS ? data->mask->bits.format : 0;
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
