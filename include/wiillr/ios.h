/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_IOS_H
#define WIILLR_IOS_H
#include "cpu/cpu.h"
#include "wiillr/ipc.h"
typedef struct {
    u8 descriptors[24],phase;
    u32 message,command;
    s32 result;
    u64 ready_at,operations;
} WiiIosNative;
/* Experimental native service boundary. Only implemented operations may be
 * acknowledged. False stops discovery with the original request intact. */
void wii_ios_native_init(WiiIosNative*);
bool wii_ios_native_tick(WiiIosNative*,WiiIpc*,CPUState*);
#endif
