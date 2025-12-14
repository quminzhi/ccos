/* spawn.c */

#include <stdint.h>

#include "syscall.h"
#include "ulib.h"     /* u_printf/u_puts/sleep/u_atoi... */

/* ---- spawn config ---- */
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

  /* Behavior knobs. */
  uint32_t work_loops;   /* Busy-loop iterations per round. */
  uint32_t sleep_ticks;  /* Used when mode == SPAWN_MODE_SLEEP. */
  uint32_t print_every;  /* Print once every N iterations. */

  /* Observed statistics, maintained by each thread. */
  volatile int      last_hart;   /* Last hart this thread ran on. */
  volatile uint32_t migrations;  /* Number of hart changes observed. */
  volatile uint32_t prints;      /* Number of log lines printed. */
} spawn_cfg_t;

static spawn_cfg_t s_spawn_cfg[SPAWN_MAX];
static int s_spawn_count = 0;

/* Small helper: write "spawn0" into buf without snprintf. */
static void
make_name(char* buf, int buf_len, const char* prefix, int n)
{
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

/* ---- worker ---- */
static __attribute__((noreturn)) void
spawn_worker(void* arg)
{
  spawn_cfg_t* c = (spawn_cfg_t*)arg;

  /* Slight stagger to avoid overwhelming the console at start. */
  sleep(1);

  uint32_t it = 0;

  for (;;) {
    /* 1) Busy work loop. */
    for (volatile uint32_t i = 0; i < c->work_loops; ++i) {
      __asm__ volatile("" ::: "memory");
    }

    /* 2) Optional yield/sleep. */
    if (c->mode == SPAWN_MODE_YIELD) {
      sleep(0);  /* Yield. */
    } else if (c->mode == SPAWN_MODE_SLEEP) {
      sleep(c->sleep_ticks);
    }

    /* 3) Track hart placement and migrations. */
    int hart = get_hartid();
    int last = c->last_hart;
    if (last != -1 && last != hart) {
      c->migrations++;
    }
    c->last_hart = hart;

    /* 4) Print occasionally. */
    it++;
    if (c->print_every && (it % c->print_every) == 0) {
      c->prints++;
      /* Keep each print on a single line to minimize interleave. */
      u_printf("[spawn] tid=%d wid=%d mode=%d hart=%d mig=%u prints=%u\n",
               c->tid, c->wid, (int)c->mode, hart, (unsigned)c->migrations,
               (unsigned)c->prints);
    }
  }
}

/* ---- shell cmd ---- */
static void
spawn_usage(void)
{
  u_puts(
      "usage:\n"
      "  spawn spin  N [print_every]\n"
      "  spawn yield N [print_every]\n"
      "  spawn sleep N <sleep_ticks> [print_every]\n"
      "  spawn list\n"
      "  spawn kill\n"
      "notes:\n"
      "  - print_every is in 'iterations' (not ticks)\n"
      "  - print_every=0 disables all worker logs; default spin=0, others=50\n"
      "  - N is capped to SPAWN_MAX\n");
}

static int
spawn_add(spawn_mode_t mode, uint32_t sleep_ticks, uint32_t print_every,
          const char* name_prefix)
{
  if (s_spawn_count >= SPAWN_MAX) return -1;

  int wid        = s_spawn_count;
  spawn_cfg_t* c = &s_spawn_cfg[wid];

  c->wid         = wid;
  c->tid         = -1;
  c->mode        = mode;
  c->sleep_ticks = sleep_ticks;
  c->print_every = print_every;  /* 0 disables printing. */

  /* Adjust work_loops depending on performance: too small spams logs, */
  /* too large hides migration behavior. */
  c->work_loops  = 200000;

  c->last_hart   = -1;
  c->migrations  = 0;
  c->prints      = 0;

  static char names[SPAWN_MAX][8];
  make_name(names[wid], (int)sizeof(names[wid]), name_prefix, wid);

  tid_t tid = thread_create(spawn_worker, c, names[wid]);
  if (tid < 0) return -2;
  /* Spawned workers are fire-and-forget: detach to auto-recycle. */
  thread_detach(tid);

  c->tid = (int)tid;
  s_spawn_count++;
  return c->tid;
}

void
spawn(int argc, char** argv)
{
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
    /* Reuse the existing kill syscall wrapper (same one cmd_kill uses). */
    /* Replace with your own helper if it has a different name. */
    int killed = 0;
    for (int i = 0; i < s_spawn_count; ++i) {
      int tid = s_spawn_cfg[i].tid;
      if (tid >= 0) {
        thread_kill(tid);  /* Replace with your own helper name if needed. */
        killed++;
      }
    }
    u_printf("spawn: kill requested for %d threads\n", killed);
    return;
  }

  /* ---- spawn spin/yield/sleep ---- */
  if (argc < 3) {
    spawn_usage();
    return;
  }

  int n = u_atoi(argv[2]);
  if (n <= 0) n = 1;
  if (n > SPAWN_MAX) n = SPAWN_MAX;

  if (!u_strcmp(sub, "spin")) {
    uint32_t print_every = 0;
    if (argc >= 4) print_every = (uint32_t)u_atoi(argv[3]);

    for (int i = 0; i < n; ++i) {
      int tid = spawn_add(SPAWN_MODE_SPIN, 0, print_every, "sp");
      if (tid < 0) {
        u_puts("spawn: create failed\n");
        break;
      }
      if (print_every) {
        u_printf("spawn: spin wid=%d tid=%d\n", s_spawn_count - 1, tid);
      }
    }
    return;
  }

  if (!u_strcmp(sub, "yield")) {
    uint32_t print_every = 50;
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
    uint32_t print_every = 50;
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
