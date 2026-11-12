/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_GX_H
#define WIILLR_GX_H
#include "wiillr/machine.h"
#include "gcnllr/native_gx.h"
#include "gcnllr/native_video.h"
typedef struct {
    void* library;
    void* output;
    const GcnllrNativeGxApi* api;
    const GcWiiNativeVideoApi* video;
    WiiMachine* machine;
    CPUState* cpu;
    u64 copies,scanouts,last_service;
    u32 scanout_address,scanout_width,scanout_height,scanout_stride;
    bool visible,input_sent,mouse_active,quit;
    u64 input_field;
    WiiBluetoothInput input;
    /* Optional host controls; the renderer's mouse IR path remains shared. */
    void* host_input_user;
    void (*host_input)(void*,GcnllrPad*);
    void (*host_input_stop)(void*);
} WiiGx;
bool wii_gx_start(WiiGx*,WiiMachine*,CPUState*,const char* library,const char* data,const char* cache);
bool wii_gx_tick(WiiGx*);
void wii_gx_stop(WiiGx*);
#endif
