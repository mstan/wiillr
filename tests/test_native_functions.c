/* SPDX-License-Identifier: MIT */
/* Synthetic function extents and callbacks; no title code or descriptors. */
#include "wiillr/native_function.h"
#include "wiillr/native.h"
#include "cpu/aot_module.h"
#include "memory/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);++failures;}}while(0)
static unsigned ordinary_calls,producer_calls;
static bool pending,decline,first_fault,nested,reset_nested,safety_probe,raise_pending;
static bool interior_invalidate,changed_interior;
static bool delegate_helper,helper_changes_ram,delegate_store;
static bool immediate_delegate,delegate_continuation;
static bool memory_probe,malformed_banks;
static u8* alternate_ram;
static u32 mutate;
bool gcn_pi_external_pending(void){return pending;}
static void ordinary(CPUState* cpu){
    ++ordinary_calls;
    if(delegate_continuation) {
        if(cpu->pc==0x80002008u){cpu->gpr[3]+=2;++cpu->cycles;cpu->pc+=4;}
        CHECK(cpu->pc==0x8000200cu);cpu->gpr[3]+=18;cpu->cycles+=2;cpu->pc=0x80002040u;return;
    }
    if(interior_invalidate || delegate_helper || delegate_store) {
        u8* const ram=cpu->ram;
        if(cpu->pc==0x80001ff0u) {
            cpu->gpr[3]+=10;cpu->cycles+=2;cpu->pc+=4;
            if(interior_invalidate)mem_write8(cpu,0x80001000u,changed_interior?1:0,cpu->pc);
        }
        CHECK(cpu->pc==0x80001ff4u);
        if(delegate_helper) {
            cpu->gpr[4]+=7;
            if(helper_changes_ram)cpu->ram=alternate_ram;
        }
        if(delegate_store)mem_write8(cpu,0x80001000u,1,cpu->pc);
        cpu->gpr[3]+=20+(helper_changes_ram?ram[0x200]:0);
        cpu->cycles+=3;cpu->pc=0x80002000u;return;
    }
    ++cpu->gpr[3];++cpu->cycles;cpu->pc+=4;
}
static bool function(CPUState*,const DrFunctionUnitOwner*);
static bool inner(CPUState*,const DrFunctionUnitOwner*);
static const u32 entries[]={0x80001ff0u,0x80002008u};
static const u32 blocks[]={0x80001ff0u,0x80002000u,0x80002008u};
static const DrFunctionUnitInfo info={0x80001ff0u,0x80002040u,entries,2,blocks,3};
static const u32 inner_entries[]={0x80003010u},inner_blocks[]={0x80003010u,0x80004000u};
static const DrFunctionUnitInfo inner_info={0x80003010u,0x80004010u,inner_entries,1,inner_blocks,2};
static const u32 owner_entries[]={0x80005010u},owner_blocks[]={0x80005010u,0x80006000u};
static const DrFunctionUnitInfo owner_info={0x80005010u,0x80006010u,owner_entries,1,owner_blocks,2};
static const GcnOverlayFn members[]={ordinary,ordinary};
#define WIILLR_FUNCTION_UNIT_COUNT 3u
#define WIILLR_NATIVE_EXTERNAL_FUNCTIONS 1
static WiiNativeFunctionUnit native_function_units[]={
    {.region={.start=0x80001000u,.page_count=2,.members=members},.info=&info,.body=function},
    {.region={.start=0x80003000u,.page_count=2,.members=members},.info=&inner_info,.body=inner},
    {.region={.start=0x80005000u,.page_count=2,.members=members},.info=&owner_info,.body=inner}};
#define GCN_OVERLAY_PAGE_COUNT 10u
#define GCN_OVERLAY_VARIANT_COUNT 1u
#define WIILLR_NATIVE_EXTERNAL_TABLES 1
#define WIILLR_PPC_CATALOG_SHA "synthetic"
#define WIILLR_NATIVE_OWNED_PAGES 0x80006000u
static GcnOverlayPage s_overlay_pages[]={
    {0x1000u,0x2000u,0,1},{0x2000u,0x3000u,0,1},
    {0x80001000u,0x80002000u,0,1},{0x80002000u,0x80003000u,0,1},
    {0x80003000u,0x80004000u,0,1},{0x80004000u,0x80005000u,0,1},
    {0x80005000u,0x80006000u,0,1},{0x80006000u,0x80007000u,0,1},
    {0xc0001000u,0xc0002000u,0,1},{0xc0002000u,0xc0003000u,0,1}};
static GcnOverlayVariant s_overlay_variants[]={{0,ordinary}};
#include "../src/native.c"
bool wii_mm9_mods_dispatch(CPUState* cpu,u32 address){(void)cpu;(void)address;return false;}
void wii_mm9_mods_invalidate(u32 address,u32 size){(void)address;(void)size;}
bool wii_mm9_dlc_dispatch(CPUState* cpu,u32 address){(void)cpu;(void)address;return false;}
void wii_mm9_dlc_invalidate(u32 address,u32 size){(void)address;(void)size;}

static bool inner(CPUState* cpu,const DrFunctionUnitOwner* owner) {
    if(!owner->guard(cpu,cpu->pc,cpu->cycles,true,owner->user))return false;
    ++cpu->gpr[4];++cpu->cycles;cpu->pc=0x80004000u;
    if(reset_nested)gcn_native_code_reset();
    if(!owner->guard(cpu,cpu->pc,cpu->cycles,true,owner->user))return true;
    ++cpu->gpr[4];++cpu->cycles;cpu->pc=inner_info.end;
    return true;
}
static bool function(CPUState* cpu,const DrFunctionUnitOwner* owner) {
    ++producer_calls;
    if(decline || !owner->guard(cpu,cpu->pc,cpu->cycles,true,owner->user))return false;
    if(memory_probe) {
        const CPUState before=*cpu;
        CHECK(owner->memory);
        if(malformed_banks) {
            CHECK(!owner->memory(cpu,0x81107fecu,4,true,owner->user));
            CHECK(memcmp(cpu,&before,sizeof before)==0);return true;
        }
        const u32 aliases[]={0x01107fecu,0x81107fecu,0xc1107fecu};
        CHECK(gcn_native_store_pages[0x1107u]==0); /* Actual clean stack case. */
        for(unsigned i=0;i<3;++i)for(u32 width=1;width<=8;width*=2) {
            CHECK(owner->memory(cpu,aliases[i],width,true,owner->user));
            CHECK(owner->memory(cpu,aliases[i],width,false,owner->user));
        }
        CHECK(!owner->memory(cpu,0x80001fffu,2,true,owner->user));
        CHECK(!owner->memory(cpu,0x80001000u,4,true,owner->user));
        CHECK(!owner->memory(cpu,0xc0006000u,4,true,owner->user));
        CHECK(!owner->memory(cpu,0x817fffffu,2,true,owner->user));
        CHECK(!owner->memory(cpu,0xcc008000u,4,true,owner->user));
        CHECK(!owner->memory(cpu,0xfffffffdu,8,false,owner->user));
        CHECK(!owner->memory(cpu,aliases[0],3,true,owner->user));
        CHECK(!owner->memory(cpu,aliases[0],0,true,owner->user));
        const u32 mem2[]={0x10000000u,WII_MEM2_BASE,WII_MEM2_UNCACHED};
        for(unsigned i=0;i<3;++i) {
            CHECK(owner->memory(cpu,mem2[i],8,true,owner->user));
            CHECK(owner->memory(cpu,mem2[i],8,false,owner->user));
            CHECK(!owner->memory(cpu,mem2[i]+cpu->mem2_size-1,2,true,owner->user));
        }
        CHECK(memcmp(cpu,&before,sizeof before)==0);
        const u8 flags=gcn_native_store_pages[0x1107u];
        gcn_native_code_watch(0x81107000u,1);
        CHECK(!owner->memory(cpu,aliases[0],4,true,owner->user));
        CHECK(owner->memory(cpu,aliases[0],4,false,owner->user));
        gcn_native_store_pages[0x1107u]=flags; /* Test-only conservative watch cleanup. */
        const u8* ram=cpu->ram;cpu->ram+=1;
        CHECK(!owner->memory(cpu,aliases[0],4,true,owner->user));cpu->ram=(u8*)ram;
        ++cpu->mem2_size;CHECK(!owner->memory(cpu,mem2[0],4,true,owner->user));--cpu->mem2_size;
        CHECK(memcmp(cpu,&before,sizeof before)==0);return true;
    }
    if(immediate_delegate) {
        CHECK(owner->guard(cpu,cpu->pc,cpu->cycles,false,owner->user));return true;
    }
    if(first_fault){cpu->exception=1;return true;}
    CHECK(!owner->guard(cpu,cpu->pc+4,cpu->cycles,false,owner->user));
    CHECK(!owner->guard(cpu,cpu->pc,cpu->cycles+1,false,owner->user));
    if(delegate_continuation) {
        cpu->gpr[3]+=2;++cpu->cycles;cpu->pc+=4;
        CHECK(owner->guard(cpu,cpu->pc,cpu->cycles,false,owner->user));return true;
    }
    if(cpu->pc==info.entry) {
        cpu->gpr[3]+=10;cpu->cycles+=2;
        if(interior_invalidate) {
            cpu->pc=info.entry+4;
            mem_write8(cpu,0x80001000u,changed_interior?1:0,cpu->pc);
            CHECK(!owner->guard(cpu,cpu->pc,cpu->cycles,false,owner->user));
            return true;
        }
        if(delegate_helper || delegate_store) {
            cpu->pc=info.entry+4;
            CHECK(owner->guard(cpu,cpu->pc,cpu->cycles,false,owner->user));
            return true; /* No helper/store or CPU effect after this token. */
        }
        if(safety_probe) {
            cpu->pc=info.entry+4;
            if(raise_pending){pending=true;cpu->msr|=0x8000u;}
            const CPUState before=*cpu;
            /* This same-page publication is not an original scheduling
             * checkpoint, even when a deadline, stop or IRQ is now pending. */
            CHECK(owner->guard(cpu,cpu->pc,cpu->cycles,false,owner->user));
            CHECK(memcmp(cpu,&before,sizeof before)==0);
        }
        cpu->pc=0x80002000u;
        if(mutate)mem_write8(cpu,mutate,0,cpu->pc);
        if(nested) {
            const GcnOverlayRegionInvocation before=gcn_overlay_region_invocation();
            cpu->pc=inner_info.entry;CHECK(wii_native_call(cpu,cpu->pc));cpu->pc=0x80002000u;
            const GcnOverlayRegionInvocation after=gcn_overlay_region_invocation();
            CHECK(before.region==after.region && before.cpu==after.cpu && before.generation==after.generation);
        }
        if(!owner->guard(cpu,cpu->pc,cpu->cycles,true,owner->user))return true;
    }
    cpu->gpr[3]+=20;cpu->cycles+=3;cpu->pc=info.end;
    return true;
}
static void reset(CPUState* cpu,u32 pc) {
    memset(cpu->gpr,0,sizeof cpu->gpr);cpu->pc=pc;cpu->cycles=0;cpu->cycle_deadline=100;
    cpu->exception=0;cpu->halted=false;cpu->msr=0;
    pending=decline=first_fault=nested=reset_nested=safety_probe=raise_pending=false;
    interior_invalidate=changed_interior=false;
    delegate_helper=helper_changes_ram=delegate_store=false;
    immediate_delegate=delegate_continuation=false;
    memory_probe=malformed_banks=false;
    mutate=0;wii_native_set_stop_pc(0);
}
int main(void) {
    CPUState cpu;if(!cpu_init(&cpu))return 1;
    if(!cpu_alloc_mem2(&cpu,0x1000u)){cpu_free(&cpu);return 1;}
    s_overlay_variants[0].hash=gcn_aot_fnv1a64(cpu.ram+0x1000u,4096u);
    wii_native_install();reset(&cpu,info.entry);
    CHECK(native_function_units[0].region.enabled && native_function_units[1].region.enabled);
    CHECK(!native_function_units[2].region.enabled); /* Owner in the second member. */
    /* A descriptor can never authorize an entry that its CFG did not name,
     * or a function whose original-page tuple/extent was mismatched. */
    WiiNativeFunctionUnit bad=native_function_units[0];DrFunctionUnitInfo bad_info=info;
    bad.info=&bad_info;bad_info.entry=info.entry+4;
    wii_native_function_enable(&bad,true,hook_pages,sizeof hook_pages);CHECK(!bad.region.enabled);
    bad_info=info;bad_info.end=info.entry;
    wii_native_function_enable(&bad,true,hook_pages,sizeof hook_pages);CHECK(!bad.region.enabled);
    bad_info=info;const u32 interior_entries[]={info.entry,0x80002004u};bad_info.entries=interior_entries;
    wii_native_function_enable(&bad,true,hook_pages,sizeof hook_pages);CHECK(!bad.region.enabled);
    bad_info=info;bad.region.start=0xc0001000u;
    wii_native_function_enable(&bad,true,hook_pages,sizeof hook_pages);CHECK(!bad.region.enabled);
    bad=native_function_units[0];
    wii_native_function_enable(&bad,true,NULL,0);CHECK(!bad.region.enabled);
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==info.end && cpu.cycles==5 && cpu.gpr[3]==30);
    CHECK(native_function_units[0].handled_root==1 && !gcn_overlay_region_invocation().region);
    CHECK(native_function_units[0].progressed_root==1 && native_function_units[0].root_guest_cycles==5);
    reset(&cpu,entries[1]);CHECK(wii_native_call(&cpu,cpu.pc));
    CHECK(cpu.pc==info.end && cpu.cycles==3 && cpu.gpr[3]==20 && native_function_units[0].handled_continuation==1);
    CHECK(native_function_units[0].progressed_continuation==1 && native_function_units[0].continuation_guest_cycles==3);
    /* In-range nonleader/page continuation and both neighboring extents use
     * ordinary native dispatch. No physical/cached/uncached substitution. */
    const u32 ordinary_pcs[]={0x80001fecu,0x80002000u,0x80002004u,0x80002040u,0x1ff0u,0xc0001ff0u};
    for(unsigned i=0;i<sizeof ordinary_pcs/sizeof *ordinary_pcs;++i) {
        reset(&cpu,ordinary_pcs[i]);const unsigned before=producer_calls;
        CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==ordinary_pcs[i]+4 && cpu.gpr[3]==1 && producer_calls==before);
    }
    reset(&cpu,info.entry);decline=true;CPUState expected=cpu;ordinary(&expected);
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(memcmp(&cpu,&expected,sizeof cpu)==0);
    CHECK(native_function_units[0].compatibility==1);
    reset(&cpu,info.entry);first_fault=true;const unsigned old_ordinary=ordinary_calls;
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.exception==1 && cpu.pc==info.entry && !cpu.cycles && ordinary_calls==old_ordinary);
    reset(&cpu,info.entry);cpu.cycle_deadline=2;
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==0x80002000u && cpu.cycles==2 && cpu.gpr[3]==10);
    reset(&cpu,info.entry);cpu.cycle_deadline=2;safety_probe=true;
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==0x80002000u && cpu.cycles==2 && cpu.gpr[3]==10);
    reset(&cpu,info.entry);safety_probe=true;wii_native_set_stop_pc(info.entry+4);
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==info.end && cpu.cycles==5 && cpu.gpr[3]==30);
    reset(&cpu,info.entry);safety_probe=raise_pending=true;
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==0x80002000u && cpu.cycles==2 && cpu.gpr[3]==10);
    /* Original page execution does not yield at a newly added safety check.
     * An identical-byte invalidation still needs a fresh identity decision,
     * then the same-page remainder runs despite the expired deadline. */
    reset(&cpu,info.entry);interior_invalidate=true;cpu.cycle_deadline=2;
    expected=cpu;ordinary(&expected);const u64 hashes_before=module.hashes;
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(memcmp(&cpu,&expected,sizeof cpu)==0);
    CHECK(cpu.pc==0x80002000u && cpu.cycles==5 && module.hashes>hashes_before);
    /* Unknown changed contents must not be mislabeled as a successful native
     * compatibility binding. Exact changed-content timing is separate. */
    reset(&cpu,info.entry);interior_invalidate=changed_interior=true;
    const unsigned calls_before=ordinary_calls;
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==info.entry+4 && cpu.cycles==2 && cpu.gpr[3]==10);
    CHECK(ordinary_calls==calls_before);
    mem_write8(&cpu,0x80001000u,0,cpu.pc);
    /* Delegating before the callback retains the original page's cached RAM
     * for its remainder, even if that callback changes canonical CPU RAM. */
    reset(&cpu,info.entry);delegate_helper=helper_changes_ram=true;cpu.cycle_deadline=2;
    u8* const original_ram=cpu.ram;alternate_ram=calloc(1,GC_MAIN_RAM_SIZE);
    if(!alternate_ram){cpu_free(&cpu);return 1;}
    original_ram[0x200]=7;alternate_ram[0x200]=99;
    expected=cpu;ordinary(&expected);
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(memcmp(&cpu,&expected,sizeof cpu)==0);
    CHECK(cpu.gpr[3]==37 && cpu.gpr[4]==7 && cpu.ram==alternate_ram && cpu.cycles==5);
    cpu.ram=original_ram;free(alternate_ram);alternate_ram=NULL;original_ram[0x200]=0;
    /* A code-invalidating store is also delegated before its effect, so the
     * original in-flight body completes with its exact expired-deadline exit. */
    reset(&cpu,info.entry);delegate_store=true;cpu.cycle_deadline=2;
    expected=cpu;ordinary(&expected);mem_write8(&cpu,0x80001000u,0,cpu.pc);
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(memcmp(&cpu,&expected,sizeof cpu)==0);
    CHECK(cpu.ram[0x1000]==1 && cpu.pc==0x80002000u && cpu.cycles==5);
    mem_write8(&cpu,0x80001000u,0,cpu.pc);
    /* Metadata-only entry/delegation is handled but contributes zero unit
     * cycles, even though the original compatibility body makes progress. */
    reset(&cpu,info.entry);delegate_helper=immediate_delegate=true;
    const u64 root_cycles=native_function_units[0].root_guest_cycles;
    const u64 progressed=native_function_units[0].progressed_root;
    const u64 zero=native_function_units[0].zero_progress_delegations;
    expected=cpu;ordinary(&expected);CHECK(wii_native_call(&cpu,cpu.pc));
    CHECK(memcmp(&cpu,&expected,sizeof cpu)==0 && cpu.cycles==5);
    CHECK(native_function_units[0].root_guest_cycles==root_cycles && native_function_units[0].progressed_root==progressed);
    CHECK(native_function_units[0].zero_progress_delegations==zero+1);
    reset(&cpu,entries[1]);delegate_continuation=true;
    const u64 continuation_cycles=native_function_units[0].continuation_guest_cycles;
    expected=cpu;ordinary(&expected);CHECK(wii_native_call(&cpu,cpu.pc));
    CHECK(memcmp(&cpu,&expected,sizeof cpu)==0 && cpu.cycles==3);
    CHECK(native_function_units[0].continuation_guest_cycles==continuation_cycles+1);
    reset(&cpu,info.entry);wii_native_set_stop_pc(0x80002000u);
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==0x80002000u && cpu.gpr[3]==10);
    reset(&cpu,info.entry);pending=true;cpu.msr=0x8000u;
    CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==info.entry+4 && cpu.gpr[3]==1);
    const u32 aliases[]={0x2000u,0x80002000u,0xc0002000u};
    for(unsigned i=0;i<3;++i) {
        reset(&cpu,info.entry);mutate=aliases[i];const u64 gen=native_function_units[0].region.generation;
        CHECK(wii_native_call(&cpu,cpu.pc));CHECK(cpu.pc==0x80002000u && cpu.gpr[3]==10 && cpu.cycles==2);
        CHECK(native_function_units[0].region.generation>gen && !native_function_units[0].region.known);
    }
    reset(&cpu,info.entry);nested=true;CHECK(wii_native_call(&cpu,cpu.pc));
    CHECK(cpu.pc==info.end && cpu.gpr[3]==30 && cpu.gpr[4]==2 && cpu.cycles==7);
    reset(&cpu,info.entry);nested=reset_nested=true;CHECK(wii_native_call(&cpu,cpu.pc));
    CHECK(cpu.pc==0x80002000u && cpu.gpr[3]==10 && cpu.gpr[4]==1 && cpu.cycles==3);
    CHECK(!gcn_overlay_region_invocation().region && !native_function_units[0].region.known);
    wii_native_install();reset(&cpu,info.entry);CHECK(wii_native_call(&cpu,cpu.pc) && cpu.gpr[3]==30);
    mem_write8(&cpu,0x2000u,1,cpu.pc);reset(&cpu,info.entry);
    const unsigned before=producer_calls;CHECK(wii_native_call(&cpu,cpu.pc));
    CHECK(cpu.pc==info.entry+4 && producer_calls==before); /* Tuple mismatch -> original member. */
    mem_write8(&cpu,0xc0002000u,0,cpu.pc);reset(&cpu,info.entry);CHECK(wii_native_call(&cpu,cpu.pc) && cpu.pc==info.end);
    CPUState other=cpu;other.pc=info.entry;const u32 old_pc=other.pc;
    CHECK(!wii_native_function_call(&module,&other,&native_function_units[0],other.pc) && other.pc==old_pc);
    reset(&cpu,info.entry);memory_probe=true;
    CHECK(wii_native_call(&cpu,cpu.pc));
    /* A bank alias into MEM1 or the CPU cannot safely retain local GPRs. */
    u8* const mem2=cpu.mem2;reset(&cpu,info.entry);memory_probe=malformed_banks=true;
    cpu.mem2=cpu.ram+0x100;CHECK(wii_native_call(&cpu,cpu.pc));
    cpu.mem2=(u8*)&cpu;CHECK(wii_native_call(&cpu,cpu.pc));cpu.mem2=mem2;
    gcn_native_code_set_candidate(0,0,NULL,NULL);cpu_free(&cpu);
    return failures?1:0;
}
