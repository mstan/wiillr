/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_NATIVE_H
#define WIILLR_NATIVE_H
#include "cpu/cpu.h"
void wii_native_install(void);
bool wii_native_call(CPUState* cpu,u32 address);
void wii_native_summary(void);
#endif
