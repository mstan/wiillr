/* SPDX-License-Identifier: MIT */
#include "wiillr/gx.h"
#include "gx/gx.h"
#include "gx/gx_memory.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while (0)
static void be32(u8* p,u32 v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
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
    CHECK(!status.failed && status.display_copies==3 && status.texture_copies==2 && status.presents==1);
    CHECK(gx.api->drain());wii_gx_stop(&gx);cpu_free(&cpu);
    return 0;
}
