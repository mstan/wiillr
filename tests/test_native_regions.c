/* SPDX-License-Identifier: MIT */
#include "cpu/overlay_region.h"
#include "cpu/aot_module.h"
#include "memory/memory.h"
#include "wiillr/native.h"
#include <stdio.h>
#include <string.h>
static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(0)
static bool pending,mutate;
bool gcn_pi_external_pending(void) {return pending;}
static void step(CPUState* cpu) {++cpu->gpr[3];++cpu->cycles;cpu->pc+=4096u;}
static void combined(CPUState* cpu) {
    const GcnOverlayRegionInvocation invocation=gcn_overlay_region_invocation();
    if(cpu->pc==0x80001000u) {
        step(cpu);
        if(mutate)mem_write8(cpu,0xc0002000u,0,0x80001004u);
        if(!gcn_overlay_region_cross(&invocation,cpu,cpu->pc,cpu->cycles,cpu->cycle_deadline))return;
    }
    step(cpu);
}
#define GCN_OVERLAY_PAGE_COUNT 6u
#define GCN_OVERLAY_VARIANT_COUNT 1u
#define WIILLR_PPC_CATALOG_SHA "synthetic"
#define WIILLR_PPC_REGIONS_SHA "synthetic"
#define WIILLR_NATIVE_EXTERNAL_TABLES 1
#define WIILLR_NATIVE_EXTERNAL_REGIONS 1
#define GCN_OVERLAY_REGION_COUNT 3u
static GcnOverlayPage s_overlay_pages[]={
    {0x1000u,0x2000u,0,1},{0x2000u,0x3000u,0,1},
    {0x80001000u,0x80002000u,0,1},{0x80002000u,0x80003000u,0,1},
    {0xc0001000u,0xc0002000u,0,1},{0xc0002000u,0xc0003000u,0,1}};
static GcnOverlayVariant s_overlay_variants[]={{0,step}};
static const GcnOverlayFn members[]={step,step};
static GcnOverlayRegion native_regions[]={
    {.start=0x80001000u,.page_count=2,.members=members,.body=combined},
    {.start=0x80088000u,.page_count=2,.members=members,.body=combined},
    {.start=0x81554000u,.page_count=2,.members=members,.body=combined}};
#include "../src/native.c"
bool wii_mm9_mods_dispatch(CPUState* cpu,u32 address) {(void)cpu;return address==0x80089e68u;}
void wii_mm9_mods_invalidate(u32 address,u32 size) {(void)address;(void)size;}
bool wii_mm9_dlc_dispatch(CPUState* cpu,u32 address) {(void)cpu;(void)address;return false;}
void wii_mm9_dlc_invalidate(u32 address,u32 size) {(void)address;(void)size;}
static void reset(CPUState* cpu) {
    memset(cpu->gpr,0,sizeof cpu->gpr);cpu->pc=0x80001000u;
    cpu->cycles=0;cpu->cycle_deadline=100;cpu->exception=0;cpu->halted=false;
    cpu->msr=0;pending=false;mutate=false;wii_native_set_stop_pc(0);
}
int main(void) {
    CPUState cpu;if(!cpu_init(&cpu))return 1;
    s_overlay_variants[0].hash=gcn_aot_fnv1a64(cpu.ram+0x1000u,4096u);
    wii_native_install();reset(&cpu);
    CHECK(native_regions[0].enabled && !native_regions[1].enabled && !native_regions[2].enabled);
    CHECK(wii_native_call(&cpu,cpu.pc));
    CHECK(cpu.pc==0x80003000u && cpu.gpr[3]==2 && module.dispatches==1);
    CHECK(native_regions[0].calls==1 && native_regions[0].crossings==1);
    /* Physical and uncached PCs keep their ordinary native page bodies. */
    const u32 aliases[]={0x1000u,0xc0001000u};
    for(unsigned i=0;i<2;++i) {
        reset(&cpu);cpu.pc=aliases[i];const u64 calls=native_regions[0].calls;
        CHECK(wii_native_call(&cpu,cpu.pc) && cpu.pc==aliases[i]+4096u && cpu.gpr[3]==1);
        CHECK(native_regions[0].calls==calls);
    }
    reset(&cpu);wii_native_set_stop_pc(0x80002000u);
    CHECK(wii_native_call(&cpu,cpu.pc) && cpu.pc==0x80002000u && cpu.gpr[3]==1);
    reset(&cpu);pending=true;cpu.msr=0x8000u;
    CHECK(wii_native_call(&cpu,cpu.pc) && cpu.pc==0x80002000u && cpu.gpr[3]==1);
    reset(&cpu);pending=true; /* Masked interrupts do not force an extra stop. */
    CHECK(wii_native_call(&cpu,cpu.pc) && cpu.pc==0x80003000u && cpu.gpr[3]==2);
    reset(&cpu);mutate=true;const u64 generation=native_regions[0].generation;
    CHECK(wii_native_call(&cpu,cpu.pc) && cpu.pc==0x80002000u && cpu.gpr[3]==1);
    CHECK(native_regions[0].generation>generation && !native_regions[0].known);
    mutate=false;CHECK(wii_native_call(&cpu,cpu.pc) && cpu.pc==0x80003000u && cpu.gpr[3]==2);
    reset(&cpu);cpu.pc=0x80089e68u;CHECK(wii_native_call(&cpu,cpu.pc));
    CHECK(cpu.pc==0x80089e68u); /* Owner hook remains reachable. */
    wii_native_install();CHECK(!native_regions[0].known);
    gcn_native_code_set_candidate(0,0,NULL,NULL);cpu_free(&cpu);
    return failures?1:0;
}
