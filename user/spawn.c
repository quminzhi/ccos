#include <stdint.h>
#include "ulib.h"     // u_printf/u_puts/sleep/u_atoi...
#include "syscall.h"

// ---- spawn config ----
#define SPAWN_MAX 16

typedef enum {
  SPAWN_MODE_SPIN  = 0,
  SPAWN_MODE_YIELD = 1,
  SPAWN_MODE_SLEEP = 2,
} spawn_mode_t;

typedef struct {
  int wid;
  int tid;

  spawn_mode_t mode;

  // 行为参数
  uint32_t work_loops;   // 每轮忙循环次数
  uint32_t sleep_ticks;  // mode==SLEEP时用
  uint32_t print_every;  // 每多少轮打印一次

  // 观测数据（线程自己维护）
  volatile int last_hart;        // 最近一次运行在哪个 hart
  volatile uint32_t migrations;  // 发生过几次 hart 变化
  volatile uint32_t prints;      // 打印次数
} spawn_cfg_t;

static spawn_cfg_t s_spawn_cfg[SPAWN_MAX];
static int s_spawn_count = 0;

// 小工具：把 "spawn0" 写到 buf（避免依赖 snprintf）
static void make_name(char* buf, int buf_len, const char* prefix, int n) {
  if (buf_len <= 0) return;
  int i = 0;
  while (prefix[i] && i < buf_len - 1) {
    buf[i] = prefix[i];
    i++;
  }
  if (i < buf_len - 1) {
    int d    = (n % 10);
    buf[i++] = (char)('0' + d);
  }
  if (i < buf_len) buf[i] = '\0';
}

// ---- worker ----
static __attribute__((noreturn)) void spawn_worker(void* arg) {
  spawn_cfg_t* c = (spawn_cfg_t*)arg;

  // 稍微错开启动，减少所有线程同时打印导致串口淹没
  sleep(1);

  uint32_t it = 0;

  for (;;) {
    // 1) busy work
    for (volatile uint32_t i = 0; i < c->work_loops; ++i) {
      __asm__ volatile("" ::: "memory");
    }

    // 2) optional yield/sleep
    if (c->mode == SPAWN_MODE_YIELD) {
      sleep(0);  // yield
    } else if (c->mode == SPAWN_MODE_SLEEP) {
      sleep(c->sleep_ticks);
    }

    // 3) observe hart + migrations
    int hart = get_hartid();
    int last = c->last_hart;
    if (last != -1 && last != hart) {
      c->migrations++;
    }
    c->last_hart = hart;

    // 4) print occasionally
    it++;
    if (c->print_every && (it % c->print_every) == 0) {
      c->prints++;
      // 一行打印：尽量减少字符级 interleave
      u_printf("[spawn] tid=%d wid=%d mode=%d hart=%d mig=%u prints=%u\n",
               c->tid, c->wid, (int)c->mode, hart, (unsigned)c->migrations,
               (unsigned)c->prints);
    }
  }
}

// ---- shell cmd ----
static void spawn_usage(void) {
  u_puts(
      "usage:\n"
      "  spawn spin  N [print_every]\n"
      "  spawn yield N [print_every]\n"
      "  spawn sleep N <sleep_ticks> [print_every]\n"
      "  spawn list\n"
      "  spawn kill\n"
      "notes:\n"
      "  - print_every is in 'iterations' (not ticks). default=50\n"
      "  - N is capped to SPAWN_MAX\n");
}

static int spawn_add(spawn_mode_t mode, uint32_t sleep_ticks,
                     uint32_t print_every, const char* name_prefix) {
  if (s_spawn_count >= SPAWN_MAX) return -1;

  int wid        = s_spawn_count;
  spawn_cfg_t* c = &s_spawn_cfg[wid];

  c->wid         = wid;
  c->tid         = -1;
  c->mode        = mode;
  c->sleep_ticks = sleep_ticks;
  c->print_every = (print_every == 0) ? 50 : print_every;

  // work_loops 你可以按性能调：太小会刷爆日志，太大迁移不明显
  c->work_loops  = 200000;

  c->last_hart   = -1;
  c->migrations  = 0;
  c->prints      = 0;

  static char names[SPAWN_MAX][8];
  make_name(names[wid], (int)sizeof(names[wid]), name_prefix, wid);

  tid_t tid = thread_create(spawn_worker, c, names[wid]);
  if (tid < 0) return -2;

  c->tid = (int)tid;
  s_spawn_count++;
  return c->tid;
}

void spawn(int argc, char** argv) {
  if (argc < 2) {
    spawn_usage();
    return;
  }

  const char* sub = argv[1];

  if (!u_strcmp(sub, "list")) {
    u_printf("spawned=%d\n", s_spawn_count);
    u_printf(" WID  TID  MODE  LAST_HART  MIGRATIONS  PRINTS\n");
    u_printf(" ---- ---- ----  ---------  ----------  ------\n");
    for (int i = 0; i < s_spawn_count; ++i) {
      spawn_cfg_t* c = &s_spawn_cfg[i];
      u_printf(" %-4d %-4d %-4d  %-9d  %-10u  %-6u\n", c->wid, c->tid,
               (int)c->mode, c->last_hart, (unsigned)c->migrations,
               (unsigned)c->prints);
    }
    return;
  }

  if (!u_strcmp(sub, "kill")) {
    // 复用你现有的 kill syscall wrapper（cmd_kill 用的那个）
    // 假设函数叫 thread_kill(tid)：
    int killed = 0;
    for (int i = 0; i < s_spawn_count; ++i) {
      int tid = s_spawn_cfg[i].tid;
      if (tid >= 0) {
        thread_kill(tid);  // <- 用你现有的接口名替换
        killed++;
      }
    }
    u_printf("spawn: kill requested for %d threads\n", killed);
    return;
  }

  // ---- spawn spin/yield/sleep ----
  if (argc < 3) {
    spawn_usage();
    return;
  }

  int n = u_atoi(argv[2]);
  if (n <= 0) n = 1;
  if (n > SPAWN_MAX) n = SPAWN_MAX;

  uint32_t print_every = 50;

  if (!u_strcmp(sub, "spin")) {
    if (argc >= 4) print_every = (uint32_t)u_atoi(argv[3]);

    for (int i = 0; i < n; ++i) {
      int tid = spawn_add(SPAWN_MODE_SPIN, 0, print_every, "sp");
      if (tid < 0) {
        u_puts("spawn: create failed\n");
        break;
      }
      u_printf("spawn: spin wid=%d tid=%d\n", s_spawn_count - 1, tid);
    }
    return;
  }

  if (!u_strcmp(sub, "yield")) {
    if (argc >= 4) print_every = (uint32_t)u_atoi(argv[3]);

    for (int i = 0; i < n; ++i) {
      int tid = spawn_add(SPAWN_MODE_YIELD, 0, print_every, "y");
      if (tid < 0) {
        u_puts("spawn: create failed\n");
        break;
      }
      u_printf("spawn: yield wid=%d tid=%d\n", s_spawn_count - 1, tid);
    }
    return;
  }

  if (!u_strcmp(sub, "sleep")) {
    if (argc < 4) {
      spawn_usage();
      return;
    }
    uint32_t sleep_ticks = (uint32_t)u_atoi(argv[3]);
    if (argc >= 5) print_every = (uint32_t)u_atoi(argv[4]);

    for (int i = 0; i < n; ++i) {
      int tid = spawn_add(SPAWN_MODE_SLEEP, sleep_ticks, print_every, "sl");
      if (tid < 0) {
        u_puts("spawn: create failed\n");
        break;
      }
      u_printf("spawn: sleep wid=%d tid=%d sleep=%u\n", s_spawn_count - 1, tid,
               (unsigned)sleep_ticks);
    }
    return;
  }

  spawn_usage();
}
