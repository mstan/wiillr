/* SPDX-License-Identifier: MIT */
#pragma once
#include "wiillr/sha1.hpp"
#include <atomic>
#include <stdexcept>
#include <limits>
namespace wiillr {
/* https://wiibrew.org/wiki/Hardware/SHA-1_engine
 * Only raw SHA-1 block DMA is modeled. The deterministic timing profile uses
 * one ARM bus cycle per compression round; physical throughput is unqualified.
 * Hash/source state publishes per completed block, then IRQ on final block. */
class Sha1Engine {
public:
    static constexpr uint64_t block_cycles=80;
    struct Bus {void* user;const uint8_t* (*map)(void*,uint32_t,uint32_t);};
    bool read(uint32_t at,uint32_t& value) const {
        if((at&3) || at>0x18)return false;
        value=at==0?control_:at==4?source_:hash_[(at-8)/4];return true;
    }
    bool write(uint32_t at,uint32_t value,uint64_t cycles,const Bus& bus) {
        if((at&3) || at>0x18)return false;
        if(!at && !value) {control_=remaining_=0;until_=0;return true;}
        if(remaining_)throw std::runtime_error("Starlet SHA write while busy");
        if(at==4) {
            if(value&63)throw std::runtime_error("Starlet SHA source must be 64-byte aligned");
            source_=value;return true;
        }
        if(at>=8) {hash_[(at-8)/4]=value;return true;}
        if(!(value&0x80000000u) || (value&~0xc00003ffu))
            throw std::runtime_error("Starlet unsupported SHA control mode");
        const uint32_t blocks=(value&0x3ffu)+1,size=blocks*64;
        if(uint64_t(source_)+size>0x100000000ull || !bus.map || !bus.map(bus.user,source_,size))
            throw std::runtime_error("Starlet SHA DMA outside physical RAM");
        if(cycles>std::numeric_limits<uint64_t>::max()-blocks*block_cycles)
            throw std::runtime_error("Starlet SHA completion cycle overflow");
        control_=value&0xc0000000u;remaining_=blocks;until_=cycles+block_cycles;return true;
    }
    bool advance(uint64_t cycles,const Bus& bus) {
        while(remaining_ && cycles>=until_) {
            const auto* block=bus.map?bus.map(bus.user,source_,64):nullptr;
            if(!block)throw std::runtime_error("Starlet SHA DMA outside physical RAM");
            auto next=hash_;sha1_compress(next,block);
            hash_=next;source_+=64;--remaining_;
            if(remaining_)until_+=block_cycles;
            else {
                control_&=~0x80000000u;until_=0;
                std::atomic_thread_fence(std::memory_order_seq_cst);
                return (control_&0x40000000u)!=0;
            }
        }
        return false;
    }
private:
    std::array<uint32_t,5> hash_{};
    uint32_t control_=0,source_=0,remaining_=0;
    uint64_t until_=0;
};
}
