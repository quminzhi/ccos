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
static int s_spawn_active = 0;
static int s_spawn_inited = 0;

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

static void
spawn_init_once(void)
{
  if (s_spawn_inited) return;
  for (int i = 0; i < SPAWN_MAX; ++i) {
    s_spawn_cfg[i].wid = i;
    s_spawn_cfg[i].tid = -1;
  }
  s_spawn_active = 0;
  s_spawn_inited = 1;
}

static int
spawn_find_free_wid(void)
{
  for (int i = 0; i < SPAWN_MAX; ++i) {
    if (s_spawn_cfg[i].tid < 0) return i;
  }
  return -1;
}

static void
spawn_cfg_init(spawn_cfg_t* c, int wid, spawn_mode_t mode, uint32_t sleep_ticks,
               uint32_t print_every)
{
  c->wid         = wid;
  c->tid         = -1;
  c->mode        = mode;
  c->sleep_ticks = sleep_ticks;
  c->print_every = print_every;  /* 0 disables printing. */

  c->work_loops  = 200000;

  c->last_hart   = -1;
  c->migrations  = 0;
  c->prints      = 0;
}

static int
spawn_add(spawn_mode_t mode, uint32_t sleep_ticks, uint32_t print_every,
          const char* name_prefix)
{
  int wid = spawn_find_free_wid();
  if (wid < 0) return -1;

  spawn_cfg_t* c = &s_spawn_cfg[wid];
  spawn_cfg_init(c, wid, mode, sleep_ticks, print_every);

  static char names[SPAWN_MAX][8];
  make_name(names[wid], (int)sizeof(names[wid]), name_prefix, wid);

  tid_t tid = thread_create(spawn_worker, c, names[wid]);
  if (tid < 0) return -2;
  c->tid = (int)tid;
  s_spawn_active++;
  return c->tid;
}

void
spawn(int argc, char** argv)
{
  spawn_init_once();

  if (argc < 2) {
    spawn_usage();
    return;
  }

  const char* sub = argv[1];

  if (!u_strcmp(sub, "list")) {
    u_printf("spawned=%d\n", s_spawn_active);
    u_printf(" WID  TID  MODE  LAST_HART  MIGRATIONS  PRINTS\n");
    u_printf(" ---- ---- ----  ---------  ----------  ------\n");
    for (int i = 0; i < SPAWN_MAX; ++i) {
      spawn_cfg_t* c = &s_spawn_cfg[i];
      if (c->tid < 0) continue;
      u_printf(" %-4d %-4d %-4d  %-9d  %-10u  %-6u\n", c->wid, c->tid,
               (int)c->mode, c->last_hart, (unsigned)c->migrations,
               (unsigned)c->prints);
    }
    return;
  }

  if (!u_strcmp(sub, "kill")) {
    /*
     * Make kill deterministic:
     *  - do not detach spawn workers
     *  - kill + join each worker so its TID is actually recycled
     *  - clear bookkeeping so subsequent spawn/kill only targets new workers
     */
    int requested = 0;
    int joined    = 0;
    int errors    = 0;
    for (int i = 0; i < SPAWN_MAX; ++i) {
      spawn_cfg_t* c = &s_spawn_cfg[i];
      tid_t tid      = (tid_t)c->tid;
      if (tid < 0) continue;

      requested++;

      int rc = thread_kill(tid);
      if (rc < 0) {
        errors++;
      } else {
        int status = 0;
        rc         = thread_join(tid, &status);
        if (rc == 0 || rc == -3) {
          joined++;
        } else {
          errors++;
        }
      }

      /* Clear our bookkeeping so next spawn/kill only targets new workers. */
      c->tid = -1;
    }

    s_spawn_active = 0;
    u_printf("spawn: kill requested=%d joined=%d errors=%d\n", requested, joined, errors);
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
        u_printf("spawn: spin tid=%d\n", tid);
      }
    }
    return;
  }

  if (!u_strcmp(sub, "yield")) {
    uint32_t print_every = 0;
    if (argc >= 4) print_every = (uint32_t)u_atoi(argv[3]);

    for (int i = 0; i < n; ++i) {
      int tid = spawn_add(SPAWN_MODE_YIELD, 0, print_every, "y");
      if (tid < 0) {
        u_puts("spawn: create failed\n");
        break;
      }
      u_printf("spawn: yield tid=%d\n", tid);
    }
    return;
  }

  if (!u_strcmp(sub, "sleep")) {
    uint32_t print_every = 0;
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
      u_printf("spawn: sleep tid=%d sleep=%u\n", tid, (unsigned)sleep_ticks);
    }
    return;
  }

  spawn_usage();
}
