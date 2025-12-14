/* shell.c */

#include "datetime.h"
#include "monitor.h"
#include "shell.h"
#include "spawn.h"
#include "syscall.h"
#include "ulib.h"
#include "uthread.h"
#include "utime.h"

/* -------------------------------------------------------------------------- */
/* Configuration.                                                             */
/* -------------------------------------------------------------------------- */

#define SHELL_MAX_LINE  128
#define SHELL_MAX_ARGS  8
#define SHELL_MAX_PROCS 4

typedef struct ShellProc {
  int  in_use;
  char line[SHELL_MAX_LINE];
} ShellProc;

static ShellProc g_procs[SHELL_MAX_PROCS];
static struct irqstat_user g_irqstat_buf[IRQSTAT_MAX_IRQ];

static ShellProc*
shell_proc_alloc(const char* line)
{
  for (int i = 0; i < SHELL_MAX_PROCS; ++i) {
    ShellProc* p = &g_procs[i];
    if (!p->in_use) {
      p->in_use = 1;

      /* Copy the command line safely and ensure it is NUL-terminated. */
      int j     = 0;
      while (line[j] && j < SHELL_MAX_LINE - 1) {
        p->line[j] = line[j];
        ++j;
      }
      p->line[j] = '\0';

      return p;
    }
  }

  return NULL;  /* No available slot. */
}

static void
shell_proc_free(ShellProc* p)
{
  if (!p) return;
  p->in_use  = 0;
  p->line[0] = '\0';
}

/* -------------------------------------------------------------------------- */
/* Utility helpers.                                                           */
/* -------------------------------------------------------------------------- */

/* Lightweight decimal atoi with optional +/- prefix and no error checks. */
static int
shell_atoi(const char* s)
{
  int neg = 0;
  int val = 0;

  if (*s == '+') {
    s++;
  } else if (*s == '-') {
    neg = 1;
    s++;
  }

  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    s++;
  }

  return neg ? -val : val;
}

#define SHELL_READ_OK   1  /* Successfully read a full line. */
#define SHELL_READ_EOF  0  /* EOF or empty input. */
#define SHELL_READ_INTR -2 /* Interrupted by Ctrl-C. */
#define SHELL_READ_ERR  -1 /* Other errors. */

static int
shell_read_line(char* line, int line_size)
{
  int len = u_gets(line, line_size);
  if (len == U_GETS_INTR) {
    return SHELL_READ_INTR;
  }
  if (len < 0) {
    return SHELL_READ_ERR;
  }
  if (len == 0) {
    return SHELL_READ_EOF;
  }
  return SHELL_READ_OK;
}

/* Split a line by whitespace into argv[], inserting '\0' delimiters in-place. */
static int
shell_parse_line(char* line, char** argv, int max_args)
{
  int argc = 0;
  char* p  = line;

  /* Skip leading whitespace. */
  while (*p == ' ' || *p == '\t') {
    p++;
  }

  while (*p != '\0' && argc < max_args) {
    argv[argc++] = p;

    /* Find this token's end. */
    while (*p != '\0' && *p != ' ' && *p != '\t') {
      p++;
    }

    if (*p == '\0') {
      break;
    }

    /* Replace separating whitespace with '\0'. */
    *p++ = '\0';

    /* Skip whitespace before the next token. */
    while (*p == ' ' || *p == '\t') {
      p++;
    }
  }

  return argc;
}

/* -------------------------------------------------------------------------- */
/* Command table definitions.                                                 */
/* -------------------------------------------------------------------------- */

typedef void (*shell_cmd_fn)(int argc, char** argv);

typedef struct {
  const char* name;
  shell_cmd_fn fn;
  const char* help;
  int run_in_shell;  /* 1: run in the shell thread, 0: spawn sh-cmd worker. */
} shell_cmd_t;

/* Forward declarations for command handlers. */
static void cmd_help(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_exit(int argc, char** argv);
static void cmd_sleep(int argc, char** argv);
static void cmd_ps(int argc, char** argv);
static void cmd_jobs(int argc, char** argv);
static void cmd_kill(int argc, char** argv);
static void cmd_rq(int argc, char** argv);
static void cmd_date(int argc, char** argv);
static void cmd_uptime(int argc, char** argv);
static void cmd_irqstat(int argc, char** argv);
static void cmd_spawn(int argc, char** argv);
static void cmd_mon(int argc, char** argv);

/* Command table. */
static const shell_cmd_t g_shell_cmds[] = {
    {"help",    cmd_help,    "show this help",                                  1},
    {"echo",    cmd_echo,    "echo arguments",                                  1}, /* Could also run via sh-cmd if desired. */
    {"sleep",   cmd_sleep,   "sleep <ticks> (thread sleep)",                    0},
    {"ps",      cmd_ps,      "list threads",                                    1},
    {"jobs",    cmd_jobs,    "list user threads",                               1},
    {"kill",    cmd_kill,    "kill <tid>",                                      1},
    {"rq",      cmd_rq,      "show per-hart runqueues",                         1},
    {"date",    cmd_date,    "date",                                            0},
    {"uptime",  cmd_uptime,  "uptime",                                          0},
    {"irqstat", cmd_irqstat, "irqstat",                                         0},
    {"spawn",   cmd_spawn,   "spawn test threads (spin/yield/sleep/list/kill)", 1},
    {"mon",     cmd_mon,
     "monitor: mon once | mon start <ticks> [count] | mon stop <tid> | mon "
     "list",                                                                    0},

    {"exit",    cmd_exit,    "exit shell",                                      1},
};

static const int g_shell_cmd_count =
    (int)(sizeof(g_shell_cmds) / sizeof(g_shell_cmds[0]));

/* Command lookup. */
static const shell_cmd_t*
shell_find_cmd(const char* name)
{
  for (int i = 0; i < g_shell_cmd_count; ++i) {
    if (u_strcmp(name, g_shell_cmds[i].name) == 0) {
      return &g_shell_cmds[i];
    }
  }
  return NULL;
}

/* -------------------------------------------------------------------------- */
/* Command implementations.                                                   */
/* -------------------------------------------------------------------------- */

static void
cmd_help(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  u_puts("available commands:");
  for (int i = 0; i < g_shell_cmd_count; ++i) {
    u_printf("  %-6s - %s\n", g_shell_cmds[i].name, g_shell_cmds[i].help);
  }
}

static void
cmd_echo(int argc, char** argv)
{
  if (argc <= 1) {
    u_puts(""); /* Print an empty line. */
    return;
  }

  for (int i = 1; i < argc; ++i) {
    u_printf("%s%s", argv[i], (i + 1 < argc) ? " " : "\n");
  }
}

static void
cmd_sleep(int argc, char** argv)
{
  if (argc < 2) {
    u_puts("usage: sleep <ticks>");
    return;
  }

  int ticks = shell_atoi(argv[1]);
  if (ticks <= 0) {
    u_puts("invalid ticks");
    return;
  }

  u_printf("sleeping %d ticks...\n", ticks);
  sleep((uint64_t)ticks);
  u_puts("done.");
}

static void
cmd_exit(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  u_puts("shell exiting...");
  thread_exit(0); /* Does not return. */
}

static void
cmd_ps(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  struct u_thread_info infos[THREAD_MAX];
  int n = thread_list(infos, THREAD_MAX);
  if (n < 0) {
    u_printf("ps: thread_list failed, rc=%d\n", n);
    return;
  }

  u_printf(" TID  STATE     MODE CPU LAST   MIG      RUNS  NAME\n");
  u_printf(" ---- --------- ---- --- ---- ------ --------- ---------------\n");

  for (int i = 0; i < n; ++i) {
    const struct u_thread_info* ti = &infos[i];
    const char* st                 = thread_state_name(ti->state);
    char mode                      = ti->is_user ? 'U' : 'S';

    /* Print CPU/LAST, using --- for -1. */
    char cpu_s[4];
    char last_s[5];

    if (ti->cpu >= 0) {
      u_snprintf(cpu_s, sizeof(cpu_s), "%d", ti->cpu);
    } else {
      u_snprintf(cpu_s, sizeof(cpu_s), "---");
    }

    if (ti->last_hart >= 0) {
      u_snprintf(last_s, sizeof(last_s), "%d", ti->last_hart);
    } else {
      u_snprintf(last_s, sizeof(last_s), "---");
    }

    u_printf(" %-4d %-9s  %c   %-3s %-4s %6u %9llu %s\n", ti->tid, st, mode,
             cpu_s, last_s, (unsigned)ti->migrations,
             (unsigned long long)ti->runs, ti->name);
  }
}

static void
cmd_jobs(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  struct u_thread_info infos[THREAD_MAX];
  int n = thread_list(infos, THREAD_MAX);
  if (n < 0) {
    u_printf("jobs: sys_thread_list failed, rc=%d\n", n);
    return;
  }

  u_printf(" TID  STATE     NAME\n");
  u_printf(" ---- --------- ------------\n");

  for (int i = 0; i < n; ++i) {
    const struct u_thread_info* ti = &infos[i];
    if (!ti->is_user) {
      continue;  /* Only show user-mode threads. */
    }
    /* Additional filtering (shell/user_main) can be added here if desired. */
    u_printf(" %-4d %-9s %s\n", ti->tid, thread_state_name(ti->state),
             ti->name);
  }
}

static void
cmd_kill(int argc, char** argv)
{
  if (argc < 2) {
    u_puts("usage: kill <tid>");
    return;
  }

  int tid = shell_atoi(argv[1]);
  if (tid <= 0) {
    u_puts("kill: invalid tid");
    return;
  }

  tid_t self = thread_current();
  if (tid == self) {
    u_puts("kill: killing myself...");
    /* Self-terminate via the well-tested thread_exit path. */
    thread_exit(THREAD_EXITCODE_SIGKILL);
    /* No return. */
  }

  int rc = thread_kill((tid_t)tid);
  if (rc < 0) {
    u_printf("kill: failed to kill tid=%d, rc=%d\n", tid, rc);
  } else {
    u_printf("kill: sent SIGKILL to tid=%d\n", tid);
  }
}

static void
cmd_rq(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  struct rq_state states[MAX_HARTS];
  int n = runqueue_snapshot(states, MAX_HARTS);
  if (n < 0) {
    u_printf("rq: syscall failed, rc=%d\n", n);
    return;
  }

  u_printf("hart  len  queue\n");
  u_printf("---- ----  ------------------------------\n");

  for (int i = 0; i < n; ++i) {
    const struct rq_state* s = &states[i];
    u_printf("%-4u %-4u  ", (unsigned)s->hart, (unsigned)s->len);
    if (s->len == 0) {
      u_puts("<empty>");
      continue;
    }
    for (uint32_t k = 0; k < s->len; ++k) {
      u_printf("%d", (int)s->tids[k]);
      if (k + 1 < s->len) {
        u_printf(" -> ");
      }
    }
    u_printf("\n");
  }
}

static void
cmd_date(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  struct timespec ts;
  int ret = clock_gettime(CLOCK_REALTIME, &ts);
  if (ret < 0) {
    u_printf("date: clock_gettime failed (%d)\n", ret);
  }

  datetime_t dt;
  epoch_to_utc_datetime(ts.tv_sec, &dt);

  u_printf("%04d-%02d-%02d %02d:%02d:%02d\n", dt.year, dt.month, dt.day,
           dt.hour, dt.min, dt.sec);
}

static void
cmd_uptime(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    u_printf("uptime: clock_gettime failed\n");
  }

  u_printf("uptime: %llu.%09u seconds since kernel boot\n",
           (unsigned long long)ts.tv_sec, (unsigned)ts.tv_nsec);
}

static void
cmd_irqstat(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  long n = irq_get_stats(g_irqstat_buf, IRQSTAT_MAX_IRQ);
  if (n < 0) {
    u_printf("irqstat: syscall failed (%ld)\n", n);
    return;
  }

  u_printf(
      "irq  count            last_tick(ns)        max_delta(ns)       name\n");

  for (long i = 0; i < n; ++i) {
    if (g_irqstat_buf[i].count == 0) {
      continue;
    }

    const char* name = g_irqstat_buf[i].name[0] ? g_irqstat_buf[i].name : "-";

    u_printf("%3u  %10llu   0x%016llx   0x%016llx   %s\n",
             (unsigned)g_irqstat_buf[i].irq,
             (unsigned long long)g_irqstat_buf[i].count,
             (unsigned long long)g_irqstat_buf[i].last_tick,
             (unsigned long long)g_irqstat_buf[i].max_delta, name);
  }
}

static void
cmd_spawn(int argc, char** argv)
{
  spawn(argc, argv);
}

static void
cmd_mon(int argc, char** argv)
{
  if (argc <= 1) {
    u_puts(
        "usage:\n"
        "  mon once\n"
        "  mon start <period_ticks> [count]\n"
        "  mon stop <tid>\n"
        "  mon list\n");
    return;
  }

  if (!u_strcmp(argv[1], "once")) {
    mon_once();
    return;
  }

  if (!u_strcmp(argv[1], "list")) {
    mon_list();
    return;
  }

  if (!u_strcmp(argv[1], "start")) {
    if (argc < 3) {
      u_puts("mon start: missing period_ticks\n");
      return;
    }
    uint32_t period = (uint32_t)u_atoi(argv[2]);
    int32_t count   = -1;
    if (argc >= 4) count = (int32_t)u_atoi(argv[3]);

    tid_t tid = mon_start(period, count);
    if (tid < 0) {
      u_printf("mon start failed rc=%d\n", (int)tid);
    } else {
      u_printf("mon started: tid=%d period=%u count=%d\n", (int)tid,
               (unsigned)period, (int)count);
    }
    return;
  }

  if (!u_strcmp(argv[1], "stop")) {
    if (argc < 3) {
      u_puts("mon stop: missing tid\n");
      return;
    }
    tid_t tid = (tid_t)u_atoi(argv[2]);
    int rc    = mon_stop(tid);
    u_printf("mon stop: tid=%d rc=%d\n", (int)tid, rc);
    return;
  }

  u_puts("mon: unknown subcommand\n");
}

/* -------------------------------------------------------------------------- */
/* Shell main loop.                                                           */
/* -------------------------------------------------------------------------- */

static void __attribute__((noreturn)) shell_cmd_worker(void* arg) {
  ShellProc* proc = (ShellProc*)arg;

  /* 1. Copy to the local stack to avoid mutating the global buffer. */
  char line[SHELL_MAX_LINE];
  {
    int i = 0;
    while (proc->line[i] && i < SHELL_MAX_LINE - 1) {
      line[i] = proc->line[i];
      ++i;
    }
    line[i] = '\0';
  }

  /* This "process" is no longer needed; release the slot. */
  shell_proc_free(proc);

  /* 2. Parse into argc/argv. */
  char* argv[SHELL_MAX_ARGS];
  int argc = shell_parse_line(line, argv, SHELL_MAX_ARGS);
  if (argc == 0) {
    thread_exit(0);
  }

  /* 3. Locate the command implementation. */
  const shell_cmd_t* cmd = shell_find_cmd(argv[0]);
  if (!cmd) {
    u_printf("unknown command: %s\n", argv[0]);
    thread_exit(-1);
  }

  /* 4. Run the command; returning ends the worker thread. */
  cmd->fn(argc, argv);

  /* 5. Exit normally. */
  thread_exit(0);
  __builtin_unreachable();
}

/*
 * Run a command in the foreground.
 *   - line: original command line (without newline)
 * Return values:
 *   - >= 0: exit_code of the child thread
 *   - < 0: creation/join failed (not the command's exit code)
 */
static int shell_run_command(const char* line) {
  ShellProc* proc = shell_proc_alloc(line);
  if (!proc) {
    u_puts("shell: no free proc slot (too many concurrent commands)");
    return -1;
  }

  tid_t tid = thread_create(shell_cmd_worker, (void*)proc, "sh-cmd");
  if (tid < 0) {
    shell_proc_free(proc);
    u_puts("shell: failed to create command thread");
    return -2;
  }

  int status = 0;
  int rc     = thread_join(tid, &status);
  if (rc < 0) {
    u_printf("shell: thread_join failed, rc=%d\n", rc);
    return -3;
  }

  /* Optional: print the status for debugging. */
  /* u_printf("[cmd exit] tid=%d, status=%d\n", tid, status); */

  return status;  /* Similar to waitpid's WEXITSTATUS. */
}

static void shell_dispatch_line(char* line) {
  /* Copy argv for builtin detection. */
  char raw_line[SHELL_MAX_LINE];

  int i = 0;
  while (line[i] && i < SHELL_MAX_LINE - 1) {
    raw_line[i] = line[i];
    ++i;
  }
  raw_line[i] = '\0';

  /* Parse argv in-place on line (safe to insert '\0'). */
  char* argv[SHELL_MAX_ARGS];
  int argc = shell_parse_line(line, argv, SHELL_MAX_ARGS);
  if (argc == 0) {
    return;  /* Empty line. */
  }

  const shell_cmd_t* cmd = shell_find_cmd(argv[0]);
  if (!cmd) {
    u_printf("unknown command: %s\n", argv[0]);
    return;
  }

  /* Commands that must run inside the shell thread (exit/ps/jobs/kill/etc.). */
  if (cmd->run_in_shell) {
    cmd->fn(argc, argv);
    return;
  }

  /* Run all other commands via worker threads. */
  int status = shell_run_command(raw_line);
  (void)status;
  /* Handle status here if further processing is needed. */
}

static void shell_main_loop(void) {
  char line[SHELL_MAX_LINE];

  u_puts("tiny shell started. type 'help' for commands.");

  for (;;) {
    u_printf("> ");

    int r = shell_read_line(line, sizeof(line));
    if (r == SHELL_READ_INTR) {
      /* Ctrl-C: abort current line and reprint the prompt. */
      continue;
    }
    if (r == SHELL_READ_ERR) {
      u_puts("shell: read error");
      continue;
    }
    if (r == SHELL_READ_EOF) {
      /* EOF: ignore for now instead of exiting the shell. */
      continue;
    }

    shell_dispatch_line(line);
  }
}

/* -------------------------------------------------------------------------- */
/* Public API.                                                                */
/* -------------------------------------------------------------------------- */

void shell_thread(void* arg) {
  (void)arg;
  shell_main_loop();
  /* Should never reach here because shell_exit invokes thread_exit(). */
  thread_exit(0);
}

tid_t shell_start(void) {
  /* Create a shell thread in user mode. */
  tid_t tid = thread_create(shell_thread, NULL, "shell");
  if (tid < 0) {
    u_puts("shell_start: failed to create shell thread");
  }
  return tid;
}
