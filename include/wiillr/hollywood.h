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
typedef struct {uint32_t compat,audio_pll,audio_control,video_regs[3],vi_solid;} WiiHollywood;
static inline uint32_t wii_audio_pll_preset(bool compat_b) {
    return compat_b ? (300u<<18)|(1023u<<6)|14u : (281u<<18)|(63u<<6);
}
static inline void wii_hollywood_init(WiiHollywood* h) {
    h->compat=0x00053102u;h->audio_pll=wii_audio_pll_preset(true);h->audio_control=0xc0000000u;
    for(unsigned n=0;n<3;n++)h->video_regs[n]=0;
    h->vi_solid=0;
}
static inline bool wii_hollywood_audio_clock(const WiiHollywood* h) {
    return h->audio_control==0xc0000000u && h->audio_pll==wii_audio_pll_preset((h->compat&0x100u)!=0);
}
static inline bool wii_hollywood_read(const WiiHollywood* h,uint32_t address,uint32_t* value) {
    switch(address&~0x00800000u) {
    case 0x0d00001cu:*value=h->video_regs[0];return true;
    case 0x0d000020u:*value=h->video_regs[1];return true;
    case 0x0d000024u:*value=h->vi_solid;return true;
    case 0x0d000028u:*value=h->video_regs[2];return true;
    case 0x0d000180u:*value=h->compat;return true;
    case 0x0d0001ccu:*value=h->audio_pll;return true;
    case 0x0d0001d0u:*value=h->audio_control;return true;
    default:return false;
    }
}
/* STM mask/set transaction. Normal scanout with forced dimming disabled is
 * implemented; enabling dimming or changing the other two video controls must
 * stop until their actual presentation effects exist. Validate atomically. */
static inline bool wii_hollywood_video_update(WiiHollywood* h,const uint32_t set[3],
    const uint32_t keep[3],uint32_t old[3],uint32_t* flags) {
    uint32_t next[3];
    for(unsigned n=0;n<3;n++)next[n]=(h->video_regs[n]&keep[n])|set[n];
    if((next[0]&~0x3fu) || next[1]!=h->video_regs[1] || next[2]!=h->video_regs[2])return false;
    *flags=0;
    for(unsigned n=0;n<3;n++){old[n]=h->video_regs[n];h->video_regs[n]=next[n];if(keep[n]!=0xffffffffu)*flags|=1u<<n;}
    return true;
}
static inline bool wii_hollywood_write(WiiHollywood* h,uint32_t address,uint32_t value) {
    switch(address&~0x00800000u) {
    case 0x0d000024u:
        /* Y/U/V may be configured while solid output is disabled. Bit zero
         * enables replacement of scanout; keep that transition unsupported
         * until its actual presentation effect exists. Bits1..7 are reserved. */
        if(value&0xffu)return false;
        h->vi_solid=value;return true;
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
