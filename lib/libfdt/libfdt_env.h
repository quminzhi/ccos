#ifndef _LIBFDT_ENV_H
#define _LIBFDT_ENV_H

#include <stddef.h>  // Required for size_t; drop if your libc already provides it
#include <stdint.h>
#include <string.h>

/*
 * Endian helpers: FDT is big-endian; provide fdt16_to_cpu / fdt32_to_cpu /
 * fdt64_to_cpu and the reverse macros.
 *
 * Derived from Linux libfdt_env.h with minimal changes.
 */

#define EXTRACT_BYTE(n) ((unsigned long long)((uint8_t *)&x)[n])

static inline uint16_t fdt16_to_cpu(uint16_t x)
{
  return (EXTRACT_BYTE(0) << 8) | EXTRACT_BYTE(1);
}
#define cpu_to_fdt16(x) fdt16_to_cpu(x)

static inline uint32_t fdt32_to_cpu(uint32_t x)
{
  return (EXTRACT_BYTE(0) << 24) | (EXTRACT_BYTE(1) << 16) |
         (EXTRACT_BYTE(2) << 8) | (EXTRACT_BYTE(3) << 0);
}
#define cpu_to_fdt32(x) fdt32_to_cpu(x)

static inline uint64_t fdt64_to_cpu(uint64_t x)
{
  return ((uint64_t)EXTRACT_BYTE(0) << 56) | ((uint64_t)EXTRACT_BYTE(1) << 48) |
         ((uint64_t)EXTRACT_BYTE(2) << 40) | ((uint64_t)EXTRACT_BYTE(3) << 32) |
         ((uint64_t)EXTRACT_BYTE(4) << 24) | ((uint64_t)EXTRACT_BYTE(5) << 16) |
         ((uint64_t)EXTRACT_BYTE(6) << 8) | ((uint64_t)EXTRACT_BYTE(7) << 0);
}
#define cpu_to_fdt64(x) fdt64_to_cpu(x)

#undef EXTRACT_BYTE

/*
 * Memory/string helpers used by libfdt:
 *   memcmp, memcpy, memmove, memset, strlen
 * As long as string.h declares them and you implement them elsewhere, you
 * can use libfdt without stdlib.h or malloc/free.
 */

#endif /* _LIBFDT_ENV_H */
