/* SPDX-License-Identifier: MIT */
#include "wiillr/aes_engine.hpp"
#include <cstdio>
#include <string_view>
#if defined(CBC) || defined(CTR) || defined(ECB)
#error Upstream AES configuration must remain private to the implementation.
#endif
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static bool faults(auto&& f){try{f();return false;}catch(const std::runtime_error&){return true;}}
static std::vector<uint8_t> hex(std::string_view s) {
    auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};std::vector<uint8_t> bytes;
    for(size_t i=0;i<s.size();i+=2)bytes.push_back(uint8_t(nibble(s[i])*16+nibble(s[i+1])));return bytes;
}
struct Fixture {
    wiillr::AesEngine engine;std::vector<uint8_t> ram=std::vector<uint8_t>(0x22000,0x55);
    unsigned stores=0;bool ordered=true;
    wiillr::AesEngine::Bus bus() {
        return {this,[](void* p,uint32_t at,uint32_t size)->uint8_t* {
            auto& f=*static_cast<Fixture*>(p);return uint64_t(at)+size<=f.ram.size()?f.ram.data()+at:nullptr;
        },[](void* p,uint32_t,uint32_t size) {
            auto& f=*static_cast<Fixture*>(p);uint32_t control=0;f.engine.read(0,control);
            f.ordered&=size==16 && (control&0x80000000u);++f.stores;
        }};
    }
    void put(uint32_t at,uint32_t value,uint64_t now=0){if(!engine.write(at,value,now,bus()))throw std::runtime_error("Bad fixture register");}
    uint32_t get(uint32_t at){uint32_t value=0;if(!engine.read(at,value))throw std::runtime_error("Bad fixture read");return value;}
    void fifo(uint32_t at,const std::vector<uint8_t>& bytes,bool prime=true) {
        auto word=[&](unsigned i){return uint32_t(bytes[i])<<24|uint32_t(bytes[i+1])<<16|uint32_t(bytes[i+2])<<8|bytes[i+3];};
        if(prime)put(at,word(0)); // Actual IOS primes once, then writes all four words.
        for(unsigned i=0;i<16;i+=4)put(at,word(i));
    }
    void pointers(uint32_t source,uint32_t destination){put(4,source);put(8,destination);}
    bool advance(uint64_t cycles){return engine.advance(cycles,bus());}
    bool equal(uint32_t at,const std::vector<uint8_t>& bytes)const{return std::equal(bytes.begin(),bytes.end(),ram.begin()+at);}
};
int main() {
    // Independent FIPS197 (2001), Appendix C.1; not an encrypt/decrypt round trip.
    const auto fips_key=hex("000102030405060708090a0b0c0d0e0f"),fips_plain=hex("00112233445566778899aabbccddeeff"),fips_cipher=hex("69c4e0d86a7b0430d8cdb78070b4c55a");
    std::array<uint8_t,16> key{},block{};std::copy(fips_key.begin(),fips_key.end(),key.begin());std::copy(fips_plain.begin(),fips_plain.end(),block.begin());
    const auto expanded=wiillr::aes128_expand(key);wiillr::aes128_encrypt(expanded,block);CHECK(std::equal(block.begin(),block.end(),fips_cipher.begin()));
    std::copy(fips_cipher.begin(),fips_cipher.end(),block.begin());wiillr::aes128_decrypt(expanded,block);CHECK(std::equal(block.begin(),block.end(),fips_plain.begin()));
    // NIST SP800-38A F.2.1/F.2.2 AES128 CBC, four independently specified blocks.
    const auto cbc_key=hex("2b7e151628aed2a6abf7158809cf4f3c"),iv=hex("000102030405060708090a0b0c0d0e0f");
    const auto plain=hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
    const auto cipher=hex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b273bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7");
    const auto cycles=wiillr::AesEngine::block_cycles;
    {
        Fixture f;f.fifo(12,cbc_key);f.fifo(16,iv);std::copy(plain.begin(),plain.end(),f.ram.begin()+0x100);f.pointers(0x100,0x500);
        f.put(0,0xd0000003);CHECK(f.get(0)==0xd0000000 && !f.stores);
        CHECK(!f.advance(cycles-1) && f.ram[0x500]==0x55);CHECK(!f.advance(cycles) && f.stores==1 && f.get(4)==0x110 && f.get(8)==0x510);
        CHECK(std::equal(cipher.begin(),cipher.begin()+16,f.ram.begin()+0x500) && f.ram[0x510]==0x55);
        CHECK(f.advance(cycles*4) && f.stores==4 && f.ordered && f.equal(0x500,cipher));
        CHECK(f.get(0)==0x50000000 && f.get(4)==0x140 && f.get(8)==0x540 && !f.advance(cycles*5));
        CHECK(f.ram[0x4ff]==0x55 && f.ram[0x540]==0x55);
        uint32_t hidden=0x12345678;CHECK(!f.engine.read(12,hidden) && !f.engine.read(16,hidden) && hidden==0x12345678);
    }
    {
        Fixture f;f.fifo(12,cbc_key);f.fifo(16,iv);std::copy(cipher.begin(),cipher.end(),f.ram.begin()+0x100);f.pointers(0x100,0x100);
        f.put(0,0xd8000003);CHECK(f.advance(cycles*4) && f.equal(0x100,plain) && f.ordered); // Original ciphertext, not overwritten plaintext, carries forward.
    }
    for(bool decrypt:{false,true}) {
        Fixture f;f.fifo(12,cbc_key);f.fifo(16,iv);const auto& input=decrypt?cipher:plain;const auto& output=decrypt?plain:cipher;
        std::copy(input.begin(),input.end(),f.ram.begin()+0x100);f.pointers(0x100,decrypt?0x100:0x500);
        const uint32_t mode=decrypt?0xd8000000:0xd0000000;
        f.put(0,mode|1);CHECK(f.advance(cycles*2));
        f.fifo(16,std::vector<uint8_t>(16,0)); // Continued mode must ignore the newly supplied IV.
        f.put(0,mode|0x1001,cycles*2);CHECK(f.advance(cycles*4));CHECK(f.equal(decrypt?0x100:0x500,output));
    }
    {
        Fixture f;f.fifo(12,cbc_key);f.fifo(16,iv);std::copy(plain.begin(),plain.end(),f.ram.begin()+0x100);f.pointers(0x100,0x500);f.put(0,0xd0000003);
        for(uint32_t at:{0u,4u,8u,12u,16u})CHECK(faults([&]{f.put(at,at?0:0xd0000000);}));
        std::fill_n(f.ram.begin()+0x100,64,0); // Command source image is latched before asynchronous publication.
        CHECK(f.advance(cycles*4) && f.equal(0x500,cipher));
    }
    {
        Fixture f;f.fifo(12,cbc_key);f.fifo(16,iv);std::copy(plain.begin(),plain.end(),f.ram.begin()+0x100);f.pointers(0x100,0x500);f.put(0,0xd0000003);
        f.put(0,0,cycles-1);CHECK(!f.advance(cycles*8) && f.stores==0 && f.get(0)==0 && f.ram[0x500]==0x55);
        CHECK(faults([&]{f.put(0,0xd0001000,cycles*8);}));
        f.put(0,0xd0000003,cycles*8);CHECK(!f.advance(cycles*9) && f.stores==1);f.put(0,0,cycles*9);
        CHECK(!f.advance(cycles*20) && f.stores==1 && f.ram[0x510]==0x55); // No late block or IRQ after reset.
    }
    {
        Fixture f;for(unsigned i=0;i<65536;++i)f.ram[0x1000+i]=uint8_t(i*13);f.pointers(0x1000,0x11000);
        f.put(0,0xc0000fff);CHECK(f.advance(cycles*4096) && f.stores==4096 && f.ordered);
        CHECK(std::equal(f.ram.begin()+0x1000,f.ram.begin()+0x11000,f.ram.begin()+0x11000));
        CHECK(f.get(4)==0x11000 && f.get(8)==0x21000 && f.ram[0x21000]==0x55); // Full12-bit count; copy needs no key/IV.
    }
    {
        Fixture f;f.pointers(0x21ff0,0x500);CHECK(faults([&]{f.put(0,0xc0000001);}));CHECK(!f.stores && f.get(0)==0 && f.ram[0x500]==0x55);
        f.pointers(0x100,0x21ff0);CHECK(faults([&]{f.put(0,0xc0000001);}));CHECK(!f.stores && f.ram[0x21ff0]==0x55);
        f.pointers(0x100,0x110);CHECK(faults([&]{f.put(0,0xc0000003);}));CHECK(!f.stores);
        CHECK(faults([&]{f.put(4,0x101);}) && f.get(4)==0x100);CHECK(faults([&]{f.put(8,0x111);}) && f.get(8)==0x110);
        f.pointers(0x100,0x500);CHECK(faults([&]{f.put(0,0xe0000000);}));CHECK(faults([&]{f.put(0,0x10000000);}));
        CHECK(faults([&]{f.put(0,0xd0000000);}));f.fifo(12,cbc_key);CHECK(faults([&]{f.put(0,0xd0000000);}));
        f.fifo(16,iv);CHECK(faults([&]{f.put(0,0xd0001000);}));CHECK(faults([&]{f.put(0,0xc0001000);}));
        CHECK(faults([&]{f.put(0,0xd0000000,std::numeric_limits<uint64_t>::max()-10);}));CHECK(!f.stores && f.get(0)==0);
    }
    return 0;
}
