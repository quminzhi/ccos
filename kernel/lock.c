#include "spinlock.h"

// 全局日志锁：只保护 log 库内部的输出与 ring buffer
spinlock_t g_log_lock = SPINLOCK_INIT;

spinlock_t g_kernel_lock = SPINLOCK_INIT;
