    .section .text
    .globl   trap_entry
    .align   2
    .option  norvc

#include "trapframe_offset.inc"
#include "cpu_defs.h"

    .extern  trap_entry_c

/* 在 kernel stack 上临时保存 t0..t3 原值（保证不污染 t1/t2） */
#define ENTRY_SCRATCH_SIZE   (32)
#define SCR_T0_OFF           (0)
#define SCR_T1_OFF           (8)
#define SCR_T2_OFF           (16)
#define SCR_T3_OFF           (24)

trap_entry:
    /* ------------------------------------------------------------
     * 0) tp := cpu* （per-CPU 约定：tp 不作为线程上下文恢复）
     * ------------------------------------------------------------ */
    csrr     tp, sscratch

    /* ------------------------------------------------------------
     * 1) 把“陷入前 sp”暂存到 sscratch，然后切到本 hart 的 kstack
     *    注意：这里临时覆盖 sscratch，一会儿会恢复成 cpu*
     * ------------------------------------------------------------ */
    csrw     sscratch, sp                      /* sscratch = old_sp */
    ld       sp, CPU_KSTACK_TOP_OFF(tp)        /* sp = cpu->kstack_top */
    addi     sp, sp, -ENTRY_SCRATCH_SIZE       /* reserve scratch, keep 16B aligned */

    /* ------------------------------------------------------------
     * 2) 先把 t0..t3 原值落到 scratch，之后随便用 t0/t1/t2/t3 都不怕
     * ------------------------------------------------------------ */
    sd       t0, SCR_T0_OFF(sp)
    sd       t1, SCR_T1_OFF(sp)
    sd       t2, SCR_T2_OFF(sp)
    sd       t3, SCR_T3_OFF(sp)

    /* ------------------------------------------------------------
     * 3) tf* = cpu->cur_tf
     * ------------------------------------------------------------ */
    ld       t0, CPU_CUR_TF_OFF(tp)            /* t0 = tf* */
    beqz     t0, .Lno_tf

    /* ------------------------------------------------------------
     * 4) 保存 t4（因为我们将用 t4 作为临时寄存器）
     * ------------------------------------------------------------ */
    sd       t4, TF_T4(t0)

    /* ------------------------------------------------------------
     * 5) 把 scratch 中的 t0..t3 原值写入 tf（严谨：不污染 t1/t2）
     * ------------------------------------------------------------ */
    ld       t4, SCR_T0_OFF(sp)
    sd       t4, TF_T0(t0)

    ld       t4, SCR_T1_OFF(sp)
    sd       t4, TF_T1(t0)

    ld       t4, SCR_T2_OFF(sp)
    sd       t4, TF_T2(t0)

    ld       t4, SCR_T3_OFF(sp)
    sd       t4, TF_T3(t0)

    /* ------------------------------------------------------------
     * 6) 保存 old_sp 到 TF_SP，并把 sscratch 恢复为 cpu*
     * ------------------------------------------------------------ */
    csrr     t4, sscratch                      /* t4 = old_sp */
    sd       t4, TF_SP(t0)
    csrw     sscratch, tp                      /* sscratch = cpu* (restore invariant) */

    /* ------------------------------------------------------------
     * 7) 保存剩余通用寄存器
     *    注意：tp 保存的是 cpu*（仅用于调试观察），恢复时不会用它覆盖 tp
     * ------------------------------------------------------------ */
    sd       ra, TF_RA(t0)
    sd       gp, TF_GP(t0)
    sd       tp, TF_TP(t0)

    sd       s0, TF_S0(t0)
    sd       s1, TF_S1(t0)
    sd       s2, TF_S2(t0)
    sd       s3, TF_S3(t0)
    sd       s4, TF_S4(t0)
    sd       s5, TF_S5(t0)
    sd       s6, TF_S6(t0)
    sd       s7, TF_S7(t0)
    sd       s8, TF_S8(t0)
    sd       s9, TF_S9(t0)
    sd       s10, TF_S10(t0)
    sd       s11, TF_S11(t0)

    sd       a0, TF_A0(t0)
    sd       a1, TF_A1(t0)
    sd       a2, TF_A2(t0)
    sd       a3, TF_A3(t0)
    sd       a4, TF_A4(t0)
    sd       a5, TF_A5(t0)
    sd       a6, TF_A6(t0)
    sd       a7, TF_A7(t0)

    /* t5/t6 没被我们当 scratch，用原值直接保存即可 */
    sd       t5, TF_T5(t0)
    sd       t6, TF_T6(t0)

    /* ------------------------------------------------------------
     * 8) 保存 CSR
     * ------------------------------------------------------------ */
    csrr     t4, sepc
    sd       t4, TF_SEPC(t0)

    csrr     t4, sstatus
    sd       t4, TF_SSTATUS(t0)

    csrr     t4, scause
    sd       t4, TF_SCAUSE(t0)

    csrr     t4, stval
    sd       t4, TF_STVAL(t0)

    /* ------------------------------------------------------------
     * 9) 调用 C：a0=tf*，返回 a0=next_tf*
     * ------------------------------------------------------------ */
    mv       a0, t0
    call     trap_entry_c

    /* ------------------------------------------------------------
     * 10) 更新 cpu->cur_tf = next_tf*
     * ------------------------------------------------------------ */
    csrr     tp, sscratch                      /* tp = cpu*（防御性） */
    sd       a0, CPU_CUR_TF_OFF(tp)            /* cpu->cur_tf = next_tf* */
    mv       t0, a0                            /* t0 = next_tf* */

    /* ------------------------------------------------------------
     * 11) 恢复 CSR
     * ------------------------------------------------------------ */
    ld       t4, TF_SSTATUS(t0)
    csrw     sstatus, t4

    ld       t4, TF_SEPC(t0)
    csrw     sepc, t4

    /* ------------------------------------------------------------
     * 12) 恢复寄存器（注意：tp 不从 tf 恢复！tp 必须保持 cpu*）
     * ------------------------------------------------------------ */
    ld       ra, TF_RA(t0)
    ld       gp, TF_GP(t0)

    ld       s0, TF_S0(t0)
    ld       s1, TF_S1(t0)
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

    ld       a0, TF_A0(t0)
    ld       a1, TF_A1(t0)
    ld       a2, TF_A2(t0)
    ld       a3, TF_A3(t0)
    ld       a4, TF_A4(t0)
    ld       a5, TF_A5(t0)
    ld       a6, TF_A6(t0)
    ld       a7, TF_A7(t0)

    ld       t1, TF_T1(t0)
    ld       t2, TF_T2(t0)
    ld       t3, TF_T3(t0)
    ld       t4, TF_T4(t0)
    ld       t5, TF_T5(t0)
    ld       t6, TF_T6(t0)

    /* 最后恢复目标线程 sp，然后再恢复 t0 */
    ld       sp, TF_SP(t0)
    ld       t0, TF_T0(t0)

    sret

.Lno_tf:
    /* cpu->cur_tf 为空但你收到了 trap：通常是太早开中断 */
1:  j 1b
