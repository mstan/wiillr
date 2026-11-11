/* SPDX-License-Identifier: MIT */
#include "wiillr/machine.h"
#include "cpu/timing.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)

int main(void) {
    CPUState cpu;CHECK(cpu_init(&cpu));
    WiiMachine m;wii_machine_init(&m,&cpu);
    CHECK(!cpu.halted);
#if WIILLR_SCHED_DEADLINE
    CHECK(wii_machine_deadline(&m,0)>0 && wii_machine_deadline(&m,0)<=WII_MACHINE_EVENT_CAP);
    cpu.cycles=11;CHECK(ppc_mftb(&cpu,268,0)==0);
    cpu.cycles=12;CHECK(ppc_mftb(&cpu,268,0)==1);
    ppc_mtspr(&cpu,22,2,0);
    CHECK(wii_machine_deadline(&m,12)==48);
    cpu.cycles=47;wii_machine_tick(&m,&cpu);
    CHECK(!cpu.exception && ppc_mfspr(&cpu,22,0)==0);
    cpu.msr|=0x8000;cpu.pc=0x80001000;
    cpu.cycles=48;wii_machine_tick(&m,&cpu);
    CHECK(cpu.exception==PPC_EXC_DECREMENTER && cpu.srr0==0x80001000);
    CHECK(cpu.timebase==4 && !m.domains.failed);
    cpu.exception=0;
    /* Register observation must see all elapsed time even before the next
     * owner credit, then rebase the clock before changing its rate/state. */
    mem_write32_cia(&cpu,0xcd006c00,GCN_AI_CR_PSTAT|GCN_AI_CR_AISFR,0);
    cpu.cycles+=60750;
    CHECK(mem_read32_cia(&cpu,0xcd006c08,0)==4);
    CHECK(!cpu.halted && m.domains.services[0]>0);
    wii_machine_tick(&m,&cpu);CHECK(!cpu.halted);
    /* AID's initial FIFO interrupt and ARAM completion are real deadlines,
     * independent of the fallback DSP execution cap. */
    m.dsp.registers.aid_int_pending=1;m.dsp.registers.aid_start_cycles=200;
    gcn_domains_changed(&m.domains,GCN_DOMAIN_AUDIO);
    CHECK(wii_machine_deadline(&m,cpu.cycles)==cpu.cycles+200);
    m.dsp.registers.aid_int_pending=0;
    m.dsp.registers.dma_active=1;m.dsp.registers.dma_cycles_left=32;
    gcn_domains_changed(&m.domains,GCN_DOMAIN_AUDIO);
    CHECK(wii_machine_deadline(&m,cpu.cycles)==cpu.cycles+32);
    m.dsp.registers.dma_active=0;gcn_domains_changed(&m.domains,GCN_DOMAIN_AUDIO);
    /* An IPC register mutation replaces the cached IO event immediately. */
    mem_write32_cia(&cpu,0xcd000004,0x31,0);
    CHECK(wii_machine_deadline(&m,cpu.cycles)>cpu.cycles);
#else
    CHECK(wii_machine_deadline(&m,0)==64);
    cpu.cycles=11;wii_machine_tick(&m,&cpu);CHECK(cpu.timebase==0);
    cpu.cycles=12;wii_machine_tick(&m,&cpu);CHECK(cpu.timebase==1);
#endif
    ppc_timing_set_observer(NULL,NULL,NULL);
    cpu_free(&cpu);return 0;
}
