/* SPDX-License-Identifier: MIT */
/* CPU-only control contracts with synthetic RAM. Original guest routines are
 * deliberately represented by call/return spies: these tests establish routing,
 * input/state isolation and bank semantics, not original gameplay correctness.
 * Include the implementation so its private identity state can be admitted
 * without embedding copyrighted original function heads in a source fixture. */
#include "../src/mm9_mods.c"
#include <stdlib.h>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static bool owned=true;
static unsigned watch_calls;
static u32 watch_address,watch_size;
void gcn_native_code_watch(u32 address,u32 size) {
    ++watch_calls;watch_address=address;watch_size=size;
}
bool wii_mm9_dlc_available(void){return owned;}
u8* gcn_mem_resolve(CPUState* c,u32 a,u32* size) {
    const u32 p=a&0x1fffffffu;
    if(p>=c->ram_size){if(size)*size=0;return NULL;}
    if(size)*size=c->ram_size-p;
    return c->ram+p;
}
u8 mem_read8_cia(CPUState* c,u32 a,u32 cia){(void)cia;CHECK(ram(c,a,1));return c->ram[a&0x1fffffffu];}
u16 mem_read16_cia(CPUState* c,u32 a,u32 cia){return (u16)mem_read8_cia(c,a,cia)<<8|mem_read8_cia(c,a+1,cia);}
u32 mem_read32_cia(CPUState* c,u32 a,u32 cia){return (u32)mem_read16_cia(c,a,cia)<<16|mem_read16_cia(c,a+2,cia);}
void mem_write8_cia(CPUState* c,u32 a,u8 v,u32 cia){(void)cia;CHECK(ram(c,a,1));c->ram[a&0x1fffffffu]=v;}
void mem_write16_cia(CPUState* c,u32 a,u16 v,u32 cia){mem_write8_cia(c,a,(u8)(v>>8),cia);mem_write8_cia(c,a+1,(u8)v,cia);}
void mem_write32_cia(CPUState* c,u32 a,u32 v,u32 cia){mem_write16_cia(c,a,(u16)(v>>16),cia);mem_write16_cia(c,a+2,(u16)v,cia);}
static CPUState cpu;
static WiiGx gx;
static unsigned calls[512],called_count,loader_calls;
static bool fail_loader,fail_effect;
static unsigned selected_pool;
enum { TEST_FX=0x80560000u };
static void seed_file(unsigned n,unsigned type) {
    const u32 p=0x80600000u+n*0x1000u+(type-1)*0x100u;
    w32(&cpu,0x80383e70u+(type==1?4u:0x804u)+character_files[n].id*4u,p);
    w32(&cpu,p,type==1?12:8);w32(&cpu,p+4,32);
}

static void setup(bool characters,bool weapons) {
    memset(cpu.ram,0,cpu.ram_size);memset(cpu.gpr,0,sizeof cpu.gpr);
    cpu.halted=false;watch_calls=0;
    gx.cpu=&cpu;gx.title=0x0001000157523945ull;owned=true;
    wii_mm9_mods_configure(characters,weapons);wii_mm9_mods_bind(&gx);
    state.attested=true; /* synthetic instruction identity intentionally omitted */
    state.freeze_ready=true;
    w32(&cpu,STAGE,0x000a0000);w32(&cpu,STAGE+0x2290,PLAYER);
    w16(&cpu,PLAYER+0xb6,28);w16(&cpu,PLAYER+0xb8,28);
    w8(&cpu,PLAYER+0xa,1);
    w32(&cpu,FX_SENTINEL,FX_SENTINEL);w32(&cpu,FX_SENTINEL+4,FX_SENTINEL);
    w8(&cpu,PLAYER+0x14c,4);w8(&cpu,SAVED,4);
    w32(&cpu,PLAYER+0x164,1u|(1u<<3)|(1u<<7));
    for(unsigned n=0;n<WEAPONS;n++)w16(&cpu,PLAYER+0x14eu+n*2,0x1c00);
    for(unsigned n=0;n<4;n++)w16(&cpu,character_files[n].list,character_files[n].id);
    w32(&cpu,0x80383e70u,0x80610000u);
    /* Real normal-game setup has Mega/109 files; Proto/181 are absent. */
    for(unsigned t=1;t<=2;t++){seed_file(0,t);seed_file(2,t);}
    cpu.pc=FRAME;called_count=loader_calls=0;fail_loader=fail_effect=false;selected_pool=0;
}
static bool tick(unsigned actions,bool minus) {
    wii_mm9_mods_host_actions(actions);w32(&cpu,HELD,minus?MINUS:0);
    w32(&cpu,PRESSED,minus?MINUS:0);w16(&cpu,REPEAT,minus?MINUS:0);
    cpu.pc=FRAME;return wii_mm9_mods_dispatch(&cpu,FRAME);
}
static void finish_native_calls(void) {
    unsigned remaining=24;
    while(state.flight!=IDLE) {
        CHECK(remaining--);CHECK(called_count<sizeof calls/sizeof calls[0]);
        const u32 target=cpu.pc;calls[called_count++]=target;
        if(target==FILES) {
            CHECK(cpu.gpr[3]==1 || cpu.gpr[3]==2);CHECK(cpu.gpr[5]==1);
            CHECK(cpu.gpr[4]==character_files[state.load_index].list);
            if(!fail_loader)seed_file(state.load_index,cpu.gpr[3]);
            cpu.gpr[3]=fail_loader?0:1;++loader_calls;
        } else if(target==WEAPON) {
            CHECK(cpu.gpr[3]==PLAYER);CHECK(cpu.gpr[4]<WEAPONS);
            CHECK(acquired(&cpu,PLAYER)&(1u<<cpu.gpr[4]));
            w8(&cpu,PLAYER+0x10c,(u8)cpu.gpr[4]);w8(&cpu,PLAYER+0x112,(u8)cpu.gpr[4]);
            w8(&cpu,PLAYER+0x10f,0);
        } else if(target==STAND) {
            CHECK(cpu.gpr[3]==PLAYER);
            const unsigned n=r8(&cpu,PLAYER+9)==2?1:0;
            CHECK(file_valid(&cpu,n,1) && file_valid(&cpu,n,2));
            w16(&cpu,PLAYER+0x24,character_files[n].id);
            const u32 object=file_pointer(&cpu,n,2);
            w32(&cpu,PLAYER+0x48,object+r32(&cpu,object)+4);
        } else if(target==TELEPORT_FACTORY) {
            CHECK(cpu.gpr[3]<=1 && cpu.gpr[4]==0 && cpu.gpr[5]==PLAYER+0x60);
            CHECK(cpu.gpr[6]==((r8(&cpu,PLAYER+0xa)>>4)&1u));
            if(fail_effect)cpu.gpr[3]=0;
            else {
                const u32 first=r32(&cpu,FX_SENTINEL);
                w32(&cpu,TEST_FX,first);w32(&cpu,TEST_FX+4,FX_SENTINEL);
                w32(&cpu,first+4,TEST_FX);w32(&cpu,FX_SENTINEL,TEST_FX);
                w8(&cpu,TEST_FX+8,8);w8(&cpu,TEST_FX+9,2);w8(&cpu,TEST_FX+0x10,0);
                w32(&cpu,TEST_FX+0x18,FX_INIT);cpu.gpr[3]=TEST_FX;
            }
        } else if(target==FX_POOL_SELECT) {
            CHECK(cpu.gpr[3]==FX_POOL);selected_pool=FX_POOL;
        } else if(target==FX_INIT || target==FX_TICK || target==FX_DEATH || target==FX_FREE) {
            CHECK(cpu.gpr[3]==TEST_FX && selected_pool==FX_POOL);
            CHECK(r16(&cpu,TEST_FX+0x32)==0x100);
            if(target==FX_INIT){w8(&cpu,TEST_FX+0x10,1);w32(&cpu,TEST_FX+0x18,FX_TICK);}
            else if(target==FX_DEATH){w8(&cpu,TEST_FX+0x10,4);w32(&cpu,TEST_FX+0x18,FX_FREE);}
            else if(target==FX_FREE) {
                const u32 next=r32(&cpu,TEST_FX),previous=r32(&cpu,TEST_FX+4);
                w32(&cpu,previous,next);w32(&cpu,next+4,previous);
            }
            selected_pool=0; /* Every callback must select its pool again. */
        } else CHECK(target==CLEANUP || target==SOUND);
        const u32 ret=cpu.lr;
        /* Model arbitrary allowed guest ABI clobbers. Restoring only args or LR
         * would corrupt the original frame; all registers must survive. */
        cpu.gpr[0]^=0x55aa55aa;if(target!=FILES && target!=TELEPORT_FACTORY)cpu.gpr[3]^=0xa5a5a5a5;
        cpu.fpr[0]=99;cpu.ps1[0]=-3;cpu.cr^=0x12345678;cpu.ctr=0;
        cpu.xer^=1;cpu.fpscr^=3;cpu.gqr[0]^=9;
        cpu.pc=ret;CHECK(ret==FRAME);CHECK(wii_mm9_mods_dispatch(&cpu,ret));
    }
    CHECK(!wii_mm9_mods_dispatch(&cpu,cpu.pc)); /* original resume exactly once */
}
static void finish_calls(void) {
    finish_native_calls();
    for(unsigned n=0;state.teleport!=TELEPORT_NONE;n++) {
        CHECK(n<100);
        if(state.effect && live_effect(&cpu)) {
            /* Test-controlled native completion, not an animation model. */
            w8(&cpu,state.effect+0x10,2);w32(&cpu,state.effect+0x18,FX_DEATH);
        }
        if(tick(0,false))finish_native_calls();
    }
}
static void short_press(void){CHECK(!tick(0,true));CHECK(tick(0,false));finish_calls();}

int main(void) {
    memset(&cpu,0,sizeof cpu);cpu.ram_size=GC_MAIN_RAM_SIZE;cpu.ram=calloc(1,cpu.ram_size);CHECK(cpu.ram);
    setup(false,false);state.attested=false;
    CHECK(!tick(2,true));CHECK(r32(&cpu,PRESSED)==MINUS && !state.rejected);
    CHECK(watch_calls==0);
    setup(true,true);state.attested=false;
    CHECK(!tick(2,true));CHECK(state.rejected && r32(&cpu,PRESSED)==MINUS);
    CHECK(watch_calls==1 && watch_address==FRAME && watch_size==64);
    CHECK(!tick(2,true));CHECK(watch_calls==1); /* rejected identity is cached */
    wii_mm9_mods_invalidate(0x80600000u,4);CHECK(state.rejected);
    wii_mm9_mods_invalidate((FRAME&0x1fffffffu)|0xc0000000u,1);
    CHECK(!state.attested && !state.rejected);
    CHECK(!tick(2,true));CHECK(watch_calls==2 && state.rejected);
    for(unsigned n=0;n<sizeof code_heads/sizeof code_heads[0];++n) {
        state.attested=true;state.rejected=true;
        wii_mm9_mods_invalidate((code_heads[n][0]&0x1fffffffu)+63,1);
        CHECK(!state.attested && !state.rejected);
        state.attested=true;
        const u32 end=code_heads[n][0]+64;bool overlaps=false;
        for(unsigned k=0;k<sizeof code_heads/sizeof code_heads[0];++k)
            if(end>=code_heads[k][0] && end<code_heads[k][0]+64)overlaps=true;
        wii_mm9_mods_invalidate(end,1);CHECK(state.attested==!overlaps);
    }
    /* Invalidating an identity while a guest helper is in flight must stop
     * safely rather than resume FRAME with that helper's live registers. */
    setup(true,false);CHECK(!tick(0,true));CHECK(tick(0,false));
    CHECK(state.flight!=IDLE);const int flight=state.flight;
    wii_mm9_mods_invalidate(FRAME,64);cpu.pc=FRAME;cpu.gpr[3]=0x12345678;
    CHECK(wii_mm9_mods_dispatch(&cpu,FRAME));
    CHECK(cpu.halted && state.flight==flight && cpu.pc==FRAME && cpu.gpr[3]==0x12345678);
    CHECK(watch_calls==1 && state.rejected);
    setup(true,false);owned=false;
    CHECK(!tick(0,true));CHECK(r32(&cpu,PRESSED)==MINUS);
    CHECK(!tick(0,false));CHECK(r8(&cpu,PLAYER+9)==0);

    /* Departure must finish before the character changes. A native modal
     * pause must not consume the exchange delay, and arrival must finish
     * before input/visibility return. Native effect completion is controlled
     * explicitly here; actual animation/rendering is a separate game check. */
    setup(true,true);CHECK(!tick(0,false));
    w8(&cpu,PLAYER+0xa,0x11);w16(&cpu,PLAYER+0xb6,13);w16(&cpu,PLAYER+0xb8,13);
    w16(&cpu,PLAYER+0x154,0x0a5f);w32(&cpu,PLAYER+0x38,0x8056a000);
    w32(&cpu,PLAYER+0x104,0x12340000);CHECK(!wii_mm9_mods_stage_frozen(&cpu));
    CHECK(!tick(0,true));CHECK(tick(0,false));finish_native_calls();
    CHECK(state.teleport==TELEPORT_OUT && state.switches==0 && r8(&cpu,PLAYER+9)==0);
    CHECK(!(r8(&cpu,PLAYER+0xa)&1) && wii_mm9_mods_stage_frozen(&cpu));
    for(unsigned n=0;n<4;n++) {
        CHECK(tick(3,true));finish_native_calls();
        CHECK(state.teleport==TELEPORT_OUT && !state.switches && r8(&cpu,PLAYER+9)==0);
        CHECK(!r32(&cpu,HELD) && !r32(&cpu,PRESSED) && !state.pending_weapon);
    }
    w8(&cpu,TEST_FX+0x10,2);w32(&cpu,TEST_FX+0x18,FX_DEATH);
    CHECK(tick(0,false));finish_native_calls();
    CHECK(state.teleport==TELEPORT_GAP && !state.effect && r8(&cpu,PLAYER+9)==0);
    for(unsigned n=1;n<EXCHANGE_FIELDS;n++)CHECK(!tick(0,false));
    w32(&cpu,STAGE+0x2148,1);
    for(unsigned n=0;n<50;n++)CHECK(!tick(0,false));
    CHECK(state.teleport_fields==EXCHANGE_FIELDS-1 && r8(&cpu,PLAYER+9)==0);
    w32(&cpu,STAGE+0x2148,0);CHECK(tick(0,false));finish_native_calls();
    CHECK(state.teleport==TELEPORT_IN && r8(&cpu,PLAYER+9)==2 && !state.switches);
    CHECK(!(r8(&cpu,PLAYER+0xa)&1) && state.pools[0].hp==13 && state.pools[0].ammo[3]==0x0a5f);
    CHECK(r32(&cpu,PLAYER+0x38)==0x8056a000 && r32(&cpu,PLAYER+0x104)==0x12340000);
    CHECK(tick(0,false));finish_native_calls();CHECK(!state.switches);
    w8(&cpu,TEST_FX+0x10,2);w32(&cpu,TEST_FX+0x18,FX_DEATH);
    CHECK(tick(0,false));finish_native_calls();
    CHECK(state.teleport==TELEPORT_NONE && state.switches==1 && r8(&cpu,PLAYER+0xa)==0x11);
    CHECK(!wii_mm9_mods_stage_frozen(&cpu) && r32(&cpu,FX_SENTINEL)==FX_SENTINEL);
    CHECK(!state.long_presses && !state.cycles && !state.pending_weapon);
    setup(true,false);fail_effect=true;short_press();
    CHECK(!state.switches && state.resource_failures==1 && state.teleport==TELEPORT_NONE);
    CHECK(r8(&cpu,PLAYER+9)==0 && r8(&cpu,PLAYER+0xa)==1);
    setup(true,false);CHECK(!tick(0,true));CHECK(tick(0,false));finish_native_calls();
    w8(&cpu,TEST_FX+9,3);CHECK(tick(0,false));finish_native_calls();
    CHECK(state.teleport==TELEPORT_NONE && r8(&cpu,PLAYER+9)==0 && r8(&cpu,PLAYER+0xa)==1);
    CHECK(state.resource_failures==1); /* Reused/nonowned effects are never ticked/freed. */

    setup(true,true);CHECK(!tick(0,false));
    w16(&cpu,PLAYER+0xb6,13);w16(&cpu,PLAYER+0xb8,13);
    w16(&cpu,PLAYER+0x154,0x0a5f);w8(&cpu,PLAYER+0x10c,3);
    for(unsigned n=0;n<32;n++){cpu.gpr[n]=0x13570000u+n;cpu.fpr[n]=n+0.5;cpu.ps1[n]=-1.0-n;}
    cpu.lr=0x80089990;cpu.ctr=0x80089e68;cpu.cr=0x87654321;cpu.xer=8;cpu.fpscr=16;cpu.gqr[0]=23;
    Registers before;save_registers(&cpu);before=state.saved;
    short_press();
    CHECK(!memcmp(&before,&state.saved,sizeof before));
    CHECK(!memcmp(cpu.gpr,before.gpr,sizeof before.gpr));CHECK(cpu.lr==before.lr && cpu.ctr==before.ctr);
    CHECK(cpu.cr==before.cr && cpu.xer==before.xer && cpu.fpscr==before.fpscr);
    CHECK(cpu.fpr[0]==before.fpr[0] && cpu.ps1[0]==before.ps1[0] && cpu.gqr[0]==before.gqr[0]);
    CHECK(r8(&cpu,PLAYER+9)==2 && r8(&cpu,PLAYER+0x14d)==2 && r8(&cpu,SAVED+1)==2);
    CHECK(r16(&cpu,PLAYER+0x24)==0x180 && r16(&cpu,PLAYER+0xb6)==28);
    CHECK(r32(&cpu,PLAYER+0x48)>0x80000000u && state.preloads==1 && loader_calls==4);
    CHECK(r16(&cpu,PLAYER+0xb4)==0 && r8(&cpu,PLAYER+0x14c)==4 && r8(&cpu,SAVED)==4);
    CHECK(r8(&cpu,STAGE+0x227c)==0 && r32(&cpu,PLAYER+0x164)==(1u|8u|128u));
    w16(&cpu,PLAYER+0xb6,9);w16(&cpu,PLAYER+0xb8,9);w16(&cpu,PLAYER+0x154,0x0501);
    called_count=0;short_press();
    CHECK(r8(&cpu,PLAYER+9)==0 && r16(&cpu,PLAYER+0xb6)==13 && r16(&cpu,PLAYER+0xb8)==13);
    CHECK(r16(&cpu,PLAYER+0x154)==0x0a5f && r8(&cpu,PLAYER+0x10c)==3);
    called_count=0;short_press();CHECK(r16(&cpu,PLAYER+0xb6)==9 && r16(&cpu,PLAYER+0x154)==0x0501);

    setup(true,false);
    for(unsigned n=1;n<=HOLD_FIELDS;n++) {
        CHECK(!tick(0,true));CHECK((r32(&cpu,PRESSED)&MINUS)==(n==HOLD_FIELDS?MINUS:0));
    }
    CHECK(!tick(0,true));CHECK(!(r32(&cpu,PRESSED)&MINUS));CHECK(!tick(0,false));
    CHECK(state.long_presses==1 && state.switches==0 && r8(&cpu,PLAYER+9)==0);
    /* A leaderboard/modal excursion preserves the physical-hold latch. */
    for(unsigned n=0;n<HOLD_FIELDS;n++)CHECK(!tick(0,true));
    CHECK(state.long_presses==2);w32(&cpu,STAGE+0x2148,1);
    for(unsigned n=0;n<3;n++)CHECK(!tick(0,true));
    w32(&cpu,STAGE+0x2148,0);
    for(unsigned n=0;n<HOLD_FIELDS+2;n++)CHECK(!tick(0,true));
    CHECK(state.long_presses==2);CHECK(!tick(0,false));CHECK(!state.pending_character);
    /* Short release while airborne queues once; normal ground entry commits. */
    w8(&cpu,PLAYER+0x11,2);CHECK(!tick(0,true));CHECK(!tick(0,false));CHECK(state.pending_character);
    w8(&cpu,PLAYER+0x11,0);CHECK(tick(0,false));called_count=0;finish_calls();CHECK(state.switches==1);

    setup(false,true);CHECK(tick(2,false));finish_calls();CHECK(r8(&cpu,PLAYER+0x10c)==3);
    CHECK(!tick(2,false));CHECK(state.cycles==1);CHECK(!tick(0,false));
    called_count=0;CHECK(tick(2,false));finish_calls();CHECK(r8(&cpu,PLAYER+0x10c)==7);
    CHECK(!tick(0,false));called_count=0;CHECK(tick(2,false));finish_calls();CHECK(r8(&cpu,PLAYER+0x10c)==0);
    CHECK(!tick(0,false));called_count=0;CHECK(tick(1,false));finish_calls();CHECK(r8(&cpu,PLAYER+0x10c)==7);
    CHECK(!tick(0,false));CHECK(!tick(3,false));CHECK(r8(&cpu,PLAYER+0x10c)==7);
    w8(&cpu,PLAYER+0x11,4);CHECK(!tick(0,false));CHECK(!tick(2,false));CHECK(state.pending_weapon==1);
    CHECK(!tick(3,false));CHECK(!state.pending_weapon);w8(&cpu,PLAYER+0x11,0);
    CHECK(!tick(0,false));wii_mm9_mods_test_actions(2);called_count=0;CHECK(tick(0,false));finish_calls();
    CHECK(r8(&cpu,PLAYER+0x10c)==0);wii_mm9_mods_test_actions(0);

    /* Original Plus/menu: several input fields execute with NO FRAME callback.
     * A test-held R remains combined with host0 and must not fire on resume. */
    setup(false,true);CHECK(!tick(0,false));w8(&cpu,PLAYER+0x11,4);
    CHECK(!tick(2,false));CHECK(state.pending_weapon==1);
    w32(&cpu,STAGE,0x000b0000);wii_mm9_mods_test_actions(2);
    for(unsigned n=0;n<5;n++)wii_mm9_mods_host_actions(0);
    CHECK(!state.pending_weapon && state.previous_actions==2 && state.cycles==0);
    w8(&cpu,PLAYER+0x11,0);w32(&cpu,STAGE,0x000a0000);
    CHECK(!tick(0,false));CHECK(state.cycles==0 && r8(&cpu,PLAYER+0x10c)==0);
    wii_mm9_mods_test_actions(0);CHECK(!tick(0,false));
    wii_mm9_mods_test_actions(2);CHECK(tick(0,false));finish_calls();
    CHECK(state.cycles==1 && r8(&cpu,PLAYER+0x10c)==3);
    wii_mm9_mods_test_actions(0);CHECK(!tick(0,false));
    w32(&cpu,STAGE,0x000b0000);
    for(unsigned n=0;n<5;n++)wii_mm9_mods_host_actions(2);
    w32(&cpu,STAGE,0x000a0000);CHECK(!tick(2,false));CHECK(state.cycles==1);
    CHECK(!tick(0,false));called_count=0;CHECK(tick(2,false));finish_calls();CHECK(state.cycles==2);

    /* GPU slot140 already exists while f/o140 are NULL, matching the actual
     * invisible-sprite failure. Metadata loading must ignore GPU-slot state. */
    setup(true,false);w8(&cpu,PLAYER+9,2);w8(&cpu,PLAYER+0x14d,2);
    for(unsigned t=1;t<=2;t++){seed_file(1,t);seed_file(3,t);
        w32(&cpu,0x80383e70u+(t==1?4u:0x804u)+0x140u*4u,0);
        w32(&cpu,0x80383e70u+(t==1?4u:0x804u)+0x109u*4u,0);}
    w8(&cpu,0x8037e998u,7);w8(&cpu,0x8037e9d8u,0xff);
    const u32 proto_object=file_pointer(&cpu,1,2);
    short_press();CHECK(r8(&cpu,PLAYER+9)==0 && r32(&cpu,PLAYER+0x48)>0x80000000u);
    CHECK(loader_calls==4 && file_pointer(&cpu,1,2)==proto_object && state.preloads==1);
    CHECK(!tick(0,false));state.pools[0].hp=3;state.pools[1].hp=6;
    cpu.gpr[4]=SAVED;CHECK(!wii_mm9_mods_dispatch(&cpu,SPAWN));
    w16(&cpu,PLAYER+0xb6,28);w16(&cpu,PLAYER+0xb8,28);CHECK(!tick(0,false));
    CHECK(state.pools[0].hp==28 && state.pools[1].hp==28);
    /* Installed alternate modes preserve their original session byte. */
    w8(&cpu,STAGE+0x227c,3);called_count=0;short_press();CHECK(r8(&cpu,STAGE+0x227c)==3);
    /* A dead actor never switches/resurrects and receives the original input. */
    w16(&cpu,PLAYER+0xb6,0);CHECK(!tick(0,true));CHECK(r32(&cpu,PRESSED)==MINUS);
    CHECK(!tick(0,false));CHECK(r16(&cpu,PLAYER+0xb6)==0);
    setup(true,false);fail_loader=true;short_press();
    CHECK(state.resource_failures==1 && state.switches==0 && r8(&cpu,PLAYER+9)==0);
    CHECK(r16(&cpu,PLAYER+0xb6)==28 && file_valid(&cpu,0,1) && file_valid(&cpu,0,2));
    /* Original b5ac0 is li r0,1c00; native fractional fire consumes 0080
     * directly from these same halfwords. Refill all 11 pools, not 001c. */
    CHECK(!tick(0,false));state.pools[0].hp=13;state.pools[1].hp=9;
    for(unsigned k=0;k<2;k++)for(unsigned n=0;n<WEAPONS;n++)state.pools[k].ammo[n]=0x0a5f;
    cpu.gpr[3]=SAVED;CHECK(!wii_mm9_mods_dispatch(&cpu,REFILL));
    for(unsigned k=0;k<2;k++)for(unsigned n=0;n<WEAPONS;n++)CHECK(state.pools[k].ammo[n]==0x1c00);
    CHECK(state.pools[0].hp==13 && state.pools[1].hp==9);
    /* Native SAVE snapshots include the respawn cache. Only the independent
     * outgoing snapshot gets the campaign's canonical character: a normal
     * save made as swapped Proto remains usable with the optional mod off.
     * The running actor/cache, mode, ammo/progress and CPU call stay intact. */
    const struct {u8 mode,incoming,mega,valid,expected;} saves[]={
        {0,0,0,0,0},{0,1,0,0,1},{0,1,0,1,1},
        {0,2,1,1,1},{0,2,0,1,0},{0,2,1,0,0},{0,2,2,1,0},
        {1,0,1,1,2},{1,1,1,1,2},{1,2,1,1,2},
        {2,1,0,1,1},{2,2,1,1,1}
    };
    for(unsigned n=0;n<sizeof saves/sizeof saves[0];n++) {
        const u8 mode=saves[n].mode,incoming=saves[n].incoming;
        setup(true,false);w8(&cpu,STAGE+0x227c,(u8)mode);
        state.pools[0].valid=saves[n].valid;state.pools[0].character=saves[n].mega;
        w8(&cpu,SAVED+1,incoming);w8(&cpu,PLAYER+9,incoming);
        u8 expected[SAVE_SIZE];memset(expected,0xa5,sizeof expected);
        expected[0x120]=(u8)mode;expected[0x85]=incoming;
        memcpy(cpu.ram+(SAVE_BUFFER&0x1fffffffu),expected,sizeof expected);
        cpu.gpr[3]=7;cpu.gpr[4]=SAVE_BUFFER;cpu.gpr[5]=SAVE_SIZE;
        cpu.pc=SAVE;cpu.lr=0x80090088;Registers args;
        save_registers(&cpu);args=state.saved;
        CHECK(!wii_mm9_mods_dispatch(&cpu,SAVE));expected[0x85]=saves[n].expected;
        CHECK(!memcmp(cpu.ram+(SAVE_BUFFER&0x1fffffffu),expected,sizeof expected));
        CHECK(r8(&cpu,SAVED+1)==incoming && r8(&cpu,PLAYER+9)==incoming);
        CHECK(state.pools[0].valid==saves[n].valid && state.pools[0].character==saves[n].mega);
        CHECK(r8(&cpu,STAGE+0x227c)==mode && cpu.pc==SAVE && cpu.lr==args.lr);
        CHECK(!memcmp(cpu.gpr,args.gpr,sizeof args.gpr) && state.flight==IDLE);
    }
    setup(true,false);w8(&cpu,SAVE_BUFFER+0x85,2);w8(&cpu,SAVE_BUFFER+0x120,0);
    cpu.gpr[3]=8;cpu.gpr[4]=SAVE_BUFFER;cpu.gpr[5]=SAVE_SIZE;
    CHECK(!wii_mm9_mods_dispatch(&cpu,SAVE));CHECK(r8(&cpu,SAVE_BUFFER+0x85)==2);
    cpu.gpr[3]=0;cpu.gpr[5]=SAVE_SIZE-1;
    CHECK(!wii_mm9_mods_dispatch(&cpu,SAVE));CHECK(r8(&cpu,SAVE_BUFFER+0x85)==2);
    cpu.gpr[5]=SAVE_SIZE;cpu.gpr[4]=SAVE_BUFFER+4;
    CHECK(!wii_mm9_mods_dispatch(&cpu,SAVE));CHECK(r8(&cpu,SAVE_BUFFER+0x85)==2);
    cpu.gpr[4]=SAVE_BUFFER;w8(&cpu,SAVE_BUFFER+0x120,1);w8(&cpu,SAVE_BUFFER+0x85,1);
    CHECK(!wii_mm9_mods_dispatch(&cpu,SAVE));CHECK(r8(&cpu,SAVE_BUFFER+0x85)==1);
    for(unsigned enabled=0;enabled<2;enabled++) {
        setup(false,enabled);w8(&cpu,SAVE_BUFFER+0x85,2);w8(&cpu,SAVE_BUFFER+0x120,0);
        cpu.gpr[3]=0;cpu.gpr[4]=SAVE_BUFFER;cpu.gpr[5]=SAVE_SIZE;
        CHECK(!wii_mm9_mods_dispatch(&cpu,SAVE));CHECK(r8(&cpu,SAVE_BUFFER+0x85)==2);
    }
    setup(true,false);
    char json[768];CHECK(wii_mm9_mods_status(json,sizeof json)>0);CHECK(strstr(json,"\"attested\":true"));
    wii_mm9_mods_bind(NULL);free(cpu.ram);puts("MM9 optional-mod routing/input/pool contracts passed");return 0;
}
