/* SPDX-License-Identifier: MIT */
#include <wiillr/mm9_renderer_hook.h>
#include <wiillr/mm9_compositor.h>
#include <wiillr/mm9_screen.h>
#include <wiillr/mm9_overlay.h>
#include <wiillr/mm9_hbm.h>
#include <memory/memory.h>
#include <cpu/native_code.h>
#include <debug/cost_ring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#define COMPOSITOR 0x800e7308u
#define SCREEN 0x800d5e50u
#define AFTER_COMPOSITOR 0x80099394u
#define FINAL_TEXTURE 0x8039b2c0u
static WiiGx* owner;
static const GcWiiNativeFrameApi* frame_api;
static const GcWiiNativeOutputApi* output_api;
void wii_mm9_renderer_bind_output(const GcWiiNativeOutputApi* api) {output_api=api;}
static int output_complete(void) {return output_api?output_api->submit():frame_api->complete(0,0);}
static WiiMm9ScreenNative* screen_renderer;
static WiiMm9OverlayNative* overlay_renderer;
static WiiMm9Hbm* hbm_renderer;
static WiiMm9Frame* prepared;
static u8* oracle_entry;
static bool verified,oracle_pending,input_invalid;
static u64 frames,uncovered,failed,oracle_checked,oracle_different;
static u32 oracle_max_delta;
static const char* oracle_directory;
static bool view_enabled,view_ready;
static u32 view_fixed_width,view_width;
static u8* view_rgba;
static u32 view_capacity;
static WiiMm9MarginSprite margin_sprites[4096];
static WiiMm9SpriteBatch sprite_batch;
static u32 margin_sprite_count;
static bool sprite_attested,sprite_oracle,sprite_pending;
static u64 sprite_calls,sprite_records,sprite_margins,sprite_oracle_checked;
bool wii_mm9_renderer_view_supported(void) {
    return WIILLR_MM9_RENDERER_MODE>=2 && WIILLR_MM9_FRAME_MODE;
}
bool wii_mm9_renderer_set_view(WiiGx* gx,WiiMm9View mode) {
    if(!wii_mm9_renderer_view_supported() || gx!=owner || !gx || !gx->presentation ||
       gx->title!=0x0001000157523945ull || mode<WII_MM9_VIEW_ORIGINAL || mode>WII_MM9_VIEW_FIT)
        return false;
    view_enabled=mode!=WII_MM9_VIEW_ORIGINAL;
    if(!view_enabled)margin_sprite_count=0;
    view_fixed_width=mode==WII_MM9_VIEW_16_9?342:mode==WII_MM9_VIEW_21_9?448:0;
    view_ready=false;
    return true;
}
static const u8* read_memory(void*,u32,u32);
static bool prepare_view(CPUState* cpu) {
    view_ready=false;
    char error[192];const WiiMm9Memory memory={cpu,read_memory};
    if(!view_enabled) {
        if(wii_mm9_composite_prepare(memory,prepared,error,sizeof error))return true;
        fprintf(stderr,"Native MM9 compositor: %s\n",error);return false;
    }
    u32 width=view_fixed_width;
    if(!width) {
        u32 w,h;if(!owner->presentation->window_size(&w,&h) || !h)return false;
        const double desired=256.0*0.75*(double)w/(double)h;
        width=desired>=WII_MM9_VIEW_MAX_WIDTH?WII_MM9_VIEW_MAX_WIDTH:
            desired<=256?256:2u*(u32)(desired/2.0+0.5);
    }
    view_width=width;if(width==256) {
        if(wii_mm9_composite_prepare(memory,prepared,error,sizeof error))return true;
        fprintf(stderr,"Native MM9 compositor: %s\n",error);return false;
    }
    const u32 bytes=width*240*4;
    if(bytes>view_capacity) {
        void* next=realloc(view_rgba,bytes);if(!next)return false;
        view_rgba=next;view_capacity=bytes;
    }
    if(!wii_mm9_composite_present_sprites_prepare(memory,width,margin_sprites,margin_sprite_count,
        prepared,view_rgba,view_capacity,error,sizeof error)) {
        fprintf(stderr,"Native MM9 view: %s\n",error);return false;
    }
    view_ready=true;return true;
}
#if WIILLR_MM9_FRAME_MODE
static int owned_effects(CPUState*,GcWiiNativeClear*,GcWiiNativeDisplayCopy*,bool);
static int owned_camera(CPUState*,GcWiiNativeDraw*);
static int owned_call(CPUState*,u32);
static void owned_reset(void);
static void owned_invalidate(u32,u32);
static void owned_summary(void);
static bool owned_active(void);
static bool owned_read_allowed(u32,u32);
static int owned_finish_compositor(CPUState*,const GcWiiNativeDisplayCopy*);
#else
void wii_mm9_renderer_frame_marker(CPUState* cpu,u32 address){(void)cpu;(void)address;}
#endif
static const u8* read_memory(void* user,u32 address,u32 size) {
#if WIILLR_MM9_FRAME_MODE
    if(!owned_read_allowed(address,size))return NULL;
#endif
    CPUState* cpu=user;const u32 physical=address&0x1fffffffu;
    if(!(physical<cpu->ram_size || (physical>=0x10000000u && physical-0x10000000u<cpu->mem2_size)))return NULL;
    u32 available;const u8* bytes=gcn_mem_resolve(cpu,address,&available);return bytes && size<=available?bytes:NULL;
}
static u32 r32(CPUState* cpu,u32 address) {
    const u8* b=read_memory(cpu,address,4);if(!b){input_invalid=true;return 0;}return (u32)b[0]<<24|(u32)b[1]<<16|(u32)b[2]<<8|b[3];
}
static u16 r16(CPUState* cpu,u32 address) {
    const u8* b=read_memory(cpu,address,2);if(!b){input_invalid=true;return 0;}return (u16)b[0]<<8|b[1];
}
static u8 r8(CPUState* cpu,u32 address) {const u8* b=read_memory(cpu,address,1);if(!b){input_invalid=true;return 0;}return *b;}
static float rf(CPUState* cpu,u32 address){const u32 bits=r32(cpu,address);float value;memcpy(&value,&bits,sizeof value);return value;}
static bool identity(CPUState* cpu,u32 address,u32 size,u64 expected) {
    const u8* b=read_memory(cpu,address,size);if(!b)return false;u64 h=UINT64_C(14695981039346656037);
    for(u32 n=0;n<size;n++){h^=b[n];h*=UINT64_C(1099511628211);}return h==expected;
}
static bool attest(CPUState* cpu) {
    return identity(cpu,COMPOSITOR,0x108,UINT64_C(0x20a0940bded73216)) && identity(cpu,SCREEN,0x21c,UINT64_C(0xd63d49fecbf1e422));
}
static bool ordered_boundary(void) {
    WiiMachine* m=owner->machine;CPUState* cpu=owner->cpu;
#if WIILLR_MM9_FRAME_MODE
    /* Inside one graphics owner, typed operations already share recording
     * order. Reject unexpected guest ingress instead of inserting a CPU/GPU
     * completion barrier between each native phase. */
    if(owned_active())return !m->gp.count && !gcn_cp_fifo_rw_distance(&m->cp);
#endif
    if(m->gp.count){const u8 padding[32]={0};if(!gcn_gp_write_stream(&m->gp,cpu,padding,32-m->gp.count))return false;}
    while(m->cp.gp_read_enable && gcn_cp_fifo_rw_distance(&m->cp)>=32 && !gcn_cp_at_breakpoint(&m->cp)) {
        u32 available;const u8* bytes=gcn_mem_resolve(cpu,gcn_cp_fifo_read_pointer(&m->cp),&available);
        if(!bytes || available<32 || !owner->api->fifo(bytes,32))return false;gcn_cp_gpu_consume_chunk(&m->cp);
    }
    return !gcn_cp_fifo_rw_distance(&m->cp) && owner->api->drain() && output_complete();
}
static int unsupported(CPUState* cpu,const char* why) {
    ++uncovered;if(uncovered<=8)fprintf(stderr,"Native MM9 unsupported: %s\n",why);
#if WIILLR_MM9_FRAME_MODE
    /* An owned frame has no legacy setup to fall back into. */
    if(owned_active()){cpu->halted=true;return 1;}
#endif
    if(WIILLR_MM9_RENDERER_MODE!=2){cpu->halted=true;return 1;}return 0;
}
static int failure(CPUState* cpu,const char* why){++failed;fprintf(stderr,"Native MM9 failed: %s\n",why);cpu->halted=true;return 1;}
static bool frame_effects(CPUState* cpu,GcWiiNativeClear* clear,GcWiiNativeDisplayCopy* copy) {
#if WIILLR_MM9_FRAME_MODE
    const int native=owned_effects(cpu,clear,copy,false);if(native)return native>0;
#endif
    input_invalid=false;const u32 sdk=r32(cpu,0x804ef8e0),source=r32(cpu,sdk+0x230),size=r32(cpu,sdk+0x234),stride=r32(cpu,sdk+0x238),control=r32(cpu,sdk+0x23c);
    const u32 cmode=r32(cpu,sdk+0x220),depth=r32(cpu,sdk+0x228),format=r32(cpu,sdk+0x22c)&7;
    /* The original GXDrawDone wait is internal to this replaced compositor.
     * No title callback is registered on the supported route. A callback
     * would be caller-visible and must be implemented before accepting it. */
    if(r32(cpu,cpu->gpr[13]-0x579c) || format>1 || r8(cpu,0x803b92c0+0x19) || (control&(1u<<10)) || r16(cpu,0x803b92c0+6)!=r16(cpu,0x803b92c0+8))return false;
    const u32 rgba=r32(cpu,0x8031638c);*clear=(GcWiiNativeClear){.width=256,.height=240,.red=rgba>>24,.green=(rgba>>16)&255,.blue=(rgba>>8)&255,.alpha=rgba&255,.depth=0xffffff,
        .flags=((cmode&8)?GC_WII_NATIVE_CLEAR_COLOR:0)|((format==1 && (cmode&16))?GC_WII_NATIVE_CLEAR_ALPHA:0)|((depth&16)?GC_WII_NATIVE_CLEAR_DEPTH:0)};
    const u32 index=r8(cpu,0x803b92c0+0x44);if(index>1)return false;
    *copy=(GcWiiNativeDisplayCopy){.destination=r32(cpu,0x803b92c0+0x3c+index*4),.stride=(stride&1023)*32,.x=source&1023,.y=(source>>10)&1023,.width=(size&1023)+1,.height=((size>>10)&1023)+1,
        .scale_y=256,.gamma=(control>>7)&3,.flags=(control&3)|GC_WII_NATIVE_COPY_CLEAR,.filter={0,0,21,22,21,0,0},.clear_value=*clear};
    return !input_invalid && !copy->gamma && copy->width==640 && copy->height==480 && copy->stride==1280 && !(copy->destination&31) && read_memory(cpu,copy->destination,copy->stride*copy->height);
}
static bool camera(CPUState* cpu,GcWiiNativeDraw* out) {
#if WIILLR_MM9_FRAME_MODE
    const int native=owned_camera(cpu,out);if(native)return native>0;
#endif
    input_invalid=false;memset(out,0,sizeof *out);const u32 sdk=r32(cpu,0x804ef8e0),type=r32(cpu,sdk+0x528),mode=r32(cpu,sdk+0x220),depth=r32(cpu,sdk+0x228);
    if(type>1 || (mode&2) || (mode&(1u<<11)) || (mode&24)!=24 || (r32(cpu,sdk+0x224)&0x100))return false;
    if((mode&1) && (((mode>>8)&7)!=4 || ((mode>>5)&7)!=5))return false;
    const u32 comparison=(depth>>1)&7;if((depth&1) && comparison!=3 && comparison!=7)return false;
    out->material.flags=((mode&1)?GC_WII_NATIVE_BLEND:0)|((depth&1)?GC_WII_NATIVE_DEPTH_TEST:0)|((depth&16)?GC_WII_NATIVE_DEPTH_WRITE:0)|((comparison==7)?GC_WII_NATIVE_DEPTH_ALWAYS:0);
    for(u32 n=0;n<12;n++)out->model_view[n]=rf(cpu,0x8031635c+n*4);
    out->projection[0]=rf(cpu,sdk+0x52c);out->projection[5]=rf(cpu,sdk+0x534);out->projection[10]=rf(cpu,sdk+0x53c);out->projection[11]=rf(cpu,sdk+0x540);
    out->projection[type?3:2]=rf(cpu,sdk+0x530);out->projection[type?7:6]=rf(cpu,sdk+0x538);if(type)out->projection[15]=1;else out->projection[14]=-1;
    out->x=rf(cpu,sdk+0x544);out->y=rf(cpu,sdk+0x548);out->width=rf(cpu,sdk+0x54c);out->height=rf(cpu,sdk+0x550);out->near_depth=rf(cpu,sdk+0x554);out->far_depth=rf(cpu,sdk+0x558);
    const u32 top=r32(cpu,sdk+0x148),bottom=r32(cpu,sdk+0x14c);out->clip_x=(int)((top>>12)&2047)-342;out->clip_y=(int)(top&2047)-342;
    out->clip_width=(int)((bottom>>12)&2047)-342-out->clip_x+1;out->clip_height=(int)(bottom&2047)-342-out->clip_y+1;return !input_invalid;
}
static bool publish(CPUState* cpu,u32 address,const u8* bytes,u32 size) {
    u8* target=(u8*)read_memory(cpu,address,size);if(!target)return false;memcpy(target,bytes,size);const u32 physical=address&0x1fffffffu;gcn_native_code_content_dirty(physical,size);
    if(cpu->reserve_valid){const u32 reservation=gcn_reservation_key(cpu->reserve_addr)&~31u;if((u64)reservation+32>physical && reservation<(u64)physical+size)cpu->reserve_valid=false;}return true;
}
static int sprite_finish(CPUState* cpu) {
    if(view_enabled && sprite_batch.margin_count) {
        if(sprite_batch.margin_count>4096-margin_sprite_count || (margin_sprite_count &&
           sprite_batch.margins[0].before<margin_sprites[margin_sprite_count-1].before))
            return failure(cpu,"native margin sprite queue capacity/order");
        memcpy(margin_sprites+margin_sprite_count,sprite_batch.margins,sprite_batch.margin_count*sizeof *margin_sprites);
        margin_sprite_count+=sprite_batch.margin_count;
    }
    ++sprite_calls;sprite_records+=sprite_batch.size/8;sprite_margins+=sprite_batch.margin_count;return 0;
}
static int sprite_call(CPUState* cpu,u32 physical) {
    if(WIILLR_MM9_RENDERER_MODE<2 || !WIILLR_MM9_FRAME_MODE || owner->title!=0x0001000157523945ull)return 0;
    if(physical==0xa7764 || physical==0xa77c4) {margin_sprite_count=0;sprite_pending=false;return 0;}
    if(physical==0xe88d8 && sprite_pending) {
        sprite_pending=false;
        const u8* actual=sprite_batch.size?read_memory(cpu,sprite_batch.address,sprite_batch.size):NULL;
        if(r32(cpu,0x8037cc30)!=sprite_batch.end || (sprite_batch.size &&
           (!actual || memcmp(actual,sprite_batch.packed,sprite_batch.size))))return failure(cpu,"original/native sprite queue mismatch");
        ++sprite_oracle_checked;return sprite_finish(cpu);
    }
    if(physical!=0xe8700)return 0;
    if(cpu->cycles>=cpu->cycle_deadline)return 1;
    if(!sprite_attested)sprite_attested=identity(cpu,0x800e8700,0x1e4,UINT64_C(0x51a3a13c724ad69a));
    if(!sprite_attested || sprite_pending)return failure(cpu,"sprite emitter identity/reentry");
    char error[192];
    if(!wii_mm9_sprites_prepare((WiiMm9Memory){cpu,read_memory},cpu->gpr[3],cpu->gpr[4],
        &sprite_batch,error,sizeof error))return failure(cpu,error);
    if(sprite_oracle){sprite_pending=true;return 0;}
    if(sprite_batch.size && !publish(cpu,sprite_batch.address,sprite_batch.packed,sprite_batch.size))return failure(cpu,"native sprite queue publication");
    if(sprite_batch.admitted){mem_write32(cpu,0x8037cc30,sprite_batch.end);cpu->gpr[3]=0x8037c428;}
    if(sprite_finish(cpu))return 1;
    cpu->pc=cpu->lr;++cpu->cycles;return 1;
}
static void overlay_write_byte(void* user,u32 address,u8 value){mem_write8(user,address,value);}
bool wii_mm9_renderer_bind_hbm(const GcWiiNativeRenderApi* render,const GcWiiNativeGuiApi* gui,
    const GcWiiNativeGuiLayersApi* layers,const GcWiiNativeGuiMaskApi* mask,const GcWiiNativeGuiEffectsApi* effects) {
#if WIILLR_MM9_FRAME_MODE
    if(WIILLR_MM9_RENDERER_MODE>=2) {
        if(!owner || hbm_renderer)return false;
        hbm_renderer=wii_mm9_hbm_create((WiiMenuMemory){owner->cpu,read_memory},render,gui,layers,mask,effects);
        return hbm_renderer!=NULL;
    }
#endif
    return true;
}
#if WIILLR_MM9_FRAME_MODE
#include "mm9_frame_owner.inc"
#endif
static bool clear_retired_atlases(CPUState* cpu,const GcWiiNativeClear* value) {
    /* The atlas images are internal to this replaced compositor, but their
     * copy-clears affect the later caller-visible XFB. Preserve that union
     * of EFB regions without building or copying any intermediate atlas. */
    const u32 config=r32(cpu,0x803012c0),enabled=r32(cpu,config)>>8;
    for(u32 n=0;n<4;n++) {
        const u32 id=r32(cpu,0x80281e48+n*4);if(!(enabled&(id&15)))continue;
        const u16 attributes=r16(cpu,config+8+(id>>4)*2);
        GcWiiNativeClear atlas=*value;atlas.width=256u<<((attributes>>14)&1);atlas.height=256u<<((attributes>>15)&1);
        if(!frame_api->clear(&atlas))return false;
    }
    return !input_invalid;
}
static bool retire_logical_inputs(CPUState* cpu) {
    input_invalid=false;
    if(!r8(cpu,0x80316d28+3) && r8(cpu,0x80316d28+4)<2) {
        for(u32 n=0;n<4;n++){const u32 slot=r32(cpu,0x80281e48+n*4)>>4;if(slot>3)return false;mem_write8(cpu,0x8039b190+0x90+slot,0);mem_write32(cpu,0x8039b190+0x94+slot*4,0);}
        mem_write32(cpu,0x8037c428+0x800,0x8037c428);mem_write32(cpu,0x8037c428+0x808,0x8037c428);mem_write32(cpu,0x8037c428+0x80c,0x8037c428);mem_write32(cpu,0x8037c428+0x810,0x8037cc28);
          margin_sprite_count=0;
    }
    mem_write32(cpu,0x803b9308,prepared->final_sprite_count);mem_write32(cpu,0x803b930c,0);return !input_invalid;
}
static void dump(const char* name,const void* bytes,u32 size) {
    if(!oracle_directory || !bytes)return;char path[1024];snprintf(path,sizeof path,"%s/%s",oracle_directory,name);FILE* file=fopen(path,"wb");if(file){fwrite(bytes,1,size,file);fclose(file);}
}
static int compare_oracle(CPUState* cpu) {
    oracle_pending=false;if(!ordered_boundary())return failure(cpu,"oracle copy completion");const u8* actual=read_memory(cpu,FINAL_TEXTURE,WII_MM9_TEXTURE_BYTES);if(!actual)return failure(cpu,"oracle target memory");
    u32 changed=0,maximum=0;
    for(u32 n=0;n<WII_MM9_TEXTURE_BYTES;n+=2){const u32 a=(u32)actual[n]<<8|actual[n+1],b=(u32)prepared->texture[n]<<8|prepared->texture[n+1];if(a!=b)++changed;
        const u32 av[3]={(a>>11)&31,(a>>5)&63,a&31},bv[3]={(b>>11)&31,(b>>5)&63,b&31};for(u32 ch=0;ch<3;ch++){u32 aa=av[ch],bb=bv[ch];if(ch==1){aa=(aa<<2)|(aa>>4);bb=(bb<<2)|(bb>>4);}else{aa=(aa<<3)|(aa>>2);bb=(bb<<3)|(bb>>2);}const u32 delta=aa>bb?aa-bb:bb-aa;if(delta>maximum)maximum=delta;}}
    ++oracle_checked;oracle_different+=changed;if(maximum>oracle_max_delta)oracle_max_delta=maximum;
    if(oracle_checked<=4 || maximum>5)fprintf(stderr,"Native MM9 oracle frame=%llu cycles=%llu different_pixels=%u max_channel_delta=%u\n",(unsigned long long)oracle_checked,(unsigned long long)cpu->cycles,changed,maximum);
    if(oracle_checked==1 || maximum>5){dump("oracle-entry-mem1.bin",oracle_entry,cpu->ram_size);dump("oracle-native.rgb565",prepared->texture,WII_MM9_TEXTURE_BYTES);dump("oracle-original.rgb565",actual,WII_MM9_TEXTURE_BYTES);}
    if(maximum>5)return failure(cpu,"same-entry compositor image mismatch");return 0;
}
bool wii_mm9_renderer_hook_start(WiiGx* gx,const GcWiiNativeRenderApi* render,const GcWiiNativeFrameApi* frame) {
    const char* fit=getenv("WIILLR_MM9_FIT"),*fixed=getenv("WIILLR_MM9_VIEW_WIDTH");
    view_enabled=(fit && !strcmp(fit,"1")) || fixed;view_fixed_width=0;view_ready=false;
    if(fit && strcmp(fit,"0") && strcmp(fit,"1"))return false;
    if(fixed) {
        char* end;errno=0;unsigned long value=strtoul(fixed,&end,10);
        if(errno || !*fixed || *end || value<256 || value>WII_MM9_VIEW_MAX_WIDTH || (value&1))return false;
        view_fixed_width=(u32)value;
    }
    if(view_enabled && (!gx || !gx->presentation || WIILLR_MM9_RENDERER_MODE<2 || !WIILLR_MM9_FRAME_MODE)) {
        fprintf(stderr,"MM9 host view requires its native frame and presentation providers\n");return false;
    }
    if(WIILLR_MM9_RENDERER_MODE==0)return true;
    if(owner || !gx || !render || !frame || frame->abi!=GC_WII_NATIVE_FRAME_ABI)return false;
    const WiiMm9Memory memory={gx->cpu,read_memory};screen_renderer=wii_mm9_screen_create(memory,render);prepared=calloc(1,sizeof *prepared);
    overlay_renderer=wii_mm9_overlay_create(memory,overlay_write_byte,render);
    if(!screen_renderer || !overlay_renderer || !prepared){wii_mm9_screen_destroy(screen_renderer);screen_renderer=NULL;wii_mm9_overlay_destroy(overlay_renderer);overlay_renderer=NULL;free(prepared);prepared=NULL;return false;}
    owner=gx;frame_api=frame;output_api=NULL;verified=oracle_pending=false;frames=uncovered=failed=oracle_checked=oracle_different=0;oracle_max_delta=0;
    margin_sprite_count=0;sprite_attested=sprite_pending=false;
    sprite_calls=sprite_records=sprite_margins=sprite_oracle_checked=0;
    const char* sprite_check=getenv("WIILLR_MM9_SPRITES_ORACLE");
    sprite_oracle=sprite_check && !strcmp(sprite_check,"1");
#if WIILLR_MM9_FRAME_MODE
    owned_reset();
#endif
    oracle_directory=getenv("WIILLR_MM9_ORACLE_DIRECTORY");if(WIILLR_MM9_RENDERER_MODE==1 && oracle_directory)oracle_entry=malloc(gx->cpu->ram_size);return true;
}
void wii_mm9_renderer_hook_invalidate(u32 address,u32 size) {
#if WIILLR_MM9_FRAME_MODE
    owned_invalidate(address,size);
#endif
    const u32 a=address&0x1fffffffu;if(((u64)a+size>(COMPOSITOR&0x1fffffffu) && a<(COMPOSITOR&0x1fffffffu)+0x108) || ((u64)a+size>(SCREEN&0x1fffffffu) && a<(SCREEN&0x1fffffffu)+0x21c))verified=false;
    if(a<0xe88e4 && (u64)a+size>0xe8700)sprite_attested=false;
}
int wii_mm9_renderer_hook_call(CPUState* cpu,u32 address) {
    if(WIILLR_MM9_RENDERER_MODE==0)return 0;
    if(!owner || owner->cpu!=cpu)return 0;const u32 physical=address&0x1fffffffu;
    const int sprite=sprite_call(cpu,physical);if(sprite)return sprite;
#if WIILLR_MM9_FRAME_MODE
    const int frame=owned_call(cpu,address);if(frame)return frame;
#endif
    if(WIILLR_MM9_RENDERER_MODE==1 && oracle_pending && physical==(AFTER_COMPOSITOR&0x1fffffffu))return compare_oracle(cpu);
    if(physical!=(COMPOSITOR&0x1fffffffu) && physical!=(SCREEN&0x1fffffffu))return 0;
    if(!verified){verified=attest(cpu);if(!verified)return unsupported(cpu,"original function identity");}
    if(cpu->cycles>=cpu->cycle_deadline)return 1;
    const WiiMm9Memory memory={cpu,read_memory};char error[192];
    if(physical==(SCREEN&0x1fffffffu)) {
        if(WIILLR_MM9_RENDERER_MODE==1)return 0;GcWiiNativeDraw view;WiiMm9Screen screen;
        if(!camera(cpu,&view))return unsupported(cpu,"screen view");
        if(!wii_mm9_screen_prepare(memory,cpu->gpr[3],cpu->gpr[4],&view,&screen,error,sizeof error))return unsupported(cpu,error);
        if(!ordered_boundary())return failure(cpu,"screen prior work");if(wii_mm9_screen_draw(screen_renderer,cpu->gpr[3],cpu->gpr[4],&view)!=1)return failure(cpu,"screen submission");
        /* GXLoadTexObj's four slot tags are persistent object metadata. Keep
         * those caller-visible bytes without constructing or submitting any
         * register command. Logical texture fields stay unchanged. */
        const u8 tags[4]={0x80,0x84,0x88,0x94};for(u32 n=0;n<4;n++)mem_write8(cpu,cpu->gpr[3]+n*4,tags[n]);
        cpu->pc=cpu->lr;++cpu->cycles;return 1;
    }
    if(cpu->lr!=0x8009dd4c)return unsupported(cpu,"compositor caller");
    gcn_cost_owner_boundary(GCN_COST_OWNER_GRAPHICS);
    const bool view_ok=prepare_view(cpu);
    gcn_cost_owner_boundary(GCN_COST_OWNER_EXECUTION);
    if(!view_ok)return unsupported(cpu,"host view preparation");
    if(WIILLR_MM9_RENDERER_MODE==1){if(oracle_entry)memcpy(oracle_entry,cpu->ram,cpu->ram_size);oracle_pending=true;return 0;}
    GcWiiNativeClear clear;GcWiiNativeDisplayCopy copy;if(!frame_effects(cpu,&clear,&copy))return unsupported(cpu,"frame copy settings");
    if(!ordered_boundary())return failure(cpu,"frame prior completion");
    if(!publish(cpu,FINAL_TEXTURE,prepared->texture,WII_MM9_TEXTURE_BYTES) || !clear_retired_atlases(cpu,&clear) || !frame_api->clear(&clear))return failure(cpu,"frame publication/clear");
#if WIILLR_MM9_FRAME_MODE
    if(owned_active()) {
        const int retire=owned_finish_compositor(cpu,&copy);
        if(retire<0)return failure(cpu,"native copy-clear retirement");if(!retire)return owned_stop(cpu,"intermediate copy consumer");
    } else
#endif
    if(!frame_api->display_copy(&copy) || !frame_api->complete(0,0))return failure(cpu,"frame copy/completion");
    if(!retire_logical_inputs(cpu))return failure(cpu,"logical input retirement");
#if WIILLR_MM9_FRAME_MODE
    if(!owned_active())
#endif
    {const u32 sdk=r32(cpu,0x804ef8e0);mem_write32(cpu,sdk+0x240,0x49000000);mem_write32(cpu,sdk+0x244,0x4a03bcff);mem_write32(cpu,sdk+0x248,0x4d000040);mem_write32(cpu,sdk+0x24c,0x52010843);mem_write8(cpu,sdk+0x250,0);mem_write32(cpu,sdk+0x23c,r32(cpu,sdk+0x23c)|0x800);mem_write16(cpu,sdk+2,0);}
    /* Preserve the actual mutex-protected final-screen queue operation rather
     * than recreating its internal list/OS protocol. It returns to this void
     * compositor's caller through the original LR. */
    cpu->gpr[3]=2;cpu->gpr[4]=0x8039b240;cpu->gpr[5]=3;cpu->pc=0x800d5c38;++cpu->cycles;++frames;return 1;
}
int wii_mm9_renderer_hook_dispatch(CPUState* cpu,u32 address) {
    /* Screen calls are intercepted at their verified generated label, which
     * also covers internal same-page BLs. Avoid counting a declined call at
     * both the external dispatcher and that label. */
    const u32 p=address&0x1fffffffu;
#if WIILLR_MM9_FRAME_MODE
    if(p==0x9905c || p==0x99200 || p==0x99394 || p==0x995f0 || p==0xd5990 || p==0xe8024 || p==0x83730 ||
       p==0xe8700 || p==0xe88d8 || p==0xa7764 || p==0xa77c4)return 0;
#endif
    return p==(SCREEN&0x1fffffffu)?0:wii_mm9_renderer_hook_call(cpu,address);
}
void wii_mm9_renderer_hook_stop(void) {
    if(!owner)return;WiiMm9ScreenStatus status={0};wii_mm9_screen_status(screen_renderer,&status);
    printf("{\"native_mm9_sprites\":1,\"calls\":%llu,\"guest_records\":%llu,\"margin_records\":%llu,\"oracle_checked\":%llu}\n",
        (unsigned long long)sprite_calls,(unsigned long long)sprite_records,(unsigned long long)sprite_margins,(unsigned long long)sprite_oracle_checked);
    WiiMm9HbmStatus hbm={0};wii_mm9_hbm_status(hbm_renderer,&hbm);
    printf("{\"native_mm9_hbm\":1,\"scenes\":%llu,\"rejected\":%llu}\n",(unsigned long long)hbm.scenes,(unsigned long long)hbm.rejected);
    wii_mm9_hbm_destroy(hbm_renderer);hbm_renderer=NULL;
    WiiMm9OverlayStatus overlay={0};wii_mm9_overlay_status(overlay_renderer,&overlay);
    printf("{\"native_mm9_overlay\":1,\"entries\":%llu,\"nodes\":%llu,\"glyphs\":%llu,\"draws\":%llu,\"mesh_uploads\":%llu,\"rejected\":%llu}\n",(unsigned long long)overlay.entries,(unsigned long long)overlay.nodes,(unsigned long long)overlay.glyphs,(unsigned long long)overlay.draws,(unsigned long long)overlay.mesh_uploads,(unsigned long long)overlay.rejected);
#if WIILLR_MM9_FRAME_MODE
    owned_summary();
#endif
    printf("{\"native_mm9_renderer\":%u,\"compositor_frames\":%llu,\"screen_draws\":%llu,\"screen_mesh_uploads\":%llu,\"uncovered\":%llu,\"failed\":%llu,\"oracle_frames\":%llu,\"oracle_different_pixels\":%llu,\"oracle_max_channel_delta\":%u}\n",WIILLR_MM9_RENDERER_MODE,(unsigned long long)frames,(unsigned long long)status.draws,(unsigned long long)status.mesh_uploads,(unsigned long long)uncovered,(unsigned long long)failed,(unsigned long long)oracle_checked,(unsigned long long)oracle_different,oracle_max_delta);
    wii_mm9_screen_destroy(screen_renderer);screen_renderer=NULL;wii_mm9_overlay_destroy(overlay_renderer);overlay_renderer=NULL;free(prepared);prepared=NULL;free(oracle_entry);oracle_entry=NULL;
    free(view_rgba);view_rgba=NULL;view_capacity=view_width=0;view_enabled=view_ready=false;
    owner=NULL;frame_api=NULL;output_api=NULL;
}

