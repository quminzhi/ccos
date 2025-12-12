#include "spinlock.h"

// Global log lock: protects log output and its ring buffer only.
spinlock_t g_log_lock = SPINLOCK_INIT;

spinlock_t g_kernel_lock = SPINLOCK_INIT;
