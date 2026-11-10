/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_HOLLYWOOD_H
#define WIILLR_HOLLYWOOD_H
#include <stdint.h>
#include <stdbool.h>
/* Selected NAND-title handoff, not power-on reset: extended VI/AID/CP/DI
 * addresses and the IPC interrupt are already enabled by the IOS provider.
 * Audio PLL fields follow the original __AIClockInit programming sequence:
 * https://github.com/TwilitRealm/dusklight/blob/main/libs/revolution/src/os/OSAudioSystem.c
 * Unsupported compatibility transitions and PLL presets remain explicit misses. */
typedef struct {uint32_t compat,audio_pll,audio_control;} WiiHollywood;
static inline uint32_t wii_audio_pll_preset(bool compat_b) {
    return compat_b ? (300u<<18)|(1023u<<6)|14u : (281u<<18)|(63u<<6);
}
static inline void wii_hollywood_init(WiiHollywood* h) {
    h->compat=0x00053102u;h->audio_pll=wii_audio_pll_preset(true);h->audio_control=0xc0000000u;
}
static inline bool wii_hollywood_audio_clock(const WiiHollywood* h) {
    return h->audio_control==0xc0000000u && h->audio_pll==wii_audio_pll_preset((h->compat&0x100u)!=0);
}
static inline bool wii_hollywood_read(const WiiHollywood* h,uint32_t address,uint32_t* value) {
    switch(address&~0x00800000u) {
    case 0x0d000180u:*value=h->compat;return true;
    case 0x0d0001ccu:*value=h->audio_pll;return true;
    case 0x0d0001d0u:*value=h->audio_control;return true;
    default:return false;
    }
}
static inline bool wii_hollywood_write(WiiHollywood* h,uint32_t address,uint32_t value) {
    switch(address&~0x00800000u) {
    case 0x0d000180u:
        if((value^h->compat)&~0x100u)return false;
        h->compat=value;return true;
    case 0x0d0001ccu:
        if(value!=wii_audio_pll_preset(false) && value!=wii_audio_pll_preset(true))return false;
        h->audio_pll=value;return true;
    case 0x0d0001d0u:
        if(value&~0xc0000000u)return false;
        h->audio_control=value;return true;
    default:return false;
    }
}
#endif
