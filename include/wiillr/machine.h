/* SPDX-License-Identifier: MIT */
#ifndef WIILLR_MACHINE_H
#define WIILLR_MACHINE_H
#include "cpu/cpu.h"
#include "pi/pi.h"
#include "wiillr/ipc.h"
#include "wiillr/ios.h"
#include "wiillr/ave.h"
#include "wiillr/hollywood.h"
#include "mi/mi.h"
#include "vi/vi.h"
#include "wiillr/dsp.h"
#include "ai/ai.h"
#include "exi/exi.h"
#include "di/di.h"
#include "si/si.h"
#include "cp/cp.h"
#include "pe/pe.h"
#include "gp/gp.h"
#include "gcnllr/device_domains.h"
#include "wiillr/pacing.h"
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
    WiiPacing pacing;
    u32 tb_phase;
    u64 mmio_reads, mmio_writes;
    u32 unsupported_address;
    u32 gpio_out,gpio_direction;
    WiiAve ave;
    u32 vi_config; /* HW_VI1CFG bit17: region/VISEL selection latch */
    WiiHollywood hollywood;
    GcnCp cp;
    GcnPe pe;
    GcnGp gp;
    u64 gx_cpu_gather_writes,gx_cpu_gather_bytes,gx_cpu_gather_rejected;
    const char* gx_native_owner;
    int (*gx_fifo)(void* user,const u8* bytes,u32 size);
    void* gx_user;
    CPUState* cpu;
    GcnDeviceDomains domains;
    u64 domain_max_lateness;
    u64 domain_max_interval[GCN_DOMAIN_COUNT];
    bool idle_memory_serial;
} WiiMachine;
void wii_machine_init(WiiMachine* machine, CPUState* cpu);
/* A verified whole graphics initializer may claim the FIFO-free contract.
 * Earlier CPU ingress or pending legacy work rejects ownership. Owner must
 * outlive the machine. Subsequent CPU gather writes count and stop. */
bool wii_machine_claim_native_graphics(WiiMachine*,const char* owner);
void wii_machine_tick(WiiMachine* machine, CPUState* cpu);
/* Build-selected event owner. Lockstep keeps its original 64-cycle floor. */
#define WII_MACHINE_EVENT_CAP 16384u
/* Native code yields at a block boundary. The exercised catalog's largest
 * block charge is 997 cycles; leave 1024 cycles inside the delivery cap.
 * Qualifying a new catalog also checks its maximum block and the measured
 * AUDIO/IO service intervals. This is not an arbitrary-code timing proof. */
#define WII_MACHINE_POLL_PERIOD (WII_MACHINE_EVENT_CAP-1024u)
void wii_machine_scheduler_init(WiiMachine*,CPUState*);
u64 wii_machine_deadline(WiiMachine*,u64 now);
void wii_machine_observe_register(WiiMachine*,CPUState*,u32 physical);
bool wii_machine_service_domains(WiiMachine*,CPUState*);
#endif
