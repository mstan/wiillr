/* SPDX-License-Identifier: MIT */
#include "wiillr/nand_controller.hpp"
#include <cstdio>
#include <vector>
#include <algorithm>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
static bool faults(auto&& f) {try {f();return false;}catch(const std::runtime_error&) {return true;}}
struct Memory {
    std::array<uint8_t,0x4000> bytes{};
    std::vector<std::pair<uint32_t,uint32_t>> stores;
    wiillr::NandController::Bus bus() {
        return {this,[](void* u,uint32_t at,uint32_t size)->uint8_t* {
            auto& m=*static_cast<Memory*>(u);
            return at<=m.bytes.size() && size<=m.bytes.size()-at?m.bytes.data()+at:nullptr;
        },[](void* u,uint32_t at,uint32_t size) {static_cast<Memory*>(u)->stores.emplace_back(at,size);}};
    }
};
int main() {
    using Nand=wiillr::NandController;
    Nand nand;Memory memory;const auto bus=memory.bus();uint64_t now=0;uint32_t value=0;
    CHECK(nand.read(4,value) && value==0);
    CHECK(faults([&]{nand.write(0,0x80000000,now,bus);}));
    std::vector<uint8_t> original(Nand::page_size*128,0xff);
    for(unsigned i=0;i<Nand::page_size;++i)original[5*Nand::page_size+i]=uint8_t(i^(i>>5));
    nand.load(original);
    auto reg=[&](unsigned at,uint32_t v) {return nand.write(at,v,now,bus);};
    auto execute=[&](uint32_t command,uint64_t delay) {
        reg(0,0x7fffffff);reg(0,0);reg(0,command);
        uint32_t status=0;nand.read(0,status);
        if(!(status&0x80000000) || nand.advance(now+delay-1,bus))throw std::runtime_error("Early NAND completion");
        now+=delay;return nand.advance(now,bus);
    };
    reg(4,0x4b3e0e7f);CHECK(nand.read(4,value) && value==0x4b3e0e7f);
    reg(8,0);reg(12,5);CHECK(!execute(0x9f000000,243));
    reg(16,0x1000);reg(20,0x2000);reg(0,0xc030b840);
    reg(16,0x3000); /* Submission latches DMA pointers. */
    CHECK(!nand.advance(now+6074,bus));now+=6075;CHECK(nand.advance(now,bus));
    CHECK(!nand.advance(now,bus));
    CHECK(std::equal(memory.bytes.begin()+0x1000,memory.bytes.begin()+0x1800,original.begin()+5*Nand::page_size));
    CHECK(std::equal(memory.bytes.begin()+0x2000,memory.bytes.begin()+0x2040,original.begin()+5*Nand::page_size+0x800));
    CHECK(memory.stores.size()==2 && memory.stores[0]==std::make_pair(0x1000u,0x800u) && memory.stores[1]==std::make_pair(0x2000u,0x50u));
    std::array<uint8_t,512> zero{};CHECK(Nand::ecc(zero.data())==0);
    zero[3]=4;CHECK(Nand::ecc(zero.data())==0xe50f1a00);
    reg(16,0x1000);std::fill_n(memory.bytes.data()+0x1000,0x800,0x0f);std::fill_n(memory.bytes.data()+0x2000,0x40,0xff);
    reg(12,6);CHECK(!execute(0x9f805840,243));CHECK(execute(0xc0108000,48600));
    CHECK(nand.page_bytes(6)[0]==0x0f && original[6*Nand::page_size]==0xff);
    std::fill_n(memory.bytes.data()+0x1000,0x800,0xf0);
    CHECK(!execute(0x9f805840,243));CHECK(execute(0xc0108000,48600));CHECK(nand.page_bytes(6)[0]==0);
    /* RESET discards both staged command types without reverting COW media. */
    CHECK(!execute(0x9f805840,243));CHECK(!execute(0x80ff8000,24300));
    CHECK(faults([&]{reg(0,0xc0108000);}));CHECK(nand.page_bytes(6)[0]==0);
    CHECK(!execute(0x9c600000,243));CHECK(!execute(0x80ff8000,24300));
    CHECK(faults([&]{reg(0,0xc0d08000);}));
    CHECK(nand.page_bytes(6)[0]==0 && nand.page_bytes(5)[1]==1);
    /* Reset can also abort an actual busy program before its commit/IRQ. */
    reg(12,7);CHECK(!execute(0x9f805840,243));reg(0,0xc0108000);
    reg(0,0x80ff8000);now+=24300;CHECK(!nand.advance(now,bus));
    now+=48600;CHECK(!nand.advance(now,bus) && nand.page_bytes(7)[0]==0xff);
    reg(12,6);
    CHECK(!execute(0x9c600000,243));CHECK(execute(0xc0d08000,486000));
    CHECK(std::all_of(nand.page_bytes(5),nand.page_bytes(5)+Nand::page_size,[](auto b){return b==0xff;}));
    CHECK(original[5*Nand::page_size+1]==1); /* Backing input was never programmed/erased. */
    nand.load(original);now=0;memory.bytes.fill(0x6d);memory.stores.clear();
    reg(8,0);reg(12,5);CHECK(!execute(0x9f000000,243));reg(16,0x1000);reg(20,0xffff);reg(0,0xc030b840);
    const auto before=memory.bytes;now+=6075;CHECK(faults([&]{nand.advance(now,bus);}));
    CHECK(memory.bytes==before && memory.stores.empty());
    return 0;
}
