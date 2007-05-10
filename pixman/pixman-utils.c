/*
 * Copyright © 2000 SuSE, Inc.
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

#include "pixman.h"
#include "pixman-private.h"

pixman_bool_t
pixman_transform_point_3d (pixman_transform_t *transform,
			   pixman_vector_t *vector)
{
    pixman_vector_t		result;
    int				i, j;
    pixman_fixed_32_32_t	partial;
    pixman_fixed_48_16_t	v;

    for (j = 0; j < 3; j++)
    {
	v = 0;
	for (i = 0; i < 3; i++)
	{
	    partial = ((pixman_fixed_48_16_t) transform->matrix[j][i] *
		       (pixman_fixed_48_16_t) vector->vector[i]);
	    v += partial >> 16;
	}

	if (v > pixman_max_fixed_48_16 || v < pixman_min_fixed_48_16)
	    return FALSE;

	result.vector[j] = (pixman_fixed_48_16_t) v;
    }
    
    if (!result.vector[2])
	return FALSE;
    
    *vector = result;
    return TRUE;
}
