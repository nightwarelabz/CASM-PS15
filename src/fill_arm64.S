/* CASM-PS15 AArch64 assembly hot loops:
 *   1. ps15_fill32        — NEON 64-byte unrolled 32-bit buffer fill
 *   2. ps15_upscale2x     — NEON zip1/zip2 nearest-neighbor 2x upscaler
 *   3. ps15_upscale2x_crt — NEON 2x upscaler + 75% Trinitron odd-row scanlines
 *   4. ps15_span_tex64    — Opaque textured Gouraud + fog + 1/z + Bayer RGB555
 *   5. ps15_span_abr64    — PS1 ABR semi-transparent (HALF/ADD/SUB/QUARTER) span
 *
 * Every routine is bit-for-bit identical to its portable C counterpart in
 * src/raster.c (verified by tools/test_engine.c). Never touches x18. */

#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

    .text
    .align 4

/* =========================================================================
 * void ps15_fill32(uint32_t *dst=x0, uint32_t value=w1, size_t count=x2)
 * ========================================================================= */
    .globl SYM(ps15_fill32)
#if !defined(__APPLE__)
    .type  ps15_fill32, %function
#endif
SYM(ps15_fill32):
    cbz     x2, .Lfill_done
    dup     v0.4s, w1
.Lfill16:
    cmp     x2, #16
    b.lo    .Lfill4
    stp     q0, q0, [x0], #32
    stp     q0, q0, [x0], #32
    sub     x2, x2, #16
    b       .Lfill16
.Lfill4:
    cmp     x2, #4
    b.lo    .Lfill1
    str     q0, [x0], #16
    sub     x2, x2, #4
    b       .Lfill4
.Lfill1:
    cbz     x2, .Lfill_done
    str     w1, [x0], #4
    sub     x2, x2, #1
    b       .Lfill1
.Lfill_done:
    ret
#if !defined(__APPLE__)
    .size   ps15_fill32, .-ps15_fill32
#endif

/* =========================================================================
 * void ps15_upscale2x(uint32_t *dst=x0, const uint32_t *src=x1, int w=w2, int h=w3)
 * Expands w*h framebuffer into (2w)*(2h) in one cache-friendly pass.
 * ========================================================================= */
    .globl SYM(ps15_upscale2x)
#if !defined(__APPLE__)
    .type  ps15_upscale2x, %function
#endif
SYM(ps15_upscale2x):
    cmp     w2, #0
    b.le    .Lup_done
    cmp     w3, #0
    b.le    .Lup_done
    sxtw    x2, w2
    sxtw    x3, w3
    lsl     x4, x2, #3              /* dst row stride in bytes = 2 * w * 4 */
.Lup_row:
    mov     x5, x0                  /* dst row 2*y */
    add     x6, x0, x4              /* dst row 2*y + 1 */
    mov     x7, x2                  /* remaining source pixels */
.Lup4:
    cmp     x7, #4
    b.lo    .Lup1
    ldr     q0, [x1], #16
    zip1    v1.4s, v0.4s, v0.4s     /* [p0, p0, p1, p1] */
    zip2    v2.4s, v0.4s, v0.4s     /* [p2, p2, p3, p3] */
    stp     q1, q2, [x5], #32
    stp     q1, q2, [x6], #32
    sub     x7, x7, #4
    b       .Lup4
.Lup1:
    cbz     x7, .Lup_next
    ldr     w9, [x1], #4
    bfi     x9, x9, #32, #32        /* duplicate pixel into upper 32 bits */
    str     x9, [x5], #8
    str     x9, [x6], #8
    sub     x7, x7, #1
    b       .Lup1
.Lup_next:
    add     x0, x0, x4, lsl #1      /* advance dst by 2 output rows */
    subs    x3, x3, #1
    b.ne    .Lup_row
.Lup_done:
    ret
#if !defined(__APPLE__)
    .size   ps15_upscale2x, .-ps15_upscale2x
#endif

/* =========================================================================
 * void ps15_upscale2x_crt(uint32_t *dst=x0, const uint32_t *src=x1, int w=w2, int h=w3)
 * Even rows = 100%, odd rows = p - ((p >> 2) & 0x003F3F3F) (75% PVM scanline).
 * ========================================================================= */
    .globl SYM(ps15_upscale2x_crt)
#if !defined(__APPLE__)
    .type  ps15_upscale2x_crt, %function
#endif
SYM(ps15_upscale2x_crt):
    cmp     w2, #0
    b.le    .Lcrt_done
    cmp     w3, #0
    b.le    .Lcrt_done
    sxtw    x2, w2
    sxtw    x3, w3
    lsl     x4, x2, #3              /* dst row stride in bytes = 2 * w * 4 */
    mov     w8, #0x3F3F
    movk    w8, #0x003F, lsl #16    /* w8 = 0x003F3F3F */
    dup     v3.4s, w8
.Lcrt_row:
    mov     x5, x0                  /* dst row 2*y */
    add     x6, x0, x4              /* dst row 2*y + 1 */
    mov     x7, x2
.Lcrt4:
    cmp     x7, #4
    b.lo    .Lcrt1
    ldr     q0, [x1], #16
    zip1    v1.4s, v0.4s, v0.4s
    zip2    v2.4s, v0.4s, v0.4s
    stp     q1, q2, [x5], #32
    ushr    v4.4s, v0.4s, #2
    and     v4.16b, v4.16b, v3.16b
    sub     v4.4s, v0.4s, v4.4s
    zip1    v5.4s, v4.4s, v4.4s
    zip2    v6.4s, v4.4s, v4.4s
    stp     q5, q6, [x6], #32
    sub     x7, x7, #4
    b       .Lcrt4
.Lcrt1:
    cbz     x7, .Lcrt_next
    ldr     w9, [x1], #4
    lsr     w10, w9, #2
    and     w10, w10, w8
    sub     w10, w9, w10
    bfi     x9, x9, #32, #32
    bfi     x10, x10, #32, #32
    str     x9, [x5], #8
    str     x10, [x6], #8
    sub     x7, x7, #1
    b       .Lcrt1
.Lcrt_next:
    add     x0, x0, x4, lsl #1
    subs    x3, x3, #1
    b.ne    .Lcrt_row
.Lcrt_done:
    ret
#if !defined(__APPLE__)
    .size   ps15_upscale2x_crt, .-ps15_upscale2x_crt
#endif

/* =========================================================================
 * void ps15_span_tex64(const Span64 *s=x0)
 * Register-resident PS1.5 textured Gouraud RGB555 Bayer-dithered span loop.
 * ========================================================================= */
    .globl SYM(ps15_span_tex64)
#if !defined(__APPLE__)
    .type  ps15_span_tex64, %function
#endif
SYM(ps15_span_tex64):
    ldp     w4, w5, [x0, #24]       /* w4 = x, w5 = count */
    cmp     w5, #0
    b.le    .Lsp_ret

    sub     sp, sp, #64
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]

    ldp     x1, x2, [x0, #0]        /* x1 = row_px, x2 = row_zb */
    ldr     x3, [x0, #16]           /* x3 = tex */
    add     x1, x1, w4, sxtw #2     /* px = row_px + x */
    add     x2, x2, w4, sxtw #2     /* zb = row_zb + x */

    ldp     s0, s1, [x0, #32]       /* s0 = invz, s1 = dinvz */
    ldp     w6, w7, [x0, #40]       /* w6 = u, w7 = du */
    ldp     w8, w9, [x0, #48]       /* w8 = v, w9 = dv */
    ldp     w10, w11, [x0, #56]     /* w10 = r, w11 = dr */
    ldp     w12, w13, [x0, #64]     /* w12 = g, w13 = dg */
    ldp     w14, w15, [x0, #72]     /* w14 = b, w15 = db */
    ldp     w19, w20, [x0, #80]     /* w19 = inv_fog, w20 = fogr */
    ldp     w21, w22, [x0, #88]     /* w21 = fogg, w22 = fogb */
    add     x26, x0, #96            /* x26 = &s->dither[0] */
    mov     w24, #255               /* constant 255 for clamp */

.Lsp_loop:
    ldr     s2, [x2]
    fcmp    s0, s2
    b.le    .Lsp_step

    ubfx    w16, w6, #16, #6        /* tx = (u >> 16) & 63 */
    ubfx    w17, w8, #16, #6        /* ty = (v >> 16) & 63 */
    orr     w16, w16, w17, lsl #6   /* idx = (ty << 6) | tx */
    ldr     w16, [x3, x16, lsl #2]  /* t = tex[idx] */
    tbnz    w16, #31, .Lsp_step     /* PS1 color-key mask bit */

    str     s0, [x2]                /* *zb = invz */
    and     w17, w4, #3
    ldr     w25, [x26, x17, lsl #2] /* d = s->dither[x & 3] */

    /* Red channel */
    asr     w23, w10, #16
    cmp     w23, #0
    csel    w23, wzr, w23, lt
    ubfx    w0, w16, #16, #8
    mul     w23, w0, w23
    asr     w23, w23, #7
    cmp     w23, #255
    csel    w23, w24, w23, gt
    madd    w23, w23, w19, w20
    asr     w23, w23, #8
    adds    w23, w23, w25
    csel    w23, wzr, w23, lt
    cmp     w23, #255
    csel    w23, w24, w23, gt
    and     w23, w23, #0xF8

    /* Green channel */
    asr     w17, w12, #16
    cmp     w17, #0
    csel    w17, wzr, w17, lt
    ubfx    w0, w16, #8, #8
    mul     w17, w0, w17
    asr     w17, w17, #7
    cmp     w17, #255
    csel    w17, w24, w17, gt
    madd    w17, w17, w19, w21
    asr     w17, w17, #8
    adds    w17, w17, w25
    csel    w17, wzr, w17, lt
    cmp     w17, #255
    csel    w17, w24, w17, gt
    and     w17, w17, #0xF8

    /* Blue channel */
    asr     w0, w14, #16
    cmp     w0, #0
    csel    w0, wzr, w0, lt
    and     w16, w16, #0xFF
    mul     w0, w16, w0
    asr     w0, w0, #7
    cmp     w0, #255
    csel    w0, w24, w0, gt
    madd    w0, w0, w19, w22
    asr     w0, w0, #8
    adds    w0, w0, w25
    csel    w0, wzr, w0, lt
    cmp     w0, #255
    csel    w0, w24, w0, gt
    and     w0, w0, #0xF8

    orr     w0, w0, w17, lsl #8
    orr     w0, w0, w23, lsl #16
    str     w0, [x1]

.Lsp_step:
    fadd    s0, s0, s1
    add     w6, w6, w7
    add     w8, w8, w9
    add     w10, w10, w11
    add     w12, w12, w13
    add     w14, w14, w15
    add     x1, x1, #4
    add     x2, x2, #4
    add     w4, w4, #1
    subs    w5, w5, #1
    b.gt    .Lsp_loop

    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    add     sp, sp, #64
.Lsp_ret:
    ret
#if !defined(__APPLE__)
    .size   ps15_span_tex64, .-ps15_span_tex64
#endif

/* =========================================================================
 * void ps15_span_abr64(const Span64 *s=x0, int abr_mode=w1)
 * Read-only 1/z test + PS1 ABR semi-transparency (1:HALF, 2:ADD, 3:SUB, 4:QUARTER)
 * + Bayer RGB555 quantization. Never touches x18.
 * ========================================================================= */
    .globl SYM(ps15_span_abr64)
#if !defined(__APPLE__)
    .type  ps15_span_abr64, %function
#endif
SYM(ps15_span_abr64):
    ldp     w4, w5, [x0, #24]       /* w4 = x, w5 = count */
    cmp     w5, #0
    b.le    .Labr_ret

    sub     sp, sp, #80
    stp     x19, x20, [sp, #0]
    stp     x21, x22, [sp, #16]
    stp     x23, x24, [sp, #32]
    stp     x25, x26, [sp, #48]
    stp     x27, x28, [sp, #64]

    mov     w27, w1                 /* w27 = abr_mode */
    ldp     x1, x2, [x0, #0]        /* x1 = row_px, x2 = row_zb */
    ldr     x3, [x0, #16]           /* x3 = tex */
    add     x1, x1, w4, sxtw #2
    add     x2, x2, w4, sxtw #2

    ldp     s0, s1, [x0, #32]       /* s0 = invz, s1 = dinvz */
    ldp     w6, w7, [x0, #40]       /* w6 = u, w7 = du */
    ldp     w8, w9, [x0, #48]       /* w8 = v, w9 = dv */
    ldp     w10, w11, [x0, #56]     /* w10 = r, w11 = dr */
    ldp     w12, w13, [x0, #64]     /* w12 = g, w13 = dg */
    ldp     w14, w15, [x0, #72]     /* w14 = b, w15 = db */
    ldp     w19, w20, [x0, #80]     /* w19 = inv_fog, w20 = fogr */
    ldp     w21, w22, [x0, #88]     /* w21 = fogg, w22 = fogb */
    add     x26, x0, #96            /* x26 = &s->dither[0] */
    mov     w24, #255

.Labr_loop:
    ldr     s2, [x2]
    fcmp    s0, s2
    b.le    .Labr_step

    ubfx    w16, w6, #16, #6
    ubfx    w17, w8, #16, #6
    orr     w16, w16, w17, lsl #6
    ldr     w16, [x3, x16, lsl #2]
    tbnz    w16, #31, .Labr_step    /* TEX_MASK_BIT skip (no zb write!) */

    and     w17, w4, #3
    ldr     w25, [x26, x17, lsl #2] /* d = s->dither[x & 3] */

    /* Foreground fogged R -> w23 */
    asr     w23, w10, #16
    cmp     w23, #0
    csel    w23, wzr, w23, lt
    ubfx    w0, w16, #16, #8
    mul     w23, w0, w23
    asr     w23, w23, #7
    cmp     w23, #255
    csel    w23, w24, w23, gt
    madd    w23, w23, w19, w20
    asr     w23, w23, #8

    /* Foreground fogged G -> w17 */
    asr     w17, w12, #16
    cmp     w17, #0
    csel    w17, wzr, w17, lt
    ubfx    w0, w16, #8, #8
    mul     w17, w0, w17
    asr     w17, w17, #7
    cmp     w17, #255
    csel    w17, w24, w17, gt
    madd    w17, w17, w19, w21
    asr     w17, w17, #8

    /* Foreground fogged B -> w0 */
    asr     w0, w14, #16
    cmp     w0, #0
    csel    w0, wzr, w0, lt
    and     w16, w16, #0xFF
    mul     w0, w16, w0
    asr     w0, w0, #7
    cmp     w0, #255
    csel    w0, w24, w0, gt
    madd    w0, w0, w19, w22
    asr     w0, w0, #8

    /* Load background pixel bg = *px into w28 */
    ldr     w28, [x1]
    cmp     w27, #1
    b.eq    .Labr_half
    cmp     w27, #2
    b.eq    .Labr_add
    cmp     w27, #3
    b.eq    .Labr_sub

    /* Default / 4: BLEND_QUARTER (B + (F >> 2)) */
    ubfx    w16, w28, #16, #8
    add     w23, w16, w23, asr #2
    ubfx    w16, w28, #8, #8
    add     w17, w16, w17, asr #2
    and     w16, w28, #0xFF
    add     w0, w16, w0, asr #2
    b       .Labr_quant

.Labr_half:
    ubfx    w16, w28, #16, #8
    add     w23, w16, w23
    asr     w23, w23, #1
    ubfx    w16, w28, #8, #8
    add     w17, w16, w17
    asr     w17, w17, #1
    and     w16, w28, #0xFF
    add     w0, w16, w0
    asr     w0, w0, #1
    b       .Labr_quant

.Labr_add:
    ubfx    w16, w28, #16, #8
    add     w23, w16, w23
    ubfx    w16, w28, #8, #8
    add     w17, w16, w17
    and     w16, w28, #0xFF
    add     w0, w16, w0
    b       .Labr_quant

.Labr_sub:
    ubfx    w16, w28, #16, #8
    sub     w23, w16, w23
    ubfx    w16, w28, #8, #8
    sub     w17, w16, w17
    and     w16, w28, #0xFF
    sub     w0, w16, w0

.Labr_quant:
    adds    w23, w23, w25
    csel    w23, wzr, w23, lt
    cmp     w23, #255
    csel    w23, w24, w23, gt
    and     w23, w23, #0xF8

    adds    w17, w17, w25
    csel    w17, wzr, w17, lt
    cmp     w17, #255
    csel    w17, w24, w17, gt
    and     w17, w17, #0xF8

    adds    w0, w0, w25
    csel    w0, wzr, w0, lt
    cmp     w0, #255
    csel    w0, w24, w0, gt
    and     w0, w0, #0xF8

    orr     w0, w0, w17, lsl #8
    orr     w0, w0, w23, lsl #16
    str     w0, [x1]

.Labr_step:
    fadd    s0, s0, s1
    add     w6, w6, w7
    add     w8, w8, w9
    add     w10, w10, w11
    add     w12, w12, w13
    add     w14, w14, w15
    add     x1, x1, #4
    add     x2, x2, #4
    add     w4, w4, #1
    subs    w5, w5, #1
    b.gt    .Labr_loop

    ldp     x19, x20, [sp, #0]
    ldp     x21, x22, [sp, #16]
    ldp     x23, x24, [sp, #32]
    ldp     x25, x26, [sp, #48]
    ldp     x27, x28, [sp, #64]
    add     sp, sp, #80
.Labr_ret:
    ret
#if !defined(__APPLE__)
    .size   ps15_span_abr64, .-ps15_span_abr64
    .section .note.GNU-stack,"",%progbits
#endif
