#ifndef RISCV_CSR_H
#define RISCV_CSR_H

#include <stdint.h>

/*
 * 统一的基本寄存器类型 + XLEN
 * 依赖 RISC-V 工具链提供的 __riscv_xlen 宏。
 * 参考: RISC-V psABI / 编译器文档。
 */
#if !defined(__riscv_xlen)
# error "__riscv_xlen is not defined. Are you using a RISC-V toolchain?"
#endif

#if __riscv_xlen == 32
typedef uint32_t reg_t;
# define RISCV_XLEN 32
# define MCAUSE_INT         0x80000000u
#elif __riscv_xlen == 64
typedef uint64_t reg_t;
# define RISCV_XLEN 64
# define MCAUSE_INT         (1ULL << 63)
#else
# error "Unsupported __riscv_xlen (only 32/64 are supported)"
#endif

#define MCAUSE_CODE_MASK    (MCAUSE_INT - 1u)

/*
 * 帮助函数：判断中断/异常 + 取出 code
 *
 * 注：虽然名字叫 mcause_*，但对 scause 也同样适用，
 *     因为编码格式完全一致（MSB=1 表示中断，低位 code 相同）。
 */
static inline int mcause_is_interrupt(reg_t mcause)
{
    return (mcause & MCAUSE_INT) != 0;
}

static inline reg_t mcause_code(reg_t mcause)
{
    return (mcause & MCAUSE_CODE_MASK);
}

/* ===================== mstatus bits ===================== */
/* 参考: RISC-V Privileged Spec: mstatus. */

#define MSTATUS_UIE   (1UL << 0)
#define MSTATUS_SIE   (1UL << 1)
#define MSTATUS_MIE   (1UL << 3)

#define MSTATUS_UPIE  (1UL << 4)
#define MSTATUS_SPIE  (1UL << 5)
#define MSTATUS_MPIE  (1UL << 7)

#define MSTATUS_SPP   (1UL << 8)

#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_MPP_MASK  (3UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_U     (0UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_S     (1UL << MSTATUS_MPP_SHIFT)
#define MSTATUS_MPP_M     (3UL << MSTATUS_MPP_SHIFT)

/* ===================== sstatus bits ===================== */
/*
 * sstatus 实际是 mstatus 的一个“视图”，很多 bit 与 mstatus 共用。
 * 为了可读性，这里给出 S 模式的别名。
 */

#define SSTATUS_UIE   MSTATUS_UIE    /* sstatus.UIE */
#define SSTATUS_SIE   MSTATUS_SIE    /* sstatus.SIE */

#define SSTATUS_UPIE  MSTATUS_UPIE   /* sstatus.UPIE */
#define SSTATUS_SPIE  MSTATUS_SPIE   /* sstatus.SPIE */

#define SSTATUS_SPP   MSTATUS_SPP    /* sstatus.SPP */

/* ===================== mie / mip / sie / sip bits ===================== */
/* 参考: RISC-V Privileged Spec / 五嵌 quick-ref。 */

/* Machine Interrupt Pending / Enable 位布局（低 16 位标准定义） */
#define MIP_USIP   (1UL << 0)   /* User Software Interrupt Pending */
#define MIP_SSIP   (1UL << 1)   /* Supervisor Software Interrupt Pending */
#define MIP_MSIP   (1UL << 3)   /* Machine Software Interrupt Pending */

#define MIP_UTIP   (1UL << 4)   /* User Timer Interrupt Pending */
#define MIP_STIP   (1UL << 5)   /* Supervisor Timer Interrupt Pending */
#define MIP_MTIP   (1UL << 7)   /* Machine Timer Interrupt Pending */

#define MIP_UEIP   (1UL << 8)   /* User External Interrupt Pending */
#define MIP_SEIP   (1UL << 9)   /* Supervisor External Interrupt Pending */
#define MIP_MEIP   (1UL << 11)  /* Machine External Interrupt Pending */

/* mie 的 bit 位置和 mip 一致 */
#define MIE_USIE   MIP_USIP
#define MIE_SSIE   MIP_SSIP
#define MIE_MSIE   MIP_MSIP

#define MIE_UTIE   MIP_UTIP
#define MIE_STIE   MIP_STIP
#define MIE_MTIE   MIP_MTIP

#define MIE_UEIE   MIP_UEIP
#define MIE_SEIE   MIP_SEIP
#define MIE_MEIE   MIP_MEIP

/*
 * S 模式视角下的 sie / sip 位
 * 注意：sie/sip 的寄存器名字不同，但 bit layout 复用 mie/mip。
 */
#define SIP_SSIP   MIP_SSIP
#define SIP_STIP   MIP_STIP
#define SIP_SEIP   MIP_SEIP

#define SIE_SSIE   MIE_SSIE
#define SIE_STIE   MIE_STIE
#define SIE_SEIE   MIE_SEIE

/* ===================== 异常 / 中断 code ===================== */
/* 参考: RISC-V Privileged Spec Table "Cause Register". */

/* 异常 codes（xCAUSE MSB=0 时），Machine/Supervisor 共用 */
enum {
    EXC_INST_MISALIGNED   = 0,
    EXC_INST_ACCESS_FAULT = 1,
    EXC_ILLEGAL_INSTR     = 2,
    EXC_BREAKPOINT        = 3,
    EXC_LOAD_MISALIGNED   = 4,
    EXC_LOAD_ACCESS_FAULT = 5,
    EXC_STORE_MISALIGNED  = 6,
    EXC_STORE_ACCESS_FAULT= 7,
    EXC_ENV_CALL_U        = 8,
    EXC_ENV_CALL_S        = 9,
    /* 10 reserved */
    EXC_ENV_CALL_M        = 11,
    /* 其余用到时再补 */
};

/*
 * 中断 codes（xCAUSE MSB=1 时）
 *
 * 注意：code 的含义与“触发特权级”有关：
 *   - 在 Machine 上：code=3/7/11 对应 M 级软/时钟/外部中断；
 *   - 在 Supervisor 上：code=1/5/9 对应 S 级软/时钟/外部中断。
 * 也就是说，scause == (MCAUSE_INT | IRQ_TIMER_S) 表示 S 模式定时器中断。
 */

/* U/S/M 级中断 code（编码在低位） */
enum {
    IRQ_SOFT_U   = 0,   /* User Software Interrupt */
    IRQ_SOFT_S   = 1,   /* Supervisor Software Interrupt */
    IRQ_SOFT_M   = 3,   /* Machine Software Interrupt */

    IRQ_TIMER_U  = 4,   /* User Timer Interrupt */
    IRQ_TIMER_S  = 5,   /* Supervisor Timer Interrupt */
    IRQ_TIMER_M  = 7,   /* Machine Timer Interrupt */

    IRQ_EXT_U    = 8,   /* User External Interrupt */
    IRQ_EXT_S    = 9,   /* Supervisor External Interrupt */
    IRQ_EXT_M    = 11,  /* Machine External Interrupt */
};

/* ===================== 一些常用 CSR 访问 helpers ===================== */
/* 统一用这些宏，避免到处写内联汇编 */

#define csr_read(csr)                            \
    ({ reg_t __tmp;                              \
       __asm__ volatile ("csrr %0, " #csr        \
                         : "=r"(__tmp));         \
       __tmp; })

#define csr_write(csr, val)                      \
    do {                                         \
        reg_t __v = (reg_t)(val);                \
        __asm__ volatile ("csrw " #csr ", %0"    \
                         :: "r"(__v));           \
    } while (0)

/* mtvec 低 2 bit 是 mode */
#define MTVEC_MODE_MASK     0x3UL
#define MTVEC_MODE_DIRECT   0x0UL
#define MTVEC_MODE_VECTORED 0x1UL

#endif /* RISCV_CSR_H */
