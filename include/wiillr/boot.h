/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_BOOT_H
#define WIILLR_BOOT_H
#include "cpu/cpu.h"
/* Development NAND-title launch boundary for IOS4 v3. This models its public
 * memory contract; it does not execute IOS, boot0, boot1 or boot2. Apply before
 * loading the title, whose entry stub occupies the same low-memory region. */
bool wii_ios4_boot_memory(CPUState* cpu);
/* Native development contract for the requested IOS, not original ARM boot. */
bool wii_native_ios_boot_memory(CPUState* cpu,u32 major);
#endif
