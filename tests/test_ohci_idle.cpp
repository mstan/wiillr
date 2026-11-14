/* SPDX-License-Identifier: MIT */
#include "wiillr/ohci_init.hpp"
#include <cstdio>
#include <algorithm>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static bool faults(auto&& f){try {f();return false;}catch(const std::runtime_error&){return true;}}
struct Memory {std::array<uint8_t,1024> bytes;bool mapped=true;uint64_t stores=0;uint32_t last=0,size=0;};
int main() {
    Memory memory;memory.bytes.fill(0x5a);
    using Ohci=wiillr::OhciInit;
    Ohci::Bus bus{&memory,[](void* p,uint32_t at,uint32_t size)->uint8_t* {
        auto& m=*static_cast<Memory*>(p);return m.mapped && at<=m.bytes.size() && size<=m.bytes.size()-at?m.bytes.data()+at:nullptr;
    },[](void* p,uint32_t at,uint32_t size){auto& m=*static_cast<Memory*>(p);++m.stores;m.last=at;m.size=size;}};
    Ohci hc;uint64_t now=10;
    auto read=[&](uint32_t at){uint32_t v=0;if(!hc.read(at,now,v,bus))throw std::runtime_error("Invalid register");return v;};
    auto write=[&](uint32_t at,uint32_t v){return hc.write(at,v,now,bus);};
    CHECK(read(0x34)==0x2edf && read(0x3c)==0);
    write(0x18,0x400);CHECK(faults([&]{write(4,0x283);}) && read(4)==0 && !memory.stores);
    write(0x18,0x1ff);CHECK(read(0x18)==0x100); // HCCA low8 bits are reserved.
    const auto original=memory.bytes;write(0x10,0x80000004);write(4,0x283);
    CHECK(read(0x38)==11999 && read(4)==0x283);
    now+=242999;CHECK(!hc.interrupt(now,bus) && read(0x3c)==0 && memory.bytes==original);
    ++now;CHECK(hc.interrupt(now,bus) && read(0x3c)==1 && memory.stores==1);
    CHECK(memory.last==0x180 && memory.size==4 && memory.bytes[0x180]==1 && !memory.bytes[0x181] && !memory.bytes[0x182] && !memory.bytes[0x183]);
    for(unsigned i=0;i<memory.bytes.size();++i)if(i<0x180 || i>=0x184)CHECK(memory.bytes[i]==original[i]);
    CHECK(faults([&]{write(4,0x287);}) && read(4)==0x283); // No periodic traversal.
    CHECK(faults([&]{write(8,2);})); // No fabricated control/bulk completion.
    CHECK(faults([&]{write(0x18,0xffffff00);}) && read(0x18)==0x100);
    write(0x0c,4);write(0x14,4);now+=243000;
    CHECK(!hc.interrupt(now,bus) && (read(0x0c)&4) && read(0x3c)==2);
    write(0x10,4);CHECK(hc.interrupt(now,bus));write(0x0c,4);CHECK(!hc.interrupt(now,bus));
    write(4,0xc0);const auto before_suspend=memory.stores;now+=2430000;
    CHECK(!hc.interrupt(now,bus) && read(0x3c)==2 && memory.stores==before_suspend);
    write(4,0x40);now+=4860000;CHECK(!hc.interrupt(now,bus) && read(0x3c)==2);
    write(4,0x283);now+=243000;CHECK(hc.interrupt(now,bus) && read(0x3c)==3);
    write(0x0c,4);write(0x34,0x80000007);now+=162;
    CHECK(!hc.interrupt(now,bus) && read(0x3c)==3); // New interval loads at next SOF.
    now+=243000-162;CHECK(hc.interrupt(now,bus) && read(0x3c)==4 && read(0x38)==0x80000007u);
    write(0x0c,4);now+=161;CHECK(!hc.interrupt(now,bus));++now;CHECK(hc.interrupt(now,bus) && read(0x3c)==5);
    // OpenHCI6.5.6: FNO occurs on BOTH bit15 transitions, not every SOF.
    Ohci overflow;auto ticks=[](uint64_t frames){return (frames*81+3)/4;};uint32_t value;
    overflow.write(0x18,0x100,0,bus);overflow.write(0x34,0,0,bus);overflow.write(0x10,0x80000020,0,bus);overflow.write(4,0x283,0,bus);
    CHECK(!overflow.interrupt(ticks(32767),bus));overflow.read(0x3c,ticks(32767),value,bus);CHECK(value==0x7fff);
    CHECK(overflow.interrupt(ticks(32768),bus));overflow.read(0x3c,ticks(32768),value,bus);CHECK(value==0x8000);
    overflow.write(0x0c,0x20,ticks(32768),bus);CHECK(!overflow.interrupt(ticks(65535),bus));
    CHECK(overflow.interrupt(ticks(65536),bus));overflow.read(0x3c,ticks(65536),value,bus);CHECK(value==0);
    const auto reset_at=ticks(65536);overflow.write(0x0c,0x7f,reset_at,bus);overflow.write(8,1,reset_at,bus);
    const auto stores=memory.stores;CHECK(!overflow.interrupt(reset_at+2429,bus) && memory.stores==stores);
    overflow.read(8,reset_at+2429,value,bus);CHECK(value==1);overflow.read(8,reset_at+2430,value,bus);CHECK(value==0);
    overflow.read(0x3c,reset_at+2430,value,bus);CHECK(!value);overflow.read(0x34,reset_at+2430,value,bus);CHECK(value==0x2edf);
    overflow.read(4,reset_at+2430,value,bus);CHECK(value==0xc0 && memory.stores==stores);
    Ohci invalid;invalid.write(0x18,0x100,0,bus);invalid.write(4,0x283,0,bus);memory.mapped=false;
    CHECK(faults([&]{invalid.interrupt(243000,bus);}) && memory.stores==stores);
    invalid.read(0x3c,0,value,bus);CHECK(value==0);invalid.read(0x0c,0,value,bus);CHECK(value==0);
    memory.mapped=true;invalid.write(8,1,0,bus);CHECK(!invalid.interrupt(300000,bus) && memory.stores==stores);
    return 0;
}
