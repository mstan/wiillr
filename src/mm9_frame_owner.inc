/* SPDX-License-Identifier: MIT
 * One native graphics owner inside the verified original frame orchestration.
 * Input, logical palette updates, VRAM transfers and VI handoff stay original.
 * No graphics SDK routine, gather encoding or FIFO replay is used here. */
static struct {
    bool scope,active,view_ready,attested,initialized,deferred;
    u32 work_address,work_size;
    GcWiiNativeDraw view;
    u64 initializations,frames,setups,queues,palettes,overlays,submits,retired_copies,observable_copies,fifo_bytes,fifo_start;
} owned;
static bool owned_active(void){return owned.active;}
static bool owned_overlap(u32 a,u32 n,u32 b,u32 m) {
    a&=0x1fffffff;b&=0x1fffffff;return (u64)a+n>b && (u64)b+m>a;
}
static bool owned_read_allowed(u32 address,u32 size) {
    return !owned.deferred || !owned_overlap(address,size,owned.work_address,owned.work_size);
}
static void owned_reset(void){memset(&owned,0,sizeof owned);}
static int owned_stop(CPUState* cpu,const char* why){++uncovered;fprintf(stderr,"Native MM9 frame unsupported: %s\n",why);cpu->halted=true;return 1;}
static void owned_invalidate(u32 address,u32 size) {
    const u32 a=address&0x1fffffff;const u64 end=(u64)a+size;
    if((a<0x840f8 && end>0x83730)||(a<0x99650 && end>0x99200)||(a<0x9de40 && end>0x9dcb8)||
       (a<0xd5b90 && end>0xd5990)||(a<0xe8204 && end>0xe8024)||(a<0x1e1eac && end>0x1e1800))owned.attested=false;
}
static bool owned_attest(CPUState* cpu) {
    return identity(cpu,0x80083730,0x9c8,UINT64_C(0xe88b23cf449d4c9f)) &&
        identity(cpu,0x801e1800,0x6ac,UINT64_C(0xa03767f1e3a73d42)) &&
        identity(cpu,0x8009dcb8,0x188,UINT64_C(0x2651ec4888522db1)) &&
        identity(cpu,0x80099200,0x194,UINT64_C(0x1425568b3744f92e)) &&
        identity(cpu,0x80099394,0x25c,UINT64_C(0x0fefe46b8cc497bc)) &&
        identity(cpu,0x800995f0,0x5c,UINT64_C(0x6f0581da08be7ad6)) &&
        identity(cpu,0x800d5990,0x200,UINT64_C(0xf76ba00f650669fa)) &&
        identity(cpu,0x800e8024,0x1e0,UINT64_C(0xaf1351d129852ec9));
}
void wii_mm9_renderer_frame_marker(CPUState* cpu,u32 address) {
    if(!owner || owner->cpu!=cpu || WIILLR_MM9_RENDERER_MODE<2)return;
    if(address==0x8009dcb8) {
        if(!owned.attested)owned.attested=owned_attest(cpu);
        if(!owned.attested){owned_stop(cpu,"original frame identity");return;}
        owned.scope=true;owned.active=owned.view_ready=false;
    } else if(address==0x8009de3c) {
        if(owned.deferred)owned_stop(cpu,"unfinished frame publication");
        if(owned.active) {
            GcnllrNativeGxStatus s;owner->api->status(&s);const u64 bytes=s.fifo_bytes-owned.fifo_start;owned.fifo_bytes+=bytes;
            if(bytes || owner->machine->gp.count || gcn_cp_fifo_rw_distance(&owner->machine->cp))owned_stop(cpu,"unowned graphics in frame");
        }
        owned.scope=owned.active=owned.view_ready=false;
    }
}
static bool owned_mode(CPUState* cpu) {
    return r16(cpu,0x803b92c4)==640 && r16(cpu,0x803b92c6)==480 && r16(cpu,0x803b92c8)==480 &&
        !r8(cpu,0x803b92d8) && !r8(cpu,0x803b92d9) && r8(cpu,cpu->gpr[13]-0x5dc8)<=1 &&
        !r32(cpu,cpu->gpr[13]-0x579c) && !(cpu->fpscr&3u) && !input_invalid;
}
static int owned_effects(CPUState* cpu,GcWiiNativeClear* clear,GcWiiNativeDisplayCopy* copy,bool final) {
    if(!owned.active)return 0;input_invalid=false;
    const u32 rgba=r32(cpu,0x8031638c),index=r8(cpu,0x803b9304),sdk=r32(cpu,0x804ef8e0),control=r32(cpu,sdk+0x23c);
    if(index>1 || ((control>>7)&3))return -1;
    *clear=(GcWiiNativeClear){.width=256,.height=240,.red=rgba>>24,.green=(rgba>>16)&255,.blue=(rgba>>8)&255,.alpha=rgba&255,.depth=0xffffff,.flags=7};
    *copy=(GcWiiNativeDisplayCopy){.destination=r32(cpu,0x803b92fc+index*4),.stride=1280,.width=640,.height=480,.scale_y=256,
        .flags=(control&3)|GC_WII_NATIVE_COPY_CLEAR,.filter={0,0,21,22,21,0,0},.clear_value=*clear};
    if(final)for(u32 n=0;n<7;n++)copy->filter[n]=r8(cpu,0x803b92f2+n);
    return !input_invalid && !(copy->destination&31) && read_memory(cpu,copy->destination,1280*480)?1:-1;
}
static int owned_camera(CPUState* cpu,GcWiiNativeDraw* view) {
    (void)cpu;if(!owned.active)return 0;if(!owned.view_ready)return -1;*view=owned.view;return 1;
}
static bool owned_view(CPUState* cpu) {
    input_invalid=false;GcWiiNativeDraw* v=&owned.view;memset(v,0,sizeof *v);
    const float top=(float)(r16(cpu,0x803b92d0)>>1),bottom=-top;
    float right=(float)(r16(cpu,0x803b92ce)>>1),left=-right;
    if(r8(cpu,cpu->gpr[13]-0x5dc8)){left=rf(cpu,cpu->gpr[2]-0x7d94);right=rf(cpu,cpu->gpr[2]-0x7d90);}
    const float near=rf(cpu,cpu->gpr[2]-0x7db0),far=rf(cpu,cpu->gpr[2]-0x7d98);
    if(!(top>bottom && right>left && far>near))return false;
    v->projection[0]=2/(right-left);v->projection[3]=-(right+left)/(right-left);
    v->projection[5]=2/(top-bottom);v->projection[7]=-(top+bottom)/(top-bottom);
    v->projection[10]=-1/(far-near);v->projection[11]=-far/(far-near);v->projection[15]=1;
    float z[3],up[3],x[3],y[3];for(u32 n=0;n<3;n++){z[n]=rf(cpu,0x802536fc+n*4);up[n]=rf(cpu,0x802536f0+n*4);}
    const float zl=sqrtf(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]);if(!(zl>0))return false;
    for(u32 n=0;n<3;n++)z[n]/=zl;
    x[0]=up[1]*z[2]-up[2]*z[1];x[1]=up[2]*z[0]-up[0]*z[2];x[2]=up[0]*z[1]-up[1]*z[0];
    const float xl=sqrtf(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]);if(!(xl>0))return false;
    for(u32 n=0;n<3;n++)x[n]/=xl;
    y[0]=z[1]*x[2]-z[2]*x[1];y[1]=z[2]*x[0]-z[0]*x[2];y[2]=z[0]*x[1]-z[1]*x[0];
    for(u32 n=0;n<3;n++){v->model_view[n]=x[n];v->model_view[4+n]=y[n];v->model_view[8+n]=z[n];}
    for(u32 row=0;row<3;row++)for(u32 n=0;n<3;n++)v->model_view[row*4+3]-=v->model_view[row*4+n]*rf(cpu,0x802536fc+n*4);
    /* This is the title's public camera matrix, also used by CPU clients. */
    for(u32 n=0;n<12;n++){u32 bits;memcpy(&bits,&v->model_view[n],4);mem_write32(cpu,0x8031635c+n*4,bits);}
    v->width=v->clip_width=640;v->height=v->clip_height=480;v->far_depth=1;
    v->material.flags=GC_WII_NATIVE_BLEND|GC_WII_NATIVE_DEPTH_TEST|GC_WII_NATIVE_DEPTH_WRITE;
    owned.view_ready=!input_invalid;return owned.view_ready;
}
static void owned_put32(u8* b,u32 v){b[0]=v>>24;b[1]=v>>16;b[2]=v>>8;b[3]=v;}
static bool owned_work_is_hidden(u32 address,u32 size) {
    u32 vi,width,height,stride;
    if(gcn_vi_xfb_info(&vi,&width,&height,&stride) && width && height &&
       owned_overlap(address,size,vi,(height-1)*stride+width*2))return false;
    return !owner->scanout_width || !owner->scanout_height ||
        !owned_overlap(address,size,owner->scanout_address,(owner->scanout_height-1)*owner->scanout_stride+owner->scanout_width*2);
}
static int owned_finish_compositor(CPUState* cpu,const GcWiiNativeDisplayCopy* copy) {
    /* In the attested enclosing frame, only queue insertion, the native
     * camera/queue/empty-overlay passes and an inactive pause overlay occur
     * before final overwrite. The work XFB is not the active VI source.
     * Native CPU/resource reads from it reject until final publication. */
    const u32 mutex=0x8038e074,thread=r32(cpu,mutex+8),count=r32(cpu,mutex+12);
    if(owned.deferred || r8(cpu,0x80316d2b) || r8(cpu,0x80316d29) ||
       (thread && (thread!=r32(cpu,0x800000e4) || !count)) || input_invalid)return false;
    /* Double-buffering can temporarily expose the nominal work image to VI.
     * That publication is observable, so retain its native copy and barrier.
     * Hidden work images alone qualify for retirement. */
    if(!owned_work_is_hidden(copy->destination,copy->stride*copy->height)) {
        if(!frame_api->display_copy(copy) || !output_complete())return -1;
        ++owned.observable_copies;return 1;
    }
    GcWiiNativeClear clear=copy->clear_value;clear.x=copy->x;clear.y=copy->y;clear.width=copy->width;clear.height=copy->height;
    if(!frame_api->clear(&clear))return -1;
    owned.work_address=copy->destination;owned.work_size=copy->stride*copy->height;owned.deferred=true;++owned.retired_copies;return true;
}
static int owned_initialize(CPUState* cpu) {
    input_invalid=false;
    if(owned.initialized || !identity(cpu,0x8009905c,0x1a4,UINT64_C(0xd0a755b54ed9b1b8)) ||
       cpu->lr!=0x8009db64 || !owned_mode(cpu))return owned_stop(cpu,"graphics initialization identity/mode");
    const u32 base=cpu->gpr[3],size=cpu->gpr[4],sdk=r32(cpu,0x804ef8e0),index=r8(cpu,0x803b9304);
    if(!sdk || (sdk&3) || (base&31) || (size&31) || size<0x4000 || !read_memory(cpu,base,size) || index>1 ||
       !read_memory(cpu,sdk,0x600))return owned_stop(cpu,"graphics initialization storage");
    if(!ordered_boundary())return failure(cpu,"graphics initialization prior work");
    if(!wii_machine_claim_native_graphics(owner->machine,"mm9-frame"))
        return owned_stop(cpu,"graphics initialization guest FIFO ingress");
    /* Preserve the actual SDK FIFO object's CPU ABI and the handle returned
     * to the title. It describes the caller's real storage; the native owner
     * never binds it to command construction, gather or replay. */
    u8 fifo[32]={0};owned_put32(fifo,base);owned_put32(fifo+4,base+size-4);owned_put32(fifo+8,size);
    owned_put32(fifo+12,size-0x4000);owned_put32(fifo+16,(size/2)&~31u);
    owned_put32(fifo+20,base);owned_put32(fifo+24,base);
    if(!publish(cpu,0x80458fe0,fifo,sizeof fifo))return owned_stop(cpu,"graphics FIFO object");
    mem_write32(cpu,0x80316358,0x80458fe0);mem_write32(cpu,0x8031638c,0);mem_write32(cpu,0x80316390,0);
    /* These are caller-queryable SDK values, not commands. Pipeline state is
     * supplied by the native draw descriptors, independent of this cache. */
    mem_write32(cpu,sdk+0x22c,0x43000001);mem_write32(cpu,sdk+0x230,0x49000000);
    mem_write32(cpu,sdk+0x234,0x4a077e7f);mem_write32(cpu,sdk+0x238,0x4d000028);mem_write32(cpu,sdk+0x23c,0x52004803);
    const u32 viewport[6]={0,0,0x44200000,0x43f00000,0,0x3f800000};
    for(u32 n=0;n<6;n++)mem_write32(cpu,sdk+0x544+n*4,viewport[n]);
    const GcWiiNativeClear clear={.width=640,.height=528,.depth=0xffffff,.flags=7};
    GcWiiNativeDisplayCopy copy={.destination=r32(cpu,0x803b92fc+((index+1)&1)*4),.stride=1280,
        .width=640,.height=480,.scale_y=256,.flags=GC_WII_NATIVE_COPY_CLAMP_TOP|GC_WII_NATIVE_COPY_CLAMP_BOTTOM|GC_WII_NATIVE_COPY_CLEAR,.clear_value=clear};
    for(u32 n=0;n<7;n++)copy.filter[n]=r8(cpu,0x803b92f2+n);
    if(input_invalid)return owned_stop(cpu,"graphics initial copy settings");
    if(!frame_api->clear(&clear) || !frame_api->display_copy(&copy) || !output_complete())return failure(cpu,"native graphics initialization");
    owned.initialized=true;++owned.initializations;cpu->pc=cpu->lr;++cpu->cycles;return 1;
}
static bool owned_textures(CPUState* cpu) {
    const u32 q=0x8038e038,n=r32(cpu,q+0xd4);if(n>4)return false;
    for(u32 i=0;i<n;i++) {
        const u32 p=q+0x54+i*32,obj=r32(cpu,p),data=r32(cpu,p+4),fmt=r32(cpu,p+8),w=r16(cpu,p+12),h=r16(cpu,p+14);
        const u32 s=r32(cpu,p+16),t=r32(cpu,p+20),min=r32(cpu,p+24),mag=r32(cpu,p+28);
        if(!w || w>1024 || !h || h>1024 || s>2 || t>2 || min>1 || min!=mag || !(fmt<=6 || fmt==14))return false;
        u32 bw=4,bh=4,load=2;if(fmt==0 || fmt==14){bw=bh=8;load=fmt==0?1:0;}else if(fmt==1 || fmt==2)bw=8;else if(fmt==6)load=3;
        u8 value[32]={0};owned_put32(value,s|(t<<2)|(mag<<4)|(min<<7)|0x100);
        owned_put32(value+8,(w-1)|((h-1)<<10)|((fmt&15)<<20));owned_put32(value+12,(data>>5)&0xffffff);
        owned_put32(value+20,fmt);const u32 tiles=(((w+bw-1)/bw)*((h+bh-1)/bh))&0x7fff;
        value[28]=tiles>>8;value[29]=tiles;value[30]=load;value[31]=2;
        if(!publish(cpu,obj,value,32))return false;gcn_native_code_invalidate(obj,32);
    }
    mem_write32(cpu,q+0xd4,0);return !input_invalid;
}
static int owned_queues(CPUState* cpu) {
    input_invalid=false;const u32 q=0x8038e038,mutex=q+0x3c,thread=r32(cpu,mutex+8),count=r32(cpu,mutex+12);
    if(thread && (thread!=r32(cpu,0x800000e4) || !count))return false;
    if(!thread && (count || r32(cpu,mutex) || r32(cpu,mutex+4)))return false;
    if(!owned_textures(cpu))return 0;if(!ordered_boundary())return -1;
    const u32 rgba=r32(cpu,0x8031638c),level=r32(cpu,0x803bb32c),color_address=cpu->gpr[13]-0x5d20;
    const u16 color=((rgba>>16)&0xf800)|((rgba>>13)&0x7e0)|((rgba>>11)&31);mem_write16(cpu,color_address,color);
    WiiMm9Screen bg={0};bg.draw=owned.view;bg.draw.material.texture=(GcWiiNativeTexture){.address=color_address&0x1fffffff,.size=32,.format=4,.width=1,.height=1};
    const u32 packed=level>=32?65535:((level&31)<<11)|((level&31)<<6)|(level&31);
    const u32 red=(packed>>11)&31,green=(packed>>5)&63,blue=packed&31;
    const float tint[4]={((red<<3)|(red>>2))/255.f,((green<<2)|(green>>4))/255.f,((blue<<3)|(blue>>2))/255.f,1};
    const float xs[4]={-416,-416,416,416},ys[4]={240,-240,-240,240};
    for(u32 i=0;i<4;i++){bg.vertices[i].position[0]=xs[i];bg.vertices[i].position[1]=ys[i];bg.vertices[i].position[2]=75;memcpy(bg.vertices[i].color,tint,sizeof tint);bg.vertices[i].uv[0]=i>=2;bg.vertices[i].uv[1]=i==0 || i==3;}
    if(wii_mm9_screen_draw_prepared(screen_renderer,color_address,&bg)!=1)return -1;
    for(int depth=3;depth>=0;depth--)for(int list=2;list>=0;list--) {
        const u32 n=r32(cpu,q+0x30+list*4);if(n>4)return false;
        for(u32 i=0;i<n;i++) {
            const u32 obj=r32(cpu,q+list*16+i*4);if(r8(cpu,obj+0x38)!=(u32)depth)continue;
            const int draw=wii_mm9_screen_draw(screen_renderer,obj,level,&owned.view);if(draw!=1)return draw;
            const u8 tags[4]={0x80,0x84,0x88,0x94};for(u32 k=0;k<4;k++)mem_write8(cpu,obj+k*4,tags[k]);
        }
    }
    return !input_invalid;
}
static int owned_call(CPUState* cpu,u32 address) {
    const u32 p=address&0x1fffffff;if(WIILLR_MM9_RENDERER_MODE<2)return 0;
    if(p==0x9905c){if(cpu->cycles>=cpu->cycle_deadline)return 1;return owned_initialize(cpu);}
    if(p==0x1131a8 && owned.initialized) {
        if(cpu->cycles>=cpu->cycle_deadline)return 1;
        /* The native renderer resolves current asset bytes at submission; it
         * has no guest texture cache to invalidate. Asset loaders can still
         * request invalidation outside a frame. Retain the SDK's CPU-visible
         * flush latch without constructing or replaying its four BP commands. */
        input_invalid=false;
        if(!identity(cpu,0x801131a8,0x48,UINT64_C(0x76c472702915e9fa)))
            return owned_stop(cpu,"texture invalidation identity");
        const u32 sdk=r32(cpu,cpu->gpr[2]-0x7bc0);
        if(!sdk || !read_memory(cpu,sdk,4) || input_invalid)
            return owned_stop(cpu,"texture invalidation storage");
        mem_write16(cpu,sdk+2,0);cpu->pc=cpu->lr;++cpu->cycles;return 1;
    }
    if(!owned.scope)return 0;
    if(p!=0x99200 && p!=0x99394 && p!=0x995f0 && p!=0xd5990 && p!=0xe8024 && p!=0x83730)return 0;
    if(cpu->cycles>=cpu->cycle_deadline)return 1;
    if(p==0x99200) {
        input_invalid=false;if(!owned_mode(cpu))return owned_stop(cpu,"frame mode");
        if(!ordered_boundary())return failure(cpu,"frame prior completion");
        GcnllrNativeGxStatus s;owner->api->status(&s);owned.fifo_start=s.fifo_bytes;owned.active=true;++owned.frames;++owned.setups;
    } else if(!owned.active)return owned_stop(cpu,"frame phase ordering");
    else if(p==0x99394){if(!owned_view(cpu))return owned_stop(cpu,"native camera");++owned.setups;}
    else if(p==0xe8024){if(cpu->gpr[3]>3)return owned_stop(cpu,"palette group");++owned.palettes;}
    else if(p==0x83730) {
        if(r32(cpu,0x80305230)!=0x8030522c) {
            if(!owned.view_ready || input_invalid)return owned_stop(cpu,"overlay camera/list");
            const int result=wii_mm9_overlay_draw(overlay_renderer,0x8030522c,0x80305210,0x80272f90,r32(cpu,0x803bb32c),&owned.view);
            if(result<0)return failure(cpu,"native overlay submission");
            if(!result){WiiMm9OverlayStatus s={0};wii_mm9_overlay_status(overlay_renderer,&s);return owned_stop(cpu,s.error);}
        }
        if(input_invalid)return owned_stop(cpu,"overlay draw list");
        ++owned.overlays;
    }
    else if(p==0xd5990) {
        if(!owned.view_ready)return owned_stop(cpu,"queued camera");const int queue=owned_queues(cpu);
        if(queue<0)return failure(cpu,"native queued submission");if(!queue)return owned_stop(cpu,"queued graphics contract");++owned.queues;
    }
    else if(p==0x995f0) {
        if(owned.deferred) {
            if(!owned_work_is_hidden(owned.work_address,owned.work_size) ||
               r32(cpu,0x803b92fc+r8(cpu,0x803b9304)*4)!=owned.work_address || input_invalid)return owned_stop(cpu,"work XFB became observable");
            owned.deferred=false;
        }
        GcWiiNativeClear clear;GcWiiNativeDisplayCopy copy;
        if(owned_effects(cpu,&clear,&copy,true)!=1)return owned_stop(cpu,"final copy settings");
        /* With native startup there is no SDK draw-done waiter/interrupt to
         * wake. The typed barrier retires the same ordered copy effects. */
        if(!ordered_boundary() || !frame_api->display_copy(&copy) ||
           !(owned.initialized?output_complete():frame_api->complete(GC_WII_NATIVE_COMPLETE_DRAW_DONE,0)))return failure(cpu,"native final submission");
        ++owned.submits;cpu->pc=0x800e766c;++cpu->cycles;return 1;
    }
    cpu->pc=cpu->lr;++cpu->cycles;return 1;
}
static void owned_summary(void) {
    printf("{\"native_mm9_frame_owner\":1,\"initializations\":%llu,\"frames\":%llu,\"setups\":%llu,\"queue_passes\":%llu,\"palette_groups\":%llu,\"overlay_passes\":%llu,\"submissions\":%llu,\"retired_intermediate_copies\":%llu,\"observable_intermediate_copies\":%llu,\"scope_fifo_bytes\":%llu}\n",(unsigned long long)owned.initializations,(unsigned long long)owned.frames,(unsigned long long)owned.setups,(unsigned long long)owned.queues,(unsigned long long)owned.palettes,(unsigned long long)owned.overlays,(unsigned long long)owned.submits,(unsigned long long)owned.retired_copies,(unsigned long long)owned.observable_copies,(unsigned long long)owned.fifo_bytes);
}
