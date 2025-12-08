#ifndef _LIBFDT_ENV_H
#define _LIBFDT_ENV_H

#include <stddef.h>  // 如果你真的没有，也可以先去掉这一行
#include <stdint.h>
#include <string.h>

/*
 * endian 转换：FDT 是 big-endian 格式，这里给出
 * fdt16_to_cpu / fdt32_to_cpu / fdt64_to_cpu 以及反向宏。
 *
 * 这份实现基本来自 Linux 内核的 libfdt_env.h，只做了最小改动。
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
 * libfdt 会用到的内存/字符串操作函数：
 *   memcmp, memcpy, memmove, memset, strlen
 * 只要你的 string.h 里声明了它们，并且你在别处实现了，
 * 就可以直接用，不需要 stdlib.h 和 malloc/free。
 */

#endif /* _LIBFDT_ENV_H */
