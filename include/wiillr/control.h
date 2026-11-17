/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_CONTROL_H
#define WIILLR_CONTROL_H
#include "wiillr/gx.h"
#include "cpu/overlay_module.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiControl WiiControl;
WiiControl* wii_control_start(CPUState*,WiiMachine*,WiiGx*,WiiBluetoothInput*,bool* input_pending,
                            const GcnOverlayModule* native_module);
u64 wii_control_deadline(const WiiControl*,u64 default_deadline);
/* False means paused: do not execute or advance guest time. */
bool wii_control_poll(WiiControl*);
bool wii_control_quit_requested(WiiControl*);
void wii_control_stop(WiiControl*);
#ifdef __cplusplus
}
#endif
#endif
