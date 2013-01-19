/*
 * Copyright © 2008 Mozilla Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Mozilla Corporation not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Mozilla Corporation makes no
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
 * Author:  Jeff Muizelaar (jeff@infidigm.net)
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pixman-private.h"
#include "pixman-arm-common.h"
#include "pixman-inlines.h"

#if 0 /* This code was moved to 'pixman-arm-simd-asm.S' */

void
pixman_composite_add_8_8_asm_armv6 (int32_t  width,
				    int32_t  height,
				    uint8_t *dst_line,
				    int32_t  dst_stride,
				    uint8_t *src_line,
				    int32_t  src_stride)
{
    uint8_t *dst, *src;
    int32_t w;
    uint8_t s, d;

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	src = src_line;
	src_line += src_stride;
	w = width;

	/* ensure both src and dst are properly aligned before doing 32 bit reads
	 * we'll stay in this loop if src and dst have differing alignments
	 */
	while (w && (((uintptr_t)dst & 3) || ((uintptr_t)src & 3)))
	{
	    s = *src;
	    d = *dst;
	    asm ("uqadd8 %0, %1, %2" : "+r" (d) : "r" (s));
	    *dst = d;

	    dst++;
	    src++;
	    w--;
	}

	while (w >= 4)
	{
	    asm ("uqadd8 %0, %1, %2"
		 : "=r" (*(uint32_t*)dst)
		 : "r" (*(uint32_t*)src), "r" (*(uint32_t*)dst));
	    dst += 4;
	    src += 4;
	    w -= 4;
	}

	while (w)
	{
	    s = *src;
	    d = *dst;
	    asm ("uqadd8 %0, %1, %2" : "+r" (d) : "r" (s));
	    *dst = d;

	    dst++;
	    src++;
	    w--;
	}
    }

}

void
pixman_composite_over_8888_8888_asm_armv6 (int32_t   width,
                                           int32_t   height,
                                           uint32_t *dst_line,
                                           int32_t   dst_stride,
                                           uint32_t *src_line,
                                           int32_t   src_stride)
{
    uint32_t    *dst;
    uint32_t    *src;
    int32_t w;
    uint32_t component_half = 0x800080;
    uint32_t upper_component_mask = 0xff00ff00;
    uint32_t alpha_mask = 0xff;

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	src = src_line;
	src_line += src_stride;
	w = width;

/* #define inner_branch */
	asm volatile (
	    "cmp %[w], #0\n\t"
	    "beq 2f\n\t"
	    "1:\n\t"
	    /* load src */
	    "ldr r5, [%[src]], #4\n\t"
#ifdef inner_branch
	    /* We can avoid doing the multiplication in two cases: 0x0 or 0xff.
	     * The 0x0 case also allows us to avoid doing an unecessary data
	     * write which is more valuable so we only check for that
	     */
	    "cmp r5, #0\n\t"
	    "beq 3f\n\t"

	    /* = 255 - alpha */
	    "sub r8, %[alpha_mask], r5, lsr #24\n\t"

	    "ldr r4, [%[dest]] \n\t"

#else
	    "ldr r4, [%[dest]] \n\t"

	    /* = 255 - alpha */
	    "sub r8, %[alpha_mask], r5, lsr #24\n\t"
#endif
	    "uxtb16 r6, r4\n\t"
	    "uxtb16 r7, r4, ror #8\n\t"

	    /* multiply by 257 and divide by 65536 */
	    "mla r6, r6, r8, %[component_half]\n\t"
	    "mla r7, r7, r8, %[component_half]\n\t"

	    "uxtab16 r6, r6, r6, ror #8\n\t"
	    "uxtab16 r7, r7, r7, ror #8\n\t"

	    /* recombine the 0xff00ff00 bytes of r6 and r7 */
	    "and r7, r7, %[upper_component_mask]\n\t"
	    "uxtab16 r6, r7, r6, ror #8\n\t"

	    "uqadd8 r5, r6, r5\n\t"

#ifdef inner_branch
	    "3:\n\t"

#endif
	    "str r5, [%[dest]], #4\n\t"
	    /* increment counter and jmp to top */
	    "subs	%[w], %[w], #1\n\t"
	    "bne	1b\n\t"
	    "2:\n\t"
	    : [w] "+r" (w), [dest] "+r" (dst), [src] "+r" (src)
	    : [component_half] "r" (component_half), [upper_component_mask] "r" (upper_component_mask),
	      [alpha_mask] "r" (alpha_mask)
	    : "r4", "r5", "r6", "r7", "r8", "cc", "memory"
	    );
    }
}

void
pixman_composite_over_8888_n_8888_asm_armv6 (int32_t   width,
                                             int32_t   height,
                                             uint32_t *dst_line,
                                             int32_t   dst_stride,
                                             uint32_t *src_line,
                                             int32_t   src_stride,
                                             uint32_t  mask)
{
    uint32_t *dst;
    uint32_t *src;
    int32_t w;
    uint32_t component_half = 0x800080;
    uint32_t alpha_mask = 0xff;

    mask = (mask) >> 24;

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	src = src_line;
	src_line += src_stride;
	w = width;

/* #define inner_branch */
	asm volatile (
	    "cmp %[w], #0\n\t"
	    "beq 2f\n\t"
	    "1:\n\t"
	    /* load src */
	    "ldr r5, [%[src]], #4\n\t"
#ifdef inner_branch
	    /* We can avoid doing the multiplication in two cases: 0x0 or 0xff.
	     * The 0x0 case also allows us to avoid doing an unecessary data
	     * write which is more valuable so we only check for that
	     */
	    "cmp r5, #0\n\t"
	    "beq 3f\n\t"

#endif
	    "ldr r4, [%[dest]] \n\t"

	    "uxtb16 r6, r5\n\t"
	    "uxtb16 r7, r5, ror #8\n\t"

	    /* multiply by alpha (r8) then by 257 and divide by 65536 */
	    "mla r6, r6, %[mask_alpha], %[component_half]\n\t"
	    "mla r7, r7, %[mask_alpha], %[component_half]\n\t"

	    "uxtab16 r6, r6, r6, ror #8\n\t"
	    "uxtab16 r7, r7, r7, ror #8\n\t"

	    "uxtb16 r6, r6, ror #8\n\t"
	    "uxtb16 r7, r7, ror #8\n\t"

	    /* recombine */
	    "orr r5, r6, r7, lsl #8\n\t"

	    "uxtb16 r6, r4\n\t"
	    "uxtb16 r7, r4, ror #8\n\t"

	    /* 255 - alpha */
	    "sub r8, %[alpha_mask], r5, lsr #24\n\t"

	    /* multiply by alpha (r8) then by 257 and divide by 65536 */
	    "mla r6, r6, r8, %[component_half]\n\t"
	    "mla r7, r7, r8, %[component_half]\n\t"

	    "uxtab16 r6, r6, r6, ror #8\n\t"
	    "uxtab16 r7, r7, r7, ror #8\n\t"

	    "uxtb16 r6, r6, ror #8\n\t"
	    "uxtb16 r7, r7, ror #8\n\t"

	    /* recombine */
	    "orr r6, r6, r7, lsl #8\n\t"

	    "uqadd8 r5, r6, r5\n\t"

#ifdef inner_branch
	    "3:\n\t"

#endif
	    "str r5, [%[dest]], #4\n\t"
	    /* increment counter and jmp to top */
	    "subs	%[w], %[w], #1\n\t"
	    "bne	1b\n\t"
	    "2:\n\t"
	    : [w] "+r" (w), [dest] "+r" (dst), [src] "+r" (src)
	    : [component_half] "r" (component_half), [mask_alpha] "r" (mask),
	      [alpha_mask] "r" (alpha_mask)
	    : "r4", "r5", "r6", "r7", "r8", "r9", "cc", "memory"
	    );
    }
}

void
pixman_composite_over_n_8_8888_asm_armv6 (int32_t   width,
                                          int32_t   height,
                                          uint32_t *dst_line,
                                          int32_t   dst_stride,
                                          uint32_t  src,
                                          int32_t   unused,
                                          uint8_t  *mask_line,
                                          int32_t   mask_stride)
{
    uint32_t  srca;
    uint32_t *dst;
    uint8_t  *mask;
    int32_t w;

    srca = src >> 24;

    uint32_t component_mask = 0xff00ff;
    uint32_t component_half = 0x800080;

    uint32_t src_hi = (src >> 8) & component_mask;
    uint32_t src_lo = src & component_mask;

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	mask = mask_line;
	mask_line += mask_stride;
	w = width;

/* #define inner_branch */
	asm volatile (
	    "cmp %[w], #0\n\t"
	    "beq 2f\n\t"
	    "1:\n\t"
	    /* load mask */
	    "ldrb r5, [%[mask]], #1\n\t"
#ifdef inner_branch
	    /* We can avoid doing the multiplication in two cases: 0x0 or 0xff.
	     * The 0x0 case also allows us to avoid doing an unecessary data
	     * write which is more valuable so we only check for that
	     */
	    "cmp r5, #0\n\t"
	    "beq 3f\n\t"

#endif
	    "ldr r4, [%[dest]] \n\t"

	    /* multiply by alpha (r8) then by 257 and divide by 65536 */
	    "mla r6, %[src_lo], r5, %[component_half]\n\t"
	    "mla r7, %[src_hi], r5, %[component_half]\n\t"

	    "uxtab16 r6, r6, r6, ror #8\n\t"
	    "uxtab16 r7, r7, r7, ror #8\n\t"

	    "uxtb16 r6, r6, ror #8\n\t"
	    "uxtb16 r7, r7, ror #8\n\t"

	    /* recombine */
	    "orr r5, r6, r7, lsl #8\n\t"

	    "uxtb16 r6, r4\n\t"
	    "uxtb16 r7, r4, ror #8\n\t"

	    /* we could simplify this to use 'sub' if we were
	     * willing to give up a register for alpha_mask
	     */
	    "mvn r8, r5\n\t"
	    "mov r8, r8, lsr #24\n\t"

	    /* multiply by alpha (r8) then by 257 and divide by 65536 */
	    "mla r6, r6, r8, %[component_half]\n\t"
	    "mla r7, r7, r8, %[component_half]\n\t"

	    "uxtab16 r6, r6, r6, ror #8\n\t"
	    "uxtab16 r7, r7, r7, ror #8\n\t"

	    "uxtb16 r6, r6, ror #8\n\t"
	    "uxtb16 r7, r7, ror #8\n\t"

	    /* recombine */
	    "orr r6, r6, r7, lsl #8\n\t"

	    "uqadd8 r5, r6, r5\n\t"

#ifdef inner_branch
	    "3:\n\t"

#endif
	    "str r5, [%[dest]], #4\n\t"
	    /* increment counter and jmp to top */
	    "subs	%[w], %[w], #1\n\t"
	    "bne	1b\n\t"
	    "2:\n\t"
	    : [w] "+r" (w), [dest] "+r" (dst), [src] "+r" (src), [mask] "+r" (mask)
	    : [component_half] "r" (component_half),
	      [src_hi] "r" (src_hi), [src_lo] "r" (src_lo)
	    : "r4", "r5", "r6", "r7", "r8", "cc", "memory");
    }
}

#endif

PIXMAN_ARM_BIND_FAST_PATH_SRC_DST (armv6, src_8888_8888,
		                   uint32_t, 1, uint32_t, 1)
PIXMAN_ARM_BIND_FAST_PATH_SRC_DST (armv6, src_0565_0565,
                                   uint16_t, 1, uint16_t, 1)
PIXMAN_ARM_BIND_FAST_PATH_SRC_DST (armv6, src_8_8,
                                   uint8_t, 1, uint8_t, 1)

PIXMAN_ARM_BIND_FAST_PATH_SRC_DST (armv6, add_8_8,
                                   uint8_t, 1, uint8_t, 1)
PIXMAN_ARM_BIND_FAST_PATH_SRC_DST (armv6, over_8888_8888,
                                   uint32_t, 1, uint32_t, 1)

PIXMAN_ARM_BIND_FAST_PATH_SRC_N_DST (SKIP_ZERO_MASK, armv6, over_8888_n_8888,
                                     uint32_t, 1, uint32_t, 1)

PIXMAN_ARM_BIND_FAST_PATH_N_MASK_DST (SKIP_ZERO_SRC, armv6, over_n_8_8888,
                                      uint8_t, 1, uint32_t, 1)

PIXMAN_ARM_BIND_SCALED_NEAREST_SRC_DST (armv6, 0565_0565, SRC,
                                        uint16_t, uint16_t)
PIXMAN_ARM_BIND_SCALED_NEAREST_SRC_DST (armv6, 8888_8888, SRC,
                                        uint32_t, uint32_t)

void
pixman_composite_src_n_8888_asm_armv6 (int32_t   w,
                                       int32_t   h,
                                       uint32_t *dst,
                                       int32_t   dst_stride,
                                       uint32_t  src);

void
pixman_composite_src_n_0565_asm_armv6 (int32_t   w,
                                       int32_t   h,
                                       uint16_t *dst,
                                       int32_t   dst_stride,
                                       uint16_t  src);

void
pixman_composite_src_n_8_asm_armv6 (int32_t   w,
                                    int32_t   h,
                                    uint8_t  *dst,
                                    int32_t   dst_stride,
                                    uint8_t  src);

static pixman_bool_t
arm_simd_fill (pixman_implementation_t *imp,
               uint32_t *               bits,
               int                      stride, /* in 32-bit words */
               int                      bpp,
               int                      x,
               int                      y,
               int                      width,
               int                      height,
               uint32_t                 _xor)
{
    /* stride is always multiple of 32bit units in pixman */
    uint32_t byte_stride = stride * sizeof(uint32_t);

    switch (bpp)
    {
    case 8:
	pixman_composite_src_n_8_asm_armv6 (
		width,
		height,
		(uint8_t *)(((char *) bits) + y * byte_stride + x),
		byte_stride,
		_xor & 0xff);
	return TRUE;
    case 16:
	pixman_composite_src_n_0565_asm_armv6 (
		width,
		height,
		(uint16_t *)(((char *) bits) + y * byte_stride + x * 2),
		byte_stride / 2,
		_xor & 0xffff);
	return TRUE;
    case 32:
	pixman_composite_src_n_8888_asm_armv6 (
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

static pixman_bool_t
arm_simd_blt (pixman_implementation_t *imp,
              uint32_t *               src_bits,
              uint32_t *               dst_bits,
              int                      src_stride, /* in 32-bit words */
              int                      dst_stride, /* in 32-bit words */
              int                      src_bpp,
              int                      dst_bpp,
              int                      src_x,
              int                      src_y,
              int                      dest_x,
              int                      dest_y,
              int                      width,
              int                      height)
{
    if (src_bpp != dst_bpp)
	return FALSE;

    switch (src_bpp)
    {
    case 8:
        pixman_composite_src_8_8_asm_armv6 (
                width, height,
                (uint8_t *)(((char *) dst_bits) +
                dest_y * dst_stride * 4 + dest_x * 1), dst_stride * 4,
                (uint8_t *)(((char *) src_bits) +
                src_y * src_stride * 4 + src_x * 1), src_stride * 4);
        return TRUE;
    case 16:
	pixman_composite_src_0565_0565_asm_armv6 (
		width, height,
		(uint16_t *)(((char *) dst_bits) +
		dest_y * dst_stride * 4 + dest_x * 2), dst_stride * 2,
		(uint16_t *)(((char *) src_bits) +
		src_y * src_stride * 4 + src_x * 2), src_stride * 2);
	return TRUE;
    case 32:
	pixman_composite_src_8888_8888_asm_armv6 (
		width, height,
		(uint32_t *)(((char *) dst_bits) +
		dest_y * dst_stride * 4 + dest_x * 4), dst_stride,
		(uint32_t *)(((char *) src_bits) +
		src_y * src_stride * 4 + src_x * 4), src_stride);
	return TRUE;
    default:
	return FALSE;
    }
}

static const pixman_fast_path_t arm_simd_fast_paths[] =
{
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, a8r8g8b8, armv6_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, a8b8g8r8, armv6_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, x8r8g8b8, armv6_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, x8b8g8r8, armv6_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8r8g8b8, null, x8r8g8b8, armv6_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8b8g8r8, null, x8b8g8r8, armv6_composite_src_8888_8888),

    PIXMAN_STD_FAST_PATH (SRC, r5g6b5, null, r5g6b5, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, b5g6r5, null, b5g6r5, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a1r5g5b5, null, a1r5g5b5, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a1b5g5r5, null, a1b5g5r5, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a1r5g5b5, null, x1r5g5b5, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a1b5g5r5, null, x1b5g5r5, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, x1r5g5b5, null, x1r5g5b5, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, x1b5g5r5, null, x1b5g5r5, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a4r4g4b4, null, a4r4g4b4, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a4b4g4r4, null, a4b4g4r4, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a4r4g4b4, null, x4r4g4b4, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, a4b4g4r4, null, x4b4g4r4, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, x4r4g4b4, null, x4r4g4b4, armv6_composite_src_0565_0565),
    PIXMAN_STD_FAST_PATH (SRC, x4b4g4r4, null, x4b4g4r4, armv6_composite_src_0565_0565),

    PIXMAN_STD_FAST_PATH (SRC, a8, null, a8, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, r3g3b2, null, r3g3b2, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, b2g3r3, null, b2g3r3, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, a2r2g2b2, null, a2r2g2b2, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, a2b2g2r2, null, a2b2g2r2, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, c8, null, c8, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, g8, null, g8, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, x4a4, null, x4a4, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, x4c4, null, x4c4, armv6_composite_src_8_8),
    PIXMAN_STD_FAST_PATH (SRC, x4g4, null, x4g4, armv6_composite_src_8_8),

    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, null, a8r8g8b8, armv6_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, null, x8r8g8b8, armv6_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, null, a8b8g8r8, armv6_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, null, x8b8g8r8, armv6_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, solid, a8r8g8b8, armv6_composite_over_8888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, solid, x8r8g8b8, armv6_composite_over_8888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, solid, a8b8g8r8, armv6_composite_over_8888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, solid, x8b8g8r8, armv6_composite_over_8888_n_8888),

    PIXMAN_STD_FAST_PATH (ADD, a8, null, a8, armv6_composite_add_8_8),

    PIXMAN_STD_FAST_PATH (OVER, solid, a8, a8r8g8b8, armv6_composite_over_n_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, x8r8g8b8, armv6_composite_over_n_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, a8b8g8r8, armv6_composite_over_n_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, x8b8g8r8, armv6_composite_over_n_8_8888),

    PIXMAN_ARM_SIMPLE_NEAREST_FAST_PATH (SRC, r5g6b5, r5g6b5, armv6_0565_0565),
    PIXMAN_ARM_SIMPLE_NEAREST_FAST_PATH (SRC, b5g6r5, b5g6r5, armv6_0565_0565),

    PIXMAN_ARM_SIMPLE_NEAREST_FAST_PATH (SRC, a8r8g8b8, a8r8g8b8, armv6_8888_8888),
    PIXMAN_ARM_SIMPLE_NEAREST_FAST_PATH (SRC, a8r8g8b8, x8r8g8b8, armv6_8888_8888),
    PIXMAN_ARM_SIMPLE_NEAREST_FAST_PATH (SRC, x8r8g8b8, x8r8g8b8, armv6_8888_8888),
    PIXMAN_ARM_SIMPLE_NEAREST_FAST_PATH (SRC, a8b8g8r8, a8b8g8r8, armv6_8888_8888),
    PIXMAN_ARM_SIMPLE_NEAREST_FAST_PATH (SRC, a8b8g8r8, x8b8g8r8, armv6_8888_8888),
    PIXMAN_ARM_SIMPLE_NEAREST_FAST_PATH (SRC, x8b8g8r8, x8b8g8r8, armv6_8888_8888),

    { PIXMAN_OP_NONE },
};

pixman_implementation_t *
_pixman_implementation_create_arm_simd (pixman_implementation_t *fallback)
{
    pixman_implementation_t *imp = _pixman_implementation_create (fallback, arm_simd_fast_paths);

    imp->blt = arm_simd_blt;
    imp->fill = arm_simd_fill;

    return imp;
}
