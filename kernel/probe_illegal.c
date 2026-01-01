/* probe_illegal.c */

#include <stdint.h>

#include "cpu.h"
#include "log.h"
#include "probe_illegal.h"
#include "platform.h"
#include "riscv_csr.h"
#include "trap.h"

/* Temporary trapframe for probe-time illegal instruction handling. */
static struct trapframe g_probe_tf;

static struct trapframe *
probe_enter(void) {
  cpu_t *c = cpu_this();
  struct trapframe *old = c->cur_tf;
  c->cur_tf = &g_probe_tf;
  return old;
}

static void
probe_exit(struct trapframe *old) {
  cpu_this()->cur_tf = old;
}

#define PROBE_INSN(tag, insn_const)                \
  do {                                             \
    trap_illegal_probe_clear();                    \
    trap_illegal_probe_enable();                   \
    asm volatile(                                  \
        ".option push\n"                           \
        ".option norvc\n"                          \
        ".4byte " #insn_const "\n"                 \
        ".option pop\n"                            \
        :                                          \
        :                                          \
        : "memory");                               \
    trap_illegal_probe_disable();                  \
    pr_info("  %s: %s", tag,                       \
            trap_illegal_probe_hit() ? "ILLEGAL" : "OK"); \
  } while (0)

void
probe_privileged_isa(void) {
  struct trapframe *old = probe_enter();

  pr_info("Probing privileged ISA instructions...");

  /* sfence.vma (standard) / sfence.vm (legacy) */
  PROBE_INSN("sfence.vma", 0x12000073);
  PROBE_INSN("sfence.vm",  0x10000073);

  /* satp access (TVM may forbid these in S-mode) */
  reg_t satp_val = 0;
  trap_illegal_probe_clear();
  trap_illegal_probe_enable();
  asm volatile("csrr %0, satp" : "=r"(satp_val) : : "memory");
  trap_illegal_probe_disable();
  pr_info("  csrr satp: %s", trap_illegal_probe_hit() ? "ILLEGAL" : "OK");

  if (!trap_illegal_probe_hit()) {
    trap_illegal_probe_clear();
    trap_illegal_probe_enable();
    asm volatile("csrw satp, %0" : : "r"(satp_val) : "memory");
    trap_illegal_probe_disable();
    pr_info("  csrw satp: %s", trap_illegal_probe_hit() ? "ILLEGAL" : "OK");
  } else {
    pr_info("  csrw satp: SKIP");
  }

  /* fence.i (zifencei) */
  PROBE_INSN("fence.i", 0x0000100F);

  /* wfi probe disabled: on this platform it can leave stale pending state that
   * interacts badly before the scheduler is up. Keep the log for clarity.
   */
  pr_info("  wfi: SKIP (disabled for bring-up)");

  probe_exit(old);
}
