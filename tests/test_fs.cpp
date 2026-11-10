/* SPDX-License-Identifier: MIT */
#include "wiillr/fs.h"
extern "C" {
#include "wiillr/ios.h"
}
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
int main(int argc,char** argv) {
  namespace fs=std::filesystem;
  CHECK(argc==2);
  const fs::path root=fs::path(argv[1])/("fs-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directories(root));
  const auto path=root.string();
  CHECK(!wii_fs_mount(path.c_str(),0,0));
  std::ofstream(root/".wiillr-nand-profile")<<"wiillr-copied-nand-v1\n";
  fs::create_directories(root/"sys");fs::create_directories(root/"title/00010001/41424344/content");
  const uint8_t uid_record[]={0,1,0,1,0x41,0x42,0x43,0x44,0,0,0,0};
  std::ofstream(root/"sys/uid.sys",std::ios::binary).write(reinterpret_cast<const char*>(uid_record),12);
  std::array<uint8_t,0x1e4> tmd{};tmd[1]=1;tmd[3]=1;
  std::memcpy(tmd.data()+0x18c,uid_record,8);
  std::ofstream(root/"title/00010001/41424344/content/title.tmd",std::ios::binary).write(reinterpret_cast<const char*>(tmd.data()),tmd.size());
  auto* nand=wii_fs_mount(path.c_str(),0,0);CHECK(nand);
  uint64_t boot_title=0;CHECK(wii_fs_boot_title(nand,&boot_title) && boot_title==0x0001000141424344ull);
  std::array<uint8_t,74> params{};std::memcpy(params.data()+6,"/save",6);params[70]=3;params[71]=1;params[72]=0;
  int32_t result=-99;
  CHECK(wii_fs_ioctl(nand,-1,9,params.data(),params.size(),nullptr,0,&result) && !result);
  const int fd=wii_fs_open(nand,"/save",3);CHECK(fd>=0);
  const int fd2=wii_fs_open(nand,"/save",1);CHECK(fd2>=0);
  const uint8_t progress[]={0x57,0x49,0x49,0,0x09};uint8_t readback[8]{};
  CHECK(wii_fs_write(nand,fd,progress,sizeof progress)==sizeof progress);
  CHECK(wii_fs_read(nand,fd2,readback,sizeof readback)==sizeof progress);
  CHECK(!std::memcmp(progress,readback,sizeof progress));
  CHECK(wii_fs_write(nand,fd2,progress,1)==-102);
  CHECK(wii_fs_seek(nand,fd,6,0)==-101);
  CHECK(wii_fs_seek(nand,fd,uint32_t(-2),2)==3);
  uint8_t status[8]{};
  CHECK(wii_fs_ioctl(nand,fd,11,nullptr,0,status,8,&result) && !result && status[3]==5 && status[7]==3);
  CHECK(!wii_fs_ioctl(nand,-1,10,params.data(),params.size(),nullptr,0,&result));
  CHECK(wii_fs_open(nand,"/../outside",3)==-101 && wii_fs_open(nand,"/NUL",3)==-101);
  CHECK(!wii_fs_close(nand,fd) && wii_fs_close(nand,fd)==-101);
  wii_fs_unmount(nand);
  nand=wii_fs_mount(path.c_str(),0,0);CHECK(nand);
  const int restored=wii_fs_open(nand,"/save",1);CHECK(restored>=0);
  CHECK(wii_fs_read(nand,restored,readback,8)==sizeof progress && !std::memcmp(progress,readback,sizeof progress));
  uint8_t name[64]{};std::memcpy(name,"/save",6);std::memset(params.data(),0xff,params.size());
  CHECK(wii_fs_ioctl(nand,-1,6,name,64,params.data(),74,&result) && !result && params[70]==3 && params[71]==1 && !params[72]);
  CHECK(!wii_fs_close(nand,restored));wii_fs_unmount(nand);
  // Exercise the real mailbox adapter with a file buffer in physical MEM2.
  nand=wii_fs_mount(path.c_str(),0,0);CHECK(nand);
  CPUState cpu;WiiIosNative ios;WiiIpc ipc;
  CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));cpu.spr[287]=PPC_BROADWAY_PVR;
  wii_ios_native_init(&ios);wii_ipc_init(&ipc);ios.fs=nand;
  const uint32_t message=0x10000200,buffer=0x10000400;
  const auto exchange=[&]() {
    wii_ipc_write(&ipc,4,0x36,cpu.cycles);wii_ipc_write(&ipc,0,message,cpu.cycles);
    wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    if(!wii_ios_native_tick(&ios,&ipc,&cpu))return false;cpu.cycles+=6000;
    if(!wii_ios_native_tick(&ios,&ipc,&cpu))return false;
    wii_ipc_write(&ipc,4,0x32,cpu.cycles);cpu.cycles+=6000;
    return wii_ios_native_tick(&ios,&ipc,&cpu);
  };
  std::memcpy(cpu.ram+0x800,"/save",6);
  mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,0x800,0);mem_write32_cia(&cpu,message+16,1,0);
  CHECK(exchange() && mem_read32_cia(&cpu,message+4,0)==0 && ios.descriptors[0]==5);
  mem_write32_cia(&cpu,message,3,0);mem_write32_cia(&cpu,message+8,0,0);
  mem_write32_cia(&cpu,message+12,buffer,0);mem_write32_cia(&cpu,message+16,8,0);
  CHECK(exchange() && mem_read32_cia(&cpu,message+4,0)==5 && !std::memcmp(cpu.mem2+0x400,progress,5));
  mem_write32_cia(&cpu,message,3,0);mem_write32_cia(&cpu,message+8,0,0);
  mem_write32_cia(&cpu,message+12,0x13fffffe,0);mem_write32_cia(&cpu,message+16,8,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-101);
  mem_write32_cia(&cpu,message,2,0);mem_write32_cia(&cpu,message+8,0,0);
  CHECK(exchange() && !ios.descriptors[0]);
  std::memcpy(cpu.ram+0x800,"/dev/es",8);
  mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,0x800,0);mem_write32_cia(&cpu,message+16,0,0);
  CHECK(exchange() && ios.descriptors[0]==6);
  mem_write32_cia(&cpu,message,7,0);mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0x20,0);
  mem_write32_cia(&cpu,message+16,0,0);mem_write32_cia(&cpu,message+20,1,0);mem_write32_cia(&cpu,message+24,0x1000,0);
  mem_write32_cia(&cpu,0x1000,buffer,0);mem_write32_cia(&cpu,0x1004,8,0);
  CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer,0)==0x10001 && mem_read32_cia(&cpu,buffer+4,0)==0x41424344);
  mem_write32_cia(&cpu,message,7,0);mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0x1d,0);
  mem_write32_cia(&cpu,message+16,1,0);mem_write32_cia(&cpu,0x1008,buffer+32,0);mem_write32_cia(&cpu,0x100c,32,0);
  CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && !std::memcmp(cpu.mem2+0x420,"/title/00010001/41424344/data",29));
  // A malformed ES output vector leaves the destination intact.
  mem_write32_cia(&cpu,message,7,0);mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0x20,0);
  mem_write32_cia(&cpu,message+16,0,0);mem_write32_cia(&cpu,0x1004,7,0);mem_write32_cia(&cpu,buffer,0x12345678,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017 && mem_read32_cia(&cpu,buffer,0)==0x12345678);
  cpu_free(&cpu);wii_fs_unmount(nand);
  nand=wii_fs_mount(path.c_str(),0x1001,1);CHECK(nand && wii_fs_open(nand,"/save",1)==-102);wii_fs_unmount(nand);
  // Only this exclusively created, flat disposable fixture is removed.
  CHECK(fs::remove(root/"save"));CHECK(fs::remove(root/"fst.bin"));
  CHECK(fs::remove(root/"title/00010001/41424344/content/title.tmd"));
  CHECK(fs::remove(root/"title/00010001/41424344/content"));CHECK(fs::remove(root/"title/00010001/41424344"));
  CHECK(fs::remove(root/"title/00010001"));CHECK(fs::remove(root/"title"));
  CHECK(fs::remove(root/"sys/uid.sys"));CHECK(fs::remove(root/"sys"));
  CHECK(fs::remove(root/".wiillr-nand-profile"));CHECK(fs::remove(root));
  return 0;
}
