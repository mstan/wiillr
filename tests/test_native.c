/* SPDX-License-Identifier: MIT */
/* Synthetic content and HLE spies exercise execution bindings, not game code. */
#include "cpu/overlay_module.h"
#include "cpu/aot_module.h"
#include "memory/memory.h"
#include "wiillr/native.h"
#if WIILLR_NATIVE_CHAIN
#include "cpu/native_chain_inline.h"
static bool exercise_chain,nested_chain,in_nested,invalidate_chain;
static u32 chain_target;
#endif
#include <stdio.h>
#include <stdlib.h>
static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;}}while(0)
static unsigned body_calls,mods_calls,dlc_calls,renderer_calls,menu_calls,save_calls;
static bool handle_mods,handle_dlc,handle_renderer,handle_menu;
static void body(CPUState* cpu){
    ++body_calls;cpu->pc+=4;
#if WIILLR_NATIVE_CHAIN
    if(exercise_chain) {
        ++cpu->cycles;
        if(chain_target)cpu->pc=chain_target;
        if(invalidate_chain){invalidate_chain=false;mem_write8(cpu,0x1000,1,cpu->pc);}
        if(nested_chain && !in_nested) {
            in_nested=true;const u32 resume=cpu->pc;
            CHECK(wii_native_call(cpu,cpu->pc));cpu->pc=resume;in_nested=false;
        }
        (void)dr_native_chain_next(cpu);
    }
#endif
}
static void save_body(CPUState* cpu){CHECK(cpu->gpr[4]==save_calls);body(cpu);}
#define GCN_OVERLAY_PAGE_COUNT 5u
#define GCN_OVERLAY_VARIANT_COUNT 2u
#define WIILLR_PPC_CATALOG_SHA "synthetic"
#define WIILLR_NATIVE_EXTERNAL_TABLES 1
static GcnOverlayPage s_overlay_pages[]={
    {0x00001000,0x00002000,0,1},{0x80001000,0x80002000,0,1},
    {0x8000d000,0x8000e000,1,1},{0x80089000,0x8008a000,0,1},
    {0xc0001000,0xc0002000,0,1}
};
static GcnOverlayVariant s_overlay_variants[]={{0,body},{0,save_body}};
#include "../src/native.c"

bool wii_mm9_mods_dispatch(CPUState* cpu,u32 address){
    ++mods_calls;
    if(address==0x8000d664u){cpu->gpr[4]=++save_calls;return false;}
    return handle_mods;
}
void wii_mm9_mods_invalidate(u32 address,u32 size){(void)address;(void)size;}
bool wii_mm9_dlc_dispatch(CPUState* cpu,u32 address){(void)cpu;(void)address;++dlc_calls;return handle_dlc;}
void wii_mm9_dlc_invalidate(u32 address,u32 size){(void)address;(void)size;}
int wii_mm9_renderer_hook_dispatch(CPUState* cpu,u32 address){(void)cpu;(void)address;++renderer_calls;return handle_renderer;}
void wii_mm9_renderer_hook_invalidate(u32 address,u32 size){(void)address;(void)size;}
int wii_menu_picture_hook_call(CPUState* cpu,u32 address){(void)cpu;(void)address;++menu_calls;return handle_menu;}
void wii_menu_picture_hook_invalidate(u32 address,u32 size){(void)address;(void)size;}
static bool call(CPUState* cpu,u32 pc){cpu->pc=pc;return wii_native_call(cpu,pc);}
static void clear_spies(void){mods_calls=dlc_calls=renderer_calls=menu_calls=0;}
int main(void){
    CPUState cpu;if(!cpu_init(&cpu))return 1;
    const u64 hash=gcn_aot_fnv1a64(cpu.ram+0x1000,4096);
    s_overlay_variants[0].hash=s_overlay_variants[1].hash=hash;
    wii_native_install();clear_spies();
    const u32 aliases[]={0x00001008,0x80001008,0xc0001008};
    for(unsigned round=0;round<2;++round)for(unsigned n=0;n<3;++n){
        CHECK(call(&cpu,aliases[n]));CHECK(cpu.pc==aliases[n]+4);
    }
    CHECK(body_calls==6 && module.dispatches==6 && module.hashes==3);
    CHECK(!mods_calls && !dlc_calls && !renderer_calls && !menu_calls);
    CHECK(!wii_native_call(NULL,0x80001008));
    /* A real uncached write retires all three positive decisions. Negative
     * decisions remain cached until a physical write restores known bytes. */
    mem_write8(&cpu,0xc0001000,1,cpu.pc);
    for(unsigned round=0;round<2;++round)for(unsigned n=0;n<3;++n)CHECK(!call(&cpu,aliases[n]));
    CHECK(module.hashes==6 && module.dispatches==6);
    mem_write8(&cpu,0x1000,0,cpu.pc);
    for(unsigned n=0;n<3;++n)CHECK(call(&cpu,aliases[n]));
    CHECK(module.hashes==9 && module.dispatches==9);
    cpu.ram[0x1000]=1;gcn_native_code_content_dirty(0x80001000,1);
    CHECK(call(&cpu,0x80001008)); /* content-only write retains I-cache authorization */
    gcn_native_code_invalidate(0x1000,32);CHECK(!call(&cpu,0x80001008));
    cpu.ram[0x1000]=0;gcn_native_code_reset();
    CHECK(call(&cpu,0x80001008));
    CHECK(!call(&cpu,0x40001008)); /* no namespace substitution */
    /* SAVE staging must execute on every call, including a cached native
     * body. FRAME must also remain interceptable after its body was bound. */
    for(unsigned n=0;n<3;++n)CHECK(call(&cpu,0x8000d664));
    CHECK(save_calls==3);
    CHECK(call(&cpu,0x80089e68));const unsigned before=body_calls;
    handle_mods=true;CHECK(call(&cpu,0x80089e68));CHECK(body_calls==before);
    const u32 mod_entries[]={0x800b709c,0x800b730c,0x800b5ac0,0x8008938c,0x80089604};
    for(unsigned n=0;n<sizeof mod_entries/sizeof *mod_entries;++n){
        clear_spies();CHECK(call(&cpu,mod_entries[n]));CHECK(mods_calls==1 && !renderer_calls && !menu_calls);
    }
    handle_mods=false;handle_dlc=true;
    const u32 dlc_entries[]={0x8024c5f4,0x8024d424,0x8024c6f0};
    for(unsigned n=0;n<3;++n){clear_spies();CHECK(call(&cpu,dlc_entries[n]));CHECK(dlc_calls==1);}
    handle_dlc=false;handle_renderer=true;
    const u32 render_entries[]={0x800e7308,0x800d5e50,0x80099394,0x800e8700,0x800e88d8,
        0x800a7764,0x800a77c4,0x800ea2e4,0x8009905c,0x801131a8,0x8009b6fc,
        0x80099200,0x800995f0,0x800d5990,0x800e8024,0x80083730};
    for(unsigned n=0;n<sizeof render_entries/sizeof *render_entries;++n){
        clear_spies();CHECK(call(&cpu,render_entries[n]));CHECK(renderer_calls==1 && !menu_calls);
    }
    handle_renderer=false;handle_menu=true;
    const u32 menu_entries[]={0x8152126c,0x81520d34,0x81521dd8,0x815218e4};
    for(unsigned n=0;n<4;++n)for(unsigned bank=0;bank<3;++bank){
        const u32 pc=(menu_entries[n]&0x1fffffffu)|(bank==0?0:bank==1?0x80000000u:0xc0000000u);
        clear_spies();CHECK(call(&cpu,pc));CHECK(menu_calls==1 && !mods_calls && !renderer_calls);
    }
#if WIILLR_NATIVE_CHAIN
    handle_menu=false;exercise_chain=true;cpu.cycles=100;cpu.cycle_deadline=10000;
    unsigned calls=body_calls;u64 dispatched=module.dispatches;
    CHECK(call(&cpu,0x80001000));
    CHECK(body_calls==calls+9 && module.dispatches==dispatched+9 && cpu.pc==0x80001024);
    CHECK(wii_native_chained_dispatches()==8 && !chain.depth && !chain.admitted && !call_depth);
    cpu.cycles=100;cpu.cycle_deadline=103;calls=body_calls;
    CHECK(call(&cpu,0x80001000));CHECK(cpu.pc==0x8000100c && body_calls==calls+3);
    CHECK(wii_native_chained_dispatches()==2);
    cpu.cycles=100;cpu.cycle_deadline=10000;wii_native_set_stop_pc(0x8000100c);
    CHECK(call(&cpu,0x80001000));CHECK(cpu.pc==0x8000100c && wii_native_chained_dispatches()==2);
    wii_native_set_stop_pc(0);
    /* A pure source must yield before a SAVE owner, even if that owner's
     * body has already been verified and cached by the ordinary route. */
    chain_target=0x8000d664;calls=save_calls;
    CHECK(call(&cpu,0x80001000));CHECK(cpu.pc==chain_target && save_calls==calls);
    CHECK(!wii_native_chained_dispatches());chain_target=0;exercise_chain=false;
    CHECK(call(&cpu,0x8000d664));CHECK(save_calls==calls+1);
    exercise_chain=true;hook_pages[1]|=HOOK_OWNER;calls=body_calls;
    CHECK(call(&cpu,0x80001000));CHECK(body_calls==calls+1 && !wii_native_chained_dispatches());
    hook_pages[1]&=(u8)~HOOK_OWNER;
    /* Nested guest ownership executes once and suspends chaining. Its native
     * dispatches are counted once, independently of additional root chunks. */
    nested_chain=true;calls=body_calls;dispatched=module.dispatches;
    CHECK(call(&cpu,0x80001000));CHECK(body_calls==calls+18 && module.dispatches==dispatched+18);
    CHECK(wii_native_chained_dispatches()==8 && !call_depth && !chain.depth);
    nested_chain=false;invalidate_chain=true;calls=body_calls;
    CHECK(call(&cpu,0x80001000));CHECK(body_calls==calls+1 && !wii_native_chained_dispatches());
    exercise_chain=false;CHECK(!call(&cpu,0x80001000));
    mem_write8(&cpu,0x1000,0,cpu.pc);CHECK(call(&cpu,0x80001000));
#endif
    /* A partial catalog must keep search routing even after an inside call
     * has selected its body. Neither neighboring address shares that range. */
    handle_menu=false;
    s_overlay_pages[1].start=0x80001020;s_overlay_pages[1].end=0x80001040;
    s_overlay_variants[0].hash=gcn_aot_fnv1a64(cpu.ram+0x1020,32);
    wii_native_install();CHECK(!bindings_ready && !module.index);
    CHECK(call(&cpu,0x80001028));CHECK(cpu.pc==0x8000102c);
    CHECK(!call(&cpu,0x80001008) && cpu.pc==0x80001008);
    CHECK(!call(&cpu,0x80001040) && cpu.pc==0x80001040);
    gcn_native_code_set_candidate(0,0,NULL,NULL);cpu_free(&cpu);
    if(!failures)puts("Native page binding/alias/invalidation/HLE routing contracts passed");
    return failures?1:0;
}
