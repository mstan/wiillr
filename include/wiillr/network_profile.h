/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_NETWORK_PROFILE_H
#define WIILLR_NETWORK_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* NCD config.dat: eight-byte header, three 2332-byte connection records.
 * Only replace the former generated, entirely empty offline default. A
 * disabled or unselected real connection still belongs to the player. */
static inline bool wii_network_seed_config(uint8_t* data,size_t size) {
    if(!data || size!=7004)return false;
    for(size_t n=0;n<size;++n)if(data[n])return false;
    data[4]=2; /* wired interface, backed by ordinary host routing */
    data[8]=0x87; /* selected, wired, automatic IP/DNS; NOT connection-test OK */
    data[8+26]=0x05;data[8+27]=0xdc; /* MTU 1500 */
    return true;
}
/* Only call while building a NEW copied Menu profile. Recognize the exact
 * former generated Wi-Fi template; a custom/edited connection is preserved. */
static inline bool wii_network_seed_fresh_menu_config(uint8_t* data,size_t size) {
    if(!data || size!=7004)return false;
    uint8_t former[7004]={0};former[4]=1;former[8]=0x86;
    former[8+26]=0x05;former[8+27]=0xdc;
    memcpy(former+8+1988,"wiillr",6);former[8+2021]=6;
    if(!memcmp(data,former,size))memset(data,0,size);
    return wii_network_seed_config(data,size);
}
/* A saved external SSID is never reported as associated with the virtual AP.
 * Wired selection can use ordinary host routing without radio association. */
static inline bool wii_network_host_profile_selected(const uint8_t* data,size_t size) {
    if(!data || size!=7004)return false;
    for(unsigned n=0;n<3;++n) {
        const uint8_t* connection=data+8+n*2332;
        if(!(connection[0]&0x80))continue;
        if(data[4]==2 && (connection[0]&1))return true;
        if(data[4]==1 && !(connection[0]&1) && connection[2021]==6 &&
           !memcmp(connection+1988,"wiillr",6))return true;
    }
    return false;
}
#endif
