/* SPDX-License-Identifier: MIT */
#include "wiillr/gx.h"
#include "memory/memory.h"
#include "cpu/native_code.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
static int resolve(void* user,u32 address,u32 size,GcnllrNativeGxMemory* range) {
    WiiGx* g=user;const u32 physical=address&0x1fffffffu;
    /* GPU clients may only resolve physical main RAM, never MMIO or locked L1. */
    if(!((physical<g->cpu->ram_size) || (physical>=0x10000000u && physical-0x10000000u<g->cpu->mem2_size)))return 0;
    u32 available;u8* data=gcn_mem_resolve(g->cpu,physical,&available);
    if(!data || available<size)return 0;
    *range=(GcnllrNativeGxMemory){data,physical,available};return 1;
}
static int locate(void* user,const void* pointer,u32 size,u32* physical) {
    WiiGx* g=user;const uintptr_t p=(uintptr_t)pointer;
    const uintptr_t starts[]={(uintptr_t)g->cpu->ram,(uintptr_t)g->cpu->mem2};
    const u32 sizes[]={g->cpu->ram_size,g->cpu->mem2_size},bases[]={0,0x10000000u};
    for(unsigned n=0;n<2;n++)if(p>=starts[n] && p-starts[n]<sizes[n] && size<=sizes[n]-(p-starts[n])) {
        *physical=bases[n]+(u32)(p-starts[n]);return 1;
    }
    return 0;
}
static void bp_write(void* user,u32 reg,u32 value) {
    WiiGx* g=user;
    if(reg==0x45 && (value&0xff)==2)gcn_pe_set_finish(&g->machine->pe);
    if(reg==0x47 || reg==0x48)gcn_pe_set_token(&g->machine->pe,(u16)value,reg==0x48);
}
static int write_copy(void* user,u32 address,const u8* bytes,u32 size,u32 flags,u32 width,u32 height,u32 stride) {
    (void)flags;(void)width;(void)height;(void)stride;
    WiiGx* g=user;GcnllrNativeGxMemory range;
    if(!resolve(user,address,size,&range))return 0;
    memcpy(range.bytes,bytes,size);gcn_native_code_content_dirty(range.physical_address,size);
    if(g->cpu->reserve_valid && size) {
        const u32 reservation=gcn_reservation_key(g->cpu->reserve_addr)&~31u;
        if((u64)reservation+32>range.physical_address && reservation<(u64)range.physical_address+size)g->cpu->reserve_valid=false;
    }
    ++g->copies;return 1;
}
static int fifo(void* user,const u8* bytes,u32 size) {WiiGx* g=user;return g->api->fifo(bytes,size);}
static void drain(void* user) {WiiGx* g=user;if(!g->api->drain())g->cpu->halted=true;}
bool wii_gx_start(WiiGx* g,WiiMachine* m,CPUState* cpu,const char* library,const char* data,const char* cache) {
    memset(g,0,sizeof *g);g->machine=m;g->cpu=cpu;
#ifdef _WIN32
    g->library=(void*)LoadLibraryExA(library,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    GcnllrNativeGxGetApi get=g->library?(GcnllrNativeGxGetApi)GetProcAddress((HMODULE)g->library,"gcnllr_native_gx_get_api"):NULL;
#else
    g->library=dlopen(library,RTLD_NOW|RTLD_LOCAL);
    GcnllrNativeGxGetApi get=g->library?(GcnllrNativeGxGetApi)dlsym(g->library,"gcnllr_native_gx_get_api"):NULL;
#endif
    if(!get || !(g->api=get(GCNLLR_NATIVE_GX_ABI))) {wii_gx_stop(g);return false;}
    const GcnllrNativeGxConfig config={.abi=GCNLLR_NATIVE_GX_ABI,.title="Wii Startup Menu",.data_directory=data,
        .cache_directory=cache,.hidden=1,.user=g,.bp_write=bp_write,.write_copy=write_copy,
        .resolve_memory=resolve,.locate_memory=locate};
    if(!g->api->start(&config)) {wii_gx_stop(g);return false;}
    m->gx_fifo=fifo;m->gx_user=g;gcn_gp_set_drain_hook(&m->gp,drain,g);return true;
}
bool wii_gx_tick(WiiGx* g) {
    if(!g->api)return true;
    if(g->cpu->cycles-g->last_service<16384 && g->machine->fields==g->scanouts)return true;
    g->last_service=g->cpu->cycles;
    if(g->api->service && !g->api->service())return false;
    if(g->machine->fields!=g->scanouts) {
        g->scanouts=g->machine->fields;
        u32 address,width,height,stride;
        if(gcn_vi_xfb_info(&address,&width,&height,&stride)) {
            GcnllrNativeGxMemory range;
            const u64 extent=(u64)(height-1)*stride+(u64)width*2;
            if(!width || !height || width>1024 || height>1024 || extent>0xffffffffu ||
               !resolve(g,address,(u32)extent,&range) || !g->api->present(range.bytes,width,height,stride))return false;
        }
    }
    GcnllrNativeGxStatus status={0};g->api->status(&status);
    return !status.failed && !status.quit;
}
void wii_gx_stop(WiiGx* g) {
    if(g->machine) {g->machine->gx_fifo=NULL;g->machine->gx_user=NULL;gcn_gp_set_drain_hook(&g->machine->gp,NULL,NULL);}
    if(g->api)g->api->stop();g->api=NULL;
    if(g->library) {
#ifdef _WIN32
        FreeLibrary((HMODULE)g->library);
#else
        dlclose(g->library);
#endif
        g->library=NULL;
    }
}
