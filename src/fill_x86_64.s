/* ps15_fill32(uint32_t *dst=rdi, uint32_t value=esi, size_t count=rdx)
 * The one assembly hot loop in the trial: framebuffer / depth clears.
 * rep stosl fills count dwords from eax. */
    .text
    .globl ps15_fill32
    .type  ps15_fill32, @function
ps15_fill32:
    movq    %rdx, %rcx        /* count -> rep counter */
    movl    %esi, %eax        /* fill value */
    cld
    rep     stosl
    ret
    .size ps15_fill32, .-ps15_fill32
    .section .note.GNU-stack,"",@progbits
