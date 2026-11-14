/* SPDX-License-Identifier: MIT */
#include "wiillr/network.h"
#include "wiillr/network_profile.h"
#include "memory/memory.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using HostSocket=SOCKET;using SockLen=int;
static void close_host(HostSocket s){closesocket(s);}
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using HostSocket=int;using SockLen=socklen_t;
static void close_host(HostSocket s){close(s);}
#endif
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned writes;
extern "C" u8* gcn_mem_resolve(CPUState* cpu,u32 address,u32* available){
    const u32 p=gcn_reservation_key(address);u8* bytes=nullptr;u32 count=0;
    if(p<cpu->ram_size){bytes=cpu->ram+p;count=cpu->ram_size-p;}
    if(p>=0x10000000u && p-0x10000000u<cpu->mem2_size){bytes=cpu->mem2+p-0x10000000u;count=cpu->mem2_size-(p-0x10000000u);}
    if(available)*available=count;
    return bytes;
}
extern "C" void mem_write8_cia(CPUState* cpu,u32 address,u8 value,u32 cia){
    u32 available=0;u8* p=gcn_mem_resolve(cpu,address,&available);
    if(!p || !available || cia!=cpu->pc)std::abort();
    *p=value;++writes;
}
static void put(u8* p,u32 value){p[0]=u8(value>>24);p[1]=u8(value>>16);p[2]=u8(value>>8);p[3]=u8(value);}
static u32 get(const u8* p){return u32(p[0])<<24|u32(p[1])<<16|u32(p[2])<<8|p[3];}
static int ioctl(WiiNetwork* net,CPUState* cpu,u32 op,const std::vector<u8>& input,u32 output,u32 size,s32* result,u32 message=0x200){
    u8* r=cpu->ram+message;std::memset(r,0,32);put(r,6);put(r+8,12);put(r+12,op);
    put(r+16,0x1000);put(r+20,u32(input.size()));put(r+24,output);put(r+28,size);
    std::memcpy(cpu->ram+0x1000,input.data(),input.size());return wii_network_submit(net,cpu,message,result);
}
static int ioctlv(WiiNetwork* net,CPUState* cpu,u32 op,const std::vector<std::pair<u32,u32>>& in,const std::vector<std::pair<u32,u32>>& out,s32* result,u32 message=0x200,bool wireless=false){
    u8* r=cpu->ram+message;std::memset(r,0,32);put(r,7);put(r+8,12);put(r+12,op);
    put(r+16,u32(in.size()));put(r+20,u32(out.size()));put(r+24,0x4000);u8* at=cpu->ram+0x4000;
    for(const auto& list:{in,out})for(const auto [address,size]:list){put(at,address);put(at+4,size);at+=8;}
    return wireless?wii_network_wireless_submit(net,cpu,message,result):wii_network_submit(net,cpu,message,result);
}
static bool completion(WiiNetwork* net,CPUState* cpu,u32 expected,u32 command,s32* result){
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<until){u32 message=0,cmd=0;
        if(wii_network_poll(net,cpu,&message,&cmd,result))return message==expected && cmd==command;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;
}
/* The filter fixtures request a 40ms single-channel dwell. */
static bool scan_complete(WiiNetwork* net,CPUState* cpu,s32* result){
    if(ioctlv(net,cpu,0x100a,{{0x6000,0x4e}},{{0x7000,0x100}},result,0x200,true)!=2)return false;
    cpu->cycles+=40ull*729000;
    return completion(net,cpu,0x200,7,result);
}
static std::vector<u8> words(std::initializer_list<u32> list){std::vector<u8> bytes(list.size()*4);unsigned n=0;for(const auto v:list){put(bytes.data()+n,v);n+=4;}return bytes;}
static void address(u8* out,const sockaddr_in& in){out[0]=8;out[1]=2;std::memcpy(out+2,&in.sin_port,2);std::memcpy(out+4,&in.sin_addr,4);}
int main(){
    std::array<u8,7004> profile{};CHECK(wii_network_seed_config(profile.data(),profile.size()));
    CHECK(profile[4]==1 && profile[8]==0x86 && !(profile[8]&0x21) && profile[8+2021]==6 && !std::memcmp(profile.data()+8+1988,"wiillr",6));
    const auto generated=profile;CHECK(!wii_network_seed_config(profile.data(),profile.size()) && profile==generated);
    CHECK(wii_network_host_profile_selected(profile.data(),profile.size()));
    profile[8+1988]='x';CHECK(!wii_network_host_profile_selected(profile.data(),profile.size()));
    profile[4]=2;profile[8]=0x87;CHECK(wii_network_host_profile_selected(profile.data(),profile.size()));
    profile[8]=7;CHECK(!wii_network_host_profile_selected(profile.data(),profile.size()));
    profile.fill(0);profile[8+2332+7]=0x55;const auto owner=profile;
    CHECK(!wii_network_seed_config(profile.data(),profile.size()) && profile==owner);
    CHECK(!wii_network_seed_config(profile.data(),7003));
    CPUState cpu{};std::vector<u8> ram(0x20000),mem2(0x10000);
    cpu.ram=ram.data();cpu.ram_size=u32(ram.size());cpu.mem2=mem2.data();cpu.mem2_size=u32(mem2.size());cpu.pc=0x80012340;
    const u8 mac[6]={2,0x11,0x22,0x33,0x44,0x55};WiiNetwork* net=wii_network_create(mac);CHECK(net);s32 result=0;
    CHECK(ioctl(net,&cpu,0xdead,{},0,0,&result)==0 && !writes);
    CHECK(ioctl(net,&cpu,15,words({2,2,0}),0x10010000,1,&result)==1 && result==-4 && !writes);
    CHECK(ioctl(net,&cpu,15,words({2,2,0}),0,0,&result)==1 && result==0);const u32 udp=u32(result);
    auto bind=words({udp,0,0,0});sockaddr_in guest{};guest.sin_family=AF_INET;guest.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address(bind.data()+8,guest);
    CHECK(ioctl(net,&cpu,2,bind,0,0,&result)==1 && result==0);
    std::memset(cpu.ram+0x2000,0xa5,16);writes=0;
    CHECK(ioctl(net,&cpu,7,words({udp}),0x2000,16,&result)==1 && result==0 && writes==8 && cpu.ram[0x2008]==0xa5);
    std::memcpy(&guest.sin_port,cpu.ram+0x2002,2);CHECK(guest.sin_port);
    HostSocket host=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);CHECK(host!=HostSocket(-1));
    sockaddr_in server{};server.sin_family=AF_INET;server.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    CHECK(::bind(host,reinterpret_cast<sockaddr*>(&server),sizeof server)==0);SockLen length=sizeof server;
    CHECK(getsockname(host,reinterpret_cast<sockaddr*>(&server),&length)==0);
    put(cpu.ram+0x5000,udp);put(cpu.ram+0x5004,0);std::memset(cpu.mem2+0x100,0xa5,16);writes=0;
    CHECK(ioctlv(net,&cpu,12,{{0x5000,8}},{{0xd0000100,16}},&result)==2 && !writes);
    /* Captured input must survive guest mutation after ACK. */
    put(cpu.ram+0x5000,99);CHECK(sendto(host,"test",4,0,reinterpret_cast<sockaddr*>(&guest),sizeof guest)==4);
    CHECK(completion(net,&cpu,0x200,7,&result) && result==4 && writes==4 && !std::memcmp(cpu.mem2+0x100,"test",4) && cpu.mem2[0x104]==0xa5);
    CHECK(ioctl(net,&cpu,5,words({udp,4,4}),0,0,&result)==1 && !result);
    put(cpu.ram+0x5000,udp);CHECK(ioctlv(net,&cpu,12,{{0x5000,8}},{{0x2000,16}},&result)==1 && result==-6);
    std::memcpy(cpu.ram+0x8000,"reply",5);put(cpu.ram+0x5004,0);put(cpu.ram+0x5008,1);address(cpu.ram+0x500c,server);
    CHECK(ioctlv(net,&cpu,13,{{0x8000,5},{0x5000,20}},{},&result)==1 && result==5);
    char data[16]{};CHECK(recv(host,data,sizeof data,0)==5 && !std::memcmp(data,"reply",5));
    /* Malformed vectors and non-RAM endpoints fail before touching a socket. */
    writes=0;CHECK(ioctlv(net,&cpu,12,{{0x5000,8}},{{0xcc000000,16}},&result)==1 && result==-4 && !writes);
    CHECK(ioctl(net,&cpu,5,words({udp,4,0}),0,0,&result)==1 && !result);
    CHECK(ioctlv(net,&cpu,12,{{0x5000,8}},{{0x2000,16}},&result)==2);wii_network_close_device(net,12);
    CHECK(completion(net,&cpu,0x200,7,&result) && result==-11);
    CHECK(ioctlv(net,&cpu,12,{{0x5000,8}},{{0x2000,16}},&result)==2);
    CHECK(ioctl(net,&cpu,3,words({udp}),0,0,&result,0x240)==1 && !result);
    CHECK(completion(net,&cpu,0x200,7,&result) && result==-11);
    CHECK(ioctl(net,&cpu,3,words({udp}),0,0,&result)==1 && result==-8);close_host(host);
    /* Real TCP connection and payload through the same Wii IPC layouts. */
    HostSocket listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(listener!=HostSocket(-1));server.sin_port=0;
    CHECK(::bind(listener,reinterpret_cast<sockaddr*>(&server),sizeof server)==0 && listen(listener,1)==0);length=sizeof server;
    CHECK(getsockname(listener,reinterpret_cast<sockaddr*>(&server),&length)==0);
    CHECK(ioctl(net,&cpu,15,words({2,1,0}),0,0,&result)==1 && result==0);const u32 tcp=u32(result);
    auto connect=words({tcp,0,0,0});address(connect.data()+8,server);const int state=ioctl(net,&cpu,4,connect,0,0,&result);
    CHECK((state==1 && !result) || (state==2 && completion(net,&cpu,0x200,6,&result) && !result));
    HostSocket accepted=accept(listener,nullptr,nullptr);CHECK(accepted!=HostSocket(-1));
    put(cpu.ram+0x5000,tcp);put(cpu.ram+0x5004,0);put(cpu.ram+0x5008,0);
    CHECK(ioctlv(net,&cpu,13,{{0x8000,5},{0x5000,12}},{},&result)==1 && result==5);
    CHECK(recv(accepted,data,sizeof data,0)==5 && !std::memcmp(data,"reply",5));close_host(accepted);close_host(listener);
    CHECK(ioctl(net,&cpu,3,words({tcp}),0,0,&result)==1 && !result);
    /* Host resolver executes asynchronously and returns guest pointers, never
     * host pointers. It does not need or disclose a real network hostname. */
    const char name[]="localhost";std::vector<u8> input(name,name+sizeof name);
    CHECK(ioctl(net,&cpu,17,input,0x2000,0x460,&result)==2);
    CHECK(completion(net,&cpu,0x200,6,&result) && !result && get(cpu.ram+0x2000)==0x2010 && get(cpu.ram+0x200c)==0x2340);
    CHECK(get(cpu.ram+0x2110)==0x7f000001 && get(cpu.ram+0x2340)==0x2110 && !get(cpu.ram+0x2344));
    put(cpu.ram+0x5000,0xfffe);put(cpu.ram+0x5004,0x1004);writes=0;
    CHECK(ioctlv(net,&cpu,28,{{0x5000,8}},{{0x2000,8},{0x3000,4}},&result)==1 && !result && writes==10 && !std::memcmp(cpu.ram+0x2000,mac,6));
    CHECK(ioctlv(net,&cpu,27,{}, {},&result)==1 && !result);
    CHECK(wii_network_wireless_open(nullptr,12,0x10003)==-6);
    const bool host_available=wii_network_link(net);
    std::printf("virtual WD host_interface_available=%d\n",int(host_available));
    CHECK(wii_network_wireless_open(net,12,0x10003)==(host_available?0:-6));
    if(host_available){
        CHECK(wii_network_wireless_open(net,13,0x10003)==s32(0x80008002u));
        CHECK(ioctlv(net,&cpu,0x1001,{}, {},&result,0x200,true)==1 && result==3);
        std::memset(cpu.mem2+0x1000,0xa5,0xa0);writes=0;
        CHECK(ioctlv(net,&cpu,0x100e,{},{{0xd0001000,0xa0}},&result,0x200,true)==1 && !result && writes==0x90);
        CHECK(!std::memcmp(cpu.mem2+0x1000,mac,6) && cpu.mem2[0x1007]==2 && cpu.mem2[0x100f]==1 && cpu.mem2[0x1090]==0xa5);
        CHECK(ioctlv(net,&cpu,0x100e,{},{{0x7000,0x8f}},&result,0x200,true)==1 && result==s32(0x80008001u));
        /* Actual SDK ScanInfo(0x4e) and WDBssDesc fields/halfword length. */
        std::memset(cpu.ram+0x6000,0,0x4e);cpu.ram[0x6001]=2;cpu.ram[0x6003]=40;
        std::memset(cpu.ram+0x6004,0xff,6);std::memset(cpu.ram+0x602e,0xff,32);
        std::memset(cpu.ram+0x7000,0xa5,0x100);writes=0;
        const u64 started=cpu.cycles;
        CHECK(ioctlv(net,&cpu,0x100a,{{0x6000,0x4e}},{{0x7000,0x100}},&result,0x200,true)==2 && !writes);
        CHECK(cpu.ram[0x7000]==0xa5 && cpu.ram[0x7041]==0xa5);
        cpu.ram[0x600d]=33; /* queued scan must own the pre-ACK input */
        cpu.cycles=started+40ull*729000-1;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));u32 message=0,command=0;
        CHECK(!wii_network_poll(net,&cpu,&message,&command,&result) && !writes && cpu.ram[0x7000]==0xa5);
        ++cpu.cycles;
        CHECK(completion(net,&cpu,0x200,7,&result) && !result && writes==0x42);
        CHECK(!wii_network_poll(net,&cpu,&message,&command,&result)); /* exactly once */
        cpu.ram[0x600d]=0;
        CHECK(cpu.ram[0x7001]==1 && cpu.ram[0x7003]==0x20 && cpu.ram[0x700d]==6 && !std::memcmp(cpu.ram+0x700e,"wiillr",6));
        CHECK(cpu.ram[0x7038]==0 && cpu.ram[0x7039]==1 && cpu.ram[0x7042]==0xa5);
        const std::array<u8,6> bssid{cpu.ram[0x7006],cpu.ram[0x7007],cpu.ram[0x7008],cpu.ram[0x7009],cpu.ram[0x700a],cpu.ram[0x700b]};
        CHECK((bssid[0]&3)==2 && std::memcmp(bssid.data(),mac,6));
        cpu.ram[0x600d]=6;std::memcpy(cpu.ram+0x600e,"other!",6);writes=0;
        CHECK(scan_complete(net,&cpu,&result) && !result && writes==2 && !cpu.ram[0x7001]);
        std::memcpy(cpu.ram+0x600e,"wiillr",6);std::memcpy(cpu.ram+0x6004,bssid.data(),6);
        CHECK(scan_complete(net,&cpu,&result) && !result && cpu.ram[0x7001]==1);
        cpu.ram[0x6009]^=1;CHECK(scan_complete(net,&cpu,&result) && !result && !cpu.ram[0x7001]);
        std::memset(cpu.ram+0x6004,0xff,6);cpu.ram[0x6001]=4;
        CHECK(ioctlv(net,&cpu,0x100a,{{0x6000,0x4e}},{{0x7000,0x100}},&result,0x200,true)==1 && !result && !cpu.ram[0x7001]);
        cpu.ram[0x6001]=2;writes=0;
        CHECK(ioctlv(net,&cpu,0x100a,{{0x6000,0x4e}},{{0x7000,0x40}},&result,0x200,true)==1 && result==s32(0x80008001u) && !writes);
        cpu.ram[0x600d]=33;CHECK(ioctlv(net,&cpu,0x100a,{{0x6000,0x4e}},{{0x7000,0x100}},&result,0x200,true)==1 && result==s32(0x80008001u));
        CHECK(ioctlv(net,&cpu,0x100c,{}, {},&result,0x200,true)==1 && result==s32(0x80008002u)); /* no AOSS pairing */
        put(cpu.ram+0x5000,1);CHECK(ioctlv(net,&cpu,0x1002,{{0x5000,4}}, {},&result,0x200,true)==1 && !result);
        CHECK(ioctlv(net,&cpu,0x1003,{}, {},&result,0x200,true)==1 && result==1);
        put(cpu.ram+0x5000,0);CHECK(ioctlv(net,&cpu,0x1002,{{0x5000,4}}, {},&result,0x200,true)==1 && !result);
        CHECK(ioctlv(net,&cpu,0x1003,{}, {},&result,0x200,true)==1 && !result);
        std::memset(cpu.ram+0x7000,0xa5,32);writes=0;
        CHECK(ioctlv(net,&cpu,0x8001,{},{{0x7000,32}},&result,0x200,true)==2 && !writes);
        CHECK(wii_network_wireless_close(net,13)==s32(0x80008000u));
        CHECK(ioctlv(net,&cpu,27,{}, {},&result,0x240)==1 && !result); /* IP cleanup leaves WD request pending */
        CHECK(wii_network_wireless_close(net,12)==0);
        CHECK(completion(net,&cpu,0x200,7,&result) && result==s32(0x80008000u) && !writes && cpu.ram[0x7000]==0xa5);
        CHECK(wii_network_wireless_close(net,12)==s32(0x80008000u));
        CHECK(wii_network_wireless_open(net,12,0x10003)==0);
        std::memset(cpu.ram+0x6000,0,0x4e);cpu.ram[0x6001]=2;cpu.ram[0x6002]=1;cpu.ram[0x6003]=44; /* 300ms */
        std::memset(cpu.ram+0x6004,0xff,6);std::memset(cpu.ram+0x602e,0xff,32);
        std::memset(cpu.ram+0x7000,0xa5,0x100);writes=0;
        CHECK(ioctlv(net,&cpu,0x100a,{{0x6000,0x4e}},{{0x7000,0x100}},&result,0x200,true)==2 && !writes);
        CHECK(wii_network_wireless_close(net,12)==0);
        CHECK(completion(net,&cpu,0x200,7,&result) && result==s32(0x80008000u) && !writes && cpu.ram[0x7000]==0xa5);
        CHECK(wii_network_wireless_open(net,12,0x10003)==0);
        CHECK(ioctlv(net,&cpu,0x100a,{{0x6000,0x4e}},{{0x7000,0x100}},&result,0x200,true)==2 && !writes);
        wii_network_destroy(net);net=wii_network_create(mac);CHECK(net);
        cpu.cycles+=300ull*729000;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        CHECK(!wii_network_poll(net,&cpu,&message,&command,&result) && !writes && cpu.ram[0x7000]==0xa5);
        CHECK(wii_network_wireless_open(net,12,0x10003)==0);
        cpu.ram[0x6002]=0;cpu.ram[0x6003]=40;
        CHECK(scan_complete(net,&cpu,&result) && !result && cpu.ram[0x7001]==1);
        CHECK(wii_network_wireless_close(net,12)==0);
    }
    wii_network_destroy(net);std::puts("host network: profile preservation, UDP/TCP, pending/cancel, guest buffers, resolver and virtual WD PASS");return 0;
}
