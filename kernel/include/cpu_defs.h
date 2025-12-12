#pragma once

/* for assembly use */

#ifndef MAX_HARTS
#define MAX_HARTS 4
#endif

#ifndef KSTACK_SIZE
#define KSTACK_SIZE 4096
#endif

#define CPU_KSTACK_TOP_OFF 0
#define CPU_CUR_TF_OFF     8
