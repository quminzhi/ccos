#include <stdint.h>

typedef struct {
  volatile uint32_t locked;  // 0 = unlocked, 1 = locked
} spinlock_t;

static inline void spin_lock(spinlock_t *lk)
{
  uint32_t tmp;
  do {
    __asm__ volatile("amoswap.w.aq %0, %2, %1\n"
                     : "=r"(tmp), "+A"(lk->locked)
                     : "r"(1)
                     : "memory");
  } while (tmp != 0);
}

static inline void spin_unlock(spinlock_t *lk)
{
  __asm__ volatile("amoswap.w.rl x0, x0, %0\n" : "+A"(lk->locked) : : "memory");
}

spinlock_t g_kernel_lock = {0};

static inline void kernel_lock(void) { spin_lock(&g_kernel_lock); }
static inline void kernel_unlock(void) { spin_unlock(&g_kernel_lock); }
