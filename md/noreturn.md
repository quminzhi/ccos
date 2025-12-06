# 关于 `noreturn` 的那些坑（以后不要再踩 🧨）

这个文档是专门给“未来的你”看的，用来提醒自己在裸机 / 内核代码里使用 `__attribute__((noreturn))` 时，**不要再搞崩内核**。

---

## 1. `noreturn` 的真正语义是什么？

> 对编译器来说：**“从这个函数 *永远* 不会执行到调用者的下一条指令。”**

注意，是对“**调用者**”的承诺，而不是“这个函数逻辑上一般不会返回”。

一旦你标了：

```c
void foo(void) __attribute__((noreturn));
````

编译器会认为：

* `call foo();` 之后的代码路径是 **不可达的**；
* 可以重排/删掉那之后的一些代码；
* 可以把 `foo()` 做 tail-call 优化；
* 如果它静态分析发现你“可能返回”，就会给 `-Winvalid-noreturn` 警告。

---

## 2. 哪些地方“应该”用 `noreturn`？

### 2.1 用户态 / 内核态的“真正退出”函数

**例子：用户态 `thread_exit`**

```c
void thread_exit(int code) __attribute__((noreturn));

void thread_exit(int code)
{
  // 发 syscall，让内核销毁当前线程
  sys_thread_exit(code);

  // 告诉编译器：这里之后的路径逻辑上永远不会被执行
  __builtin_unreachable();
}
```

**特点：**

* 调用了它，就不可能再回到调用它的那条语句；
* 无论是内核把线程标记为 ZOMBIE，还是切走后永不调度回来，对调用者来说就是“永不返回”。

### 2.2 panic / 死循环

```c
void panic(const char *msg) __attribute__((noreturn));

void panic(const char *msg)
{
  platform_puts("panic: ");
  platform_puts(msg);
  platform_puts("\n");

  while (1) {
    asm volatile("wfi");
  }
}
```

**特点：** 函数本身显式进入死循环或直接关机，不存在“返回”。

### 2.3 线程入口函数（在你这种 `sret` 进线程的模型）

```c
typedef void (*thread_entry_t)(void *arg) __attribute__((noreturn));

static void console_worker(void *arg) __attribute__((noreturn));

static void console_worker(void *arg)
{
  // 做事...
  thread_exit(0);   // noreturn

  // !!! 不要再写 return，也不要落到函数末尾
}
```

**为什么入口函数适合 `noreturn`？**

* 线程通过 `sret` 直接跳进入口函数，**没有正常 caller**；
* 如果入口函数 `return`，会 `ret` 到一个错误的 `ra`（通常是 0）→ 直接跳到地址 0 → trap → panic。

---

## 3. 哪些地方 **绝对不要** 用 `noreturn`？

### 3.1 内核里 `schedule()` 这类“会回来”的函数

错误示例（不要再这样写）：

```c
void thread_sys_exit(struct trapframe *tf, int exit_code)
    __attribute__((noreturn));   // ❌ 这是错的

void thread_sys_exit(struct trapframe *tf, int exit_code)
{
  ...
  schedule(tf);      // 实际上会返回到 syscall_handler，只是换了个线程的 tf
  __builtin_unreachable();  // 再加这句 = 彻底骗编译器
}
```

**为什么错？**

* 从 C 语义上看，`thread_sys_exit` 是会“从 `schedule(tf)` 返回”的；
* 只是返回之后，`tf` 已经装的是别的线程的上下文；
* 你标了 `noreturn` + `__builtin_unreachable()` 等于对编译器说：
  “进来之后永远不会执行到调用点后面的代码”，于是它会：

  * 重排调用栈；
  * 做非常激进的优化；
  * 甚至把这段当成 tail-call；
  * 最后导致 **参数乱掉 / 栈乱掉 / 回到奇怪的地方**。

典型症状就是你见到的：

* `thread_sys_exit` 的 backtrace 里莫名其妙长出 `thread_sys_join`；
* `target_tid` 之类参数变成 24864 这种垃圾值；
* 最后一路跑到 `pc=0`、`ra=垃圾 ASCII`，panic。

**记住一句话：**

> **只要函数在 C 语义上“会返回到调用者”——哪怕换了上下文——就不要标 `noreturn`。**

包括但不限于：

* `schedule(tf)`
* `thread_sys_exit(tf, code)`（你现在这版）
* 任何“调用完后还要回到上层逻辑”的内核函数

---

## 4. `__builtin_unreachable()` 怎么用才安全？

### ✅ 正确用法

只能用在这两类场景：

1. **确实“不可能走到这里”的代码路径**（逻辑保证）：

   ```c
   switch (x) {
     case 0: ...; break;
     case 1: ...; break;
   }
   // x 只能是 0 或 1
   __builtin_unreachable();
   ```

2. **真正 noreturn 函数的最后一句**

   ```c
   void thread_exit(int code) __attribute__((noreturn));
   void panic(const char *msg) __attribute__((noreturn));
   ```

   这类函数在语义上就“不应该返回”，给编译器一个 hint 很合理。

### ❌ 错误用法

在任何**实际上会返回**的函数结尾写这句，都在制造 UB（未定义行为）：

```c
void may_return_or_not(...)
{
  if (cond)
    panic("oops");   // 不返回
  else
    return;          // 会返回

  __builtin_unreachable();  // ❌ 编译器会认为上面的 return 不存在
}
```

**规则：**

> 如果函数里存在任何一条“正常 return”路径，就不要用 `noreturn`，
> 也不要在结尾写 `__builtin_unreachable()`。

---

## 5. attribute 的写法：避免 GCC/Clang 兼容问题

### ✅ 推荐写法

* 在 **声明** 上加：

  ```c
  void thread_exit(int code) __attribute__((noreturn));
  ```

* 或者在定义前的声明：

  ```c
  static void console_worker(void *arg) __attribute__((noreturn));

  static void console_worker(void *arg) {
      ...
  }
  ```

* 或者放在返回类型前/函数名后：

  ```c
  static __attribute__((noreturn)) void console_worker(void *arg);
  static void __attribute__((noreturn)) console_worker(void *arg);
  ```

### ❌ 不推荐写法

```c
// Clang 可以，GCC 不喜欢，还会触发 -Wgcc-compat
static void console_worker(void *arg) __attribute__((noreturn)) {
    ...
}
```

为了省心，以后就：**“声明写 attribute，定义不写”** —— C 里最安全的一种模式。

---

## 6. 给未来自己的小 checklist ✅

以后写线程/内核代码时，先对照下面几条：

1. **这是线程入口函数吗？**

   * 是 → 用 `thread_entry_t`（带 `noreturn`），函数内部最后必须调用 `thread_exit()`，不得 `return`。
2. **这是退出当前线程/进程的 syscall stub 吗？**

   * 是 → 标 `noreturn`，结尾 `__builtin_unreachable()`。
3. **这是 panic / 死循环 / 关机函数吗？**

   * 是 → 标 `noreturn` 或直接写无限 `for(;;) {}`。
4. **这个函数会从调用点“C 语义地返回”吗？（比如 `schedule(tf)` 那类）**

   * 是 → **绝对不要** 标 `noreturn`，也不要在结尾写 `__builtin_unreachable()`。
5. **如果编译器报 `Function declared 'noreturn' should not return`：**

   * 检查：

     * 是否还有 `return` 分支；
     * 是否结尾确实会执行到；
     * 如果只是逻辑上“几乎不会走到”，但 C 语义允许走到 → 那就干脆别标 `noreturn`。

---

## 7. 一句话总结

> `noreturn` 是一个**对编译器的承诺**，不是对自己说的注释。
> 一旦承诺错了，编译器就会“帮你优化成地狱难 debug 的未定义行为”。

以后只要你想在内核里再加 `noreturn`，先停一秒，问自己：

> “这个函数从 C 的角度看，**真的、绝对、完全不可能返回到调用者吗？**”

如果答案里有一丁点犹豫——就不要加。🙂
