/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "peripherals_internal.h"
#include "memory/memory.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(int argc,char** argv){try{
 const auto dir=(argc>1?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path())/("peripheral-portable-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(dir));const auto path=dir/"storage.img";{std::ofstream f(path,std::ios::binary);f.seekp(2*1024*1024-1);f.put(0);CHECK(f.good());}
 CPUState cpu{};CHECK(cpu_init(&cpu));WiiPeripherals* p=wii_peripherals_create();CHECK(p);WiiPeripheralsConfig c;wii_peripherals_defaults(&c);char error[256];const std::string file=path.string();c.sd={file.c_str(),false};CHECK(wii_peripherals_configure(p,&c,error,sizeof error));
 const u32 message=0x1000,request=0x2000,response=0x3000,data=0x4000;s32 result;auto put=[&](u32 at,u32 v){mem_write32_cia(&cpu,at,v,0);};
 auto send=[&](u32 cmd,u32 arg){std::memset(cpu.ram+message,0,32);std::memset(cpu.ram+request,0,36);put(message,6);put(message+12,7);put(message+16,request);put(message+20,36);put(message+24,response);put(message+28,16);put(request,cmd);put(request+12,arg);put(request+16,1);put(request+20,512);put(request+24,data);CHECK(wii_peripherals_submit(p,&cpu,cpu.ram+message,message,13,&result)==1);return result;};
 std::array<u8,512> pattern{};for(unsigned i=0;i<512;i++)pattern[i]=u8(17*i+3);std::memcpy(cpu.ram+data,pattern.data(),512);CHECK(send(24,512)==0);std::memset(cpu.ram+data,0,512);CHECK(send(17,512)==0 && !std::memcmp(cpu.ram+data,pattern.data(),512));
 c.sd.read_only=true;CHECK(wii_peripherals_configure(p,&c,error,sizeof error));CHECK(send(24,1024)==-13);put(response,0xaabbccdd);CHECK(send(17,2*1024*1024)==-4 && mem_read32_cia(&cpu,response,0)==0xaabbccdd);
 auto bad=c;const std::string missing=(dir/"missing.img").string();bad.sd.image_path=missing.c_str();CHECK(!wii_peripherals_configure(p,&bad,error,sizeof error));CHECK(send(17,512)==0 && !std::memcmp(cpu.ram+data,pattern.data(),512));
 CHECK(wii_peripherals_eject(p,2,error,sizeof error));CHECK(send(17,512)==-6);wii_peripherals_destroy(p);cpu_free(&cpu);
 std::ifstream disk(path,std::ios::binary);std::array<u8,512> read{};disk.seekg(512);disk.read(reinterpret_cast<char*>(read.data()),512);CHECK(read==pattern);disk.read(reinterpret_cast<char*>(read.data()),512);const std::array<u8,512> empty{};CHECK(read==empty);
 std::puts("portable image/SD DMA/read-only/failed-apply/eject contract passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
