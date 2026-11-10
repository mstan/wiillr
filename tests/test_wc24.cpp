/* SPDX-License-Identifier: MIT */
#include "wiillr/fs.h"
extern "C" {
#include "wiillr/ios.h"
}
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
static void put32(uint8_t* p,uint32_t v){for(int n=3;n>=0;--n){p[n]=uint8_t(v);v>>=8;}}
static uint32_t get32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
int main(int argc,char** argv) {
  namespace fs=std::filesystem;CHECK(argc==2);
  const auto root=fs::path(argv[1])/("wc24-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  CHECK(fs::create_directories(root/"shared2/wc24"));
  CHECK(fs::create_directories(root/"title/00000001/00000002/data"));
  std::ofstream(root/".wiillr-nand-profile")<<"wiillr-copied-nand-v1\n";
  const auto write=[&](const char* path,const auto& bytes) {
    std::ofstream file(root/path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());return bool(file);
  };
  const auto read=[&](const char* path) {
    std::ifstream file(root/path,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),{});
  };
  constexpr char config_path[]="shared2/wc24/nwc24msg.cfg",backup_path[]="shared2/wc24/nwc24msg.cbk";
  std::array<uint8_t,1024> config{};put32(config.data(),0x57634366);put32(config.data()+4,8);
  config[100]=0x5a;config[1000]=0xa5; // unrelated stored data must survive generation
  const auto sum=[](auto& bytes) {uint32_t value=0;for(unsigned n=0;n<1020;n+=4)value+=get32(bytes.data()+n);put32(bytes.data()+1020,value);};
  sum(config);CHECK(write(config_path,config) && write(backup_path,config));
  std::array<uint8_t,256> settings{};uint32_t key=0x73b5dbfa;
  const std::string text="AREA=USA\r\nMODEL=RVL-001(USA)\r\nVIDEO=NTSC\r\nGAME=US\r\n";
  for(size_t n=0;n<text.size();n++){settings[n]=uint8_t(text[n])^uint8_t(key);CHECK(settings[n]);key=(key<<1)|(key>>31);}
  CHECK(write("title/00000001/00000002/data/setting.txt",settings));
  const std::array<uint8_t,4> chip{0x12,0x34,0x56,0x78};CHECK(write(".wiillr-console-id",chip));
  auto* nand=wii_fs_mount(root.string().c_str(),0x1000,1);CHECK(nand);
  uint64_t id=0;uint32_t stage=0;CHECK(!wii_fs_wc24_generate_id(nand,&id,&stage) && stage==1 && id && id<10000000000000000ull);
  const auto first=read(config_path);CHECK(first.size()==1024 && read(backup_path)==first);
  CHECK(get32(first.data()+16)==1 && get32(first.data()+20)==1);
  for(size_t n=0;n<config.size();n++)if(!(n>=8 && n<24) && n<1020)CHECK(first[n]==config[n]);
  auto verify=first;sum(verify);CHECK(verify==first);
  const auto first_id=id;wii_fs_unmount(nand);nand=wii_fs_mount(root.string().c_str(),0x1000,1);CHECK(nand);
  id=0;stage=0;CHECK(wii_fs_wc24_generate_id(nand,&id,&stage)==-35 && id==first_id && stage==1);
  CHECK(read(config_path)==first && read(backup_path)==first && read(".wiillr-console-id")==std::vector<uint8_t>(chip.begin(),chip.end()));
  // The real mailbox returns ID/stage for an already generated identity and
  // leaves the unused output tail untouched. Short output cannot mutate it.
  CPUState cpu;WiiIosNative ios;WiiIpc ipc;CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));
  wii_ios_native_init(&ios);wii_ipc_init(&ipc);ios.fs=nand;ios.descriptors[0]=7;
  constexpr uint32_t message=0x10000200,output=0x10000400;
  const auto exchange=[&]() {
    wii_ipc_init(&ipc);ios.phase=0;wii_ipc_write(&ipc,0,message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);
    if(!wii_ios_native_tick(&ios,&ipc,&cpu))return false;cpu.cycles+=6000;
    return wii_ios_native_tick(&ios,&ipc,&cpu);
  };
  mem_write32_cia(&cpu,message,6,0);mem_write32_cia(&cpu,message+8,0,0);mem_write32_cia(&cpu,message+12,0xf,0);
  mem_write32_cia(&cpu,message+16,0,0);mem_write32_cia(&cpu,message+20,0,0);
  mem_write32_cia(&cpu,message+24,output,0);mem_write32_cia(&cpu,message+28,32,0);mem_write32_cia(&cpu,output+16,0xabcdef01,0);
  CHECK(exchange() && !ios.result && int32_t(mem_read32_cia(&cpu,output,0))==-35);
  CHECK((uint64_t(mem_read32_cia(&cpu,output+4,0))<<32|mem_read32_cia(&cpu,output+8,0))==first_id);
  CHECK(mem_read32_cia(&cpu,output+12,0)==1 && mem_read32_cia(&cpu,output+16,0)==0xabcdef01);
  mem_write32_cia(&cpu,message+28,16,0);mem_write32_cia(&cpu,output,0x12345678,0);
  CHECK(exchange() && ios.result==-4 && mem_read32_cia(&cpu,output,0)==0x12345678);
  cpu_free(&cpu);
  // Existing registered identities remain byte-for-byte intact. Corrupt
  // inputs cannot be silently reset or reported as newly generated.
  auto registered=first;put32(registered.data()+20,2);sum(registered);CHECK(write(config_path,registered));
  CHECK(wii_fs_wc24_generate_id(nand,&id,&stage)==-36 && id==first_id && stage==2 && read(config_path)==registered);
  auto broken=registered;broken[1023]^=1;CHECK(write(config_path,broken));id=0x11223344;stage=99;
  CHECK(wii_fs_wc24_generate_id(nand,&id,&stage)==-14 && id==0x11223344 && stage==99 && read(config_path)==broken);
  auto wrap=config;put32(wrap.data()+16,31);sum(wrap);CHECK(write(config_path,wrap));
  CHECK(!wii_fs_wc24_generate_id(nand,&id,&stage) && id!=first_id && stage==1 && get32(read(config_path).data()+16)==0);
  CHECK(write(config_path,config));const std::array<uint8_t,1> truncated{0};CHECK(write("title/00000001/00000002/data/setting.txt",truncated));
  CHECK(wii_fs_wc24_generate_id(nand,&id,&stage)==-18 && read(config_path)==std::vector<uint8_t>(config.begin(),config.end()));
  wii_fs_unmount(nand);
  for(const char* path:{config_path,backup_path,"shared2/wc24","shared2","title/00000001/00000002/data/setting.txt",
      "title/00000001/00000002/data","title/00000001/00000002","title/00000001","title","tmp","fst.bin",".wiillr-console-id",".wiillr-nand-profile"})CHECK(fs::remove(root/path));
  CHECK(fs::remove(root));return 0;
}
