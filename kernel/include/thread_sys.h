// thread_sys.h （shared by user and kernel）

#pragma once
#include <stdint.h>

typedef int tid_t; /* 线程 ID = g_threads[] 的 index      */
typedef void (*thread_entry_t)(void *arg) __attribute__((noreturn));

typedef enum {
  THREAD_UNUSED   = 0,
  THREAD_RUNNABLE = 1,
  THREAD_RUNNING  = 2,
  THREAD_SLEEPING = 3,
  THREAD_WAITING  = 4, /* thread_join 中，等待其它线程 */
  THREAD_ZOMBIE   = 5, /* 已退出，等待 join 回收     */
  THREAD_BLOCKED  = 6, /* 通用阻塞：比如等 stdin 数据 */
} ThreadState;

/* exit_code 约定：类似信号风格的负数 */
enum {
  THREAD_EXITCODE_NORMAL  = 0,    // 正常退出（例如 thread_exit(0)）
  THREAD_EXITCODE_SIGTERM = -15,  // 类似 SIGTERM
  THREAD_EXITCODE_SIGKILL = -9,   // 类似 SIGKILL（当前 kill 默认用这个）
};

struct u_thread_info {
  int tid;
  int state;
  int is_user;  // 0=S, 1=U
  int exit_code;
  char name[16];  // 简单拷贝前 15 字符，结尾 '\0'
};
