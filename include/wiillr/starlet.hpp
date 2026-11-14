/* SPDX-License-Identifier: MIT */
#pragma once
#include "bus.h"
#include "cpu_state.h"
#include "wiillr/hollywood.h"
#include "wiillr/ipc.h"
#include "wiillr/seeprom.hpp"
#include "wiillr/ohci_init.hpp"
#include <array>
#include <span>
#include <cstdint>
namespace armv4t {struct Instr;}

namespace wiillr {
/* Original IOS kernel entry discovery. Consumers own MEM1/MEM2; SRAM belongs
 * to Starlet. Unknown hardware/CP15 operations fail, rather than succeed. */
class Starlet final : public armv4t::Bus {
public:
    Starlet(std::span<uint8_t> mem1, std::span<uint8_t> mem2);
    uint32_t load_ios(std::span<const uint8_t> binary);
    void load_otp(std::span<const uint8_t> bytes);
    void load_seeprom(std::span<const uint8_t> bytes) {seeprom_.load(bytes);}
    std::span<const uint8_t> seeprom_image() {return seeprom_.image(cycles_);}
    uint32_t execute(const armv4t::Instr& instruction);
    uint32_t step();
    uint64_t cycles() const {return cycles_;}
    uint64_t swi_entries() const {return swi_entries_;}
    uint64_t irq_entries() const {return irq_entries_;}
    uint64_t undefined_entries() const {return undefined_entries_;}
    uint64_t data_aborts() const {return data_aborts_;}
    uint64_t prefetch_aborts() const {return prefetch_aborts_;}
    void advance(uint64_t arm_cycles);
    void attach_ipc(WiiIpc& ipc,uint64_t ppc_origin=0) {ipc_=&ipc;ppc_origin_=ppc_origin;}
    /* Attach before PPC execution, establishing the original-kernel profile. */
    void attach_hollywood(WiiHollywood& hardware) {hardware=owned_hollywood_;hollywood_=&hardware;}
    WiiIpc& ipc() {return *ipc_;}
    void observe_stores(void (*observer)(void*,uint32_t,uint32_t),void* user) {
        store_observer_=observer;store_user_=user;
    }
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
    void exception(armv4t::Mode mode,uint32_t vector,uint32_t link);
    uint32_t gpio_outputs() const {return gpio_[0]&gpio_[1]&gpio_enable_;}
    /* Starlet's high exception vectors are selected on entry (ARM926
     * VINITHI/SCTLR.V). The original IOS startup preserves that V bit. */
    uint32_t sctl_=0x000520f8u,ttbr_=0,dacr_=0,dfsr_=0,far_=0,fcse_=0;
    uint32_t ifsr_=0;
    void (*store_observer_)(void*,uint32_t,uint32_t)=nullptr;
    void* store_user_=nullptr;
    bool fetching_=false;
    uint64_t instruction_epoch_=0;
    uint64_t cycles_=0;
    uint64_t swi_entries_=0;
    uint64_t irq_entries_=0;
    uint64_t undefined_entries_=0;
    uint64_t data_aborts_=0,prefetch_aborts_=0;
    uint64_t ppc_origin_=0;
    WiiIpc owned_ipc_{};
    WiiIpc* ipc_=&owned_ipc_;
    uint32_t alarm_=0;
    uint32_t debug_irq_enable_=0;
    uint32_t ahb_protection_=0;
    uint32_t otp_command_=0;
    std::array<uint8_t,128> otp_{};
    bool otp_loaded_=false;
    uint16_t flush_request_=0,flush_ack_=0;
    uint32_t ahb_flush_mask_=0;
    uint32_t exi_control_=0;
    uint32_t ahb_prefetch_=0;
    uint32_t vi_pll_=0,vi_pll_ext_=0;
    uint32_t usb_pll_=0,usb_pll_ext_=0;
    uint32_t usb_force_reset_=0;
    std::array<uint32_t,4> usb_phy_{};
    std::array<uint32_t,3> io_strength_{};
    std::array<uint32_t,3> dma_control_{}; /* NAND, AES, SHA idle controls */
    std::array<uint32_t,8> gpio_{};
    uint32_t gpio_enable_=0;
    Seeprom seeprom_;
    std::array<OhciInit,2> ohci_;
    uint32_t resets_=0x08000000u; /* isolated entry: IOP running; PPC held */
    std::array<uint32_t,18> arbiter_config_{};
    uint32_t spare1_=0x1000; /* IOS entry: boot0 ROM is already unmapped. */
    WiiHollywood owned_hollywood_{};
    WiiHollywood* hollywood_=&owned_hollywood_;
    uint16_t protect_enabled_=0,protect_start_=0,protect_end_=0;
    std::span<uint8_t> mem1_,mem2_;
    std::array<uint8_t,0x10000> sram_{};
};
}
