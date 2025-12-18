    /*       start.S */
    .section .text.entry
    .globl   _start
    .globl   secondary_entry     # Reserved for future sbi_hart_start use.
    .option  norvc

#include "cpu_defs.h"

    .extern  g_kstack            # uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];
    .extern  kernel_main

_start:
# a0 = hartid
# a1 = dtb_pa
    mv       t3, a0              # t3 = hartid

# -------------------- Cold boot path: clear BSS --------------------
# Under current OpenSBI HSM semantics only the boot hart reaches here,
# so this block executes exactly once.

    la       sp, __stack_top

    la       t0, __bss_start
    la       t1, __bss_end

# Align the starting address to 8 bytes.
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
# BSS is zeroed; global data including g_kstack is now safe to use.
    j        per_hart_start

# -------------------- Secondary entry for sbi_hart_start ------------ 
# When primary_main uses the HSM hooks via
#   sbi_hart_start(h, secondary_entry, dtb_pa);
# other harts arrive here without clearing BSS again.
secondary_entry:
# a0 = hartid, a1 = dtb_pa (opaque)
    mv       t4, a0                    # preserve hartid
    mv       t5, a1                    # preserve dtb_pa
    la       a0, .Lsec_entry_msg
    call     sbi_puts_asm
    mv       a0, t4
    addi     a0, a0, '0'
    li       a7, 0x4442434e            # SBI DBCN
    li       a6, 2                     # write_byte
    ecall
    li       a0, '\n'
    li       a7, 0x4442434e
    li       a6, 2
    ecall
    mv       t3, t4

    /* Restore original arguments for kernel_main */
    mv       a0, t4    # hartid
    mv       a1, t5    # dtb_pa

per_hart_start:
# Set up each hart's kernel stack and sscratch.
    la       t0, g_kstack        # &g_kstack[0][0]
    li       t1, KSTACK_SIZE
    mul      t2, t3, t1          # t2 = hartid * KSTACK_SIZE
    add      t0, t0, t2          # &g_kstack[hartid][0]
    add      t0, t0, t1          # &g_kstack[hartid][KSTACK_SIZE]

    mv       sp, t0
    csrw     sscratch, t0

# Preserve the OpenSBI contract: a0 = hartid, a1 = dtb_pa.
    tail     kernel_main

/* Minimal SBI console puts (no stack needed) */
sbi_puts_asm:
    mv       t2, a0
.Lputs_loop:
    lbu      a0, 0(t2)
    beqz     a0, .Lputs_done
    li       a7, 0x4442434e       # SBI DBCN
    li       a6, 2                # write_byte
    ecall
    addi     t2, t2, 1
    j        .Lputs_loop
.Lputs_done:
    ret

.section .rodata
.align 2
.Lsec_entry_msg:
    .asciz "secondary_entry hart="
