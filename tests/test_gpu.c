/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/machine.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static u8 observed[64];static unsigned used;
static int consume(void* user,const u8* bytes,u32 size){(void)user;if(used+size>sizeof observed)return 0;memcpy(observed+used,bytes,size);used+=size;return 1;}
static void cp32(CPUState* cpu,u32 off,u32 value){mem_write16_cia(cpu,GCN_CP_BASE+off,(u16)value,0);mem_write16_cia(cpu,GCN_CP_BASE+off+2,(u16)(value>>16),0);}
int main(void) {
    CPUState cpu;WiiMachine m;CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));
    cpu.spr[287]=PPC_BROADWAY_PVR;wii_machine_init(&m,&cpu);
    GcnCp gc;gcn_cp_init(&gc);gcn_cp_write(&gc,&cpu,GCN_CP_BASE+GCN_CP_FIFO_BASE_HI,0xffff,2);
    CHECK(gc.reg[GCN_CP_FIFO_BASE_HI>>1]==0x3ff);
    mem_write16_cia(&cpu,GCN_CP_BASE+GCN_CP_FIFO_BASE_HI,0xffff,0);
    CHECK(m.cp.reg[GCN_CP_FIFO_BASE_HI>>1]==0x1fff);
    const u32 base=0x10001000,end=base+32;
    mem_write32_cia(&cpu,GCN_PI_BASE+GCN_PI_FIFO_BASE,base,0);
    mem_write32_cia(&cpu,GCN_PI_BASE+GCN_PI_FIFO_END,end,0);
    mem_write32_cia(&cpu,GCN_PI_BASE+GCN_PI_FIFO_WPTR,base,0);
    cp32(&cpu,GCN_CP_FIFO_BASE_LO,base);cp32(&cpu,GCN_CP_FIFO_END_LO,end);
    cp32(&cpu,GCN_CP_FIFO_RPTR_LO,base);cp32(&cpu,GCN_CP_FIFO_WPTR_LO,base);
    mem_write16_cia(&cpu,GCN_CP_BASE+GCN_CP_CTRL,GCN_CP_CTRL_GPLINK_ENABLE,0);
    memset(cpu.ram+0x1000,0xa5,64);
    for(unsigned n=0;n<8;n++)mem_write64_cia(&cpu,GCN_GP_BASE,0x0123456789abcdefull+n,0);
    CHECK(m.gx_cpu_gather_writes==8 && m.gx_cpu_gather_bytes==64 && !m.gx_cpu_gather_rejected);
    CHECK(!wii_machine_claim_native_graphics(&m,"too-late"));
    CHECK(gcn_cp_fifo_rw_distance(&m.cp)==64 && m.pi.reg[GCN_PI_FIFO_WPTR>>2]==base);
    for(unsigned n=0;n<64;n++)CHECK(cpu.ram[0x1000+n]==0xa5);
    CHECK(cpu.mem2[0x1000]==1 && cpu.mem2[0x1007]==0xef && cpu.mem2[0x103f]==0xf6);
    m.gx_fifo=consume;
    mem_write16_cia(&cpu,GCN_CP_BASE+GCN_CP_CTRL,GCN_CP_CTRL_GPLINK_ENABLE|GCN_CP_CTRL_GPREAD_ENABLE,0);
    cpu.cycles+=32;wii_machine_tick(&m,&cpu);
    CHECK(!cpu.halted && used==64 && !memcmp(observed,cpu.mem2+0x1000,64) && !gcn_cp_fifo_rw_distance(&m.cp));
    CHECK(gcn_cp_fifo_read_pointer(&m.cp)==base);
    mem_write16_cia(&cpu,GCN_PE_BASE+GCN_PE_CTRL,GCN_PE_CTRL_FINISH_ENABLE,0);
    gcn_pe_set_finish(&m.pe);CHECK(mem_read32_cia(&cpu,GCN_PI_BASE,0)&GCN_PE_INT_FINISH);
    mem_write16_cia(&cpu,GCN_PE_BASE+GCN_PE_CTRL,GCN_PE_CTRL_FINISH_ENABLE|GCN_PE_CTRL_FINISH,0);
    CHECK(!(mem_read32_cia(&cpu,GCN_PI_BASE,0)&GCN_PE_INT_FINISH));
    mem_write64_cia(&cpu,GCN_GP_BASE,0,0);CHECK(m.gp.count==8);
    mem_write32_cia(&cpu,GCN_PI_BASE+GCN_PI_FIFO_RESET,1,0);CHECK(!m.gp.count);
    m.gx_fifo=NULL;for(unsigned n=0;n<8;n++)mem_write32_cia(&cpu,GCN_GP_BASE,0,0);
    cpu.cycles+=32;wii_machine_tick(&m,&cpu);CHECK(cpu.halted && gcn_cp_fifo_rw_distance(&m.cp)==32);
    wii_machine_init(&m,&cpu);cpu.halted=false;
    CHECK(wii_machine_claim_native_graphics(&m,"unit-native-initializer"));
    CHECK(!wii_machine_claim_native_graphics(&m,"second-owner"));
    mem_write32_cia(&cpu,GCN_GP_BASE,0x61000000,0);
    CHECK(cpu.halted && m.gx_cpu_gather_writes==1 && m.gx_cpu_gather_bytes==4 && m.gx_cpu_gather_rejected==1);
    CHECK(!m.gp.count && !gcn_cp_fifo_rw_distance(&m.cp));
    cpu_free(&cpu);return 0;
}
