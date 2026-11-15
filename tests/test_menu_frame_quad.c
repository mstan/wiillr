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
    cpu_free(&cpu);printf("PASS %u RGBA-pointer cases, keyboard lines, invalid pointers and Menu completion ordering\n",cases);return 0;
}
