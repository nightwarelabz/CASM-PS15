/* ps15_fill32(uint32_t *dst=x0, uint32_t value=w1, size_t count=x2)
 * AArch64 twin of the x86_64 fill: NEON stores, 8 pixels per iteration.
 * Mach-O (macOS) prefixes C symbols with an underscore; ELF does not. */
    .text
#if defined(__APPLE__)
    .globl _ps15_fill32
_ps15_fill32:
#else
    .globl ps15_fill32
    .type  ps15_fill32, %function
ps15_fill32:
#endif
    cbz     x2, .Ldone
    dup     v0.4s, w1
.L8:
    cmp     x2, #8
    b.lo    .Ltail
    stp     q0, q0, [x0], #32
    sub     x2, x2, #8
    b       .L8
.Ltail:
    cbz     x2, .Ldone
    str     w1, [x0], #4
    sub     x2, x2, #1
    b       .Ltail
.Ldone:
    ret
#if !defined(__APPLE__)
    .size   ps15_fill32, .-ps15_fill32
#endif
