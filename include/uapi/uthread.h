// uthread.h （shared by user and kernel）

#pragma once
#include <stdint.h>
#include "types.h"

typedef void (*thread_entry_t)(void* arg) __attribute__((noreturn));
tid_t thread_current(void);

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
  int  tid;
  int  state;
  int  is_user;
  int  exit_code;
  char name[32];

  int  cpu;        // 当前运行在哪个 hart：-1 = not running
  int  last_hart;  // 最近一次运行在哪个 hart：-1 = never ran

  uint32_t migrations; // hart 迁移次数
  uint32_t _pad;       // 对齐用（保证 runs 8-byte 对齐）

  uint64_t runs;       // 被调度运行次数（每次成为 RUNNING +1）
};

/* Runqueue snapshot for a single hart. */
#define RQ_MAX_TIDS THREAD_MAX
struct rq_state {
  uint32_t hart;
  uint32_t len;
  tid_t    tids[RQ_MAX_TIDS]; /* -1 for unused slots */
};

static inline const char* thread_state_name(int s) __attribute__((unused));
static inline const char* thread_state_name(int s) {
  switch (s) {
    case THREAD_UNUSED:
      return "UNUSED";
    case THREAD_RUNNABLE:
      return "RUNNABLE";
    case THREAD_RUNNING:
      return "RUNNING";
    case THREAD_SLEEPING:
      return "SLEEP";
    case THREAD_WAITING:
      return "WAIT";
    case THREAD_ZOMBIE:
      return "ZOMBIE";
    case THREAD_BLOCKED:
      return "BLOCKED";
    default:
      return "?";
  }
}
