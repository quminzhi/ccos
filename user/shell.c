#include "shell.h"
#include "ulib.h"
#include "syscall.h"
#include "thread_sys.h"
#include "time_sys.h"
#include "datetime.h"

/* -------------------------------------------------------------------------- */
/* 配置                                                                       */
/* -------------------------------------------------------------------------- */

#define SHELL_MAX_LINE  128
#define SHELL_MAX_ARGS  8
#define SHELL_MAX_PROCS 4

typedef struct ShellProc {
  int in_use;
  char line[SHELL_MAX_LINE];
} ShellProc;

static ShellProc g_procs[SHELL_MAX_PROCS];

static ShellProc* shell_proc_alloc(const char* line)
{
  for (int i = 0; i < SHELL_MAX_PROCS; ++i) {
    ShellProc* p = &g_procs[i];
    if (!p->in_use) {
      p->in_use = 1;

      // 安全拷贝命令行，确保以 '\0' 结尾
      int j = 0;
      while (line[j] && j < SHELL_MAX_LINE - 1) {
        p->line[j] = line[j];
        ++j;
      }
      p->line[j] = '\0';

      return p;
    }
  }

  return NULL; // 没空位
}

static void shell_proc_free(ShellProc* p)
{
  if (!p) return;
  p->in_use  = 0;
  p->line[0] = '\0';
}

/* -------------------------------------------------------------------------- */
/* 小工具函数                                                                 */
/* -------------------------------------------------------------------------- */

/* 简单十进制 atoi（支持可选的 '+'/'-' 前缀，不做错误检查） */
static int shell_atoi(const char* s)
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

#define SHELL_READ_OK   1  /* 正常读到一行，line 里有内容 */
#define SHELL_READ_EOF  0  /* EOF / 无内容 */
#define SHELL_READ_INTR -2 /* 被 Ctrl-C 中断 */
#define SHELL_READ_ERR  -1 /* 其他错误 */

static int shell_read_line(char* line, int line_size)
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

/* 把一行按空白拆成 argv[]，原地在 line 里插入 '\0' 作为分隔 */
static int shell_parse_line(char* line, char** argv, int max_args)
{
  int argc = 0;
  char* p  = line;

  /* 跳过前导空白 */
  while (*p == ' ' || *p == '\t') {
    p++;
  }

  while (*p != '\0' && argc < max_args) {
    argv[argc++] = p;

    /* 走到这个单词的结尾 */
    while (*p != '\0' && *p != ' ' && *p != '\t') {
      p++;
    }

    if (*p == '\0') {
      break;
    }

    /* 把空白变成 '\0'，分隔字符串 */
    *p++ = '\0';

    /* 跳过下一个单词前的空白 */
    while (*p == ' ' || *p == '\t') {
      p++;
    }
  }

  return argc;
}

/* -------------------------------------------------------------------------- */
/* 命令表定义                                                                 */
/* -------------------------------------------------------------------------- */

static const char* thread_state_name(int s)
{
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
    default:
      return "?";
  }
}

typedef void (*shell_cmd_fn)(int argc, char** argv);

typedef struct {
  const char* name;
  shell_cmd_fn fn;
  const char* help;
  int run_in_shell; // 1 = 在 shell 线程中直接执行（不 spawn 子线程）
  // 0 = 通过 sh-cmd 子线程执行（create + join）
} shell_cmd_t;

/* 前向声明命令处理函数 */
static void cmd_help(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_exit(int argc, char** argv);
static void cmd_sleep(int argc, char** argv);
static void cmd_ps(int argc, char** argv);
static void cmd_jobs(int argc, char** argv);
static void cmd_kill(int argc, char** argv);
static void cmd_date(int argc, char** argv);
static void cmd_uptime(int argc, char** argv);


/* 命令表 */
static const shell_cmd_t g_shell_cmds[] = {
    {"help", cmd_help, "show this help", 1},
    {"echo", cmd_echo, "echo arguments", 1}, // 也可以设成 0，看你喜好
    {"sleep", cmd_sleep, "sleep <ticks> (thread sleep)", 0},
    {"ps", cmd_ps, "list threads", 1},
    {"jobs", cmd_jobs, "list user threads", 1},
    {"kill", cmd_kill, "kill <tid>", 1},
    {"date", cmd_date, "date", 0},
    {"uptime", cmd_uptime, "uptime", 0},
    {"exit", cmd_exit, "exit shell", 1},
};

static const int g_shell_cmd_count =
    (int)(sizeof(g_shell_cmds) / sizeof(g_shell_cmds[0]));

/* 查找命令 */
static const shell_cmd_t* shell_find_cmd(const char* name)
{
  for (int i = 0; i < g_shell_cmd_count; ++i) {
    if (u_strcmp(name, g_shell_cmds[i].name) == 0) {
      return &g_shell_cmds[i];
    }
  }
  return NULL;
}

/* -------------------------------------------------------------------------- */
/* 命令实现                                                                   */
/* -------------------------------------------------------------------------- */

static void cmd_help(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  u_puts("available commands:");
  for (int i = 0; i < g_shell_cmd_count; ++i) {
    u_printf("  %-6s - %s\n", g_shell_cmds[i].name, g_shell_cmds[i].help);
  }
}

static void cmd_echo(int argc, char** argv)
{
  if (argc <= 1) {
    u_puts(""); /* 输出一个空行 */
    return;
  }

  for (int i = 1; i < argc; ++i) {
    u_printf("%s%s", argv[i], (i + 1 < argc) ? " " : "\n");
  }
}

static void cmd_sleep(int argc, char** argv)
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

static void cmd_exit(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  u_puts("shell exiting...");
  thread_exit(0); /* 不会返回 */
}

static void cmd_ps(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  struct u_thread_info infos[THREAD_MAX];
  int n = thread_list(infos, THREAD_MAX);
  if (n < 0) {
    u_printf("ps: thread_list failed, rc=%d\n", n);
    return;
  }

  u_printf(" TID  STATE     MODE  EXIT   NAME\n");
  u_printf(" ---- --------- ----  ----- ------------\n");

  for (int i = 0; i < n; ++i) {
    const struct u_thread_info* ti = &infos[i];
    const char* st                 = thread_state_name(ti->state);
    char mode                      = ti->is_user ? 'U' : 'S';

    u_printf(" %-4d %-9s  %c   %5d %s\n", ti->tid, st, mode, ti->exit_code,
             ti->name);
  }
}

static void cmd_jobs(int argc, char** argv)
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
      continue; // 只关心 U 模式线程
    }
    // 你也可以在这里再过滤掉 shell 自己 / user_main 等
    u_printf(" %-4d %-9s %s\n", ti->tid, thread_state_name(ti->state),
             ti->name);
  }
}

static void cmd_kill(int argc, char** argv)
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
    // 正常自杀：走已经验证过的 thread_exit 路径
    thread_exit(THREAD_EXITCODE_SIGKILL);
    // 不会返回
  }

  int rc = thread_kill((tid_t)tid);
  if (rc < 0) {
    u_printf("kill: failed to kill tid=%d, rc=%d\n", tid, rc);
  } else {
    u_printf("kill: sent SIGKILL to tid=%d\n", tid);
  }
}

static void cmd_date(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  struct timespec ts;
  int ret = clock_gettime(CLOCK_REALTIME, &ts);
  if (ret < 0) {
    u_printf("date: clock_gettime failed (%d)\n", ret);
  }

  datetime_t dt;
  // epoch_to_utc_datetime(ts.tv_sec, &dt);

  epoch_to_utc_datetime(ts.tv_sec, &dt);

  u_printf("%04d-%02d-%02d %02d:%02d:%02d\n",
         dt.year, dt.month, dt.day,
         dt.hour, dt.min, dt.sec);
}

static void cmd_uptime(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    u_printf("uptime: clock_gettime failed\n");
  }

  u_printf("uptime: %llu.%09u seconds since kernel boot\n",
           (unsigned long long)ts.tv_sec,
           (unsigned)ts.tv_nsec);
}


/* -------------------------------------------------------------------------- */
/* shell 主循环                                                               */
/* -------------------------------------------------------------------------- */

static void __attribute__((noreturn)) shell_cmd_worker(void* arg)
{
  ShellProc* proc = (ShellProc*)arg;

  /* 1. 拷一份到自己的栈上，避免直接在全局 buffer 上原地改 */
  char line[SHELL_MAX_LINE];
  {
    int i = 0;
    while (proc->line[i] && i < SHELL_MAX_LINE - 1) {
      line[i] = proc->line[i];
      ++i;
    }
    line[i] = '\0';
  }

  /* 这个 “进程” 不再需要，释放 slot */
  shell_proc_free(proc);

  /* 2. 解析成 argc/argv */
  char* argv[SHELL_MAX_ARGS];
  int argc = shell_parse_line(line, argv, SHELL_MAX_ARGS);
  if (argc == 0) {
    thread_exit(0);
  }

  /* 3. 找到命令实现 */
  const shell_cmd_t* cmd = shell_find_cmd(argv[0]);
  if (!cmd) {
    u_printf("unknown command: %s\n", argv[0]);
    thread_exit(-1);
  }

  /* 4. 真正执行命令（命令函数返回 = 程序退出） */
  cmd->fn(argc, argv);

  /* 5. 正常退出 */
  thread_exit(0);
  __builtin_unreachable();
}

/*
 * 在前台运行一条命令：
 *   - line: 原始命令行（不含换行）
 * 返回：
 *   - >=0: 子线程的 exit_code
 *   - < 0: 表示创建/等待过程中出错（不是命令返回值）
 */
static int shell_run_command(const char* line)
{
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

  // 这里你可以选择打印退出码（调试时很有用）
  // u_printf("[cmd exit] tid=%d, status=%d\n", tid, status);

  return status; // 类似 waitpid 拿到的 WEXITSTATUS
}

static void shell_dispatch_line(char* line)
{
  /* 先解析成 argv，用来识别 builtin */
  char raw_line[SHELL_MAX_LINE];

  int i = 0;
  while (line[i] && i < SHELL_MAX_LINE - 1) {
    raw_line[i] = line[i];
    ++i;
  }
  raw_line[i] = '\0';

  /* 在 line 上原地解析 argv（可以安全插 '\0'）*/
  char* argv[SHELL_MAX_ARGS];
  int argc = shell_parse_line(line, argv, SHELL_MAX_ARGS);
  if (argc == 0) {
    return; // 空行
  }

  const shell_cmd_t* cmd = shell_find_cmd(argv[0]);
  if (!cmd) {
    u_printf("unknown command: %s\n", argv[0]);
    return;
  }

  /* 必须在 shell 线程执行的命令（包括 exit / ps / jobs / kill 等） */
  if (cmd->run_in_shell) {
    cmd->fn(argc, argv);
    return;
  }

  /* 其他命令全部扔给子线程执行 */
  int status = shell_run_command(raw_line);
  (void)status;
  // 有需要的话可以在这里根据 status 做额外处理/打印
}

static void shell_main_loop(void)
{
  char line[SHELL_MAX_LINE];

  u_puts("tiny shell started. type 'help' for commands.");

  for (;;) {
    u_printf("> ");

    int r = shell_read_line(line, sizeof(line));
    if (r == SHELL_READ_INTR) {
      // Ctrl-C：终止当前行，重新给提示符
      continue;
    }
    if (r == SHELL_READ_ERR) {
      u_puts("shell: read error");
      continue;
    }
    if (r == SHELL_READ_EOF) {
      // 可以选择退出 shell 或简单忽略
      // 这里简单忽略
      continue;
    }

    shell_dispatch_line(line);
  }
}

/* -------------------------------------------------------------------------- */
/* 对外 API                                                                   */
/* -------------------------------------------------------------------------- */

void shell_thread(void* arg)
{
  (void)arg;
  shell_main_loop();
  /* 理论上不会到这里，shell_exit 会调用 thread_exit() */
  thread_exit(0);
}

tid_t shell_start(void)
{
  /* 在用户态创建一个 shell 线程 */
  tid_t tid = thread_create(shell_thread, NULL, "shell");
  if (tid < 0) {
    u_puts("shell_start: failed to create shell thread");
  }
  return tid;
}
