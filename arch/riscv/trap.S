/* trap.S */
    .section .text
    .globl trap_entry
    .extern trap_entry_c

/*
 * 非严格 trapframe，只保存 ra：
 *  - trap_entry_c 按普通 C 函数调用约定执行
 *  - 返回后 sret 回到中断前
 * 对于 playground（主循环只是 wfi）足够用了。
 */
trap_entry:
    addi sp, sp, -16
    sd   ra, 8(sp)

    call trap_entry_c

    ld   ra, 8(sp)
    addi sp, sp, 16
    sret
