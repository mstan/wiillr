/* SPDX-License-Identifier: MIT */
/* ROM-free architectural bus fixture, not evidence of an original IOS reply. */
#include "wiillr/machine.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void put32(unsigned char* p,unsigned value) {
    p[0]=(unsigned char)(value>>24);p[1]=(unsigned char)(value>>16);
    p[2]=(unsigned char)(value>>8);p[3]=(unsigned char)value;
}
static void put16(unsigned char* p,unsigned value) {p[0]=(unsigned char)(value>>8);p[1]=(unsigned char)value;}
/* Selected original proves actual PPC MMIO. The native-build Cbridge control
 * uses those same DI entry points directly, leaving its native IOS owner idle. */
static unsigned di_ppc_read(CPUState* cpu,WiiMachine* machine,unsigned offset) {
#if WIILLR_IOS_LLE
    (void)machine;return mem_read32(cpu,0xcd006000u+offset,0);
#else
    return gcn_di_read(&machine->di,cpu,GCN_DI_BASE+offset,4);
#endif
}
static void di_ppc_write(CPUState* cpu,WiiMachine* machine,unsigned offset,unsigned value) {
#if WIILLR_IOS_LLE
    (void)machine;mem_write32(cpu,0xcd006000u+offset,value,0);
#else
    gcn_di_write(&machine->di,cpu,GCN_DI_BASE+offset,value,4);
#endif
}
int main(void) {
    CPUState cpu;CHECK(cpu_init(&cpu));CHECK(cpu_alloc_mem2(&cpu,WII_MEM2_SIZE));
    WiiMachine machine;wii_machine_init(&machine,&cpu);
    unsigned char image[16+84+0x44]={0};unsigned char* elf=image+16;
    put32(image,16);put32(image+8,sizeof image-16);
    put32(elf,0x7f454c46);elf[4]=1;elf[5]=2;elf[6]=1;
    put16(elf+16,2);put16(elf+18,40);put32(elf+20,1);put32(elf+24,0xffff0000);
    put32(elf+28,52);put16(elf+40,52);put16(elf+42,32);put16(elf+44,1);
    put32(elf+52,1);put32(elf+56,84);put32(elf+60,0xffff0000);put32(elf+64,0xffff0000);
    put32(elf+68,0x44);put32(elf+72,0x44);put32(elf+76,5);put32(elf+80,4);
    const unsigned words[]={
        0xe59f0030,0xe3a01010,0xe5801000,0xe59f2028,0xe59f3028,
        0xe5934000,0xe5945000,0xe5845004,0xe5824000,0xe3a01005,
        0xe5801000,0xeafffffe,0,0,0x0d80000c,0x0d800008,0x0d800000};
    for(unsigned n=0;n<sizeof words/sizeof *words;++n)put32(elf+84+4*n,words[n]);
    cpu.cycles=300;
    machine.ios_lle=wii_ios_lle_create(&cpu,&machine.ipc,&machine.hollywood,image,sizeof image,NULL,0,cpu.cycles);
    CHECK(machine.ios_lle);
    unsigned char seeprom[256]={0};
    CHECK(!wii_ios_lle_load_seeprom(NULL,seeprom,sizeof seeprom));
    CHECK(!wii_ios_lle_load_seeprom(machine.ios_lle,NULL,sizeof seeprom));
    CHECK(!wii_ios_lle_load_seeprom(machine.ios_lle,seeprom,sizeof seeprom-1));
    CHECK(wii_ios_lle_load_seeprom(machine.ios_lle,seeprom,sizeof seeprom));
    CHECK(!wii_ios_lle_load_nand(NULL,seeprom,sizeof seeprom));
    CHECK(!wii_ios_lle_load_nand(machine.ios_lle,NULL,0x21000000u));
    CHECK(!wii_ios_lle_load_nand(machine.ios_lle,seeprom,sizeof seeprom));
    mem_write32(&cpu,0x90000400,0x12345678,0);
    mem_write32(&cpu,0x80000404,0xaabbccdd,0);
    cpu.reserve_addr=0xd0000404;cpu.reserve_valid=true;
#if WIILLR_IOS_LLE
    mem_write32(&cpu,0xcd000000,0x10000400,0);
    mem_write32(&cpu,0xcd000034,0x40000000,0);
    mem_write32(&cpu,0xcd000004,0x11,0);
#else
    CHECK(wii_ipc_write(&machine.ipc,0,0x10000400,cpu.cycles));
    CHECK(wii_ipc_write(&machine.ipc,0x34,0x40000000,cpu.cycles));
    CHECK(wii_ipc_write(&machine.ipc,4,0x11,cpu.cycles));
#endif
    cpu.cycles=12300;
#if WIILLR_IOS_LLE
    wii_machine_tick(&machine,&cpu);CHECK(!cpu.halted);
#else
    CHECK(wii_ios_lle_tick(machine.ios_lle,cpu.cycles));
#endif
    CHECK(mem_read32(&cpu,0x90000404,0)==0x12345678);
    CHECK(mem_read32(&cpu,0x80000404,0)==0xaabbccdd && !cpu.reserve_valid);
    CHECK(machine.ipc.arm_message==0x10000400 && !(machine.ipc.x&1));
    CHECK(wii_ipc_ppc_interrupt(&machine.ipc));
    const WiiIosLleStatus status=wii_ios_lle_status(machine.ios_lle);
    CHECK(status.instructions>10 && status.arm_cycles>=4000 && status.arm_cycles<4004 && !status.stopped);
    CHECK(!wii_ios_lle_load_seeprom(machine.ios_lle,seeprom,sizeof seeprom));
    CHECK(!wii_ios_lle_tick(machine.ios_lle,cpu.cycles-1));
    CHECK(strstr(wii_ios_lle_error(machine.ios_lle),"monotonic"));
    wii_ios_lle_destroy(machine.ios_lle);
    /* Missing fuse input must remain an explicit failure through the C ABI. */
    put32(elf+84,0xe59f0030);put32(elf+88,0xe3a01102);
    put32(elf+92,0xe5801000);put32(elf+96,0xe5902004);
    put32(elf+84+0x38,0x0d8001ec);
    machine.ios_lle=wii_ios_lle_create(&cpu,&machine.ipc,&machine.hollywood,image,sizeof image,NULL,0,cpu.cycles);
    CHECK(machine.ios_lle && !wii_ios_lle_tick(machine.ios_lle,cpu.cycles+1000));
    CHECK(wii_ios_lle_status(machine.ios_lle).stopped);
    CHECK(strstr(wii_ios_lle_error(machine.ios_lle),"128-byte OTP"));
    wii_ios_lle_destroy(machine.ios_lle);machine.ios_lle=NULL;
    /* Same DI as the PPC MMIO path, selected no-disc model, no private drive.
     * A real existing-core RequestError command earns TCINT/PI; original ARM
     * then observes and acknowledges it through the shared status register. */
    const unsigned di_words[]={
        0xe59f0030,0xe59f1030,0xe5902000,0xe5812000,0xe5902004,
        0xe5812004,0xe5912008,0xe5802000,0xe5902000,0xe581200c,
        0xe5902004,0xe5812010,0xeafffffe,0,0x0d006000,0x800,0};
    for(unsigned physical_port=0;physical_port<2;++physical_port) {
        CHECK(!machine.di.disc_present && !machine.di.disc_file);
        cpu.halted=false;wii_machine_scheduler_init(&machine,&cpu);
        di_ppc_write(&cpu,&machine,4,GCN_DI_CVR_CVRINT);
        CHECK(machine.di.dicvr==1);
        di_ppc_write(&cpu,&machine,0,GCN_DI_SR_TCINTMASK);
        di_ppc_write(&cpu,&machine,8,0xe0000000);
        di_ppc_write(&cpu,&machine,0x1c,GCN_DI_CR_TSTART);
        CHECK(machine.di.cmd_pending && !machine.di.irq_level);
        gcn_di_tick(&cpu,729000); /* Existing PPC core fixture, not PPC execution. */
        CHECK(!machine.di.cmd_pending && machine.di.irq_level && (machine.pi.intsr&GCN_PI_INT_DI));
        for(unsigned phase=0;phase<5;++phase) {
            for(unsigned n=0;n<sizeof di_words/sizeof *di_words;++n)put32(elf+84+4*n,di_words[n]);
            put32(elf+84+0x38,physical_port?0x0d806000:0x0d006000);
            if(phase==1) { /* Read PPC command programming; ARM writes same register. */
                put32(elf+84+8,0xe5902008);put32(elf+84+28,0xe5802008);
                di_ppc_write(&cpu,&machine,8,0xaabbccdd);
            }
            if(phase==2)put32(elf+84+28,0xe5802004); /* Cover mask/W1C. */
            if(phase==3)put32(elf+84+28,0xe580201c); /* Refused ARM TSTART. */
            put32(cpu.ram+0x808,phase==0?0x18:phase==1?0x11223344:phase==2?6:1);
            memset(cpu.ram+0x2000,0x6d,32);
            CHECK(wii_ipc_write(&machine.ipc,0x38,1u<<18,cpu.cycles));
            cpu.halted=false;
            machine.ios_lle=wii_ios_lle_create(&cpu,&machine.ipc,&machine.hollywood,image,sizeof image,NULL,0,cpu.cycles);
            CHECK(machine.ios_lle);
            wii_machine_scheduler_init(&machine,&cpu); /* Fresh synthetic context, including after intentional stops. */
            GcnDi invalid={0};
            CHECK(!wii_ios_lle_attach_di(NULL,&machine.di));
            CHECK(!wii_ios_lle_attach_di(machine.ios_lle,NULL));
            CHECK(!wii_ios_lle_attach_di(machine.ios_lle,&invalid));
            if(phase!=4) {
                CHECK(wii_ios_lle_attach_di(machine.ios_lle,&machine.di));
                CHECK(!wii_ios_lle_attach_di(machine.ios_lle,&machine.di));
            }
            const GcnDi before=machine.di;
            cpu.cycles+=WII_MACHINE_EVENT_CAP;
#if WIILLR_IOS_LLE
            wii_machine_tick(&machine,&cpu);
            CHECK(cpu.halted==(phase>=3));
#else
            CHECK(wii_ios_lle_tick(machine.ios_lle,cpu.cycles)==(phase<3));
#endif
            if(phase==0) {
                CHECK(mem_read32(&cpu,0x800,0)==0x18 && mem_read32(&cpu,0x804,0)==1);
                CHECK(mem_read32(&cpu,0x80c,0)==8 && di_ppc_read(&cpu,&machine,0)==8);
                CHECK(machine.ipc.arm_flags&(1u<<18));
                CHECK(!machine.di.irq_level && !(machine.pi.intsr&GCN_PI_INT_DI));
                CHECK(wii_ipc_write(&machine.ipc,0x38,1u<<18,cpu.cycles));
                CHECK(!(machine.ipc.arm_flags&(1u<<18)));
            } else if(phase==1) {
                CHECK(mem_read32(&cpu,0x800,0)==0xaabbccdd);
                CHECK(di_ppc_read(&cpu,&machine,8)==0x11223344);
            } else if(phase==2) {
                CHECK(mem_read32(&cpu,0x804,0)==1 && mem_read32(&cpu,0x810,0)==3);
                CHECK(di_ppc_read(&cpu,&machine,4)==3 && !machine.di.irq_level);
            } else {
                CHECK(wii_ios_lle_status(machine.ios_lle).stopped);
                CHECK(strstr(wii_ios_lle_error(machine.ios_lle),"DI"));
                GcnDi after=machine.di;
#if WIILLR_IOS_LLE
                CHECK(after.now_cycles==before.now_cycles+WII_MACHINE_EVENT_CAP);
#else
                CHECK(after.now_cycles==before.now_cycles);
#endif
                after.now_cycles=before.now_cycles; /* Only the existing clock owner advanced. */
                CHECK(!memcmp(&before,&after,sizeof before));
            }
            CHECK(!wii_ios_lle_attach_di(machine.ios_lle,&machine.di));
            CHECK(!machine.di.disc_present && !machine.di.cmd_pending && !machine.di.pending_read_active);
            for(unsigned n=0;n<32;++n)CHECK(cpu.ram[0x2000+n]==0x6d);
            wii_ios_lle_destroy(machine.ios_lle);machine.ios_lle=NULL;
        }
    }
    wii_ios_native_destroy(&machine.ios);cpu_free(&cpu);return 0;
}
