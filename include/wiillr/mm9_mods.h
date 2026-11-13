/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MM9_MODS_H
#define WIILLR_MM9_MODS_H
#include "wiillr/gx.h"
#include <stddef.h>

/* Launch configuration. Both optional features default off. */
void wii_mm9_mods_configure(bool characters,bool weapons);
void wii_mm9_mods_bind(WiiGx*);
/* Called before the original native entry. True means PC was redirected. */
void wii_mm9_mods_invalidate(u32 address,u32 size);
bool wii_mm9_mods_dispatch(CPUState*,u32 pc);
/* Original stage pause predicate. Only freezes world update; native rendering,
 * audio and host controls stay live while the owned teleport effect advances. */
bool wii_mm9_mods_stage_frozen(CPUState*);
/* Sample once per VI field; 1 = previous, 2 = next. No host button remapping. */
void wii_mm9_mods_host_actions(unsigned held);
void wii_mm9_mods_test_actions(unsigned held);
int wii_mm9_mods_status(char*,size_t);
#endif
