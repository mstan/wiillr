/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_BLUETOOTH_H
#define WIILLR_BLUETOOTH_H
#ifdef __cplusplus
extern "C" {
#endif
#include "cpu/cpu.h"
typedef struct WiiBluetooth WiiBluetooth;
WiiBluetooth* wii_bluetooth_create(CPUState*);
void wii_bluetooth_destroy(WiiBluetooth*);
/* Actual USB requests are asynchronous. False preserves an unsupported request. */
bool wii_bluetooth_submit(WiiBluetooth*,u32 message);
bool wii_bluetooth_poll(WiiBluetooth*,u32* message,s32* result,bool* ready);
#ifdef __cplusplus
}
#endif
#endif
