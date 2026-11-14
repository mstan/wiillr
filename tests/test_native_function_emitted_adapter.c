/* SPDX-License-Identifier: MIT */
/* Compose the compiler's existing synthetic two-page fixture with the actual
 * consumer owner adapter. The test runner and emitted bodies stay shared. */
#include "wiillr/native_function.h"
#include "cpu/aot_module.h"
#include "cpu/native_code.h"
#include "cpu/native_candidate.h"
#include "memory/memory.h"
#include <stdio.h>
#include <stdlib.h>

void func_80006FF8_reference(CPUState*);
void func_80007000_reference(CPUState*);
bool func_80006FF8_unit_candidate(CPUState*,const DrFunctionUnitOwner*);
extern const DrFunctionUnitInfo func_80006FF8_unit_candidate_info;
void func_80007FF8_reference(CPUState*);
void func_80008000_reference(CPUState*);
bool func_80007FF8_unit_candidate(CPUState*,const DrFunctionUnitOwner*);
extern const DrFunctionUnitInfo func_80007FF8_unit_candidate_info;

static GcnOverlayPage pages[2];
static GcnOverlayVariant variants[2];
static u32 resident[2],index[GCN_OVERLAY_INDEX_SLOTS];
static GcnOverlayModule module={pages,variants,resident,2,2};
static GcnOverlayFn members[2];
static bool body(CPUState*,const DrFunctionUnitOwner*);
static WiiNativeFunctionUnit unit={.region={.start=0x80006000u,.page_count=2,.members=members},
    .info=&func_80006FF8_unit_candidate_info,.body=body};
static const u8 excluded[GC_MAIN_RAM_SIZE/4096u];
unsigned fixture_function_adapter_calls;
static unsigned unit_ram_stores,unit_ram_loads,code_store_delegations,unit_code_stores;
static unsigned unit_mem2_reads,unit_mem2_loopbacks;
static u64 unit_mem2_cycles;
static unsigned clean_store_admissions,local_store_proofs,local_mem2_store_proofs;
typedef struct {const DrFunctionUnitOwner* owner;bool root_seen;} ReadObserver;

static bool observe_memory(const CPUState* cpu,u32 ea,u32 width,bool write,void* user) {
    const ReadObserver* proxy=user;const DrFunctionUnitOwner* owner=proxy->owner;
    const bool allowed=owner->memory && owner->memory(cpu,ea,width,write,owner->user);
    if(allowed && write && ea==GC_RAM_BASE+0x1000u && width==4u) {
        if(!gcn_native_store_pages[1])++clean_store_admissions;
    }
    if(allowed && !write && ea==GC_RAM_BASE+0x1004u && width==4u &&
       read_be32(cpu->ram+0x1000u)!=cpu->gpr[7]) {
        /* The preceding store used dirty local r7 while canonical r7 is
         * still old. A callback query alone cannot establish this fact. */
        ++local_store_proofs;++unit_ram_stores;
    }
    if(allowed && !write && unit.info->entry==0x80006ff8u && width==4u &&
       (ea&0x1fffffffu)==0x10000004u && cpu->mem2 && cpu->mem2_size>=8u &&
       read_be32(cpu->mem2)!=cpu->gpr[7])++local_mem2_store_proofs;
    if(allowed && !write && unit.info->entry==0x80007ff8u && width==4u)++unit_mem2_reads;
    return allowed;
}

static bool observe(const CPUState* cpu,u32 pc,u64 cycles,bool scheduling,void* user) {
    ReadObserver* proxy=user;const DrFunctionUnitOwner* owner=proxy->owner;
    const bool allowed=owner->guard(cpu,pc,cycles,scheduling,owner->user);
    /* Read-only test observation at the generated unit's own post-access
     * guards. A store delegated before execution cannot produce these hits. */
    if(!scheduling && cpu->gpr[5]==GC_RAM_BASE+0x1000u) {
        if(pc==0x80007008u && read_be32(cpu->ram+0x1000u)==cpu->gpr[7])++unit_ram_stores;
        if(pc==0x8000700cu && cpu->gpr[9]==read_be32(cpu->ram+0x1004u))++unit_ram_loads;
    }
    if(!scheduling && cpu->gpr[5]==GC_RAM_BASE+0x7008u && pc==0x80007008u)++unit_code_stores;
    if(unit.info->entry==0x80007ff8u) {
        if(scheduling && pc==0x80007ff8u) {
            if(proxy->root_seen)++unit_mem2_loopbacks;
            proxy->root_seen=true;
        }
        if(!scheduling && pc==0x80007ffcu && cpu->mem2 && cpu->mem2_size>=4 &&
           cpu->gpr[9]==read_be32(cpu->mem2))++unit_mem2_reads;
    }
    return allowed;
}
static bool body(CPUState* cpu,const DrFunctionUnitOwner* owner) {
    ReadObserver observation={owner,false};
    const DrFunctionUnitOwner proxy={observe,&observation,observe_memory};
    const u32 old_load=cpu->gpr[9];
    const bool result=unit.info->entry==0x80007ff8u?func_80007FF8_unit_candidate(cpu,&proxy):
        func_80006FF8_unit_candidate(cpu,&proxy);
    if(result && cpu->gpr[5]==GC_RAM_BASE+0x1000u && cpu->gpr[9]!=old_load &&
       cpu->gpr[9]==read_be32(cpu->ram+0x1004u))++unit_ram_loads;
    return result;
}

static void invalidate(u32 address,u32 size) {
    gcn_overlay_module_invalidate(&module,address,size);
    wii_native_function_invalidate(&unit,address,size);
}
static int candidate(CPUState* cpu,u32 address) {
    return wii_native_function_call(&module,cpu,&unit,address);
}
void fixture_function_adapter_begin(CPUState* cpu,u32 base) {
    if(base!=0x80006ff8u && base!=0x80007ff8u)return;
    /* Exact public synthetic instructions from test_function_unit_gen.c. */
    const u32 words[]={0x38630001u,0x7c833214u,0x38e40002u,0x90e50000u,
                       0x81250004u,0x7d491a14u,0x4e800020u};
    const u32 reads[]={0x81250000u,0x7cc64a14u,0x38630001u,
                       0x2c030004u,0x4180fff0u,0x4e800020u};
    const bool mem2=base==0x80007ff8u;const u32 start=base&~4095u;
    const u32* code=mem2?reads:words;const unsigned count=mem2?6u:7u;
    members[0]=mem2?func_80007FF8_reference:func_80006FF8_reference;
    members[1]=mem2?func_80008000_reference:func_80007000_reference;
    unit.region.start=start;unit.info=mem2?&func_80007FF8_unit_candidate_info:&func_80006FF8_unit_candidate_info;
    for(unsigned i=0;i<count;++i)write_be32(cpu->ram+(base&0x1fffffffu)+i*4u,code[i]);
    for(unsigned i=0;i<2;++i) {
        pages[i]=(GcnOverlayPage){start+i*4096u,start+(i+1u)*4096u,i,1};
        resident[i]=GCN_OVERLAY_UNKNOWN;
        variants[i]=(GcnOverlayVariant){gcn_aot_fnv1a64(cpu->ram+(start&0x1fffffffu)+i*4096u,4096u),members[i]};
    }
    if(!gcn_overlay_module_index(&module,index,GCN_OVERLAY_INDEX_SLOTS))abort();
    wii_native_function_enable(&unit,true,excluded,sizeof excluded);
    if(!unit.region.enabled)abort();
    gcn_native_code_set_candidate_scoped(start&0x1fffffffu,(start&0x1fffffffu)+8192u,candidate,invalidate);
    /* Ordinary scratch begins clean. The real owner must admit its first
     * write without weakening code/native/resident observation boundaries. */
}
void fixture_function_adapter_event(CPUState* cpu) {
    (void)cpu;
    /* A real identical-byte authorization retirement through the existing
     * observer, rather than a boolean that only the test guard can see. */
    gcn_native_code_invalidate(unit.region.start,8192u);
}
bool fixture_function_adapter(CPUState* cpu,u32 base) {
    if(base!=0x80006ff8u && base!=0x80007ff8u)abort();
    ++fixture_function_adapter_calls;
    const bool code_store=cpu->gpr[5]==GC_RAM_BASE+0x7008u;
    const u64 prior_compatibility=unit.compatibility,prior_cycles=unit.root_guest_cycles+unit.continuation_guest_cycles;
    const bool result=wii_native_function_call(&module,cpu,&unit,cpu->pc);
    if(base==0x80007ff8u)unit_mem2_cycles+=unit.root_guest_cycles+unit.continuation_guest_cycles-prior_cycles;
    if(code_store && result && unit.compatibility>prior_compatibility &&
       unit.root_guest_cycles+unit.continuation_guest_cycles>prior_cycles)++code_store_delegations;
    return result;
}
void fixture_function_adapter_end(void) {
    gcn_native_code_set_candidate(0,0,NULL,NULL);
    if(!unit_ram_stores || !unit_ram_loads || !code_store_delegations || unit_code_stores ||
       !unit_mem2_reads || !unit_mem2_loopbacks || !unit_mem2_cycles ||
       !clean_store_admissions || !local_store_proofs || !local_mem2_store_proofs)abort();
    printf("composed function adapter: calls=%u unit_ram_stores=%u unit_ram_loads=%u code_store_delegations=%u unit_code_stores=%u unit_root_cycles=%llu mem2_reads=%u mem2_loopbacks=%u mem2_guest_cycles=%llu\n",
        fixture_function_adapter_calls,unit_ram_stores,unit_ram_loads,code_store_delegations,unit_code_stores,
        (unsigned long long)unit.root_guest_cycles,unit_mem2_reads,unit_mem2_loopbacks,
        (unsigned long long)unit_mem2_cycles);
    printf("composed clean stores: admissions=%u dirty_local_store_proofs=%u mem2_dirty_local_store_proofs=%u\n",
        clean_store_admissions,local_store_proofs,local_mem2_store_proofs);
}
