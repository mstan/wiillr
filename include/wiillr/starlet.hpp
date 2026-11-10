/* SPDX-License-Identifier: MIT */
#pragma once
#include "bus.h"
#include "cpu_state.h"
#include "wiillr/hollywood.h"
#include <array>
#include <span>
#include <cstdint>

namespace wiillr {
/* Original IOS kernel entry discovery. Consumers own MEM1/MEM2; SRAM belongs
 * to Starlet. Unknown hardware/CP15 operations fail, rather than succeed. */
class Starlet final : public armv4t::Bus {
public:
    Starlet(std::span<uint8_t> mem1, std::span<uint8_t> mem2);
    uint32_t load_ios(std::span<const uint8_t> binary);
    void advance(uint64_t arm_cycles) {cycles_+=arm_cycles;}
    uint32_t fetch32(uint32_t);
    uint16_t fetch16(uint32_t);
    uint8_t read8(uint32_t) override;
    uint16_t read16(uint32_t) override;
    uint32_t read32(uint32_t) override;
    void write8(uint32_t,uint8_t) override;
    void write16(uint32_t,uint16_t) override;
    void write32(uint32_t,uint32_t) override;
    void coproc_write(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) override;
    uint32_t coproc_read(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) override;
    void coproc_cdp(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) override;
    armv4t::CPUState cpu{};
private:
    uint8_t* physical(uint32_t,uint32_t);
    void check_alignment(uint32_t,uint32_t);
    uint32_t translate(uint32_t,bool writing);
    uint32_t sctl_=0x000500f8u,ttbr_=0,dacr_=0,dfsr_=0,far_=0,fcse_=0;
    uint64_t instruction_epoch_=0;
    uint64_t cycles_=0;
    uint16_t flush_request_=0,flush_ack_=0;
    uint32_t ahb_flush_mask_=0;
    uint32_t exi_control_=0;
    uint32_t ahb_prefetch_=0;
    std::array<uint32_t,18> arbiter_config_{};
    uint32_t spare1_=0x1000; /* IOS entry: boot0 ROM is already unmapped. */
    WiiHollywood hollywood_{};
    uint16_t protect_enabled_=0,protect_start_=0,protect_end_=0;
    std::span<uint8_t> mem1_,mem2_;
    std::array<uint8_t,0x10000> sram_{};
};
}
