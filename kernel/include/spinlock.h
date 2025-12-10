// kernel/include/spinlock.h
#pragma once

#ifndef __ASSEMBLER__  // ← 只有 C/C++ 编译时才看到下面的内容

#include <stdint.h>

typedef struct {
  volatile uint32_t locked;  // 0 = unlocked, 1 = locked
} spinlock_t;

#define SPINLOCK_INIT {.locked = 0}

static inline void spinlock_init(spinlock_t *lk) {
  lk->locked = 0;
}

static inline void spin_lock(spinlock_t *lk) {
  uint32_t tmp;
  do {
    __asm__ volatile("amoswap.w.aq %0, %2, %1\n"
                     : "=r"(tmp), "+A"(lk->locked)
                     : "r"(1)
                     : "memory");
  } while (tmp != 0);
}

static inline void spin_unlock(spinlock_t *lk) {
  __asm__ volatile("amoswap.w.rl x0, x0, %0\n" : "+A"(lk->locked) : : "memory");
}

#endif /* !__ASSEMBLER__ */
