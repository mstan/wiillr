/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_PERIPHERALS_INTERNAL_H
#define WIILLR_PERIPHERALS_INTERNAL_H
#include "wiillr/peripherals.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct WiiPeripherals WiiPeripherals;
WiiPeripherals* wii_peripherals_create(void);
void wii_peripherals_destroy(WiiPeripherals*);
bool wii_peripherals_configure(WiiPeripherals*,const WiiPeripheralsConfig*,char*,size_t);
void wii_peripherals_get(const WiiPeripherals*,WiiPeripheralsConfig*);
bool wii_peripherals_flush(WiiPeripherals*,char*,size_t);
bool wii_peripherals_eject(WiiPeripherals*,unsigned,char*,size_t);
bool wii_peripherals_keyboard(const WiiPeripherals*);
bool wii_peripherals_sd_present(const WiiPeripherals*);
void wii_peripherals_keyboard_input(WiiPeripherals*,const WiiKeyboardReport*);
void wii_peripherals_open(WiiPeripherals*,u32 fd,u8 device);
void wii_peripherals_close(WiiPeripherals*,u32 fd);
bool wii_peripherals_poll(WiiPeripherals*,CPUState*,u32* message,u32* command,s32* result);
/* Device ids: 13 SD, 15 USB host, 16/17 USB port1/2. Returns 1 for a
 * completed operation (including explicit guest error), 0 if unsupported. */
int wii_peripherals_submit(WiiPeripherals*,CPUState*,const u8* request,u32 message,u8 device,s32* result);
#ifdef __cplusplus
}
#endif
#endif
