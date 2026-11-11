/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_DOL_H
#define WIILLR_DOL_H
#include "cpu/cpu.h"
#include <stddef.h>

/* Validate the entire image before writing RAM. Entry retains its original
 * physical/cached/uncached namespace. Clear BSS before loading initialized
 * sections: packed executables can deliberately overlap the two. */
bool wii_dol_validate(CPUState* cpu, const u8* data, size_t size, u32* entry);
bool wii_dol_load(CPUState* cpu, const u8* data, size_t size, u32* entry);
#endif
