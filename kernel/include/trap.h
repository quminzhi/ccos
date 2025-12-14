#ifndef TRAP_H
#define TRAP_H

#include <stddef.h>
#include <stdint.h>

#define ADVANCE_SEPC()   \
  do {                   \
    tf->sepc = sepc + 4; \
  } while (0)

struct trapframe {
  uint64_t ra;       /* 0 */
  uint64_t sp;       /* 8 */
  uint64_t gp;       /* 16 */
  uint64_t tp;       /* 24 */
  uint64_t t0;       /* 32 */
  uint64_t t1;       /* 40 */
  uint64_t t2;       /* 48 */
  uint64_t s0;       /* 56 */
  uint64_t s1;       /* 64 */
  uint64_t a0;       /* 72 */
  uint64_t a1;       /* 80 */
  uint64_t a2;       /* 88 */
  uint64_t a3;       /* 96 */
  uint64_t a4;       /* 104 */
  uint64_t a5;       /* 112 */
  uint64_t a6;       /* 120 */
  uint64_t a7;       /* 128 */
  uint64_t s2;       /* 136 */
  uint64_t s3;       /* 144 */
  uint64_t s4;       /* 152 */
  uint64_t s5;       /* 160 */
  uint64_t s6;       /* 168 */
  uint64_t s7;       /* 176 */
  uint64_t s8;       /* 184 */
  uint64_t s9;       /* 192 */
  uint64_t s10;      /* 200 */
  uint64_t s11;      /* 208 */
  uint64_t t3;       /* 216 */
  uint64_t t4;       /* 224 */
  uint64_t t5;       /* 232 */
  uint64_t t6;       /* 240 */
  uint64_t sepc;     /* 248 */
  uint64_t sstatus;  /* 256 */
  uint64_t scause;   /* 264 */
  uint64_t stval;    /* 272 */
  uint64_t pad;      /* 280 (让 sizeof=288) */
};

_Static_assert(offsetof(struct trapframe, ra) == 0, "tf offset mismatch");
_Static_assert(offsetof(struct trapframe, stval) == 272, "tf offset mismatch");
_Static_assert(sizeof(struct trapframe) == 288, "tf size mismatch");

void trap_init(void);
struct trapframe *trap_entry_c(struct trapframe *tf);

#endif
