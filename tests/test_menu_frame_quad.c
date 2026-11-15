/* SPDX-License-Identifier: MIT */
/* Exercise the private typed-quad boundary without a GPU or desktop window.
 * Build this TU with function/data sections and linker GC, linking the normal
 * gcn_runtime_core; unused frame-owner paths are deliberately not required. */
#ifndef WIILLR_MENU_QUAD_SOURCE
#define WIILLR_MENU_QUAD_SOURCE "../src/menu_frame_hook.c"
#endif
#include WIILLR_MENU_QUAD_SOURCE
#include <stdlib.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
static GcWiiNativeVertex captured[4];
static GcWiiNativeDraw draw;
static unsigned submitted;
static unsigned frontier_order,frontier_completions;
static bool frontier_drains=true;
static unsigned display_calls,display_numerator,display_denominator;
static bool display_ready=true;
static WiiMachine* gather_machine;
static unsigned gather_retired,gather_fallthrough;
static void gather_bridge(CPUState* cpu,u32 address,u64 value,u8 width) {
    if(wii_menu_frame_gather(gather_machine,cpu,address,value,width))++gather_retired;
    else ++gather_fallthrough;
}
static int display_aspect(u32 numerator,u32 denominator) {
    ++display_calls;if(!display_ready)return 0;
    display_numerator=numerator;display_denominator=denominator;return 1;
}
static int drain_frontier(void){if(frontier_order)abort();frontier_order=1;return frontier_drains;}
static int complete_frontier(u32 events,u32 token){if(events || token || frontier_order!=1)abort();frontier_order=2;++frontier_completions;return 1;}
static u64 create(const GcWiiNativeMesh* mesh) {
    if(mesh->vertex_count!=4 || mesh->index_count!=6)return 0;
    memcpy(captured,mesh->vertices,sizeof captured);return 1;
}
static int destroy(u64 handle){(void)handle;return 1;}
static int submit(const GcWiiNativeDraw* value){draw=*value;++submitted;return 1;}
static void put(CPUState* cpu,u32 address,u32 value) {
    u32 available;u8* p=gcn_mem_resolve(cpu,address,&available);
    if(!p || available<4)abort();
    p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value;
}
static void put_float(CPUState* cpu,u32 address,float value){u32 bits;memcpy(&bits,&value,4);put(cpu,address,bits);}
int main(void) {
    CPUState cpu;CHECK(cpu_init(&cpu));
    const GcWiiNativeRenderApi api={.abi=GC_WII_NATIVE_RENDER_ABI,.create_mesh=create,.destroy_mesh=destroy,.draw=submit};
    const u32 rect=0x80001000,texture=0x80001100;
    put_float(&cpu,rect,0);put_float(&cpu,rect+4,0);put_float(&cpu,rect+8,640);put_float(&cpu,rect+12,456);
    /* 4x4 RGBA8 texture with a simple clamp/nearest descriptor. */
    put(&cpu,texture,0);put(&cpu,texture+8,3u|(3u<<10)|(6u<<20));put(&cpu,texture+12,0x3000>>5);
    const u32 values[]={0xffffffff,0xffffff00,0x000000ff,0x00000080,0x12345678};
    const u32 aliases[]={0x00001201,0x80001201,0xc0001201};
    unsigned cases=0;
    for(unsigned kind=0;kind<2;++kind)for(unsigned a=0;a<3;++a)for(unsigned n=0;n<5;++n) {
        memset(&state,0,sizeof state);state.active=state.projection_ready=true;state.render=&api;
        state.view.width=state.view.clip_width=640;state.view.height=state.view.clip_height=456;state.view.far_depth=1;
        const u32 color=aliases[a],rgba=values[n];put(&cpu,color,rgba);
        cpu.gpr[3]=rect;cpu.gpr[4]=kind?texture:color;cpu.gpr[5]=color;cpu.gpr[6]=1;cpu.gpr[7]=0;
        const unsigned before=submitted;
        CHECK(utility_quad(&cpu,kind?0x81362fbc:0x81362d3c)==1 && submitted==before+1);
        for(unsigned v=0;v<4;++v)for(unsigned ch=0;ch<4;++ch)
            CHECK(captured[v].color[ch]==(float)((rgba>>(24-ch*8))&255)/255.f);
        CHECK(draw.material.flags==GC_WII_NATIVE_BLEND); /* src-alpha/inverse-src-alpha; depth off */
        CHECK((draw.material.texture.size!=0)==(kind!=0));
        ++cases;
    }
    for(unsigned kind=0;kind<2;++kind) {
        memset(&state,0,sizeof state);state.active=state.projection_ready=true;state.render=&api;
        cpu.gpr[3]=rect;cpu.gpr[4]=kind?texture:0x81800000;cpu.gpr[5]=0x81800000;
        const unsigned before=submitted;CHECK(!utility_quad(&cpu,kind?0x81362fbc:0x81362d3c));
        CHECK(state.bad && submitted==before);
    }
    /* These endpoints come from the original quad-producing line service:
     * width6 is one pixel, centered on a vertical/horizontal segment. */
    cpu.gpr[2]=0x80010000;
    put_float(&cpu,cpu.gpr[2]-30000,0);put_float(&cpu,cpu.gpr[2]-29992,6);put_float(&cpu,cpu.gpr[2]-29996,.5f);
    const struct {double x1,y1,x2,y2;u32 width;float left,top,right,bottom;} lines[]={
        {10,20,10,40,6,9.5f,20,10.5f,40},
        {10,20,40,20,6,10,19.5f,40,20.5f},
        {40,20,10,20,12,40,19,10,21},
        {-2,-8,-2,4,18,-3.5f,-8,-.5f,4},
        {10.00000001,20,10.00000002,40,6,10,19.5f,10,40.5f},
    };
    for(unsigned n=0;n<sizeof lines/sizeof *lines;++n) {
        memset(&state,0,sizeof state);state.active=state.projection_ready=true;state.render=&api;
        state.view.width=state.view.clip_width=640;state.view.height=state.view.clip_height=456;state.view.far_depth=1;
        cpu.gpr[3]=lines[n].width;cpu.gpr[4]=0xc0001201;put(&cpu,cpu.gpr[4],0x12345678);
        cpu.fpr[1]=lines[n].x1;cpu.fpr[2]=lines[n].y1;cpu.fpr[3]=lines[n].x2;cpu.fpr[4]=lines[n].y2;cpu.fpr[5]=.25;
        const unsigned before=submitted;CHECK(keyboard_line(&cpu)==1);
        /* The near-equal double endpoints round to a degenerate float quad;
         * original double comparison still selects the horizontal branch. */
        if(n==4){CHECK(submitted==before);continue;}
        CHECK(submitted==before+1);
        const float x[]={lines[n].left,lines[n].left,lines[n].right,lines[n].right};
        const float y[]={lines[n].top,lines[n].bottom,lines[n].bottom,lines[n].top};
        for(unsigned v=0;v<4;++v){CHECK(captured[v].position[0]==x[v]&&captured[v].position[1]==y[v]&&captured[v].position[2]==.25f);CHECK(captured[v].color[3]==120.f/255.f);}
        CHECK(draw.model_view[0]==1&&draw.model_view[5]==1&&draw.model_view[10]==1);
        CHECK(draw.model_view[3]==0&&draw.model_view[7]==0&&draw.model_view[11]==0);
        CHECK(draw.material.flags==GC_WII_NATIVE_BLEND&&!draw.material.texture.size);
    }
    const unsigned before=submitted;cpu.gpr[4]=0x81800000;state.bad=false;
    CHECK(!keyboard_line(&cpu)&&state.bad&&submitted==before);
    /* Actual Menu frontier preserves the earlier drain and zero PE events
     * when the portable/no-guarded-owner completion floor is selected. */
    WiiMachine machine={0};WiiGx gx={.cpu=&cpu,.machine=&machine};
    const GcnllrNativeGxApi graphics={.drain=drain_frontier};
    const GcWiiNativeFrameApi frame={.abi=GC_WII_NATIVE_FRAME_ABI,.complete=complete_frontier};
    gx.api=&graphics;state.owner=&gx;state.frame=&frame;
    frontier_order=0;CHECK(frontier() && frontier_order==2 && frontier_completions==1);
    frontier_order=0;frontier_drains=false;
    CHECK(!frontier() && frontier_order==1 && frontier_completions==1);
    /* Camera getters are shared with other overlays; only the original
     * keyboard draw wrapper marks text entry for the completed frame. */
    state.keyboard_visible=false;state.keyboard=false;state.active=state.projection_ready=true;
    keyboard_frame_begin();CHECK(keyboard_begin(&cpu));state.keyboard_drawn=true;
    keyboard_frame_end();CHECK(state.keyboard_visible);
    keyboard_frame_begin();keyboard_frame_end();CHECK(!state.keyboard_visible); /* Back/non-keyboard frame */
    state.keyboard_visible=true;state.mesh=0;wii_menu_frame_stop();CHECK(!state.owner && !state.keyboard_visible); /* title retirement */
    /* The real original Menu mode controls display aspect without changing
     * the physical viewport, RAM, or an existing service's legacy contract. */
    const u32 object=0x80002000,mode_data=0x80002100;
    put(&cpu,object,mode_data);put(&cpu,mode_data+4,(640u<<16)|456u);
    put(&cpu,mode_data+8,456u<<16);put(&cpu,mode_data+0x14,1);
    put(&cpu,object+0x10,1);memset(&state,0,sizeof state);
    CHECK(set_viewport(&cpu,object)); /* Optional extension absent. */
    const GcWiiNativeDisplayApi display={GC_WII_NATIVE_DISPLAY_ABI,display_aspect};
    wii_menu_frame_display(&display);
    CHECK(set_viewport(&cpu,object) && display_calls==1 && display_numerator==16 && display_denominator==9);
    CHECK(state.view.width==640 && state.view.height==456 && half(&cpu,mode_data+4)==640);
    CHECK(set_viewport(&cpu,object) && display_calls==1);
    put(&cpu,object+0x10,0);CHECK(set_viewport(&cpu,object) && display_calls==2 && !display_numerator && !display_denominator);
    put(&cpu,object+0x10,2);CHECK(!set_viewport(&cpu,object) && display_calls==2);
    put(&cpu,object+0x10,1);display_ready=false;
    CHECK(!set_viewport(&cpu,object) && display_calls==3 && !state.display_aspect);
    display_ready=true;CHECK(set_viewport(&cpu,object) && display_calls==4 && state.display_aspect==1);
    /* Cache retirement must leave original CPU callbacks and region tables
     * intact. Public synthetic instructions avoid embedding title code. */
    memset(&state,0,sizeof state);gx.title=UINT64_C(0x0000000100000002);
    machine.gx_native_owner="ipl43u-frame";state.owner=&gx;state.sdk_verified=true;
    const u32 sdk=0x80008000,callbacks=0x80009000;
    put(&cpu,sdk+0x518,callbacks);put(&cpu,sdk+0x51c,callbacks+4);
    for(unsigned n=0;n<20;n++)put(&cpu,sdk+600+n*16,0x10000000u+n);
    u8 logical[0x600];memcpy(logical,cpu.ram+0x8000,sizeof logical);
    const CPUState unchanged=cpu;
    for(unsigned slot=0;slot<8;slot++) {
        const u32 commands[]={0x815460bc,0x815460d4,0x815460e8,0x815460f4,0x815460fc,0x81546104,0x81546150};
        const u32 payloads[]={0x815460c8,0x815460dc,0x815460f0,0x815460f8,0x81546100,0x81546120,0x81546158};
        const u32 bases[]={0x80,0x84,0x88,0x8c,0x90,0x94,0x98};
        cpu.gpr[31]=slot;
        for(unsigned n=0;n<7;n++) {
            cpu.pc=commands[n];const CPUState before=cpu;
            CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1));
            CHECK(!memcmp(&before,&cpu,sizeof cpu));
            cpu.pc=payloads[n];const u32 value=((bases[n]+slot+(slot<4?0:28))<<24)|0x123456;
            CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,value,4));
            CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,value^0x01000000,4));
        }
    }
    CHECK(state.sdk_texture_writes==112 && !memcmp(logical,cpu.ram+0x8000,sizeof logical));
    const u32 last_pc=cpu.pc;cpu.pc=0x815460cc;
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1));cpu.pc=last_pc;
    WiiMachine foreign={0};CHECK(!wii_menu_frame_gather(&foreign,&cpu,0xcc008000,0x61,1));
    gx.title=0;CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1));gx.title=UINT64_C(0x0000000100000002);
    machine.gx_native_owner=NULL;CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1));machine.gx_native_owner="ipl43u-frame";
    cpu=unchanged;state.sdk_initializing=true;state.sdk_thread=0x8000a000;
    put(&cpu,0x800000e4,state.sdk_thread);cpu.pc=0x81540000;
    put(&cpu,cpu.pc,0x98000000);put(&cpu,cpu.pc+4,0x90000000); /* stb/stw */
    put(&cpu,0x800000e4,state.sdk_thread+4);
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1) && !state.sdk_setup.command);
    put(&cpu,0x800000e4,state.sdk_thread);
    CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1));cpu.pc+=4;
    CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x40000017,4));
    CHECK(!state.sdk_setup.command && state.sdk_init_writes==2);
    CHECK(sdk_bp_state(0x5800000f) && !sdk_bp_state(0x58000000));
    cpu.pc-=4;CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x08,1));
    CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x20,1));cpu.pc+=4;
    CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0,4));CHECK(!state.sdk_setup.command);
    cpu.pc-=4;CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x10,1));cpu.pc+=4;
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x00101000,4)); /* 17 XF words */
    CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x00001000,4));
    CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x3f800000,4));CHECK(!state.sdk_setup.command);
    /* Use the actual PSQ and memory helpers, rather than assuming that a
     * 64-bit instruction reaches MMIO as one width8 callback. */
    gather_machine=&machine;cpu.external_write=gather_bridge;
    put(&cpu,0x81540008,0xf0000000); /* synthetic PSQ_ST f0,0(r0),W0,GQR0 */
    cpu.gqr[0]=0;cpu.msr|=0x2000u;cpu.hid2|=PPC_HID2_PSE|PPC_HID2_LSQE;cpu.fpr[0]=1;cpu.ps1[0]=2;
    cpu.pc=0;mem_write8_cia(&cpu,0xcc008000,0x10,0x81540000);
    mem_write32_cia(&cpu,0xcc008000,0x00011000,0x81540004);
    const unsigned pair_before=gather_retired;
    cpu.pc=0x81540008; /* PSQ emitter publishes CIA before the existing helper. */
    ppc_psq_store(&cpu,0,0xcc008000,false,0,false,0x81540008);
    CHECK(!cpu.exception && gather_retired==pair_before+2 && !gather_fallthrough && !state.sdk_setup.command);
    CHECK(cpu.pc==0x81540008);
    put(&cpu,0x8154000c,0xd8000000); /* synthetic STFD */
    mem_write8_cia(&cpu,0xcc008000,0x10,0x81540000);
    mem_write32_cia(&cpu,0xcc008000,0x00011000,0x81540004);
    const unsigned wide_before=gather_retired;
    mem_write64_cia(&cpu,0xcc008000,UINT64_C(0x3ff0000000000000),0x8154000c);
    CHECK(gather_retired==wide_before+1 && !state.sdk_setup.command && cpu.pc==0x8154000c);
    mem_write32_cia(&cpu,0xcc008004,0,0x81540004);CHECK(gather_fallthrough==1); /* scalar +4 remains raw */
    put(&cpu,0x800000e4,state.sdk_thread+4);
    mem_write8_cia(&cpu,0xcc008000,0x61,0x81540000);CHECK(gather_fallthrough==2 && !state.sdk_setup.command);
    put(&cpu,0x800000e4,state.sdk_thread);
    cpu.external_write=unchanged.external_write;cpu.pc=0x81540004;
    cpu.pc-=4;CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x90,1)); /* draw */
    CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1));cpu.pc+=4;
    const u32 saved_word=state.sdk_setup.word;const u8 saved_header=state.sdk_setup.header;
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x45000002,4)); /* draw-done */
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x52004000,4)); /* EFB copy */
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x65000000,4)); /* palette upload */
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x63000000,4)); /* texture preload */
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x4c000000,4)); /* reserved register */
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x5a000000,4));
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0xff000000,4));
    CHECK(state.sdk_setup.word==saved_word && state.sdk_setup.header==saved_header);
    CHECK(wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x41000000,4));
    cpu.gpr[3]=0x81116460;state.sdk_fifo=0x80010000;state.sdk_size=0x4000;
    put(&cpu,cpu.gpr[3],state.sdk_fifo);put(&cpu,cpu.gpr[3]+8,state.sdk_size);
    CHECK(fifo_complete(&cpu) && !state.sdk_initializing && state.initializers==1);
    CHECK(!memcmp(logical,cpu.ram+0x8000,sizeof logical));
    CHECK(word(&cpu,sdk+0x518)==callbacks && word(&cpu,sdk+0x51c)==callbacks+4);
    cpu=unchanged;state.sdk_verified=true;wii_menu_frame_invalidate(0x81546000,4);CHECK(!state.sdk_verified);
    CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1));
    wii_menu_frame_stop();CHECK(!wii_menu_frame_gather(&machine,&cpu,0xcc008000,0x61,1));
    cpu_free(&cpu);printf("PASS %u RGBA-pointer cases, keyboard lifecycle, Menu completion and display mode\n",cases);return 0;
}
