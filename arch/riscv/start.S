/* start.S */
    .section .text.entry
    .globl _start

_start:
    /* 设置栈顶（在 .bss 里留 4KB 栈空间） */
    la   sp, stack_top

    /* 清 BSS 段 */
    la   t0, __bss_start
    la   t1, __bss_end
1:
    bge  t0, t1, 2f
    sd   zero, 0(t0)
    addi t0, t0, 8
    j    1b
2:
    /*
     * OpenSBI fw_jump/fw_dynamic 一般按 Linux 约定：
     *  a0 = hartid, a1 = dtb 物理地址
     * 我们直接把 a0/a1 传给 C 的 main(long hartid, long dtb_pa)
     */
    tail kernel_main

    .section .bss
    .align 12
    .globl stack_bottom
stack_bottom:
    .space 4096
    .globl stack_top
stack_top:
