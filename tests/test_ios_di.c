/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/ios.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}} while(0)
static void consume_mailbox(WiiIpc* ipc,u32 control,u64 cycles) {
    wii_ipc_write(ipc,4,control,cycles);
    wii_ipc_write(ipc,0x30,0x40000000u,cycles);
}
static bool exchange(WiiIosNative* ios,WiiIpc* ipc,CPUState* cpu,u32 message) {
    consume_mailbox(ipc,0x36,cpu->cycles);
    wii_ipc_write(ipc,0,message,cpu->cycles);wii_ipc_write(ipc,4,0x31,cpu->cycles);
    if(!wii_ios_native_tick(ios,ipc,cpu))return false;
    cpu->cycles+=6000;
    if(!wii_ios_native_tick(ios,ipc,cpu))return false;
    consume_mailbox(ipc,0x32,cpu->cycles);cpu->cycles+=6000;
    return wii_ios_native_tick(ios,ipc,cpu);
}
int main(void) {
    CPUState cpu;GcnDi di;WiiIosNative ios;WiiIpc ipc;
    CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));cpu.spr[287]=PPC_BROADWAY_PVR;
    gcn_di_init_model(&di,true);wii_ios_native_init(&ios);ios.di=&di;wii_ipc_init(&ipc);
    const u32 message=0x10000200,input=0x10000400,output=0x10000600;
    memcpy(cpu.ram+0x800,"/dev/di",8);
    mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,0x800,0);
    CHECK(exchange(&ios,&ipc,&cpu,message) && ios.descriptors[0]==3);
    CHECK((di.disr&(GCN_DI_SR_DEINTMASK|GCN_DI_SR_TCINTMASK))==(GCN_DI_SR_DEINTMASK|GCN_DI_SR_TCINTMASK));
    CHECK(!(di.dicvr&GCN_DI_CVR_CVRINTMASK));
    for(unsigned n=0;n<3;n++) {
        const u32 ioctl=n==0 ? 0x7a:n==1 ? 0x88:0x95;
        mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
        mem_write32_cia(&cpu,message+12,ioctl,0);mem_write32_cia(&cpu,message+16,input,0);
        mem_write32_cia(&cpu,message+20,32,0);mem_write32_cia(&cpu,message+24,output,0);
        mem_write32_cia(&cpu,message+28,4,0);mem_write32_cia(&cpu,input,ioctl<<24,0);
        CHECK(exchange(&ios,&ipc,&cpu,message));
        CHECK(mem_read32_cia(&cpu,message+4,0)==1);
        CHECK(mem_read32_cia(&cpu,output,0)==(n<2 ? 1:di.disr));
    }
    /* An undersized destination is rejected without a partial write. */
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+28,3,0);mem_write32_cia(&cpu,output,0x55667788,0);
    CHECK(exchange(&ios,&ipc,&cpu,message) && mem_read32_cia(&cpu,message+4,0)==32);
    CHECK(mem_read32_cia(&cpu,output,0)==0x55667788);
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+12,0x12,0);mem_write32_cia(&cpu,input,0x12000000,0);
    mem_write32_cia(&cpu,message+24,0xe0000600,0);mem_write32_cia(&cpu,message+28,32,0);
    CHECK(exchange(&ios,&ipc,&cpu,message) && mem_read32_cia(&cpu,message+4,0)==32 && !di.cmd_pending);
    mem_write32_cia(&cpu,message+24,output,0);
    /* Inquiry executes real DMA into MEM2, then waits for device completion.
     * The ordinary command result remains intact after the IPC ACK. */
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+4,0x12345678,0);
    mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0x12,0);
    mem_write32_cia(&cpu,message+28,32,0);mem_write32_cia(&cpu,input,0x12000000,0);
    memset(cpu.ram+0x600,0xa5,32);
    CHECK(exchange(&ios,&ipc,&cpu,message) && ios.di_waiting && di.cmd_pending && !ipc.y);
    CHECK(mem_read32_cia(&cpu,message,0)==6 && mem_read32_cia(&cpu,message+4,0)==0x12345678);
    CHECK(di.dimar==output && mem_read32_cia(&cpu,output,0)==2 && mem_read32_cia(&cpu,output+4,0)==0x20060526);
    for(unsigned n=0;n<32;n++)CHECK(cpu.ram[0x600+n]==0xa5);
    /* A second async drive request must wait for the first, preserving both
     * messages and the first destination until actual device completion. */
    const u32 second=message+64,second_input=input+64,second_output=output+64;
    mem_write32_cia(&cpu,second,6,0);mem_write32_cia(&cpu,second+4,0x55667788,0);
    mem_write32_cia(&cpu,second+8,0,0);mem_write32_cia(&cpu,second+12,0x8d,0);
    mem_write32_cia(&cpu,second+16,second_input,0);mem_write32_cia(&cpu,second+20,32,0);
    mem_write32_cia(&cpu,second+24,second_output,0);mem_write32_cia(&cpu,second+28,32,0);
    mem_write32_cia(&cpu,second_input,0x8d000000,0);mem_write32_cia(&cpu,second_input+4,32,0);
    mem_write32_cia(&cpu,second_input+8,0,0);
    CHECK(exchange(&ios,&ipc,&cpu,second));
    CHECK(ios.di_message==message && ios.di_waiting && ios.phase==1);
    CHECK(mem_read32_cia(&cpu,message+4,0)==0x12345678 && mem_read32_cia(&cpu,second+4,0)==0x55667788);
    const u32 delay=(u32)di.pending_cycles;
    gcn_di_tick(&cpu,delay-1);cpu.cycles+=delay-1;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.di_waiting && !ipc.y);
    gcn_di_tick(&cpu,1);++cpu.cycles;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.di_waiting && ipc.y==1);
    CHECK(mem_read32_cia(&cpu,message,0)==8 && mem_read32_cia(&cpu,message+4,0)==1);
    consume_mailbox(&ipc,0x36,cpu.cycles);
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.di_message==second && ios.di_waiting);
    consume_mailbox(&ipc,0x36,cpu.cycles);
    gcn_di_tick(&cpu,(u32)di.pending_cycles);cpu.cycles+=300000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && !ios.di_waiting);
    CHECK(mem_read32_cia(&cpu,second,0)==8 && mem_read32_cia(&cpu,second+4,0)==2);
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+12,0x70,0);mem_write32_cia(&cpu,input,0x70000000,0);
    mem_write32_cia(&cpu,output,0x55667788,0);
    CHECK(exchange(&ios,&ipc,&cpu,message) && ios.di_waiting);
    gcn_di_tick(&cpu,(u32)di.pending_cycles);cpu.cycles+=300000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && mem_read32_cia(&cpu,message+4,0)==2);
    CHECK(mem_read32_cia(&cpu,output,0)==0x55667788 && di.error_code!=GCN_DI_ERR_NONE);
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+12,0xe0,0);mem_write32_cia(&cpu,input,0xe0000000,0);
    CHECK(exchange(&ios,&ipc,&cpu,message) && ios.di_waiting);
    gcn_di_tick(&cpu,(u32)di.pending_cycles);cpu.cycles+=300000;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && mem_read32_cia(&cpu,message+4,0)==1);
    CHECK(mem_read32_cia(&cpu,output,0)==di.diimmbuf && di.diimmbuf!=0);
    const u32 cover_message=message+64;
    mem_write32_cia(&cpu,cover_message,6,0);mem_write32_cia(&cpu,cover_message+4,0x55667788,0);
    mem_write32_cia(&cpu,cover_message+8,0,0);mem_write32_cia(&cpu,cover_message+12,0x79,0);
    mem_write32_cia(&cpu,cover_message+16,input,0);mem_write32_cia(&cpu,cover_message+20,32,0);
    mem_write32_cia(&cpu,input,0x79000000,0);
    CHECK(exchange(&ios,&ipc,&cpu,cover_message) && ios.di_waiting && !ipc.y);
    cpu.cycles+=729000000u;
    CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ios.di_waiting && !ipc.y);
    CHECK(mem_read32_cia(&cpu,cover_message,0)==6 && mem_read32_cia(&cpu,cover_message+4,0)==0x55667788);
    /* Unknown commands cannot turn into a successful empty-drive response. */
    mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+12,0xdead,0);mem_write32_cia(&cpu,message+28,4,0);
    CHECK(!exchange(&ios,&ipc,&cpu,message) && ipc.x==1 && !ipc.y);
    CHECK(mem_read32_cia(&cpu,message,0)==6);
    gcn_di_free(&di);cpu_free(&cpu);return 0;
}
