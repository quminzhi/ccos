#include "shell.h"
#include "ulib.h"
#include "syscall.h"
#include "thread.h"

/* -------------------------------------------------------------------------- */
/* 配置                                                                       */
/* -------------------------------------------------------------------------- */

#define SHELL_MAX_LINE 128
#define SHELL_MAX_ARGS 8

/* -------------------------------------------------------------------------- */
/* 小工具函数                                                                 */
/* -------------------------------------------------------------------------- */

/* 简单十进制 atoi（支持可选的 '+'/'-' 前缀，不做错误检查） */
static int shell_atoi(const char *s)
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

/* 把一行按空白拆成 argv[]，原地在 line 里插入 '\0' 作为分隔 */
static int shell_parse_line(char *line, char **argv, int max_args)
{
  int argc = 0;
  char *p  = line;

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

typedef void (*shell_cmd_fn)(int argc, char **argv);

typedef struct {
  const char *name;
  shell_cmd_fn fn;
  const char *help;
} shell_cmd_t;

/* 前向声明命令处理函数 */
static void cmd_help(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_exit(int argc, char **argv);
static void cmd_sleep(int argc, char **argv);

/* 命令表 */
static const shell_cmd_t g_shell_cmds[] = {
    {"help",  cmd_help,  "show this help"              },
    {"echo",  cmd_echo,  "echo arguments"              },
    {"sleep", cmd_sleep, "sleep <ticks> (thread sleep)"},
    {"exit",  cmd_exit,  "exit shell"                  },
};

static const int g_shell_cmd_count =
    (int)(sizeof(g_shell_cmds) / sizeof(g_shell_cmds[0]));

/* 查找命令 */
static const shell_cmd_t *shell_find_cmd(const char *name)
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

static void cmd_help(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  u_puts("available commands:");
  for (int i = 0; i < g_shell_cmd_count; ++i) {
    u_printf("  %-6s - %s\n", g_shell_cmds[i].name, g_shell_cmds[i].help);
  }
}

static void cmd_echo(int argc, char **argv)
{
  if (argc <= 1) {
    u_puts(""); /* 输出一个空行 */
    return;
  }

  for (int i = 1; i < argc; ++i) {
    u_printf("%s%s", argv[i], (i + 1 < argc) ? " " : "\n");
  }
}

static void cmd_sleep(int argc, char **argv)
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

static void cmd_exit(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  u_puts("shell exiting...");
  thread_exit(0); /* 不会返回 */
}

/* -------------------------------------------------------------------------- */
/* shell 主循环                                                               */
/* -------------------------------------------------------------------------- */

static void shell_main_loop(void)
{
  char line[SHELL_MAX_LINE];
  char *argv[SHELL_MAX_ARGS];

  u_puts("tiny shell started. type 'help' for commands.");

  for (;;) {
    /* 提示符（不自动换行） */
    u_printf("> ");

    int len = u_gets(line, sizeof(line));
    if (len < 0) {
      u_puts("read error");
      continue;
    }
    if (len == 0) {
      /* EOF 或空行，简单跳过 */
      continue;
    }

    int argc = shell_parse_line(line, argv, SHELL_MAX_ARGS);
    if (argc == 0) {
      continue;
    }

    const shell_cmd_t *cmd = shell_find_cmd(argv[0]);
    if (!cmd) {
      u_printf("unknown command: %s\n", argv[0]);
      continue;
    }

    /* v0：命令在 shell 线程里前台执行，不做后台 job */
    cmd->fn(argc, argv);
  }
}

/* -------------------------------------------------------------------------- */
/* 对外 API                                                                   */
/* -------------------------------------------------------------------------- */

void shell_thread(void *arg)
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
