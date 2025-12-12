  .section .text
  .globl   arch_first_switch
  .align   2
  .option  norvc

#include "trapframe_offset.inc"
#include "cpu_defs.h"              /* 需要 CPU_CUR_TF_OFF 等偏移 */

# void arch_first_switch(struct trapframe *tf);
arch_first_switch:
  # a0 = tf*
  # 约定：tp = cpu*（cpu_init_this_hart 已经设置过）
  # 保险：确保 sscratch 也等于 cpu*
  csrw     sscratch, tp

  # cpu->cur_tf = tf*
  sd       a0, CPU_CUR_TF_OFF(tp)

  # t0 = tf*
  mv       t0, a0

  # 先恢复 sstatus / sepc：决定 sret 去 U/S 模式，以及是否开中断
  ld       t1, TF_SSTATUS(t0)
  csrw     sstatus, t1

  ld       t1, TF_SEPC(t0)
  csrw     sepc, t1

  # 恢复通用寄存器（注意：不恢复 tp，tp 永远是 cpu*）
  ld       ra, TF_RA(t0)
  ld       gp, TF_GP(t0)
  # ld     tp, TF_TP(t0)         # 不恢复

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

  # 最后恢复 sp（更安全：避免中途改 sp 影响调试/异常路径）
  ld       sp, TF_SP(t0)

  # 最后恢复 t0 自己（ld 的基址用的是旧 t0，OK）
  ld       t0, TF_T0(t0)

  sret
