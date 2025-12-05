#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>
#include "platform.h"

/* 简单版 panic：打印 message + 文件名 + 行号，然后停在 S 模式 */
#define panic(msg)                                \
  do {                                            \
    platform_puts("\n!!! KERNEL PANIC !!!\n");    \
    platform_puts("  message: " msg "\n");        \
    platform_puts("  file: " __FILE__ "\n");      \
    platform_puts("  line: ");                    \
    platform_put_dec_us((uintptr_t)__LINE__);     \
    platform_puts("\n");                          \
    for (;;) {                                    \
      __asm__ volatile("wfi"); /* 省电地死循环 */ \
    }                                             \
  } while (0)

#endif /* PANIC_H */
