/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_BOOT_H
#define WIILLR_BOOT_H
#include "cpu/cpu.h"
#include "wiillr/disc.h"
/* Development NAND-title launch boundary for IOS4 v3. This models its public
 * memory contract; it does not execute IOS, boot0, boot1 or boot2. Apply before
 * loading the title, whose entry stub occupies the same low-memory region. */
bool wii_ios4_boot_memory(CPUState* cpu);
/* Native development contract for the requested IOS, not original ARM boot. */
bool wii_native_ios_boot_memory(CPUState* cpu,u32 major);
/* Explicit disc DOL-entry development boundary. Reserves the live FST from
 * the game's MEM1 arena and records real disc/partition capacity metadata.
 * Does not execute an apploader or establish authenticated disc startup. */
bool wii_disc_boot_memory(CPUState*,WiiDisc*);
#endif
