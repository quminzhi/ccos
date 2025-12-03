  .section .text
  .globl   arch_drop_to_user
  .type    arch_drop_to_user, @function

  /*       void arch_drop_to_user(void (*entry)(void *), void *arg, uintptr_t user_sp)
  *        a0 = entry
  *        a1 = arg
  *        a2 = user_sp
  *
  *        作用：
  *        - 配好 sepc / sstatus，使用 sret 从 S 模式“掉到” U 模式；
  *        - 第一个用户线程从 entry(arg) 开始执行，栈为 user_sp。
  */

#define SSTATUS_UIE   (1 << 0)
#define SSTATUS_SIE   (1 << 1)
#define SSTATUS_UPIE  (1 << 4)
#define SSTATUS_SPIE  (1 << 5)
#define SSTATUS_SPP   (1 << 8)

arch_drop_to_user:
  /*       1. 切换到用户栈 */
  mv       sp, a2 /* sp = user_sp */

  /*       2. 设置 sepc = entry（sret 会跳到这里） */
  csrw     sepc, a0

/* 3. 配置 sstatus:
  *        - SPP = 0 (U 级作为“返回目标”)
  *        - SPIE = 1（sret 时让 U 级打开中断）
  *        - SIE 先清 0（内核中关闭中断）
  */
  csrr     t0, sstatus

  /*       清 SPP（变成 U） */
  li       t1, ~SSTATUS_SPP
  and      t0, t0, t1

  /*       设置 SPIE=1, UPIE=1（UPIE 其实无所谓，主要是 SPIE） */
  li       t1, (SSTATUS_SPIE | SSTATUS_UPIE)
  or       t0, t0, t1

  /*       清 SIE，保证 sret 之前内核中断关闭 */
  li       t1, ~SSTATUS_SIE
  and      t0, t0, t1

  csrw     sstatus, t0

  /*       4. 把参数放到 a0（entry 的第一个参数），a1 不再需要 */
  mv       a0, a1 /* a0 = arg */

  /*       5. sret：切到 U 模式，从 sepc 开始执行 */
  sret
