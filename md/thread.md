# 线程调度 & 生命周期模型笔记  
（schedule / exit / join / kill / recycle）

> 面向未来的自己 / 读代码的人：这是现在这套内核线程模型的“设计文档简版”。

---

## 1. 大局观：我们在做什么

- 这是一个 **S-mode 内核线程调度器**，每个线程存在于一个固定大小的数组 `g_threads[THREAD_MAX]` 中。
- 线程上下文保存在 `struct trapframe` 里，通过 `schedule(tf)` 在 **trap 返回前** 切换。
- 线程生命周期通过状态机管理：

```c
  typedef enum {
    THREAD_UNUSED   = 0,
    THREAD_RUNNABLE = 1,
    THREAD_RUNNING  = 2,
    THREAD_SLEEPING = 3,
    THREAD_WAITING  = 4, // join 等待中
    THREAD_ZOMBIE   = 5, // 已退出，等待回收
  } ThreadState;
````

* join 语义类似 `pthread_join`：

  * 一个线程 **最多允许一个 joiner**。
  * 被 join 的线程退出时，会唤醒 joiner 并写回 `exit_code`。
  * 线程的 slot 回收由 **join 或 exit/kill** 负责，而不是调度器。

---

## 2. Thread 结构中和 join/exit 相关的字段

简化版结构（只列和本主题相关的字段）：

```c
typedef struct Thread {
  tid_t id;
  ThreadState state;
  uint64_t wakeup_tick;
  const char *name;
  int is_user;         // 0 = S, 1 = U

  struct trapframe tf; // 寄存器上下文
  uint8_t *stack_base;

  int exit_code;       // thread_exit(exit_code) 保存的值

  // join 相关
  tid_t join_waiter;         // 谁在 join 我？(-1 表示没人)
  tid_t waiting_for;         // 我在 join 谁？（仅 WAITING 时有效）
  uintptr_t join_status_ptr; // join 时传入的 int*，用于写 exit_code

  int can_be_killed;         // 0 = 不允许 kill；1 = 允许（用在 thread_sys_kill）
} Thread;

static Thread  g_threads[THREAD_MAX];
static tid_t   g_current_tid;
```

### 状态转换（核心）

* `RUNNING` → `THREAD_ZOMBIE`：`thread_sys_exit()` / `thread_sys_kill()`
* `THREAD_ZOMBIE` → `THREAD_UNUSED`：

  * 有 joiner：由 `thread_sys_exit()` / `thread_sys_kill()` 中的 `recycle_thread()` 完成
  * 无 joiner：由 `thread_sys_join()` 里“目标已是 ZOMBIE”路径回收
* `THREAD_WAITING` → `THREAD_RUNNABLE`：目标线程 exit/kill 时唤醒 joiner

---

## 3. schedule(tf)：只负责“换人跑”，不做后续逻辑

```c
void schedule(struct trapframe *tf)
{
  Thread *cur = &g_threads[g_current_tid];

  /* 保存当前上下文 */
  cur->tf = *tf;
  if (cur->state == THREAD_RUNNING) {
    cur->state = THREAD_RUNNABLE;
  }

  // 选下一个 RUNNABLE（round-robin）…
  tid_t next_tid = ...;
  g_current_tid  = next_tid;

  Thread *next = &g_threads[next_tid];
  next->state  = THREAD_RUNNING;

  /* 用下一个线程的 tf 覆盖当前 tf：
   * 真正的寄存器恢复发生在 trap 返回时（trap.S）。
   */
  *tf = next->tf;
}
```

> **重要约定：**
>
> * `schedule(tf)` 只改 `g_threads[]` 和 `*tf`，**不切换内核栈**。
> * 这意味着：
>   **任何调用 `schedule(tf)` 的函数，`schedule` 必须是“最后一行行为性的代码”**。
>   在 `schedule(tf)` 后面继续写复杂逻辑（尤其是基于 `g_current_tid` 的逻辑）会很危险。

---

## 4. thread_sys_exit：退出 + 唤醒 joiner +（有 joiner 时）回收自己

```c
void thread_sys_exit(struct trapframe *tf, int exit_code)
{
  Thread *cur      = &g_threads[g_current_tid];
  tid_t   self_tid = g_current_tid;
  tid_t   joiner   = cur->join_waiter;

  /* 保存当前上下文（调试 / backtrace 用） */
  cur->tf        = *tf;
  cur->exit_code = exit_code;
  cur->state     = THREAD_ZOMBIE;

  if (joiner >= 0 && joiner < THREAD_MAX) {
    Thread *w = &g_threads[joiner];

    /* 如果 join 时传了 status 指针，这里写入 exit_code */
    if (w->join_status_ptr != 0) {
      int *p = (int *)w->join_status_ptr;
      *p     = exit_code;
    }

    /* 让 join() 返回 0（成功） */
    w->tf.a0           = 0;

    /* 清理等待关系并唤醒 joiner */
    w->waiting_for     = -1;
    w->join_status_ptr = 0;
    if (w->state == THREAD_WAITING) {
      w->state = THREAD_RUNNABLE;
    }

    /* 有 joiner 的话，直接回收自己，避免长期 ZOMBIE */
    recycle_thread(self_tid);
  }

  cur->join_waiter = -1;

  /* 没有 joiner 的线程保持 ZOMBIE 等待将来 join() */
  schedule(tf);
  __builtin_unreachable();
}
```

**语义：**

* 有 joiner：

  * joiner 的 `thread_join()` syscall 返回 0，`*status = exit_code`
  * 当前线程的 slot 在这里就被 `recycle_thread` 释放，不会留 ZOMBIE
* 没 joiner：

  * 当前线程状态变成 ZOMBIE，等待以后有人 `thread_join()` -> 再由 join 回收

---

## 5. thread_sys_join：两种路径（立即 / 阻塞）

```c
void thread_sys_join(struct trapframe *tf, tid_t target_tid,
                     uintptr_t status_ptr)
{
  Thread *cur = &g_threads[g_current_tid];

  /* 基本检查 */
  if (target_tid <= 0 || target_tid >= THREAD_MAX) {
    tf->a0 = -1; /* EINVAL */
    return;
  }
  if (target_tid == g_current_tid) {
    tf->a0 = -2; /* EDEADLK: 自己 join 自己 */
    return;
  }

  Thread *t = &g_threads[target_tid];

  if (t->state == THREAD_UNUSED) {
    tf->a0 = -3; /* ESRCH: 不存在或已回收 */
    return;
  }

  /* 路径 1：目标已经是 ZOMBIE -> 立即回收并返回 */
  if (t->state == THREAD_ZOMBIE) {
    if (status_ptr != 0) {
      int *p = (int *)status_ptr;
      *p     = t->exit_code;
    }
    recycle_thread(target_tid);
    tf->a0 = 0;      /* join 返回 0 */
    return;
  }

  /* 限制：一次只允许一个 joiner */
  if (t->join_waiter >= 0 && t->join_waiter != g_current_tid) {
    tf->a0 = -4; /* EBUSY: 已有其它线程在 join 它 */
    return;
  }

  /* 路径 2：目标还在运行 -> 阻塞当前线程，等待它 exit/kill */

  cur->state           = THREAD_WAITING;
  cur->waiting_for     = target_tid;
  cur->join_status_ptr = status_ptr;

  t->join_waiter       = g_current_tid;

  /*
   * 注意：
   *  - 这里之后不要再写任何逻辑！
   *  - 返回值 a0 由 thread_sys_exit()/thread_sys_kill()
   *    在唤醒 joiner 时通过修改 w->tf.a0 = 0 设置。
   */
  schedule(tf);
}
```

**总结：**

* **同步 join**（目标已经 ZOMBIE）：
  在 join 内完成所有事：写 status → `recycle_thread` → `tf->a0=0` 返回。
* **异步 join**（目标还在跑，需要等）：
  join 只负责挂起自己，登记好 `waiting_for / join_status_ptr` & `t->join_waiter`，
  真正的唤醒 + 设置返回值 + 回收由 `thread_sys_exit` / `thread_sys_kill` 完成。

---

## 6. thread_sys_kill：强制退出 + 唤醒 joiner + 回收（有 joiner 时）

```c
void thread_sys_kill(struct trapframe *tf, tid_t target_tid)
{
  /* 基本检查 */
  if (target_tid < 0 || target_tid >= THREAD_MAX) {
    tf->a0 = -1;  // EINVAL
    return;
  }

  if (target_tid == 0) {
    tf->a0 = -2;  // 不允许杀 idle
    return;
  }

  if (target_tid == g_current_tid) {
    tf->a0 = -4;  // 不允许通过 kill 自杀（自杀用 thread_exit）
    return;
  }

  Thread *t = &g_threads[target_tid];

  if (t->can_be_killed == 0) {
    tf->a0 = -3;  // 该线程不许 killed
    return;
  }

  if (t->state == THREAD_UNUSED) {
    tf->a0 = -3;  // ESRCH: 不存在
    return;
  }

  if (t->state == THREAD_ZOMBIE) {
    tf->a0 = 0;   // 已经死了，当成功
    return;
  }

  tid_t joiner = t->join_waiter;

  /* 强制标记为 ZOMBIE（SIGKILL 风格） */
  t->exit_code = THREAD_EXITCODE_SIGKILL;  // -9
  t->state     = THREAD_ZOMBIE;

  /* 如果有人在 join 它，就按 exit 逻辑处理 joiner */
  if (joiner >= 0 && joiner < THREAD_MAX) {
    Thread *w = &g_threads[joiner];

    if (w->join_status_ptr != 0) {
      int *p = (int *)w->join_status_ptr;
      *p     = t->exit_code;
    }

    w->tf.a0           = 0;  // join 返回 0
    w->waiting_for     = -1;
    w->join_status_ptr = 0;
    if (w->state == THREAD_WAITING) {
      w->state = THREAD_RUNNABLE;  // 唤醒 joiner
    }

    /* 有 joiner 的话，立刻回收被 kill 的线程 */
    recycle_thread(target_tid);
  }

  t->join_waiter = -1;

  /* kill syscall 返回 0 表示成功 */
  tf->a0 = 0;
}
```

**语义：**

* 如果没人 join 目标：

  * 目标线程只变成 ZOMBIE，和 exit 一样，等待未来的 join 回收。
* 如果有人 join 目标：

  * 立即唤醒 joiner、写 exit_code（一般为 -9）、回收目标 slot。

---

## 7. recycle_thread：只能在“确定不会再用到这个 tid”时调用

简化版本（你已有）的大意：

```c
static void recycle_thread(tid_t tid)
{
  if (tid <= 0 || tid >= THREAD_MAX) return; // 不回收 idle/main

  Thread *t    = &g_threads[tid];
  t->state     = THREAD_UNUSED;
  t->wakeup_tick     = 0;
  t->name      = "unused";
  t->exit_code = 0;
  t->join_waiter     = -1;
  t->waiting_for     = -1;
  t->join_status_ptr = 0;
  memset(&t->tf, 0, sizeof(t->tf));
  /* 栈数组 g_thread_stacks[tid] 可复用 */
}
```

**使用约束：**

* 调用 `recycle_thread()` 后，**不能再访问这个 `tid` 对应的 Thread 结构**。
* 永远不要在 `recycle_thread()` 后面再写 `Thread *t = &g_threads[tid]; ...` 这类逻辑。

当前模型里，允许回收的位置只有：

* `thread_sys_join()`，“目标已是 ZOMBIE”的那条路径；
* `thread_sys_exit()`，有 joiner 的情况下；
* `thread_sys_kill()`，有 joiner 的情况下。

---

## 8. 一些“别踩第二次”的坑

1. **`schedule(tf)` 后不要再写逻辑**

   * 你的调度模型是 trap 内换 tf，trap 返回时才真正切线程。
   * 在 `schedule(tf)` 之后继续基于 `g_current_tid` 做逻辑，很容易搞乱线程状态。

2. **ZOMBIE 泄漏的唯一来源：没人回收**

   * 有 joiner 的线程必须在 exit/kill 时回收自己（我们已经这么做了）。
   * 无 joiner 的线程必须由 join 来回收；如果从来没人 join，它就会一直是 ZOMBIE（和 POSIX 一样）。

3. **不要在 kill 中尝试“自杀”**

   * `thread_sys_kill` 不允许 `target_tid == g_current_tid`，自杀用 `thread_exit()` 更简单，也更容易 reason。

4. **回收的时机要统一**

   * 以前我们试过在 `thread_sys_join` 的 `schedule()` 之后做逻辑，和当前调度模型不兼容，最后导致乱内存和坏指针。
   * 现在的约定是：

     * join 要么同步回收（目标已 ZOMBIE）
     * 要么只挂起，不负责后续收尾；收尾统一在 exit/kill 里做。

---

## 9. 用户态契约（简单回顾）

* `int thread_join(tid_t tid, int *status)`：

  * 成功返回 0，`*status = exit_code`（可能是正常 0，或 -9/-15 等 kill 码）
  * 失败返回负数（EINVAL / ESRCH / EBUSY / EDEADLK）
* `int thread_kill(tid_t tid)`：

  * 成功返回 0，被杀的线程的 `exit_code = THREAD_EXITCODE_SIGKILL`（比如 -9）
  * shell 的 `ps` 命令可以在 `EXIT` 列看到这个 exit_code

---

这就是现在这一套 **schedule + exit + join + kill + recycle** 模型的设计要点。
以后改内核线程相关逻辑时，先回来看一眼这个约定，可以少踩很多坑。😄
