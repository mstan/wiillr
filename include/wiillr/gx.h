/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_GX_H
#define WIILLR_GX_H
#include "wiillr/machine.h"
#include "gcnllr/native_gx.h"
typedef struct {
    void* library;
    const GcnllrNativeGxApi* api;
    WiiMachine* machine;
    CPUState* cpu;
    u64 copies,scanouts,last_service;
} WiiGx;
bool wii_gx_start(WiiGx*,WiiMachine*,CPUState*,const char* library,const char* data,const char* cache);
bool wii_gx_tick(WiiGx*);
void wii_gx_stop(WiiGx*);
#endif
