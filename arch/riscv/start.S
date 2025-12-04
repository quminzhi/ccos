    /*       start.S */
    .section .text.entry
    .globl   _start
    .option  norvc

    /*       S Mode NOW from M-Mode openSBI */
_start:
    /*       设置栈顶 */
    la       sp, __stack_top

    la       t0, __bss_start
    la       t1, __bss_end

    /*       保险起见：按 8 字节对齐起始地址 */
    andi     t2, t0, 7
    beqz     t2, 1f
0:
    bgeu     t0, t1, 2f
    sb       zero, 0(t0)
    addi     t0, t0, 1
    andi     t2, t0, 7
    bnez     t2, 0b

1:
    bgeu     t0, t1, 2f
    sd       zero, 0(t0)
    addi     t0, t0, 8
    j        1b

2:
    /*
    *        OpenSBI fw_jump/fw_dynamic 一般按 Linux 约定：
    *        a0 = hartid, a1 = dtb 物理地址
    *        直接 tail 进入 kernel_main(hartid, dtb_pa)
    */
    tail     main
