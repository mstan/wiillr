/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_GX_H
#define WIILLR_GX_H
#include "wiillr/machine.h"
#include "gcnllr/native_gx.h"
#include "gcnllr/native_video.h"
#include "gcnllr/native_presentation.h"
#include "gcnllr/native_resolution.h"
typedef struct {
    void* library;
    void* output;
    const GcnllrNativeGxApi* api;
    const GcWiiNativeVideoApi* video;
    const GcWiiNativePresentationApi* presentation;
    const GcWiiNativeResolutionApi* resolution;
    WiiMachine* machine;
    CPUState* cpu;
    u64 title;
    u64 copies,scanouts,last_service;
    u32 scanout_address,scanout_width,scanout_height,scanout_stride;
    bool visible,input_sent,mouse_active,quit;
    u64 input_field;
    WiiBluetoothInput input;
    /* Optional host controls; the renderer's mouse IR path remains shared. */
    void* host_input_user;
    void (*host_input)(void*,GcnllrPad*);
    void (*host_input_stop)(void*);
    bool (*host_settings)(void*); /* optional owner-thread in-session UI */
    bool (*host_review)(void*,bool,u32); /* paused test window visibility/volume */
    bool host_input_capture; /* host menu holds input until its controls release */
    bool scripted_input; /* setup owns input until the explicit review handoff */
} WiiGx;
/* Optional four-player host input. The callback returns PRESENT=1/FOCUSED=2;
 * zero is an unassigned/disconnected Remote. P1 mouse remains renderer-owned.
 * Registration is private and retired by stop; WiiGx's layout is unchanged. */
#define WIILLR_GX_INPUT_PRESENT 1u
#define WIILLR_GX_INPUT_FOCUSED 2u
typedef unsigned (*WiiGxPlayerInput)(void*,unsigned,GcnllrPad*);
bool wii_gx_set_player_input(WiiGx*,WiiGxPlayerInput,void*);
bool wii_gx_start(WiiGx*,WiiMachine*,CPUState*,const char* library,const char* data,const char* cache);
bool wii_gx_tick(WiiGx*);
void wii_gx_stop(WiiGx*);
#endif
