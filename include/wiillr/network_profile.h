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
    data[4]=1; /* wireless interface */
    data[8]=0x86; /* selected, automatic IP/DNS; NOT connection-test OK */
    data[8+26]=0x05;data[8+27]=0xdc; /* MTU 1500 */
    memcpy(data+8+1988,"wiillr",6);data[8+2021]=6;
    return true;
}
#endif
