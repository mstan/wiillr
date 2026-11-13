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
#if WIILLR_MENU_PICTURES
#include "wiillr/menu_picture_hook.h"
#endif
#if WIILLR_MM9_RENDERER
#include "wiillr/mm9_renderer_hook.h"
#endif
#ifndef WIILLR_NATIVE_EXTERNAL_TABLES
#include "overlay_module_tables.inc"
#endif
static u32 resident[GCN_OVERLAY_PAGE_COUNT];
static u32 page_index[GCN_OVERLAY_INDEX_SLOTS];
static GcnOverlayModule module={s_overlay_pages,s_overlay_variants,resident,
    GCN_OVERLAY_PAGE_COUNT,GCN_OVERLAY_VARIANT_COUNT};
/* One resolved execution binding per original namespace, including negative
 * identities. The existing invalidation observer retires all three aliases. */
static GcnOverlayFn bound[GCN_OVERLAY_INDEX_SLOTS];
static u8 bound_known[GCN_OVERLAY_INDEX_SLOTS];
enum {HOOK_MODS=1,HOOK_DLC=2,HOOK_RENDERER=4,HOOK_MENU=8};
static u8 hook_pages[GC_MAIN_RAM_SIZE/4096u];
static bool bindings_ready;
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
    if(!bound_known[slot]) {
        bound[slot]=gcn_overlay_module_bind(&module,cpu,address);
        bound_known[slot]=1;
    }
    return gcn_overlay_binding_call(&module,cpu,bound[slot]);
}
void gcn_overlay_icbi(u32 address,u32 size){
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
bool wii_native_call(CPUState* cpu,u32 address){return gcn_overlay_call(cpu,address);}
static int candidate(CPUState* cpu,u32 address){return wii_native_call(cpu,address);}
void wii_native_install(void){
    bindings_ready=false;
    memset(bound,0,sizeof bound);memset(bound_known,0,sizeof bound_known);
    install_hook_pages();
    for(u32 i=0;i<GCN_OVERLAY_PAGE_COUNT;i++)resident[i]=GCN_OVERLAY_UNKNOWN;
    gcn_overlay_module_index(&module,page_index,GCN_OVERLAY_INDEX_SLOTS);
    /* Reuse the shared store/DMA invalidation observer. Direct calls retain
     * the live physical/cached/uncached PC when selecting the compiled body. */
    /* AOT and HLE identity checks watch their code ranges. Ordinary data keeps
     * the existing first-write/CRC fence without repeated code invalidation. */
    gcn_native_code_set_candidate_scoped(0,GC_MAIN_RAM_SIZE,candidate,gcn_overlay_icbi);
    bindings_ready=true;
}
void wii_native_summary(void){
    u32 observed=0;
    for(u32 page=0;page<GC_MAIN_RAM_SIZE/4096u;++page)
        if(gcn_native_store_pages[page]&GCN_NATIVE_STORE_OBSERVED)++observed;
    printf("{\"PPC_provider\":\"static-content-verified\",\"catalog_sha256\":\"%s\","
           "\"native_dispatches\":%llu,\"content_hashes\":%llu,\"invalidations\":%llu,\"store_observed_pages\":%u}\n",
        WIILLR_PPC_CATALOG_SHA,(unsigned long long)module.dispatches,
        (unsigned long long)module.hashes,(unsigned long long)module.invalidations,observed);
}
