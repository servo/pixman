/*
 * $Id$
 *
 * Copyright © 2004 Keith Packard
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
#include <config.h>
#include <string.h>
#include "pixman.h"
#include "pixman-private.h"

/*
 * 4 bit alpha
 */

#define N_BITS	4
#define rasterizeEdges	fbRasterizeEdges4

#if BITMAP_BIT_ORDER == LSBFirst
#define Shift4(o)	((o) << 2)
#else
#define Shift4(o)	((1-(o)) << 2)
#endif

#define Get4(x,o)	(((x) >> Shift4(o)) & 0xf)
#define Put4(x,o,v)	(((x) & ~(0xf << Shift4(o))) | (((v) & 0xf) << Shift4(o)))

#define DefineAlpha(line,x) \
    uint8_t   *__ap = (uint8_t *) line + ((x) >> 1); \
    int	    __ao = (x) & 1

#define StepAlpha	((__ap += __ao), (__ao ^= 1))

#define AddAlpha(a) {						\
    uint8_t   __o = READ(__ap);					\
    uint8_t   __a = (a) + Get4(__o, __ao);			\
    WRITE(__ap, Put4 (__o, __ao, __a | (0 - ((__a) >> 4))));	\
}

#include "pixman-edge-imp.h"

#undef AddAlpha
#undef StepAlpha
#undef DefineAlpha
#undef rasterizeEdges
#undef N_BITS


/*
 * 1 bit alpha
 */

#define N_BITS 1
#define rasterizeEdges	fbRasterizeEdges1

#include "pixman-edge-imp.h"

#undef rasterizeEdges
#undef N_BITS

/*
 * 8 bit alpha
 */

static inline uint8_t
clip255 (int x)
{
    if (x > 255) return 255;
    return x;
}

#define add_saturate_8(buf,val,length)				\
    do {							\
	int i__ = (length);					\
	uint8_t *buf__ = (buf);					\
	int val__ = (val);					\
								\
	while (i__--)						\
	{							\
	    WRITE((buf__), clip255 (READ((buf__)) + (val__)));	\
	    (buf__)++;						\
	}							\
    } while (0)

/*
 * We want to detect the case where we add the same value to a long
 * span of pixels.  The triangles on the end are filled in while we
 * count how many sub-pixel scanlines contribute to the middle section.
 *
 *                 +--------------------------+
 *  fill_height =|   \                      /
 *                     +------------------+
 *                      |================|
 *                   fill_start       fill_end
 */
static void
fbRasterizeEdges8 (pixman_image_t       *image,
		   pixman_edge_t	*l,
		   pixman_edge_t	*r,
		   pixman_fixed_t	t,
		   pixman_fixed_t	b)
{
    pixman_fixed_t  y = t;
    uint32_t  *line;
    int fill_start = -1, fill_end = -1;
    int fill_size = 0;
    uint32_t *buf = (image)->bits.bits;		
    uint32_t stride = (image)->bits.rowstride;	
    uint32_t width = (image)->bits.width;

    line = buf + pixman_fixed_to_int (y) * stride;

    for (;;)
    {
        uint8_t *ap = (uint8_t *) line;
	pixman_fixed_t	lx, rx;
	int	lxi, rxi;
	
	/* clip X */
	lx = l->x;
	if (lx < 0)
	    lx = 0;
	rx = r->x;
	if (pixman_fixed_to_int (rx) >= width)
	    rx = pixman_int_to_fixed (width);
	
	/* Skip empty (or backwards) sections */
	if (rx > lx)
	{
            int lxs, rxs;

	    /* Find pixel bounds for span. */
	    lxi = pixman_fixed_to_int (lx);
	    rxi = pixman_fixed_to_int (rx);

            /* Sample coverage for edge pixels */
            lxs = RenderSamplesX (lx, 8);
            rxs = RenderSamplesX (rx, 8);

            /* Add coverage across row */
            ACCESS_MEM (
		if (lxi == rxi)
		{
		    WRITE(ap +lxi, clip255 (READ(ap + lxi) + rxs - lxs));
		}
		else
		{
		    WRITE(ap + lxi, clip255 (READ(ap + lxi) + N_X_FRAC(8) - lxs));
		    
		    /* Move forward so that lxi/rxi is the pixel span */
		    lxi++;
		    
		    /* Don't bother trying to optimize the fill unless
		     * the span is longer than 4 pixels. */
		    if (rxi - lxi > 4)
		    {
			if (fill_start < 0)
			{
			    fill_start = lxi;
			    fill_end = rxi;
			    fill_size++;
			}
			else
			{
			    if (lxi >= fill_end || rxi < fill_start)
			    {
				/* We're beyond what we saved, just fill it */
				add_saturate_8 (ap + fill_start,
						fill_size * N_X_FRAC(8),
						fill_end - fill_start);
				fill_start = lxi;
				fill_end = rxi;
				fill_size = 1;
			    }
			    else
			    {
				/* Update fill_start */
				if (lxi > fill_start)
				{
				    add_saturate_8 (ap + fill_start,
						    fill_size * N_X_FRAC(8),
						    lxi - fill_start);
				    fill_start = lxi;
				}
				else if (lxi < fill_start)
				{
				    add_saturate_8 (ap + lxi, N_X_FRAC(8),
						    fill_start - lxi);
				}
				
				/* Update fill_end */
				if (rxi < fill_end)
				{
				    add_saturate_8 (ap + rxi,
						    fill_size * N_X_FRAC(8),
						    fill_end - rxi);
				    fill_end = rxi;
				}
				else if (fill_end < rxi)
				{
				    add_saturate_8 (ap + fill_end,
						    N_X_FRAC(8),
						    rxi - fill_end);
				}
				fill_size++;
			    }
			}
		    }
		    else
		    {
			add_saturate_8 (ap + lxi, N_X_FRAC(8), rxi - lxi);
		    }
		    
		    /* Do not add in a 0 alpha here. This check is
		     * necessary to avoid a buffer overrun, (when rx
		     * is exactly on a pixel boundary). */
		    if (rxs)
			WRITE(ap + rxi, clip255 (READ(ap + rxi) + rxs));
		});
	}

	if (y == b) {
            /* We're done, make sure we clean up any remaining fill. */
            if (fill_start != fill_end) {
                ACCESS_MEM(
		    if (fill_size == N_Y_FRAC(8))
		    {
			MEMSET_WRAPPED (ap + fill_start, 0xff, fill_end - fill_start);
		    }
		    else
		    {
			add_saturate_8 (ap + fill_start, fill_size * N_X_FRAC(8),
					fill_end - fill_start);
		    });
            }
	    break;
        }

	if (pixman_fixed_frac (y) != Y_FRAC_LAST(8))
	{
	    RenderEdgeStepSmall (l);
	    RenderEdgeStepSmall (r);
	    y += STEP_Y_SMALL(8);
	}
	else
	{
	    RenderEdgeStepBig (l);
	    RenderEdgeStepBig (r);
	    y += STEP_Y_BIG(8);
            if (fill_start != fill_end)
            {
                ACCESS_MEM(
		    if (fill_size == N_Y_FRAC(8))
		    {
			MEMSET_WRAPPED (ap + fill_start, 0xff, fill_end - fill_start);
		    }
		    else
		    {
			add_saturate_8 (ap + fill_start, fill_size * N_X_FRAC(8),
					fill_end - fill_start);
		    });
                fill_start = fill_end = -1;
                fill_size = 0;
            }
	    line += stride;
	}
    }
}

void
pixman_rasterize_edges (pixman_image_t *image,
			pixman_edge_t	*l,
			pixman_edge_t	*r,
			pixman_fixed_t	t,
			pixman_fixed_t	b)
{
    switch (PIXMAN_FORMAT_BPP (image->bits.format))
    {
    case 1:
	fbRasterizeEdges1 (image, l, r, t, b);
	break;
    case 4:
	fbRasterizeEdges4 (image, l, r, t, b);
	break;
    case 8:
	fbRasterizeEdges8 (image, l, r, t, b);
	break;
    }
}

/*
 * Compute the smallest value no less than y which is on a
 * grid row
 */

pixman_fixed_t
pixman_sample_ceil_y (pixman_fixed_t y, int n)
{
    pixman_fixed_t   f = pixman_fixed_frac(y);
    pixman_fixed_t   i = pixman_fixed_floor(y);
    
    f = ((f + Y_FRAC_FIRST(n)) / STEP_Y_SMALL(n)) * STEP_Y_SMALL(n) + Y_FRAC_FIRST(n);
    if (f > Y_FRAC_LAST(n))
    {
	f = Y_FRAC_FIRST(n);
	i += pixman_fixed_1;
    }
    return (i | f);
}

#define _div(a,b)    ((a) >= 0 ? (a) / (b) : -((-(a) + (b) - 1) / (b)))

/*
 * Compute the largest value no greater than y which is on a
 * grid row
 */
pixman_fixed_t
pixman_sample_floor_y (pixman_fixed_t y, int n)
{
    pixman_fixed_t   f = pixman_fixed_frac(y);
    pixman_fixed_t   i = pixman_fixed_floor (y);
    
    f = _div(f - Y_FRAC_FIRST(n), STEP_Y_SMALL(n)) * STEP_Y_SMALL(n) + Y_FRAC_FIRST(n);
    if (f < Y_FRAC_FIRST(n))
    {
	f = Y_FRAC_LAST(n);
	i -= pixman_fixed_1;
    }
    return (i | f);
}

/*
 * Step an edge by any amount (including negative values)
 */
void
pixman_edge_step (pixman_edge_t *e, int n)
{
    pixman_fixed_48_16_t	ne;

    e->x += n * e->stepx;
    
    ne = e->e + n * (pixman_fixed_48_16_t) e->dx;
    
    if (n >= 0)
    {
	if (ne > 0)
	{
	    int nx = (ne + e->dy - 1) / e->dy;
	    e->e = ne - nx * (pixman_fixed_48_16_t) e->dy;
	    e->x += nx * e->signdx;
	}
    }
    else
    {
	if (ne <= -e->dy)
	{
	    int nx = (-ne) / e->dy;
	    e->e = ne + nx * (pixman_fixed_48_16_t) e->dy;
	    e->x -= nx * e->signdx;
	}
    }
}

/*
 * A private routine to initialize the multi-step
 * elements of an edge structure
 */
static void
_pixman_edge_tMultiInit (pixman_edge_t *e, int n, pixman_fixed_t *stepx_p, pixman_fixed_t *dx_p)
{
    pixman_fixed_t	stepx;
    pixman_fixed_48_16_t	ne;
    
    ne = n * (pixman_fixed_48_16_t) e->dx;
    stepx = n * e->stepx;
    if (ne > 0)
    {
	int nx = ne / e->dy;
	ne -= nx * e->dy;
	stepx += nx * e->signdx;
    }
    *dx_p = ne;
    *stepx_p = stepx;
}

/*
 * Initialize one edge structure given the line endpoints and a
 * starting y value
 */
void
pixman_edge_init (pixman_edge_t	*e,
		  int		n,
		  pixman_fixed_t		y_start,
		  pixman_fixed_t		x_top,
		  pixman_fixed_t		y_top,
		  pixman_fixed_t		x_bot,
		  pixman_fixed_t		y_bot)
{
    pixman_fixed_t	dx, dy;

    e->x = x_top;
    e->e = 0;
    dx = x_bot - x_top;
    dy = y_bot - y_top;
    e->dy = dy;
    e->dx = 0;
    if (dy)
    {
	if (dx >= 0)
	{
	    e->signdx = 1;
	    e->stepx = dx / dy;
	    e->dx = dx % dy;
	    e->e = -dy;
	}
	else
	{
	    e->signdx = -1;
	    e->stepx = -(-dx / dy);
	    e->dx = -dx % dy;
	    e->e = 0;
	}
    
	_pixman_edge_tMultiInit (e, STEP_Y_SMALL(n), &e->stepx_small, &e->dx_small);
	_pixman_edge_tMultiInit (e, STEP_Y_BIG(n), &e->stepx_big, &e->dx_big);
    }
    pixman_edge_step (e, y_start - y_top);
}

/*
 * Initialize one edge structure given a line, starting y value
 * and a pixel offset for the line
 */
void
pixman_line_fixed_edge_init (pixman_edge_t *e,
			     int	    n,
			     pixman_fixed_t	    y,
			     const pixman_line_fixed_t *line,
			     int	    x_off,
			     int	    y_off)
{
    pixman_fixed_t	x_off_fixed = pixman_int_to_fixed(x_off);
    pixman_fixed_t	y_off_fixed = pixman_int_to_fixed(y_off);
    const pixman_point_fixed_t *top, *bot;

    if (line->p1.y <= line->p2.y)
    {
	top = &line->p1;
	bot = &line->p2;
    }
    else
    {
	top = &line->p2;
	bot = &line->p1;
    }
    pixman_edge_init (e, n, y,
		    top->x + x_off_fixed,
		    top->y + y_off_fixed,
		    bot->x + x_off_fixed,
		    bot->y + y_off_fixed);
}

