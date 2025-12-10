    /*       start.S */
    .section .text.entry
    .globl   _start
    .option  norvc

#include "cpu_defs.h"

    .extern  g_kstack                                # uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];

_start:
# OpenSBI 约定：
# a0 = hartid
# a1 = dtb_pa
# 不能直接 csrr t3,mhartid
    mv       t3, a0                                  # t3 = hartid

# a0 / a1 保留 OpenSBI 约定：
# a0 = hartid, a1 = dtb_pa
    mv       a0, t3

# -------------------- 只让 hart0 清 BSS --------------------
    bnez     t3, 2f                                  # 如果 hartid != 0，跳过 BSS 清零

# 这里是主核专属路径：用启动栈清 BSS
    la       sp, __stack_top

    la       t0, __bss_start
    la       t1, __bss_end

# 按 8 字节对齐起始地址
    andi     t2, t0, 7
    beqz     t2, 1f

0:
    bgeu     t0, t1, bss_done
    sb       zero, 0(t0)
    addi     t0, t0, 1
    andi     t2, t0, 7
    bnez     t2, 0b

1:
    bgeu     t0, t1, bss_done
    sd       zero, 0(t0)
    addi     t0, t0, 8
    j        1b

bss_done:
# 主核 BSS 清完，继续到后面的 per-hart 栈设置
    j        2f

# -------------------- 其他 hart 从这里接着走 --------------------
2:
# 从这里开始，假定 BSS 已经被 hart0 清完，可以安全使用 g_kstack

# t3 里已经是 hartid
    la       t0, g_kstack                            # &g_kstack[0][0]
    li       t1, KSTACK_SIZE                         # 每 hart 的内核栈大小
    mul      t2, t3, t1                              # t2 = hartid * KSTACK_SIZE
    add      t0, t0, t2                              # &g_kstack[hartid][0]
    add      t0, t0, t1                              # &g_kstack[hartid][KSTACK_SIZE] = 栈顶

    mv       sp, t0                                  # sp = 当前 hart 的内核栈顶
    csrw     sscratch, t0                            # trap_entry 里会用 sscratch 作内核栈顶

    /*
    *        a0 = hartid, a1 = dtb_pa（保持 OpenSBI 约定）
    */
    tail     kernel_main
