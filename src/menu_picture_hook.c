/* SPDX-License-Identifier: MIT */
#include "wiillr/menu_picture_hook.h"
#include "wiillr/menu_picture.h"
#include "memory/memory.h"
#include <stdio.h>
#include <string.h>

#ifndef WIILLR_MENU_PICTURES
#define WIILLR_MENU_PICTURES 0
#endif
#define MENU_DRAW 0x8152126cu
#define MENU_DRAW_BYTES 0xdcu
#define MENU_DRAW_FNV UINT64_C(0x14fd2b0633c97978)
#define MENU_DESTRUCTOR 0x81520d34u
static WiiGx* owner;
static WiiMenuNative* renderer;
static bool verified;
static u64 declines,failures,entries;
static const u8* read_memory(void* user,u32 address,u32 size) {
    CPUState* cpu=user;const u32 physical=address&0x1fffffffu;
    if(!(physical<cpu->ram_size || (physical>=0x10000000u && physical-0x10000000u<cpu->mem2_size)))return NULL;
    u32 available;const u8* bytes=gcn_mem_resolve(cpu,address,&available);
    return bytes && size<=available ? bytes:NULL;
}
static bool attest(CPUState* cpu) {
    const u8* bytes=read_memory(cpu,MENU_DRAW,MENU_DRAW_BYTES);if(!bytes)return false;
    u64 hash=UINT64_C(14695981039346656037);
    for(u32 i=0;i<MENU_DRAW_BYTES;i++){hash^=bytes[i];hash*=UINT64_C(1099511628211);}
    return hash==MENU_DRAW_FNV;
}
static bool ordered_boundary(WiiGx* gx) {
    WiiMachine* machine=gx->machine;CPUState* cpu=gx->cpu;
    /* At this high-level function boundary earlier guest commands are
     * complete. Publish a partial gather with NOP padding, then consume the
     * queued bytes before recording native geometry on the same owner. */
    if(machine->gp.count) {
        const u8 padding[32]={0};const u32 size=32-machine->gp.count;
        if(!gcn_gp_write_stream(&machine->gp,cpu,padding,size))return false;
    }
    while(machine->cp.gp_read_enable && gcn_cp_fifo_rw_distance(&machine->cp)>=32 && !gcn_cp_at_breakpoint(&machine->cp)) {
        const u32 address=gcn_cp_fifo_read_pointer(&machine->cp);u32 available;
        const u8* bytes=gcn_mem_resolve(cpu,address,&available);
        if(!bytes || available<32 || !gx->api->fifo(bytes,32))return false;
        gcn_cp_gpu_consume_chunk(&machine->cp);
    }
    /* Disabled reads or a breakpoint may retain earlier machine-owned work.
     * A native draw must not overtake that blocked command stream. */
    if(gcn_cp_fifo_rw_distance(&machine->cp))return false;
    return gx->api->drain()!=0;
}
bool wii_menu_picture_hook_start(WiiGx* gx,const GcWiiNativeRenderApi* api) {
    if(owner || !gx || !gx->cpu || !api)return false;
    const WiiMenuMemory memory={gx->cpu,read_memory};renderer=wii_menu_native_create(memory,api);
    if(!renderer)return false;owner=gx;verified=false;entries=declines=failures=0;return true;
}
void wii_menu_picture_hook_invalidate(u32 address,u32 size) {
    const u32 physical=address&0x1fffffffu;
    if((u64)physical+size>(MENU_DRAW&0x1fffffffu) && physical<(MENU_DRAW&0x1fffffffu)+MENU_DRAW_BYTES)verified=false;
}
int wii_menu_picture_hook_call(CPUState* cpu,u32 address) {
    if(!owner || owner->cpu!=cpu)return 0;
    const u32 physical=address&0x1fffffffu;
    if(physical==(MENU_DESTRUCTOR&0x1fffffffu)) {
        if(verified)wii_menu_native_forget_picture(renderer,cpu->gpr[3]);
        return 0; /* The original destructor still owns guest allocations. */
    }
    if(physical!=(MENU_DRAW&0x1fffffffu))return 0;
    if(!verified) {
        verified=attest(cpu);
        if(!verified){++declines;if(WIILLR_MENU_PICTURES==2){fprintf(stderr,"Native Menu Picture original-function identity mismatch\n");cpu->halted=true;return 1;}return 0;}
    }
    ++entries;
    if(cpu->cycles>=cpu->cycle_deadline)return 0;
    const WiiMenuMemory memory={cpu,read_memory};WiiMenuView view;char error[192];
    if(!wii_menu_picture_sdk_view(memory,&view,error,sizeof error)) {
        ++declines;if(WIILLR_MENU_PICTURES==2){fprintf(stderr,"Native Menu Picture rejected view: %s\n",error);cpu->halted=true;return 1;}return 0;
    }
    /* Reject unsupported material families before changing shared GPU order. */
    WiiMenuPicture prepared;
    if(!wii_menu_picture_prepare(memory,cpu->gpr[3],cpu->gpr[4],&view,&prepared,error,sizeof error)) {
        ++declines;if(WIILLR_MENU_PICTURES==2){fprintf(stderr,"Native Menu Picture rejected input: %s\n",error);cpu->halted=true;return 1;}return 0;
    }
    if(!prepared.skipped && !ordered_boundary(owner)){++failures;fprintf(stderr,"Native Menu Picture could not drain previous work\n");cpu->halted=true;return 1;}
    const int result=wii_menu_native_draw_picture(renderer,cpu->gpr[3],cpu->gpr[4],&view);
    if(result!=1){++failures;WiiMenuNativeStatus status;wii_menu_native_status(renderer,&status);fprintf(stderr,"Native Menu Picture submission failed: %s\n",status.error);cpu->halted=true;return 1;}
    /* Void C++ method: preserve caller-owned registers, return through LR.
     * One native service charge provides forward progress without recreating
     * each internal SDK operation; device deadlines remain machine-owned. */
    cpu->pc=cpu->lr;cpu->cycles++;
    return 1;
}
void wii_menu_picture_hook_stop(void) {
    if(!renderer)return;WiiMenuNativeStatus status;memset(&status,0,sizeof status);wii_menu_native_status(renderer,&status);
    printf("{\"native_menu_picture\":\"%s\",\"entries\":%llu,\"draws\":%llu,\"skipped\":%llu,\"mesh_uploads\":%llu,\"uncovered\":%llu,\"failed\":%llu}\n",
        WIILLR_MENU_PICTURES==2?"strict":"declared-slice",(unsigned long long)entries,(unsigned long long)status.draws,(unsigned long long)status.skipped,(unsigned long long)status.mesh_uploads,(unsigned long long)declines,(unsigned long long)failures);
    wii_menu_native_destroy(renderer);renderer=NULL;owner=NULL;verified=false;
}
