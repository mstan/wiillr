/* SPDX-License-Identifier: MIT */
#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace wiillr {
/* Wii EXI boot RAM, separate from the three normal EXI channels.
 * https://wiibrew.org/wiki/Hardware/External_Interface#EXI_boot_vector
 * mini/powerpc.c uploads, sets HW_COMPAT.PPCBOOT, then disables EXI for reset.
 * Starlet applies that boot selector before exposing this PPC data view.
 * Unwritten words have no qualified initial contents; never invent a vector. */
class ExiBootMemory {
public:
    static constexpr uint32_t arm_base=0x0d806840u,ppc_base=0xfff00100u,size=64;
    static constexpr uint32_t ppc_boot_mask=1u<<20;
    static bool word(uint32_t address,uint32_t base,uint32_t width=4) {
        return width==4 && !(address&3u) && address>=base && address-base<=size-4;
    }
    bool arm_read(uint32_t address,bool enabled,uint32_t& value) const {
        return enabled && read(address,arm_base,4,value);
    }
    bool arm_write(uint32_t address,bool enabled,uint32_t value) {
        if(!enabled || !word(address,arm_base))return false;
        const uint32_t n=address-arm_base;
        bytes_[n]=uint8_t(value>>24);bytes_[n+1]=uint8_t(value>>16);
        bytes_[n+2]=uint8_t(value>>8);bytes_[n+3]=uint8_t(value);
        written_|=uint16_t(1u<<(n/4));return true;
    }
    bool ppc_read(uint32_t address,uint32_t width,bool exi_enabled,uint32_t& value) const {
        return !exi_enabled && read(address,ppc_base,width,value);
    }
    std::span<const uint8_t> bytes() const {return bytes_;}
private:
    bool read(uint32_t address,uint32_t base,uint32_t width,uint32_t& value) const {
        if(!word(address,base,width))return false;
        const uint32_t n=address-base;
        if(!(written_&(1u<<(n/4))))return false;
        value=uint32_t(bytes_[n])<<24|uint32_t(bytes_[n+1])<<16|
              uint32_t(bytes_[n+2])<<8|bytes_[n+3];return true;
    }
    std::array<uint8_t,size> bytes_{};
    uint16_t written_=0;
};
}
