/*
 * Copyright 2010, 2012, Soren Sandmann <sandmann@cs.au.dk>
 * Copyright 2010, 2011, 2012, Red Hat, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Soren Sandmann <sandmann@cs.au.dk>
 */
#include <config.h>
#include <stdlib.h>
#include "pixman-private.h"

typedef struct glyph_metrics_t glyph_metrics_t;
typedef struct glyph_t glyph_t;

#define TOMBSTONE ((glyph_t *)0x1)

/* XXX: These numbers are arbitrary---we've never done any measurements.
 */
#define N_GLYPHS_HIGH_WATER  (16384)
#define N_GLYPHS_LOW_WATER   (8192)
#define HASH_SIZE (2 * N_GLYPHS_HIGH_WATER)
#define HASH_MASK (HASH_SIZE - 1)

struct glyph_t
{
    void *		font_key;
    void *		glyph_key;
    int			origin_x;
    int			origin_y;
    pixman_image_t *	image;
    pixman_link_t	mru_link;
};

struct pixman_glyph_cache_t
{
    int			n_glyphs;
    int			n_tombstones;
    int			freeze_count;
    pixman_list_t	mru;
    glyph_t *		glyphs[HASH_SIZE];
};

static void
free_glyph (glyph_t *glyph)
{
    pixman_list_unlink (&glyph->mru_link);
    pixman_image_unref (glyph->image);
    free (glyph);
}

static unsigned int
hash (const void *font_key, const void *glyph_key)
{
    size_t key = (size_t)font_key + (size_t)glyph_key;

    /* This hash function is based on one found on Thomas Wang's
     * web page at
     *
     *    http://www.concentric.net/~Ttwang/tech/inthash.htm
     *
     */
    key = (key << 15) - key - 1;
    key = key ^ (key >> 12);
    key = key + (key << 2);
    key = key ^ (key >> 4);
    key = key + (key << 3) + (key << 11);
    key = key ^ (key >> 16);

    return key;
}

static glyph_t *
lookup_glyph (pixman_glyph_cache_t *cache,
	      void                 *font_key,
	      void                 *glyph_key)
{
    unsigned idx;
    glyph_t *g;

    idx = hash (font_key, glyph_key);
    while ((g = cache->glyphs[idx++ & HASH_MASK]))
    {
	if (g != TOMBSTONE			&&
	    g->font_key == font_key		&&
	    g->glyph_key == glyph_key)
	{
	    return g;
	}
    }

    return NULL;
}

static void
insert_glyph (pixman_glyph_cache_t *cache,
	      glyph_t              *glyph)
{
    unsigned idx;
    glyph_t **loc;

    idx = hash (glyph->font_key, glyph->glyph_key);

    /* Note: we assume that there is room in the table. If there isn't,
     * this will be an infinite loop.
     */
    do
    {
	loc = &cache->glyphs[idx++ & HASH_MASK];
    } while (*loc && *loc != TOMBSTONE);

    if (*loc == TOMBSTONE)
	cache->n_tombstones--;
    cache->n_glyphs++;

    *loc = glyph;
}

static void
remove_glyph (pixman_glyph_cache_t *cache,
	      glyph_t              *glyph)
{
    unsigned idx;

    idx = hash (glyph->font_key, glyph->glyph_key);
    while (cache->glyphs[idx & HASH_MASK] != glyph)
	idx++;

    cache->glyphs[idx & HASH_MASK] = TOMBSTONE;
    cache->n_tombstones++;
    cache->n_glyphs--;

    /* Eliminate tombstones if possible */
    if (cache->glyphs[(idx + 1) & HASH_MASK] == NULL)
    {
	while (cache->glyphs[idx & HASH_MASK] == TOMBSTONE)
	{
	    cache->glyphs[idx & HASH_MASK] = NULL;
	    cache->n_tombstones--;
	    idx--;
	}
    }
}

static void
clear_table (pixman_glyph_cache_t *cache)
{
    int i;

    for (i = 0; i < HASH_SIZE; ++i)
    {
	glyph_t *glyph = cache->glyphs[i];

	if (glyph && glyph != TOMBSTONE)
	    free_glyph (glyph);

	cache->glyphs[i] = NULL;
    }

    cache->n_glyphs = 0;
    cache->n_tombstones = 0;
}

PIXMAN_EXPORT pixman_glyph_cache_t *
pixman_glyph_cache_create (void)
{
    pixman_glyph_cache_t *cache;

    if (!(cache = malloc (sizeof *cache)))
	return NULL;

    memset (cache->glyphs, 0, sizeof (cache->glyphs));
    cache->n_glyphs = 0;
    cache->n_tombstones = 0;
    cache->freeze_count = 0;

    pixman_list_init (&cache->mru);

    return cache;
}

PIXMAN_EXPORT void
pixman_glyph_cache_destroy (pixman_glyph_cache_t *cache)
{
    return_if_fail (cache->freeze_count == 0);

    clear_table (cache);

    free (cache);
}

PIXMAN_EXPORT void
pixman_glyph_cache_freeze (pixman_glyph_cache_t  *cache)
{
    cache->freeze_count++;
}

PIXMAN_EXPORT void
pixman_glyph_cache_thaw (pixman_glyph_cache_t  *cache)
{
    if (--cache->freeze_count == 0					&&
	cache->n_glyphs + cache->n_tombstones > N_GLYPHS_HIGH_WATER)
    {
	if (cache->n_tombstones > N_GLYPHS_HIGH_WATER)
	{
	    /* More than half the entries are
	     * tombstones. Just dump the whole table.
	     */
	    clear_table (cache);
	}

	while (cache->n_glyphs > N_GLYPHS_LOW_WATER)
	{
	    glyph_t *glyph = CONTAINER_OF (glyph_t, mru_link, cache->mru.tail);

	    remove_glyph (cache, glyph);
	    free_glyph (glyph);
	}
    }
}

PIXMAN_EXPORT const void *
pixman_glyph_cache_lookup (pixman_glyph_cache_t  *cache,
			   void                  *font_key,
			   void                  *glyph_key)
{
    return lookup_glyph (cache, font_key, glyph_key);
}

PIXMAN_EXPORT const void *
pixman_glyph_cache_insert (pixman_glyph_cache_t  *cache,
			   void                  *font_key,
			   void                  *glyph_key,
			   int			  origin_x,
			   int                    origin_y,
			   pixman_image_t        *image)
{
    glyph_t *glyph;
    int32_t width, height;

    return_val_if_fail (cache->freeze_count > 0, NULL);
    return_val_if_fail (image->type == BITS, NULL);

    width = image->bits.width;
    height = image->bits.height;

    if (cache->n_glyphs >= HASH_SIZE)
	return NULL;

    if (!(glyph = malloc (sizeof *glyph)))
	return NULL;

    glyph->font_key = font_key;
    glyph->glyph_key = glyph_key;
    glyph->origin_x = origin_x;
    glyph->origin_y = origin_y;

    if (!(glyph->image = pixman_image_create_bits (
	      image->bits.format, width, height, NULL, -1)))
    {
	free (glyph);
	return NULL;
    }

    pixman_image_composite32 (PIXMAN_OP_SRC,
			      image, NULL, glyph->image, 0, 0, 0, 0, 0, 0,
			      width, height);

    if (PIXMAN_FORMAT_A   (glyph->image->bits.format) != 0	&&
	PIXMAN_FORMAT_RGB (glyph->image->bits.format) != 0)
    {
	pixman_image_set_component_alpha (glyph->image, TRUE);
    }

    pixman_list_prepend (&cache->mru, &glyph->mru_link);

    _pixman_image_validate (glyph->image);
    insert_glyph (cache, glyph);

    return glyph;
}

PIXMAN_EXPORT void
pixman_glyph_cache_remove (pixman_glyph_cache_t  *cache,
			   void                  *font_key,
			   void                  *glyph_key)
{
    glyph_t *glyph;

    if ((glyph = lookup_glyph (cache, font_key, glyph_key)))
    {
	remove_glyph (cache, glyph);

	free_glyph (glyph);
    }
}

PIXMAN_EXPORT void
pixman_glyph_get_extents (pixman_glyph_cache_t *cache,
			  int                   n_glyphs,
			  pixman_glyph_t       *glyphs,
			  pixman_box32_t       *extents)
{
    int i;

    extents->x1 = extents->y1 = INT32_MAX;
    extents->x2 = extents->y2 = INT32_MIN;

    for (i = 0; i < n_glyphs; ++i)
    {
	glyph_t *glyph = (glyph_t *)glyphs[i].glyph;
	int x1, y1, x2, y2;

	x1 = glyphs[i].x - glyph->origin_x;
	y1 = glyphs[i].y - glyph->origin_y;
	x2 = glyphs[i].x - glyph->origin_x + glyph->image->bits.width;
	y2 = glyphs[i].y - glyph->origin_y + glyph->image->bits.height;

	if (x1 < extents->x1)
	    extents->x1 = x1;
	if (y1 < extents->y1)
	    extents->y1 = y1;
	if (x2 > extents->x2)
	    extents->x2 = x2;
	if (y2 > extents->y2)
	    extents->y2 = y2;
    }
}

/* This function returns a format that is suitable for use as a mask for the
 * set of glyphs in question.
 */
PIXMAN_EXPORT pixman_format_code_t
pixman_glyph_get_mask_format (pixman_glyph_cache_t *cache,
			      int		    n_glyphs,
			      pixman_glyph_t *      glyphs)
{
    pixman_format_code_t format = PIXMAN_a1;
    int i;

    for (i = 0; i < n_glyphs; ++i)
    {
	const glyph_t *glyph = glyphs[i].glyph;
	pixman_format_code_t glyph_format = glyph->image->bits.format;

	if (PIXMAN_FORMAT_TYPE (glyph_format) == PIXMAN_TYPE_A)
	{
	    if (PIXMAN_FORMAT_A (glyph_format) > PIXMAN_FORMAT_A (format))
		format = glyph_format;
	}
	else
	{
	    return PIXMAN_a8r8g8b8;
	}
    }

    return format;
}

PIXMAN_EXPORT void
pixman_composite_glyphs_no_mask (pixman_op_t            op,
				 pixman_image_t        *src,
				 pixman_image_t        *dest,
				 int32_t                src_x,
				 int32_t                src_y,
				 int32_t                dest_x,
				 int32_t                dest_y,
				 pixman_glyph_cache_t  *cache,
				 int                    n_glyphs,
				 pixman_glyph_t        *glyphs)
{
    int i;

    for (i = 0; i < n_glyphs; ++i)
    {
	glyph_t *glyph = (glyph_t *)glyphs[i].glyph;
	pixman_image_t *glyph_img = glyph->image;

	pixman_image_composite32 (op, src, glyph_img, dest,
				  src_x + glyphs[i].x - glyph->origin_x,
				  src_y + glyphs[i].y - glyph->origin_y,
				  0, 0,
				  dest_x + glyphs[i].x - glyph->origin_x,
				  dest_y + glyphs[i].y - glyph->origin_y,
				  glyph_img->bits.width,
				  glyph_img->bits.height);

	pixman_list_move_to_front (&cache->mru, &glyph->mru_link);
    }
}

/* Conceptually, for each glyph, (white IN glyph) is PIXMAN_OP_ADDed to an
 * infinitely big mask image at the position such that the glyph origin point
 * is positioned at the (glyphs[i].x, glyphs[i].y) point.
 *
 * Then (mask_x, mask_y) in the infinite mask and (src_x, src_y) in the source
 * image are both aligned with (dest_x, dest_y) in the destination image. Then
 * these three images are composited within the 
 *
 *       (dest_x, dest_y, dst_x + width, dst_y + height)
 *
 * rectangle.
 *
 * TODO:
 *   - Trim the mask to the destination clip/image?
 *   - Trim composite region based on sources, when the op ignores 0s.
 */
PIXMAN_EXPORT void
pixman_composite_glyphs (pixman_op_t            op,
			 pixman_image_t        *src,
			 pixman_image_t        *dest,
			 pixman_format_code_t   mask_format,
			 int32_t                src_x,
			 int32_t                src_y,
			 int32_t		mask_x,
			 int32_t		mask_y,
			 int32_t                dest_x,
			 int32_t                dest_y,
			 int32_t                width,
			 int32_t                height,
			 pixman_glyph_cache_t  *cache,
			 int			n_glyphs,
			 pixman_glyph_t        *glyphs)
{
    pixman_color_t white_color = { 0xffff, 0xffff, 0xffff, 0xffff };
    pixman_image_t *white;
    pixman_image_t *mask;
    int i;

    if (!(mask = pixman_image_create_bits (mask_format, width, height, NULL, -1)))
	return;

    if (!(white = pixman_image_create_solid_fill (&white_color)))
	goto out;

    if (PIXMAN_FORMAT_A   (mask_format) != 0 &&
	PIXMAN_FORMAT_RGB (mask_format) != 0)
    {
	pixman_image_set_component_alpha (mask, TRUE);
    }

    for (i = 0; i < n_glyphs; ++i)
    {
	glyph_t *glyph = (glyph_t *)glyphs[i].glyph;
	pixman_image_t *glyph_img = glyph->image;

	pixman_image_composite32 (PIXMAN_OP_ADD, white, glyph_img, mask,
				  0, 0, 0, 0,
				  glyphs[i].x - glyph->origin_x - mask_x,
				  glyphs[i].y - glyph->origin_y - mask_y,
				  glyph->image->bits.width,
				  glyph->image->bits.height);

	pixman_list_move_to_front (&cache->mru, &glyph->mru_link);
    }

    pixman_image_composite32 (op, src, mask, dest,
			      src_x, src_y,
			      0, 0,
			      dest_x, dest_y,
			      width, height);

out:
    pixman_image_unref (mask);
}
