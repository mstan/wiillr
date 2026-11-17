// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiillr/bluetooth.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
  CPUState cpu;CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));
  cpu.spr[287]=PPC_BROADWAY_PVR;
  auto* bt=wii_bluetooth_create(&cpu,nullptr,nullptr);CHECK(bt);
  const u64 first_report=wii_bluetooth_deadline(bt,0);
  CHECK(first_report>6000 && wii_bluetooth_deadline(bt,0)==first_report);
  auto put=[&](u32 p,u32 v){mem_write32_cia(&cpu,p,v,0);};
  const u32 event=0x10000200,control=0x10000300;
  auto vector=[&](u32 base,u32 n,u32 address,u32 size){put(base+n*8,address);put(base+n*8+4,size);};
  put(event,7);put(event+12,2);put(event+16,2);put(event+20,1);put(event+24,0x1000);
  vector(0x1000,0,0x1100,1);vector(0x1000,1,0x1102,2);vector(0x1000,2,0x10001000,64);
  cpu.ram[0x1100]=0x81;cpu.ram[0x1103]=64;
  CHECK(wii_bluetooth_submit(bt,event));
  const u32 acl=0x10000600;
  put(acl,7);put(acl+12,1);put(acl+16,2);put(acl+20,1);put(acl+24,0x1600);
  vector(0x1600,0,0x1700,1);vector(0x1600,1,0x1702,2);vector(0x1600,2,0x10002000,64);
  cpu.ram[0x1700]=0x82;cpu.ram[0x1703]=64;CHECK(wii_bluetooth_submit(bt,acl));
  u32 message=0;s32 result=0;bool ready=true;
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && !ready);
  put(control,7);put(control+12,0);put(control+16,6);put(control+20,1);put(control+24,0x1200);
  const u32 sizes[]={1,1,2,2,2,1};
  for(u32 n=0;n<6;n++)vector(0x1200,n,0x1300+n*4,sizes[n]);
  cpu.ram[0x1300]=0x20;cpu.ram[0x1310]=3;
  vector(0x1200,6,0x1400,3);cpu.ram[0x1400]=3;cpu.ram[0x1401]=12; // reset0x0c03
  CHECK(wii_bluetooth_submit(bt,control));
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && !ready);
  cpu.cycles=6000;
  CHECK(wii_bluetooth_deadline(bt,cpu.cycles)==1);
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && ready && message==event && result==6);
  const u8 expected[]={0x0e,4,1,3,12,0};
  CHECK(!std::memcmp(cpu.mem2+0x1000,expected,sizeof expected));
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && ready && message==control && result==3);
  // Reset preserves the original outstanding ACL transfer. Binding a second
  // one must still be rejected (the old reset erroneously discarded it).
  CHECK(!wii_bluetooth_submit(bt,acl));
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && !ready);
  // Unknown opcodes cannot create a success event or modify the request.
  cpu.ram[0x1400]=0xff;cpu.ram[0x1401]=0xff;put(control+4,0x12345678);
  CHECK(!wii_bluetooth_submit(bt,control));
  CHECK(mem_read32_cia(&cpu,control+4,0)==0x12345678);
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && !ready);
  // Reject a short actual output buffer before binding the endpoint.
  const auto command=[&](u16 opcode,std::initializer_list<u8> parameters) {
    if(!wii_bluetooth_submit(bt,event))return false;
    const u32 length=u32(parameters.size())+3;
    cpu.ram[0x1310]=u8(length);cpu.ram[0x1311]=u8(length>>8);vector(0x1200,6,0x1400,length);
    cpu.ram[0x1400]=u8(opcode);cpu.ram[0x1401]=u8(opcode>>8);cpu.ram[0x1402]=u8(parameters.size());
    std::copy(parameters.begin(),parameters.end(),cpu.ram+0x1403);
    if(!wii_bluetooth_submit(bt,control))return false;
    cpu.cycles+=6000;
    if(!wii_bluetooth_poll(bt,&message,&result,&ready) || !ready || message!=event)return false;
    if(!wii_bluetooth_poll(bt,&message,&result,&ready) || !ready || message!=control)return false;
    return true;
  };
  CHECK(command(0x0c24,{0x12,0x34,0x56}));
  CHECK(command(0x0c23,{}));
  const u8 configured[]={0x0e,7,1,0x23,0x0c,0,0x12,0x34,0x56};
  CHECK(!std::memcmp(cpu.mem2+0x1000,configured,sizeof configured));
  CHECK(command(0x0c03,{}));CHECK(command(0x0c23,{}));
  CHECK(!cpu.mem2[0x1006] && !cpu.mem2[0x1007] && !cpu.mem2[0x1008]);
  CHECK(command(0x1001,{}));
  CHECK(!cpu.mem2[0x1007] && !cpu.mem2[0x1008]); // explicit native firmware revision0
  CHECK(command(0x0c0d,{0,0,0,0,0,0,1}));
  CHECK(!cpu.mem2[0x1008] && !cpu.mem2[0x1009]); // no fabricated stored pairings
  CHECK(wii_bluetooth_submit(bt,event));CHECK(wii_bluetooth_press_sync(bt));
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && !ready);
  cpu.cycles+=6000;
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && ready && message==event && result==3);
  const u8 sync_event[]={0xff,1,8};CHECK(!std::memcmp(cpu.mem2+0x1000,sync_event,3));
  vector(0x1000,2,0x13fffffe,64);
  CHECK(!wii_bluetooth_submit(bt,event));
  // Closing the USB handle releases both pending reads without publishing a
  // reply or writing their buffers. A new handle can bind both endpoints.
  vector(0x1000,2,0x10001000,64);
  put(0x10001000,0x12345678);put(0x10002000,0x90abcdef);
  CHECK(wii_bluetooth_submit(bt,event));
  wii_bluetooth_close(bt);
  cpu.cycles+=6000;
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && !ready);
  CHECK(mem_read32_cia(&cpu,0x10001000,0)==0x12345678);
  CHECK(mem_read32_cia(&cpu,0x10002000,0)==0x90abcdef);
  CHECK(wii_bluetooth_submit(bt,acl));
  vector(0x1000,2,0x10003000,64);
  CHECK(wii_bluetooth_submit(bt,event) && wii_bluetooth_press_sync(bt));
  cpu.cycles+=6000;
  CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && ready && message==event && result==3);
  CHECK(!std::memcmp(cpu.mem2+0x3000,sync_event,3));
  CHECK(mem_read32_cia(&cpu,0x10001000,0)==0x12345678);
  wii_bluetooth_destroy(bt);cpu_free(&cpu);return 0;
}
