#ifndef CASM_ENGINE_H
#define CASM_ENGINE_H

#include <stdint.h>
#include <stddef.h>

/* Assembly hot loop (x86_64): fill count uint32 pixels with value.
 * C fallback is used on any other arch — same result, less speed. */
void ps15_fill32(uint32_t *dst, uint32_t value, size_t count);

/* Fixed point: 16.16 */
typedef int32_t fix16;
#define FIX_ONE 65536
static inline fix16 fix_mul(fix16 a, fix16 b) {
    return (fix16)(((int64_t)a * (int64_t)b) / FIX_ONE);
}

#endif
