/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIILLR_BLUETOOTH_H
#define WIILLR_BLUETOOTH_H
#ifdef __cplusplus
extern "C" {
#endif
#include "cpu/cpu.h"
typedef struct WiiBluetooth WiiBluetooth;
typedef struct {void* user;bool (*load)(void*,u8*,u32,u32*);bool (*save)(void*,const u8*,u32);} WiiBluetoothStore;
typedef struct {bool attached;u8 stick_x,stick_y,buttons;u16 accel[3];} WiiNunchukInput;
/* Nunchuk buttons: bit 0 Z, bit 1 C (pressed=1 at this host interface).
 * Acceleration retains all ten wire bits; stick neutral is 128/128. */
typedef struct {u16 buttons,accel[3];u16 pointer_x,pointer_y;bool pointing,present,pairing;WiiNunchukInput nunchuk;} WiiBluetoothInput;
static inline WiiNunchukInput wii_nunchuk_neutral(void) {
    const WiiNunchukInput value={true,128,128,0,{512,512,716}};return value;
}
static inline bool wii_nunchuk_equal(const WiiNunchukInput* a,const WiiNunchukInput* b) {
    return a->attached==b->attached && a->stick_x==b->stick_x && a->stick_y==b->stick_y &&
        a->buttons==b->buttons && a->accel[0]==b->accel[0] && a->accel[1]==b->accel[1] && a->accel[2]==b->accel[2];
}
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
