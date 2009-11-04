/*
 * Copyright © 2009 ARM Ltd, Movial Creative Technologies Oy
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of ARM Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  ARM Ltd makes no
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
 *
 * Author:  Ian Rickards (ian.rickards@arm.com)
 * Author:  Jonathan Morton (jonathan.morton@movial.com)
 * Author:  Markku Vire (markku.vire@movial.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "pixman-private.h"

void
pixman_composite_src_n_8_asm_neon (int32_t   w,
                                   int32_t   h,
                                   uint8_t  *dst,
                                   int32_t   dst_stride,
                                   uint8_t   src);

void
pixman_composite_src_n_0565_asm_neon (int32_t   w,
                                      int32_t   h,
                                      uint16_t *dst,
                                      int32_t   dst_stride,
                                      uint16_t  src);

void
pixman_composite_src_n_8888_asm_neon (int32_t   w,
                                      int32_t   h,
                                      uint32_t *dst,
                                      int32_t   dst_stride,
                                      uint32_t  src);

static pixman_bool_t
pixman_fill_neon (uint32_t *bits,
                  int       stride,
                  int       bpp,
                  int       x,
                  int       y,
                  int       width,
                  int       height,
                  uint32_t  _xor)
{
    /* stride is always multiple of 32bit units in pixman */
    uint32_t byte_stride = stride * sizeof(uint32_t);

    switch (bpp)
    {
    case 8:
	pixman_composite_src_n_8_asm_neon (
		width,
		height,
		(uint8_t *)(((char *) bits) + y * byte_stride + x),
		byte_stride,
		_xor & 0xff);
	return TRUE;
    case 16:
	pixman_composite_src_n_0565_asm_neon (
		width,
		height,
		(uint16_t *)(((char *) bits) + y * byte_stride + x * 2),
		byte_stride / 2,
		_xor & 0xffff);
	return TRUE;
    case 32:
	pixman_composite_src_n_8888_asm_neon (
		width,
		height,
		(uint32_t *)(((char *) bits) + y * byte_stride + x * 4),
		byte_stride / 4,
		_xor);
	return TRUE;
    default:
	return FALSE;
    }
}

static const pixman_fast_path_t arm_neon_fast_path_array[] =
{
    { PIXMAN_OP_NONE },
};

const pixman_fast_path_t *const arm_neon_fast_paths = arm_neon_fast_path_array;

static void
arm_neon_composite (pixman_implementation_t *imp,
                    pixman_op_t              op,
                    pixman_image_t *         src,
                    pixman_image_t *         mask,
                    pixman_image_t *         dest,
                    int32_t                  src_x,
                    int32_t                  src_y,
                    int32_t                  mask_x,
                    int32_t                  mask_y,
                    int32_t                  dest_x,
                    int32_t                  dest_y,
                    int32_t                  width,
                    int32_t                  height)
{
    if (_pixman_run_fast_path (arm_neon_fast_paths, imp,
                               op, src, mask, dest,
                               src_x, src_y,
                               mask_x, mask_y,
                               dest_x, dest_y,
                               width, height))
    {
	return;
    }

    _pixman_implementation_composite (imp->delegate, op,
                                      src, mask, dest,
                                      src_x, src_y,
                                      mask_x, mask_y,
                                      dest_x, dest_y,
                                      width, height);
}

static pixman_bool_t
arm_neon_fill (pixman_implementation_t *imp,
               uint32_t *               bits,
               int                      stride,
               int                      bpp,
               int                      x,
               int                      y,
               int                      width,
               int                      height,
               uint32_t xor)
{
    if (pixman_fill_neon (bits, stride, bpp, x, y, width, height, xor))
	return TRUE;

    return _pixman_implementation_fill (
	imp->delegate, bits, stride, bpp, x, y, width, height, xor);
}

pixman_implementation_t *
_pixman_implementation_create_arm_neon (void)
{
    pixman_implementation_t *general = _pixman_implementation_create_fast_path ();
    pixman_implementation_t *imp = _pixman_implementation_create (general);

    imp->composite = arm_neon_composite;
    imp->fill = arm_neon_fill;

    return imp;
}
