/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "wiillr/aes128.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <vector>
namespace wiillr {
/* https://wiibrew.org/wiki/Hardware/AES_Engine and original IOS callers.
 * AES-128 CBC, rolling big-endian key/IV FIFOs, 12-bit blocks-minus-one.
 * The functional DMA profile latches the command and complete source image;
 * only disjoint or exactly in-place buffers are admitted. Each completed block
 * publishes before progress and final IRQ. 160 cycles/block is a deterministic
 * profile (one byte per round), not qualified physical throughput/contention.
 * Reset aborts pending DMA/IRQ and CBC continuation; programmed FIFOs/pointers
 * remain available, as with the other engine command-reset interfaces. */
class AesEngine {
public:
    static constexpr uint64_t block_cycles=160;
    struct Bus {void* user;uint8_t* (*map)(void*,uint32_t,uint32_t);void (*stored)(void*,uint32_t,uint32_t);};
    bool read(uint32_t at,uint32_t& value) const {
        if(at==0)value=control_;
        else if(at==4)value=source_;
        else if(at==8)value=destination_;
        else return false; // Key and IV FIFOs are write-only; never expose secrets.
        return true;
    }
    bool write(uint32_t at,uint32_t value,uint64_t cycles,const Bus& bus) {
        if((at&3) || at>0x10)return false;
        if(!at && !value) {
            control_=remaining_=0;until_=0;input_.clear();chain_valid_=false;return true;
        }
        if(remaining_)throw std::runtime_error("Starlet AES write while busy");
        if(at==4 || at==8) {
            if(value&15)throw std::runtime_error("Starlet AES DMA requires 16-byte alignment");
            (at==4?source_:destination_)=value;return true;
        }
        if(at==0xc || at==0x10) {
            auto& fifo=at==0xc?key_:iv_;auto& words=at==0xc?key_words_:iv_words_;
            std::move(fifo.begin()+4,fifo.end(),fifo.begin());
            for(unsigned i=0;i<4;++i)fifo[12+i]=uint8_t(value>>(24-i*8));
            if(words<4)++words;return true;
        }
        constexpr uint32_t allowed=0xd8001fffu;
        if(!(value&0x80000000u) || (value&~allowed))throw std::runtime_error("Starlet unsupported AES control mode");
        const bool cipher=(value&0x10000000u)!=0,continued=(value&0x1000u)!=0;
        if(cipher && (key_words_<4 || (!continued && iv_words_<4) || (continued && !chain_valid_)))
            throw std::runtime_error("Starlet AES requires programmed key and IV/CBC state");
        if(!cipher && continued)throw std::runtime_error("Starlet unsupported AES copy continuation");
        const uint32_t blocks=(value&0xfffu)+1,size=blocks*16;
        const uint64_t source_end=uint64_t(source_)+size,destination_end=uint64_t(destination_)+size;
        if(source_end>0x100000000ull || destination_end>0x100000000ull || !bus.map)
            throw std::runtime_error("Starlet AES DMA outside physical RAM");
        const auto* input=bus.map(bus.user,source_,size);
        auto* output=bus.map(bus.user,destination_,size);
        if(!input || !output)throw std::runtime_error("Starlet AES DMA outside physical RAM");
        /* Distinct bus aliases can name the same SRAM bytes. Admit exact
         * in-place operation, but detect partial overlap by actual backing. */
        const auto source_pointer=reinterpret_cast<uintptr_t>(input),destination_pointer=reinterpret_cast<uintptr_t>(output);
        if(input!=output && (source_pointer>destination_pointer?source_pointer-destination_pointer:destination_pointer-source_pointer)<size)
            throw std::runtime_error("Starlet unsupported partially overlapping AES DMA");
        if(cycles>std::numeric_limits<uint64_t>::max()-blocks*block_cycles)
            throw std::runtime_error("Starlet AES completion cycle overflow");
        std::vector<uint8_t> captured(input,input+size); // Allocation/validation precede any state publication.
        const auto expanded=cipher?aes128_expand(key_):Aes128Key{};
        active_key_=expanded;active_carry_=continued?carry_:iv_;input_=std::move(captured);
        cipher_=cipher;decrypt_=(value&0x08000000u)!=0;processed_=0;remaining_=blocks;
        control_=value&0xd8000000u;until_=cycles+block_cycles;return true;
    }
    bool advance(uint64_t cycles,const Bus& bus) {
        while(remaining_ && cycles>=until_) {
            auto* output=bus.map?bus.map(bus.user,destination_,16):nullptr;
            if(!output)throw std::runtime_error("Starlet AES DMA outside physical RAM");
            std::array<uint8_t,16> block;
            std::copy_n(input_.data()+processed_*16,16,block.begin());
            auto next=active_carry_;
            if(cipher_) {
                if(decrypt_) {next=block;aes128_decrypt(active_key_,block);}
                for(unsigned i=0;i<16;++i)block[i]^=active_carry_[i];
                if(!decrypt_) {aes128_encrypt(active_key_,block);next=block;}
            }
            std::copy(block.begin(),block.end(),output);
            if(bus.stored)bus.stored(bus.user,destination_,16);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            active_carry_=next;source_+=16;destination_+=16;++processed_;--remaining_;
            if(cipher_) {carry_=active_carry_;chain_valid_=true;}
            if(remaining_)until_+=block_cycles;
            else {
                control_&=~0x80000000u;until_=0;input_.clear();
                return (control_&0x40000000u)!=0;
            }
        }
        return false;
    }
private:
    std::array<uint8_t,16> key_{},iv_{},carry_{},active_carry_{};
    Aes128Key active_key_{};
    std::vector<uint8_t> input_;
    uint32_t control_=0,source_=0,destination_=0,remaining_=0,processed_=0;
    uint64_t until_=0;
    unsigned key_words_=0,iv_words_=0;
    bool cipher_=false,decrypt_=false,chain_valid_=false;
};
}
