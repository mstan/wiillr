/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MM9_DLC_H
#define WIILLR_MM9_DLC_H
#include "wiillr/gx.h"
void wii_mm9_dlc_bind(WiiGx*);
/* Verified, ticket-permitted Proto Man pack for the optional character mod. */
bool wii_mm9_dlc_available(void);
bool wii_mm9_dlc_dispatch(CPUState*,u32 address);
void wii_mm9_dlc_invalidate(u32 address,u32 size);
#endif
