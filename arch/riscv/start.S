    /*       start.S */
    .section .text.entry
    .globl   _start
    .globl   secondary_entry     # 预留给以后 sbi_hart_start 用
    .option  norvc

#include "cpu_defs.h"

    .extern  g_kstack            # uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];
    .extern  kernel_main

_start:
# a0 = hartid
# a1 = dtb_pa
    mv       t3, a0              # t3 = hartid

# -------------------- 冷启动路径：清 BSS --------------------
# 当前 OpenSBI HSM 语义下，只有 Boot HART 会走到这里，
# 所以这里可以放心认为“只执行一次”。

    la       sp, __stack_top

    la       t0, __bss_start
    la       t1, __bss_end

# 按 8 字节对齐起始地址
    andi     t2, t0, 7
    beqz     t2, .L_bss_aligned

.L_bss_align:
    bgeu     t0, t1, .L_bss_done
    sb       zero, 0(t0)
    addi     t0, t0, 1
    andi     t2, t0, 7
    bnez     t2, .L_bss_align

.L_bss_aligned:
    bgeu     t0, t1, .L_bss_done
    sd       zero, 0(t0)
    addi     t0, t0, 8
    j        .L_bss_aligned

.L_bss_done:
# BSS 已清零，可以安全用 g_kstack / 全局变量
    j        per_hart_start

# -------------------- 二级入口：给 sbi_hart_start 用 --------------------
# 以后在 primary_main 里用 HSM:
# sbi_hart_start(h, secondary_entry, dtb_pa);
# 这样其它 hart 就从这里进来，不会再清 BSS。
secondary_entry:
# a0 = hartid, a1 = dtb_pa (opaque)
    mv       t3, a0

per_hart_start:
# 为每个 hart 设置自己的内核栈和 sscratch
    la       t0, g_kstack        # &g_kstack[0][0]
    li       t1, KSTACK_SIZE
    mul      t2, t3, t1          # t2 = hartid * KSTACK_SIZE
    add      t0, t0, t2          # &g_kstack[hartid][0]
    add      t0, t0, t1          # &g_kstack[hartid][KSTACK_SIZE]

    mv       sp, t0
    csrw     sscratch, t0

# 保持 OpenSBI 约定：a0 = hartid, a1 = dtb_pa
    tail     kernel_main
