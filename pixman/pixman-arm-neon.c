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

#include "pixman-arm-neon.h"

#include <arm_neon.h>
#include <string.h>

// Deal with an intrinsic that is defined differently in GCC
#if !defined(__ARMCC_VERSION) && !defined(__pld)
#define __pld(_x) __builtin_prefetch(_x)
#endif

static force_inline uint8x8x4_t unpack0565(uint16x8_t rgb)
{
    uint16x8_t gb, b;
    uint8x8x4_t res;

    res.val[3] = vdup_n_u8(0);
    gb = vshrq_n_u16(rgb, 5);
    b = vshrq_n_u16(rgb, 5+6);
    res.val[0] = vmovn_u16(rgb);  // get low 5 bits
    res.val[1] = vmovn_u16(gb);   // get mid 6 bits
    res.val[2] = vmovn_u16(b);    // get top 5 bits

    res.val[0] = vshl_n_u8(res.val[0], 3); // shift to top
    res.val[1] = vshl_n_u8(res.val[1], 2); // shift to top
    res.val[2] = vshl_n_u8(res.val[2], 3); // shift to top

    res.val[0] = vsri_n_u8(res.val[0], res.val[0], 5); 
    res.val[1] = vsri_n_u8(res.val[1], res.val[1], 6);
    res.val[2] = vsri_n_u8(res.val[2], res.val[2], 5);

    return res;
}

static force_inline uint16x8_t pack0565(uint8x8x4_t s)
{
    uint16x8_t rgb, val_g, val_r;

    rgb = vshll_n_u8(s.val[2],8);
    val_g = vshll_n_u8(s.val[1],8);
    val_r = vshll_n_u8(s.val[0],8);
    rgb = vsriq_n_u16(rgb, val_g, 5);
    rgb = vsriq_n_u16(rgb, val_r, 5+6);

    return rgb;
}

static force_inline uint8x8_t neon2mul(uint8x8_t x, uint8x8_t alpha)
{
    uint16x8_t tmp,tmp2;
    uint8x8_t res;

    tmp = vmull_u8(x,alpha);
    tmp2 = vrshrq_n_u16(tmp,8);
    res = vraddhn_u16(tmp,tmp2);

    return res;
}

static force_inline uint8x8x4_t neon8mul(uint8x8x4_t x, uint8x8_t alpha)
{
    uint16x8x4_t tmp;
    uint8x8x4_t res;
    uint16x8_t qtmp1,qtmp2;

    tmp.val[0] = vmull_u8(x.val[0],alpha);
    tmp.val[1] = vmull_u8(x.val[1],alpha);
    tmp.val[2] = vmull_u8(x.val[2],alpha);
    tmp.val[3] = vmull_u8(x.val[3],alpha);

    qtmp1 = vrshrq_n_u16(tmp.val[0],8);
    qtmp2 = vrshrq_n_u16(tmp.val[1],8);
    res.val[0] = vraddhn_u16(tmp.val[0],qtmp1);
    qtmp1 = vrshrq_n_u16(tmp.val[2],8);
    res.val[1] = vraddhn_u16(tmp.val[1],qtmp2);
    qtmp2 = vrshrq_n_u16(tmp.val[3],8);
    res.val[2] = vraddhn_u16(tmp.val[2],qtmp1);
    res.val[3] = vraddhn_u16(tmp.val[3],qtmp2);

    return res;
}

static force_inline uint8x8x4_t neon8qadd(uint8x8x4_t x, uint8x8x4_t y)
{
    uint8x8x4_t res;

    res.val[0] = vqadd_u8(x.val[0],y.val[0]);
    res.val[1] = vqadd_u8(x.val[1],y.val[1]);
    res.val[2] = vqadd_u8(x.val[2],y.val[2]);
    res.val[3] = vqadd_u8(x.val[3],y.val[3]);

    return res;
}


void
fbCompositeSrcAdd_8000x8000neon (
                            pixman_implementation_t * impl,
                            pixman_op_t op,
                                pixman_image_t * pSrc,
                                pixman_image_t * pMask,
                                pixman_image_t * pDst,
                                int32_t      xSrc,
                                int32_t      ySrc,
                                int32_t      xMask,
                                int32_t      yMask,
                                int32_t      xDst,
                                int32_t      yDst,
                                int32_t      width,
                                int32_t      height)
{
    uint8_t     *dstLine, *dst;
    uint8_t     *srcLine, *src;
    int dstStride, srcStride;
    uint16_t    w;

    fbComposeGetStart (pSrc, xSrc, ySrc, uint8_t, srcStride, srcLine, 1);
    fbComposeGetStart (pDst, xDst, yDst, uint8_t, dstStride, dstLine, 1);

    if (width>=8)
    {
        // Use overlapping 8-pixel method
        while (height--)
        {
            dst = dstLine;
            dstLine += dstStride;
            src = srcLine;
            srcLine += srcStride;
            w = width;

            uint8_t *keep_dst=0;

#ifndef USE_GCC_INLINE_ASM
            uint8x8_t sval,dval,temp;

            sval = vld1_u8((void*)src);
            dval = vld1_u8((void*)dst);
            keep_dst = dst;

            temp = vqadd_u8(dval,sval);

            src += (w & 7);
            dst += (w & 7);
            w -= (w & 7);

            while (w)
            {
                sval = vld1_u8((void*)src);
                dval = vld1_u8((void*)dst);

                vst1_u8((void*)keep_dst,temp);
                keep_dst = dst;

                temp = vqadd_u8(dval,sval);

                src+=8;
                dst+=8;
                w-=8;
            }
            vst1_u8((void*)keep_dst,temp);
#else
            asm volatile (
// avoid using d8-d15 (q4-q7) aapcs callee-save registers
                        "vld1.8  {d0}, [%[src]]\n\t"
                        "vld1.8  {d4}, [%[dst]]\n\t"
                        "mov     %[keep_dst], %[dst]\n\t"

                        "and ip, %[w], #7\n\t"
                        "add %[src], %[src], ip\n\t"
                        "add %[dst], %[dst], ip\n\t"
                        "subs %[w], %[w], ip\n\t"
                        "b 9f\n\t"
// LOOP
                        "2:\n\t"
                        "vld1.8  {d0}, [%[src]]!\n\t"
                        "vld1.8  {d4}, [%[dst]]!\n\t"
                        "vst1.8  {d20}, [%[keep_dst]]\n\t"
                        "sub     %[keep_dst], %[dst], #8\n\t"
                        "subs %[w], %[w], #8\n\t"
                        "9:\n\t"
                        "vqadd.u8 d20, d0, d4\n\t"

                        "bne 2b\n\t"

                        "1:\n\t"
                        "vst1.8  {d20}, [%[keep_dst]]\n\t"

                        : [w] "+r" (w), [src] "+r" (src), [dst] "+r" (dst), [keep_dst] "=r" (keep_dst)
                        :
                        : "ip", "cc", "memory", "d0","d4",
                          "d20"
                        );
#endif
        }
    }
    else
    {
        const uint8_t nil = 0;
        const uint8x8_t vnil = vld1_dup_u8(&nil);

        while (height--)
        {
            dst = dstLine;
            dstLine += dstStride;
            src = srcLine;
            srcLine += srcStride;
            w = width;
            uint8x8_t sval=vnil, dval=vnil;
            uint8_t *dst4=0, *dst2=0;

            if (w&4)
            {
                sval = vreinterpret_u8_u32(vld1_lane_u32((void*)src,vreinterpret_u32_u8(sval),1));
                dval = vreinterpret_u8_u32(vld1_lane_u32((void*)dst,vreinterpret_u32_u8(dval),1));
                dst4=dst;
                src+=4;
                dst+=4;
            }
            if (w&2)
            {
                sval = vreinterpret_u8_u16(vld1_lane_u16((void*)src,vreinterpret_u16_u8(sval),1));
                dval = vreinterpret_u8_u16(vld1_lane_u16((void*)dst,vreinterpret_u16_u8(dval),1));
                dst2=dst;
                src+=2;
                dst+=2;
            }
            if (w&1)
            {
                sval = vld1_lane_u8(src,sval,1);
                dval = vld1_lane_u8(dst,dval,1);
            }

            dval = vqadd_u8(dval,sval);

            if (w&1)
                vst1_lane_u8(dst,dval,1);
            if (w&2)
                vst1_lane_u16((void*)dst2,vreinterpret_u16_u8(dval),1);
            if (w&4)
                vst1_lane_u32((void*)dst4,vreinterpret_u32_u8(dval),1);
        }
    }
}


void
fbCompositeSrc_8888x8888neon (
                            pixman_implementation_t * impl,
                            pixman_op_t op,
			 pixman_image_t * pSrc,
			 pixman_image_t * pMask,
			 pixman_image_t * pDst,
			 int32_t      xSrc,
			 int32_t      ySrc,
			 int32_t      xMask,
			 int32_t      yMask,
			 int32_t      xDst,
			 int32_t      yDst,
			 int32_t      width,
			 int32_t      height)
{
    uint32_t	*dstLine, *dst;
    uint32_t	*srcLine, *src;
    int	dstStride, srcStride;
    uint32_t	w;

    fbComposeGetStart (pDst, xDst, yDst, uint32_t, dstStride, dstLine, 1);
    fbComposeGetStart (pSrc, xSrc, ySrc, uint32_t, srcStride, srcLine, 1);

    if (width>=8)
    {
        // Use overlapping 8-pixel method  
        while (height--)
        {
	    dst = dstLine;
	    dstLine += dstStride;
	    src = srcLine;
	    srcLine += srcStride;
	    w = width;

            uint32_t *keep_dst=0;

#ifndef USE_GCC_INLINE_ASM
            uint8x8x4_t sval,dval,temp;

            sval = vld4_u8((void*)src);
            dval = vld4_u8((void*)dst);
            keep_dst = dst;

            temp = neon8mul(dval,vmvn_u8(sval.val[3]));
            temp = neon8qadd(sval,temp);

            src += (w & 7);
            dst += (w & 7);
            w -= (w & 7);

            while (w)
            {
                sval = vld4_u8((void*)src);
                dval = vld4_u8((void*)dst);

                vst4_u8((void*)keep_dst,temp);
                keep_dst = dst;

                temp = neon8mul(dval,vmvn_u8(sval.val[3]));
                temp = neon8qadd(sval,temp);

                src+=8;
                dst+=8;
                w-=8;
            }
            vst4_u8((void*)keep_dst,temp);
#else
            asm volatile (
// avoid using d8-d15 (q4-q7) aapcs callee-save registers
                        "vld4.8  {d0-d3}, [%[src]]\n\t"
                        "vld4.8  {d4-d7}, [%[dst]]\n\t"
                        "mov     %[keep_dst], %[dst]\n\t"

                        "and ip, %[w], #7\n\t"
                        "add %[src], %[src], ip, LSL#2\n\t"
                        "add %[dst], %[dst], ip, LSL#2\n\t"
                        "subs %[w], %[w], ip\n\t"
                        "b 9f\n\t"
// LOOP
                        "2:\n\t"
                        "vld4.8  {d0-d3}, [%[src]]!\n\t"
                        "vld4.8  {d4-d7}, [%[dst]]!\n\t"
                        "vst4.8  {d20-d23}, [%[keep_dst]]\n\t"
                        "sub     %[keep_dst], %[dst], #8*4\n\t"
                        "subs %[w], %[w], #8\n\t"
                        "9:\n\t"
                        "vmvn.8  d31, d3\n\t"
                        "vmull.u8 q10, d31, d4\n\t"
                        "vmull.u8 q11, d31, d5\n\t"
                        "vmull.u8 q12, d31, d6\n\t"
                        "vmull.u8 q13, d31, d7\n\t"
                        "vrshr.u16 q8, q10, #8\n\t"
                        "vrshr.u16 q9, q11, #8\n\t"
                        "vraddhn.u16 d20, q10, q8\n\t"
                        "vraddhn.u16 d21, q11, q9\n\t"
                        "vrshr.u16 q8, q12, #8\n\t"
                        "vrshr.u16 q9, q13, #8\n\t"
                        "vraddhn.u16 d22, q12, q8\n\t"
                        "vraddhn.u16 d23, q13, q9\n\t"
// result in d20-d23
                        "vqadd.u8 d20, d0, d20\n\t"
                        "vqadd.u8 d21, d1, d21\n\t"
                        "vqadd.u8 d22, d2, d22\n\t"
                        "vqadd.u8 d23, d3, d23\n\t"

                        "bne 2b\n\t"

                        "1:\n\t"
                        "vst4.8  {d20-d23}, [%[keep_dst]]\n\t"

                        : [w] "+r" (w), [src] "+r" (src), [dst] "+r" (dst), [keep_dst] "=r" (keep_dst)
                        : 
                        : "ip", "cc", "memory", "d0","d1","d2","d3","d4","d5","d6","d7",
                          "d16","d17","d18","d19","d20","d21","d22","d23"
                        );
#endif
        }
    }
    else
    {
        uint8x8_t    alpha_selector=vreinterpret_u8_u64(vcreate_u64(0x0707070703030303ULL));

        // Handle width<8
        while (height--)
        {
            dst = dstLine;
            dstLine += dstStride;
            src = srcLine;
            srcLine += srcStride;
            w = width;

            while (w>=2)
            {
                uint8x8_t sval,dval;

                /* two 32-bit pixels packed into D-reg; ad-hoc vectorization */
                sval = vreinterpret_u8_u32(vld1_u32((void*)src));
                dval = vreinterpret_u8_u32(vld1_u32((void*)dst));
                dval = neon2mul(dval,vtbl1_u8(vmvn_u8(sval),alpha_selector));
                vst1_u8((void*)dst,vqadd_u8(sval,dval));

                src+=2;
                dst+=2;
                w-=2;
            }

            if (w)
            {
                uint8x8_t sval,dval;

                /* single 32-bit pixel in lane 0 */
                sval = vreinterpret_u8_u32(vld1_dup_u32((void*)src));  // only interested in lane 0
                dval = vreinterpret_u8_u32(vld1_dup_u32((void*)dst));  // only interested in lane 0
                dval = neon2mul(dval,vtbl1_u8(vmvn_u8(sval),alpha_selector));
                vst1_lane_u32((void*)dst,vreinterpret_u32_u8(vqadd_u8(sval,dval)),0);
            }
        }
    }
}

void
fbCompositeSrc_8888x8x8888neon (
                               pixman_implementation_t * impl,
                               pixman_op_t op,
			       pixman_image_t * pSrc,
			       pixman_image_t * pMask,
			       pixman_image_t * pDst,
			       int32_t	xSrc,
			       int32_t	ySrc,
			       int32_t      xMask,
			       int32_t      yMask,
			       int32_t      xDst,
			       int32_t      yDst,
			       int32_t      width,
			       int32_t      height)
{
    uint32_t	*dstLine, *dst;
    uint32_t	*srcLine, *src;
    uint32_t	mask;
    int	dstStride, srcStride;
    uint32_t	w;
    uint8x8_t mask_alpha;

    fbComposeGetStart (pDst, xDst, yDst, uint32_t, dstStride, dstLine, 1);
    fbComposeGetStart (pSrc, xSrc, ySrc, uint32_t, srcStride, srcLine, 1);

    _pixman_image_get_solid (pMask, mask, pDst->bits.format);
    mask_alpha = vdup_n_u8((mask) >> 24);

    if (width>=8)
    {
        // Use overlapping 8-pixel method
        while (height--)
        {
            dst = dstLine;
            dstLine += dstStride;
            src = srcLine;
            srcLine += srcStride;
            w = width;

            uint32_t *keep_dst=0;

#ifndef USE_GCC_INLINE_ASM
            uint8x8x4_t sval,dval,temp;

            sval = vld4_u8((void*)src);
            dval = vld4_u8((void*)dst);
            keep_dst = dst;

            sval = neon8mul(sval,mask_alpha);
            temp = neon8mul(dval,vmvn_u8(sval.val[3]));
            temp = neon8qadd(sval,temp);

            src += (w & 7);
            dst += (w & 7);
            w -= (w & 7);

            while (w)
            {
                sval = vld4_u8((void*)src);
                dval = vld4_u8((void*)dst);

                vst4_u8((void*)keep_dst,temp);
                keep_dst = dst;

                sval = neon8mul(sval,mask_alpha);
                temp = neon8mul(dval,vmvn_u8(sval.val[3]));
                temp = neon8qadd(sval,temp);

                src+=8;
                dst+=8;
                w-=8;
            }
            vst4_u8((void*)keep_dst,temp);
#else
            asm volatile (
// avoid using d8-d15 (q4-q7) aapcs callee-save registers
                        "vdup.32      d30, %[mask]\n\t"
                        "vdup.8       d30, d30[3]\n\t"

                        "vld4.8       {d0-d3}, [%[src]]\n\t"
                        "vld4.8       {d4-d7}, [%[dst]]\n\t"
                        "mov  %[keep_dst], %[dst]\n\t"

                        "and  ip, %[w], #7\n\t"
                        "add  %[src], %[src], ip, LSL#2\n\t"
                        "add  %[dst], %[dst], ip, LSL#2\n\t"
                        "subs  %[w], %[w], ip\n\t"
                        "b 9f\n\t"
// LOOP
                        "2:\n\t"
                        "vld4.8       {d0-d3}, [%[src]]!\n\t"
                        "vld4.8       {d4-d7}, [%[dst]]!\n\t"
                        "vst4.8       {d20-d23}, [%[keep_dst]]\n\t"
                        "sub  %[keep_dst], %[dst], #8*4\n\t"
                        "subs  %[w], %[w], #8\n\t"

                        "9:\n\t"
                        "vmull.u8     q10, d30, d0\n\t"
                        "vmull.u8     q11, d30, d1\n\t"
                        "vmull.u8     q12, d30, d2\n\t"
                        "vmull.u8     q13, d30, d3\n\t"
                        "vrshr.u16    q8, q10, #8\n\t"
                        "vrshr.u16    q9, q11, #8\n\t"
                        "vraddhn.u16  d0, q10, q8\n\t"
                        "vraddhn.u16  d1, q11, q9\n\t"
                        "vrshr.u16    q9, q13, #8\n\t"
                        "vrshr.u16    q8, q12, #8\n\t"
                        "vraddhn.u16  d3, q13, q9\n\t"
                        "vraddhn.u16  d2, q12, q8\n\t"

                        "vmvn.8       d31, d3\n\t"
                        "vmull.u8     q10, d31, d4\n\t"
                        "vmull.u8     q11, d31, d5\n\t"
                        "vmull.u8     q12, d31, d6\n\t"
                        "vmull.u8     q13, d31, d7\n\t"
                        "vrshr.u16    q8, q10, #8\n\t"
                        "vrshr.u16    q9, q11, #8\n\t"
                        "vraddhn.u16  d20, q10, q8\n\t"
                        "vrshr.u16    q8, q12, #8\n\t"
                        "vraddhn.u16  d21, q11, q9\n\t"
                        "vrshr.u16    q9, q13, #8\n\t"
                        "vraddhn.u16  d22, q12, q8\n\t"
                        "vraddhn.u16  d23, q13, q9\n\t"
// result in d20-d23
                        "vqadd.u8     d20, d0, d20\n\t"
                        "vqadd.u8     d21, d1, d21\n\t"
                        "vqadd.u8     d22, d2, d22\n\t"
                        "vqadd.u8     d23, d3, d23\n\t"

                        "bne  2b\n\t"

                        "1:\n\t"
                        "vst4.8       {d20-d23}, [%[keep_dst]]\n\t"

                        : [w] "+r" (w), [src] "+r" (src), [dst] "+r" (dst), [keep_dst] "=r" (keep_dst)
                        : [mask] "r" (mask)
                        : "ip", "cc", "memory", "d0","d1","d2","d3","d4","d5","d6","d7",
                          "d16","d17","d18","d19","d20","d21","d22","d23","d24","d25","d26","d27",
                          "d30","d31"
                        );
#endif
        }
    }
    else
    {
        uint8x8_t    alpha_selector=vreinterpret_u8_u64(vcreate_u64(0x0707070703030303ULL));

        // Handle width<8
        while (height--)
        {
            dst = dstLine;
            dstLine += dstStride;
            src = srcLine;
            srcLine += srcStride;
            w = width;

            while (w>=2)
            {
                uint8x8_t sval,dval;

                sval = vreinterpret_u8_u32(vld1_u32((void*)src));
                dval = vreinterpret_u8_u32(vld1_u32((void*)dst));

                /* sval * const alpha_mul */
                sval = neon2mul(sval,mask_alpha);

                /* dval * 255-(src alpha) */
                dval = neon2mul(dval,vtbl1_u8(vmvn_u8(sval), alpha_selector));

                vst1_u8((void*)dst,vqadd_u8(sval,dval));

                src+=2;
                dst+=2;
                w-=2;
            }

            if (w)
            {
                uint8x8_t sval,dval;

                sval = vreinterpret_u8_u32(vld1_dup_u32((void*)src));
                dval = vreinterpret_u8_u32(vld1_dup_u32((void*)dst));

                /* sval * const alpha_mul */
                sval = neon2mul(sval,mask_alpha);

                /* dval * 255-(src alpha) */
                dval = neon2mul(dval,vtbl1_u8(vmvn_u8(sval), alpha_selector));

                vst1_lane_u32((void*)dst,vreinterpret_u32_u8(vqadd_u8(sval,dval)),0);
            }
        }
    }
}


void
fbCompositeSolidMask_nx8x8888neon (
                            pixman_implementation_t * impl,
                            pixman_op_t      op,
			       pixman_image_t * pSrc,
			       pixman_image_t * pMask,
			       pixman_image_t * pDst,
			       int32_t      xSrc,
			       int32_t      ySrc,
			       int32_t      xMask,
			       int32_t      yMask,
			       int32_t      xDst,
			       int32_t      yDst,
			       int32_t      width,
			       int32_t      height)
{
    uint32_t	 src, srca;
    uint32_t	*dstLine, *dst;
    uint8_t	*maskLine, *mask;
    int		 dstStride, maskStride;
    uint32_t	 w;
    uint8x8_t    sval2;
    uint8x8x4_t  sval8;
    uint8x8_t    mask_selector=vreinterpret_u8_u64(vcreate_u64(0x0101010100000000ULL));
    uint8x8_t    alpha_selector=vreinterpret_u8_u64(vcreate_u64(0x0707070703030303ULL));

    _pixman_image_get_solid(pSrc, src, pDst->bits.format);

    srca = src >> 24;
    if (src == 0)
	return;

    sval2=vreinterpret_u8_u32(vdup_n_u32(src));
    sval8.val[0]=vdup_lane_u8(sval2,0);
    sval8.val[1]=vdup_lane_u8(sval2,1);
    sval8.val[2]=vdup_lane_u8(sval2,2);
    sval8.val[3]=vdup_lane_u8(sval2,3);

    fbComposeGetStart (pDst, xDst, yDst, uint32_t, dstStride, dstLine, 1);
    fbComposeGetStart (pMask, xMask, yMask, uint8_t, maskStride, maskLine, 1);

    if (width>=8)
    {
        // Use overlapping 8-pixel method, modified to avoid rewritten dest being reused
        while (height--)
        {
            uint32_t *keep_dst=0;

            dst = dstLine;
            dstLine += dstStride;
            mask = maskLine;
            maskLine += maskStride;
            w = width;

#ifndef USE_GCC_INLINE_ASM
            uint8x8_t alpha;
            uint8x8x4_t dval, temp;

            alpha = vld1_u8((void*)mask);
            dval = vld4_u8((void*)dst);
            keep_dst = dst;

            temp = neon8mul(sval8,alpha);
            dval = neon8mul(dval,vmvn_u8(temp.val[3]));
            temp = neon8qadd(temp,dval);

            mask += (w & 7);
            dst += (w & 7);
            w -= (w & 7);

            while (w)
            {
                alpha = vld1_u8((void*)mask);
                dval = vld4_u8((void*)dst);

                vst4_u8((void*)keep_dst,temp);
                keep_dst = dst;

                temp = neon8mul(sval8,alpha);
                dval = neon8mul(dval,vmvn_u8(temp.val[3]));
                temp = neon8qadd(temp,dval);

                mask+=8;
                dst+=8;
                w-=8;
            }
            vst4_u8((void*)keep_dst,temp);
#else
        asm volatile (
                        "vdup.32      d0, %[src]\n\t"
                        "vdup.8       d1, d0[1]\n\t"
                        "vdup.8       d2, d0[2]\n\t"
                        "vdup.8       d3, d0[3]\n\t"
                        "vdup.8       d0, d0[0]\n\t"

                        "vld4.8       {d4-d7}, [%[dst]]\n\t"
                        "vld1.8       {d31}, [%[mask]]\n\t"
                        "mov  %[keep_dst], %[dst]\n\t"

                        "and  ip, %[w], #7\n\t"
                        "add  %[mask], %[mask], ip\n\t"
                        "add  %[dst], %[dst], ip, LSL#2\n\t"
                        "subs  %[w], %[w], ip\n\t"
                        "b 9f\n\t"
// LOOP
                        "2:\n\t" 
                        "vld4.8       {d4-d7}, [%[dst]]!\n\t"
                        "vld1.8       {d31}, [%[mask]]!\n\t"
                        "vst4.8       {d20-d23}, [%[keep_dst]]\n\t"
                        "sub  %[keep_dst], %[dst], #8*4\n\t"
                        "subs  %[w], %[w], #8\n\t"
                        "9:\n\t"

                        "vmull.u8     q10, d31, d0\n\t"
                        "vmull.u8     q11, d31, d1\n\t"
                        "vmull.u8     q12, d31, d2\n\t"
                        "vmull.u8     q13, d31, d3\n\t"
                        "vrshr.u16    q8, q10, #8\n\t"
                        "vrshr.u16    q9, q11, #8\n\t"
                        "vraddhn.u16  d20, q10, q8\n\t"
                        "vraddhn.u16  d21, q11, q9\n\t"
                        "vrshr.u16    q9, q13, #8\n\t"
                        "vrshr.u16    q8, q12, #8\n\t"
                        "vraddhn.u16  d23, q13, q9\n\t"
                        "vraddhn.u16  d22, q12, q8\n\t"

                        "vmvn.8       d30, d23\n\t"
                        "vmull.u8     q12, d30, d4\n\t"
                        "vmull.u8     q13, d30, d5\n\t"
                        "vmull.u8     q14, d30, d6\n\t"
                        "vmull.u8     q15, d30, d7\n\t"

                        "vrshr.u16    q8, q12, #8\n\t"
                        "vrshr.u16    q9, q13, #8\n\t"
                        "vraddhn.u16  d4, q12, q8\n\t"
                        "vrshr.u16    q8, q14, #8\n\t"
                        "vraddhn.u16  d5, q13, q9\n\t"
                        "vrshr.u16    q9, q15, #8\n\t"
                        "vraddhn.u16  d6, q14, q8\n\t"
                        "vraddhn.u16  d7, q15, q9\n\t"
// result in d4-d7

                        "vqadd.u8     d20, d4, d20\n\t"
                        "vqadd.u8     d21, d5, d21\n\t"
                        "vqadd.u8     d22, d6, d22\n\t"
                        "vqadd.u8     d23, d7, d23\n\t"

                        "bne 2b\n\t"

                        "1:\n\t"
                        "vst4.8       {d20-d23}, [%[keep_dst]]\n\t"

                        : [w] "+r" (w), [dst] "+r" (dst), [mask] "+r" (mask), [keep_dst] "=r" (keep_dst)
                        : [src] "r" (src) 
                        : "ip", "cc", "memory", "d0","d1","d2","d3","d4","d5","d6","d7",
                          "d16","d17","d18","d19","d20","d21","d22","d23","d24","d25","d26","d27","d28","d29",
                          "d30","d31"
                        );
#endif
        }
    }
    else
    {
        while (height--)
        {
            uint8x8_t alpha;

            dst = dstLine;
            dstLine += dstStride;
            mask = maskLine;
            maskLine += maskStride;
            w = width;

            while (w>=2)
            {
                uint8x8_t dval, temp, res;

                alpha = vtbl1_u8(vreinterpret_u8_u16(vld1_dup_u16((void*)mask)), mask_selector);
                dval = vld1_u8((void*)dst);

                temp = neon2mul(sval2,alpha);
                res = vqadd_u8(temp,neon2mul(dval,vtbl1_u8(vmvn_u8(temp), alpha_selector)));

                vst1_u8((void*)dst,res);

                mask+=2;
                dst+=2;
                w-=2;
            }
            if (w)
            {
                uint8x8_t dval, temp, res;

                alpha = vtbl1_u8(vld1_dup_u8((void*)mask), mask_selector);
                dval = vreinterpret_u8_u32(vld1_dup_u32((void*)dst));

                temp = neon2mul(sval2,alpha);
                res = vqadd_u8(temp,neon2mul(dval,vtbl1_u8(vmvn_u8(temp), alpha_selector)));

                vst1_lane_u32((void*)dst,vreinterpret_u32_u8(res),0);
            }
        }
    }
}


void
fbCompositeSrcAdd_8888x8x8neon (
                            pixman_implementation_t * impl,
                            pixman_op_t op,
                            pixman_image_t * pSrc,
                            pixman_image_t * pMask,
                            pixman_image_t * pDst,
                            int32_t      xSrc,
                            int32_t      ySrc,
                            int32_t      xMask,
                            int32_t      yMask,
                            int32_t      xDst,
                            int32_t      yDst,
                            int32_t      width,
                            int32_t      height)
{
    uint8_t     *dstLine, *dst;
    uint8_t     *maskLine, *mask;
    int dstStride, maskStride;
    uint32_t    w;
    uint32_t    src;
    uint8x8_t   sa;

    fbComposeGetStart (pDst, xDst, yDst, uint8_t, dstStride, dstLine, 1);
    fbComposeGetStart (pMask, xMask, yMask, uint8_t, maskStride, maskLine, 1);
    _pixman_image_get_solid (pSrc, src, pDst->bits.format);
    sa = vdup_n_u8((src) >> 24);

    if (width>=8)
    {
        // Use overlapping 8-pixel method, modified to avoid rewritten dest being reused
        while (height--)
        {
            dst = dstLine;
            dstLine += dstStride;
            mask = maskLine;
            maskLine += maskStride;
            w = width;

            uint8x8_t mval, dval, res;
            uint8_t     *keep_dst;

            mval = vld1_u8((void *)mask);
            dval = vld1_u8((void *)dst);
            keep_dst = dst;

            res = vqadd_u8(neon2mul(mval,sa),dval);

            mask += (w & 7);
            dst += (w & 7);
            w -= w & 7;

            while (w)
            {
                mval = vld1_u8((void *)mask);
                dval = vld1_u8((void *)dst);
                vst1_u8((void *)keep_dst, res);
                keep_dst = dst;

                res = vqadd_u8(neon2mul(mval,sa),dval);

                mask += 8;
                dst += 8;
                w -= 8;
            }
            vst1_u8((void *)keep_dst, res);
        }
    }
    else
    {
        // Use 4/2/1 load/store method to handle 1-7 pixels
        while (height--)
        {
            dst = dstLine;
            dstLine += dstStride;
            mask = maskLine;
            maskLine += maskStride;
            w = width;

            uint8x8_t mval=sa, dval=sa, res;
            uint8_t *dst4=0, *dst2=0;

            if (w&4)
            {
                mval = vreinterpret_u8_u32(vld1_lane_u32((void *)mask, vreinterpret_u32_u8(mval), 1));
                dval = vreinterpret_u8_u32(vld1_lane_u32((void *)dst, vreinterpret_u32_u8(dval), 1));

                dst4 = dst;
                mask += 4;
                dst += 4;
            }
            if (w&2)
            {
                mval = vreinterpret_u8_u16(vld1_lane_u16((void *)mask, vreinterpret_u16_u8(mval), 1));
                dval = vreinterpret_u8_u16(vld1_lane_u16((void *)dst, vreinterpret_u16_u8(dval), 1));
                dst2 = dst;
                mask += 2;
                dst += 2;
            }
            if (w&1)
            {
                mval = vld1_lane_u8(mask, mval, 1);
                dval = vld1_lane_u8(dst, dval, 1);
            }

            res = vqadd_u8(neon2mul(mval,sa),dval);

            if (w&1)
                vst1_lane_u8(dst, res, 1);
            if (w&2)
                vst1_lane_u16((void *)dst2, vreinterpret_u16_u8(res), 1);
            if (w&4)
                vst1_lane_u32((void *)dst4, vreinterpret_u32_u8(res), 1);
        }
    }
}

#ifdef USE_GCC_INLINE_ASM

void
fbCompositeSrc_16x16neon (
	pixman_implementation_t * impl,
	pixman_op_t op,
	pixman_image_t * pSrc,
	pixman_image_t * pMask,
	pixman_image_t * pDst,
	int32_t      xSrc,
	int32_t      ySrc,
	int32_t      xMask,
	int32_t      yMask,
	int32_t      xDst,
	int32_t      yDst,
	int32_t      width,
	int32_t      height)
{
	uint16_t    *dstLine, *srcLine;
	uint32_t     dstStride, srcStride;

	if(!height || !width)
		return;

	/* We simply copy 16-bit-aligned pixels from one place to another. */
	fbComposeGetStart (pSrc, xSrc, ySrc, uint16_t, srcStride, srcLine, 1);
	fbComposeGetStart (pDst, xDst, yDst, uint16_t, dstStride, dstLine, 1);

	/* Preload the first input scanline */
	{
		uint16_t *srcPtr = srcLine;
		uint32_t count = width;

		asm volatile (
		"0: @ loop							\n"
		"	subs    %[count], %[count], #32				\n"
		"	pld     [%[src]]					\n"
		"	add     %[src], %[src], #64				\n"
		"	bgt 0b							\n"

		// Clobbered input registers marked as input/outputs
		: [src] "+r" (srcPtr), [count] "+r" (count)
		: // no unclobbered inputs
		: "cc"
		);
	}

	while(height--) {
		uint16_t *dstPtr = dstLine;
		uint16_t *srcPtr = srcLine;
		uint32_t count = width;
		uint32_t tmp = 0;

		// Uses multi-register access and preloading to maximise bandwidth.
		// Each pixel is one halfword, so a quadword contains 8px.
		// Preload frequency assumed a 64-byte cacheline.
		asm volatile (
		"	cmp       %[count], #64				\n"
		"	blt 1f    @ skip oversized fragments		\n"
		"0: @ start with eight quadwords at a time		\n"
		"	pld       [%[src], %[srcStride], LSL #1]	\n" // preload from next scanline
		"	sub       %[count], %[count], #64		\n"
		"	vld1.16   {d16,d17,d18,d19}, [%[src]]!		\n"
		"	vld1.16   {d20,d21,d22,d23}, [%[src]]!		\n"
		"	pld       [%[src], %[srcStride], LSL #1]	\n" // preload from next scanline
		"	vld1.16   {d24,d25,d26,d27}, [%[src]]!		\n"
		"	vld1.16   {d28,d29,d30,d31}, [%[src]]!		\n"
		"	cmp       %[count], #64				\n"
		"	vst1.16   {d16,d17,d18,d19}, [%[dst]]!		\n"
		"	vst1.16   {d20,d21,d22,d23}, [%[dst]]!		\n"
		"	vst1.16   {d24,d25,d26,d27}, [%[dst]]!		\n"
		"	vst1.16   {d28,d29,d30,d31}, [%[dst]]!		\n"
		"	bge 0b						\n"
		"	cmp       %[count], #0				\n"
		"	beq 7f    @ aligned fastpath			\n"
		"1: @ four quadwords					\n"
		"	tst       %[count], #32				\n"
		"	beq 2f    @ skip oversized fragment		\n"
		"	pld       [%[src], %[srcStride], LSL #1]	\n" // preload from next scanline
		"	vld1.16   {d16,d17,d18,d19}, [%[src]]!		\n"
		"	vld1.16   {d20,d21,d22,d23}, [%[src]]!		\n"
		"	vst1.16   {d16,d17,d18,d19}, [%[dst]]!		\n"
		"	vst1.16   {d20,d21,d22,d23}, [%[dst]]!		\n"
		"2: @ two quadwords					\n"
		"	tst       %[count], #16				\n"
		"	beq 3f    @ skip oversized fragment		\n"
		"	pld       [%[src], %[srcStride], LSL #1]	\n" // preload from next scanline
		"	vld1.16   {d16,d17,d18,d19}, [%[src]]!		\n"
		"	vst1.16   {d16,d17,d18,d19}, [%[dst]]!		\n"
		"3: @ one quadword					\n"
		"	tst       %[count], #8				\n"
		"	beq 4f    @ skip oversized fragment		\n"
		"	vld1.16   {d16,d17}, [%[src]]!			\n"
		"	vst1.16   {d16,d17}, [%[dst]]!			\n"
		"4: @ one doubleword					\n"
		"	tst       %[count], #4				\n"
		"	beq 5f    @ skip oversized fragment		\n"
		"	vld1.16   {d16}, [%[src]]!			\n"
		"	vst1.16   {d16}, [%[dst]]!			\n"
		"5: @ one word						\n"
		"	tst       %[count], #2				\n"
		"	beq 6f    @ skip oversized fragment		\n"
		"	ldr       %[tmp], [%[src]], #4			\n"
		"	str       %[tmp], [%[dst]], #4			\n"
		"6: @ one halfword					\n"
		"	tst       %[count], #1				\n"
		"	beq 7f    @ skip oversized fragment		\n"
		"	ldrh      %[tmp], [%[src]]			\n"
		"	strh      %[tmp], [%[dst]]			\n"
		"7: @ end						\n"

		// Clobbered input registers marked as input/outputs
		: [dst] "+r" (dstPtr), [src] "+r" (srcPtr), [count] "+r" (count), [tmp] "+r" (tmp)

		// Unclobbered input
		: [srcStride] "r" (srcStride)

		// Clobbered vector registers
		// NB: these are the quad aliases of the double registers used in the asm
		: "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "cc", "memory"
		);

		srcLine += srcStride;
		dstLine += dstStride;
	}
}

#endif /* USE_GCC_INLINE_ASM */

void
fbCompositeSrc_24x16neon (
	pixman_implementation_t * impl,
	pixman_op_t op,
	pixman_image_t * pSrc,
	pixman_image_t * pMask,
	pixman_image_t * pDst,
	int32_t      xSrc,
	int32_t      ySrc,
	int32_t      xMask,
	int32_t      yMask,
	int32_t      xDst,
	int32_t      yDst,
	int32_t      width,
	int32_t      height)
{
	uint16_t    *dstLine;
	uint32_t    *srcLine;
	uint32_t     dstStride, srcStride;

	if(!width || !height)
		return;

	/* We simply copy pixels from one place to another, assuming that the source's alpha is opaque. */
	fbComposeGetStart (pSrc, xSrc, ySrc, uint32_t, srcStride, srcLine, 1);
	fbComposeGetStart (pDst, xDst, yDst, uint16_t, dstStride, dstLine, 1);

	/* Preload the first input scanline */
	{
		uint8_t *srcPtr = (uint8_t*) srcLine;
		uint32_t count = (width + 15) / 16;

#ifdef USE_GCC_INLINE_ASM
		asm volatile (
		"0: @ loop						\n"
		"	subs    %[count], %[count], #1			\n"
		"	pld     [%[src]]				\n"
		"	add     %[src], %[src], #64			\n"
		"	bgt 0b						\n"

		// Clobbered input registers marked as input/outputs
		: [src] "+r" (srcPtr), [count] "+r" (count)
		: // no unclobbered inputs
		: "cc"
		);
#else
		do {
			__pld(srcPtr);
			srcPtr += 64;
		} while(--count);
#endif
	}

	while(height--) {
		uint16_t *dstPtr = dstLine;
		uint32_t *srcPtr = srcLine;
		uint32_t count = width;
		const uint32_t RBmask = 0x1F;
		const uint32_t Gmask = 0x3F;

		// If you're going to complain about a goto, take a long hard look
		// at the massive blocks of assembler this skips over.  ;-)
		if(count < 8)
			goto smallStuff;

#ifdef USE_GCC_INLINE_ASM

		// This is not as aggressive as the RGB565-source case.
		// Generally the source is in cached RAM when the formats are different, so we use preload.
		// We don't need to blend, so we are not reading from the uncached framebuffer.
		asm volatile (
		"	cmp       %[count], #16										\n"
		"	blt 1f    @ skip oversized fragments								\n"
		"0: @ start with sixteen pixels at a time								\n"
		"	sub       %[count], %[count], #16								\n"
		"	pld      [%[src], %[srcStride], lsl #2]         @ preload from next scanline			\n"
		"	vld4.8    {d0,d1,d2,d3}, [%[src]]!		@ d3 is alpha and ignored, d2-0 are rgb.	\n"
		"	vld4.8    {d4,d5,d6,d7}, [%[src]]!		@ d7 is alpha and ignored, d6-4 are rgb.	\n"
		"	vshll.u8  q8, d2, #8				@ expand first red for repacking		\n"
		"	vshll.u8  q10, d1, #8				@ expand first green for repacking		\n"
		"	vshll.u8  q11, d0, #8				@ expand first blue for repacking		\n"
		"	vshll.u8  q9, d6, #8				@ expand second red for repacking		\n"
		"	vsri.u16  q8, q10, #5				@ insert first green after red			\n"
		"	vshll.u8  q10, d5, #8				@ expand second green for repacking		\n"
		"	vsri.u16  q8, q11, #11				@ insert first blue after green			\n"
		"	vshll.u8  q11, d4, #8				@ expand second blue for repacking		\n"
		"	vsri.u16  q9, q10, #5				@ insert second green after red			\n"
		"	vsri.u16  q9, q11, #11				@ insert second blue after green		\n"
		"	cmp       %[count], #16										\n"
		"	vst1.16   {d16,d17,d18,d19}, [%[dst]]!          @ store 16 pixels				\n"
		"	bge 0b												\n"
		"1: @ end of main loop	\n"
		"	cmp       %[count], #8				@ can we still do an 8-pixel block?		\n"
		"	blt 2f												\n"
		"	sub       %[count], %[count], #8	\n"
		"	pld      [%[src], %[srcStride], lsl #2]         @ preload from next scanline			\n"
		"	vld4.8    {d0,d1,d2,d3}, [%[src]]!		@ d3 is alpha and ignored, d2-0 are rgb.	\n"
		"	vshll.u8  q8, d2, #8				@ expand first red for repacking		\n"
		"	vshll.u8  q10, d1, #8				@ expand first green for repacking		\n"
		"	vshll.u8  q11, d0, #8				@ expand first blue for repacking		\n"
		"	vsri.u16  q8, q10, #5				@ insert first green after red			\n"
		"	vsri.u16  q8, q11, #11				@ insert first blue after green			\n"
		"	vst1.16   {d16,d17}, [%[dst]]!          @ store 8 pixels				\n"
		"2: @ end												\n"

		// Clobbered input and working registers marked as input/outputs
		: [dst] "+r" (dstPtr), [src] "+r" (srcPtr), [count] "+r" (count)

		// Unclobbered input
		: [srcStride] "r" (srcStride)

		// Clobbered vector registers
		// NB: these are the quad aliases of the double registers used in the asm
		: "q0", "q1", "q2", "q3", "q8", "q9", "q10", "q11", "cc", "memory"
		);
#else
		// A copy of the above code, in intrinsics-form.
		// This should be pretty self-documenting...
		while(count >= 16) {
			uint8x8x4_t pixelSetA, pixelSetB;
			uint16x8_t redA, greenA, blueA;
			uint16x8_t redB, greenB, blueB;
			uint16x8_t destPixelsA, destPixelsB;

			count -= 16;
			__pld(srcPtr + srcStride);
			pixelSetA = vld4_u8((uint8_t*)(srcPtr));
			pixelSetB = vld4_u8((uint8_t*)(srcPtr+8));
			srcPtr += 16;

			redA   = vshll_n_u8(pixelSetA.val[2], 8);
			greenA = vshll_n_u8(pixelSetA.val[1], 8);
			blueA  = vshll_n_u8(pixelSetA.val[0], 8);
			redB   = vshll_n_u8(pixelSetB.val[2], 8);
			greenB = vshll_n_u8(pixelSetB.val[1], 8);
			blueB  = vshll_n_u8(pixelSetB.val[0], 8);
			destPixelsA = vsriq_n_u16(redA, greenA, 5);
			destPixelsB = vsriq_n_u16(redB, greenB, 5);
			destPixelsA = vsriq_n_u16(destPixelsA, blueA, 11);
			destPixelsB = vsriq_n_u16(destPixelsB, blueB, 11);

			// There doesn't seem to be an intrinsic for the double-quadword variant
			vst1q_u16(dstPtr  , destPixelsA);
			vst1q_u16(dstPtr+8, destPixelsB);
			dstPtr += 16;
		}

		// 8-pixel loop
		if(count >= 8) {
			uint8x8x4_t pixelSetA;
			uint16x8_t redA, greenA, blueA;
			uint16x8_t destPixelsA;

			__pld(srcPtr + srcStride);
			count -= 8;
			pixelSetA = vld4_u8((uint8_t*)(srcPtr));
			srcPtr += 8;

			redA   = vshll_n_u8(pixelSetA.val[2], 8);
			greenA = vshll_n_u8(pixelSetA.val[1], 8);
			blueA  = vshll_n_u8(pixelSetA.val[0], 8);
			destPixelsA = vsriq_n_u16(redA, greenA, 5);
			destPixelsA = vsriq_n_u16(destPixelsA, blueA, 11);

			vst1q_u16(dstPtr  , destPixelsA);
			dstPtr += 8;
		}

#endif	// USE_GCC_INLINE_ASM

	smallStuff:

		if(count)
			__pld(srcPtr + srcStride);

		while(count >= 2) {
			uint32_t srcPixelA = *srcPtr++;
			uint32_t srcPixelB = *srcPtr++;

			// ARM is really good at shift-then-ALU ops.
			// This should be a total of six shift-ANDs and five shift-ORs.
			uint32_t dstPixelsA;
			uint32_t dstPixelsB;

			dstPixelsA  = ((srcPixelA >>  3) & RBmask);
			dstPixelsA |= ((srcPixelA >> 10) &  Gmask) << 5;
			dstPixelsA |= ((srcPixelA >> 19) & RBmask) << 11;

			dstPixelsB  = ((srcPixelB >>  3) & RBmask);
			dstPixelsB |= ((srcPixelB >> 10) &  Gmask) << 5;
			dstPixelsB |= ((srcPixelB >> 19) & RBmask) << 11;

			// little-endian mode only
			*((uint32_t*) dstPtr) = dstPixelsA | (dstPixelsB << 16);
			dstPtr += 2;
			count -= 2;
		}

		if(count) {
			uint32_t srcPixel = *srcPtr++;

			// ARM is really good at shift-then-ALU ops.
			// This block should end up as three shift-ANDs and two shift-ORs.
			uint32_t tmpBlue  = (srcPixel >>  3) & RBmask;
			uint32_t tmpGreen = (srcPixel >> 10) & Gmask;
			uint32_t tmpRed   = (srcPixel >> 19) & RBmask;
			uint16_t dstPixel = (tmpRed << 11) | (tmpGreen << 5) | tmpBlue;

			*dstPtr++ = dstPixel;
			count--;
		}

		srcLine += srcStride;
		dstLine += dstStride;
	}
}


pixman_bool_t
pixman_fill_neon (uint32_t *bits,
		  int stride,
		  int bpp,
		  int x,
		  int y,
		  int width,
		  int height,
		  uint32_t _xor)
{
    uint32_t byte_stride, color;
    char *dst;

    /* stride is always multiple of 32bit units in pixman */
    byte_stride = stride * sizeof(uint32_t);

    switch (bpp)
    {
	case 8:
	    dst = ((char *) bits) + y * byte_stride + x;
	    _xor &= 0xff;
	    color = _xor << 24 | _xor << 16 | _xor << 8 | _xor;
	    break;
	case 16:
	    dst = ((char *) bits) + y * byte_stride + x * 2;
	    _xor &= 0xffff;
	    color = _xor << 16 | _xor;
	    width *= 2;     /* width to bytes */
	    break;
	case 32:
	    dst = ((char *) bits) + y * byte_stride + x * 4;
	    color = _xor;
	    width *= 4;     /* width to bytes */
	    break;
	default:
	    return FALSE;
    }

#ifdef USE_GCC_INLINE_ASM
    if (width < 16)
	/* We have a special case for such small widths that don't allow
	   us to use wide 128-bit stores anyway. We don't waste time
	   trying to align writes, since there are only very few of them anyway */
	asm volatile (
	"cmp		%[height], #0\n" /* Check if empty fill */
	"beq		3f\n"
	"vdup.32	d0, %[color]\n"  /* Fill the color to neon req */

	/* Check if we have a such width that can easily be handled by single
	   operation for each scanline. This significantly reduces the number
	   of test/branch instructions for each scanline */
	"cmp		%[width], #8\n"
	"beq		4f\n"
	"cmp		%[width], #4\n"
	"beq		5f\n"
	"cmp		%[width], #2\n"
	"beq		6f\n"

	/* Loop starts here for each scanline */
	"1:\n"
	"mov		r4, %[dst]\n"    /* Starting address of the current line */
	"tst		%[width], #8\n"
	"beq		2f\n"
	"vst1.8		{d0}, [r4]!\n"
	"2:\n"
	"tst		%[width], #4\n"
	"beq		2f\n"
	"str		%[color], [r4], #4\n"
	"2:\n"
	"tst		%[width], #2\n"
	"beq		2f\n"
	"strh		%[color], [r4], #2\n"
	"2:\n"
	"tst		%[width], #1\n"
	"beq		2f\n"
	"strb		%[color], [r4], #1\n"
	"2:\n"

	"subs		%[height], %[height], #1\n"
	"add		%[dst], %[dst], %[byte_stride]\n"
	"bne		1b\n"
	"b		3f\n"

	/* Special fillers for those widths that we can do with single operation */
	"4:\n"
	"subs		%[height], %[height], #1\n"
	"vst1.8		{d0}, [%[dst]]\n"
	"add		%[dst], %[dst], %[byte_stride]\n"
	"bne		4b\n"
	"b		3f\n"

	"5:\n"
	"subs		%[height], %[height], #1\n"
	"str		%[color], [%[dst]]\n"
	"add		%[dst], %[dst], %[byte_stride]\n"
	"bne		5b\n"
	"b		3f\n"

	"6:\n"
	"subs		%[height], %[height], #1\n"
	"strh		%[color], [%[dst]]\n"
	"add		%[dst], %[dst], %[byte_stride]\n"
	"bne		6b\n"

	"3:\n"
	: /* No output members */
	: [color] "r" (color), [height] "r" (height), [width] "r" (width),
	  [dst] "r" (dst) , [byte_stride] "r" (byte_stride)
	: "memory", "cc", "d0", "r4", "r5");
    else
	asm volatile (
	"cmp		%[height], #0\n" /* Check if empty fill */
	"beq		5f\n"
	"vdup.32	q0, %[color]\n"  /* Fill the color to neon req */

	/* Loop starts here for each scanline */
	"1:\n"
	"mov		r4, %[dst]\n"    /* Starting address of the current line */
	"mov		r5, %[width]\n"  /* We're going to write this many bytes */
	"ands		r6, r4, #15\n"   /* Are we at the 128-bit aligned address? */
	"beq		2f\n"            /* Jump to the best case */

	/* We're not 128-bit aligned: However, we know that we can get to the
	   next aligned location, since the fill is at least 16 bytes wide */
	"rsb 		r6, r6, #16\n"   /* We would need to go forward this much */
	"sub		r5, r5, r6\n"    /* Update bytes left */
	"tst		r6, #1\n"
	"beq		6f\n"
	"vst1.8		{d0[0]}, [r4]!\n"/* Store byte, now we are word aligned */
	"6:\n"
	"tst		r6, #2\n"
	"beq		6f\n"
	"vst1.16	{d0[0]}, [r4, :16]!\n"/* Store half word, now we are 16-bit aligned */
	"6:\n"
	"tst		r6, #4\n"
	"beq		6f\n"
	"vst1.32	{d0[0]}, [r4, :32]!\n"/* Store word, now we're 32-bit aligned */
	"6:\n"
	"tst		r6, #8\n"
	"beq		2f\n"
	"vst1.64	{d0}, [r4, :64]!\n"    /* Store qword now we're 64-bit aligned */

	/* The good case: We're 128-bit aligned for this scanline */
	"2:\n"
	"and		r6, r5, #15\n"        /* Number of tailing bytes */
	"cmp		r5, r6\n"             /* Do we have at least one qword to write? */
	"beq		6f\n"                 /* No, we just write the tail */
	"lsr		r5, r5, #4\n"         /* This many full qwords to write */

	/* The main block: Do 128-bit aligned writes */
	"3:\n"
	"subs		r5, r5, #1\n"
	"vst1.64	{d0,d1}, [r4, :128]!\n"
	"bne		3b\n"

	/* Handle the tailing bytes: Do 64, 32, 16 and 8-bit aligned writes as needed.
	    We know that we're currently at 128-bit aligned address, so we can just
	    pick the biggest operations that the remaining write width allows */
	"6:\n"
	"cmp		r6, #0\n"
	"beq		4f\n"
	"tst		r6, #8\n"
	"beq		6f\n"
	"vst1.64	{d0}, [r4, :64]!\n"
	"6:\n"
	"tst		r6, #4\n"
	"beq		6f\n"
	"vst1.32	{d0[0]}, [r4, :32]!\n"
	"6:\n"
	"tst		r6, #2\n"
	"beq		6f\n"
	"vst1.16	{d0[0]}, [r4, :16]!\n"
	"6:\n"
	"tst		r6, #1\n"
	"beq		4f\n"
	"vst1.8		{d0[0]}, [r4]!\n"
	"4:\n"

	/* Handle the next scanline */
	"subs		%[height], %[height], #1\n"
	"add		%[dst], %[dst], %[byte_stride]\n"
	"bne		1b\n"
	"5:\n"
	: /* No output members */
	: [color] "r" (color), [height] "r" (height), [width] "r" (width),
	  [dst] "r" (dst) , [byte_stride] "r" (byte_stride)
	: "memory", "cc", "q0", "d0", "d1", "r4", "r5", "r6");

    return TRUE;

#else

    // TODO: intrinsic version for armcc
    return FALSE;

#endif
}


// TODO: is there a more generic way of doing this being introduced?
#define NEON_SCANLINE_BUFFER_PIXELS (1024)

static inline void QuadwordCopy_neon(
	void* dst,
	void* src,
	uint32_t count,       // of quadwords
	uint32_t trailerCount // of bytes
)
{
	uint8_t *tDst = dst, *tSrc = src;

	// Uses aligned multi-register loads to maximise read bandwidth
	// on uncached memory such as framebuffers
	// The accesses do not have the aligned qualifiers, so that the copy
	// may convert between aligned-uncached and unaligned-cached memory.
	// It is assumed that the CPU can infer alignedness from the address.

#ifdef USE_GCC_INLINE_ASM

	asm volatile (
	"	cmp       %[count], #8						\n"
	"	blt 1f    @ skip oversized fragments		\n"
	"0: @ start with eight quadwords at a time		\n"
	"	sub       %[count], %[count], #8			\n"
	"	vld1.8    {d16,d17,d18,d19}, [%[src]]!		\n"
	"	vld1.8    {d20,d21,d22,d23}, [%[src]]!		\n"
	"	vld1.8    {d24,d25,d26,d27}, [%[src]]!		\n"
	"	vld1.8    {d28,d29,d30,d31}, [%[src]]!		\n"
	"	cmp       %[count], #8						\n"
	"	vst1.8    {d16,d17,d18,d19}, [%[dst]]!		\n"
	"	vst1.8    {d20,d21,d22,d23}, [%[dst]]!		\n"
	"	vst1.8    {d24,d25,d26,d27}, [%[dst]]!		\n"
	"	vst1.8    {d28,d29,d30,d31}, [%[dst]]!		\n"
	"	bge 0b										\n"
	"1: @ four quadwords							\n"
	"	tst       %[count], #4						\n"
	"	beq 2f    @ skip oversized fragment			\n"
	"	vld1.8    {d16,d17,d18,d19}, [%[src]]!		\n"
	"	vld1.8    {d20,d21,d22,d23}, [%[src]]!		\n"
	"	vst1.8    {d16,d17,d18,d19}, [%[dst]]!		\n"
	"	vst1.8    {d20,d21,d22,d23}, [%[dst]]!		\n"
	"2: @ two quadwords								\n"
	"	tst       %[count], #2						\n"
	"	beq 3f    @ skip oversized fragment			\n"
	"	vld1.8    {d16,d17,d18,d19}, [%[src]]!		\n"
	"	vst1.8    {d16,d17,d18,d19}, [%[dst]]!		\n"
	"3: @ one quadword								\n"
	"	tst       %[count], #1						\n"
	"	beq 4f    @ skip oversized fragment			\n"
	"	vld1.8    {d16,d17}, [%[src]]!				\n"
	"	vst1.8    {d16,d17}, [%[dst]]!				\n"
	"4: @ end										\n"

	// Clobbered input registers marked as input/outputs
	: [dst] "+r" (tDst), [src] "+r" (tSrc), [count] "+r" (count)

	// No unclobbered inputs
	:

	// Clobbered vector registers
	// NB: these are the quad aliases of the double registers used in the asm
	: "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "cc", "memory"
	);

#else

	while(count >= 8) {
		uint8x16x4_t t1 = vld4q_u8(tSrc);
		uint8x16x4_t t2 = vld4q_u8(tSrc + sizeof(uint8x16x4_t));
		tSrc += sizeof(uint8x16x4_t) * 2;
		vst4q_u8(tDst, t1);
		vst4q_u8(tDst + sizeof(uint8x16x4_t), t2);
		tDst += sizeof(uint8x16x4_t) * 2;
		count -= 8;
	}

	if(count & 4) {
		uint8x16x4_t t1 = vld4q_u8(tSrc);
		tSrc += sizeof(uint8x16x4_t);
		vst4q_u8(tDst, t1);
		tDst += sizeof(uint8x16x4_t);
	}

	if(count & 2) {
		uint8x8x4_t t1 = vld4_u8(tSrc);
		tSrc += sizeof(uint8x8x4_t);
		vst4_u8(tDst, t1);
		tDst += sizeof(uint8x8x4_t);
	}

	if(count & 1) {
		uint8x16_t t1 = vld1q_u8(tSrc);
		tSrc += sizeof(uint8x16_t);
		vst1q_u8(tDst, t1);
		tDst += sizeof(uint8x16_t);
	}

#endif  // !USE_GCC_INLINE_ASM

	if(trailerCount) {
		if(trailerCount & 8) {
			uint8x8_t t1 = vld1_u8(tSrc);
			tSrc += sizeof(uint8x8_t);
			vst1_u8(tDst, t1);
			tDst += sizeof(uint8x8_t);
		}

		if(trailerCount & 4) {
			*((uint32_t*) tDst) = *((uint32_t*) tSrc);
			tDst += 4;
			tSrc += 4;
		}

		if(trailerCount & 2) {
			*((uint16_t*) tDst) = *((uint16_t*) tSrc);
			tDst += 2;
			tSrc += 2;
		}

		if(trailerCount & 1) {
			*tDst++ = *tSrc++;
		}
	}
}

static inline void SolidOver565_8pix_neon(
	uint32_t  glyphColour,
	uint16_t *dest,
	uint8_t  *inMask,
	uint32_t  destStride,  // bytes, not elements
	uint32_t  maskStride,
	uint32_t  count        // 8-pixel groups
)
{
	// Inner loop of glyph blitter (solid colour, alpha mask)

#ifdef USE_GCC_INLINE_ASM

	asm volatile (
	"	vld4.8 {d20[],d21[],d22[],d23[]}, [%[glyphColour]]  @ splat solid colour components	\n"
	"0:	@ loop																				\n"
	"	vld1.16   {d0,d1}, [%[dest]]         @ load first pixels from framebuffer			\n"
	"	vld1.8    {d17}, [%[inMask]]         @ load alpha mask of glyph						\n"
	"	vmull.u8  q9, d17, d23               @ apply glyph colour alpha to mask				\n"
	"	vshrn.u16 d17, q9, #8                @ reformat it to match original mask			\n"
	"	vmvn      d18, d17                   @ we need the inverse mask for the background	\n"
	"	vsli.u16  q3, q0, #5                 @ duplicate framebuffer blue bits				\n"
	"	vshrn.u16 d2, q0, #8                 @ unpack red from framebuffer pixels			\n"
	"	vshrn.u16 d4, q0, #3                 @ unpack green									\n"
	"	vsri.u8   d2, d2, #5                 @ duplicate red bits (extend 5 to 8)			\n"
	"	vshrn.u16 d6, q3, #2                 @ unpack extended blue (truncate 10 to 8)		\n"
	"	vsri.u8   d4, d4, #6                 @ duplicate green bits (extend 6 to 8)			\n"
	"	vmull.u8  q1, d2, d18                @ apply inverse mask to background red...		\n"
	"	vmull.u8  q2, d4, d18                @ ...green...									\n"
	"	vmull.u8  q3, d6, d18                @ ...blue										\n"
	"	subs      %[count], %[count], #1     @ decrement/test loop counter					\n"
	"	vmlal.u8  q1, d17, d22               @ add masked foreground red...					\n"
	"	vmlal.u8  q2, d17, d21               @ ...green...									\n"
	"	vmlal.u8  q3, d17, d20               @ ...blue										\n"
	"	add %[inMask], %[inMask], %[maskStride] @ advance mask pointer, while we wait		\n"
	"	vsri.16   q1, q2, #5                 @ pack green behind red						\n"
	"	vsri.16   q1, q3, #11                @ pack blue into pixels						\n"
	"	vst1.16   {d2,d3}, [%[dest]]         @ store composited pixels						\n"
	"	add %[dest], %[dest], %[destStride]  @ advance framebuffer pointer					\n"
	"	bne 0b                               @ next please									\n"

	// Clobbered registers marked as input/outputs
	: [dest] "+r" (dest), [inMask] "+r" (inMask), [count] "+r" (count)

	// Inputs
	: [destStride] "r" (destStride), [maskStride] "r" (maskStride), [glyphColour] "r" (&glyphColour)

	// Clobbers, including the inputs we modify, and potentially lots of memory
	: "q0", "q1", "q2", "q3", "d17", "q9", "q10", "q11", "q12", "cc", "memory"
	);

#else

	uint8x8x4_t solidColour = vld4_dup_u8((uint8_t*) &glyphColour);

	while(count--)
	{
		uint16x8_t  pixels = vld1q_u16(dest);
		uint8x8_t   mask = vshrn_n_u16(vmull_u8(solidColour.val[3], vld1_u8(inMask)), 8);
		uint8x8_t  iMask = vmvn_u8(mask);

		uint8x8_t  tRed   = vshrn_n_u16(pixels, 8);
		uint8x8_t  tGreen = vshrn_n_u16(pixels, 3);
		uint8x8_t  tBlue  = vshrn_n_u16(vsli_n_u8(pixels, pixels, 5), 2);

		uint16x8_t sRed   = vmull_u8(vsri_n_u8(tRed  , tRed  , 5), iMask);
		uint16x8_t sGreen = vmull_u8(vsri_n_u8(tGreen, tGreen, 6), iMask);
		uint16x8_t sBlue  = vmull_u8(          tBlue             , iMask);

		sRed   = vmlal(sRed  , mask, solidColour.val[2]);
		sGreen = vmlal(sGreen, mask, solidColour.val[1]);
		sBlue  = vmlal(sBlue , mask, solidColour.val[0]);

		pixels = vsri_n_u16(sRed, sGreen, 5);
		pixels = vsri_n_u16(pixels, sBlue, 11);
		vst1q_u16(dest, pixels);

		dest += destStride;
		mask += maskStride;
	}

#endif
}

void
fbCompositeSolidMask_nx8x0565neon (
	pixman_implementation_t * impl,
	pixman_op_t op,
	pixman_image_t * pSrc,
	pixman_image_t * pMask,
	pixman_image_t * pDst,
	int32_t      xSrc,
	int32_t      ySrc,
	int32_t      xMask,
	int32_t      yMask,
	int32_t      xDst,
	int32_t      yDst,
	int32_t      width,
	int32_t      height)
{
	uint32_t     src, srca;
	uint16_t    *dstLine, *alignedLine;
	uint8_t     *maskLine;
	uint32_t     dstStride, maskStride;
	uint32_t     kernelCount, copyCount;
	uint8_t      kernelOffset, copyOffset;

	_pixman_image_get_solid(pSrc, src, pDst->bits.format);

	// bail out if fully transparent or degenerate
	srca = src >> 24;
	if(srca == 0)
		return;
	if(width == 0 || height == 0)
		return;

	if(width > NEON_SCANLINE_BUFFER_PIXELS) {
		// split the blit, so we can use a fixed-size scanline buffer
		// TODO: there must be a more elegant way of doing this.
		int x;
		for(x=0; x < width; x += NEON_SCANLINE_BUFFER_PIXELS) {
			fbCompositeSolidMask_nx8x0565neon(impl, op, pSrc, pMask, pDst, xSrc+x, ySrc, xMask+x, yMask, xDst+x, yDst,
											  (x+NEON_SCANLINE_BUFFER_PIXELS > width) ? width-x : NEON_SCANLINE_BUFFER_PIXELS, height);
		}
		return;
	}

	fbComposeGetStart (pDst, xDst, yDst, uint16_t, dstStride, dstLine, 1);
	fbComposeGetStart (pMask, xMask, yMask, uint8_t, maskStride, maskLine, 1);

	// keep within minimum number of aligned quadwords on width
	// while also keeping the minimum number of columns to process
	{
		unsigned long alignedLeft = (unsigned long)(dstLine) & ~0xF;
		unsigned long alignedRight = (((unsigned long)(dstLine + width)) + 0xF) & ~0xF;
		unsigned long ceilingLength = (((unsigned long) width) * sizeof(*dstLine) + 0xF) & ~0xF;

		// the fast copy must always be quadword aligned
		copyOffset = dstLine - ((uint16_t*) alignedLeft);
		alignedLine = dstLine - copyOffset;
		copyCount = (uint32_t) ((alignedRight - alignedLeft) >> 4);

		if(alignedRight - alignedLeft > ceilingLength) {
			// unaligned routine is tightest, and will not overrun
			kernelCount = (uint32_t) (ceilingLength >> 4);
			kernelOffset = copyOffset;
		} else {
			// aligned routine is equally tight, so it is safer to align
			kernelCount = copyCount;
			kernelOffset = 0;
		}
	}

	{
		uint16_t scanLine[NEON_SCANLINE_BUFFER_PIXELS + 8]; // deliberately not initialised
		uint8_t glyphLine[NEON_SCANLINE_BUFFER_PIXELS + 8];
		int y = height;

		// row-major order
		// left edge, middle block, right edge
		for( ; y--; maskLine += maskStride, alignedLine += dstStride, dstLine += dstStride) {
			// We don't want to overrun the edges of the glyph, so realign the edge data into known buffers
			QuadwordCopy_neon(glyphLine + copyOffset, maskLine, width >> 4, width & 0xF);

			// Uncached framebuffer access is really, really slow if we do it piecemeal.
			// It should be much faster if we grab it all at once.
			// One scanline should easily fit in L1 cache, so this should not waste RAM bandwidth.
			QuadwordCopy_neon(scanLine, alignedLine, copyCount, 0);

			// Apply the actual filter
			SolidOver565_8pix_neon(src, scanLine + kernelOffset, glyphLine + kernelOffset, 8 * sizeof(*dstLine), 8, kernelCount);

			// Copy the modified scanline back
			QuadwordCopy_neon(dstLine, scanLine + copyOffset, width >> 3, (width & 7) * 2);
		}
	}
}

#ifdef USE_GCC_INLINE_ASM

static inline void PlainOver565_8pix_neon(
	uint32_t  colour,
	uint16_t *dest,
	uint32_t  destStride,  // bytes, not elements
	uint32_t  count        // 8-pixel groups
)
{
	// Inner loop for plain translucent rects (solid colour without alpha mask)
	asm volatile (
	"	vld4.8   {d20[],d21[],d22[],d23[]}, [%[colour]]  @ solid colour load/splat \n"
	"	vmull.u8  q12, d23, d22              @ premultiply alpha red   \n"
	"	vmull.u8  q13, d23, d21              @ premultiply alpha green \n"
	"	vmull.u8  q14, d23, d20              @ premultiply alpha blue  \n"
	"	vmvn      d18, d23                   @ inverse alpha for background \n"
	"0:	@ loop\n"
	"	vld1.16   {d0,d1}, [%[dest]]         @ load first pixels from framebuffer	\n"
	"	vshrn.u16 d2, q0, #8                 @ unpack red from framebuffer pixels	\n"
	"	vshrn.u16 d4, q0, #3                 @ unpack green				\n"
	"	vsli.u16  q3, q0, #5                 @ duplicate framebuffer blue bits		\n"
	"	vsri.u8   d2, d2, #5                 @ duplicate red bits (extend 5 to 8)	\n"
	"	vsri.u8   d4, d4, #6                 @ duplicate green bits (extend 6 to 8)	\n"
	"	vshrn.u16 d6, q3, #2                 @ unpack extended blue (truncate 10 to 8)	\n"
	"	vmov      q0, q12                    @ retrieve foreground red   \n"
	"	vmlal.u8  q0, d2, d18                @ blend red - my kingdom for a four-operand MLA \n"
	"	vmov      q1, q13                    @ retrieve foreground green \n"
	"	vmlal.u8  q1, d4, d18                @ blend green               \n"
	"	vmov      q2, q14                    @ retrieve foreground blue  \n"
	"	vmlal.u8  q2, d6, d18                @ blend blue                \n"
	"	subs      %[count], %[count], #1     @ decrement/test loop counter		\n"
	"	vsri.16   q0, q1, #5                 @ pack green behind red			\n"
	"	vsri.16   q0, q2, #11                @ pack blue into pixels			\n"
	"	vst1.16   {d0,d1}, [%[dest]]         @ store composited pixels			\n"
	"	add %[dest], %[dest], %[destStride]  @ advance framebuffer pointer		\n"
	"	bne 0b                               @ next please				\n"

	// Clobbered registers marked as input/outputs
	: [dest] "+r" (dest), [count] "+r" (count)

	// Inputs
	: [destStride] "r" (destStride), [colour] "r" (&colour)

	// Clobbers, including the inputs we modify, and potentially lots of memory
	: "q0", "q1", "q2", "q3", "q9", "q10", "q11", "q12", "q13", "q14", "cc", "memory"
	);
}

void
fbCompositeSolid_nx0565neon (
	pixman_implementation_t * impl,
	pixman_op_t op,
	pixman_image_t * pSrc,
	pixman_image_t * pMask,
	pixman_image_t * pDst,
	int32_t      xSrc,
	int32_t      ySrc,
	int32_t      xMask,
	int32_t      yMask,
	int32_t      xDst,
	int32_t      yDst,
	int32_t      width,
	int32_t      height)
{
	uint32_t     src, srca;
	uint16_t    *dstLine, *alignedLine;
	uint32_t     dstStride;
	uint32_t     kernelCount, copyCount;
	uint8_t      kernelOffset, copyOffset;

	_pixman_image_get_solid(pSrc, src, pDst->bits.format);

	// bail out if fully transparent
	srca = src >> 24;
	if(srca == 0)
		return;
	if(width == 0 || height == 0)
		return;

	if(width > NEON_SCANLINE_BUFFER_PIXELS) {
		// split the blit, so we can use a fixed-size scanline buffer
		// TODO: there must be a more elegant way of doing this.
		int x;
		for(x=0; x < width; x += NEON_SCANLINE_BUFFER_PIXELS) {
			fbCompositeSolid_nx0565neon(impl, op, pSrc, pMask, pDst, xSrc+x, ySrc, xMask+x, yMask, xDst+x, yDst,
										(x+NEON_SCANLINE_BUFFER_PIXELS > width) ? width-x : NEON_SCANLINE_BUFFER_PIXELS, height);
		}
		return;
	}

	fbComposeGetStart (pDst, xDst, yDst, uint16_t, dstStride, dstLine, 1);

	// keep within minimum number of aligned quadwords on width
	// while also keeping the minimum number of columns to process
	{
		unsigned long alignedLeft = (unsigned long)(dstLine) & ~0xF;
		unsigned long alignedRight = (((unsigned long)(dstLine + width)) + 0xF) & ~0xF;
		unsigned long ceilingLength = (((unsigned long) width) * sizeof(*dstLine) + 0xF) & ~0xF;

		// the fast copy must always be quadword aligned
		copyOffset = dstLine - ((uint16_t*) alignedLeft);
		alignedLine = dstLine - copyOffset;
		copyCount = (uint32_t) ((alignedRight - alignedLeft) >> 4);

		if(alignedRight - alignedLeft > ceilingLength) {
			// unaligned routine is tightest, and will not overrun
			kernelCount = (uint32_t) (ceilingLength >> 4);
			kernelOffset = copyOffset;
		} else {
			// aligned routine is equally tight, so it is safer to align
			kernelCount = copyCount;
			kernelOffset = 0;
		}
	}

	{
		uint16_t scanLine[NEON_SCANLINE_BUFFER_PIXELS + 8]; // deliberately not initialised

		// row-major order
		// left edge, middle block, right edge
		for( ; height--; alignedLine += dstStride, dstLine += dstStride) {

			// Uncached framebuffer access is really, really slow if we do it piecemeal.
			// It should be much faster if we grab it all at once.
			// One scanline should easily fit in L1 cache, so this should not waste RAM bandwidth.
			QuadwordCopy_neon(scanLine, alignedLine, copyCount, 0);

			// Apply the actual filter
			PlainOver565_8pix_neon(src, scanLine + kernelOffset, 8 * sizeof(*dstLine), kernelCount);

			// Copy the modified scanline back
			QuadwordCopy_neon(dstLine, scanLine + copyOffset, width >> 3, (width & 7) * 2);
		}
	}
}

static inline void ARGB8_Over565_8pix_neon(
	uint32_t *src,
	uint16_t *dest,
	uint32_t  srcStride,  // bytes, not elements
	uint32_t  count        // 8-pixel groups
)
{
	asm volatile (
	"0:	@ loop\n"
	"	pld   [%[src], %[srcStride]]         @ preload from next scanline	\n"
	"	vld1.16   {d0,d1}, [%[dest]]         @ load pixels from framebuffer	\n"
	"	vld4.8   {d20,d21,d22,d23},[%[src]]! @ load source image pixels		\n"
	"	vsli.u16  q3, q0, #5                 @ duplicate framebuffer blue bits		\n"
	"	vshrn.u16 d2, q0, #8                 @ unpack red from framebuffer pixels	\n"
	"	vshrn.u16 d4, q0, #3                 @ unpack green				\n"
	"	vmvn      d18, d23                   @ we need the inverse alpha for the background	\n"
	"	vsri.u8   d2, d2, #5                 @ duplicate red bits (extend 5 to 8)	\n"
	"	vshrn.u16 d6, q3, #2                 @ unpack extended blue (truncate 10 to 8)	\n"
	"	vsri.u8   d4, d4, #6                 @ duplicate green bits (extend 6 to 8)	\n"
	"	vmull.u8  q1, d2, d18                @ apply inverse alpha to background red...	\n"
	"	vmull.u8  q2, d4, d18                @ ...green...				\n"
	"	vmull.u8  q3, d6, d18                @ ...blue					\n"
	"	subs      %[count], %[count], #1     @ decrement/test loop counter		\n"
	"	vmlal.u8  q1, d23, d22               @ add blended foreground red...		\n"
	"	vmlal.u8  q2, d23, d21               @ ...green...				\n"
	"	vmlal.u8  q3, d23, d20               @ ...blue					\n"
	"	vsri.16   q1, q2, #5                 @ pack green behind red			\n"
	"	vsri.16   q1, q3, #11                @ pack blue into pixels			\n"
	"	vst1.16   {d2,d3}, [%[dest]]!        @ store composited pixels			\n"
	"	bne 0b                               @ next please				\n"

	// Clobbered registers marked as input/outputs
	: [dest] "+r" (dest), [src] "+r" (src), [count] "+r" (count)

	// Inputs
	: [srcStride] "r" (srcStride)

	// Clobbers, including the inputs we modify, and potentially lots of memory
	: "q0", "q1", "q2", "q3", "d17", "d18", "q10", "q11", "cc", "memory"
	);
}

void
fbCompositeOver_8888x0565neon (
	pixman_implementation_t * impl,
	pixman_op_t op,
	pixman_image_t * pSrc,
	pixman_image_t * pMask,
	pixman_image_t * pDst,
	int32_t      xSrc,
	int32_t      ySrc,
	int32_t      xMask,
	int32_t      yMask,
	int32_t      xDst,
	int32_t      yDst,
	int32_t      width,
	int32_t      height)
{
	uint32_t    *srcLine;
	uint16_t    *dstLine, *alignedLine;
	uint32_t     dstStride, srcStride;
	uint32_t     kernelCount, copyCount;
	uint8_t      kernelOffset, copyOffset;

	// we assume mask is opaque
	// so the only alpha to deal with is embedded in src

	if(width > NEON_SCANLINE_BUFFER_PIXELS) {
		// split the blit, so we can use a fixed-size scanline buffer
		int x;
		for(x=0; x < width; x += NEON_SCANLINE_BUFFER_PIXELS) {
			fbCompositeOver_8888x0565neon(impl, op, pSrc, pMask, pDst, xSrc+x, ySrc, xMask+x, yMask, xDst+x, yDst,
										  (x+NEON_SCANLINE_BUFFER_PIXELS > width) ? width-x : NEON_SCANLINE_BUFFER_PIXELS, height);
		}
		return;
	}

	fbComposeGetStart (pDst, xDst, yDst, uint16_t, dstStride, dstLine, 1);
	fbComposeGetStart (pSrc, xSrc, ySrc, uint32_t, srcStride, srcLine, 1);

	// keep within minimum number of aligned quadwords on width
	// while also keeping the minimum number of columns to process
	{
		unsigned long alignedLeft = (unsigned long)(dstLine) & ~0xF;
		unsigned long alignedRight = (((unsigned long)(dstLine + width)) + 0xF) & ~0xF;
		unsigned long ceilingLength = (((unsigned long) width) * sizeof(*dstLine) + 0xF) & ~0xF;

		// the fast copy must always be quadword aligned
		copyOffset = dstLine - ((uint16_t*) alignedLeft);
		alignedLine = dstLine - copyOffset;
		copyCount = (uint32_t) ((alignedRight - alignedLeft) >> 4);

		if(alignedRight - alignedLeft > ceilingLength) {
			// unaligned routine is tightest, and will not overrun
			kernelCount = (uint32_t) (ceilingLength >> 4);
			kernelOffset = copyOffset;
		} else {
			// aligned routine is equally tight, so it is safer to align
			kernelCount = copyCount;
			kernelOffset = 0;
		}
	}

	/* Preload the first input scanline */
	{
		uint8_t *srcPtr = (uint8_t*) srcLine;
		uint32_t count = (width + 15) / 16;

#ifdef USE_GCC_INLINE_ASM
		asm volatile (
		"0: @ loop						\n"
		"	subs    %[count], %[count], #1			\n"
		"	pld     [%[src]]				\n"
		"	add     %[src], %[src], #64			\n"
		"	bgt 0b						\n"

		// Clobbered input registers marked as input/outputs
		: [src] "+r" (srcPtr), [count] "+r" (count)
		: // no unclobbered inputs
		: "cc"
		);
#else
		do {
			__pld(srcPtr);
			srcPtr += 64;
		} while(--count);
#endif
	}

	{
		uint16_t scanLine[NEON_SCANLINE_BUFFER_PIXELS + 8]; // deliberately not initialised

		// row-major order
		// left edge, middle block, right edge
		for( ; height--; srcLine += srcStride, alignedLine += dstStride) {
			// Uncached framebuffer access is really, really slow if we do it piecemeal.
			// It should be much faster if we grab it all at once.
			// One scanline should easily fit in L1 cache, so this should not waste RAM bandwidth.
			QuadwordCopy_neon(scanLine, alignedLine, copyCount, 0);

			// Apply the actual filter
			ARGB8_Over565_8pix_neon(srcLine, scanLine + kernelOffset, srcStride * sizeof(*srcLine), kernelCount);

			// Copy the modified scanline back
			QuadwordCopy_neon(dstLine, scanLine + copyOffset, width >> 3, (width & 7) * 2);
		}
	}
}

#endif  // USE_GCC_INLINE_ASM

static const pixman_fast_path_t arm_neon_fast_path_array[] = 
{
    { PIXMAN_OP_ADD,  PIXMAN_solid,    PIXMAN_a8,       PIXMAN_a8,       fbCompositeSrcAdd_8888x8x8neon,        0 },
    { PIXMAN_OP_ADD,  PIXMAN_a8,       PIXMAN_null,     PIXMAN_a8,       fbCompositeSrcAdd_8000x8000neon,       0 },
    { PIXMAN_OP_OVER, PIXMAN_solid,    PIXMAN_a8,       PIXMAN_r5g6b5,   fbCompositeSolidMask_nx8x0565neon,     0 },
    { PIXMAN_OP_OVER, PIXMAN_solid,    PIXMAN_a8,       PIXMAN_b5g6r5,   fbCompositeSolidMask_nx8x0565neon,     0 },
    { PIXMAN_OP_SRC,  PIXMAN_a8r8g8b8, PIXMAN_null,     PIXMAN_r5g6b5,   fbCompositeSrc_24x16neon,              0 },
    { PIXMAN_OP_SRC,  PIXMAN_x8r8g8b8, PIXMAN_null,     PIXMAN_r5g6b5,   fbCompositeSrc_24x16neon,              0 },
    { PIXMAN_OP_SRC,  PIXMAN_a8b8g8r8, PIXMAN_null,     PIXMAN_b5g6r5,   fbCompositeSrc_24x16neon,              0 },
    { PIXMAN_OP_SRC,  PIXMAN_x8b8g8r8, PIXMAN_null,     PIXMAN_b5g6r5,   fbCompositeSrc_24x16neon,              0 },
#ifdef USE_GCC_INLINE_ASM
    { PIXMAN_OP_SRC,  PIXMAN_r5g6b5,   PIXMAN_null,     PIXMAN_r5g6b5,   fbCompositeSrc_16x16neon,              0 },
    { PIXMAN_OP_SRC,  PIXMAN_b5g6r5,   PIXMAN_null,     PIXMAN_b5g6r5,   fbCompositeSrc_16x16neon,              0 },
    { PIXMAN_OP_OVER, PIXMAN_solid,    PIXMAN_null,     PIXMAN_r5g6b5,   fbCompositeSolid_nx0565neon,           0 },
    { PIXMAN_OP_OVER, PIXMAN_solid,    PIXMAN_null,     PIXMAN_b5g6r5,   fbCompositeSolid_nx0565neon,           0 },
    { PIXMAN_OP_OVER, PIXMAN_a8r8g8b8, PIXMAN_null,     PIXMAN_r5g6b5,   fbCompositeOver_8888x0565neon,         0 },
    { PIXMAN_OP_OVER, PIXMAN_a8b8g8r8, PIXMAN_null,     PIXMAN_b5g6r5,   fbCompositeOver_8888x0565neon,         0 },
#endif
    { PIXMAN_OP_OVER, PIXMAN_a8r8g8b8, PIXMAN_null,     PIXMAN_a8r8g8b8, fbCompositeSrc_8888x8888neon,          0 },
    { PIXMAN_OP_OVER, PIXMAN_a8r8g8b8, PIXMAN_null,     PIXMAN_x8r8g8b8, fbCompositeSrc_8888x8888neon,          0 },
    { PIXMAN_OP_OVER, PIXMAN_a8b8g8r8, PIXMAN_null,     PIXMAN_a8b8g8r8, fbCompositeSrc_8888x8888neon,          0 },
    { PIXMAN_OP_OVER, PIXMAN_a8b8g8r8, PIXMAN_null,     PIXMAN_x8b8g8r8, fbCompositeSrc_8888x8888neon,          0 },
    { PIXMAN_OP_OVER, PIXMAN_a8r8g8b8, PIXMAN_a8,       PIXMAN_a8r8g8b8, fbCompositeSrc_8888x8x8888neon,        NEED_SOLID_MASK },
    { PIXMAN_OP_OVER, PIXMAN_a8r8g8b8, PIXMAN_a8,       PIXMAN_x8r8g8b8, fbCompositeSrc_8888x8x8888neon,        NEED_SOLID_MASK },
    { PIXMAN_OP_OVER, PIXMAN_solid,    PIXMAN_a8,       PIXMAN_a8r8g8b8, fbCompositeSolidMask_nx8x8888neon,     0 },
    { PIXMAN_OP_OVER, PIXMAN_solid,    PIXMAN_a8,       PIXMAN_x8r8g8b8, fbCompositeSolidMask_nx8x8888neon,     0 },
    { PIXMAN_OP_OVER, PIXMAN_solid,    PIXMAN_a8,       PIXMAN_a8b8g8r8, fbCompositeSolidMask_nx8x8888neon,     0 },
    { PIXMAN_OP_OVER, PIXMAN_solid,    PIXMAN_a8,       PIXMAN_x8b8g8r8, fbCompositeSolidMask_nx8x8888neon,     0 },
    { PIXMAN_OP_NONE },
};

const pixman_fast_path_t *const arm_neon_fast_paths = arm_neon_fast_path_array;

static void
arm_neon_composite (pixman_implementation_t *imp,
		pixman_op_t     op,
		pixman_image_t *src,
		pixman_image_t *mask,
		pixman_image_t *dest,
		int32_t         src_x,
		int32_t         src_y,
		int32_t         mask_x,
		int32_t         mask_y,
		int32_t         dest_x,
		int32_t         dest_y,
		int32_t        width,
		int32_t        height)
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

pixman_bool_t
pixman_blt_neon (
	void *src_bits,
	void *dst_bits,
	int src_stride,
	int dst_stride,
	int src_bpp,
	int dst_bpp,
	int src_x, int src_y,
	int dst_x, int dst_y,
	int width, int height)
{
	if(!width || !height)
		return TRUE;

	// accelerate only straight copies involving complete bytes
	if(src_bpp != dst_bpp || (src_bpp & 7))
		return FALSE;

	{
		uint32_t bytes_per_pixel = src_bpp >> 3;
		uint32_t byte_width = width * bytes_per_pixel;
		int32_t src_stride_bytes = src_stride * 4; // parameter is in words for some reason
		int32_t dst_stride_bytes = dst_stride * 4;
		uint8_t *src_bytes = ((uint8_t*) src_bits) + src_y * src_stride_bytes + src_x * bytes_per_pixel;
		uint8_t *dst_bytes = ((uint8_t*) dst_bits) + dst_y * dst_stride_bytes + dst_x * bytes_per_pixel;
		uint32_t quadword_count = byte_width / 16;
		uint32_t offset         = byte_width % 16;

		while(height--) {
			QuadwordCopy_neon(dst_bytes, src_bytes, quadword_count, offset);
			src_bytes += src_stride_bytes;
			dst_bytes += dst_stride_bytes;
		}
	}

	return TRUE;
}

static pixman_bool_t
arm_neon_blt (pixman_implementation_t *imp,
	  uint32_t *src_bits,
	  uint32_t *dst_bits,
	  int src_stride,
	  int dst_stride,
	  int src_bpp,
	  int dst_bpp,
	  int src_x, int src_y,
	  int dst_x, int dst_y,
	  int width, int height)
{
	if (pixman_blt_neon (
			src_bits, dst_bits, src_stride, dst_stride, src_bpp, dst_bpp,
			src_x, src_y, dst_x, dst_y, width, height))
		return TRUE;

	return _pixman_implementation_blt (
			imp->delegate,
			src_bits, dst_bits, src_stride, dst_stride, src_bpp, dst_bpp,
			src_x, src_y, dst_x, dst_y, width, height);
}

static pixman_bool_t
arm_neon_fill (pixman_implementation_t *imp,
	   uint32_t *bits,
	   int stride,
	   int bpp,
	   int x,
	   int y,
	   int width,
	   int height,
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
	pixman_implementation_t *simd = _pixman_implementation_create_arm_simd();
	pixman_implementation_t *imp  = _pixman_implementation_create (simd);

	imp->composite = arm_neon_composite;
	imp->blt = arm_neon_blt;
	imp->fill = arm_neon_fill;

	return imp;
}
