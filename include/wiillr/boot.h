/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_BOOT_H
#define WIILLR_BOOT_H
#include "cpu/cpu.h"
/* Development NAND-title launch boundary for IOS4 v3. This models its public
 * memory contract; it does not execute IOS, boot0, boot1 or boot2. Apply before
 * loading the title, whose entry stub occupies the same low-memory region. */
bool wii_ios4_boot_memory(CPUState* cpu);
#endif
