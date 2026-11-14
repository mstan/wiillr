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

static const GcnOverlayPage pages[]={{0x80006000u,0x80007000u,0,1},
                                    {0x80007000u,0x80008000u,1,1}};
static GcnOverlayVariant variants[2];
static u32 resident[2],index[GCN_OVERLAY_INDEX_SLOTS];
static GcnOverlayModule module={pages,variants,resident,2,2};
static const GcnOverlayFn members[]={func_80006FF8_reference,func_80007000_reference};
static bool body(CPUState*,const DrFunctionUnitOwner*);
static WiiNativeFunctionUnit unit={.region={.start=0x80006000u,.page_count=2,.members=members},
    .info=&func_80006FF8_unit_candidate_info,.body=body};
static const u8 excluded[GC_MAIN_RAM_SIZE/4096u];
unsigned fixture_function_adapter_calls;
static unsigned unit_ram_stores,unit_ram_loads,code_store_delegations,unit_code_stores;

static bool observe(const CPUState* cpu,u32 pc,u64 cycles,bool scheduling,void* user) {
    const DrFunctionUnitOwner* owner=user;
    const bool allowed=owner->guard(cpu,pc,cycles,scheduling,owner->user);
    /* Read-only test observation at the generated unit's own post-access
     * guards. A store delegated before execution cannot produce these hits. */
    if(!scheduling && cpu->gpr[5]==GC_RAM_BASE+0x1000u) {
        if(pc==0x80007008u && read_be32(cpu->ram+0x1000u)==cpu->gpr[7])++unit_ram_stores;
        if(pc==0x8000700cu && cpu->gpr[9]==read_be32(cpu->ram+0x1004u))++unit_ram_loads;
    }
    if(!scheduling && cpu->gpr[5]==GC_RAM_BASE+0x7008u && pc==0x80007008u)++unit_code_stores;
    return allowed;
}
static bool body(CPUState* cpu,const DrFunctionUnitOwner* owner) {
    const DrFunctionUnitOwner proxy={observe,(void*)owner};
    return func_80006FF8_unit_candidate(cpu,&proxy);
}

static void invalidate(u32 address,u32 size) {
    gcn_overlay_module_invalidate(&module,address,size);
    wii_native_function_invalidate(&unit,address,size);
}
static int candidate(CPUState* cpu,u32 address) {
    return wii_native_function_call(&module,cpu,&unit,address);
}
void fixture_function_adapter_begin(CPUState* cpu,u32 base) {
    if(base!=0x80006ff8u)return;
    /* Exact public synthetic instructions from test_function_unit_gen.c. */
    const u32 words[]={0x38630001u,0x7c833214u,0x38e40002u,0x90e50000u,
                       0x81250004u,0x7d491a14u,0x4e800020u};
    for(unsigned i=0;i<sizeof words/sizeof *words;++i)
        write_be32(cpu->ram+0x6ff8u+i*4u,words[i]);
    for(unsigned i=0;i<2;++i) {
        resident[i]=GCN_OVERLAY_UNKNOWN;
        variants[i]=(GcnOverlayVariant){gcn_aot_fnv1a64(cpu->ram+0x6000u+i*4096u,4096u),members[i]};
    }
    if(!gcn_overlay_module_index(&module,index,GCN_OVERLAY_INDEX_SLOTS))abort();
    wii_native_function_enable(&unit,true,excluded,sizeof excluded);
    if(!unit.region.enabled)abort();
    gcn_native_code_set_candidate_scoped(0x6000u,0x8000u,candidate,invalidate);
    if(cpu->gpr[5]==GC_RAM_BASE+0x1000u)
        gcn_native_code_invalidate(0x1000u,8u); /* Existing real dirty-page policy. */
}
void fixture_function_adapter_event(CPUState* cpu) {
    (void)cpu;
    /* A real identical-byte authorization retirement through the existing
     * observer, rather than a boolean that only the test guard can see. */
    gcn_native_code_invalidate(0x80006000u,8192u);
}
bool fixture_function_adapter(CPUState* cpu,u32 base) {
    if(base!=0x80006ff8u)abort();
    ++fixture_function_adapter_calls;
    const bool code_store=cpu->gpr[5]==GC_RAM_BASE+0x7008u;
    const u64 prior_compatibility=unit.compatibility,prior_cycles=unit.root_guest_cycles+unit.continuation_guest_cycles;
    const bool result=wii_native_function_call(&module,cpu,&unit,cpu->pc);
    if(code_store && result && unit.compatibility>prior_compatibility &&
       unit.root_guest_cycles+unit.continuation_guest_cycles>prior_cycles)++code_store_delegations;
    return result;
}
void fixture_function_adapter_end(void) {
    gcn_native_code_set_candidate(0,0,NULL,NULL);
    if(!unit_ram_stores || !unit_ram_loads || !code_store_delegations || unit_code_stores)abort();
    printf("composed function adapter: calls=%u unit_ram_stores=%u unit_ram_loads=%u code_store_delegations=%u unit_code_stores=%u unit_root_cycles=%llu\n",
        fixture_function_adapter_calls,unit_ram_stores,unit_ram_loads,code_store_delegations,unit_code_stores,
        (unsigned long long)unit.root_guest_cycles);
}
