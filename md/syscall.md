# 开发笔记：从 UART write 到 阻塞 read 与用户态 console

> 目标：  
> - 打通一条 **U 模式 `write()` → S 模式 syscall → UART** 的输出路径  
> - 再打通一条 **UART 中断 → 内核 ring buffer → 阻塞 `read()` → 用户态 console** 的输入路径  
> - 最后在用户态写一个小小的 `console_worker` 线程做 demo

---

## 1. 分层结构回顾（OpenSBI 风格）

我们现在的栈大概是这样：

- **M 模式**：QEMU + OpenSBI（Timer、外部中断、SBI 调用）
- **S 模式内核**  
  - trap / syscall / PLIC / 中断分发  
  - 线程子系统：`THREAD_RUNNABLE / BLOCKED / SLEEPING / ZOMBIE`  
  - UART 平台驱动（MMIO 16550）  
  - 控制台：ring buffer + console API（`console_read_nonblock` 等）
- **U 模式用户程序**  
  - `sys_read/sys_write` syscall stub  
  - `ulib`：迷你版 stdio + 字符串函数  
  - 应用线程：`user_main`、`worker_thread`、`console_worker`

关键点：**S 模式只做“最小必要的抽象”，用户态通过 syscall 自己玩花活**。

---

## 2. write 路径：U → S → UART

### 2.1 用户态：sys_write

```c
int sys_write(int fd, const void *buf, uint64_t len)
{
  register uintptr_t a0 asm("a0") = SYS_WRITE;
  register uintptr_t a1 asm("a1") = (uintptr_t)fd;
  register uintptr_t a2 asm("a2") = (uintptr_t)buf;
  register uintptr_t a3 asm("a3") = (uintptr_t)len;

  __asm__ volatile("ecall"
                   : "+r"(a0), "+r"(a1), "+r"(a2), "+r"(a3)
                   :
                   : "memory");

  return (int)a0;  // 返回写入字节数 or 负错误码
}
````

### 2.2 内核：SYS_WRITE 分发

```c
void syscall_handler(struct trapframe *tf)
{
  reg_t sysno = tf->a0;
  reg_t a1    = tf->a1;
  reg_t a2    = tf->a2;
  reg_t a3    = tf->a3;

  long ret = -1;

  switch (sysno) {
    case SYS_WRITE:
      ret = ksys_write((int)a1, (const char *)a2, (uint64_t)a3);
      break;
    ...
  }

  tf->a0   = (reg_t)ret;
  tf->sepc += 4;     // 跳过 ecall
}
```

### 2.3 内核：ksys_write → UART 驱动

```c
static long ksys_write(int fd, const char *buf, uint64_t len)
{
  if (fd == 1 || fd == 2) { // stdout/stderr
    console_write(buf, (size_t)len);  // 内部调 uart16550_write
    return (long)len;
  }
  return -1;
}
```

UART 驱动本身是简单的 busy wait：

```c
void uart16550_putc(char c)
{
  while ((uart_lsr_read() & UART_LSR_THRE) == 0) {
    /* spin */
  }
  uart_thr_write((uint8_t)c);
}
```

---

## 3. read 路径：阻塞读 + UART 中断唤醒

### 3.1 线程状态：BLOCKED

在线程状态里加一个通用阻塞态：

```c
typedef enum {
  THREAD_UNUSED   = 0,
  THREAD_RUNNABLE = 1,
  THREAD_RUNNING  = 2,
  THREAD_SLEEPING = 3,
  THREAD_WAITING  = 4, // join 用
  THREAD_ZOMBIE   = 5,
  THREAD_BLOCKED  = 6, // I/O 阻塞
} ThreadState;
```

提供两个原语：

```c
void thread_block(struct trapframe *tf)
{
  Thread *cur = &g_threads[g_current_tid];
  cur->state = THREAD_BLOCKED;
  schedule(tf);                 // 切到别的线程
}

void thread_wake(tid_t tid)
{
  if (tid < 0 || tid >= THREAD_MAX) return;
  Thread *t = &g_threads[tid];
  if (t->state == THREAD_BLOCKED) {
    t->state = THREAD_RUNNABLE;
  }
}
```

### 3.2 控制台输入：ring buffer + stdin 等待者

核心状态：

```c
#define CONSOLE_RBUF_SIZE 1024

static char g_rx_buf[CONSOLE_RBUF_SIZE];
static uint32_t g_rx_head = 0;
static uint32_t g_rx_tail = 0;

/* 当前有没有线程在等 stdin？ */
static tid_t g_stdin_waiter = -1;

/* 每个线程记录一次阻塞 read 的上下文 */
typedef struct Thread {
  ...
  uintptr_t pending_read_buf;
  uint64_t  pending_read_len;
} Thread;
```

非阻塞读（仅从 ring buffer 拿数据）：

```c
static int console_read_nonblock(char *buf, size_t len)
{
  size_t n = 0;
  while (n < len && g_rx_head != g_rx_tail) {
    buf[n++] = g_rx_buf[g_rx_tail];
    g_rx_tail = (g_rx_tail + 1) % CONSOLE_RBUF_SIZE;
  }
  return (int)n;
}
```

### 3.3 内核：ksys_read（可能阻塞）

设计成“**有结果就返回，没有结果就 block**”的风格：

```c
long ksys_read(int fd, char *buf, uint64_t len,
               struct trapframe *tf,
               int *done)
{
  if (fd != 0) {
    *done = 1;
    return -1;
  }

  // 1. 先尝试非阻塞读
  int n = console_read_nonblock(buf, (size_t)len);
  if (n > 0) {
    *done = 1;
    return n;
  }

  // 2. 没数据：记录上下文 + 阻塞当前线程
  Thread *cur = &g_threads[thread_current()];
  cur->pending_read_buf = (uintptr_t)buf;
  cur->pending_read_len = len;
  g_stdin_waiter        = cur->id;

  thread_block(tf);    // 当前线程从此挂起

  *done = 0;
  return 0;            // 对当前线程来说这个返回值不会再被用到
}
```

syscall handler 里只在 `done=1` 时才写返回值：

```c
case SYS_READ: {
  ADVANCE_SEPC();

  int done = 1;
  long nread = ksys_read((int)tf->a1, (char *)tf->a2, (uint64_t)tf->a3,
                         tf, &done);

  if (done) {
    tf->a0 = (reg_t)nread;
  }
  break;
}
```

### 3.4 UART IRQ：写 ring buffer + 唤醒线程

中断路径大致是：

> S 外部中断 → trap → `platform_handle_s_external` → `uart16550_irq()` → `console_on_char_from_irq(ch)`

在 `console_on_char_from_irq` 中：

```c
void console_on_char_from_irq(uint8_t ch)
{
  /* 1. 写 ring buffer */
  if (!rb_is_full()) {
    g_rx_buf[g_rx_head] = (char)ch;
    g_rx_head = (g_rx_head + 1) % CONSOLE_RBUF_SIZE;
  }

  /* 2. 有没有线程在等 stdin？ */
  if (g_stdin_waiter < 0) return;

  Thread *t = &g_threads[g_stdin_waiter];

  char   *user_buf = (char *)t->pending_read_buf;
  size_t  max_len  = (size_t)t->pending_read_len;

  int n = console_read_nonblock(user_buf, max_len);
  if (n <= 0) return;

  /* 3. 设置这次 read 的返回值 */
  t->tf.a0 = (uintptr_t)n;

  t->pending_read_buf = 0;
  t->pending_read_len = 0;
  thread_wake(g_stdin_waiter);
  g_stdin_waiter = -1;
}
```

这样：

* 当没有数据时，`read()` 会阻塞当前线程（BLOCKED）
* 当有新的 UART 字节到达时，中断把数据填进用户缓冲区 + 写好 `tf->a0` + 唤醒线程
* 下一次调度到这个线程时，trap 直接 `sret` 回用户态，用户态看到的就是 `read()` 返回的字节数

---

## 4. 用户态 ulib：tiny stdio + 输入工具

### 4.1 写：u_putchar / u_puts / u_printf ...

```c
#define FD_STDOUT 1

int u_putchar(int c)
{
  char ch = (char)c;
  return write(FD_STDOUT, &ch, 1);
}

int u_puts(const char *s)
{
  int n = u_strlen(s);
  int r1 = write(FD_STDOUT, s, (uint64_t)n);
  int r2 = write(FD_STDOUT, "\n", 1);
  return (r1 < 0 || r2 < 0) ? -1 : (r1 + r2);
}

/* u_printf / u_snprintf 使用内部 buffer + write，这里略 */
```

### 4.2 读：u_gets（从 stdin 读一行，不含行尾）

简单、鲁棒版本：

```c
#define FD_STDIN 0

int u_gets(char *buf, int buf_size)
{
  if (buf_size <= 1) {
    return -1;
  }

  int used = 0;

  for (;;) {
    char c;
    int n = read(FD_STDIN, &c, 1);
    if (n < 0) {
      return n;
    }
    if (n == 0) {
      if (used == 0) return 0;  // EOF
      break;
    }

    if (c == '\n' || c == '\r') {
      break;                    // 丢掉行尾
    }

    if (used < buf_size - 1) {
      buf[used++] = c;
    } else {
      // 缓冲区已满，后面的字符丢弃，直到遇到行尾
    }
  }

  buf[used] = '\0';
  return used;                  // 返回的是不含行尾的长度
}
```

---

## 5. 用户态 console_worker：最小 REPL

```c
static void console_worker(void *arg)
{
  (void)arg;

  char line[128];

  u_puts("console worker started. type something (\"exit\" to quit):");

  for (;;) {
    int len = u_gets(line, sizeof(line));
    if (len <= 0) {
      continue;  // 简单 ignore
    }

    if (u_strcmp(line, "exit") == 0 || u_strcmp(line, "quit") == 0) {
      u_puts("console worker exiting.");
      thread_exit(0);  // noreturn
    }

    u_printf("you typed: %s\n", line);
  }
}
```

配合 `user_main` 大致是：

```c
tid_t console_tid =
    thread_create(console_worker, NULL, "console");
...
thread_join(console_tid, &status);
```

输入：

```text
hello⏎
you typed: hello
exit⏎
console worker exiting.
(main join 返回)
```

---

## 6. 一些坑 & 总结性提醒

### 6.1 线程入口函数不能 `return`

* 线程是通过 `sret` 直接跳到入口函数，没有合法的 caller。
* 如果入口函数 `return`，`ra` 通常是 0 → `ret` 跳到地址 0 → trap → panic。
* 约定：**所有线程入口最后都必须 `thread_exit()` 或死循环**。

可以用：

```c
typedef void (*thread_entry_t)(void *arg);
```

配合代码审查保证不 `return`（noreturn 只用于用户态 `thread_exit` 这类真正不回来的封装）。

### 6.2 `sys_read` 的语义

* 对用户态：`read()` 只关心 **“本次返回多少字节”**。
* 阻塞与否对用户完全透明：

  * 有数据 → 一次 trap 内直接返回；
  * 没数据 → 记录上下文 + block 线程，等中断来填 `tf->a0` 后再“晚点返回”。

### 6.3 行读取 vs 单次 read

* **单次 read 不保证读到完整的一行**，只能保证“最多 len、至少 1 字节”。
* 行模式行为放在用户态用 `u_gets` / `u_read_line` 做：

  * 多次 `read()` 拼一行
  * 去掉 `\r` / `\n` 等行尾
  * 再做命令解析（比如识别 `"exit"`）

---

## 7. 这条链路的最终形态

整条链路现在是：

> **UART 中断 → PLIC → S-mode trap → console ring buffer → 阻塞线程唤醒 → 用户态 `read` / `u_gets` / `console_worker`**

这条路径打通之后，你已经有了一个：

* 有调度器
* 有阻塞 syscall
* 有基本 I/O 抽象（read/write/stdio）
* 有用户态线程 + join

的迷你 OS，可以继续在上面叠应用（shell、小工具、多线程 demo 等）了。😄
