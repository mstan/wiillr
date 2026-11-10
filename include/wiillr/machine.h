/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MACHINE_H
#define WIILLR_MACHINE_H
#include "cpu/cpu.h"
#include "pi/pi.h"
#include "wiillr/ipc.h"
#include "wiillr/ios.h"
#include "wiillr/ave.h"
#include "mi/mi.h"
#include "vi/vi.h"
#include "wiillr/dsp.h"
#include "ai/ai.h"
#include "exi/exi.h"
#include "di/di.h"
#include "si/si.h"
typedef struct {
    GcnPi pi;
    WiiIpc ipc;
    WiiIosNative ios;
    GcnMi mi;
    GcnVi vi;
    WiiDsp dsp;
    GcnAi ai;
    GcnExi exi;
    GcnDi di;
    GcnSi si;
    u64 last_cycles, fields;
    u32 tb_phase;
    u64 mmio_reads, mmio_writes;
    u32 unsupported_address;
    u32 gpio_out,gpio_direction;
    WiiAve ave;
} WiiMachine;
void wii_machine_init(WiiMachine* machine, CPUState* cpu);
void wii_machine_tick(WiiMachine* machine, CPUState* cpu);
#endif
