#ifndef BAREMETAL_LOG_CONFIG_H
#define BAREMETAL_LOG_CONFIG_H

#include "spinlock.h"

#ifndef __ASSEMBLER__

/* Declare the global lock defined in log_lock.c. */
extern spinlock_t g_log_lock;

/* LOG_LOCK and LOG_UNLOCK hooks. */
#define LOG_LOCK()   spin_lock(&g_log_lock)
#define LOG_UNLOCK() spin_unlock(&g_log_lock)

#endif /* __ASSEMBLER__ */

#define LOG_COMPILE_LEVEL         LOG_LEVEL_DEBUG
#define LOG_RUNTIME_DEFAULT_LEVEL LOG_LEVEL_DEBUG
#define LOG_DEFAULT_PATH_MODE     LOG_PATH_BASENAME
#define LOG_ENABLE_TIMESTAMP      0

#endif  /* BAREMETAL_LOG_CONFIG_H */
