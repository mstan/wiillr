/* SPDX-License-Identifier: MIT */
/* Hollywood mailbox semantics: two request bits and two reply bits, each
 * cleared by its receiving processor. No IOS result is synthesized here.
 * Register layout and hardware-tested ~100-TB response interrupt delay:
 * https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/WII_IPC.cpp
 * https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/HW/WII_IPC.h
 */
#include "wiillr/ipc.h"
#include <string.h>
#define PPC_IPC 0x40000000u
#define ARM_IPC 0x80000000u

void wii_ipc_init(WiiIpc* ipc) {
    memset(ipc, 0, sizeof *ipc);
}

void wii_ipc_tick(WiiIpc* ipc, uint64_t cycles) {
    if (!ipc->interrupt_scheduled || cycles < ipc->interrupt_at) return;
    ipc->interrupt_scheduled = false;
    if (ipc->y & ipc->iy) ipc->ppc_flags |= PPC_IPC;
    if (ipc->x & ipc->ix) ipc->arm_flags |= ARM_IPC;
}

static void schedule(WiiIpc* ipc, uint64_t cycles) {
    if (!ipc->interrupt_scheduled) {
        ipc->interrupt_scheduled = true;
        ipc->interrupt_at = cycles + 1200; /* Broadway TB is core/12. */
    }
}

bool wii_ipc_read(WiiIpc* ipc, uint32_t offset, uint32_t* value) {
    switch (offset) {
    case 0x00: *value = ipc->ppc_message; return true;
    case 0x04:
        *value = (ipc->x & 1) | ((ipc->x & 2) << 2) |
                 ((ipc->y & 1) << 2) | (ipc->y & 2) | (ipc->iy << 4); return true;
    case 0x08: *value = ipc->arm_message; return true;
    case 0x0c:
        *value = (ipc->y & 1) | ((ipc->y & 2) << 2) |
                 ((ipc->x & 1) << 2) | (ipc->x & 2) | (ipc->ix << 4); return true;
    case 0x30: *value = ipc->ppc_flags; return true;
    case 0x34: *value = ipc->ppc_mask; return true;
    case 0x38: *value = ipc->arm_flags; return true;
    case 0x3c: *value = ipc->arm_mask; return true;
    default: return false;
    }
}

bool wii_ipc_write(WiiIpc* ipc, uint32_t offset, uint32_t v, uint64_t cycles) {
    wii_ipc_tick(ipc, cycles);
    switch (offset) {
    case 0x00: ipc->ppc_message = v; break;
    case 0x04:
        ipc->x = (v & 1) | ((v >> 2) & 2);
        ipc->y &= ~(((v >> 2) & 1) | (v & 2));
        ipc->iy = (v >> 4) & 3;
        break;
    case 0x08: ipc->arm_message = v; break;
    case 0x0c:
        ipc->y = (v & 1) | ((v >> 2) & 2);
        ipc->x &= ~(((v >> 2) & 1) | (v & 2));
        ipc->ix = (v >> 4) & 3;
        break;
    case 0x30: ipc->ppc_flags &= ~v; break;
    case 0x34: ipc->ppc_mask = v; break;
    case 0x38: ipc->arm_flags &= ~v; break;
    case 0x3c: ipc->arm_mask = v; break;
    default: return false;
    }
    schedule(ipc, cycles);
    return true;
}

bool wii_ipc_ppc_interrupt(const WiiIpc* ipc) { return (ipc->ppc_flags & ipc->ppc_mask) != 0; }
bool wii_ipc_arm_interrupt(const WiiIpc* ipc) { return (ipc->arm_flags & ipc->arm_mask) != 0; }
