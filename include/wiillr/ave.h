/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_AVE_H
#define WIILLR_AVE_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint8_t registers[256],valid[256],index,byte,bits,state,stage;
    bool scl,sda,drive_low,selected,read,ack,failed;
    uint64_t transactions,writes;
} WiiAve;
void wii_ave_init(WiiAve*);
/* GPIO clock/data are sampled on transitions, including direction changes. */
bool wii_ave_gpio(WiiAve*,uint32_t out,uint32_t direction);
uint32_t wii_ave_pins(const WiiAve*);
#endif
