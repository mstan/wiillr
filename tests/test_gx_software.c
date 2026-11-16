/* SPDX-License-Identifier: MIT */
#include "wiillr/gx.h"
#include "gx/gx.h"
#include "gx/gx_raster.h"
#include "gx/gx_memory.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
static void be32(u8* p,u32 v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static u32 float_word(float v) {u32 word;memcpy(&word,&v,4);return word;}
static void vertex(u8* p,float x,float y,float u,float v) {
    be32(p,float_word(x));be32(p+4,float_word(y));be32(p+8,0);
    be32(p+12,float_word(u));be32(p+16,float_word(v));
}
static int bp(WiiGx* gx,u32 reg,u32 value) {
    u8 command[5]={0x61,(u8)reg,(u8)(value>>16),(u8)(value>>8),(u8)value};
    /* Exercise commands crossing individual FIFO submissions. */
    for (unsigned i=0;i<sizeof command;i++) if(!gx->api->fifo(command+i,1))return 0;
    return 1;
}
static int cp(WiiGx* gx,u32 reg,u32 value) {
    u8 command[6]={8,(u8)reg};be32(command+2,value);return gx->api->fifo(command,sizeof command);
}
int main(void) {
    CPUState cpu;WiiMachine m;WiiGx gx;
    CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));
    cpu.spr[287]=PPC_BROADWAY_PVR;wii_machine_init(&m,&cpu);
    CHECK(wii_gx_start(&gx,&m,&cpu,NULL,NULL,NULL));
    CHECK(gcn_gx_memory(&cpu,0x90001000,32,NULL)==cpu.mem2+0x1000);
    CHECK(gcn_gx_memory(&cpu,0xd0001000,32,NULL)==cpu.mem2+0x1000);
    CHECK(!gcn_gx_memory(&cpu,0x13fffff0,32,NULL));
    CHECK(!gcn_gx_memory(&cpu,0x08000000,4,NULL));
    CHECK(!gcn_gx_memory(&cpu,0x20001000,4,NULL));
    memset(cpu.ram+0x1000,0xa5,0x3000);
    for(unsigned i=0;i<64;i++)cpu.mem2[0x1000+i]=(u8)(i^0x73);
    CHECK(bp(&gx,0x64,0x10001000u>>5));CHECK(bp(&gx,0x65,2u<<10));
    CHECK(!memcmp(gcn_gx_tmem(),cpu.mem2+0x1000,64));
    for(unsigned i=0;i<16;i++)cpu.mem2[0x1110+i]=(u8)(i+0x21);
    CHECK(cp(&gx,0xac,0x10001100));CHECK(cp(&gx,0xbc,16));
    u8 indexed[5]={0x20};be32(indexed+1,0x00013500);
    CHECK(gx.api->fifo(indexed,sizeof indexed));
    CHECK(gcn_gx_xf()[0x500]==0x21222324 && gcn_gx_xf()[0x503]==0x2d2e2f30);
    memset(cpu.mem2+0x3000,0,32);
    cpu.mem2[0x3000]=0x61;cpu.mem2[0x3001]=0x47;cpu.mem2[0x3003]=0x12;cpu.mem2[0x3004]=0x34;
    u8 list[9]={0x40};be32(list+1,0x90003000);be32(list+5,32);
    CHECK(gx.api->fifo(list,sizeof list));CHECK(m.pe.token==0x1234);
    CHECK(bp(&gx,0x41,(1u<<3)|(1u<<4)));CHECK(bp(&gx,0x43,0));
    CHECK(bp(&gx,0x49,0));CHECK(bp(&gx,0x4a,3u|(3u<<10)));
    CHECK(bp(&gx,0x4b,0x10002000u>>5));CHECK(bp(&gx,0x4d,2));
    CHECK(bp(&gx,0x4e,256));CHECK(bp(&gx,0x53,63u<<18));CHECK(bp(&gx,0x54,1));
    CHECK(bp(&gx,0x4f,0xff40));CHECK(bp(&gx,0x50,0x80c0));CHECK(bp(&gx,0x51,0xffffff));
    /* Copy then clear, followed by the same copy from the colored EFB. */
    CHECK(bp(&gx,0x52,(1u<<14)|(1u<<11)));
    cpu.reserve_valid=true;cpu.reserve_addr=0x90002000;
    CHECK(bp(&gx,0x52,1u<<14));CHECK(!cpu.reserve_valid);
    u8 xfb[200];memcpy(xfb,cpu.mem2+0x2000,sizeof xfb);
    const u8 expected_yuy2[4]={116,166,116,96}; /* RGB(64,128,192), BT.601. */
    CHECK(!memcmp(xfb,expected_yuy2,4));CHECK(cpu.ram[0x2000]==0xa5);
    CHECK(bp(&gx,0x4b,0x2000u>>5));CHECK(bp(&gx,0x52,1u<<14));
    for(unsigned row=0;row<4;row++)CHECK(!memcmp(cpu.ram+0x2000+row*64,xfb+row*64,8));
    CHECK(bp(&gx,0x4b,0x10002400u>>5));CHECK(bp(&gx,0x52,12u<<3));
    u8 texture[64];memcpy(texture,cpu.mem2+0x2400,64);
    CHECK(bp(&gx,0x4b,0x2400u>>5));CHECK(bp(&gx,0x52,12u<<3));
    CHECK(!memcmp(texture,cpu.ram+0x2400,64));
    CHECK(texture[0]==255 && texture[1]==64 && texture[32]==128 && texture[33]==192);
    /* The Menu alternates register/konst writes to the same BP addresses.
     * A stage writing C0 must not feed that result into the next fragment. */
    u32 xf[0x1058]={0};xf[0]=xf[5]=xf[10]=float_word(1);
    xf[0x1020]=xf[0x1022]=float_word(1);xf[0x1025]=float_word(-.5f);xf[0x1026]=1;
    xf[0x101a]=float_word(2);xf[0x101b]=float_word(-2);xf[0x101c]=float_word(1);
    xf[0x101d]=xf[0x101e]=float_word(344);xf[0x101f]=float_word(1);
    xf[0x103f]=1;xf[0x1040]=5u<<7;gcn_gx_set_xf(xf,0x1058);
    CHECK(cp(&gx,0x30,0));CHECK(cp(&gx,0x50,1u<<9));CHECK(cp(&gx,0x60,1));
    CHECK(cp(&gx,0x70,1u|(4u<<1)|(1u<<21)|(4u<<22)));
    CHECK(bp(&gx,0x00,1));CHECK(bp(&gx,0x20,(342u<<12)|342u));
    CHECK(bp(&gx,0x21,(345u<<12)|345u));CHECK(bp(&gx,0x59,(171u<<10)|171u));
    CHECK(bp(&gx,0x30,7));CHECK(bp(&gx,0x31,3));CHECK(bp(&gx,0x80,0));
    CHECK(bp(&gx,0x88,(1u<<20)|(3u<<10)|7u));CHECK(bp(&gx,0x94,0x10002800u>>5));
    for(unsigned i=0;i<32;i++)cpu.mem2[0x2800+i]=(i&1)?0:128;
    CHECK(bp(&gx,0x28,0x3c0));CHECK(bp(&gx,0x40,0));CHECK(bp(&gx,0x43,1));
    CHECK(bp(&gx,0xf3,0x3f0000));
    CHECK(bp(&gx,0xe2,255u<<12));CHECK(bp(&gx,0xe3,0));
    CHECK(bp(&gx,0xe4,(255u<<12)|255u));CHECK(bp(&gx,0xe5,(255u<<12)|255u));
    for(unsigned reg=0xe2;reg<=0xe5;reg++)CHECK(bp(&gx,reg,0x800000u|(37u<<12)|37u));
    CHECK(bp(&gx,0xf6,29u<<9)); /* K1 alpha, independently retained as37. */
    CHECK(bp(&gx,0xc0,0x48248f));CHECK(bp(&gx,0xc1,0x08ffe0));
    u8 quad[83]={0x80,0,4};vertex(quad+3,-1,1,0,0);vertex(quad+23,1,1,.5f,0);
    vertex(quad+43,1,-1,.5f,1);vertex(quad+63,-1,-1,0,1);
    CHECK(gx.api->fifo(quad,sizeof quad));
    CHECK(bp(&gx,0x4b,0x10002c00u>>5));CHECK(bp(&gx,0x52,12u<<3));
    CHECK(cpu.mem2[0x2c00]==36 && cpu.mem2[0x2c01]==130 && cpu.mem2[0x2c03]==0);
    CHECK(gx.api->fifo(quad,sizeof quad));CHECK(bp(&gx,0x52,12u<<3));
    CHECK(cpu.mem2[0x2c00]==36 && cpu.mem2[0x2c01]==130 && cpu.mem2[0x2c03]==0);
    u32 saved_bp[256],saved_tev[16];memcpy(saved_bp,gcn_gx_bp(),sizeof saved_bp);
    memcpy(saved_tev,gx_raster_tev_state(),sizeof saved_tev);
    CHECK(bp(&gx,0xe2,0x800000u|(74u<<12)|55u));
    gcn_gx_set_bp(saved_bp);gx_raster_set_tev_state(saved_tev);
    CHECK(!memcmp(saved_tev,gx_raster_tev_state(),sizeof saved_tev));
    CHECK(gx.api->fifo(quad,sizeof quad));CHECK(bp(&gx,0x52,12u<<3));
    CHECK(cpu.mem2[0x2c00]==36 && cpu.mem2[0x2c01]==130 && cpu.mem2[0x2c03]==0);
    CHECK(bp(&gx,0x45,2));CHECK(m.pe.signal_finish_interrupt);
    m.vi.reg[GCN_VI_VTR>>1]=4u<<4;
    m.vi.reg[GCN_VI_VTO_HI>>1]=m.vi.reg[GCN_VI_VTO_LO>>1]=0;
    m.vi.reg[GCN_VI_VTE_HI>>1]=m.vi.reg[GCN_VI_VTE_LO>>1]=0;
    m.vi.reg[0x48>>1]=0x0102;
    const u32 top=0x10000000u|(0x10002000u>>5);
    m.vi.reg[GCN_VI_FB_LEFT_TOP_HI>>1]=(u16)(top>>16);
    m.vi.reg[(GCN_VI_FB_LEFT_TOP_HI+2)>>1]=(u16)top;
    m.fields=1;CHECK(wii_gx_tick(&gx));
    CHECK(gx.scanout_address==0x10002000 && gx.scanout_width==16 &&
          gx.scanout_height==4 && gx.scanout_stride==64);
    GcnllrNativeGxStatus status;gx.api->status(&status);
    CHECK(!status.failed && status.display_copies==3 && status.texture_copies==5 && status.presents==1);
    /* Actual CPU bus peeks must reach the same ordered EFB as FIFO copies.
     * Alpha policy is separate from the provider's raw ARGB sample. */
    CHECK(bp(&gx,0x40,1u<<4));
    CHECK(bp(&gx,0x41,(1u<<3)|(1u<<4)));CHECK(bp(&gx,0x43,1));
    CHECK(bp(&gx,0x49,0));CHECK(bp(&gx,0x4a,3u|(3u<<10)));
    CHECK(bp(&gx,0x4b,0x10002000u>>5));CHECK(bp(&gx,0x4d,2));
    CHECK(bp(&gx,0x4f,0x7f25));CHECK(bp(&gx,0x50,0x63d2));CHECK(bp(&gx,0x51,0x123456));
    CHECK(bp(&gx,0x52,(1u<<14)|(1u<<11)));
    u32 peeked=0;CHECK(gx.api->peek(0,0,0,&peeked) && peeked==0x7d2461d3u);
    CHECK(mem_read32_cia(&cpu,0xc8000000,0x100)==0x002461d3u && !cpu.halted);
    mem_write16_cia(&cpu,0xcc001008,1,0x104);
    CHECK(mem_read32_cia(&cpu,0x08000000,0x108)==0xff2461d3u && !cpu.halted);
    mem_write16_cia(&cpu,0xcc001008,2,0x10c);
    CHECK(mem_read32_cia(&cpu,0x88000000,0x110)==0x7d2461d3u && !cpu.halted);
    CHECK(mem_read32_cia(&cpu,0xc8400000,0x114)==0x123456u && !cpu.halted);
    CHECK(mem_read32_cia(&cpu,0x08400000,0x118)==0x123456u && !cpu.halted);
    CHECK(mem_read32_cia(&cpu,0xc8000a00,0x11c)==0 && !cpu.halted); /* x640 */
    CHECK(mem_read32_cia(&cpu,0xc8210000,0x120)==0 && !cpu.halted); /* y528 */
    /* A format change without another draw must affect a subsequent peek. */
    CHECK(bp(&gx,0x43,0));
    CHECK(gx.api->peek(0,0,0,&peeked) && peeked==0xff258d1fu);
    CHECK(!gx.api->peek(2,0,0,&peeked)); /* no fabricated scanout-as-depth */
    mem_write16_cia(&cpu,0xcc001008,3,0x124);
    (void)mem_read32_cia(&cpu,0xc8000000,0x128);CHECK(cpu.halted);cpu.halted=false;
    (void)mem_read32_cia(&cpu,0xc8800000,0x12c);CHECK(cpu.halted);cpu.halted=false;
    (void)mem_read16_cia(&cpu,0xc8000000,0x130);CHECK(cpu.halted);cpu.halted=false;
    CHECK(gx.api->drain());wii_gx_stop(&gx);
    CHECK(!m.gx_peek && !m.gx_user);
    (void)mem_read32_cia(&cpu,0xc8400000,0x134);CHECK(cpu.halted);
    cpu_free(&cpu);
    return 0;
}
