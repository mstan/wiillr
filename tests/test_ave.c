/* SPDX-License-Identifier: MIT */
#include "wiillr/machine.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static CPUState cpu;
static WiiMachine machine;
static void bus(bool scl,bool sda,bool output) {
    mem_write32_cia(&cpu,0xcd8000c0,(scl?0x4000u:0)|(sda?0x8000u:0),0);
    mem_write32_cia(&cpu,0x0d0000c4,0x4000u|(output?0x8000u:0),0);
}
static bool sda(void){return (mem_read32_cia(&cpu,0xcd8000c8,0)&0x8000u)!=0;}
static void start(void){bus(false,true,true);bus(true,true,true);bus(true,false,true);bus(false,false,true);}
static void stop(void){bus(false,false,true);bus(true,false,true);bus(true,true,true);}
static bool send(unsigned byte) {
    for(unsigned mask=0x80;mask;mask>>=1) {bus(false,(byte&mask)!=0,true);bus(true,(byte&mask)!=0,true);bus(false,(byte&mask)!=0,true);}
    bus(false,true,false);bus(true,true,false);const bool ack=!sda();
    bus(false,true,false);bus(false,false,true);return ack;
}
static unsigned receive(bool ack) {
    unsigned value=0;
    for(unsigned n=0;n<8;n++){bus(false,true,false);bus(true,true,false);value=(value<<1)|sda();bus(false,true,false);}
    bus(false,!ack,true);bus(true,!ack,true);bus(false,!ack,true);return value;
}
int main(void) {
    CHECK(cpu_init(&cpu));wii_machine_init(&machine,&cpu);
    bus(false,true,false);CHECK(sda()); /* released data is pulled up */
    start();CHECK(send(0xe0));CHECK(send(0x71));CHECK(send(0x8e));CHECK(send(0x42));stop();
    CHECK(machine.ave.writes==2 && machine.ave.valid[0x71] && machine.ave.registers[0x71]==0x8e && machine.ave.registers[0x72]==0x42);
    start();CHECK(!send(0xe2));stop();CHECK(machine.ave.writes==2); /* absent address NACK */
    start();CHECK(send(0xe0));CHECK(send(0x71));start();CHECK(send(0xe1));
    CHECK(receive(true)==0x8e && receive(false)==0x42);stop();
    CHECK(!cpu.halted && !machine.ave.failed);
    start();CHECK(send(0xe0));CHECK(send(0x99));start();CHECK(send(0xe1));
    CHECK(cpu.halted && machine.ave.failed); /* no fabricated reset-register data */
    cpu_free(&cpu);return 0;
}
