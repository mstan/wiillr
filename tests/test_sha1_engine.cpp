/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/sha1_engine.hpp"
#include <vector>
#include <string_view>
#include <cstdio>
#include <algorithm>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static bool faults(auto&& f) {try {f();return false;}catch(const std::runtime_error&){return true;}}
static constexpr std::array<uint32_t,5> initial{0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
static std::vector<uint8_t> padded(std::string_view s) {
    std::vector<uint8_t> bytes((s.size()+72)&~size_t(63));std::copy(s.begin(),s.end(),bytes.begin());bytes[s.size()]=0x80;
    auto bits=uint64_t(s.size())*8;for(unsigned i=0;i<8;++i){bytes[bytes.size()-1-i]=uint8_t(bits);bits>>=8;}return bytes;
}
int main() {
    using Sha=wiillr::Sha1Engine;std::vector<uint8_t> memory(4096);
    Sha::Bus bus{&memory,[](void* user,uint32_t at,uint32_t size)->const uint8_t* {
        auto& m=*static_cast<std::vector<uint8_t>*>(user);
        return at<=m.size() && size<=m.size()-at?m.data()+at:nullptr;
    }};
    Sha sha;auto reg=[&](uint32_t at){uint32_t value=0;if(!sha.read(at,value))throw std::runtime_error("Invalid test register");return value;};
    auto seed=[&]{for(unsigned i=0;i<5;++i)sha.write(8+i*4,initial[i],0,bus);};
    auto state=[&]{std::array<uint32_t,5> h{};for(unsigned i=0;i<5;++i)h[i]=reg(8+i*4);return h;};
    CHECK(reg(0)==0 && reg(4)==0 && state()==(std::array<uint32_t,5>{}));
    uint32_t unused;CHECK(!sha.read(0x1c,unused) && !sha.write(2,0,0,bus));
    struct Vector {std::string_view message;std::array<uint32_t,5> hash;};
    // Independent FIPS 180-1 / standard SHA-1 message digests; hardware never pads.
    const Vector vectors[]={
        {"",{0xda39a3ee,0x5e6b4b0d,0x3255bfef,0x95601890,0xafd80709}},
        {"abc",{0xa9993e36,0x4706816a,0xba3e2571,0x7850c26c,0x9cd0d89d}},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",{0x84983e44,0x1c3bd26e,0xbaae4aa1,0xf95129e5,0xe54670f1}}
    };
    for(const auto& v:vectors) {
        const auto data=padded(v.message);std::copy(data.begin(),data.end(),memory.begin()+64);const auto original=memory;
        seed();sha.write(4,64,0,bus);sha.write(0,0xc0000000u|uint32_t(data.size()/64-1),100,bus);
        CHECK(reg(0)==0xc0000000u && state()==initial && reg(4)==64);
        CHECK(faults([&]{sha.write(4,128,100,bus);}) && faults([&]{sha.write(8,0,100,bus);}));
        CHECK(faults([&]{sha.write(0,0x80000000,100,bus);}));
        CHECK(!sha.advance(100+Sha::block_cycles-1,bus) && state()==initial && reg(4)==64);
        if(data.size()==128) {
            CHECK(!sha.advance(100+Sha::block_cycles,bus));CHECK(reg(4)==128 && state()!=initial && (reg(0)&0x80000000u));
        }
        CHECK(sha.advance(100+Sha::block_cycles*(data.size()/64),bus));
        CHECK(state()==v.hash && reg(4)==64+data.size() && reg(0)==0x40000000u);
        CHECK(!sha.advance(10000,bus) && memory==original);
        // Seed is preserved across commands, allowing split-block chaining.
        if(data.size()==128) {
            seed();sha.write(4,64,0,bus);sha.write(0,0x80000000,0,bus);
            CHECK(!sha.advance(Sha::block_cycles,bus) && reg(0)==0 && reg(4)==128);
            sha.write(0,0x80000000,Sha::block_cycles,bus);
            CHECK(!sha.advance(2*Sha::block_cycles,bus) && state()==v.hash && reg(4)==192);
        }
    }
    seed();sha.write(4,64,0,bus);sha.write(0,0xc0000000,0,bus);
    sha.write(0,0,1,bus);CHECK(reg(0)==0 && state()==initial && reg(4)==64);
    CHECK(!sha.advance(1000,bus) && state()==initial && reg(4)==64); // Reset aborts pending work/IRQ.
    CHECK(faults([&]{sha.write(4,65,0,bus);}) && reg(4)==64);
    CHECK(faults([&]{sha.write(0,0x80000400,0,bus);}) && reg(0)==0); // Only ten count bits.
    CHECK(faults([&]{sha.write(0,0xa0000000,0,bus);}) && reg(0)==0); // Unsupported error/mode write.
    sha.write(4,4096-64,0,bus);
    CHECK(faults([&]{sha.write(0,0xc0000001,0,bus);}) && state()==initial && reg(0)==0);
    sha.write(4,0xffffffc0,0,bus);
    CHECK(faults([&]{sha.write(0,0x80000001,0,bus);}) && state()==initial && reg(0)==0);
    sha.write(4,64,0,bus);
    CHECK(faults([&]{sha.write(0,0x80000000,~uint64_t(0)-10,bus);}) && reg(0)==0);
    sha.write(0,0xc0000000,0,bus);const auto saved=memory;memory.clear();
    CHECK(faults([&]{sha.advance(Sha::block_cycles,bus);}) && state()==initial && reg(4)==64 && reg(0)==0xc0000000u);
    sha.write(0,0,100,bus);memory=saved;CHECK(!sha.advance(10000,bus));
    return 0;
}
