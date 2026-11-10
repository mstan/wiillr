/* SPDX-License-Identifier: MIT */
#include "wiillr/machine.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static unsigned refreshes, resets;
static void refresh(void) { ++refreshes; }
static void reset_drive(void) { ++resets; }
int main(void) {
    CPUState cpu;
    CHECK(cpu_init(&cpu));
    WiiMachine m;
    wii_machine_init(&m, &cpu);
    CHECK(m.vi.core_clock == 729000000u);
    cpu.cycles = 11;
    wii_machine_tick(&m, &cpu);
    CHECK(cpu.timebase == 0);
    cpu.cycles = 12;
    wii_machine_tick(&m, &cpu);
    CHECK(cpu.timebase == 1);
    mem_write32(&cpu, 0xcc00401c, 0x11223344, 0x3400);
    CHECK(mem_read16(&cpu, 0x0c00401c, 0x3400) == 0x1122);
    CHECK(mem_read16(&cpu, 0xcc00401e, 0x3400) == 0x3344);
    CHECK(mem_read32(&cpu, 0xcc00302c, 0x3400) == GCN_PI_REVISION_RETAIL);
    CHECK(mem_read16(&cpu, 0xcc00302e, 0x3400) == 0x00b1);
    mem_write32(&cpu, 0xcc00302c, 0, 0x3400);
    CHECK(mem_read32(&cpu, 0x0c00302c, 0x3400) == GCN_PI_REVISION_RETAIL);
    mem_write32(&cpu, 0xcc00300c, 0x1fffffff, 0x3400);
    CHECK(m.pi.reg[3] == 0x1fffffe0);
    gcn_pi_set_refresh_hook(&m.pi, refresh);
    mem_read32(&cpu, 0xcc003000, 0x3400);
    mem_write32(&cpu, 0xcc003004, 0xff, 0x3400);
    CHECK(refreshes == 2);
    gcn_pi_set_reset_drive_hook(&m.pi, reset_drive);
    mem_write32(&cpu, 0xcc003024, 0, 0x3400);
    CHECK(resets == 0);
    GcnPi gc;
    gcn_pi_init(&gc);
    gcn_pi_set_reset_drive_hook(&gc, reset_drive);
    gcn_pi_write(&gc, &cpu, GCN_PI_BASE+GCN_PI_RESETCODE, 0, 4);
    gcn_pi_write(&gc, &cpu, GCN_PI_BASE+GCN_PI_FIFO_BASE, 0x1fffffff, 4);
    CHECK(resets == 1 && gc.reg[3] == 0x03ffffe0);
    GcnVi gc_vi;
    gcn_vi_init(&gc_vi);
    CHECK(gc_vi.core_clock == 486000000u);
    gcn_ai_write(&m.ai, &cpu, GCN_AI_BASE, GCN_AI_CR_PSTAT | GCN_AI_CR_AISFR, 4);
    gcn_ai_tick(729000000u);
    CHECK(m.ai.sample_counter == 48000u && m.ai.cycle_accum_x2 == 0);
    gcn_ai_write(&m.ai, &cpu, GCN_AI_BASE, GCN_AI_CR_PSTAT | GCN_AI_CR_SCRESET, 4);
    gcn_ai_tick(729000000u);
    CHECK(m.ai.sample_counter == 32000u && m.ai.cycle_accum_x2 == 0);
    GcnAi gc_ai;
    gcn_ai_init(&gc_ai);
    gcn_ai_write(&gc_ai, &cpu, GCN_AI_BASE, GCN_AI_CR_PSTAT | GCN_AI_CR_SCRESET, 4);
    gcn_ai_tick(486000000u);
    CHECK(gc_ai.sample_counter == 32000u && gc_ai.cycle_accum_x2 == 0);

    WiiIpc ipc;
    wii_ipc_init(&ipc);
    CHECK(wii_ipc_write(&ipc, 0x34, 0x40000000, 0));
    CHECK(wii_ipc_write(&ipc, 0x3c, 0x80000000, 0));
    CHECK(wii_ipc_write(&ipc, 0x0c, 0x30, 0)); /* ARM request interrupt masks */
    CHECK(wii_ipc_write(&ipc, 0x00, 0x1000, 0));
    CHECK(wii_ipc_write(&ipc, 0x04, 0x31, 0)); /* submit request */
    CHECK(!wii_ipc_ppc_interrupt(&ipc) && !wii_ipc_arm_interrupt(&ipc));
    wii_ipc_tick(&ipc, 1199);
    CHECK(!wii_ipc_arm_interrupt(&ipc));
    wii_ipc_tick(&ipc, 1200);
    CHECK(wii_ipc_arm_interrupt(&ipc) && !wii_ipc_ppc_interrupt(&ipc));
    CHECK(wii_ipc_write(&ipc, 0x0c, 0x3c, 1200)); /* ARM clears X1, raises Y2 */
    CHECK(ipc.x == 0 && ipc.y == 2);
    wii_ipc_write(&ipc, 0x38, 0x80000000, 1200);
    wii_ipc_tick(&ipc, 2400);
    CHECK(!wii_ipc_arm_interrupt(&ipc) && wii_ipc_ppc_interrupt(&ipc));
    wii_ipc_write(&ipc, 0x04, 0x32, 2400); /* PPC clears Y2 */
    wii_ipc_write(&ipc, 0x30, 0x40000000, 2400);
    CHECK(!wii_ipc_ppc_interrupt(&ipc) && ipc.y == 0);
    wii_ipc_write(&ipc, 0x08, 0x1000, 2400);
    wii_ipc_write(&ipc, 0x0c, 0x31, 2400); /* ARM publishes actual reply */
    wii_ipc_tick(&ipc, 3600);
    uint32_t value;
    CHECK(wii_ipc_read(&ipc, 0x04, &value) && value == 0x34);
    CHECK(wii_ipc_read(&ipc, 0x08, &value) && value == 0x1000);
    CHECK(wii_ipc_ppc_interrupt(&ipc));
    CHECK(!wii_ipc_read(&ipc, 0x10, &value));
    mem_write32_cia(&cpu,0xcd800018,0x20000,0);
    CHECK(mem_read32_cia(&cpu,0x0d000018,0)==0x20000 && m.vi_config==0x20000);
    mem_write32_cia(&cpu,0x0d000018,0,0);CHECK(!mem_read32_cia(&cpu,0xcd800018,0));
    CHECK(mem_read32_cia(&cpu,0xcd800180,0)==0x53102 && wii_hollywood_audio_clock(&m.hollywood));
    CHECK(mem_read32_cia(&cpu,0xcd800024,0)==0);
    mem_write32_cia(&cpu,0xcd800024,0,0);
    CHECK(!cpu.halted && mem_read32_cia(&cpu,0x0d000024,0)==0);
    mem_write32_cia(&cpu,0xcd800024,0x1100,0);
    CHECK(!cpu.halted && mem_read32_cia(&cpu,0x0d000024,0)==0x1100);
    CHECK(!wii_hollywood_write(&m.hollywood,0x0d800024,0x1101) && m.hollywood.vi_solid==0x1100);
    gcn_ai_init_model(&m.ai,true);
    gcn_ai_write(&m.ai,&cpu,GCN_AI_BASE,GCN_AI_CR_PSTAT|GCN_AI_CR_SCRESET,4);
    const u32 samples=m.ai.sample_counter;
    mem_write32_cia(&cpu,0xcd8001d0,0,0);cpu.cycles+=729000000u;wii_machine_tick(&m,&cpu);
    CHECK(m.ai.sample_counter==samples && !wii_hollywood_audio_clock(&m.hollywood));
    mem_write32_cia(&cpu,0xcd800180,0x53002,0);
    mem_write32_cia(&cpu,0xcd8001cc,wii_audio_pll_preset(false),0);
    CHECK(mem_read32_cia(&cpu,0x0d0001cc,0)==wii_audio_pll_preset(false));
    mem_write32_cia(&cpu,0xcd8001d0,0x40000000,0);CHECK(!wii_hollywood_audio_clock(&m.hollywood));
    mem_write32_cia(&cpu,0xcd8001d0,0xc0000000,0);CHECK(wii_hollywood_audio_clock(&m.hollywood));
    cpu.cycles+=729000000u;wii_machine_tick(&m,&cpu);CHECK(m.ai.sample_counter==samples+32000);
    mem_write32_cia(&cpu,0xcd800180,0x53102,0);CHECK(!wii_hollywood_audio_clock(&m.hollywood));
    mem_write32_cia(&cpu,0xcd8001cc,wii_audio_pll_preset(true),0);CHECK(wii_hollywood_audio_clock(&m.hollywood));
    mem_write32_cia(&cpu,0xcd800018,1,0);CHECK(cpu.halted && m.unsupported_address==0xcd800018);
    cpu_free(&cpu);
    return 0;
}
