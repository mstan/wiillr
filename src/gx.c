/* SPDX-License-Identifier: MIT */
#include "wiillr/gx.h"
#include "wiillr/pointer.h"
#include "wiillr/remote_input.h"
#include "wiillr/fs.h"
#include "wiillr/gx_output.h"
#include "wiillr/keyboard.h"
#include "wiillr/mm9_dlc.h"
#include "wiillr/mm9_mods.h"
#include "memory/memory.h"
#include "cpu/native_code.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if WIILLR_MENU_PICTURES
#include "wiillr/menu_picture_hook.h"
#include "wiillr/menu_frame_hook.h"
#endif
#if WIILLR_MM9_RENDERER
#include "wiillr/mm9_renderer_hook.h"
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
static bool idle_dimming(void* user,bool enabled) {
    WiiGx* g=user;return g->video && g->video->idle_dimming(enabled?1u:0u);
}
static int resolve(void* user,u32 address,u32 size,GcnllrNativeGxMemory* range) {
    WiiGx* g=user;const u32 physical=address&0x1fffffffu;
    /* GPU clients may only resolve physical main RAM, never MMIO or locked L1. */
    if(!((physical<g->cpu->ram_size) || (physical>=0x10000000u && physical-0x10000000u<g->cpu->mem2_size)))return 0;
    u32 available,original_available;u8* data=gcn_mem_resolve(g->cpu,physical,&available);
    if(!data || available<size || gcn_mem_resolve(g->cpu,address,&original_available)!=data)return 0;
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
    (void)width;(void)height;(void)stride;
    WiiGx* g=user;GcnllrNativeGxMemory range;
    if(!resolve(user,address,size,&range))return 0;
    if(!(flags&GCNLLR_NATIVE_GX_COPY_MATERIALIZING) && !wii_gx_output_prepare(g,address,size))return 0;
    memcpy(range.bytes,bytes,size);
    if(!(flags&GCNLLR_NATIVE_GX_COPY_MATERIALIZING))gcn_native_code_content_dirty(range.physical_address,size);
    if(!(flags&GCNLLR_NATIVE_GX_COPY_MATERIALIZING) && g->cpu->reserve_valid && size) {
        const u32 reservation=gcn_reservation_key(g->cpu->reserve_addr)&~31u;
        if((u64)reservation+32>range.physical_address && reservation<(u64)range.physical_address+size)g->cpu->reserve_valid=false;
    }
    if(!(flags&GCNLLR_NATIVE_GX_COPY_MATERIALIZING))++g->copies;return 1;
}
static int fifo(void* user,const u8* bytes,u32 size) {WiiGx* g=user;return g->api->fifo(bytes,size);}
static int peek(void* user,u32 depth,u16 x,u16 y,u32* value) {
    WiiGx* g=user;return g->api && g->api->peek && g->api->peek(depth,x,y,value);
}
static void drain(void* user) {WiiGx* g=user;if(!g->api->drain())g->cpu->halted=true;}
bool wii_gx_start(WiiGx* g,WiiMachine* m,CPUState* cpu,const char* library,const char* data,const char* cache) {
    memset(g,0,sizeof *g);g->machine=m;g->cpu=cpu;
    g->scripted_input=getenv("WIILLR_INPUT_SCRIPT")!=NULL;
    if(m->ios.fs)(void)wii_fs_boot_title(m->ios.fs,&g->title);
#if WIILLR_GX_SOFTWARE
    extern const GcnllrNativeGxApi* wii_gx_software_api(WiiMachine*,CPUState*);
    if(library && strcmp(library,"software"))return false;
    g->api=wii_gx_software_api(m,cpu);
#else
    if(library && !strcmp(library,"software"))return false;
#ifdef _WIN32
    g->library=(void*)LoadLibraryExA(library,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    GcnllrNativeGxGetApi get=g->library?(GcnllrNativeGxGetApi)GetProcAddress((HMODULE)g->library,"gcnllr_native_gx_get_api"):NULL;
#else
    g->library=dlopen(library,RTLD_NOW|RTLD_LOCAL);
    GcnllrNativeGxGetApi get=g->library?(GcnllrNativeGxGetApi)dlsym(g->library,"gcnllr_native_gx_get_api"):NULL;
#endif
    if(!get || !(g->api=get(GCNLLR_NATIVE_GX_ABI))) {wii_gx_stop(g);return false;}
#endif
    const char* title=getenv("WIILLR_WINDOW_TITLE"),*window=getenv("WIILLR_WINDOW");
    if(!title || !strcmp(title,"Mega Man 9") || !strcmp(title,"Wii Menu")) {
        if(g->title==0x0001000157523945ull)title="Mega Man 9";
        else if(g->title==0x0000000100000002ull)title="Wii Menu";
    }
    wii_mm9_dlc_bind(g);
    wii_mm9_mods_bind(g);
    g->visible=window && !strcmp(window,"1");
#if WIILLR_GX_SOFTWARE
    /* Reference scanout is available through the machine's TCP interface. */
    g->visible=false;
#endif
    g->input=(WiiBluetoothInput){0,{512,512,616},512,280,false,true,false};
    GcnllrNativeGxConfig config={.abi=GCNLLR_NATIVE_GX_ABI,.title=title?title:"Wii",.data_directory=data,
        .cache_directory=cache,.hidden=!(window && !strcmp(window,"1")),.user=g,.bp_write=bp_write,.write_copy=write_copy,
        .resolve_memory=resolve,.locate_memory=locate};
#if WIILLR_NATIVE_OUTPUT
    if(wii_gx_output_open(g)) {
        config.own_copy=wii_gx_output_own;config.pending_read=wii_gx_output_pending;config.prepare_read=wii_gx_output_prepare;
    } else fprintf(stderr,"Native output ownership unavailable; retaining eager copy floor\n");
#endif
    if(!g->api->start(&config)) {wii_gx_stop(g);return false;}
#ifdef _WIN32
    GcWiiNativeVideoGetApi video_get=g->library?(GcWiiNativeVideoGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_video_get_api"):NULL;
#else
    GcWiiNativeVideoGetApi video_get=g->library?(GcWiiNativeVideoGetApi)dlsym(g->library,"gc_wii_native_video_get_api"):NULL;
#endif
    g->video=video_get?video_get(GC_WII_NATIVE_VIDEO_ABI):NULL;
#ifdef _WIN32
    GcWiiNativePresentationGetApi presentation_get=(GcWiiNativePresentationGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_presentation_get_api");
#else
    GcWiiNativePresentationGetApi presentation_get=(GcWiiNativePresentationGetApi)dlsym(g->library,"gc_wii_native_presentation_get_api");
#endif
    g->presentation=presentation_get?presentation_get(GC_WII_NATIVE_PRESENTATION_ABI):NULL;
    if(!g->presentation && presentation_get)g->presentation=presentation_get(1);
#ifdef _WIN32
    GcWiiNativeResolutionGetApi resolution_get=(GcWiiNativeResolutionGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_resolution_get_api");
#else
    GcWiiNativeResolutionGetApi resolution_get=(GcWiiNativeResolutionGetApi)dlsym(g->library,"gc_wii_native_resolution_get_api");
#endif
    g->resolution=resolution_get?resolution_get(GC_WII_NATIVE_RESOLUTION_ABI):NULL;
    if(g->video){m->hollywood.idle_dimming=idle_dimming;m->hollywood.video_user=g;}
#if WIILLR_MENU_PICTURES || WIILLR_MM9_RENDERER
#ifdef _WIN32
    GcWiiNativeRenderGetApi native_get=(GcWiiNativeRenderGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_render_get_api");
#else
    GcWiiNativeRenderGetApi native_get=(GcWiiNativeRenderGetApi)dlsym(g->library,"gc_wii_native_render_get_api");
#endif
    const GcWiiNativeRenderApi* native_api=native_get?native_get(GC_WII_NATIVE_RENDER_ABI):NULL;
#ifdef _WIN32
    GcWiiNativeGuiGetApi gui_get=(GcWiiNativeGuiGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_gui_get_api");
    GcWiiNativeGuiLayersGetApi layers_get=(GcWiiNativeGuiLayersGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_gui_layers_get_api");
    GcWiiNativeGuiMaskGetApi mask_get=(GcWiiNativeGuiMaskGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_gui_mask_get_api");
    GcWiiNativeGuiEffectsGetApi effects_get=(GcWiiNativeGuiEffectsGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_gui_effects_get_api");
#if WIILLR_MENU_PICTURES
    GcWiiNativeGuiProgramGetApi program_get=(GcWiiNativeGuiProgramGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_gui_program_get_api");
#endif
#else
    GcWiiNativeGuiGetApi gui_get=(GcWiiNativeGuiGetApi)dlsym(g->library,"gc_wii_native_gui_get_api");
    GcWiiNativeGuiLayersGetApi layers_get=(GcWiiNativeGuiLayersGetApi)dlsym(g->library,"gc_wii_native_gui_layers_get_api");
    GcWiiNativeGuiMaskGetApi mask_get=(GcWiiNativeGuiMaskGetApi)dlsym(g->library,"gc_wii_native_gui_mask_get_api");
    GcWiiNativeGuiEffectsGetApi effects_get=(GcWiiNativeGuiEffectsGetApi)dlsym(g->library,"gc_wii_native_gui_effects_get_api");
#if WIILLR_MENU_PICTURES
    GcWiiNativeGuiProgramGetApi program_get=(GcWiiNativeGuiProgramGetApi)dlsym(g->library,"gc_wii_native_gui_program_get_api");
#endif
#endif
    const GcWiiNativeGuiApi* gui_api=gui_get?gui_get(GC_WII_NATIVE_GUI_ABI):NULL;
#if WIILLR_MENU_PICTURES
    if(!wii_menu_picture_hook_start(g,native_api,gui_api)){wii_gx_stop(g);return false;}
    if(layers_get)wii_menu_picture_hook_layers(layers_get(GC_WII_NATIVE_GUI_LAYERS_ABI));
    if(mask_get)wii_menu_picture_hook_mask(mask_get(GC_WII_NATIVE_GUI_MASK_ABI));
    if(effects_get)wii_menu_picture_hook_effects(effects_get(GC_WII_NATIVE_GUI_EFFECTS_ABI));
    if(program_get)wii_menu_picture_hook_program(program_get(GC_WII_NATIVE_GUI_PROGRAM_ABI));
#endif
#if WIILLR_MM9_RENDERER || WIILLR_MENU_FRAME_ACTIVE
#ifdef _WIN32
    GcWiiNativeFrameGetApi frame_get=(GcWiiNativeFrameGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_frame_get_api");
#else
    GcWiiNativeFrameGetApi frame_get=(GcWiiNativeFrameGetApi)dlsym(g->library,"gc_wii_native_frame_get_api");
#endif
    const GcWiiNativeFrameApi* frame_api=frame_get?frame_get(GC_WII_NATIVE_FRAME_ABI):NULL;
#if WIILLR_MM9_RENDERER
    if(!wii_mm9_renderer_hook_start(g,native_api,frame_api)){wii_gx_stop(g);return false;}
    if(!wii_mm9_renderer_bind_hbm(native_api,gui_api,
        layers_get?layers_get(GC_WII_NATIVE_GUI_LAYERS_ABI):NULL,
        mask_get?mask_get(GC_WII_NATIVE_GUI_MASK_ABI):NULL,
        effects_get?effects_get(GC_WII_NATIVE_GUI_EFFECTS_ABI):NULL)){wii_gx_stop(g);return false;}
#endif
#if WIILLR_NATIVE_OUTPUT
    if(g->output) {
#ifdef _WIN32
        GcWiiNativeOutputGetApi output_get=(GcWiiNativeOutputGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_output_get_api");
#else
        GcWiiNativeOutputGetApi output_get=(GcWiiNativeOutputGetApi)dlsym(g->library,"gc_wii_native_output_get_api");
#endif
        const GcWiiNativeOutputApi* output=output_get?output_get(GC_WII_NATIVE_OUTPUT_ABI):NULL;
        if(!wii_gx_output_bind_completion(g,output)){wii_gx_stop(g);return false;}
#if WIILLR_MM9_RENDERER
        /* Preserve the existing renderer's required extension contract. */
        if(!output){wii_gx_stop(g);return false;}
        wii_mm9_renderer_bind_output(output);
#endif
    }
#endif
#if WIILLR_MENU_FRAME_ACTIVE
    if(!wii_menu_frame_start(g,native_api,frame_api)){wii_gx_stop(g);return false;}
#ifdef _WIN32
    GcWiiNativeCaptureGetApi capture_get=(GcWiiNativeCaptureGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_capture_get_api");
    GcWiiNativeDisplayGetApi display_get=(GcWiiNativeDisplayGetApi)GetProcAddress((HMODULE)g->library,"gc_wii_native_display_get_api");
#else
    GcWiiNativeCaptureGetApi capture_get=(GcWiiNativeCaptureGetApi)dlsym(g->library,"gc_wii_native_capture_get_api");
    GcWiiNativeDisplayGetApi display_get=(GcWiiNativeDisplayGetApi)dlsym(g->library,"gc_wii_native_display_get_api");
#endif
    if(capture_get)wii_menu_frame_capture(capture_get(GC_WII_NATIVE_CAPTURE_ABI));
    if(display_get)wii_menu_frame_display(display_get(GC_WII_NATIVE_DISPLAY_ABI));
#endif
#endif
#endif
    /* The current unowned synchronous renderer publishes on this machine
     * thread. Deferred providers need memory ownership before idle admission. */
    m->idle_memory_serial=(g->api->capabilities&GCNLLR_NATIVE_GX_SERIAL_RAM_WRITES)!=0;
    m->gx_fifo=fifo;m->gx_peek=peek;m->gx_user=g;gcn_gp_set_drain_hook(&m->gp,drain,g);return true;
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
               !resolve(g,address,(u32)extent,&range))return false;
            if(g->presentation && !g->presentation->select_source(address))return false;
            const int resident=wii_gx_output_present(g,address,width,height,stride);
            if(resident<0 || (!resident && !g->api->present(range.bytes,width,height,stride)))return false;
            g->scanout_address=address;g->scanout_width=width;g->scanout_height=height;g->scanout_stride=stride;
        }
    }
    GcnllrNativeGxStatus status={0};g->api->status(&status);
    g->quit=status.quit!=0;
    if(g->quit)return !status.failed;
    const bool live_input=g->visible && !g->scripted_input;
#if WIILLR_IOS_NATIVE
    const bool native_keyboard=true;
    if(!live_input) {
        const WiiKeyboardReport disconnected={0};
        if(wii_ios_native_keyboard_input(&g->machine->ios,&disconnected)<0)return false;
        wii_keyboard_host_reset();
    }
#else
    const bool native_keyboard=false; /* Original IOS keeps its own USB bus. */
#endif
    /* Setup owns input until review explicitly hands it to the player. The
     * startup journal remains in the environment after its events finish. */
    if(live_input && (native_keyboard || g->machine->ios.bluetooth) &&
       (!g->input_sent || g->input_field!=g->machine->fields)) {
        g->input_field=g->machine->fields;
        WiiBluetoothInput next=g->input;next.buttons=0;
        GcnllrPad pad=status.pad;
        bool typing=false;
#if WIILLR_IOS_NATIVE && WIILLR_MENU_PICTURES
        typing=wii_menu_frame_keyboard_visible(g);
#endif
        wii_keyboard_host_begin(typing);
        if(g->host_input)g->host_input(g->host_input_user,&pad);
        WiiKeyboardReport keyboard;
        wii_keyboard_host_end(native_keyboard && !g->host_input_capture,&keyboard);
#if WIILLR_IOS_NATIVE
        if(wii_ios_native_keyboard_input(&g->machine->ios,&keyboard)<0)return false;
#endif
        if(g->host_input_capture)status.pointer.valid=false;
        next.buttons=wii_remote_buttons(&pad,g->title==0x0001000157523945ull);
        int x=(int)next.pointer_x-((int)pad.substick_x-128)/16;
        int y=(int)next.pointer_y-((int)pad.substick_y-128)/16;
        next.pointer_x=(u16)(x<0?0:x>1023?1023:x);next.pointer_y=(u16)(y<0?0:y>767?767:y);
        if(pad.substick_x<112 || pad.substick_x>144 || pad.substick_y<112 || pad.substick_y>144) {
            g->mouse_active=false;next.pointing=true;
        } else if(status.pointer.valid) {
            g->mouse_active=true;next.pointing=true;
            const WiiPointerCamera point=wii_pointer_camera(status.pointer.x,status.pointer.y);
            next.pointer_x=point.x;next.pointer_y=point.y;
        } else if(g->mouse_active)next.pointing=false;
        /* A+B held together performs the original menu's channel drag. */
        if(status.pointer.valid && (status.pointer.buttons&1))next.buttons|=0x800;
        if(status.pointer.valid && (status.pointer.buttons&2))next.buttons|=0x400;
        if(status.pointer.valid && (status.pointer.buttons&4))next.buttons|=0x8000;
        next.pairing=(pad.buttons&0x20)!=0;
        const bool sync=next.pairing && !g->input.pairing;
        if(g->machine->ios.bluetooth && (!g->input_sent || next.buttons!=g->input.buttons || next.pairing!=g->input.pairing ||
           next.pointer_x!=g->input.pointer_x || next.pointer_y!=g->input.pointer_y ||
           next.pointing!=g->input.pointing)) {
            if(!wii_bluetooth_set_input(g->machine->ios.bluetooth,&next))return false;
            g->input=next;g->input_sent=true;
        }
        if(sync && g->machine->ios.bluetooth && !wii_bluetooth_press_sync(g->machine->ios.bluetooth))return false;
    }
    return !status.failed;
}
void wii_gx_stop(WiiGx* g) {
    wii_keyboard_host_reset();
    wii_mm9_dlc_bind(NULL);
    wii_mm9_mods_bind(NULL);
    if(g->host_input_stop)g->host_input_stop(g->host_input_user);
    g->host_input=NULL;g->host_input_stop=NULL;g->host_settings=NULL;g->host_input_user=NULL;
    if(g->machine && g->machine->hollywood.video_user==g) {
        g->machine->hollywood.idle_dimming=NULL;g->machine->hollywood.video_user=NULL;
    }
    g->video=NULL;g->presentation=NULL;g->resolution=NULL;
#if WIILLR_MM9_RENDERER
    wii_mm9_renderer_hook_stop();
#endif
#if WIILLR_MENU_PICTURES
    wii_menu_frame_stop();
    wii_menu_picture_hook_stop();
#endif
    if(g->machine) {g->machine->gx_fifo=NULL;g->machine->gx_peek=NULL;g->machine->gx_user=NULL;gcn_gp_set_drain_hook(&g->machine->gp,NULL,NULL);}
    if(!wii_gx_output_close(g)) {
        fprintf(stderr,"Could not materialize native output at shutdown\n");
        if(g->cpu)g->cpu->halted=true;
        return; /* Preserve unresolved ownership and its live renderer. */
    }
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
