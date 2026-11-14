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
#include <vector>
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
  fs::create_directories(root/"title/00010001/55555555/data");
  std::ofstream(root/"title/00010001/55555555/data/progress")<<"fixture progress";
  std::ofstream(root/"title/00010001/77777777")<<"not a directory";
  const uint8_t uid_record[]={0,1,0,1,0x41,0x42,0x43,0x44,0,0,0,0};
  std::ofstream(root/"sys/uid.sys",std::ios::binary).write(reinterpret_cast<const char*>(uid_record),12);
  std::array<uint8_t,0x208> tmd{};tmd[1]=1;tmd[3]=1;
  std::memcpy(tmd.data()+0x18c,uid_record,8);
  tmd[0x1df]=1;tmd[0x1e7]=1;tmd[0x1e9]=3;tmd[0x1eb]=1;tmd[0x1f3]=5;
  std::ofstream(root/"title/00010001/41424344/content/title.tmd",std::ios::binary).write(reinterpret_cast<const char*>(tmd.data()),tmd.size());
  std::ofstream(root/"title/00010001/41424344/content/00000001.app",std::ios::binary)<<"title";
  fs::create_directories(root/"ticket/00010001");
  std::array<uint8_t,0x2a4> ticket{};ticket[1]=1;ticket[3]=1;
  std::memcpy(ticket.data()+0x1dc,uid_record,8);ticket[0x1f1]=152;ticket[0x2a3]=0x7b;
  std::ofstream(root/"ticket/00010001/41424344.tik",std::ios::binary).write(reinterpret_cast<const char*>(ticket.data()),ticket.size());
  auto* nand=wii_fs_mount(path.c_str(),0,0);CHECK(nand);
  // Launch preparation checks the actual installed view, boot index, UID and
  // content digest without changing active credentials or persistent data.
  auto launch_tmd=tmd;launch_tmd[0x187]=1;launch_tmd[0x18b]=37;launch_tmd[0x1e1]=3;
  constexpr uint8_t title_sha1[]{0x3c,0x6d,0xe1,0xb7,0xdd,0x91,0x46,0x5d,0x43,0x7e,
      0xf4,0x15,0xf9,0x4f,0x36,0xaf,0xc1,0xfb,0xc8,0xa8};
  std::memcpy(launch_tmd.data()+0x1f4,title_sha1,20);
  const auto launch_tmd_path=root/"title/00010001/41424344/content/title.tmd";
  const auto launch_content_path=root/"title/00010001/41424344/content/00000001.app";
  std::ofstream(launch_tmd_path,std::ios::binary).write(reinterpret_cast<const char*>(launch_tmd.data()),launch_tmd.size());
  std::array<uint8_t,0xd8> launch_view{};uint32_t launch_views=0;int32_t launch_result=99;
  CHECK(!wii_es_ticket_views(nand,0x0001000141424344ull,launch_view.data(),1,&launch_views) && launch_views==1);
  CHECK(wii_es_verify_content(nand,0x0001000141424344ull,3));
  CHECK(!wii_es_verify_content(nand,0x0001000141424344ull,2));
  std::ofstream(launch_content_path,std::ios::binary)<<"wrong";
  CHECK(!wii_es_verify_content(nand,0x0001000141424344ull,3));
  std::ofstream(launch_content_path,std::ios::binary)<<"title";
  CHECK(wii_es_verify_content(nand,0x0001000141424344ull,3));
  WiiTitleLaunch launch{};
  CHECK(wii_es_prepare_launch(nand,0x0001000141424344ull,launch_view.data(),&launch,&launch_result) && !launch_result);
  CHECK(launch.title==0x0001000141424344ull && launch.ios==0x100000025ull && !launch.uid && !launch.gid);
  CHECK(launch.size==5 && !std::memcmp(launch.image,"title",5) && !wii_fs_uid(nand));
  wii_es_release_launch(&launch);CHECK(!launch.image && !launch.size);
  launch_view[20]^=1;
  CHECK(wii_es_prepare_launch(nand,0x0001000141424344ull,launch_view.data(),&launch,&launch_result) && launch_result==-1017 && !launch.image);
  launch_view[20]^=1;std::ofstream(launch_content_path,std::ios::binary)<<"Title";
  CHECK(wii_es_prepare_launch(nand,0x0001000141424344ull,launch_view.data(),&launch,&launch_result) && launch_result==-1022 && !launch.image);
  std::ofstream(launch_content_path,std::ios::binary)<<"title";
  {
    CPUState handoff_cpu;WiiIosNative handoff;WiiIpc mailbox;
    CHECK(cpu_init(&handoff_cpu) && cpu_alloc_mem2(&handoff_cpu,0x4000000));
    constexpr uint32_t request=0x10000200,vectors=0x10000300,id=0x10000400,view=0x10000500;
    const auto put=[&](uint32_t at,uint32_t value){mem_write32_cia(&handoff_cpu,at,value,0);};
    for(unsigned n=0;n<3;n++) {
      wii_ios_native_init(&handoff);wii_ipc_init(&mailbox);handoff.fs=nand;handoff.descriptors[3]=6;
      if(n)handoff.validate_launch=[](void*,const WiiTitleLaunch* title) {
        return title->title==0x0001000141424344ull && title->size==5 && !std::memcmp(title->image,"title",5);
      };
      put(request,7);put(request+4,0x11223344);put(request+8,3);put(request+12,8);
      put(request+16,2);put(request+20,0);put(request+24,vectors);
      put(vectors,id);put(vectors+4,8);put(vectors+8,view);put(vectors+12,n==1?215:216);
      put(id,0x00010001);put(id+4,0x41424344);
      std::memcpy(handoff_cpu.mem2+0x500,launch_view.data(),launch_view.size());
      wii_ipc_write(&mailbox,0,request,handoff_cpu.cycles);wii_ipc_write(&mailbox,4,0x31,handoff_cpu.cycles);
      CHECK(wii_ios_native_tick(&handoff,&mailbox,&handoff_cpu));handoff_cpu.cycles+=6000;
      const bool handled=wii_ios_native_tick(&handoff,&mailbox,&handoff_cpu);
      if(!n)CHECK(!handled && !handoff.launch_pending && mailbox.x==1 && !mailbox.y);
      else if(n==1)CHECK(handled && !handoff.launch_pending && handoff.result==-1017 && mailbox.y==2);
      else {
        CHECK(handled && handoff.launch_pending && handoff.launch.title==0x0001000141424344ull && mailbox.y==2);
        CHECK(mem_read32_cia(&handoff_cpu,request,0)==7 && mem_read32_cia(&handoff_cpu,request+4,0)==0x11223344);
        wii_ipc_write(&mailbox,4,0x32,handoff_cpu.cycles);handoff_cpu.cycles+=10000;
        CHECK(wii_ios_native_tick(&handoff,&mailbox,&handoff_cpu) && !mailbox.y && handoff.launch_pending);
        CHECK(mem_read32_cia(&handoff_cpu,request,0)==7 && !wii_fs_uid(nand));
      }
      wii_ios_native_destroy(&handoff);
    }
    cpu_free(&handoff_cpu);
  }
  std::ofstream(launch_tmd_path,std::ios::binary).write(reinterpret_cast<const char*>(tmd.data()),tmd.size());
  CHECK(fs::is_directory(root/"tmp"));
  uint8_t mac[6]{},mac_copy[6]{};
  CHECK(wii_fs_network_mac(nand,mac) && (mac[0]&3)==2);
  CHECK(wii_fs_network_mac(nand,mac_copy) && !std::memcmp(mac,mac_copy,6));
  uint8_t bt_data[64]{};uint32_t bt_size=99;
  CHECK(wii_fs_bt_read(nand,bt_data,sizeof bt_data,&bt_size) && !bt_size);
  const uint8_t bt_fixture[]={0x57,0x49,0x49,0x42,0x54,0,1,2,3};
  CHECK(wii_fs_bt_write(nand,bt_fixture,sizeof bt_fixture));
  CHECK(wii_fs_bt_write(nand,bt_fixture,sizeof bt_fixture)); // atomic replacement
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
  CHECK(wii_fs_network_mac(nand,mac_copy) && !std::memcmp(mac,mac_copy,6));
  CHECK(wii_fs_bt_read(nand,bt_data,sizeof bt_data,&bt_size) && bt_size==sizeof bt_fixture && !std::memcmp(bt_data,bt_fixture,bt_size));
  CHECK(!wii_fs_bt_read(nand,bt_data,1,&bt_size));
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
    wii_ipc_write(&ipc,4,0x36,cpu.cycles);
    wii_ipc_write(&ipc,0x30,0x40000000u,cpu.cycles); /* Complete prior reply ISR. */
    wii_ipc_write(&ipc,0,message,cpu.cycles);
    wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    if(!wii_ios_native_tick(&ios,&ipc,&cpu))return false;cpu.cycles+=6000;
    if(!wii_ios_native_tick(&ios,&ipc,&cpu))return false;
    wii_ipc_write(&ipc,4,0x32,cpu.cycles);
    wii_ipc_write(&ipc,0x30,0x40000000u,cpu.cycles); /* Complete ACK ISR. */
    cpu.cycles+=6000;
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
  mem_write32_cia(&cpu,message,7,0);mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0x45,0);
  mem_write32_cia(&cpu,message+16,0,0);mem_write32_cia(&cpu,message+20,0,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017);
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
  // The original NAND loader requests real TMD-indexed content, then copies
  // its bytes into executable RAM through ES read/seek/close transactions.
  const auto es_request=[&](uint32_t op,uint32_t inputs,uint32_t outputs) {
    mem_write32_cia(&cpu,message,7,0);mem_write32_cia(&cpu,message+8,0,0);
    mem_write32_cia(&cpu,message+12,op,0);mem_write32_cia(&cpu,message+16,inputs,0);
    mem_write32_cia(&cpu,message+20,outputs,0);mem_write32_cia(&cpu,message+24,0x1000,0);
  };
  ios.descriptors[2]=10;
  mem_write32_cia(&cpu,0x1000,buffer,0);mem_write32_cia(&cpu,0x1004,4,0);
  mem_write32_cia(&cpu,0x1008,buffer+32,0);mem_write32_cia(&cpu,0x100c,6,0);
  mem_write32_cia(&cpu,buffer+36,0x11223344,0);
  es_request(8,0,2);mem_write32_cia(&cpu,message+8,2,0);
  CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && !std::memcmp(cpu.mem2+0x420,mac,6));
  CHECK(cpu.mem2[0x426]==0x33 && cpu.mem2[0x427]==0x44);
  mem_write32_cia(&cpu,0x100c,5,0);mem_write32_cia(&cpu,buffer,0x11223344,0);
  es_request(8,0,2);mem_write32_cia(&cpu,message+8,2,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-4 && mem_read32_cia(&cpu,buffer,0)==0x11223344);
  mem_write32_cia(&cpu,0x1000,0x10002000,0);mem_write32_cia(&cpu,0x1004,WIILLR_NETWORK_CONFIG_SIZE,0);
  mem_write32_cia(&cpu,0x1008,buffer+32,0);mem_write32_cia(&cpu,0x100c,32,0);
  mem_write32_cia(&cpu,buffer+32,0x11223344,0);mem_write32_cia(&cpu,buffer+36,0xabcdef01,0);
  es_request(5,0,2);mem_write32_cia(&cpu,message+8,2,0);
  CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && !mem_read32_cia(&cpu,buffer+32,0));
  CHECK(!cpu.mem2[0x2004] && !cpu.mem2[0x2008] && mem_read32_cia(&cpu,buffer+36,0)==0xabcdef01);
  std::array<uint8_t,WIILLR_NETWORK_CONFIG_SIZE> network{};network[4]=1;network.back()=0x5a;
  std::ofstream(root/"shared2/sys/net/02/config.dat",std::ios::binary).write(reinterpret_cast<const char*>(network.data()),network.size());
  es_request(5,0,2);mem_write32_cia(&cpu,message+8,2,0);
  CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && !std::memcmp(cpu.mem2+0x2000,network.data(),network.size()));
  const auto ncd_write=[&]() {
    es_request(6,1,1);mem_write32_cia(&cpu,message+8,2,0);
    mem_write32_cia(&cpu,buffer+32,0x11223344,0);
    CHECK(exchange());
    return int32_t(mem_read32_cia(&cpu,message+4,0));
  };
  const auto network_reader=wii_fs_open(nand,"/shared2/sys/net/02/config.dat",1);CHECK(network_reader>=0);
  const auto original_network=network;
  cpu.mem2[0x2004]=0;cpu.mem2[0x2000+network.size()-1]=0x6b;
  mem_write32_cia(&cpu,0x1004,WIILLR_NETWORK_CONFIG_SIZE-1,0);
  CHECK(ncd_write()==-4 && mem_read32_cia(&cpu,buffer+32,0)==0x11223344);
  mem_write32_cia(&cpu,0x1004,WIILLR_NETWORK_CONFIG_SIZE,0);
  mem_write32_cia(&cpu,0x100c,3,0);
  CHECK(ncd_write()==-4 && mem_read32_cia(&cpu,buffer+32,0)==0x11223344);
  CHECK(!wii_fs_network_config(nand,network.data()) && network==original_network);
  mem_write32_cia(&cpu,0x100c,32,0);
  CHECK(!ncd_write() && !mem_read32_cia(&cpu,buffer+32,0) && mem_read32_cia(&cpu,buffer+36,0)==0xabcdef01);
  CHECK(wii_fs_read(nand,network_reader,network.data(),network.size())==int32_t(network.size()));
  CHECK(!std::memcmp(network.data(),cpu.mem2+0x2000,network.size()) && !wii_fs_close(nand,network_reader));
  const auto saved_network=network;
  std::ofstream(root/"shared2/sys/net/02/config.dat",std::ios::binary)<<'x';
  network.fill(0x55);CHECK(wii_fs_network_config(nand,network.data())==-117 && network.front()==0x55);
  CHECK(ncd_write()==-117 && fs::file_size(root/"shared2/sys/net/02/config.dat")==1 &&
        mem_read32_cia(&cpu,buffer+32,0)==0x11223344);
  std::ofstream(root/"shared2/sys/net/02/config.dat",std::ios::binary).write(reinterpret_cast<const char*>(saved_network.data()),saved_network.size());
  // 0x18 is ticket deletion, not a metadata query, and remains unsupported.
  for(const auto operation:{0x18u}) {
    es_request(operation,1,0);mem_write32_cia(&cpu,message+4,0x11223344,0);
    CHECK(!exchange() && mem_read32_cia(&cpu,message,0)==7 && mem_read32_cia(&cpu,message+4,0)==0x11223344);
    ios.phase=0;wii_ipc_init(&ipc);
  }
  // Directory-backed enumeration includes an installed title's empty data
  // directory, excludes ordinary files, and bounds writes to the requested list.
  uint32_t title_count=0;
  CHECK(!wii_es_titles(nand,nullptr,0,&title_count) && title_count==2);
  mem_write32_cia(&cpu,0x1000,buffer,0);mem_write32_cia(&cpu,0x1004,4,0);
  es_request(0x0e,0,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer,0)==2);
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,4,0);
  mem_write32_cia(&cpu,0x1100,1,0);mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,8,0);
  mem_write32_cia(&cpu,buffer+8,0x11223344,0);
  es_request(0x0f,1,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  CHECK(mem_read32_cia(&cpu,buffer,0)==0x10001 && mem_read32_cia(&cpu,buffer+4,0)==0x41424344);
  CHECK(mem_read32_cia(&cpu,buffer+8,0)==0x11223344);
  mem_write32_cia(&cpu,0x1100,3,0);mem_write32_cia(&cpu,0x100c,24,0);
  mem_write32_cia(&cpu,buffer+16,0x12345678,0);
  es_request(0x0f,1,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  CHECK(mem_read32_cia(&cpu,buffer+8,0)==0x10001 && mem_read32_cia(&cpu,buffer+12,0)==0x55555555);
  CHECK(mem_read32_cia(&cpu,buffer+16,0)==0x12345678);
  mem_write32_cia(&cpu,0x100c,7,0);mem_write32_cia(&cpu,buffer,0x12345678,0);
  es_request(0x0f,1,1);CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017);
  CHECK(mem_read32_cia(&cpu,buffer,0)==0x12345678);
  mem_write32_cia(&cpu,0x1100,0xffffffff,0);
  es_request(0x0f,1,1);CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017);
  mem_write32_cia(&cpu,0x1000,0,0);mem_write32_cia(&cpu,0x1004,0,0);
  mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,0xd8,0);
  es_request(0x1b,1,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  CHECK(!mem_read32_cia(&cpu,buffer,0) && mem_read32_cia(&cpu,buffer+16,0)==0x10001);
  CHECK(mem_read32_cia(&cpu,buffer+20,0)==0x41424344 && cpu.mem2[0x425]==152 && cpu.mem2[0x4d7]==0x7b);
  // Supplied metadata uses the same transformation without changing its source.
  std::array<uint8_t,0xd8> view{};
  CHECK(!wii_es_ticket_view(nand,ticket.data(),ticket.size(),view.data()));
  CHECK(!std::memcmp(view.data()+4,ticket.data()+0x1d0,0xd4));
  // Arbitrary installed content uses the requested title, exact installed
  // ticket view and TMD content index; forged views never obtain a handle.
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,8,0);
  mem_write32_cia(&cpu,0x1100,0x10001,0);mem_write32_cia(&cpu,0x1104,0x41424344,0);
  mem_write32_cia(&cpu,0x1008,0x1200,0);mem_write32_cia(&cpu,0x100c,0xd8,0);
  std::memcpy(cpu.ram+0x1200,view.data(),view.size());
  mem_write32_cia(&cpu,0x1010,0x1120,0);mem_write32_cia(&cpu,0x1014,4,0);mem_write32_cia(&cpu,0x1120,3,0);
  es_request(0x24,3,0);CHECK(exchange());
  const auto other_content=mem_read32_cia(&cpu,message+4,0);CHECK(other_content<16);
  uint8_t original_content[8]{};
  CHECK(wii_es_content_read(nand,other_content,original_content,8)==5 && !std::memcmp(original_content,"title",5));
  CHECK(!wii_es_content_close(nand,other_content));
  cpu.ram[0x1224]^=1;es_request(0x24,3,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017);cpu.ram[0x1224]^=1;
  mem_write32_cia(&cpu,0x1104,0x55555555,0);es_request(0x24,3,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1028);
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,8,0);
  mem_write32_cia(&cpu,0x1100,0x10001,0);mem_write32_cia(&cpu,0x1104,0x41424344,0);
  mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,4,0);
  es_request(0x12,1,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer,0)==1);
  mem_write32_cia(&cpu,0x1008,0x1120,0);mem_write32_cia(&cpu,0x100c,4,0);mem_write32_cia(&cpu,0x1120,1,0);
  mem_write32_cia(&cpu,0x1010,buffer,0);mem_write32_cia(&cpu,0x1014,0xd8,0);
  es_request(0x13,2,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  CHECK(!std::memcmp(cpu.mem2+0x400,view.data(),view.size()));
  mem_write32_cia(&cpu,0x1004,32,0);mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,32,0);
  mem_write32_cia(&cpu,buffer+4,0x12345678,0);
  es_request(0x12,1,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer,0)==1);
  CHECK(mem_read32_cia(&cpu,buffer+4,0)==0x12345678);
  mem_write32_cia(&cpu,0x1004,8,0);mem_write32_cia(&cpu,0x1008,0x1120,0);mem_write32_cia(&cpu,0x100c,4,0);
  mem_write32_cia(&cpu,0x1014,0xd7,0);mem_write32_cia(&cpu,buffer,0x12345678,0);
  es_request(0x13,2,1);CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017);
  CHECK(mem_read32_cia(&cpu,buffer,0)==0x12345678);
  uint32_t view_count=99;std::array<uint8_t,0xd8+1> views{};views.back()=0x77;
  CHECK(!wii_es_ticket_views(nand,0x0001000155555555ull,views.data(),1,&view_count) && !view_count && views.back()==0x77);
  // A multi-ticket container is counted in full; a smaller result buffer is
  // bounded, and an invalid later ticket cannot partially publish views.
  auto pair=std::vector<uint8_t>(ticket.begin(),ticket.end());pair.insert(pair.end(),ticket.begin(),ticket.end());
  std::ofstream(root/"ticket/00010001/41424344.tik",std::ios::binary).write(reinterpret_cast<const char*>(pair.data()),pair.size());
  CHECK(!wii_es_ticket_views(nand,0x0001000141424344ull,views.data(),1,&view_count) && view_count==2);
  CHECK(!std::memcmp(views.data(),view.data(),view.size()) && views.back()==0x77);
  pair[0x2a4+0x1e0]^=1;views.fill(0x55);
  std::ofstream(root/"ticket/00010001/41424344.tik",std::ios::binary).write(reinterpret_cast<const char*>(pair.data()),pair.size());
  CHECK(wii_es_ticket_views(nand,0x0001000141424344ull,views.data(),1,&view_count)==-1017 && views[0]==0x55);
  std::ofstream(root/"ticket/00010001/41424344.tik",std::ios::binary).write(reinterpret_cast<const char*>(ticket.data()),ticket.size());
  CHECK(wii_es_unlimited_ticket(nand,0,&result) && !result);
  // The TMD view excludes signatures/access rights/hashes and retains real
  // title identity, version and each content's ID/index/type/64-bit size.
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,8,0);
  mem_write32_cia(&cpu,0x1100,0x10001,0);mem_write32_cia(&cpu,0x1104,0x41424344,0);
  mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,4,0);
  es_request(0x14,1,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer,0)==0x6c);
  mem_write32_cia(&cpu,0x1008,0x1120,0);mem_write32_cia(&cpu,0x100c,4,0);mem_write32_cia(&cpu,0x1120,0x6c,0);
  mem_write32_cia(&cpu,0x1010,buffer,0);mem_write32_cia(&cpu,0x1014,0x6c,0);
  es_request(0x15,2,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  CHECK(mem_read32_cia(&cpu,buffer+12,0)==0x10001 && mem_read32_cia(&cpu,buffer+16,0)==0x41424344);
  CHECK(mem_read32_cia(&cpu,buffer+0x58,0)==1 && mem_read32_cia(&cpu,buffer+0x5c,0)==1);
  CHECK(mem_read32_cia(&cpu,buffer+0x60,0)==0x00030001 && mem_read32_cia(&cpu,buffer+0x68,0)==5);
  mem_write32_cia(&cpu,0x1120,0x6b,0);mem_write32_cia(&cpu,0x1014,0x6b,0);
  mem_write32_cia(&cpu,buffer,0x12345678,0);
  es_request(0x15,2,1);CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017);
  CHECK(mem_read32_cia(&cpu,buffer,0)==0x12345678);
  mem_write32_cia(&cpu,0x1104,0x55555555,0);mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,4,0);
  es_request(0x14,1,1);CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-106);
  CHECK(mem_read32_cia(&cpu,buffer,0)==0x12345678);
  auto truncated=tmd;truncated[0x1df]=2;
  std::ofstream(root/"title/00010001/41424344/content/title.tmd",std::ios::binary).write(reinterpret_cast<const char*>(truncated.data()),truncated.size());
  uint32_t tmd_view_size=0;views.fill(0x55);
  CHECK(wii_es_tmd_view(nand,0x0001000141424344ull,views.data(),views.size(),&tmd_view_size)==-1017 && views[0]==0x55);
  std::ofstream(root/"title/00010001/41424344/content/title.tmd",std::ios::binary).write(reinterpret_cast<const char*>(tmd.data()),tmd.size());
  CHECK(wii_es_unlimited_ticket(nand,1,&result) && result==-1028);
  // System Menu asks about a channel's ticket while the menu is still active.
  // Include two records so lookup must inspect every installed ticket record.
  auto channel_ticket=ticket;
  channel_ticket[0x1e0]=0x55;channel_ticket[0x1e1]=0x55;channel_ticket[0x1e2]=0x55;channel_ticket[0x1e3]=0x55;
  channel_ticket[0x1d7]=2;
  std::ofstream channel_file(root/"ticket/00010001/55555555.tik",std::ios::binary);
  channel_file.write(reinterpret_cast<const char*>(channel_ticket.data()),channel_ticket.size());
  channel_ticket[0x1d7]=1;
  channel_file.write(reinterpret_cast<const char*>(channel_ticket.data()),channel_ticket.size());channel_file.close();
  CHECK(wii_es_unlimited_ticket(nand,1,&result) && !result);
  CHECK(wii_es_unlimited_ticket(nand,2,&result) && !result);
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,8,0);
  mem_write32_cia(&cpu,0x1100,0,0);mem_write32_cia(&cpu,0x1104,1,0);
  mem_write32_cia(&cpu,0x1008,0,0);mem_write32_cia(&cpu,0x100c,0,0);
  mem_write32_cia(&cpu,0x1010,buffer,0);mem_write32_cia(&cpu,0x1014,4,0);
  mem_write32_cia(&cpu,buffer,0xabcdef12,0);mem_write32_cia(&cpu,buffer+4,0x11223344,0);
  es_request(0x16,1,2);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  CHECK(!mem_read32_cia(&cpu,buffer,0) && mem_read32_cia(&cpu,buffer+4,0)==0x11223344);
  mem_write32_cia(&cpu,0x1104,3,0);mem_write32_cia(&cpu,buffer,0xabcdef12,0);
  es_request(0x16,1,2);CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1028);
  CHECK(mem_read32_cia(&cpu,buffer,0)==0xabcdef12);
  channel_ticket[0x267]=1;
  std::ofstream(root/"ticket/00010001/55555555.tik",std::ios::binary).write(reinterpret_cast<const char*>(channel_ticket.data()),channel_ticket.size());
  CHECK(!wii_es_unlimited_ticket(nand,1,&result));
  CHECK(wii_es_unlimited_ticket(nand,0,&result) && !result);
  std::ofstream(root/"ticket/00010001/55555555.tik",std::ios::binary)<<"truncated";
  CHECK(wii_es_unlimited_ticket(nand,1,&result) && result==-1017);
  CHECK(fs::remove(root/"ticket/00010001/55555555.tik"));
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,8,0);
  mem_write32_cia(&cpu,0x1100,0x10001,0);mem_write32_cia(&cpu,0x1104,0x41424344,0);
  mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,4,0);
  es_request(0x10,1,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer,0)==1);
  mem_write32_cia(&cpu,0x1008,0x1120,0);mem_write32_cia(&cpu,0x100c,4,0);mem_write32_cia(&cpu,0x1120,1,0);
  mem_write32_cia(&cpu,0x1010,buffer,0);mem_write32_cia(&cpu,0x1014,4,0);mem_write32_cia(&cpu,buffer+4,0x11223344,0);
  es_request(0x11,2,1);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer,0)==1);
  CHECK(mem_read32_cia(&cpu,buffer+4,0)==0x11223344);
  // Declared shared content is stored only when its hash resolves to an
  // existing shared file; the returned ID is the TMD ID, not the map filename.
  std::array<uint8_t,0x22c> shared_tmd{};std::memcpy(shared_tmd.data(),tmd.data(),tmd.size());shared_tmd[0x1df]=2;
  shared_tmd[0x20b]=2;shared_tmd[0x20e]=0x80;shared_tmd[0x20f]=1;shared_tmd[0x217]=6;
  std::fill(shared_tmd.begin()+0x218,shared_tmd.end(),0x42);
  std::ofstream(root/"title/00010001/41424344/content/title.tmd",std::ios::binary).write(reinterpret_cast<const char*>(shared_tmd.data()),shared_tmd.size());
  uint32_t stored_count=0;std::array<uint8_t,12> ids{};
  CHECK(!wii_es_stored_contents(nand,boot_title,ids.data(),3,&stored_count) && stored_count==1);
  fs::create_directory(root/"shared1");
  std::array<uint8_t,28> map{};std::memcpy(map.data(),"0000000a",8);std::fill(map.begin()+8,map.end(),0x42);
  std::ofstream(root/"shared1/content.map",std::ios::binary).write(reinterpret_cast<const char*>(map.data()),map.size());
  CHECK(!wii_es_stored_contents(nand,boot_title,ids.data(),3,&stored_count) && stored_count==1);
  std::ofstream(root/"shared1/0000000a.app")<<"shared";
  CHECK(!wii_es_stored_contents(nand,boot_title,ids.data(),3,&stored_count) && stored_count==2 && ids[3]==1 && ids[7]==2);
  std::ofstream(root/"shared1/content.map",std::ios::binary|std::ios::app)<<'x';ids.fill(0x55);
  CHECK(wii_es_stored_contents(nand,boot_title,ids.data(),3,&stored_count)==-1017 && ids[0]==0x55);
  std::ofstream(root/"title/00010001/41424344/content/title.tmd",std::ios::binary).write(reinterpret_cast<const char*>(tmd.data()),tmd.size());
  ticket[0x267]=1;
  std::ofstream(root/"ticket/00010001/41424344.tik",std::ios::binary).write(reinterpret_cast<const char*>(ticket.data()),ticket.size());
  CHECK(!wii_es_unlimited_ticket(nand,0,&result));
  ticket[0x267]=0;
  std::ofstream(root/"ticket/00010001/41424344.tik",std::ios::binary).write(reinterpret_cast<const char*>(ticket.data()),ticket.size());
  ticket[0x1bc]=1;view.fill(0x55);
  CHECK(wii_es_ticket_view(nand,ticket.data(),ticket.size(),view.data())==-1017 && view[0]==0x55);
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,4,0);mem_write32_cia(&cpu,0x1100,3,0);
  es_request(9,1,0);CHECK(exchange());const uint32_t content=mem_read32_cia(&cpu,message+4,0);CHECK(content<16);
  mem_write32_cia(&cpu,0x1100,content,0);mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,8,0);
  es_request(10,1,1);CHECK(exchange() && mem_read32_cia(&cpu,message+4,0)==5 && !std::memcmp(cpu.mem2+0x400,"title",5));
  mem_write32_cia(&cpu,0x1008,0x1104,0);mem_write32_cia(&cpu,0x100c,4,0);
  mem_write32_cia(&cpu,0x1010,0x1108,0);mem_write32_cia(&cpu,0x1014,4,0);
  mem_write32_cia(&cpu,0x1104,2,0);mem_write32_cia(&cpu,0x1108,0,0);
  es_request(0x23,3,0);CHECK(exchange() && mem_read32_cia(&cpu,message+4,0)==2);
  mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,8,0);
  es_request(10,1,1);CHECK(exchange() && mem_read32_cia(&cpu,message+4,0)==3 && !std::memcmp(cpu.mem2+0x400,"tle",3));
  es_request(11,1,0);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  es_request(10,1,1);CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017);
  mem_write32_cia(&cpu,0x1100,99,0);es_request(9,1,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017);
  // FS directory count and packed names come from actual NAND entries.
  ios.descriptors[1]=4;
  std::memset(cpu.ram+0x1800,0,64);std::memcpy(cpu.ram+0x1800,"/sys",5);
  mem_write32_cia(&cpu,0x1000,0x1800,0);mem_write32_cia(&cpu,0x1004,64,0);
  mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,4,0);
  es_request(4,1,1);mem_write32_cia(&cpu,message+8,1,0);
  CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer,0)==1);
  mem_write32_cia(&cpu,0x1008,0x1840,0);mem_write32_cia(&cpu,0x100c,4,0);
  mem_write32_cia(&cpu,0x1840,1,0);mem_write32_cia(&cpu,0x1010,buffer,0);
  mem_write32_cia(&cpu,0x1014,13,0);mem_write32_cia(&cpu,0x1018,buffer+32,0);mem_write32_cia(&cpu,0x101c,4,0);
  es_request(4,2,2);mem_write32_cia(&cpu,message+8,1,0);
  CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && mem_read32_cia(&cpu,buffer+32,0)==1);
  CHECK(!std::memcmp(cpu.mem2+0x400,"uid.sys",8));
  mem_write32_cia(&cpu,0x1014,12,0);mem_write32_cia(&cpu,buffer+32,0x11223344,0);
  es_request(4,2,2);mem_write32_cia(&cpu,message+8,1,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-101 && mem_read32_cia(&cpu,buffer+32,0)==0x11223344);
  // GetUsage returns actual directory storage, and malformed output vectors
  // cannot partially overwrite the first result.
  std::memset(cpu.ram+0x1800,0,64);std::memcpy(cpu.ram+0x1800,"/sys",5);
  mem_write32_cia(&cpu,0x1000,0x1800,0);mem_write32_cia(&cpu,0x1004,64,0);
  mem_write32_cia(&cpu,0x1008,buffer,0);mem_write32_cia(&cpu,0x100c,4,0);
  mem_write32_cia(&cpu,0x1010,buffer+32,0);mem_write32_cia(&cpu,0x1014,4,0);
  es_request(0x0c,1,2);mem_write32_cia(&cpu,message+8,1,0);
  CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  CHECK(mem_read32_cia(&cpu,buffer,0)==1 && mem_read32_cia(&cpu,buffer+32,0)==2);
  mem_write32_cia(&cpu,0x1014,3,0);mem_write32_cia(&cpu,buffer,0x12345678,0);
  es_request(0x0c,1,2);mem_write32_cia(&cpu,message+8,1,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-101 && mem_read32_cia(&cpu,buffer,0)==0x12345678);
  // ES deletion protects system titles and respects handles in the target
  // tree. Removing a different title preserves this title and unrelated saves.
  CHECK(wii_es_delete_title(nand,0x0000000100000002ull)==-1017);
  auto held=wii_fs_open(nand,"/title/00010001/55555555/data/progress",1);CHECK(held>=0);
  CHECK(wii_es_delete_title(nand,0x0001000155555555ull)<0 && fs::exists(root/"title/00010001/55555555/data/progress"));
  CHECK(!wii_fs_close(nand,held));
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,8,0);
  mem_write32_cia(&cpu,0x1100,0x10001,0);mem_write32_cia(&cpu,0x1104,0x55555555,0);
  es_request(0x17,1,0);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0));
  CHECK(!fs::exists(root/"title/00010001/55555555") && fs::exists(root/"title/00010001/41424344/content/title.tmd"));
  CHECK(fs::file_size(root/"save")==5);
  wii_fs_unmount(nand);
  nand=wii_fs_mount(path.c_str(),0x1001,1);CHECK(nand && wii_fs_open(nand,"/save",1)==-102);
  CHECK(!wii_fs_network_config(nand,network.data()) && network==saved_network);
  CHECK(!wii_es_titles(nand,nullptr,0,&title_count) && title_count==1);wii_fs_unmount(nand);
  // SetUID changes actual PPC file access, while existing ES handles retain
  // their opener's authority and the active-title queries retain boot identity.
  fs::create_directories(root/"title/00000001/00000002/content");
  auto menu_tmd=tmd;std::memset(menu_tmd.data()+0x18c,0,8);menu_tmd[0x18f]=1;menu_tmd[0x193]=2;menu_tmd[0x199]=7;
  std::ofstream(root/"title/00000001/00000002/content/title.tmd",std::ios::binary).write(reinterpret_cast<const char*>(menu_tmd.data()),menu_tmd.size());
  fs::create_directories(root/"title/00010001/55555555/content");
  auto target_tmd=tmd;std::memset(target_tmd.data()+0x190,0x55,4);target_tmd[0x198]=0x33;target_tmd[0x199]=0x44;
  std::ofstream(root/"title/00010001/55555555/content/title.tmd",std::ios::binary).write(reinterpret_cast<const char*>(target_tmd.data()),target_tmd.size());
  const uint8_t menu_uid[]={0,0,0,1,0,0,0,2,0,0,0x10,0};
  std::ofstream(root/"sys/uid.sys",std::ios::binary|std::ios::app).write(reinterpret_cast<const char*>(menu_uid),12);
  std::ofstream(root/"uid-test",std::ios::binary);
  nand=wii_fs_mount(path.c_str(),0,0);CHECK(nand);
  params.fill(0);params[2]=0x10;params[3]=1;params[4]=0x33;params[5]=0x44;
  std::memcpy(params.data()+6,"/uid-test",10);params[70]=3;
  CHECK(wii_fs_ioctl(nand,-1,5,params.data(),params.size(),nullptr,0,&result) && !result);wii_fs_unmount(nand);
  nand=wii_fs_mount(path.c_str(),0x1000,7);CHECK(nand && wii_fs_open(nand,"/uid-test",1)==-102);
  wii_ios_native_init(&ios);wii_ipc_init(&ipc);ios.fs=nand;
  std::memcpy(cpu.ram+0x800,"/dev/es",8);
  mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,0x800,0);mem_write32_cia(&cpu,message+16,0,0);
  CHECK(exchange() && ios.descriptors[0]==6 && ios.es_uids[0]==0x1000);
  mem_write32_cia(&cpu,0x1000,0x1100,0);mem_write32_cia(&cpu,0x1004,8,0);
  mem_write32_cia(&cpu,0x1100,0x10001,0);mem_write32_cia(&cpu,0x1104,0x55555555,0);
  es_request(0x21,1,0);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && wii_fs_uid(nand)==0x1001);
  CHECK(fs::file_size(root/"sys/uid.sys")==36);
  const auto own_save=wii_fs_open(nand,"/uid-test",1);CHECK(own_save>=0);CHECK(!wii_fs_close(nand,own_save));
  params.fill(0);std::memcpy(params.data()+6,"/tmp/uid-safe",14);params[70]=3;
  CHECK(wii_fs_ioctl(nand,-1,9,params.data(),params.size(),nullptr,0,&result) && !result);
  const auto staged=wii_fs_open(nand,"/tmp/uid-safe",3);CHECK(staged>=0);
  CHECK(wii_fs_write(nand,staged,progress,sizeof progress)==sizeof progress && !wii_fs_close(nand,staged));
  CHECK(wii_fs_boot_title(nand,&boot_title) && boot_title==0x0000000100000002ull);
  mem_write32_cia(&cpu,message,1,0);mem_write32_cia(&cpu,message+12,0x800,0);mem_write32_cia(&cpu,message+16,0,0);
  CHECK(exchange() && ios.descriptors[1]==6 && ios.es_uids[1]==0x1001);
  mem_write32_cia(&cpu,0x1100,1,0);mem_write32_cia(&cpu,0x1104,2,0);
  es_request(0x21,1,0);mem_write32_cia(&cpu,message+8,1,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-1017 && wii_fs_uid(nand)==0x1001);
  es_request(0x21,1,0);CHECK(exchange() && !mem_read32_cia(&cpu,message+4,0) && wii_fs_uid(nand)==0x1000);
  CHECK(wii_fs_open(nand,"/uid-test",1)==-102);
  mem_write32_cia(&cpu,0x1104,99,0);es_request(0x21,1,0);
  CHECK(exchange() && int32_t(mem_read32_cia(&cpu,message+4,0))==-106 && wii_fs_uid(nand)==0x1000);
  CHECK(fs::file_size(root/"sys/uid.sys")==36);wii_fs_unmount(nand);
  nand=wii_fs_mount(path.c_str(),0x1001,0x3344);CHECK(nand && wii_fs_boot_title(nand,&boot_title) && boot_title==0x0001000155555555ull);
  wii_fs_unmount(nand);cpu_free(&cpu);
  // Only this exclusively created, flat disposable fixture is removed.
  CHECK(fs::remove(root/"uid-test"));
  CHECK(fs::remove(root/".wiillr-network-mac"));
  CHECK(fs::remove(root/"shared2/sys/net/02/config.dat"));CHECK(fs::remove(root/"shared2/sys/net/02"));
  CHECK(fs::remove(root/"shared2/sys/net"));CHECK(fs::remove(root/"shared2/sys"));CHECK(fs::remove(root/"shared2"));
  CHECK(fs::remove(root/"title/00000001/00000002/content/title.tmd"));CHECK(fs::remove(root/"title/00000001/00000002/content"));
  CHECK(fs::remove(root/"title/00000001/00000002"));CHECK(fs::remove(root/"title/00000001"));
  CHECK(fs::remove(root/"title/00010001/55555555/content/title.tmd"));CHECK(fs::remove(root/"title/00010001/55555555/content"));CHECK(fs::remove(root/"title/00010001/55555555"));
  CHECK(fs::remove(root/"save"));CHECK(fs::remove(root/"fst.bin"));
  CHECK(fs::remove(root/"tmp/uid-safe"));CHECK(fs::remove(root/"tmp"));
  CHECK(fs::remove(root/"title/00010001/41424344/content/title.tmd"));
  CHECK(fs::remove(root/"title/00010001/41424344/content/00000001.app"));
  CHECK(fs::remove(root/"title/00010001/41424344/content"));CHECK(fs::remove(root/"title/00010001/41424344"));
  CHECK(fs::remove(root/"title/00010001/77777777"));
  CHECK(fs::remove(root/"title/00010001"));CHECK(fs::remove(root/"title"));
  CHECK(fs::remove(root/"sys/uid.sys"));CHECK(fs::remove(root/"sys"));
  CHECK(fs::remove(root/"shared1/content.map"));CHECK(fs::remove(root/"shared1/0000000a.app"));CHECK(fs::remove(root/"shared1"));
  CHECK(fs::remove(root/"ticket/00010001/41424344.tik"));CHECK(fs::remove(root/"ticket/00010001"));CHECK(fs::remove(root/"ticket"));
  CHECK(fs::remove(root/".wiillr-bluetooth"));CHECK(fs::remove(root/".wiillr-nand-profile"));CHECK(fs::remove(root));
  return 0;
}
