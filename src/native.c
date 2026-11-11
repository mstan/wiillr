/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bind the shared content-verified module to this consumer's original boot. */
#include "wiillr/native.h"
#include "cpu/overlay_module.h"
#include "cpu/native_code.h"
#include <stdio.h>
#include "overlay_module_tables.inc"
static u32 resident[GCN_OVERLAY_PAGE_COUNT];
static u32 page_index[GCN_OVERLAY_INDEX_SLOTS];
static GcnOverlayModule module={s_overlay_pages,s_overlay_variants,resident,
    GCN_OVERLAY_PAGE_COUNT,GCN_OVERLAY_VARIANT_COUNT};
const GcnOverlayModule* wii_native_module(void){return &module;}
bool gcn_overlay_call(CPUState* cpu,u32 address){return gcn_overlay_module_call(&module,cpu,address);}
void gcn_overlay_icbi(u32 address,u32 size){gcn_overlay_module_invalidate(&module,address,size);}
bool wii_native_call(CPUState* cpu,u32 address){return gcn_overlay_call(cpu,address);}
static int candidate(CPUState* cpu,u32 address){return wii_native_call(cpu,address);}
void wii_native_install(void){
    for(u32 i=0;i<GCN_OVERLAY_PAGE_COUNT;i++)resident[i]=GCN_OVERLAY_UNKNOWN;
    gcn_overlay_module_index(&module,page_index,GCN_OVERLAY_INDEX_SLOTS);
    /* Reuse the shared store/DMA invalidation observer. Direct calls retain
     * the live physical/cached/uncached PC when selecting the compiled body. */
    gcn_native_code_set_candidate(0,GC_MAIN_RAM_SIZE,candidate,gcn_overlay_icbi);
}
void wii_native_summary(void){
    printf("{\"PPC_provider\":\"static-content-verified\",\"catalog_sha256\":\"%s\","
           "\"native_dispatches\":%llu,\"content_hashes\":%llu,\"invalidations\":%llu}\n",
        WIILLR_PPC_CATALOG_SHA,(unsigned long long)module.dispatches,
        (unsigned long long)module.hashes,(unsigned long long)module.invalidations);
}
