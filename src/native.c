/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bind the shared content-verified module to this consumer's original boot. */
#include "wiillr/native.h"
#include "cpu/overlay_module.h"
#include "cpu/overlay_binding.h"
#include "cpu/native_code.h"
#include "cpu/native_candidate.h"
#include "wiillr/mm9_dlc.h"
#include "wiillr/mm9_mods.h"
#include <stdio.h>
#include <string.h>
#if WIILLR_NATIVE_CHAIN
#include "cpu/native_chain.h"
#include "debug/cost_ring.h"
#endif
#if WIILLR_MENU_PICTURES
#include "wiillr/menu_picture_hook.h"
#endif
#if WIILLR_MM9_RENDERER
#include "wiillr/mm9_renderer_hook.h"
#endif
#ifndef WIILLR_NATIVE_EXTERNAL_TABLES
#include "overlay_module_tables.inc"
#endif
#if WIILLR_NATIVE_REGIONS
#include "cpu/overlay_region.h"
#include "cpu/native_boundary.h"
#include "cpu/fast_title.h"
#ifndef WIILLR_NATIVE_EXTERNAL_REGIONS
#include "overlay_regions.inc"
#endif
static GcnOverlayRegion* region_index[GCN_OVERLAY_INDEX_SLOTS];
static u32 region_stop_pc;
static bool region_boundary(const CPUState* cpu,u32 target,u64 cycles,void* user) {
    (void)cycles;(void)user;
    if(target==region_stop_pc)return false;
    return !(cpu->msr&0x8000u) ||
        (!*ppc_native_decrementer_pending() && !gcn_pi_external_pending());
}
#endif
static u32 resident[GCN_OVERLAY_PAGE_COUNT];
static u32 page_index[GCN_OVERLAY_INDEX_SLOTS];
static GcnOverlayModule module={s_overlay_pages,s_overlay_variants,resident,
    GCN_OVERLAY_PAGE_COUNT,GCN_OVERLAY_VARIANT_COUNT};
/* One resolved execution binding per original namespace, including negative
 * identities. The existing invalidation observer retires all three aliases. */
static GcnOverlayFn bound[GCN_OVERLAY_INDEX_SLOTS];
static u8 bound_known[GCN_OVERLAY_INDEX_SLOTS];
enum {HOOK_MODS=1,HOOK_DLC=2,HOOK_RENDERER=4,HOOK_MENU=8,HOOK_OWNER=16};
static u8 hook_pages[GC_MAIN_RAM_SIZE/4096u];
static bool bindings_ready;
#if WIILLR_NATIVE_CHAIN
static DrNativeChain chain;
static unsigned call_depth;
static u32 chain_stop;
static u64 chained_dispatches,last_chained;
static bool chain_boundary(const CPUState* cpu,void* user) {
    (void)user;
#if GCN_COST_RING_ENABLED
    const u8* thread=cpu->ram+0xe4;
    gcn_cost_thread_boundary(cpu->cycles,((u32)thread[0]<<24)|((u32)thread[1]<<16)|
        ((u32)thread[2]<<8)|thread[3]);
#else
    (void)cpu;
#endif
    return true;
}
#endif
static int binding_slot(u32 address,u32* page) {
    const u32 physical=address&0x1fffffffu;
    if(physical>=GC_MAIN_RAM_SIZE)return -1;
    u32 bank;
    switch(address>>29u){case 0:bank=0;break;case 4:bank=1;break;case 6:bank=2;break;default:return -1;}
    *page=physical>>12u;
    return (int)(bank*(GC_MAIN_RAM_SIZE/4096u)+*page);
}
static void install_hook_pages(void) {
    memset(hook_pages,0,sizeof hook_pages);
    /* SAVE returns false after staging its native snapshot: it must still
     * run before the original body, including when that body is bound. */
    const u32 mods[]={0x00d,0x089,0x0aa,0x0b5,0x0b7,0x0ba,0x0d5};
    for(u32 i=0;i<sizeof mods/sizeof *mods;++i)hook_pages[mods[i]]|=HOOK_MODS;
    hook_pages[0x24c]|=HOOK_DLC;hook_pages[0x24d]|=HOOK_DLC;
#if WIILLR_MM9_RENDERER
    /* Compositor/screen, sprite/HUD, frame continuations, HBM and texture
     * invalidation. Generated same-page seams remain inside their bodies. */
    const u32 renderer[]={0x083,0x099,0x09b,0x0a7,0x0d5,0x0e7,0x0e8,0x0ea,0x113};
    for(u32 i=0;i<sizeof renderer/sizeof *renderer;++i)hook_pages[renderer[i]]|=HOOK_RENDERER;
#endif
#if WIILLR_MENU_PICTURES
    /* Picture/TextBox draws and both destructors. */
    hook_pages[0x1520]|=HOOK_MENU;hook_pages[0x1521]|=HOOK_MENU;
#endif
#if WIILLR_NATIVE_CHAIN || WIILLR_NATIVE_REGIONS
    /* Reuse the actual build's transformed-page list. Every injected owner
     * and its continuations retain the ordinary native dispatch boundary. */
    const u32 owners[]={WIILLR_NATIVE_OWNED_PAGES};
    for(u32 i=0;i<sizeof owners/sizeof *owners;++i) {
        const u32 physical=owners[i]&0x1fffffffu;
        if(physical<GC_MAIN_RAM_SIZE)hook_pages[physical>>12u]|=HOOK_OWNER;
    }
#endif
}
const GcnOverlayModule* wii_native_module(void){return &module;}
static bool dispatch_hooks(CPUState* cpu,u32 address,u8 hooks) {
    if((hooks&HOOK_MODS) && ((address&0xfffc0000u)==0x80080000u || address==0x8000d664u) &&
       wii_mm9_mods_dispatch(cpu,address))return true;
    if((hooks&HOOK_DLC) && (address&0xffffe000u)==0x8024c000u && wii_mm9_dlc_dispatch(cpu,address))return true;
#if WIILLR_MM9_RENDERER
    if((hooks&HOOK_RENDERER) && wii_mm9_renderer_hook_dispatch(cpu,address))return true;
#endif
#if WIILLR_MENU_PICTURES
    if((hooks&HOOK_MENU) && wii_menu_picture_hook_call(cpu,address))return true;
#endif
    return false;
}
bool gcn_overlay_call(CPUState* cpu,u32 address){
    if(!cpu || !cpu->ram)return false;
    u32 page;
    const int slot=binding_slot(address,&page);
    if(!bindings_ready || slot<0) {
        if(dispatch_hooks(cpu,address,0xff))return true;
        return gcn_overlay_module_call(&module,cpu,address);
    }
    if(hook_pages[page] && dispatch_hooks(cpu,address,hook_pages[page]))return true;
#if WIILLR_NATIVE_REGIONS
    GcnOverlayRegion* region=region_index[slot];
    if(region && gcn_overlay_region_call(&module,cpu,region,address))return true;
#endif
    if(!bound_known[slot]) {
        bound[slot]=gcn_overlay_module_bind(&module,cpu,address);
        bound_known[slot]=1;
    }
    return gcn_overlay_binding_call(&module,cpu,bound[slot]);
}
void gcn_overlay_icbi(u32 address,u32 size){
#if WIILLR_NATIVE_REGIONS
    for(u32 i=0;i<GCN_OVERLAY_REGION_COUNT;++i)
        gcn_overlay_region_invalidate(&native_regions[i],address,size);
#endif
    const u32 physical=address&0x1fffffffu;
    u32 page;
    if(size && binding_slot(address,&page)>=0) {
        const u64 end=(u64)physical+size;
        const u32 last=end>=GC_MAIN_RAM_SIZE?GC_MAIN_RAM_SIZE/4096u-1u:(u32)(end-1u)>>12u;
        for(;page<=last;++page)for(u32 bank=0;bank<3;++bank) {
            const u32 slot=bank*(GC_MAIN_RAM_SIZE/4096u)+page;
            bound_known[slot]=0;bound[slot]=NULL;
        }
    }
    wii_mm9_mods_invalidate(address,size);
    if(physical<0x24e000u && (u64)physical+size>0x24c000u)wii_mm9_dlc_invalidate(address,size);
#if WIILLR_MM9_RENDERER
    wii_mm9_renderer_hook_invalidate(address,size);
#endif
#if WIILLR_MENU_PICTURES
    wii_menu_picture_hook_invalidate(address,size);
#endif
    gcn_overlay_module_invalidate(&module,address,size);
}
void wii_native_set_stop_pc(u32 address) {
#if WIILLR_NATIVE_REGIONS
    region_stop_pc=address;
#endif
#if WIILLR_NATIVE_CHAIN
    chain_stop=address;chain.stop_pc=address;
#else
    (void)address;
#endif
}
u64 wii_native_chained_dispatches(void) {
#if WIILLR_NATIVE_CHAIN
    return last_chained;
#else
    return 0;
#endif
}
bool wii_native_call(CPUState* cpu,u32 address){
#if WIILLR_NATIVE_CHAIN
    const bool root=call_depth++==0,previous=chain.admitted;
    const u64 before=chained_dispatches;
    u32 page=0;const int slot=binding_slot(address,&page);
    /* Only the root driver admits a pure page. Reentrant native/HLE calls
     * retain their own stop/return contract, even inside an admitted root. */
    if(root) {chain.cpu=cpu;chain.halted=cpu?&cpu->halted:NULL;last_chained=0;}
    chain.admitted=root && cpu && bindings_ready && slot>=0 && !hook_pages[page];
    const bool result=gcn_overlay_call(cpu,address);
    chain.admitted=previous;--call_depth;
    if(root) {last_chained=chained_dispatches-before;module.dispatches+=last_chained;}
    return result;
#else
    return gcn_overlay_call(cpu,address);
#endif
}
static int candidate(CPUState* cpu,u32 address){return wii_native_call(cpu,address);}
void wii_native_install(void){
#if WIILLR_NATIVE_CHAIN
    if(call_depth || !dr_native_chain_bind(NULL))return; /* Installation is quiescent only. */
#endif
    bindings_ready=false;
    memset(bound,0,sizeof bound);memset(bound_known,0,sizeof bound_known);
    install_hook_pages();
    for(u32 i=0;i<GCN_OVERLAY_PAGE_COUNT;i++)resident[i]=GCN_OVERLAY_UNKNOWN;
    const bool full_pages=gcn_overlay_module_index(&module,page_index,GCN_OVERLAY_INDEX_SLOTS);
#if WIILLR_NATIVE_REGIONS
    memset(region_index,0,sizeof region_index);
    for(u32 i=0;i<GCN_OVERLAY_REGION_COUNT;++i) {
        GcnOverlayRegion* region=&native_regions[i];
        region->boundary=region_boundary;
        gcn_overlay_region_enable(region,full_pages);
        if(!region->enabled)continue;
        bool allowed=true;
        for(u32 member=0;member<region->page_count;++member) {
            u32 physical_page;
            const int member_slot=binding_slot(region->start+member*4096u,&physical_page);
            if(member_slot<0 || hook_pages[physical_page] || region_index[member_slot]) {
                allowed=false;break;
            }
        }
        if(!allowed) {gcn_overlay_region_enable(region,false);continue;}
        for(u32 member=0;member<region->page_count;++member) {
            u32 physical_page;
            const int member_slot=binding_slot(region->start+member*4096u,&physical_page);
            region_index[member_slot]=region;
        }
    }
#endif
    /* Reuse the shared store/DMA invalidation observer. Direct calls retain
     * the live physical/cached/uncached PC when selecting the compiled body. */
    /* AOT and HLE identity checks watch their code ranges. Ordinary data keeps
     * the existing first-write/CRC fence without repeated code invalidation. */
    gcn_native_code_set_candidate_scoped(0,GC_MAIN_RAM_SIZE,candidate,gcn_overlay_icbi);
    /* Partial, unaligned or duplicate ranges retain the original search path;
     * one page-wide execution decision cannot represent those catalogs. */
    bindings_ready=full_pages;
#if WIILLR_NATIVE_CHAIN
    call_depth=0;last_chained=0;
    chain=(DrNativeChain){.entries=bound,.entry_count=GCN_OVERLAY_INDEX_SLOTS,
        .excluded_pages=hook_pages,.stop_pc=chain_stop,.dispatches=&chained_dispatches,
        .before=chain_boundary};
    if(!dr_native_chain_bind(&chain))bindings_ready=false;
#endif
}
void wii_native_summary(void){
#if WIILLR_NATIVE_REGIONS
    for(u32 i=0;i<GCN_OVERLAY_REGION_COUNT;++i) {
        const GcnOverlayRegion* region=&native_regions[i];
        printf("{\"native_region\":%u,\"region_source\":\"%s\",\"start\":%u,"
               "\"pages\":%u,\"enabled\":%u,\"calls\":%llu,\"crossings\":%llu,"
               "\"declined_crossings\":%llu}\n",i,WIILLR_PPC_REGIONS_SHA,
               region->start,region->page_count,(unsigned)region->enabled,
               (unsigned long long)region->calls,(unsigned long long)region->crossings,
               (unsigned long long)region->declined_crossings);
    }
#endif
    u32 observed=0;
    for(u32 page=0;page<GC_MAIN_RAM_SIZE/4096u;++page)
        if(gcn_native_store_pages[page]&GCN_NATIVE_STORE_OBSERVED)++observed;
    printf("{\"PPC_provider\":\"static-content-verified\",\"catalog_sha256\":\"%s\","
           "\"native_dispatches\":%llu,\"content_hashes\":%llu,\"invalidations\":%llu,\"store_observed_pages\":%u}\n",
        WIILLR_PPC_CATALOG_SHA,(unsigned long long)module.dispatches,
        (unsigned long long)module.hashes,(unsigned long long)module.invalidations,observed);
#if WIILLR_NATIVE_CHAIN
    printf("{\"native_chaining\":1,\"chained_dispatches\":%llu,\"depth_limit\":%u}\n",
        (unsigned long long)chained_dispatches,DR_NATIVE_CHAIN_MAX_DEPTH);
#else
    printf("{\"native_chaining\":0,\"chained_dispatches\":0}\n");
#endif
}
