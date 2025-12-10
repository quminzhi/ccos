// kernel/include/kernel_lock.h
#pragma once
#include "spinlock.h"

extern spinlock_t g_kernel_lock;

static inline void kernel_lock(void) {
  spin_lock(&g_kernel_lock);
}

static inline void kernel_unlock(void) {
  spin_unlock(&g_kernel_lock);
}

extern spinlock_t g_log_lock;

// // LOG_LOCK and LOG_UNLOCK hooks in log_config.h provided for log in lib
// #define LOG_LOCK()   spin_lock(&g_log_lock)
// #define LOG_UNLOCK() spin_unlock(&g_log_lock)
