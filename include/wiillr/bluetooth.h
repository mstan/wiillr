/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_BLUETOOTH_H
#define WIILLR_BLUETOOTH_H
#ifdef __cplusplus
extern "C" {
#endif
#include "cpu/cpu.h"
typedef struct WiiBluetooth WiiBluetooth;
typedef struct {void* user;bool (*load)(void*,u8*,u32,u32*);bool (*save)(void*,const u8*,u32);} WiiBluetoothStore;
typedef struct {u16 buttons,accel[3];u16 pointer_x,pointer_y;bool pointing,present,pairing;} WiiBluetoothInput;
WiiBluetooth* wii_bluetooth_create(CPUState*,const u32* gpio_out,const WiiBluetoothStore*);
void wii_bluetooth_destroy(WiiBluetooth*);
void wii_bluetooth_close(WiiBluetooth*);
bool wii_bluetooth_set_input(WiiBluetooth*,const WiiBluetoothInput*);
/* Physical console Sync-button press, delivered as the controller's vendor event. */
bool wii_bluetooth_press_sync(WiiBluetooth*);
/* Actual USB requests are asynchronous. False preserves an unsupported request. */
bool wii_bluetooth_submit(WiiBluetooth*,u32 message);
bool wii_bluetooth_poll(WiiBluetooth*,u32* message,s32* result,bool* ready);
/* Non-consuming distance to the next report, endpoint drain or completion. */
u64 wii_bluetooth_deadline(const WiiBluetooth*,u64 cycles);
#ifdef __cplusplus
}
#endif
#endif
