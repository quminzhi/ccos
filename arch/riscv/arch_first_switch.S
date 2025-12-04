
  .section .text
  .globl   arch_first_switch
  .align   2

#include "trapframe_offset.inc"

# void arch_first_switch(struct trapframe *tf);
arch_first_switch:
# a0 = tf 指针
  mv       t0, a0             # t0 = &trapframe

# 先恢复 sstatus 和 sepc：
# - sstatus.SPP 决定 sret 返回到 S/U 模式
# - sstatus.SPIE 决定返回后是否打开 S 模式中断
  ld       t1, TF_SSTATUS(t0)
  csrw     sstatus, t1

  ld       t1, TF_SEPC(t0)
  csrw     sepc, t1

# 然后恢复通用寄存器
  ld       ra, TF_RA(t0)
  ld       sp, TF_SP(t0)
  ld       gp, TF_GP(t0)
  ld       tp, TF_TP(t0)

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

# 最后恢复 t0 自己
  ld       t0, TF_T0(t0)

# 不会返回，直接按照 tf->sepc / tf->sstatus sret
  sret
