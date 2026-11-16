/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_GX_SESSION_H
#define WIILLR_GX_SESSION_H
#include "wiillr/gx.h"
#include "gcnllr/native_session.h"
typedef struct {
    void* library;
    const GcWiiNativeSessionApi* api;
    u64 window_identity;
} WiiGxHandoff;
/* 1 retains the current module/window across the existing stop/start; 0 is
 * the legacy/software floor; -1 rejects a broken retention contract. */
int wii_gx_handoff_begin(WiiGx*,const char* library,WiiGxHandoff*);
/* Cancel a failed handoff before releasing its module lease. A successful
 * restart must still name the original physical window. */
bool wii_gx_handoff_end(WiiGxHandoff*,bool success);
#endif
