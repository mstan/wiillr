/* SPDX-License-Identifier: MIT */
/* Optional WR9E gameplay controls. All extra guest subroutines execute through
 * the existing content-verified native catalog, never nested emulation or guest
 * code patches. Session mode, acquired weapons, lives and native progress stay
 * original. Host-side health/ammo banks last for this running session only. */
#include "wiillr/mm9_mods.h"
#include "wiillr/mm9_dlc.h"
#include "memory/memory.h"
#include "cpu/native_code.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum {
    FRAME=0x80089e68u, FILES=0x800aa6d8u, SPAWN=0x800b709cu,
    SAVE=0x8000d664u, SAVE_BUFFER=0x80306440u, SAVE_SIZE=0x134u,
    CACHE=0x800b730cu, REFILL=0x800b5ac0u,
    NEW_MEGA=0x8008938cu, NEW_PROTO=0x80089604u,
    CLEANUP=0x800ba51cu, STAND=0x800b56a4u, WEAPON=0x800b5f90u,
    TELEPORT_FACTORY=0x80041028u, FX_INIT=0x80041120u,
    FX_TICK=0x800411b4u, FX_DEATH=0x800411ccu,
    FX_IN=0x80041210u, FX_OUT=0x800413d4u,
    FX_POOL_SELECT=0x800e8bd0u, FX_FREE=0x800e8c9cu,
    FX_POOL=0x80314230u, FX_SENTINEL=FX_POOL+0x14u,
    SOUND=0x800d5234u, STAGE=0x803140c0u, PLAYER=0x80313e98u,
    SAVED=STAGE+0x21e0u, HELD=0x8031a5f8u, PRESSED=0x8031a600u,
    REPEAT=0x8031a604u, MINUS=0x80u, WEAPONS=11, HOLD_FIELDS=120,
    EXCHANGE_FIELDS=30
};
typedef struct {u16 hp,display,ammo[WEAPONS];u8 weapon,character;bool valid;} Pool;
typedef struct {
    u32 gpr[32],lr,ctr,cr,xer,fpscr,gqr[8];f64 fpr[32],ps1[32];
} Registers;
typedef struct {u32 pointer,head[4];} ResidentFile;
/* The original character descriptors at 8027e40c/8027e45c share eleven IDs.
 * These are their four differing entries, including the ability/projectile
 * banks. Reuse their original read-only ID lists, never a fabricated asset. */
static const struct {u16 id;u32 list;} character_files[]={
    {0x140,0x8027e3f0u},{0x180,0x8027e440u},
    {0x109,0x8027e404u},{0x181,0x8027e454u}
};
typedef enum {IDLE,LOADING,CHANGING,CYCLING,START_OUT,STEP_FX,RETIRE_FX} Flight;
typedef enum {TELEPORT_NONE,TELEPORT_OUT,TELEPORT_GAP,TELEPORT_IN} Teleport;
static WiiGx* owner;
static bool character_mod,weapon_mod;
static struct {
    bool attested,rejected,minus_down,long_sent,pending_character,reset_pending;
    u32 skip_pc,resume,player,field,last_field,hold_fields,step,target_weapon;
    unsigned load_index,load_type;u32 loader_return,resource_error;
    ResidentFile resident[4][2];
    unsigned previous_actions;int pending_weapon;
    u8 target_character;
    bool freeze_ready;u8 visibility;
    u32 effect,teleport_fields,retire_steps;
    Teleport teleport;
    Flight flight;Registers saved;Pool pools[2];
    u64 switches,cycles,preloads,resets,long_presses,resource_failures;
    u64 frozen_frames,effect_steps;
} state;
static _Atomic unsigned host_held,test_held,host_field;

static bool ram(CPUState* c,u32 a,u32 n) {
    if(a<0x80000000u || a>=0x81800000u || n>0x81800000u-a)return false;
    u32 size=0;return gcn_mem_resolve(c,a,&size)!=NULL && size>=n;
}
static u8 r8(CPUState* c,u32 a){return mem_read8(c,a,c->pc);}
static u16 r16(CPUState* c,u32 a){return mem_read16(c,a,c->pc);}
static u32 r32(CPUState* c,u32 a){return mem_read32(c,a,c->pc);}
static void w8(CPUState* c,u32 a,u8 v){mem_write8(c,a,v,c->pc);}
static void w16(CPUState* c,u32 a,u16 v){mem_write16(c,a,v,c->pc);}
static void w32(CPUState* c,u32 a,u32 v){mem_write32(c,a,v,c->pc);}
/* Original 64-byte function heads from the qualified USA image. */
static const u32 code_heads[][2]={
    {FRAME,0xe5840b5bu},{FILES,0x861cae27u},{SPAWN,0xd4e80e98u},
    {CACHE,0xbe825ba5u},{REFILL,0xfe346dd5u},{NEW_MEGA,0xc8946023u},
    {NEW_PROTO,0xd9e2767fu},{CLEANUP,0x52d86237u},{STAND,0xdd2eb4bfu},
    {WEAPON,0x82515faau},{SOUND,0x0433e1e5u},{SAVE,0x2b1a7a4eu},
    {TELEPORT_FACTORY,0xa375bfcau},{FX_INIT,0x8e86f771u},
    {FX_TICK,0xa7e461f4u},{FX_DEATH,0x612ccffcu},
    {FX_IN,0xf55d10e5u},{FX_OUT,0xc1233962u},
    {FX_POOL_SELECT,0xdcd27399u},{FX_FREE,0x9b30aaf0u}
};
bool wii_mm9_mods_stage_frozen(CPUState* c) {
    if(!owner || owner->cpu!=c || owner->title!=0x0001000157523945ull)return false;
    state.freeze_ready=true;
    if(!character_mod || state.teleport==TELEPORT_NONE)return false;
    ++state.frozen_frames;return true;
}
void wii_mm9_mods_invalidate(u32 address,u32 size) {
    const u32 a=address&0x1fffffffu;const u64 end=(u64)a+size;
    for(unsigned k=0;k<sizeof code_heads/sizeof code_heads[0];++k) {
        const u32 head=code_heads[k][0]&0x1fffffffu;
        if(a<(u64)head+64 && end>head) {
            state.attested=state.rejected=false;return;
        }
    }
}
static bool identity(CPUState* c) {
    if(state.attested || state.rejected)return state.attested;
    for(unsigned k=0;k<sizeof code_heads/sizeof code_heads[0];k++) {
        const u32 a=code_heads[k][0];if(!ram(c,a,64))return false;
        gcn_native_code_watch(a,64);
        u32 hash=2166136261u;
        for(unsigned n=0;n<64;n++)hash=(hash^r8(c,a+n))*16777619u;
        if(hash!=code_heads[k][1]) {
            state.rejected=true;
            fprintf(stderr,"MM9 optional mods: unsupported original code at %08x\n",a);
            return false;
        }
    }
    state.attested=true;return true;
}
static void save_registers(CPUState* c) {
    Registers* s=&state.saved;memcpy(s->gpr,c->gpr,sizeof s->gpr);
    memcpy(s->fpr,c->fpr,sizeof s->fpr);memcpy(s->ps1,c->ps1,sizeof s->ps1);
    memcpy(s->gqr,c->gqr,sizeof s->gqr);
    s->lr=c->lr;s->ctr=c->ctr;s->cr=c->cr;s->xer=c->xer;s->fpscr=c->fpscr;
}
static bool restore_registers(CPUState* c) {
    const Registers* s=&state.saved;memcpy(c->gpr,s->gpr,sizeof s->gpr);
    memcpy(c->fpr,s->fpr,sizeof s->fpr);memcpy(c->ps1,s->ps1,sizeof s->ps1);
    memcpy(c->gqr,s->gqr,sizeof s->gqr);
    c->lr=s->lr;c->ctr=s->ctr;c->cr=s->cr;c->xer=s->xer;c->fpscr=s->fpscr;
    c->pc=state.resume;state.skip_pc=state.resume;state.flight=IDLE;++c->cycles;
    return true;
}
static bool call(CPUState* c,u32 entry,u32 arg3,u32 arg4) {
    c->gpr[3]=arg3;c->gpr[4]=arg4;c->lr=FRAME;c->pc=entry;++c->cycles;
    return true;
}
static void restore_visibility(CPUState* c) {
    if(ram(c,STAGE,0x22a0) && r32(c,STAGE+0x2290)==state.player &&
       ram(c,state.player,0x228) && r16(c,state.player+0xb6) &&
       !(r32(c,state.player+0xa0)&0x200))
        w8(c,state.player+0xa,(u8)((r8(c,state.player+0xa)&~1u)|state.visibility));
}
static void cancel_teleport(CPUState* c) {
    if(state.teleport!=TELEPORT_NONE)restore_visibility(c);
    state.teleport=TELEPORT_NONE;state.effect=state.teleport_fields=0;
    state.pending_character=false;state.pending_weapon=0;
}
static bool live_effect(CPUState* c) {
    if(!state.effect || !ram(c,FX_SENTINEL,8))return false;
    u32 node=r32(c,FX_SENTINEL);
    for(unsigned n=0;n<64 && node!=FX_SENTINEL;n++) {
        if(!ram(c,node,0x98))return false;
        if(node==state.effect)return r8(c,node+8)==8 && r8(c,node+9)==2;
        node=r32(c,node);
    }
    return false;
}
static bool call_teleport(CPUState* c,bool outgoing) {
    call(c,TELEPORT_FACTORY,outgoing?1u:0u,0);
    c->gpr[5]=state.player+0x60;
    c->gpr[6]=(r8(c,state.player+0xa)>>4)&1u;return true;
}
static u32 file_pointer(CPUState* c,unsigned n,unsigned type) {
    return r32(c,0x80383e70u+(type==1?4u:0x804u)+character_files[n].id*4u);
}
static bool file_valid(CPUState* c,unsigned n,unsigned type) {
    const u32 p=file_pointer(c,n,type);
    const u32 frontier=r32(c,0x80383e70u);
    if((p&3) || !ram(c,p,16) || !ram(c,frontier,1) || p>=frontier)return false;
    /* These are original f/o offset tables. The object loader dereferences
     * o+word[0]+4 and o+word[1]+4; checking a resource ID alone is insufficient. */
    const unsigned offsets=type==1?1:2;
    for(unsigned k=0;k<offsets;k++) {
        const u32 offset=r32(c,p+k*4);
        if((offset&3) || offset<8 || offset>0x10000 || !ram(c,p,offset+8) ||
           offset+8>frontier-p)return false;
    }
    return true;
}
static void capture(CPUState*,u32,Pool*);
static bool resource_failure(CPUState* c,u32 reason) {
    state.resource_error=reason;++state.resource_failures;
    cancel_teleport(c);
    fprintf(stderr,"MM9 optional mods: character resource validation failed %08x (loader %08x)\n",reason,state.loader_return);
    return restore_registers(c);
}
static void snapshot_files(CPUState* c) {
    memset(state.resident,0,sizeof state.resident);
    for(unsigned n=0;n<4;n++)for(unsigned t=1;t<=2;t++)if(file_valid(c,n,t)) {
        ResidentFile* saved=&state.resident[n][t-1];saved->pointer=file_pointer(c,n,t);
        for(unsigned k=0;k<4;k++)saved->head[k]=r32(c,saved->pointer+k*4);
    }
}
static bool load_next_file(CPUState* c) {
    for(;state.load_index<4;state.load_index++) {
        const unsigned n=state.load_index;
        if(r16(c,character_files[n].list)!=character_files[n].id)
            return resource_failure(c,0x10000u|character_files[n].id);
        for(unsigned t=1;t<=2;t++)if(!file_valid(c,n,t)) {
            state.load_type=t;
            /* Actual original signature: aa6d8(type1=f/type2=o,u16* IDs,count).
             * aa7d8/aa800 publish f/o pointers separately from GPU texture slots.
             * Calling a7c78 is insufficient: its GPU-slot guard can skip these. */
            call(c,FILES,t,character_files[n].list);c->gpr[5]=1;return true;
        }
    }
    for(unsigned n=0;n<4;n++)for(unsigned t=1;t<=2;t++) {
        const ResidentFile* saved=&state.resident[n][t-1];
        if(!file_valid(c,n,t))return resource_failure(c,0x20000u|character_files[n].id);
        if(saved->pointer) {
            if(file_pointer(c,n,t)!=saved->pointer)return resource_failure(c,0x30000u|character_files[n].id);
            for(unsigned k=0;k<4;k++)if(r32(c,saved->pointer+k*4)!=saved->head[k])
                return resource_failure(c,0x40000u|character_files[n].id);
        }
    }
    ++state.preloads;state.resource_error=0;state.flight=START_OUT;state.step=0;
    const unsigned outgoing=r8(c,state.player+9)==2?1u:0u;
    capture(c,state.player,&state.pools[outgoing]);
    state.visibility=r8(c,state.player+0xa)&1u;
    return call(c,CLEANUP,state.player,0);
}
static void capture(CPUState* c,u32 p,Pool* pool) {
    pool->hp=r16(c,p+0xb6);pool->display=r16(c,p+0xb8);
    for(unsigned n=0;n<WEAPONS;n++)pool->ammo[n]=r16(c,p+0x14eu+n*2);
    pool->weapon=r8(c,p+0x10c);pool->character=r8(c,p+9);pool->valid=true;
}
static void reset_pools(CPUState* c,u32 p) {
    Pool initial;memset(&initial,0,sizeof initial);capture(c,p,&initial);
    state.pools[0]=state.pools[1]=initial;
    state.pools[0].character=initial.character==1?1:0;
    state.pools[1].character=2;
    /* Both start from the real original respawn values, including fractional
     * ammo. No inventory or acquired-weapon bits are cloned or granted. */
    state.reset_pending=false;state.pending_character=false;state.pending_weapon=0;
    ++state.resets;
}
static unsigned pool_index(u8 character){return character==2?1u:0u;}
static u32 acquired(CPUState* c,u32 p){return (r32(c,p+0x164)&0x7ffu)|1u;}
static unsigned next_weapon(u32 mask,unsigned current,int direction) {
    if(current>=WEAPONS)current=0;
    for(unsigned n=0;n<WEAPONS;n++) {
        current=(current+WEAPONS+(direction<0?-1:1))%WEAPONS;
        if(mask&(1u<<current))return current;
    }
    return 0;
}
static bool gameplay(CPUState* c,u32* player) {
    if(!ram(c,STAGE,0x22a0) || r32(c,STAGE)!=0x000a0000u)return false;
    const u32 p=r32(c,STAGE+0x2290);
    if(p!=PLAYER || !ram(c,p,0x228) || r8(c,p+9)>2 ||
       (s16)r16(c,p+0xb6)<=0 || (r32(c,p+0xa0)&0x200))return false;
    /* The original pause/records branch's existing modal/refill guards. */
    const u32 system=0x80304b90u;
    if(!ram(c,system,0x2b4) || (r8(c,system+0x125)&9) ||
       (r8(c,system+0x126)&3) || r32(c,system+0x2b0) ||
       r32(c,STAGE+0x2148) || r32(c,0x804ee978u) || r32(c,0x804ee97cu))return false;
    *player=p;return true;
}
static bool apply_character(CPUState* c) {
    const u32 p=state.player;
    const unsigned old=pool_index(r8(c,p+9)),next=old^1u;
    const Pool* pool=&state.pools[next];
    if(!pool->valid || pool->hp<1 || pool->hp>28)return false;
    state.target_character=pool->character;
    state.target_weapon=pool->weapon;
    if(state.target_weapon>=WEAPONS || !(acquired(c,p)&(1u<<state.target_weapon)))state.target_weapon=0;
    w8(c,p+9,pool->character);w8(c,p+0x14d,pool->character);
    /* This is the original checkpoint character cache, not session mode. */
    w8(c,SAVED+1,pool->character);
    w16(c,p+0xb6,pool->hp);w16(c,p+0xb8,pool->display);
    for(unsigned n=0;n<WEAPONS;n++)w16(c,p+0x14eu+n*2,pool->ammo[n]);
    return true;
}
static void consume_transition_input(CPUState* c) {
    state.pending_character=false;state.pending_weapon=0;state.hold_fields=0;
    state.minus_down=(r32(c,HELD)&MINUS)!=0;state.long_sent=state.minus_down;
    w32(c,HELD,0);w32(c,PRESSED,0);w16(c,REPEAT,0);
}
static bool begin_effect_step(CPUState* c,bool retiring) {
    state.flight=retiring?RETIRE_FX:STEP_FX;state.step=0;
    if(retiring)state.retire_steps=0;
    return call(c,FX_POOL_SELECT,FX_POOL,0x100);
}
static bool continuation(CPUState* c) {
    if(state.flight==LOADING) {
        state.loader_return=c->gpr[3];
        if(state.loader_return!=1 || !file_valid(c,state.load_index,state.load_type))
            return resource_failure(c,0x50000u|character_files[state.load_index].id);
        return load_next_file(c);
    }
    if(state.flight==START_OUT) {
        if(state.step++==0)return call_teleport(c,true);
        state.effect=c->gpr[3];
        if(!live_effect(c))return resource_failure(c,0x60001u);
        state.teleport=TELEPORT_OUT;state.teleport_fields=0;
        return begin_effect_step(c,false);
    }
    if(state.flight==CHANGING) {
        if(state.step==0) {
            /* Palette indices can match across characters; force the ORIGINAL
             * palette setter to publish the new character resource's palette. */
            w8(c,state.player+0x100,0xff);state.step=1;
            return call(c,WEAPON,state.player,state.target_weapon);
        }
        if(state.step==1){state.step=2;return call_teleport(c,false);}
        state.effect=c->gpr[3];
        if(!live_effect(c))return resource_failure(c,0x60002u);
        state.teleport=TELEPORT_IN;state.teleport_fields=0;
        return begin_effect_step(c,false);
    }
    if(state.flight==STEP_FX || state.flight==RETIRE_FX) {
        const bool retiring=state.flight==RETIRE_FX;
        if(state.step==0) {
            if(!live_effect(c))return resource_failure(c,0x60003u);
            const u32 callback=r32(c,state.effect+0x18);
            if((retiring && callback!=FX_DEATH && callback!=FX_FREE) ||
               (!retiring && callback!=FX_INIT && callback!=FX_TICK))
                return resource_failure(c,0x60004u);
            if(retiring && ++state.retire_steps>2)return resource_failure(c,0x60005u);
            /* Original group8 dispatch selects its pool and publishes this
             * timestep before invoking the actor's original callback. */
            w16(c,state.effect+0x32,0x100);state.step=1;
            return call(c,callback,state.effect,0x100);
        }
        if(retiring) {
            if(live_effect(c)) {
                state.step=0;return call(c,FX_POOL_SELECT,FX_POOL,0x100);
            }
            state.effect=0;state.teleport_fields=0;
            if(state.teleport==TELEPORT_OUT)state.teleport=TELEPORT_GAP;
            else {
                restore_visibility(c);state.teleport=TELEPORT_NONE;
                consume_transition_input(c);++state.switches;
            }
        } else {
            if(!live_effect(c))return resource_failure(c,0x60006u);
            w8(c,state.player+0xa,(u8)(r8(c,state.player+0xa)&~1u));
            ++state.effect_steps;
        }
        return restore_registers(c);
    }
    if(state.flight==CYCLING) {
        if(state.step++==0)return call(c,SOUND,3,0);
        ++state.cycles;return restore_registers(c);
    }
    return false;
}
static bool frame(CPUState* c) {
    const unsigned actions=(atomic_load(&host_held)|atomic_load(&test_held))&3u;
    const unsigned rising=actions&~state.previous_actions;state.previous_actions=actions;
    const u32 observed=atomic_load(&host_field);
    u32 elapsed=observed-state.last_field;state.last_field=observed;
    if(!elapsed)elapsed=1; /* also supports bounded CPU-only contract fixtures */
    state.field+=elapsed;
    u32 p=0;const bool playing=gameplay(c,&p);
    const bool characters=character_mod && wii_mm9_dlc_available();
    if(state.teleport!=TELEPORT_NONE) {
        if(!ram(c,state.player,0x228) || r32(c,STAGE)!=0x000a0000u ||
           r32(c,STAGE+0x2290)!=state.player || !r16(c,state.player+0xb6) ||
           (r32(c,state.player+0xa0)&0x200)) {
            cancel_teleport(c);return false;
        }
        consume_transition_input(c);
        if(!playing)return false; /* Native HOME/modal pauses the exchange. */
        ++state.teleport_fields;
        if(state.teleport==TELEPORT_GAP) {
            if(state.teleport_fields<EXCHANGE_FIELDS)return false;
            save_registers(c);state.resume=FRAME;state.flight=CHANGING;state.step=0;
            if(!apply_character(c))return resource_failure(c,0x60007u);
            return call(c,STAND,state.player,0);
        }
        save_registers(c);state.resume=FRAME;
        if(!live_effect(c) || state.teleport_fields>300)return resource_failure(c,0x60008u);
        if(r8(c,state.effect+0x10)>=2)return begin_effect_step(c,true);
        return begin_effect_step(c,false);
    }
    if(!playing) {
        state.pending_character=false;state.pending_weapon=0;
        const bool down=(r32(c,HELD)&MINUS)!=0;
        /* A modal/death boundary does not create a new physical hold. Keep a
         * delivered long press latched until release, and ignore holds begun
         * outside gameplay rather than turning their release into a switch. */
        if(!down) {state.minus_down=state.long_sent=false;state.hold_fields=0;}
        else if(!state.minus_down) {state.minus_down=true;state.long_sent=true;}
        return false;
    }
    if(state.reset_pending || !state.pools[0].valid || !state.pools[1].valid)reset_pools(c,p);
    if(characters) {
        const bool down=(r32(c,HELD)&MINUS)!=0;
        /* Preserve held/previous input so original edge detection stays correct;
         * only suppress the original immediate records-open edge/repeat. */
        w32(c,PRESSED,r32(c,PRESSED)&~MINUS);
        w16(c,REPEAT,(u16)(r16(c,REPEAT)&~MINUS));
        if(down) {
            if(!state.minus_down) {state.hold_fields=1;state.long_sent=false;}
            else if(state.hold_fields<HOLD_FIELDS) {
                const u32 left=HOLD_FIELDS-state.hold_fields;
                state.hold_fields+=elapsed<left?elapsed:left;
            }
            if(state.hold_fields>=HOLD_FIELDS && !state.long_sent) {
                w32(c,PRESSED,r32(c,PRESSED)|MINUS);
                state.long_sent=true;state.pending_character=false;++state.long_presses;
            }
        } else if(state.minus_down) {
            if(!state.long_sent)state.pending_character=true;
            state.hold_fields=0;state.long_sent=false;
        }
        state.minus_down=down;
    } else {state.minus_down=state.long_sent=false;state.hold_fields=0;state.pending_character=false;}
    if(actions==3u)state.pending_weapon=0;
    else if(weapon_mod) {
        if(rising&1u)state.pending_weapon=-1;
        if(rising&2u)state.pending_weapon=1;
    }
    if(state.long_sent)return false;
    const u8 movement=r8(c,p+0x11);
    if(characters && state.pending_character && movement==0 && state.freeze_ready) {
        state.pending_character=false;state.pending_weapon=0;
        save_registers(c);state.resume=FRAME;state.player=p;state.step=0;state.flight=LOADING;
        state.load_index=0;snapshot_files(c);
        /* Guaranteed stage-function entry, independent of which original
         * resource-list path initialized this stage. Keep already loaded banks
         * in place; original e9264 refreshes GPU alias0140 from the selected
         * f-bank and binds the actual o-bank during STAND below. */
        return load_next_file(c);
    }
    if(weapon_mod && state.pending_weapon && movement<=2) {
        const unsigned selected=r8(c,p+0x10c);
        const unsigned next=next_weapon(acquired(c,p),selected,state.pending_weapon);
        state.pending_weapon=0;if(next==selected)return false;
        save_registers(c);state.resume=FRAME;state.player=p;state.step=0;state.flight=CYCLING;
        return call(c,WEAPON,p,next);
    }
    return false;
}
void wii_mm9_mods_configure(bool characters,bool weapons) {
    character_mod=characters;weapon_mod=weapons;
}
void wii_mm9_mods_bind(WiiGx* gx) {
    owner=gx;memset(&state,0,sizeof state);state.reset_pending=true;
    atomic_store(&host_held,0);atomic_store(&test_held,0);atomic_store(&host_field,0);
}
static void latch_modal_actions(void) {
    /* Original menus do not call FRAME. Observe their held levels at the
     * existing field/input boundary so returning to gameplay cannot synthesize
     * a rising edge. Scripted QA still polls host0; its test level participates
     * in this same combined latch rather than being overwritten by host input. */
    if(!weapon_mod || !owner || !owner->cpu || owner->title!=0x0001000157523945ull ||
       !ram(owner->cpu,STAGE,0x22a0))return;
    const unsigned actions=(atomic_load(&host_held)|atomic_load(&test_held))&3u;
    u32 player;
    if(!gameplay(owner->cpu,&player)) {
        state.previous_actions=actions;state.pending_weapon=0;
    } else if(actions==3u)state.pending_weapon=0;
}
void wii_mm9_mods_host_actions(unsigned held) {
    atomic_store(&host_held,held&3u);atomic_fetch_add(&host_field,1u);
    latch_modal_actions();
}
void wii_mm9_mods_test_actions(unsigned held){atomic_store(&test_held,held&3u);latch_modal_actions();}
bool wii_mm9_mods_dispatch(CPUState* c,u32 pc) {
    if(!owner || owner->cpu!=c || owner->title!=0x0001000157523945ull ||
       (!character_mod && !weapon_mod))return false;
    if(pc==SAVE) {
        /* Original 8009003c..84 copies STAGE+215c (134 bytes) into
         * this independent snapshot before calling the native save API.
         * Its +85 byte is the respawn character cache, persisted at file+99.
         * Retain the original campaign's character in the file so that the
         * save can load with this optional mod disabled or DLC absent. Keep
         * the live cache/actor and original mode/progress byte-for-byte. */
        if(character_mod && state.flight==IDLE && c->gpr[3]<8 &&
           c->gpr[4]==SAVE_BUFFER && c->gpr[5]==SAVE_SIZE &&
           ram(c,SAVE_BUFFER,SAVE_SIZE) && identity(c)) {
            const u8 mode=r8(c,SAVE_BUFFER+0x120u);
            if(mode<=2 && mode==r8(c,STAGE+0x227cu)) {
                const u8 character=r8(c,SAVE_BUFFER+0x85u);
                if(mode==1)w8(c,SAVE_BUFFER+0x85u,2);
                else if(character==2) {
                    /* Original Mega may be helmetless (character1). A live
                     * Proto snapshot uses the retained Mega bank's choice;
                     * an already-native Mega snapshot remains unchanged. */
                    const Pool* mega=&state.pools[0];
                    w8(c,SAVE_BUFFER+0x85u,
                        mega->valid && mega->character<=1?mega->character:0);
                }
            }
        }
        return false; /* Original save API still executes exactly once. */
    }
    /* FRAME is an original indirect function-pointer entry (80089388 bctr),
     * not a same-page shadow-call return. Every injected subroutine returns
     * cross-page to this catalogued entry; no generated-code seam is required. */
    if(state.flight!=IDLE) {
        if(pc!=FRAME)return false;
        if(!identity(c)) {
            fprintf(stderr,"MM9 optional mods: code changed during native continuation\n");
            c->halted=true;return true;
        }
        return continuation(c);
    }
    if(state.skip_pc==pc) {state.skip_pc=0;return false;}
    if(pc!=FRAME && pc!=SPAWN && pc!=CACHE && pc!=REFILL &&
       pc!=NEW_MEGA && pc!=NEW_PROTO)return false;
    if(!identity(c))return false;
    if(pc==NEW_MEGA || pc==NEW_PROTO) {
        cancel_teleport(c);
        memset(state.pools,0,sizeof state.pools);state.reset_pending=true;
        state.pending_character=false;state.pending_weapon=0;state.minus_down=state.long_sent=false;
    } else if(pc==SPAWN) {
        if(c->gpr[4]==SAVED)state.reset_pending=true;
    } else if(pc==REFILL) {
        /* Original new-stage refill, distinct from an ordinary ammo pickup. */
        if(c->gpr[3]==SAVED)for(unsigned k=0;k<2;k++)
            for(unsigned n=0;n<WEAPONS;n++)state.pools[k].ammo[n]=0x1c00;
    } else if(pc==FRAME)return frame(c);
    return false;
}
int wii_mm9_mods_status(char* text,size_t capacity) {
    return snprintf(text,capacity,"{\"ok\":true,\"characters\":%s,\"weapons\":%s,\"attested\":%s,"
        "\"rejected\":%s,\"flight\":%u,\"hold_fields\":%u,\"pending_character\":%s,"
        "\"pending_weapon\":%d,\"switches\":%llu,\"weapon_changes\":%llu,"
        "\"preloads\":%llu,\"pool_resets\":%llu,\"long_presses\":%llu,"
        "\"resource_failures\":%llu,\"resource_error\":%u,\"loader_return\":%u,"
        "\"teleport\":%u,\"effect\":%u,\"freeze_ready\":%s,"
        "\"frozen_frames\":%llu,\"effect_steps\":%llu,"
        "\"mega_hp\":%u,\"proto_hp\":%u}",character_mod?"true":"false",weapon_mod?"true":"false",
        state.attested?"true":"false",state.rejected?"true":"false",(unsigned)state.flight,
        state.hold_fields,state.pending_character?"true":"false",state.pending_weapon,
        (unsigned long long)state.switches,(unsigned long long)state.cycles,
        (unsigned long long)state.preloads,(unsigned long long)state.resets,
        (unsigned long long)state.long_presses,(unsigned long long)state.resource_failures,
        state.resource_error,state.loader_return,(unsigned)state.teleport,state.effect,state.freeze_ready?"true":"false",
        (unsigned long long)state.frozen_frames,(unsigned long long)state.effect_steps,
        state.pools[0].hp,state.pools[1].hp);
}
