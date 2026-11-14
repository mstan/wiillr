/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_NETWORK_H
#define WIILLR_NETWORK_H
#ifdef __cplusplus
extern "C" {
#endif
#include "cpu/cpu.h"
typedef struct WiiNetwork WiiNetwork;
WiiNetwork* wii_network_create(const u8 mac[6]);
void wii_network_destroy(WiiNetwork*);
/* Host IPv4 interface availability, not an Internet/connection-test result. */
bool wii_network_link(WiiNetwork*);
/* Existing IOS request ABI: 0 unsupported, 1 synchronous reply, 2 ACK and
 * pending completion. Guest input is captured before ACK; output is published
 * exclusively through the bus when complete. No host call blocks emulation. */
int wii_network_submit(WiiNetwork*,CPUState*,u32 message,s32* result);
bool wii_network_poll(WiiNetwork*,CPUState*,u32* message,u32* command,s32* result);
void wii_network_close_device(WiiNetwork*,u32 fd);
#ifdef __cplusplus
}
#endif
#endif
