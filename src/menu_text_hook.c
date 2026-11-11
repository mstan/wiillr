/* SPDX-License-Identifier: MIT */
#include "wiillr/menu_text_hook.h"
#include "wiillr/menu_frame_hook.h"
#include "wiillr/menu_text.h"
#include "wiillr/menu_picture.h"
#include "wiillr/menu_picture_hook.h"
#include "memory/memory.h"
#include <stdio.h>

#define TEXT_DRAW 0x81521dd8u
#define TEXT_BYTES 0x560u
#define TEXT_HASH UINT64_C(0x36625cdd5cec4bec)
#define TEXT_DESTRUCTOR 0x815218e4u
static WiiGx* owner;
static WiiMenuTextNative* renderer;
static bool verified;
static u64 entries,declines,failures;

static const u8* read_memory(void* user,u32 address,u32 size) {
    CPUState* cpu=user;const u32 physical=address&0x1fffffffu;
    if(!(physical<cpu->ram_size || (physical>=0x10000000u && physical-0x10000000u<cpu->mem2_size)))return NULL;
    u32 available;const u8* data=gcn_mem_resolve(cpu,address,&available);
    return data && size<=available ? data:NULL;
}
static bool attest(CPUState* cpu) {
    const u8* data=read_memory(cpu,TEXT_DRAW,TEXT_BYTES);if(!data)return false;
    u64 hash=UINT64_C(14695981039346656037);
    for(u32 i=0;i<TEXT_BYTES;++i){hash^=data[i];hash*=UINT64_C(1099511628211);}
    return hash==TEXT_HASH;
}
bool wii_menu_text_hook_start(WiiGx* gx,const GcWiiNativeRenderApi* meshes,const GcWiiNativeGuiApi* gui) {
    if(owner || !gx || !gx->cpu)return false;
    renderer=wii_menu_text_create((WiiMenuMemory){gx->cpu,read_memory},meshes,gui);
    if(!renderer)return false;
    owner=gx;verified=false;entries=declines=failures=0;return true;
}
void wii_menu_text_hook_invalidate(u32 address,u32 size) {
    const u32 physical=address&0x1fffffffu;
    if((u64)physical+size>(TEXT_DRAW&0x1fffffffu) && physical<(TEXT_DRAW&0x1fffffffu)+TEXT_BYTES)verified=false;
}
static int decline(CPUState* cpu,const char* why) {
    ++declines;
    WiiMenuView view;
    if(WIILLR_MENU_TEXT==2 || wii_menu_frame_current_view(cpu,&view)!=0){fprintf(stderr,"Native Menu TextBox rejected: %s\n",why);cpu->halted=true;return 1;}
    return 0;
}
int wii_menu_text_hook_call(CPUState* cpu,u32 address) {
    if(!owner || owner->cpu!=cpu)return 0;
    const u32 physical=address&0x1fffffffu;
    if(physical==(TEXT_DESTRUCTOR&0x1fffffffu)) {
        if(verified)wii_menu_text_forget(renderer,cpu->gpr[3]);
        return 0;
    }
    if(physical!=(TEXT_DRAW&0x1fffffffu))return 0;
    if(!verified){verified=attest(cpu);if(!verified)return decline(cpu,"Original TextBox function identity mismatch");}
    ++entries;if(cpu->cycles>=cpu->cycle_deadline)return 0;
    const WiiMenuMemory memory={cpu,read_memory};WiiMenuView view;char error[192];
    const int native_view=wii_menu_frame_current_view(cpu,&view);
    if(native_view<0){++declines;fprintf(stderr,"Native Menu TextBox has no supported owned view\n");cpu->halted=true;return 1;}
    if(!native_view && !wii_menu_picture_sdk_view(memory,&view,error,sizeof error))return decline(cpu,error);
    if(native_view && (!view.clip_width || !view.clip_height)){cpu->pc=cpu->lr;++cpu->cycles;return 1;}
    if(!wii_menu_native_boundary(owner)) {
        ++failures;fprintf(stderr,"Native Menu TextBox could not drain previous work\n");cpu->halted=true;return 1;
    }
    const int result=wii_menu_text_draw(renderer,cpu->gpr[3],cpu->gpr[4],&view);
    if(result!=1) {
        WiiMenuTextStatus status;wii_menu_text_status(renderer,&status);
        if(result==0)return decline(cpu,status.error);
        ++failures;fprintf(stderr,"Native Menu TextBox submission failed: %s\n",status.error);cpu->halted=true;return 1;
    }
    cpu->pc=cpu->lr;cpu->cycles++;return 1;
}
void wii_menu_text_hook_stop(void) {
    if(!renderer)return;
    WiiMenuTextStatus status;wii_menu_text_status(renderer,&status);
    printf("{\"native_menu_text\":\"%s\",\"entries\":%llu,\"glyphs\":%llu,\"draws\":%llu,\"mesh_uploads\":%llu,\"uncovered\":%llu,\"failed\":%llu}\n",
        WIILLR_MENU_TEXT==2?"strict":"declared-slice",(unsigned long long)entries,(unsigned long long)status.glyphs,
        (unsigned long long)status.draws,(unsigned long long)status.mesh_uploads,(unsigned long long)declines,(unsigned long long)failures);
    wii_menu_text_destroy(renderer);renderer=NULL;owner=NULL;verified=false;
}
