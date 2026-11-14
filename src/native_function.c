/* SPDX-License-Identifier: MIT */
#include "wiillr/native_function.h"
#include "cpu/overlay_binding.h"

typedef struct {
    WiiNativeFunctionUnit* unit;
    GcnOverlayRegionInvocation invocation;
    GcnOverlayModule* module;
    const CPUState* cpu;
    const u8* ram;
    u32 page,resume_pc;
    u64 resume_cycles;
    bool resume;
} FunctionOwner;

static bool contains(const u32* values,size_t count,u32 value) {
    for(size_t i=0;i<count;++i)if(values[i]==value)return true;
    return false;
}
static bool addresses(const u32* values,size_t count,u32 first,u32 end) {
    if(!values || !count || values[0]!=first)return false;
    for(size_t i=0;i<count;++i)
        if((values[i]&3u) || values[i]<first || values[i]>=end ||
           (i && values[i]<=values[i-1]))return false;
    return true;
}
static bool shape(const WiiNativeFunctionUnit* unit) {
    if(!unit || !unit->info || !unit->body)return false;
    const DrFunctionUnitInfo* info=unit->info;
    if((info->entry&3u) || (info->end&3u) || info->end<=info->entry ||
       unit->region.start!=(info->entry&~4095u) ||
       unit->region.page_count!=((info->end-1u-unit->region.start)/4096u+1u) ||
       info->block_count>(info->end-info->entry)/4u ||
       info->entry_count>info->block_count ||
       !addresses(info->block_entries,info->block_count,info->entry,info->end) ||
       !addresses(info->entries,info->entry_count,info->entry,info->end))return false;
    for(size_t i=0;i<info->entry_count;++i)
        if(!contains(info->block_entries,info->block_count,info->entries[i]))return false;
    return true;
}
static bool guard(const CPUState* cpu,u32 pc,u64 cycles,bool scheduling,void* user) {
    FunctionOwner* owner=user;
    if(owner && scheduling)owner->resume=false;
    if(!owner || !cpu || cpu->pc!=pc || cpu->cycles!=cycles ||
       (pc&3u) || pc<owner->unit->info->entry || pc>=owner->unit->info->end)return false;
    const bool allowed=scheduling ?
        gcn_overlay_region_cross(&owner->invocation,cpu,pc,cycles,cpu->cycle_deadline) :
        gcn_overlay_region_identity(&owner->invocation,cpu,pc);
    if(allowed && scheduling) {
        owner->page=pc&~4095u;owner->ram=cpu->ram;
    } else if(!scheduling && (pc&~4095u)==owner->page && cpu==owner->cpu && cpu->ram==owner->ram) {
        /* Returning immediately at this published, unchanged boundary also
         * delegates an observing helper BEFORE it runs. An identity check
         * must not turn an interior instruction into a deadline admission.
         * The original remainder recaptures the same RAM before any callback;
         * a changed/unknown original body remains declined on reattestation. */
        owner->resume=true;owner->resume_pc=pc;owner->resume_cycles=cycles;
    }
    return allowed;
}
static void invoke(CPUState* cpu) {
    const GcnOverlayRegionInvocation invocation=gcn_overlay_region_invocation();
    WiiNativeFunctionUnit* unit=(WiiNativeFunctionUnit*)invocation.region;
    FunctionOwner context={.unit=unit,.invocation=invocation,
        .module=(GcnOverlayModule*)unit->region.module,.cpu=cpu,.ram=cpu->ram,
        .page=cpu->pc&~4095u};
    const DrFunctionUnitOwner owner={guard,&context};
    const u32 entry=cpu->pc;
    const u64 entry_cycles=cpu->cycles;
    if(unit->body(cpu,&owner)) {
        const u64 guest_cycles=cpu->cycles-entry_cycles;
        if(entry==unit->info->entry) {
            ++unit->handled_root;unit->root_guest_cycles+=guest_cycles;
            if(guest_cycles)++unit->progressed_root;
        } else {
            ++unit->handled_continuation;unit->continuation_guest_cycles+=guest_cycles;
            if(guest_cycles)++unit->progressed_continuation;
        }
        const bool resume=context.resume && !cpu->exception && cpu->pc==context.resume_pc &&
            cpu->cycles==context.resume_cycles && cpu->ram==context.ram && (cpu->pc&~4095u)==context.page;
        if(resume && !guest_cycles)++unit->zero_progress_delegations;
        if(resume) {
            const u32 page=(context.page-unit->region.start)/4096u;
            const GcnOverlayFn original=unit->region.members[page];
            if(gcn_overlay_module_bind(context.module,cpu,cpu->pc)==original) {
                ++unit->compatibility;
                (void)gcn_overlay_binding_call(context.module,cpu,original);
            }
        }
    } else {
        ++unit->compatibility;
        /* A false producer has performed no guest effect, callback or store.
         * The just-attested member therefore remains valid and non-NULL. This
         * direct catalog binding cannot recursively select this candidate. */
        const u32 page=(entry-unit->region.start)/4096u;
        (void)gcn_overlay_binding_call((GcnOverlayModule*)unit->region.module,
                                      cpu,unit->region.members[page]);
    }
}
void wii_native_function_enable(WiiNativeFunctionUnit* unit,bool enabled,
                                const u8* excluded_pages,u32 excluded_count) {
    if(!unit)return;
    unit->region.body=invoke;
    /* Reuse the shared range/member shape check before inspecting metadata. */
    gcn_overlay_region_enable(&unit->region,enabled);
    bool allowed=unit->region.enabled && shape(unit) && excluded_pages;
    if(allowed)for(u32 i=0;i<unit->region.page_count;++i) {
        const u32 page=((unit->region.start&0x1fffffffu)/4096u)+i;
        if(page>=excluded_count || excluded_pages[page]) {allowed=false;break;}
    }
    if(!allowed && unit->region.enabled)gcn_overlay_region_enable(&unit->region,false);
}
bool wii_native_function_call(GcnOverlayModule* module,CPUState* cpu,
                              WiiNativeFunctionUnit* unit,u32 address) {
    if(!unit || !unit->region.enabled || !cpu || cpu->pc!=address ||
       address<unit->info->entry || address>=unit->info->end || (address&3u) ||
       !contains(unit->info->entries,unit->info->entry_count,address))return false;
    return gcn_overlay_region_call(module,cpu,&unit->region,address);
}
void wii_native_function_invalidate(WiiNativeFunctionUnit* unit,u32 address,u32 size) {
    if(unit)gcn_overlay_region_invalidate(&unit->region,address,size);
}
