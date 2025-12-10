    /*       trap_s.S - S 模式 trap 入口（从原来的 M 模式版本迁移而来） */

    .section .text
    .globl   trap_entry
    .align   2                                           # trap 入口 4 字节对齐，满足 stvec 要求

#include "trapframe_offset.inc"

# S 模式 trap_entry 流程：
# 1. 在“当前线程的栈”上分配 trapframe，保存所有 GPR + CSR
# 2. 把陷入前的栈指针（thread stack top）保存到 tf->sp
# 3. 用 s0 记录 trapframe 指针
# 4. 从 sscratch 取出“内核栈顶”，切换到内核栈
# 5. 调用 C: uintptr_t trap_entry_c(struct trapframe *tf)
# 6. 用 trap_entry_c 返回的新 sepc + trapframe 内容恢复用户上下文
# 7. sret 回到用户或内核线程 (scause)

    .extern  trap_entry_c                                # C: uintptr_t trap_entry_c(struct trapframe *tf)

trap_entry:
# ---- 1. 在用户栈上分配 trapframe 并保存寄存器 ----
    addi     sp, sp, -TF_SIZE

# 先保存 t0 (后面要当 scratch 用)
    sd       t0, TF_T0(sp)

# ra, gp, tp
    sd       ra, TF_RA(sp)
    sd       gp, TF_GP(sp)
    sd       tp, TF_TP(sp)

# 保存“陷入前的用户 sp” = 当前 sp + TF_SIZE
    addi     t0, sp, TF_SIZE
    sd       t0, TF_SP(sp)

# 其它通用寄存器
    sd       t1, TF_T1(sp)
    sd       t2, TF_T2(sp)
    sd       s0, TF_S0(sp)
    sd       s1, TF_S1(sp)
    sd       a0, TF_A0(sp)
    sd       a1, TF_A1(sp)
    sd       a2, TF_A2(sp)
    sd       a3, TF_A3(sp)
    sd       a4, TF_A4(sp)
    sd       a5, TF_A5(sp)
    sd       a6, TF_A6(sp)
    sd       a7, TF_A7(sp)
    sd       s2, TF_S2(sp)
    sd       s3, TF_S3(sp)
    sd       s4, TF_S4(sp)
    sd       s5, TF_S5(sp)
    sd       s6, TF_S6(sp)
    sd       s7, TF_S7(sp)
    sd       s8, TF_S8(sp)
    sd       s9, TF_S9(sp)
    sd       s10, TF_S10(sp)
    sd       s11, TF_S11(sp)
    sd       t3, TF_T3(sp)
    sd       t4, TF_T4(sp)
    sd       t5, TF_T5(sp)
    sd       t6, TF_T6(sp)

    csrr     t1, sepc
    sd       t1, TF_SEPC(sp)

    csrr     t1, sstatus
    sd       t1, TF_SSTATUS(sp)

    csrr     t1, scause
    sd       t1, TF_SCAUSE(sp)

    csrr     t1, stval
    sd       t1, TF_STVAL(sp)

# ---- 2. 用 s0 暂存 trapframe 指针 ----
    mv       s0, sp                                      # s0 = &trapframe

# ---- 3. 切换到内核栈（从 sscratch 取内核栈顶） ----
    csrr     t0, sscratch                                # t0 = kernel_stack_top
    mv       sp, t0
    addi     sp, sp, -16                                 # 给 C 调用留一点栈空间（可选）

# ---- 4. 调用 C 层 trap_entry_c(&trapframe) ----
    mv       a0, s0                                      # a0 = struct trapframe *tf
    call     trap_entry_c                                # 返回值 a0 = 新的 sepc

# ---- 5. 用 trapframe 恢复用户上下文 ----
    mv       t0, s0                                      # t0 = &trapframe

    ld       t1, TF_SSTATUS(t0)
    csrw     sstatus, t1

    csrw     sepc, a0

# 按照 trapframe 顺序恢复寄存器
    ld       ra, TF_RA(t0)
    ld       gp, TF_GP(t0)
# ld tp, TF_TP(t0) # tp is per-cpu

    ld       t1, TF_T1(t0)
    ld       t2, TF_T2(t0)
    ld       s0, TF_S0(t0)
    ld       s1, TF_S1(t0)
    ld       a0, TF_A0(t0)
    ld       a1, TF_A1(t0)
    ld       a2, TF_A2(t0)
    ld       a3, TF_A3(t0)
    ld       a4, TF_A4(t0)
    ld       a5, TF_A5(t0)
    ld       a6, TF_A6(t0)
    ld       a7, TF_A7(t0)
    ld       s2, TF_S2(t0)
    ld       s3, TF_S3(t0)
    ld       s4, TF_S4(t0)
    ld       s5, TF_S5(t0)
    ld       s6, TF_S6(t0)
    ld       s7, TF_S7(t0)
    ld       s8, TF_S8(t0)
    ld       s9, TF_S9(t0)
    ld       s10, TF_S10(t0)
    ld       s11, TF_S11(t0)
    ld       t3, TF_T3(t0)
    ld       t4, TF_T4(t0)
    ld       t5, TF_T5(t0)
    ld       t6, TF_T6(t0)

# 恢复“用户栈指针”（新的线程栈顶）
    ld       sp, TF_SP(t0)

    ld       t0, TF_T0(t0)

# ---- 6. sret 回到用户线程（用它自己的“用户栈”）----
    sret
