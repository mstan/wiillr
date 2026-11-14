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
    cpu_free(&cpu);printf("PASS %u solid/textured RGBA-pointer cases and invalid-pointer rejection\n",cases);return 0;
}
