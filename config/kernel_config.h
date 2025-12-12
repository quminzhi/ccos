#ifndef BAREMETAL_KERNEL_CONFIG_H
#define BAREMETAL_KERNEL_CONFIG_H

#include "log_config.h"

#define KSTACK_SIZE 4096  /* Per-hart kernel stack size (4 KiB). */

#define KERNEL_STACK_SIZE 4096
#define THREAD_STACK_SIZE 4096
#define THREAD_MAX 64

#endif /* BAREMETAL_KERNEL_CONFIG_H */
