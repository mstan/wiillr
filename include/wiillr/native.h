/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_NATIVE_H
#define WIILLR_NATIVE_H
#include "cpu/cpu.h"
#include "cpu/overlay_module.h"
void wii_native_install(void);
const GcnOverlayModule* wii_native_module(void);
bool wii_native_call(CPUState* cpu,u32 address);
void wii_native_set_stop_pc(u32);
u64 wii_native_chained_dispatches(void);
void wii_native_summary(void);
#endif
