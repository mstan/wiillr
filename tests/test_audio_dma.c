/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dsp/dsp.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static u8 pcm[128];static unsigned received;
static void sink(void* user,const u8* samples,u32 size){(void)user;if(received+size<=sizeof pcm){memcpy(pcm+received,samples,size);received+=size;}}
static void source(GcnDsp* dsp,u32 address){gcn_dsp_aid_write16(dsp,GCN_DSP_AID_START_HI,(u16)(address>>16),true);gcn_dsp_aid_write16(dsp,GCN_DSP_AID_START_LO,(u16)address,true);}
int main(void) {
    CPUState cpu;CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));cpu.spr[287]=PPC_BROADWAY_PVR;
    GcnDsp dsp;gcn_dsp_reset_registers(&dsp);
    gcn_dsp_aid_write16(&dsp,GCN_DSP_AID_START_HI,0xffff,false);CHECK(dsp.aid_source==0x03ff0000);
    gcn_dsp_aid_write16(&dsp,GCN_DSP_AID_START_HI,0xffff,true);CHECK(dsp.aid_source==0x1fff0000);
    gcn_dsp_reset_registers(&dsp);dsp.audio_sink=sink;
    for(unsigned n=0;n<64;n++)cpu.mem2[0x2000+n]=(u8)(n+1);
    memset(cpu.mem2+0x3000,0xc7,32);
    source(&dsp,0x10002000);gcn_dsp_aid_write16(&dsp,GCN_DSP_AID_CTRL,0x8002,true);
    const u32 p32=729000000u/4000,p48=729000000u/6000;
    CHECK(gcn_dsp_aid_advance(&dsp,&cpu,199,p32) && !(dsp.csr&GCN_DSP_CSR_AIDINT) && !received);
    CHECK(gcn_dsp_aid_advance(&dsp,&cpu,1,p32) && (dsp.csr&GCN_DSP_CSR_AIDINT));dsp.csr&=~GCN_DSP_CSR_AIDINT;
    CHECK(gcn_dsp_aid_advance(&dsp,&cpu,p32-200,p32) && received==32 && !memcmp(pcm,cpu.mem2+0x2000,32));
    CHECK(!gcn_dsp_aid_read16(&dsp,GCN_DSP_AID_BLOCKS_LEFT) && !(dsp.csr&GCN_DSP_CSR_AIDINT));
    source(&dsp,0x10003000);gcn_dsp_aid_write16(&dsp,GCN_DSP_AID_CTRL,0x8001,true);
    CHECK(dsp.aid_cur_addr==0x10002020 && gcn_dsp_aid_advance(&dsp,&cpu,p32,p32));
    CHECK(received==64 && !memcmp(pcm+32,cpu.mem2+0x2020,32) && dsp.aid_cur_addr==0x10003000 && (dsp.csr&GCN_DSP_CSR_AIDINT));
    CHECK(gcn_dsp_aid_advance(&dsp,&cpu,p48-1,p48) && received==64);
    CHECK(gcn_dsp_aid_advance(&dsp,&cpu,1,p48) && received==96 && !memcmp(pcm+64,cpu.mem2+0x3000,32));
    gcn_dsp_aid_write16(&dsp,GCN_DSP_AID_CTRL,0,true);source(&dsp,0x13ffffe0);
    gcn_dsp_aid_write16(&dsp,GCN_DSP_AID_CTRL,0x8002,true);
    CHECK(gcn_dsp_aid_advance(&dsp,&cpu,p48,p48) && received==128);
    CHECK(!gcn_dsp_aid_advance(&dsp,&cpu,p48,p48) && received==128 && dsp.aid_blocks_left==1);
    cpu_free(&cpu);return 0;
}
