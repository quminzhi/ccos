#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>
#include "platform.h"

/* Simple panic: print message + file + line, then halt in S-mode */
#define panic(msg)                                \
  do {                                            \
    platform_puts("\n!!! KERNEL PANIC !!!\n");    \
    platform_puts("  message: " msg "\n");        \
    platform_puts("  file: " __FILE__ "\n");      \
    platform_puts("  line: ");                    \
    platform_put_dec_us((uintptr_t)__LINE__);     \
    platform_puts("\n");                          \
    for (;;) {                                    \
      __asm__ volatile("wfi"); /* Low-power halt loop */ \
    }                                             \
  } while (0)

#endif /* PANIC_H */
