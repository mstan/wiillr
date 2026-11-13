/* SPDX-License-Identifier: MIT */
#include <wiillr/menu_frame_hook.h>
#include <wiillr/menu_picture_hook.h>
#include <wiillr/gx_output.h>
#include <memory/memory.h>
#include <cpu/native_code.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef WIILLR_MENU_FRAME_MODE
#define WIILLR_MENU_FRAME_MODE 0
#endif
#define SYSTEM_FRAMEWORK 0x8108907cu
#define GRAPHICS_ARG 0x810897c0u
static struct {
    WiiGx* owner;const GcWiiNativeRenderApi* render;const GcWiiNativeFrameApi* frame;const GcWiiNativeCaptureApi* capture;
    WiiMenuView view;float model[12];bool active,bad,projection_ready;u32 object;u64 mesh;
    GcWiiNativeVertex vertices[4];u64 initializers,begins,copies,completions,views,quads,isolated_buffers,uncovered,failed,fifo_start,fifo_bytes;
} state;
static const u8* bytes(CPUState* cpu,u32 address,u32 size) {
    const u32 a=address&0x1fffffff;u32 available;
    if(!(a<cpu->ram_size || (a>=0x10000000 && a-0x10000000<cpu->mem2_size))){state.bad=true;return NULL;}
    const u8* p=gcn_mem_resolve(cpu,address,&available);if(!p || size>available){state.bad=true;return NULL;}return p;
}
static u32 word(CPUState* cpu,u32 address){const u8* p=bytes(cpu,address,4);return p?((u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3]):0;}
static u16 half(CPUState* cpu,u32 address){const u8* p=bytes(cpu,address,2);return p?((u16)p[0]<<8|p[1]):0;}
static float real(CPUState* cpu,u32 address){u32 n=word(cpu,address);float f;memcpy(&f,&n,4);if(!isfinite(f))state.bad=true;return f;}
static void store_real(CPUState* cpu,u32 address,float f){u32 n;memcpy(&n,&f,4);mem_write32(cpu,address,n);}
static bool identity(CPUState* cpu,u32 address,u32 size,u64 hash) {
    const u8* p=bytes(cpu,address,size);if(!p)return false;u64 h=UINT64_C(14695981039346656037);
    for(u32 i=0;i<size;i++){h^=p[i];h*=UINT64_C(1099511628211);}return h==hash;
}
static int stop(CPUState* cpu,const char* why,bool backend) {
    if(backend)++state.failed;else ++state.uncovered;
    fprintf(stderr,"Native Menu frame %s: %s\n",backend?"failed":"unsupported",why);cpu->halted=true;return 1;
}
static bool mode(CPUState* cpu,u32 object,u32* output) {
    if(!object || (object&3))return false;
    const u32 p=word(cpu,object);if(!p || (p&3) || !bytes(cpu,p,0x3c))return false;
    if(word(cpu,p)!=0 || word(cpu,p+0x14)!=1 || half(cpu,p+4)!=640 || half(cpu,p+6)!=456 || half(cpu,p+8)!=456 ||
       bytes(cpu,p+0x18,1)[0] || bytes(cpu,p+0x19,1)[0])return false;
    *output=p;return !state.bad;
}
static bool set_viewport(CPUState* cpu,u32 object) {
    u32 p;if(!mode(cpu,object,&p))return false;
    state.object=object;state.view.x=state.view.y=0;state.view.width=state.view.clip_width=half(cpu,p+4);
    state.view.height=state.view.clip_height=half(cpu,p+6);state.view.clip_x=state.view.clip_y=0;
    state.view.near_depth=0;state.view.far_depth=1;++state.views;return !state.bad;
}
static bool frontier(void) {
    WiiGx* g=state.owner;WiiMachine* m=g->machine;CPUState* cpu=g->cpu;
    if(m->gp.count){const u8 nop[32]={0};if(!gcn_gp_write_stream(&m->gp,cpu,nop,32-m->gp.count))return false;}
    while(m->cp.gp_read_enable && gcn_cp_fifo_rw_distance(&m->cp)>=32 && !gcn_cp_at_breakpoint(&m->cp)) {
        u32 n;const u8* p=gcn_mem_resolve(cpu,gcn_cp_fifo_read_pointer(&m->cp),&n);
        if(!p || n<32 || !g->api->fifo(p,32))return false;gcn_cp_gpu_consume_chunk(&m->cp);
    }
    return !gcn_cp_fifo_rw_distance(&m->cp) && g->api->drain() && state.frame->complete(0,0);
}
static int copy_frame(CPUState* cpu,u32 object) {
    u32 p;if(!mode(cpu,object,&p))return false;const u32 index=word(cpu,object+4);if(index>1)return false;
    const GcWiiNativeClear clear={.width=640,.height=456,.depth=0xffffff,.flags=7};
    GcWiiNativeDisplayCopy copy={.destination=word(cpu,object+8+index*4),.stride=1280,.width=640,.height=456,.scale_y=256,
        .flags=GC_WII_NATIVE_COPY_CLAMP_TOP|GC_WII_NATIVE_COPY_CLAMP_BOTTOM|GC_WII_NATIVE_COPY_CLEAR,.clear_value=clear};
    for(u32 i=0;i<7;i++)copy.filter[i]=bytes(cpu,p+0x32+i,1)[0];
    if(state.bad || (copy.destination&31) || !bytes(cpu,copy.destination,1280*456))return false;
    if(!state.frame->display_copy(&copy))return -1;
    state.view.depth_flags=GC_WII_NATIVE_DEPTH_TEST|GC_WII_NATIVE_DEPTH_WRITE;state.view.depth_func=7;++state.copies;return true;
}
static void camera_identity(CPUState* cpu) {
    for(u32 n=0;n<12;n++){state.model[n]=n==0 || n==5 || n==10?1:0;store_real(cpu,GRAPHICS_ARG+0x40+n*4,state.model[n]);}
}
static bool ortho(CPUState* cpu,u32 address) {
    if(cpu->fpscr&3u)return false;
    const u32 object=state.object?state.object:word(cpu,SYSTEM_FRAMEWORK);u32 p;if(!mode(cpu,object,&p))return false;
    float tx=0,ty=0,sx=1,sy=1,left,right,top,bottom;
    if(address==0x81362bfc){right=half(cpu,p+4)/2;left=-right;top=half(cpu,p+6)/2;bottom=-top;}
    else {
        u32 t=GRAPHICS_ARG+0x70,s=GRAPHICS_ARG+0x7c;
        if(address==0x81362b4c){t=cpu->gpr[3];s=cpu->gpr[4];}
        if(address!=0x81362acc){tx=real(cpu,t);ty=real(cpu,t+4);sx=real(cpu,s);sy=real(cpu,s+4);}
        const u32 aspect=word(cpu,object+0x10);if(aspect>1 || !sx || !sy)return false;
        left=tx-(aspect?416.f:304.f)/sx;right=tx+(aspect?416.f:304.f)/sx;top=ty+228.f/sy;bottom=ty-228.f/sy;
    }
    if(state.bad || left==right || top==bottom)return false;
    float m[16]={0};m[0]=2/(right-left);m[3]=-(right+left)/(right-left);m[5]=2/(top-bottom);m[7]=-(top+bottom)/(top-bottom);
    m[10]=-.005f;m[11]=-.5f;m[15]=1;
    for(u32 n=0;n<16;n++){state.view.projection[n]=m[n];store_real(cpu,GRAPHICS_ARG+n*4,m[n]);}state.projection_ready=true;
    if(address!=0x81362b4c)camera_identity(cpu);return !state.bad;
}
static int submit_quad(const GcWiiNativeDraw* view,const GcWiiNativeVertex* vertices) {
    if(!state.mesh || memcmp(state.vertices,vertices,sizeof state.vertices)) {
        const uint16_t indices[6]={0,1,2,0,2,3};const GcWiiNativeMesh m={vertices,indices,4,6};const u64 handle=state.render->create_mesh(&m);
        if(!handle)return -1;if(state.mesh)state.render->destroy_mesh(state.mesh);state.mesh=handle;memcpy(state.vertices,vertices,sizeof state.vertices);
    }
    GcWiiNativeDraw draw=*view;draw.mesh=state.mesh;if(!state.render->draw(&draw))return -1;++state.quads;return 1;
}
static int utility_quad(CPUState* cpu,u32 address) {
    const u32 rect=cpu->gpr[3],texture=cpu->gpr[4],rgba=cpu->gpr[address==0x81362d3c?4:5];
    if(!state.active || !state.projection_ready || (cpu->fpscr&3u))return 0;
    const float left=real(cpu,rect),top=real(cpu,rect+4),right=real(cpu,rect+8),bottom=real(cpu,rect+12);
    if(state.bad || left==right || top==bottom)return 0;
    GcWiiNativeDraw draw={0};memcpy(draw.projection,state.view.projection,sizeof draw.projection);memcpy(draw.model_view,state.model,sizeof draw.model_view);
    draw.x=state.view.x;draw.y=state.view.y;draw.width=state.view.width;draw.height=state.view.height;draw.near_depth=state.view.near_depth;draw.far_depth=state.view.far_depth;
    draw.clip_x=state.view.clip_x;draw.clip_y=state.view.clip_y;draw.clip_width=state.view.clip_width;draw.clip_height=state.view.clip_height;draw.material.flags=GC_WII_NATIVE_BLEND;
    GcWiiNativeVertex v[4]={0};const float xs[4]={left,left,right,right},ys[4]={top,bottom,bottom,top};
    for(u32 i=0;i<4;i++){v[i].position[0]=xs[i];v[i].position[1]=ys[i];for(u32 c=0;c<4;c++)v[i].color[c]=((rgba>>(24-c*8))&255)/255.f;}
    if(address==0x81362fbc) {
        GcWiiNativeTexture* t=&draw.material.texture;const u32 image=word(cpu,texture+8),sampler=word(cpu,texture);t->address=(word(cpu,texture+12)&0xffffff)<<5;
        t->width=(image&1023)+1;t->height=((image>>10)&1023)+1;t->format=(image>>20)&15;
        u32 bw=4,bh=4,block=32;if(t->format==0 || t->format==14)bw=bh=8;else if(t->format==1 || t->format==2)bw=8;else if(t->format==6)block=64;else if(t->format<3 || t->format>5)return 0;
        t->size=((t->width+bw-1)/bw)*((t->height+bh-1)/bh)*block;t->wrap_s=sampler&3;t->wrap_t=(sampler>>2)&3;t->linear=(sampler>>4)&1;
        if(t->wrap_s>2 || t->wrap_t>2 || (((sampler>>5)&7)!=0 && ((sampler>>5)&7)!=4) || ((sampler>>7)&1)!=t->linear || (sampler&0x19fe00) || !bytes(cpu,t->address,t->size))return 0;
        const u32 orientation=cpu->gpr[7];const float scale=cpu->gpr[6]&255;const u8 uv[3][8]={{0,0,0,1,1,1,1,0},{0,1,1,1,1,0,0,0},{1,0,0,0,0,1,1,1}};
        if(orientation>2)return 0;for(u32 i=0;i<4;i++){v[i].uv[0]=uv[orientation][i*2]*scale;v[i].uv[1]=uv[orientation][i*2+1]*scale;}
    }
    state.view.depth_flags=0;state.view.depth_func=7;return state.bad?0:submit_quad(&draw,v);
}
static int fader(CPUState* cpu,u32 object) {
    if(cpu->fpscr&3u)return 0;
    const u32 rgba=word(cpu,object+0x10);if(!(rgba&255))return state.bad?0:1;
    const float l=real(cpu,object+0x14),t=real(cpu,object+0x18),r=real(cpu,object+0x1c),b=real(cpu,object+0x20);
    if(state.bad || l<0 || t<0 || r<=l || b<=t || r>640 || b>528)return 0;
    GcWiiNativeDraw draw={0};draw.projection[0]=2/(r-l);draw.projection[3]=-(r+l)/(r-l);
    draw.projection[5]=2/(t-b);draw.projection[7]=-(t+b)/(t-b);draw.projection[10]=-1;draw.projection[11]=-1;draw.projection[15]=1;
    draw.model_view[0]=draw.model_view[5]=draw.model_view[10]=1;draw.x=l;draw.y=t;draw.width=r-l;draw.height=b-t;draw.far_depth=1;
    draw.clip_x=(int)l;draw.clip_y=(int)t;draw.clip_width=(int)(r-l);draw.clip_height=(int)(b-t);draw.material.flags=(rgba&255)==255?0:GC_WII_NATIVE_BLEND;
    GcWiiNativeVertex v[4]={0};const float xs[4]={l,r,r,l},ys[4]={t,t,b,b};
    for(u32 i=0;i<4;i++){v[i].position[0]=xs[i];v[i].position[1]=ys[i];for(u32 c=0;c<4;c++)v[i].color[c]=((rgba>>(24-c*8))&255)/255.f;}
    memcpy(state.view.projection,draw.projection,sizeof draw.projection);memcpy(state.model,draw.model_view,sizeof state.model);state.projection_ready=true;
    state.view.x=l;state.view.y=t;state.view.width=r-l;state.view.height=b-t;state.view.clip_x=draw.clip_x;state.view.clip_y=draw.clip_y;state.view.clip_width=draw.clip_width;state.view.clip_height=draw.clip_height;state.view.depth_flags=state.view.depth_func=0;
    return submit_quad(&draw,v);
}
static bool fifo_object(CPUState* cpu) {
    const u32 base=cpu->gpr[3],size=cpu->gpr[4];if((base&31) || (size&31) || size<0x4000 || !bytes(cpu,base,size))return false;
    if(!wii_machine_claim_native_graphics(state.owner->machine,"ipl43u-frame"))return false;
    const u32 fields[8]={base,base+size-4,size,size-0x4000,(size/2)&~31u,base,base,0};
    for(u32 n=0;n<8;n++)mem_write32(cpu,0x81116460+n*4,fields[n]);cpu->gpr[3]=0x81116460;return !state.bad;
}
static int layout_draw(CPUState* cpu,u32 address) {
    const bool explicit_pane=address==0x8136a88c;
    if(!identity(cpu,address,0x6c,explicit_pane?UINT64_C(0xd2c58b541773f9da):UINT64_C(0xc49bd6bc4144b921)))return stop(cpu,"layout draw identity",false);
    const u32 object=cpu->gpr[3];if(!object || (object&3))return stop(cpu,"layout object",false);
    const u32 pane=explicit_pane?cpu->gpr[4]:word(cpu,object+0x14);if(!pane || (pane&3))return stop(cpu,"layout root pane",false);
    const u32 table=word(cpu,pane);if(!table || (table&3))return stop(cpu,"layout pane table",false);
    const u32 target=word(cpu,table+0x14);
    if(!target || (target&3) || !bytes(cpu,object+0x298,0x54) || !bytes(cpu,target,4) || state.bad)return stop(cpu,"layout draw dispatch",false);
    /* Preserve the original virtual Pane::Draw traversal and DrawInfo. Only
     * the enclosing layout's repeated graphics-state construction is gone. */
    state.view.depth_flags=0;state.view.depth_func=7;cpu->gpr[3]=pane;cpu->gpr[4]=object+0x298;cpu->pc=target;++cpu->cycles;return 1;
}
static bool channel_clip(CPUState* cpu) {
    if((cpu->fpscr&3u) || !state.active || !state.projection_ready)return false;
    const u32 scene=cpu->gpr[3],channel=cpu->gpr[4];
    if(!scene || !channel)return false;
    const u32 layout=word(cpu,channel+0x30),pane=layout?word(cpu,layout+0x14):0;
    if(!pane)return false;
    const float vx=real(cpu,pane+0x2c),vy=real(cpu,pane+0x30),ox=real(cpu,scene+0xd0),oy=real(cpu,scene+0xd4);
    const u32 phase=word(cpu,scene+0xc0),aspect=word(cpu,state.object+0x10);if(aspect>1)return false;
    const float ratio=640.f/(aspect?832.f:608.f);float x,y,w,h;
    if(phase==9 || phase==14 || phase==16) {
        const float sx=real(cpu,scene+0xe4),sy=real(cpu,scene+0xe8),tx=real(cpu,scene+0xd8),ty=real(cpu,scene+0xdc);
        if(!sx || !sy)return false;
        const float right=tx+(aspect?416.f:304.f)/sx,left=tx-(aspect?416.f:304.f)/sx,top=ty+228.f/sy,bottom=ty-228.f/sy;
        const float px=vx*(2/(right-left))-(right+left)/(right-left),py=vy*(2/(top-bottom))-(top+bottom)/(top-bottom);
        w=2*(ox*sx)*ratio;h=2*(oy*sy);y=(456.f-((1+py)*456.f/2))-(oy*sy);x=((1+px)*640.f/2)-((ox*sx)*ratio);
    }else {w=2*ox*ratio;y=(228.f-vy)-oy;x=320.f+((vx-ox)*ratio);h=2*oy;}
    x-=1;y-=1;w+=2;h+=2;
    if(state.bad || !isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || w<0 || h<0)return false;
    if(x>=640 || x+w<=0 || y>=456 || y+h<=0){state.view.clip_x=state.view.clip_y=state.view.clip_width=state.view.clip_height=0;return true;}
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>1705)w-=(x+w)-1705;if(y+h>1705)h-=(y+h)-1705;
    state.view.clip_x=(u32)x;state.view.clip_y=(u32)y;state.view.clip_width=(u32)w;state.view.clip_height=(u32)h;return true;
}

/* The original build owns its view until the enclosing frame provider is
 * selected. Keeping this seam in one source also leaves standalone material
 * adapters independent of guest SDK cache representation. */
int wii_menu_frame_current_view(CPUState* cpu,WiiMenuView* view) {
    if(!WIILLR_MENU_FRAME_MODE || !state.owner || state.owner->cpu!=cpu)return 0;
    if(!state.active || !state.projection_ready || !view)return -1;
    *view=state.view;return 1;
}
bool wii_menu_frame_start(WiiGx* gx,const GcWiiNativeRenderApi* render,const GcWiiNativeFrameApi* frame) {
    if(!WIILLR_MENU_FRAME_MODE)return true;
    if(state.owner || !gx || !render || !frame || render->abi!=GC_WII_NATIVE_RENDER_ABI || frame->abi!=GC_WII_NATIVE_FRAME_ABI)return false;
    memset(&state,0,sizeof state);state.owner=gx;state.render=render;state.frame=frame;
    state.view.depth_flags=GC_WII_NATIVE_DEPTH_TEST|GC_WII_NATIVE_DEPTH_WRITE;state.view.depth_func=7;return true;
}
void wii_menu_frame_capture(const GcWiiNativeCaptureApi* api) {
    state.capture=api && api->abi==GC_WII_NATIVE_CAPTURE_ABI?api:NULL;
}
int wii_menu_frame_entry(CPUState* cpu,u32 address) {
    if(!WIILLR_MENU_FRAME_MODE || !state.owner || state.owner->cpu!=cpu)return 0;
    if(cpu->cycles>=cpu->cycle_deadline)return 1;state.bad=false;
    if(address==0x8133e2d4) {
        /* The two original XFB allocations otherwise share protected pages
         * with live heap headers and neighboring objects. Their unrelated
         * reads would force a full GPU download every frame. Keep the real
         * guest allocator and ownership, but isolate its private buffers.
         * Padding changes no displayed dimensions or RAM observation rule. */
        u32 p;
        if(!identity(cpu,0x8133e288,0x88,UINT64_C(0xc869ded5426aa24f)) ||
           !mode(cpu,cpu->gpr[27],&p) || cpu->gpr[3]!=640u*456u*2u ||
           cpu->gpr[5]!=32 || cpu->gpr[29]>1 || cpu->gpr[31]!=cpu->gpr[29]*4)
            return stop(cpu,"framebuffer allocation identity/mode",false);
        const u32 alignment=wii_gx_output_allocation_alignment(state.owner);
        if(alignment<32 || (alignment&(alignment-1)) || alignment>0x10000)
            return stop(cpu,"framebuffer allocation alignment",false);
        cpu->gpr[3]=(cpu->gpr[3]+alignment-1)&~(alignment-1);
        cpu->gpr[5]=alignment;
        if(alignment>32)++state.isolated_buffers;
        cpu->lr=address+4;cpu->pc=0x815f80d4;++cpu->cycles;return 1;
    } else if(address==0x8133de74) {
        if(!identity(cpu,address,4,UINT64_C(0x381876d7414e6544)) || !set_viewport(cpu,cpu->gpr[3]))return stop(cpu,"frame begin identity/mode",false);
        if(!frontier())return stop(cpu,"earlier graphics completion",true);GcnllrNativeGxStatus s;state.owner->api->status(&s);state.fifo_start=s.fifo_bytes;
        state.active=true;++state.begins;
    } else if(address==0x8133de78) {
        if(!identity(cpu,address,0x98,UINT64_C(0x60f33f6b5eb35553)))return stop(cpu,"frame copy identity",false);
        if(!frontier())return stop(cpu,"frame copy ordering",true);const int copied=copy_frame(cpu,cpu->gpr[3]);
        if(copied!=1)return stop(cpu,"frame copy mode/submission",copied<0);
    } else if(address==0x8133df24) {
        if(!identity(cpu,0x8133df10,0x48,UINT64_C(0x4eee3be39c10ead6)))return stop(cpu,"frame handoff identity",false);
        if(!state.frame->complete(0,0))return stop(cpu,"frame completion",true);
        if(state.active){GcnllrNativeGxStatus s;state.owner->api->status(&s);const u64 n=s.fifo_bytes-state.fifo_start;state.fifo_bytes+=n;
            if(n || state.owner->machine->gp.count || gcn_cp_fifo_rw_distance(&state.owner->machine->cp))return stop(cpu,"unowned graphics in frame",false);}
        state.active=false;++state.completions;cpu->pc=0x8133df28;++cpu->cycles;return 1;
    } else if(address==0x81334c7c) {
        if(!identity(cpu,0x81334b28,0x3e4,UINT64_C(0x79ec3ff84f08995c)) || !set_viewport(cpu,word(cpu,SYSTEM_FRAMEWORK)))return stop(cpu,"scene viewport identity/mode",false);
        cpu->gpr[27]=word(cpu,state.object);cpu->pc=0x81334cd4;++cpu->cycles;return 1;
    } else if(address==0x81362a6c || address==0x81362acc || address==0x81362b4c || address==0x81362bfc) {
        const u32 sizes[4]={0x60,0x80,0xb0,0xec},addresses[4]={0x81362a6c,0x81362acc,0x81362b4c,0x81362bfc};
        const u64 hashes[4]={UINT64_C(0xf8f76977982cf244),UINT64_C(0xd1674c8bfe0e4a8b),UINT64_C(0x6141a49f0b456d91),UINT64_C(0x2078c008a767f632)};
        u32 i=0;while(i<4 && addresses[i]!=address)i++;
        if(i==4 || !identity(cpu,address,sizes[i],hashes[i]) || !ortho(cpu,address))return stop(cpu,"orthographic view",false);
    } else if(address==0x81362cf8) {
        if(!identity(cpu,address,0x44,UINT64_C(0x067d8421e917c51b)))return stop(cpu,"camera identity",false);
        for(u32 i=0;i<12;i++)state.model[i]=real(cpu,GRAPHICS_ARG+0x40+i*4);if(state.bad)return stop(cpu,"camera matrix",false);
    } else if(address==0x81362d3c || address==0x81362fbc) {
        if(!identity(cpu,address,address==0x81362d3c?0x26c:0x2e0,address==0x81362d3c?UINT64_C(0x55ddc65ee9f6e70f):UINT64_C(0xa44dc2f4069a6270)))return stop(cpu,"utility draw identity",false);
        const int drawn=utility_quad(cpu,address);if(drawn!=1)return stop(cpu,"utility draw input/submission",drawn<0);
    } else if(address==0x8136339c) {
        if(!identity(cpu,address,0xc0,UINT64_C(0x76b8cf20247ebf19)) || !state.active || !state.capture || cpu->gpr[4]>1)
            return stop(cpu,"channel texture capture identity/provider",false);
        const u32 object=cpu->gpr[3];
        const GcWiiNativeTextureCopy copy={.destination=word(cpu,object+0x10),.x=half(cpu,object),.y=half(cpu,object+2),
            .width=half(cpu,object+4),.height=half(cpu,object+6),.format=word(cpu,object+8)};
        if(state.bad)return stop(cpu,"channel texture capture object",false);
        if(!frontier() || !state.capture->texture_copy(&copy))return stop(cpu,"channel texture capture submission",true);
    } else if(address==0x815f7170) {
        if(!identity(cpu,address,0x298,UINT64_C(0xd8d23825eaf80f93)))return stop(cpu,"fader identity",false);
        const int drawn=fader(cpu,cpu->gpr[3]);if(drawn!=1)return stop(cpu,"fader input/submission",drawn<0);
    } else if(address==0x815f7ca4) {
        if(!identity(cpu,0x815f7c5c,0x68,UINT64_C(0xd6de00181d4af4b3)) || !fifo_object(cpu))return stop(cpu,"graphics initializer storage/identity",false);
        ++state.initializers;cpu->pc=0x815f7ca8;++cpu->cycles;return 1;
    } else if(address==0x81334328) {
        if(!identity(cpu,0x8133400c,0xac4,UINT64_C(0x08fa40f95bcc1e55)))return stop(cpu,"title graphics initialization identity",false);
        /* The native service owns its surface format; typed draw/copy masks
         * carry the RGBA contract. Retain the surrounding heap/UI setup. */
        cpu->pc=address+4;++cpu->cycles;return 1;
    } else if(address==0x81355b88) {
        if(!identity(cpu,0x81355b68,0x40,UINT64_C(0xecafffa52519c96c)))return stop(cpu,"keyboard drawing restore identity",false);
        /* The original virtual keyboard draw already completed. Restore its
         * enclosing graphics contract before the original epilogue returns. */
        state.view.depth_flags=GC_WII_NATIVE_DEPTH_TEST|GC_WII_NATIVE_DEPTH_WRITE;state.view.depth_func=7;
        cpu->pc=0x81355b98;++cpu->cycles;return 1;
    } else if(address==0x813adfdc) {
        if(!identity(cpu,address,0x3a4,UINT64_C(0xe65a50e40d62c4d4)) || !channel_clip(cpu))return stop(cpu,"channel thumbnail clipping",false);
    } else if(address==0x813ab1e0) {
        if(!identity(cpu,0x813ab0e8,0x26c,UINT64_C(0xfaa498c6658639a3)))return stop(cpu,"channel scene view restore identity",false);
        state.view.clip_x=state.view.clip_y=0;state.view.clip_width=640;state.view.clip_height=456;
        cpu->gpr[29]=456;cpu->pc=0x813ab200;++cpu->cycles;return 1;
    } else if(address==0x8152373c) {
        if(!identity(cpu,address,0x140,UINT64_C(0x9b6d6dc9e8e91a29)))return stop(cpu,"Window draw identity",false);
        return wii_menu_picture_hook_window(cpu);
    } else if(address==0x8136a79c || address==0x8136a88c)return layout_draw(cpu,address);
    else if(address==0x8136a83c) {
        if(!identity(cpu,0x8136a808,0x84,UINT64_C(0xe40b8539125074df)))return stop(cpu,"named layout draw identity",false);
        state.view.depth_flags=0;state.view.depth_func=7;cpu->pc=0x8136a85c;++cpu->cycles;return 1;
    } else if(address==0x8133e230 || address==0x8133df74 || address==0x8133dfc0 || address==0x8133dfe4) {
        const bool initial=address==0x8133e230;
        if(!identity(cpu,initial?0x8133e1e4:0x8133df58,initial?0xa4:0xb4,initial?UINT64_C(0xc6f024b757f76456):UINT64_C(0x39ca0bddb70fd23e)))return stop(cpu,"render-mode graphics setup identity",false);
        u32 p;if(!mode(cpu,cpu->gpr[31],&p))return stop(cpu,"render-mode graphics setup mode",false);
        cpu->pc=initial?0x8133e26c:address==0x8133df74?0x8133dfb0:address+4;++cpu->cycles;return 1;
    } else return 0;
    cpu->pc=cpu->lr;++cpu->cycles;return 1;
}
void wii_menu_frame_stop(void) {
    if(!state.owner)return;if(state.mesh)state.render->destroy_mesh(state.mesh);
    printf("{\"native_menu_frame\":1,\"initializers\":%llu,\"begins\":%llu,\"copies\":%llu,\"completions\":%llu,\"views\":%llu,\"utility_quads\":%llu,\"isolated_buffers\":%llu,\"uncovered\":%llu,\"failed\":%llu,\"scope_fifo_bytes\":%llu}\n",(unsigned long long)state.initializers,(unsigned long long)state.begins,(unsigned long long)state.copies,(unsigned long long)state.completions,(unsigned long long)state.views,(unsigned long long)state.quads,(unsigned long long)state.isolated_buffers,(unsigned long long)state.uncovered,(unsigned long long)state.failed,(unsigned long long)state.fifo_bytes);
    memset(&state,0,sizeof state);
}
