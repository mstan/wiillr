/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_PERIPHERALS_H
#define WIILLR_PERIPHERALS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "wiillr/ios.h"
typedef enum {WII_USB_NONE=0,WII_USB_KEYBOARD=1,WII_USB_MASS_STORAGE=2} WiiUsbKind;
typedef struct {WiiUsbKind kind;const char* image_path;bool read_only;} WiiUsbPortConfig;
typedef struct {const char* image_path;bool read_only;} WiiSdConfig;
typedef struct {WiiUsbPortConfig usb[2];WiiSdConfig sd;} WiiPeripheralsConfig;
/* Default: USB1 keyboard, USB2 empty, SD empty. Paths are UTF-8 raw sector
 * images, never directories or physical devices. Empty paths mean no card.
 * Call configure/flush/eject on the emulation owner thread while paused.
 * Configure copies paths and validates all files before publishing changes;
 * failure preserves the prior attachments. Existing files are never formatted.
 * Get returns borrowed paths valid until configure/eject/destroy. */
void wii_peripherals_defaults(WiiPeripheralsConfig*);
bool wii_ios_native_configure_peripherals(WiiIosNative*,const WiiPeripheralsConfig*,char* error,size_t);
bool wii_ios_native_get_peripherals(const WiiIosNative*,WiiPeripheralsConfig*);
bool wii_ios_native_flush_peripherals(WiiIosNative*,char* error,size_t);
/* slot 0/1 = USB1/USB2, 2 = SD. Successful eject flushes before disconnect. */
bool wii_ios_native_eject_peripheral(WiiIosNative*,unsigned slot,char* error,size_t);
#ifdef __cplusplus
}
#endif
#endif
