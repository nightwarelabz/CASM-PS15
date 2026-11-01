/* CASM-PS15 x86_64 assembly hot loops (System V ABI, Linux ELF + macOS Mach-O):
 *   1. ps15_fill32        — ERMS rep stosl 32-bit buffer fill
 *   2. ps15_upscale2x     — SSE2 punpckldq/punpckhdq nearest-neighbor 2x upscaler
 *   3. ps15_upscale2x_crt — SSE2 2x upscaler + 75% Trinitron odd-row scanlines
 *   4. ps15_span_tex64    — Opaque textured Gouraud + fog + 1/z + Bayer RGB555
 *   5. ps15_span_abr64    — PS1 ABR semi-transparent (HALF/ADD/SUB/QUARTER) span
 *
 * Bit-for-bit identical to src/fill_arm64.S and the C fallbacks in raster.c. */

#if defined(__APPLE__)
#define SYM(name) _##name
#else
#define SYM(name) name
#endif

    .text
    .p2align 4

/* =========================================================================
 * void ps15_fill32(uint32_t *dst=%rdi, uint32_t value=%esi, size_t count=%rdx)
 * ========================================================================= */
    .globl SYM(ps15_fill32)
#if !defined(__APPLE__)
    .type  ps15_fill32, @function
#endif
SYM(ps15_fill32):
    movq    %rdx, %rcx
    movl    %esi, %eax
    cld
    rep     stosl
    ret
#if !defined(__APPLE__)
    .size   ps15_fill32, .-ps15_fill32
#endif

/* =========================================================================
 * void ps15_upscale2x(uint32_t *dst=%rdi, const uint32_t *src=%rsi, int w=%edx, int h=%ecx)
 * ========================================================================= */
    .globl SYM(ps15_upscale2x)
#if !defined(__APPLE__)
    .type  ps15_upscale2x, @function
#endif
SYM(ps15_upscale2x):
    testl   %edx, %edx
    jle     .Lup_done
    testl   %ecx, %ecx
    jle     .Lup_done
    movslq  %edx, %rdx
    movslq  %ecx, %rcx
    leaq    (,%rdx,8), %r8          /* dst row stride = 2 * w * 4 bytes */
.Lup_row:
    movq    %rdi, %r9               /* dst row 2*y */
    leaq    (%rdi,%r8), %r10        /* dst row 2*y + 1 */
    movq    %rdx, %r11              /* remaining pixels */
.Lup4:
    cmpq    $4, %r11
    jl      .Lup1
    movdqu  (%rsi), %xmm0
    movdqa  %xmm0, %xmm1
    punpckldq %xmm0, %xmm1          /* [p0, p0, p1, p1] */
    punpckhdq %xmm0, %xmm0          /* [p2, p2, p3, p3] */
    movdqu  %xmm1, (%r9)
    movdqu  %xmm0, 16(%r9)
    movdqu  %xmm1, (%r10)
    movdqu  %xmm0, 16(%r10)
    addq    $16, %rsi
    addq    $32, %r9
    addq    $32, %r10
    subq    $4, %r11
    jmp     .Lup4
.Lup1:
    testq   %r11, %r11
    jz      .Lup_next
    movl    (%rsi), %eax
    movl    %eax, (%r9)
    movl    %eax, 4(%r9)
    movl    %eax, (%r10)
    movl    %eax, 4(%r10)
    addq    $4, %rsi
    addq    $8, %r9
    addq    $8, %r10
    decq    %r11
    jmp     .Lup1
.Lup_next:
    leaq    (%rdi,%r8,2), %rdi
    decq    %rcx
    jnz     .Lup_row
.Lup_done:
    ret
#if !defined(__APPLE__)
    .size   ps15_upscale2x, .-ps15_upscale2x
#endif

/* =========================================================================
 * void ps15_upscale2x_crt(uint32_t *dst=%rdi, const uint32_t *src=%rsi, int w=%edx, int h=%ecx)
 * ========================================================================= */
    .globl SYM(ps15_upscale2x_crt)
#if !defined(__APPLE__)
    .type  ps15_upscale2x_crt, @function
#endif
SYM(ps15_upscale2x_crt):
    testl   %edx, %edx
    jle     .Lcrt_done
    testl   %ecx, %ecx
    jle     .Lcrt_done
    movslq  %edx, %rdx
    movslq  %ecx, %rcx
    leaq    (,%rdx,8), %r8
    movl    $0x003F3F3F, %eax
    movd    %eax, %xmm3
    pshufd  $0, %xmm3, %xmm3        /* broadcast 0x003F3F3F across 4 lanes */
.Lcrt_row:
    movq    %rdi, %r9
    leaq    (%rdi,%r8), %r10
    movq    %rdx, %r11
.Lcrt4:
    cmpq    $4, %r11
    jl      .Lcrt1
    movdqu  (%rsi), %xmm0
    movdqa  %xmm0, %xmm1
    movdqa  %xmm0, %xmm2
    punpckldq %xmm0, %xmm1
    punpckhdq %xmm0, %xmm2
    movdqu  %xmm1, (%r9)
    movdqu  %xmm2, 16(%r9)

    movdqa  %xmm0, %xmm4
    psrld   $2, %xmm4
    pand    %xmm3, %xmm4
    psubd   %xmm4, %xmm0            /* scan = p - ((p >> 2) & 0x003F3F3F) */
    movdqa  %xmm0, %xmm1
    punpckldq %xmm0, %xmm1
    punpckhdq %xmm0, %xmm0
    movdqu  %xmm1, (%r10)
    movdqu  %xmm0, 16(%r10)

    addq    $16, %rsi
    addq    $32, %r9
    addq    $32, %r10
    subq    $4, %r11
    jmp     .Lcrt4
.Lcrt1:
    testq   %r11, %r11
    jz      .Lcrt_next
    movl    (%rsi), %eax
    movl    %eax, (%r9)
    movl    %eax, 4(%r9)
    pushq   %rbx
    movl    %eax, %ebx
    shrl    $2, %ebx
    andl    $0x003F3F3F, %ebx
    subl    %ebx, %eax
    movl    %eax, (%r10)
    movl    %eax, 4(%r10)
    popq    %rbx
    addq    $4, %rsi
    addq    $8, %r9
    addq    $8, %r10
    decq    %r11
    jmp     .Lcrt1
.Lcrt_next:
    leaq    (%rdi,%r8,2), %rdi
    decq    %rcx
    jnz     .Lcrt_row
.Lcrt_done:
    ret
#if !defined(__APPLE__)
    .size   ps15_upscale2x_crt, .-ps15_upscale2x_crt
#endif

/* =========================================================================
 * void ps15_span_tex64(const Span64 *s=%rdi)
 * ========================================================================= */
    .globl SYM(ps15_span_tex64)
#if !defined(__APPLE__)
    .type  ps15_span_tex64, @function
#endif
SYM(ps15_span_tex64):
    movl    28(%rdi), %ecx          /* count */
    testl   %ecx, %ecx
    jle     .Lsp_ret

    pushq   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    movslq  24(%rdi), %r11          /* x */
    movq    0(%rdi), %r8            /* row_px */
    movq    8(%rdi), %r9            /* row_zb */
    movq    16(%rdi), %r10          /* tex */
    leaq    (%r8,%r11,4), %r8       /* px = row_px + x */
    leaq    (%r9,%r11,4), %r9       /* zb = row_zb + x */

    movss   32(%rdi), %xmm0         /* invz */
    movss   36(%rdi), %xmm1         /* dinvz */
    movl    40(%rdi), %r12d         /* u */
    movl    48(%rdi), %r13d         /* v */
    movl    56(%rdi), %r14d         /* r */
    movl    64(%rdi), %r15d         /* g */
    movl    72(%rdi), %ebp          /* b */

.Lsp_loop:
    ucomiss (%r9), %xmm0
    jbe     .Lsp_step

    movl    %r12d, %eax
    shrl    $16, %eax
    andl    $63, %eax
    movl    %r13d, %edx
    shrl    $16, %edx
    andl    $63, %edx
    shll    $6, %edx
    orl     %edx, %eax              /* idx = (ty << 6) | tx */
    movl    (%r10,%rax,4), %ebx     /* t = tex[idx] */
    testl   %ebx, %ebx
    js      .Lsp_step               /* bit 31 = TEX_MASK_BIT */

    movss   %xmm0, (%r9)            /* *zb = invz */
    movl    %r11d, %edx
    andl    $3, %edx
    movl    96(%rdi,%rdx,4), %esi   /* %esi = d = s->dither[x & 3] */

    /* Red channel -> %edx */
    movl    %r14d, %eax
    sarl    $16, %eax
    xorl    %edx, %edx
    testl   %eax, %eax
    cmovsl  %edx, %eax
    movl    %ebx, %edx
    shrl    $16, %edx
    andl    $255, %edx
    imull   %edx, %eax
    sarl    $7, %eax
    movl    $255, %edx
    cmpl    $255, %eax
    cmovgl  %edx, %eax
    imull   80(%rdi), %eax
    addl    84(%rdi), %eax
    sarl    $8, %eax
    addl    %esi, %eax
    movl    $0, %edx
    cmovsl  %edx, %eax
    movl    $255, %edx
    cmpl    $255, %eax
    cmovgl  %edx, %eax
    andl    $0xF8, %eax
    shll    $16, %eax
    movl    %eax, %edx              /* %edx accumulates RGB555 output */

    /* Green channel */
    movl    %r15d, %eax
    sarl    $16, %eax
    pushq   %rcx                    /* free %ecx as scratch constant */
    xorl    %ecx, %ecx
    testl   %eax, %eax
    cmovsl  %ecx, %eax
    movl    %ebx, %ecx
    shrl    $8, %ecx
    andl    $255, %ecx
    imull   %ecx, %eax
    sarl    $7, %eax
    movl    $255, %ecx
    cmpl    $255, %eax
    cmovgl  %ecx, %eax
    imull   80(%rdi), %eax
    addl    88(%rdi), %eax
    sarl    $8, %eax
    addl    %esi, %eax
    movl    $0, %ecx
    cmovsl  %ecx, %eax
    movl    $255, %ecx
    cmpl    $255, %eax
    cmovgl  %ecx, %eax
    andl    $0xF8, %eax
    shll    $8, %eax
    orl     %eax, %edx

    /* Blue channel */
    movl    %ebp, %eax
    sarl    $16, %eax
    xorl    %ecx, %ecx
    testl   %eax, %eax
    cmovsl  %ecx, %eax
    andl    $255, %ebx
    imull   %ebx, %eax
    sarl    $7, %eax
    movl    $255, %ecx
    cmpl    $255, %eax
    cmovgl  %ecx, %eax
    imull   80(%rdi), %eax
    addl    92(%rdi), %eax
    sarl    $8, %eax
    addl    %esi, %eax
    movl    $0, %ecx
    cmovsl  %ecx, %eax
    movl    $255, %ecx
    cmpl    $255, %eax
    cmovgl  %ecx, %eax
    andl    $0xF8, %eax
    orl     %eax, %edx
    popq    %rcx

    movl    %edx, (%r8)

.Lsp_step:
    addss   %xmm1, %xmm0
    addl    44(%rdi), %r12d
    addl    52(%rdi), %r13d
    addl    60(%rdi), %r14d
    addl    68(%rdi), %r15d
    addl    76(%rdi), %ebp
    addq    $4, %r8
    addq    $4, %r9
    incl    %r11d
    decl    %ecx
    jg      .Lsp_loop

    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
.Lsp_ret:
    ret
#if !defined(__APPLE__)
    .size   ps15_span_tex64, .-ps15_span_tex64
#endif

/* =========================================================================
 * void ps15_span_abr64(const Span64 *s=%rdi, int abr_mode=%esi)
 * ========================================================================= */
    .globl SYM(ps15_span_abr64)
#if !defined(__APPLE__)
    .type  ps15_span_abr64, @function
#endif
SYM(ps15_span_abr64):
    movl    28(%rdi), %ecx
    testl   %ecx, %ecx
    jle     .Labr_ret

    pushq   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    subq    $16, %rsp
    movl    %esi, 0(%rsp)           /* 0(%rsp) = abr_mode */
    movl    %ecx, 4(%rsp)           /* 4(%rsp) = remaining count */

    movslq  24(%rdi), %r11          /* x */
    movq    0(%rdi), %r8            /* row_px */
    movq    8(%rdi), %r9            /* row_zb */
    movq    16(%rdi), %r10          /* tex */
    leaq    (%r8,%r11,4), %r8
    leaq    (%r9,%r11,4), %r9

    movss   32(%rdi), %xmm0         /* invz */
    movss   36(%rdi), %xmm1         /* dinvz */
    movl    40(%rdi), %r12d         /* u */
    movl    48(%rdi), %r13d         /* v */
    movl    56(%rdi), %r14d         /* r */
    movl    64(%rdi), %r15d         /* g */
    movl    72(%rdi), %ebp          /* b */

.Labr_loop:
    ucomiss (%r9), %xmm0
    jbe     .Labr_step

    movl    %r12d, %eax
    shrl    $16, %eax
    andl    $63, %eax
    movl    %r13d, %edx
    shrl    $16, %edx
    andl    $63, %edx
    shll    $6, %edx
    orl     %edx, %eax
    movl    (%r10,%rax,4), %ebx     /* t = tex[idx] */
    testl   %ebx, %ebx
    js      .Labr_step              /* TEX_MASK_BIT skip (no zb write!) */

    movl    %r11d, %edx
    andl    $3, %edx
    movl    96(%rdi,%rdx,4), %edx
    movl    %edx, 8(%rsp)           /* 8(%rsp) = dither */

    /* Foreground fogged R -> %ecx */
    movl    %r14d, %eax
    sarl    $16, %eax
    xorl    %edx, %edx
    testl   %eax, %eax
    cmovsl  %edx, %eax
    movl    %ebx, %edx
    shrl    $16, %edx
    andl    $255, %edx
    imull   %edx, %eax
    sarl    $7, %eax
    movl    $255, %edx
    cmpl    $255, %eax
    cmovgl  %edx, %eax
    imull   80(%rdi), %eax
    addl    84(%rdi), %eax
    sarl    $8, %eax
    movl    %eax, %ecx

    /* Foreground fogged G -> %esi */
    movl    %r15d, %eax
    sarl    $16, %eax
    xorl    %edx, %edx
    testl   %eax, %eax
    cmovsl  %edx, %eax
    movl    %ebx, %edx
    shrl    $8, %edx
    andl    $255, %edx
    imull   %edx, %eax
    sarl    $7, %eax
    movl    $255, %edx
    cmpl    $255, %eax
    cmovgl  %edx, %eax
    imull   80(%rdi), %eax
    addl    88(%rdi), %eax
    sarl    $8, %eax
    movl    %eax, %esi

    /* Foreground fogged B -> %eax */
    movl    %ebp, %eax
    sarl    $16, %eax
    xorl    %edx, %edx
    testl   %eax, %eax
    cmovsl  %edx, %eax
    andl    $255, %ebx
    imull   %ebx, %eax
    sarl    $7, %eax
    movl    $255, %edx
    cmpl    $255, %eax
    cmovgl  %edx, %eax
    imull   80(%rdi), %eax
    addl    92(%rdi), %eax
    sarl    $8, %eax

    /* Load bg = (%r8) into %ebx, br into %edx */
    movl    (%r8), %ebx
    movl    %ebx, %edx
    shrl    $16, %edx
    andl    $255, %edx

    cmpl    $1, 0(%rsp)
    je      .Labr_x_half
    cmpl    $2, 0(%rsp)
    je      .Labr_x_add
    cmpl    $3, 0(%rsp)
    je      .Labr_x_sub

    /* Default / 4: BLEND_QUARTER */
    sarl    $2, %ecx
    addl    %edx, %ecx
    movl    %ebx, %edx
    shrl    $8, %edx
    andl    $255, %edx
    sarl    $2, %esi
    addl    %edx, %esi
    andl    $255, %ebx
    sarl    $2, %eax
    addl    %ebx, %eax
    jmp     .Labr_x_quant

.Labr_x_half:
    addl    %edx, %ecx
    sarl    $1, %ecx
    movl    %ebx, %edx
    shrl    $8, %edx
    andl    $255, %edx
    addl    %edx, %esi
    sarl    $1, %esi
    andl    $255, %ebx
    addl    %ebx, %eax
    sarl    $1, %eax
    jmp     .Labr_x_quant

.Labr_x_add:
    addl    %edx, %ecx
    movl    %ebx, %edx
    shrl    $8, %edx
    andl    $255, %edx
    addl    %edx, %esi
    andl    $255, %ebx
    addl    %ebx, %eax
    jmp     .Labr_x_quant

.Labr_x_sub:
    subl    %ecx, %edx
    movl    %edx, %ecx
    movl    %ebx, %edx
    shrl    $8, %edx
    andl    $255, %edx
    subl    %esi, %edx
    movl    %edx, %esi
    andl    $255, %ebx
    subl    %eax, %ebx
    movl    %ebx, %eax

.Labr_x_quant:
    movl    8(%rsp), %ebx           /* dither */
    addl    %ebx, %ecx
    movl    $0, %edx
    cmovsl  %edx, %ecx
    movl    $255, %edx
    cmpl    $255, %ecx
    cmovgl  %edx, %ecx
    andl    $0xF8, %ecx
    shll    $16, %ecx

    addl    %ebx, %esi
    movl    $0, %edx
    cmovsl  %edx, %esi
    movl    $255, %edx
    cmpl    $255, %esi
    cmovgl  %edx, %esi
    andl    $0xF8, %esi
    shll    $8, %esi
    orl     %esi, %ecx

    addl    %ebx, %eax
    movl    $0, %edx
    cmovsl  %edx, %eax
    movl    $255, %edx
    cmpl    $255, %eax
    cmovgl  %edx, %eax
    andl    $0xF8, %eax
    orl     %eax, %ecx

    movl    %ecx, (%r8)

.Labr_step:
    addss   %xmm1, %xmm0
    addl    44(%rdi), %r12d
    addl    52(%rdi), %r13d
    addl    60(%rdi), %r14d
    addl    68(%rdi), %r15d
    addl    76(%rdi), %ebp
    addq    $4, %r8
    addq    $4, %r9
    incl    %r11d
    decl    4(%rsp)
    jg      .Labr_loop

    addq    $16, %rsp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
.Labr_ret:
    ret
#if !defined(__APPLE__)
    .size   ps15_span_abr64, .-ps15_span_abr64
    .section .note.GNU-stack,"",@progbits
#endif
