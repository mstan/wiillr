/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_KEYBOARD_H
#define WIILLR_KEYBOARD_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {bool connected;uint8_t modifiers,keys[6];} WiiKeyboardReport;
typedef struct {
    WiiKeyboardReport report;
    uint8_t events[32][16];unsigned head,count;
} WiiKeyboard;
bool wii_keyboard_can_update(const WiiKeyboard*,const WiiKeyboardReport*);
bool wii_keyboard_update(WiiKeyboard*,const WiiKeyboardReport*);
bool wii_keyboard_pop(WiiKeyboard*,uint8_t event[16]);
/* Owner-thread staging for the existing launcher input callback. No SDL or
 * native renderer dependency: an absent host publisher stays disconnected. */
void wii_keyboard_host_begin(bool typing);
void wii_keyboard_host_keys(const bool*,int count,bool connected);
bool wii_keyboard_host_captured(void);
void wii_keyboard_host_end(bool enabled,WiiKeyboardReport*);
void wii_keyboard_host_reset(void);
#endif
