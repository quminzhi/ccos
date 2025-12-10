#ifndef BAREMETAL_KERNEL_H
#define BAREMETAL_KERNEL_H

#ifndef KERNEL_BUILD_TYPE
#define KERNEL_BUILD_TYPE "debug"
#endif  // KERNEL_BUILD_TYPE

void kernel_main(long hartid, long dtb_pa);

#endif  // BAREMETAL_KERNEL_H
