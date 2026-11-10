/* SPDX-License-Identifier: MIT */
#include "wiillr/machine.h"
#include "cpu/timing.h"
#include "memory/memory.h"
#include <stdio.h>
#include <string.h>

static u64 read_device(CPUState* cpu, u32 address, u8 width) {
    WiiMachine* m = cpu->external_user_data;
    ++m->mmio_reads;
    u32 physical = address & 0x3fffffffu;
    const u32 gpio=physical&~0x00800000u;
    u32 hollywood_value;
    if(width==4 && wii_hollywood_read(&m->hollywood,physical,&hollywood_value))return hollywood_value;
    if(width==4 && gpio==0x0d000018u)return m->vi_config;
    if(width==4 && gpio==0x0d0000c0u)return m->gpio_out;
    if(width==4 && gpio==0x0d0000c4u)return m->gpio_direction;
    if(width==4 && gpio==0x0d0000c8u)
        return (m->gpio_out&m->gpio_direction&0x0320u)|wii_ave_pins(&m->ave)|
               (m->di.disc_present?0x80u:0);
    if(physical>=0x0c000000u && physical+width<=0x0c000000u+GCN_CP_SIZE &&
       (width==2 || width==4) && !(address&(width-1)))
        return gcn_cp_read(&m->cp,cpu,physical|0xc0000000u,width);
    if(physical>=0x0c001000u && physical+width<=0x0c001000u+GCN_PE_SIZE &&
       (width==2 || width==4) && !(address&(width-1)))
        return gcn_pe_read(&m->pe,cpu,physical|0xc0000000u,width);
    if (physical >= 0x0d006400u && physical + width <= 0x0d006400u + GCN_SI_SIZE &&
        width == 4 && !(address & 3))
        return gcn_si_read(&m->si, cpu, GCN_SI_BASE + physical - 0x0d006400u, width);
    if (physical >= 0x0d006000u && physical < 0x0d006028u && width == 4 && !(address & 3))
        return gcn_di_read(&m->di, cpu, GCN_DI_BASE + physical - 0x0d006000u, width);
    if (physical >= 0x0d006800u && physical + width <= 0x0d006800u + GCN_EXI_REGISTER_BYTES &&
        width == 4 && !(address & 3))
        return gcn_exi_read(&m->exi, cpu, GCN_EXI_BASE + physical - 0x0d006800u, width);
    if (physical >= 0x0d006c00u && physical < 0x0d006c10u && width == 4 && !(address & 3))
        return gcn_ai_read(&m->ai, cpu, GCN_AI_BASE + physical - 0x0d006c00u, width);
    if (physical >= 0x0c005000u && physical + width <= 0x0c005000u + GCN_DSP_SIZE) {
        u64 value;
        if (wii_dsp_read(&m->dsp, physical - 0x0c005000u, width, &value)) return value;
    }
    if (physical >= 0x0c004000u && physical + width <= 0x0c004000u + GCN_MI_SIZE &&
        (width == 1 || width == 2 || width == 4) && !(address & (width - 1)))
        return gcn_mi_read(&m->mi, cpu, physical | 0xc0000000u, width);
    if (physical >= 0x0c002000u && physical + width <= 0x0c002000u + GCN_VI_SIZE &&
        (width == 1 || width == 2 || width == 4) && !(address & (width - 1)))
        return gcn_vi_read(&m->vi, cpu, physical | 0xc0000000u, width);
    if (physical >= 0x0d000000u && physical < 0x0d000040u && width == 4 && !(address & 3)) {
        u32 value;
        wii_ipc_tick(&m->ipc, cpu->cycles);
        if (wii_ipc_read(&m->ipc, physical - 0x0d000000u, &value)) return value;
    }
    if (physical >= 0x0c003000u && physical < 0x0c003000u + GCN_PI_SIZE &&
        (width == 1 || width == 2 || width == 4) && !(address & (width - 1))) {
        u32 value = gcn_pi_read(&m->pi, cpu, (physical | 0xc0000000u) & ~3u, 4);
        return width == 4 ? value : (value >> ((4 - width - (address & 3)) * 8)) &
                                           ((1u << (width * 8)) - 1);
    }
    m->unsupported_address = address;
    fprintf(stderr, "unimplemented read pc=%08x address=%08x width=%u\n", cpu->pc, address, width);
    cpu->halted = true;
    return 0;
}

static void write_device(CPUState* cpu, u32 address, u64 value, u8 width) {
    WiiMachine* m = cpu->external_user_data;
    ++m->mmio_writes;
    u32 physical = address & 0x3fffffffu;
    const u32 gpio=physical&~0x00800000u;
    if(width==4 && value<=0xffffffffu && wii_hollywood_write(&m->hollywood,physical,(u32)value))return;
    /* HW_VI1CFG: preserve the documented region-select latch. Other bits have
     * no implemented contract and must not disappear into a no-op write. */
    if(width==4 && gpio==0x0d000018u && !(value&~0x20000ull)) {m->vi_config=(u32)value;return;}
    /* GPIO1B controls only PPC-owned pins. The AV encoder sees actual clock,
     * data and direction transitions, with a pulled-up released SDA line.
     * Ownership: fail0verflow/mini gpio.h GP_OWNER_PPC. */
    if(width==4 && gpio==0x0d0000c4u) {
        m->gpio_direction=(m->gpio_direction&~0xc3a0u)|((u32)value&0xc3a0u);
        if(wii_ave_gpio(&m->ave,m->gpio_out,m->gpio_direction))return;
        fprintf(stderr,"unimplemented AVE read register=%02x\n",m->ave.index);cpu->halted=true;return;
    }
    if(width==4 && gpio==0x0d0000c0u && !(value&0x200u)) {
        m->gpio_out=(m->gpio_out&~0xc3a0u)|((u32)value&0xc3a0u);
        if(wii_ave_gpio(&m->ave,m->gpio_out,m->gpio_direction))return;
        fprintf(stderr,"unimplemented AVE read register=%02x\n",m->ave.index);cpu->halted=true;return;
    }
    if(physical>=0x0c000000u && physical+width<=0x0c000000u+GCN_CP_SIZE &&
       (width==2 || width==4) && !(address&(width-1))) {
        gcn_cp_write(&m->cp,cpu,physical|0xc0000000u,(u32)value,width);return;
    }
    if(physical>=0x0c001000u && physical+width<=0x0c001000u+GCN_PE_SIZE &&
       (width==2 || width==4) && !(address&(width-1))) {
        gcn_pe_write(&m->pe,cpu,physical|0xc0000000u,(u32)value,width);return;
    }
    if(physical>=0x0c008000u && physical+width<=0x0c008000u+GCN_GP_SIZE &&
       (width==1 || width==2 || width==4 || width==8) && !(address&(width-1))) {
        u32 available;
        if(m->gp.count+width>=32 && (!gcn_mem_resolve(cpu,m->pi.reg[GCN_PI_FIFO_WPTR>>2],&available) || available<32)) {
            fprintf(stderr,"invalid GX gather address=%08x\n",m->pi.reg[GCN_PI_FIFO_WPTR>>2]);cpu->halted=true;return;
        }
        if(width==8) {gcn_gp_write(&m->gp,cpu,address,(u32)(value>>32),4);gcn_gp_write(&m->gp,cpu,address+4,(u32)value,4);}
        else gcn_gp_write(&m->gp,cpu,address,(u32)value,width);
        return;
    }
    if (physical >= 0x0d006400u && physical + width <= 0x0d006400u + GCN_SI_SIZE &&
        width == 4 && !(address & 3)) {
        gcn_si_write(&m->si, cpu, GCN_SI_BASE + physical - 0x0d006400u, (u32)value, width);
        return;
    }
    if (physical >= 0x0d006000u && physical < 0x0d006028u && width == 4 && !(address & 3)) {
        gcn_di_write(&m->di, cpu, GCN_DI_BASE + physical - 0x0d006000u, (u32)value, width);
        return;
    }
    if (physical >= 0x0d006800u && physical + width <= 0x0d006800u + GCN_EXI_REGISTER_BYTES &&
        width == 4 && !(address & 3)) {
        gcn_exi_write(&m->exi, cpu, GCN_EXI_BASE + physical - 0x0d006800u, (u32)value, width);
        return;
    }
    if (physical >= 0x0d006c00u && physical < 0x0d006c10u && width == 4 && !(address & 3)) {
        gcn_ai_write(&m->ai, cpu, GCN_AI_BASE + physical - 0x0d006c00u, (u32)value, width);
        return;
    }
    if (physical >= 0x0c005000u && physical + width <= 0x0c005000u + GCN_DSP_SIZE &&
        wii_dsp_write(&m->dsp, physical - 0x0c005000u, width, value)) return;
    if (physical >= 0x0c004000u && physical + width <= 0x0c004000u + GCN_MI_SIZE &&
        (width == 1 || width == 2 || width == 4) && !(address & (width - 1))) {
        gcn_mi_write(&m->mi, cpu, physical | 0xc0000000u, (u32)value, width);
        return;
    }
    if (physical >= 0x0c002000u && physical + width <= 0x0c002000u + GCN_VI_SIZE &&
        (width == 1 || width == 2 || width == 4) && !(address & (width - 1))) {
        gcn_vi_write(&m->vi, cpu, physical | 0xc0000000u, (u32)value, width);
        return;
    }
    if (physical >= 0x0d000000u && physical < 0x0d000040u && width == 4 && !(address & 3) &&
        wii_ipc_write(&m->ipc, physical - 0x0d000000u, (u32)value, cpu->cycles)) {
        if (!WIILLR_IOS_NATIVE && (m->ipc.x & 1)) {
            /* Discovery stops at the real service boundary until an IOS
             * implementation is attached. Never acknowledge or invent success. */
            fprintf(stderr, "IOS request awaiting provider pc=%08x message=%08x\n",
                    cpu->pc, m->ipc.ppc_message);
            cpu->halted = true;
        }
        return;
    }
    if (physical >= 0x0c003000u && physical < 0x0c003000u + GCN_PI_SIZE &&
        width == 4 && !(address & 3)) {
        gcn_pi_write(&m->pi, cpu, physical | 0xc0000000u, (u32)value, width);
        return;
    }
    m->unsupported_address = address;
    fprintf(stderr, "unimplemented write pc=%08x address=%08x width=%u value=%llx\n",
            cpu->pc, address, width, (unsigned long long)value);
    cpu->halted = true;
}

static void vi_interrupt(void* user, int asserted) {
    gcn_pi_set_interrupt(user, GCN_PI_INT_VI, asserted);
}
static void vi_field(void* user, double period) {
    (void)period;
    ++((WiiMachine*)user)->fields;
}
static void ai_interrupt(void* user, int asserted) {
    gcn_pi_set_interrupt(user, GCN_PI_INT_AI, asserted);
}
static void exi_interrupt(void* user, int asserted) {
    gcn_pi_set_interrupt(user, GCN_PI_INT_EXI, asserted);
}
static void di_interrupt(void* user, int asserted) {
    gcn_pi_set_interrupt(user, GCN_PI_INT_DI, asserted);
}
static void si_interrupt(void* user, int asserted) {
    gcn_pi_set_interrupt(user, GCN_PI_INT_SI, asserted);
}
static void cp_interrupt(void* user,int asserted) {gcn_pi_set_interrupt(user,GCN_CP_INT_CAUSE,asserted);}
static void pe_interrupt(void* user,u32 cause,int asserted) {gcn_pi_set_interrupt(user,cause,asserted);}

void wii_machine_init(WiiMachine* m, CPUState* cpu) {
    memset(m, 0, sizeof *m);
    wii_hollywood_init(&m->hollywood);
    wii_ave_init(&m->ave);
    gcn_pi_init_model(&m->pi, true);
    gcn_cp_init_model(&m->cp,true);gcn_cp_set_irq(&m->cp,cp_interrupt,&m->pi);
    gcn_pe_init(&m->pe);gcn_pe_set_irq(&m->pe,pe_interrupt,&m->pi);
    gcn_gp_init(&m->gp,&m->pi,&m->cp);gcn_pi_set_fifo_reset_hook(&m->pi,gcn_gp_reset);
    wii_ipc_init(&m->ipc);
    wii_ios_native_init(&m->ios);
    gcn_mi_init(&m->mi);
    gcn_vi_init_model(&m->vi, true);
    gcn_vi_set_irq(&m->vi, vi_interrupt, &m->pi);
    gcn_vi_set_field_hook(&m->vi, vi_field, m);
    gcn_ai_init_model(&m->ai, true);
    gcn_ai_set_irq(&m->ai, ai_interrupt, &m->pi);
    gcn_exi_init(&m->exi);
    gcn_exi_set_irq(&m->exi, exi_interrupt, &m->pi);
    gcn_di_init_model(&m->di, true);
    m->ios.di=&m->di;
    m->ios.gpio_out=&m->gpio_out;
    gcn_di_set_irq(&m->di, di_interrupt, &m->pi);
    gcn_si_init(&m->si);
    gcn_si_set_irq(&m->si, si_interrupt, &m->pi);
    gcn_vi_set_si_poll_hook(&m->vi, gcn_si_beam_poll, &m->si);
    m->last_cycles = cpu->cycles;
    cpu->external_user_data = m;
    cpu->external_read = read_device;
    cpu->external_write = write_device;
}

void wii_machine_tick(WiiMachine* m, CPUState* cpu) {
    u64 elapsed = cpu->cycles - m->last_cycles;
    u64 delta = elapsed + m->tb_phase;
    u32 ticks = (u32)(delta / 12);
    m->tb_phase = (u32)(delta % 12);
    m->last_cycles = cpu->cycles;
    cpu->timebase += ticks;
    while(m->cp.gp_read_enable && gcn_cp_fifo_rw_distance(&m->cp)>=32 && !gcn_cp_at_breakpoint(&m->cp)) {
        u32 available;
        const u32 address=gcn_cp_fifo_read_pointer(&m->cp);
        const u8* data=gcn_mem_resolve(cpu,address,&available);
        if(!data || available<32 || !m->gx_fifo || !m->gx_fifo(m->gx_user,data,32)) {
            fprintf(stderr,"GX FIFO consumer unavailable/failed address=%08x\n",address);cpu->halted=true;break;
        }
        gcn_cp_gpu_consume_chunk(&m->cp);
    }
    ppc_decrementer_tick(cpu, ticks);
    const u32 audio_rate=(m->ai.control&GCN_AI_CR_AIDFR)?32000u:48000u;
    const bool audio_clock=wii_hollywood_audio_clock(&m->hollywood);
    if (!wii_dsp_tick(&m->dsp, (u32)elapsed,audio_clock?(u32)(729000000ull*8/audio_rate):0)) cpu->halted=true;
    u32 csr=m->dsp.registers.csr;
    gcn_pi_set_interrupt(&m->pi, GCN_PI_INT_DSP,
                         ((csr & 0x80u) && (csr & 0x100u)) || ((csr & 0x20u) && (csr & 0x40u)) ||
                         ((csr & GCN_DSP_CSR_AIDINT) && (csr & GCN_DSP_CSR_AIDMASK)));
    gcn_vi_tick(cpu->cycles);
    if(audio_clock)gcn_ai_tick((u32)elapsed);
    gcn_di_tick(cpu, (u32)elapsed);
    if(WIILLR_IOS_NATIVE && !wii_ios_native_tick(&m->ios,&m->ipc,cpu)) {
        fprintf(stderr,"native IOS stopped at unsupported operation pc=%08x message=%08x\n",
                cpu->pc,m->ipc.ppc_message);
        cpu->halted=true;
    }
    wii_ipc_tick(&m->ipc, cpu->cycles);
    gcn_pi_set_interrupt(&m->pi, 0x4000, wii_ipc_ppc_interrupt(&m->ipc));
    ppc_deliver_decrementer(cpu);
    gcn_pi_deliver_external(cpu);
}
