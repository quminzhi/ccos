// kernel/include/kernel_lock.h
#pragma once
#include "spinlock.h"

extern spinlock_t g_kernel_lock;

static inline reg_t kernel_lock(void) {
  return spin_lock_irqsave(&g_kernel_lock);
}

static inline void kernel_unlock(reg_t sstatus) {
  spin_unlock_irqrestore(&g_kernel_lock, sstatus);
}

extern spinlock_t g_log_lock;

// // LOG_LOCK and LOG_UNLOCK hooks in log_config.h provided for log in lib
// #define LOG_LOCK()   spin_lock(&g_log_lock)
// #define LOG_UNLOCK() spin_unlock(&g_log_lock)
