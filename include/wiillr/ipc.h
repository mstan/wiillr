/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_IPC_H
#define WIILLR_IPC_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t ppc_message, arm_message;
    uint32_t ppc_flags, ppc_mask, arm_flags, arm_mask;
    uint8_t x, y, ix, iy;
    uint64_t interrupt_at;
    bool interrupt_scheduled;
} WiiIpc;
void wii_ipc_init(WiiIpc* ipc);
bool wii_ipc_read(WiiIpc* ipc, uint32_t offset, uint32_t* value);
bool wii_ipc_write(WiiIpc* ipc, uint32_t offset, uint32_t value, uint64_t ppc_cycles);
void wii_ipc_tick(WiiIpc* ipc, uint64_t ppc_cycles);
bool wii_ipc_ppc_interrupt(const WiiIpc* ipc);
bool wii_ipc_arm_interrupt(const WiiIpc* ipc);
#endif
