/* SPDX-License-Identifier: GPL-3.0-or-later */
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
/* Four real Remote sources, each with its own normal HCI/L2CAP/HID state.
 * The balance-board slot is separate and is not exposed as a fifth Remote. */
bool wii_bluetooth_set_input_indexed(WiiBluetooth*,u32 index,const WiiBluetoothInput*);
/* First-use pairing for this selected Remote via ordinary Sync/PIN/key
 * exchange. Already matching keys are retained. No source means no reports. */
bool wii_bluetooth_pair_remote(WiiBluetooth*,u32 index);
/* Physical console Sync-button press, delivered as the controller's vendor event. */
bool wii_bluetooth_press_sync(WiiBluetooth*);
/* Optional first-use policy: pair remote 0 through ordinary Sync/PIN/key exchange.
   Existing remote or controller keys make this a no-op; false means invalid/error. */
bool wii_bluetooth_pair_first_remote(WiiBluetooth*);
/* Actual USB requests are asynchronous. False preserves an unsupported request. */
bool wii_bluetooth_submit(WiiBluetooth*,u32 message);
bool wii_bluetooth_poll(WiiBluetooth*,u32* message,s32* result,bool* ready);
/* Non-consuming distance to the next report, endpoint drain or completion. */
u64 wii_bluetooth_deadline(const WiiBluetooth*,u64 cycles);
#ifdef __cplusplus
}
#endif
#endif
