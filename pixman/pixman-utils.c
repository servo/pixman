/*
 * Copyright © 2000 SuSE, Inc.
 * Copyright © 1999 Keith Packard
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

#include "pixman-private.h"

/*
 * Computing composite region
 */
#define BOUND(v)	(int16_t) ((v) < INT16_MIN ? INT16_MIN : (v) > INT16_MAX ? INT16_MAX : (v))

static inline pixman_bool_t
miClipPictureReg (pixman_region32_t *	pRegion,
		  pixman_region32_t *	pClip,
		  int		dx,
		  int		dy)
{
    if (pixman_region32_n_rects(pRegion) == 1 &&
	pixman_region32_n_rects(pClip) == 1)
    {
	pixman_box32_t *  pRbox = pixman_region32_rectangles(pRegion, NULL);
	pixman_box32_t *  pCbox = pixman_region32_rectangles(pClip, NULL);
	int	v;
	
	if (pRbox->x1 < (v = pCbox->x1 + dx))
	    pRbox->x1 = BOUND(v);
	if (pRbox->x2 > (v = pCbox->x2 + dx))
	    pRbox->x2 = BOUND(v);
	if (pRbox->y1 < (v = pCbox->y1 + dy))
	    pRbox->y1 = BOUND(v);
	if (pRbox->y2 > (v = pCbox->y2 + dy))
	    pRbox->y2 = BOUND(v);
	if (pRbox->x1 >= pRbox->x2 ||
	    pRbox->y1 >= pRbox->y2)
	{
	    pixman_region32_init (pRegion);
	}
    }
    else if (!pixman_region32_not_empty (pClip))
    {
	return FALSE;
    }
    else
    {
	if (dx || dy)
	    pixman_region32_translate (pRegion, -dx, -dy);
	if (!pixman_region32_intersect (pRegion, pRegion, pClip))
	    return FALSE;
	if (dx || dy)
	    pixman_region32_translate(pRegion, dx, dy);
    }
    return pixman_region32_not_empty(pRegion);
}


static inline pixman_bool_t
miClipPictureSrc (pixman_region32_t *	pRegion,
		  pixman_image_t *	pPicture,
		  int		dx,
		  int		dy)
{
    /* Source clips are ignored, unless they are explicitly turned on
     * and the clip in question was set by an X client
     */
    if (!pPicture->common.clip_sources || !pPicture->common.client_clip)
	return TRUE;

    return miClipPictureReg (pRegion,
			     &pPicture->common.clip_region,
			     dx, dy);
}

/*
 * returns FALSE if the final region is empty.  Indistinguishable from
 * an allocation failure, but rendering ignores those anyways.
 */
static pixman_bool_t
pixman_compute_composite_region32 (pixman_region32_t *	pRegion,
				   pixman_image_t *	pSrc,
				   pixman_image_t *	pMask,
				   pixman_image_t *	pDst,
				   int16_t		xSrc,
				   int16_t		ySrc,
				   int16_t		xMask,
				   int16_t		yMask,
				   int16_t		xDst,
				   int16_t		yDst,
				   uint16_t		width,
				   uint16_t		height)
{
    int		v;
    
    pRegion->extents.x1 = xDst;
    v = xDst + width;
    pRegion->extents.x2 = BOUND(v);
    pRegion->extents.y1 = yDst;
    v = yDst + height;
    pRegion->extents.y2 = BOUND(v);

    pRegion->extents.x1 = MAX (pRegion->extents.x1, 0);
    pRegion->extents.y1 = MAX (pRegion->extents.y1, 0);
    
    /* Some X servers rely on an old bug, where pixman would just believe the
     * set clip_region and not clip against the destination geometry. So, 
     * since only X servers set "source clip", we don't clip against
     * destination geometry when that is set.
     */
    if (!pDst->common.clip_sources)
    {
	pRegion->extents.x2 = MIN (pRegion->extents.x2, pDst->bits.width);
	pRegion->extents.y2 = MIN (pRegion->extents.y2, pDst->bits.height);
    }
    
    pRegion->data = 0;
    
    /* Check for empty operation */
    if (pRegion->extents.x1 >= pRegion->extents.x2 ||
	pRegion->extents.y1 >= pRegion->extents.y2)
    {
	pixman_region32_init (pRegion);
	return FALSE;
    }
    
    if (pDst->common.have_clip_region)
    {
	if (!miClipPictureReg (pRegion, &pDst->common.clip_region, 0, 0))
	{
	    pixman_region32_fini (pRegion);
	    return FALSE;
	}
    }
    
    if (pDst->common.alpha_map && pDst->common.alpha_map->common.have_clip_region)
    {
	if (!miClipPictureReg (pRegion, &pDst->common.alpha_map->common.clip_region,
			       -pDst->common.alpha_origin_x,
			       -pDst->common.alpha_origin_y))
	{
	    pixman_region32_fini (pRegion);
	    return FALSE;
	}
    }
    
    /* clip against src */
    if (pSrc->common.have_clip_region)
    {
	if (!miClipPictureSrc (pRegion, pSrc, xDst - xSrc, yDst - ySrc))
	{
	    pixman_region32_fini (pRegion);
	    return FALSE;
	}
    }
    if (pSrc->common.alpha_map && pSrc->common.alpha_map->common.have_clip_region)
    {
	if (!miClipPictureSrc (pRegion, (pixman_image_t *)pSrc->common.alpha_map,
			       xDst - (xSrc - pSrc->common.alpha_origin_x),
			       yDst - (ySrc - pSrc->common.alpha_origin_y)))
	{
	    pixman_region32_fini (pRegion);
	    return FALSE;
	}
    }
    /* clip against mask */
    if (pMask && pMask->common.have_clip_region)
    {
	if (!miClipPictureSrc (pRegion, pMask, xDst - xMask, yDst - yMask))
	{
	    pixman_region32_fini (pRegion);
	    return FALSE;
	}	
	if (pMask->common.alpha_map && pMask->common.alpha_map->common.have_clip_region)
	{
	    if (!miClipPictureSrc (pRegion, (pixman_image_t *)pMask->common.alpha_map,
				   xDst - (xMask - pMask->common.alpha_origin_x),
				   yDst - (yMask - pMask->common.alpha_origin_y)))
	    {
		pixman_region32_fini (pRegion);
		return FALSE;
	    }
	}
    }

    return TRUE;
}

PIXMAN_EXPORT pixman_bool_t
pixman_compute_composite_region (pixman_region16_t *	pRegion,
				 pixman_image_t *	pSrc,
				 pixman_image_t *	pMask,
				 pixman_image_t *	pDst,
				 int16_t		xSrc,
				 int16_t		ySrc,
				 int16_t		xMask,
				 int16_t		yMask,
				 int16_t		xDst,
				 int16_t		yDst,
				 uint16_t	width,
				 uint16_t	height)
{
    pixman_region32_t r32;
    pixman_bool_t retval;

    pixman_region32_init (&r32);
    
    retval = pixman_compute_composite_region32 (&r32, pSrc, pMask, pDst,
						xSrc, ySrc, xMask, yMask, xDst, yDst,
						width, height);

    if (retval)
    {
	if (!pixman_region16_copy_from_region32 (pRegion, &r32))
	    retval = FALSE;
    }
    
    pixman_region32_fini (&r32);
    return retval;
}

pixman_bool_t
pixman_multiply_overflows_int (unsigned int a,
		               unsigned int b)
{
    return a >= INT32_MAX / b;
}

pixman_bool_t
pixman_addition_overflows_int (unsigned int a,
		               unsigned int b)
{
    return a > INT32_MAX - b;
}

void *
pixman_malloc_ab(unsigned int a,
		 unsigned int b)
{
    if (a >= INT32_MAX / b)
	return NULL;

    return malloc (a * b);
}

void *
pixman_malloc_abc (unsigned int a,
		   unsigned int b,
		   unsigned int c)
{
    if (a >= INT32_MAX / b)
	return NULL;
    else if (a * b >= INT32_MAX / c)
	return NULL;
    else
	return malloc (a * b * c);
}

/*
 * Helper routine to expand a color component from 0 < n <= 8 bits to 16 bits by
 * replication.
 */
static inline uint64_t
expand16(const uint8_t val, int nbits)
{
    // Start out with the high bit of val in the high bit of result.
    uint16_t result = (uint16_t)val << (16 - nbits);

    if (nbits == 0)
        return 0;

    // Copy the bits in result, doubling the number of bits each time, until we
    // fill all 16 bits.
    while (nbits < 16) {
        result |= result >> nbits;
        nbits *= 2;
    }

    return result;
}

/*
 * This function expands images from ARGB8 format to ARGB16.  To preserve
 * precision, it needs to know the original source format.  For example, if the
 * source was PIXMAN_x1r5g5b5 and the red component contained bits 12345, then
 * the expanded value is 12345123.  To correctly expand this to 16 bits, it
 * should be 1234512345123451 and not 1234512312345123.
 */
void
pixman_expand(uint64_t *dst, const uint32_t *src,
	      pixman_format_code_t format, int width)
{
    /*
     * Determine the sizes of each component and the masks and shifts required
     * to extract them from the source pixel.
     */
    const int a_size = PIXMAN_FORMAT_A(format),
              r_size = PIXMAN_FORMAT_R(format),
              g_size = PIXMAN_FORMAT_G(format),
              b_size = PIXMAN_FORMAT_B(format);
    const int a_shift = 32 - a_size,
              r_shift = 24 - r_size,
              g_shift = 16 - g_size,
              b_shift =  8 - b_size;
    const uint8_t a_mask = ~(~0 << a_size),
                  r_mask = ~(~0 << r_size),
                  g_mask = ~(~0 << g_size),
                  b_mask = ~(~0 << b_size);
    int i;

    /* Start at the end so that we can do the expansion in place when src == dst */
    for (i = width - 1; i >= 0; i--)
    {
        const uint32_t pixel = src[i];
        // Extract the components.
        const uint8_t a = (pixel >> a_shift) & a_mask,
                      r = (pixel >> r_shift) & r_mask,
                      g = (pixel >> g_shift) & g_mask,
                      b = (pixel >> b_shift) & b_mask;
        const uint64_t a16 = a_size ? expand16(a, a_size) : 0xffff,
                       r16 = expand16(r, r_size),
                       g16 = expand16(g, g_size),
                       b16 = expand16(b, b_size);

        dst[i] = a16 << 48 | r16 << 32 | g16 << 16 | b16;
    }
}

/*
 * Contracting is easier than expanding.  We just need to truncate the
 * components.
 */
void
pixman_contract(uint32_t *dst, const uint64_t *src, int width)
{
    int i;

    /* Start at the beginning so that we can do the contraction in place when
     * src == dst */
    for (i = 0; i < width; i++)
    {
        const uint8_t a = src[i] >> 56,
                      r = src[i] >> 40,
                      g = src[i] >> 24,
                      b = src[i] >> 8;
        dst[i] = a << 24 | r << 16 | g << 8 | b;
    }
}

static void
walk_region_internal (pixman_implementation_t *imp,
		      pixman_op_t op,
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
		      pixman_region32_t *region,
		      pixman_composite_func_t compositeRect)
{
    int n;
    const pixman_box32_t *pbox;
    int w, h, w_this, h_this;
    int x_msk, y_msk, x_src, y_src, x_dst, y_dst;

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
		(*compositeRect) (imp,
				  op, pSrc, pMask, pDst,
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
}

void
_pixman_walk_composite_region (pixman_implementation_t *imp,
			       pixman_op_t op,
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
			       pixman_composite_func_t compositeRect)
{
    pixman_region32_t region;
    
    pixman_region32_init (&region);

    if (pixman_compute_composite_region32 (
	    &region, pSrc, pMask, pDst, xSrc, ySrc, xMask, yMask, xDst, yDst, width, height))
    {
	walk_region_internal (imp, op,
			      pSrc, pMask, pDst,
			      xSrc, ySrc, xMask, yMask, xDst, yDst,
			      width, height, FALSE, FALSE,
			      &region,
			      compositeRect);
    }

    pixman_region32_fini (&region);
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

static const pixman_fast_path_t *
get_fast_path (const pixman_fast_path_t *fast_paths,
	       pixman_op_t         op,
	       pixman_image_t     *pSrc,
	       pixman_image_t     *pMask,
	       pixman_image_t     *pDst,
	       pixman_bool_t       is_pixbuf)
{
    const pixman_fast_path_t *info;

    for (info = fast_paths; info->op != PIXMAN_OP_NONE; info++)
    {
	pixman_bool_t valid_src = FALSE;
	pixman_bool_t valid_mask = FALSE;

	if (info->op != op)
	    continue;

	if ((info->src_format == PIXMAN_solid && _pixman_image_is_solid (pSrc)) ||
	    (pSrc->type == BITS && info->src_format == pSrc->bits.format))
	{
	    valid_src = TRUE;
	}

	if (!valid_src)
	    continue;

	if ((info->mask_format == PIXMAN_null && !pMask) ||
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

static inline pixman_bool_t
image_covers (pixman_image_t *image, pixman_box32_t *extents, int x, int y)
{
    if (image->common.type == BITS && image->common.repeat == PIXMAN_REPEAT_NONE)
    {
	if (x > extents->x1 || y > extents->y1 ||
	    x + image->bits.width < extents->x2 ||
	    y + image->bits.height < extents->y2)
	{
	    return FALSE;
	}
    }

    return TRUE;
}

pixman_bool_t
_pixman_run_fast_path (const pixman_fast_path_t *paths,
		       pixman_implementation_t *imp,
		       pixman_op_t op,
		       pixman_image_t *src,
		       pixman_image_t *mask,
		       pixman_image_t *dest,
		       int32_t src_x,
		       int32_t src_y,
		       int32_t mask_x,
		       int32_t mask_y,
		       int32_t dest_x,
		       int32_t dest_y,
		       int32_t width,
		       int32_t height)
{
    pixman_composite_func_t func = NULL;
    pixman_bool_t src_repeat = src->common.repeat == PIXMAN_REPEAT_NORMAL;
    pixman_bool_t mask_repeat = mask && mask->common.repeat == PIXMAN_REPEAT_NORMAL;
    pixman_bool_t result;

    if ((src->type == BITS || _pixman_image_is_solid (src)) &&
	(!mask || mask->type == BITS)
	&& !src->common.transform && !(mask && mask->common.transform)
	&& !(mask && mask->common.alpha_map) && !src->common.alpha_map && !dest->common.alpha_map
	&& (src->common.filter != PIXMAN_FILTER_CONVOLUTION)
	&& (src->common.repeat != PIXMAN_REPEAT_PAD)
	&& (src->common.repeat != PIXMAN_REPEAT_REFLECT)
	&& (!mask || (mask->common.filter != PIXMAN_FILTER_CONVOLUTION &&
		      mask->common.repeat != PIXMAN_REPEAT_PAD &&
		      mask->common.repeat != PIXMAN_REPEAT_REFLECT))
	&& !src->common.read_func && !src->common.write_func
	&& !(mask && mask->common.read_func)
	&& !(mask && mask->common.write_func)
	&& !dest->common.read_func
	&& !dest->common.write_func)
    {
	const pixman_fast_path_t *info;	
	pixman_bool_t pixbuf;

	pixbuf =
	    src && src->type == BITS		&&
	    mask && mask->type == BITS		&&
	    src->bits.bits == mask->bits.bits	&&
	    src_x == mask_x			&&
	    src_y == mask_y			&&
	    !mask->common.component_alpha	&&
	    !mask_repeat;
	
	info = get_fast_path (paths, op, src, mask, dest, pixbuf);
	
	if (info)
	{
	    func = info->func;
	    
	    if (info->src_format == PIXMAN_solid)
		src_repeat = FALSE;
	    
	    if (info->mask_format == PIXMAN_solid || info->flags & NEED_SOLID_MASK)
		mask_repeat = FALSE;
	    
	    if ((src_repeat			&&
		 src->bits.width == 1		&&
		 src->bits.height == 1)	||
		(mask_repeat			&&
		 mask->bits.width == 1		&&
		 mask->bits.height == 1))
	    {
		/* If src or mask are repeating 1x1 images and src_repeat or
		 * mask_repeat are still TRUE, it means the fast path we
		 * selected does not actually handle repeating images.
		 *
		 * So rather than call the "fast path" with a zillion
		 * 1x1 requests, we just fall back to the general code (which
		 * does do something sensible with 1x1 repeating images).
		 */
		func = NULL;
	    }
	}
    }
    
    result = FALSE;
    
    if (func)
    {
	pixman_region32_t region;
	pixman_region32_init (&region);

	if (pixman_compute_composite_region32 (
		&region, src, mask, dest, src_x, src_y, mask_x, mask_y, dest_x, dest_y, width, height))
	{
	    pixman_box32_t *extents = pixman_region32_extents (&region);

	    if (image_covers (src, extents, dest_x - src_x, dest_y - src_y)   &&
		(!mask || image_covers (mask, extents, dest_x - mask_x, dest_y - mask_y)))
	    {
		walk_region_internal (imp, op,
				      src, mask, dest,
				      src_x, src_y, mask_x, mask_y,
				      dest_x, dest_y,
				      width, height,
				      src_repeat, mask_repeat,
				      &region,
				      func);
	    
		result = TRUE;
	    }
	}
	    
	pixman_region32_fini (&region);
    }
    
    return result;
}

#define N_TMP_BOXES (16)

pixman_bool_t
pixman_region16_copy_from_region32 (pixman_region16_t *dst,
				    pixman_region32_t *src)
{
    int n_boxes, i;
    pixman_box32_t *boxes32;
    pixman_box16_t *boxes16;
    pixman_bool_t retval;
    
    boxes32 = pixman_region32_rectangles (src, &n_boxes);

    boxes16 = pixman_malloc_ab (n_boxes, sizeof (pixman_box16_t));

    if (!boxes16)
	return FALSE;
    
    for (i = 0; i < n_boxes; ++i)
    {
	boxes16[i].x1 = boxes32[i].x1;
	boxes16[i].y1 = boxes32[i].y1;
	boxes16[i].x2 = boxes32[i].x2;
	boxes16[i].y2 = boxes32[i].y2;
    }

    pixman_region_fini (dst);
    retval = pixman_region_init_rects (dst, boxes16, n_boxes);
    free (boxes16);
    return retval;
}

pixman_bool_t
pixman_region32_copy_from_region16 (pixman_region32_t *dst,
				    pixman_region16_t *src)
{
    int n_boxes, i;
    pixman_box16_t *boxes16;
    pixman_box32_t *boxes32;
    pixman_box32_t tmp_boxes[N_TMP_BOXES];
    pixman_bool_t retval;
    
    boxes16 = pixman_region_rectangles (src, &n_boxes);

    if (n_boxes > N_TMP_BOXES)
	boxes32 = pixman_malloc_ab (n_boxes, sizeof (pixman_box32_t));
    else
	boxes32 = tmp_boxes;
    
    if (!boxes32)
	return FALSE;
    
    for (i = 0; i < n_boxes; ++i)
    {
	boxes32[i].x1 = boxes16[i].x1;
	boxes32[i].y1 = boxes16[i].y1;
	boxes32[i].x2 = boxes16[i].x2;
	boxes32[i].y2 = boxes16[i].y2;
    }

    pixman_region32_fini (dst);
    retval = pixman_region32_init_rects (dst, boxes32, n_boxes);

    if (boxes32 != tmp_boxes)
	free (boxes32);

    return retval;
}

