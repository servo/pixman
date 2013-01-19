/*
 * Copyright © 2012 Raspberry Pi Foundation
 * Copyright © 2012 RISC OS Open Ltd
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The copyright holders make no
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
 * Author:  Ben Avison (bavison@riscosopen.org)
 *
 */

/* Prevent the stack from becoming executable */
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

	.text
	.arch armv6
	.object_arch armv4
	.arm
	.altmacro
	.p2align 2

#include "pixman-arm-simd-asm.h"

/* A head macro should do all processing which results in an output of up to
 * 16 bytes, as far as the final load instruction. The corresponding tail macro
 * should complete the processing of the up-to-16 bytes. The calling macro will
 * sometimes choose to insert a preload or a decrement of X between them.
 *   cond           ARM condition code for code block
 *   numbytes       Number of output bytes that should be generated this time
 *   firstreg       First WK register in which to place output
 *   unaligned_src  Whether to use non-wordaligned loads of source image
 *   unaligned_mask Whether to use non-wordaligned loads of mask image
 *   preload        If outputting 16 bytes causes 64 bytes to be read, whether an extra preload should be output
 */

.macro src_n_8888_init
        ldr     SRC, [sp, #ARGS_STACK_OFFSET]
        mov     STRIDE_S, SRC
        mov     MASK, SRC
        mov     STRIDE_M, SRC
.endm

.macro src_n_0565_init
        ldrh    SRC, [sp, #ARGS_STACK_OFFSET]
        orr     SRC, SRC, lsl #16
        mov     STRIDE_S, SRC
        mov     MASK, SRC
        mov     STRIDE_M, SRC
.endm

.macro src_n_8_init
        ldrb    SRC, [sp, #ARGS_STACK_OFFSET]
        orr     SRC, SRC, lsl #8
        orr     SRC, SRC, lsl #16
        mov     STRIDE_S, SRC
        mov     MASK, SRC
        mov     STRIDE_M, SRC
.endm

.macro fill_process_tail  cond, numbytes, firstreg
    WK4     .req    SRC
    WK5     .req    STRIDE_S
    WK6     .req    MASK
    WK7     .req    STRIDE_M
        pixst   cond, numbytes, 4, DST
    .unreq  WK4
    .unreq  WK5
    .unreq  WK6
    .unreq  WK7
.endm

generate_composite_function \
    pixman_composite_src_n_8888_asm_armv6, 0, 0, 32, \
    FLAG_DST_WRITEONLY | FLAG_COND_EXEC | FLAG_PROCESS_PRESERVES_PSR | FLAG_PROCESS_DOES_STORE | FLAG_PROCESS_PRESERVES_SCRATCH \
    0, /* prefetch distance doesn't apply */ \
    src_n_8888_init \
    nop_macro, /* newline */ \
    nop_macro /* cleanup */ \
    nop_macro /* process head */ \
    fill_process_tail

generate_composite_function \
    pixman_composite_src_n_0565_asm_armv6, 0, 0, 16, \
    FLAG_DST_WRITEONLY | FLAG_COND_EXEC | FLAG_PROCESS_PRESERVES_PSR | FLAG_PROCESS_DOES_STORE | FLAG_PROCESS_PRESERVES_SCRATCH \
    0, /* prefetch distance doesn't apply */ \
    src_n_0565_init \
    nop_macro, /* newline */ \
    nop_macro /* cleanup */ \
    nop_macro /* process head */ \
    fill_process_tail

generate_composite_function \
    pixman_composite_src_n_8_asm_armv6, 0, 0, 8, \
    FLAG_DST_WRITEONLY | FLAG_COND_EXEC | FLAG_PROCESS_PRESERVES_PSR | FLAG_PROCESS_DOES_STORE | FLAG_PROCESS_PRESERVES_SCRATCH \
    0, /* prefetch distance doesn't apply */ \
    src_n_8_init \
    nop_macro, /* newline */ \
    nop_macro /* cleanup */ \
    nop_macro /* process head */ \
    fill_process_tail

/******************************************************************************/

