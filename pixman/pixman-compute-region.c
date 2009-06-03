/*
 *
 * Copyright © 1999 Keith Packard
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include "pixman-private.h"

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
	return FALSE;
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

static void
print_region (pixman_region32_t *region, const char *header)
{
    int n_boxes;
    pixman_box32_t *boxes = pixman_region32_rectangles (region, &n_boxes);
    int i;

    printf ("%s\n", header);
    for (i = 0; i < n_boxes; ++i)
    {
	pixman_box32_t *box = &(boxes[i]);

	printf ("   %d %d %d %d\n", box->x1, box->y1, box->x2, box->y2);
    }
}

pixman_bool_t
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
    pRegion->extents.x2 = MIN (pRegion->extents.x2, pDst->bits.width);
    pRegion->extents.y2 = MIN (pRegion->extents.y2, pDst->bits.height);
    
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
			       -pDst->common.alpha_origin.x,
			       -pDst->common.alpha_origin.y))
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
			       xDst - (xSrc - pSrc->common.alpha_origin.x),
			       yDst - (ySrc - pSrc->common.alpha_origin.y)))
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
				   xDst - (xMask - pMask->common.alpha_origin.x),
				   yDst - (yMask - pMask->common.alpha_origin.y)))
	    {
		pixman_region32_fini (pRegion);
		return FALSE;
	    }
	}
    }

#if 0
    print_region (pRegion, "composite region");
#endif
    
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
