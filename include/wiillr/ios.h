/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_IOS_H
#define WIILLR_IOS_H
#include "cpu/cpu.h"
#include "wiillr/ipc.h"
#include "di/di.h"
#include "wiillr/fs.h"
typedef struct {
    u8 descriptors[24],phase;
    u32 message,command;
    s32 result;
    u64 ready_at,operations;
    u32 event_message,event_output,event_fd;
    bool event_waiting,event_ready;
    GcnDi* di; /* borrowed device; consumer owns storage, time and disc paths */
    bool di_initialized;
    WiiFs* fs; /* borrowed isolated NAND profile */
    u32 fs_handles[24];
} WiiIosNative;
/* Experimental native service boundary. Only implemented operations may be
 * acknowledged. False stops discovery with the original request intact. */
void wii_ios_native_init(WiiIosNative*);
bool wii_ios_native_tick(WiiIosNative*,WiiIpc*,CPUState*);
bool wii_ios_native_stm_event(WiiIosNative*,CPUState*,u32 event);
#endif
