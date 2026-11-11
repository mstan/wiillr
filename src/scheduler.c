/* SPDX-License-Identifier: MIT */
#include "wiillr/machine.h"
#include "wiillr/bluetooth.h"
#include "cpu/timing.h"
#include "debug/cost_ring.h"
#include "memory/memory.h"
#include <limits.h>

#if WIILLR_SCHED_DEADLINE
static u64 distance(u64 event,u64 cursor) {return event>cursor?event-cursor:1;}
static u64 earlier(u64 a,u64 b) {return a<b?a:b;}

static u64 domain_deadline(void* user,u32 domain,u64 cursor) {
    WiiMachine* m=user;
    switch(domain) {
    case GCN_DOMAIN_TIME:
        /* DEC is signed only for the underflow edge. Its clock is core/12. */
        return !(m->cpu->spr[22]&0x80000000u)?
            ((u64)m->cpu->spr[22]+1)*12-m->tb_phase:UINT64_MAX;
    case GCN_DOMAIN_VIDEO:
        return gcn_vi_deadline_distance(&m->vi,cursor);
    case GCN_DOMAIN_GPU:
        return m->cp.gp_read_enable && gcn_cp_fifo_rw_distance(&m->cp)>=32 &&
               !gcn_cp_at_breakpoint(&m->cp)?1:WII_MACHINE_POLL_PERIOD;
    case GCN_DOMAIN_AUDIO: {
        /* DSP LLE can assert a mailbox IRQ at any instruction. Until a
         * provider offers an exact next-event query, service within one cap. */
        u64 next=WII_MACHINE_POLL_PERIOD;
        const GcnDsp* dsp=&m->dsp.registers;
        if(dsp->dma_active)next=earlier(next,dsp->dma_cycles_left?dsp->dma_cycles_left:1);
        if(wii_hollywood_audio_clock(&m->hollywood)) {
            if(dsp->aid_int_pending)
                next=earlier(next,dsp->aid_start_cycles?dsp->aid_start_cycles:1);
            if(dsp->aid_ctrl&0x8000u) {
                const u32 rate=(m->ai.control&GCN_AI_CR_AIDFR)?32000u:48000u;
                const u32 period=(u32)(729000000ull*8/rate);
                next=earlier(next,period>dsp->aid_accum?period-dsp->aid_accum:1);
            }
            if((m->ai.control&GCN_AI_CR_PSTAT) && !(m->ai.control&GCN_AI_CR_AIINT)) {
                const u64 count=(u32)(m->ai.int_timing-m->ai.sample_counter);
                const u64 samples=count?count:(1ull<<32);
                const u64 threshold=(m->ai.control&GCN_AI_CR_AISFR)?
                    m->ai.sample_cycles_48:m->ai.sample_cycles_32;
                const u64 total=samples*threshold;
                const u64 scaled=total>m->ai.cycle_accum_x2?total-m->ai.cycle_accum_x2:0;
                next=earlier(next,scaled?(scaled+m->ai.phase_scale-1)/m->ai.phase_scale:1);
            }
        }
        return next;
    }
    case GCN_DOMAIN_IO: {
        u64 next=WII_MACHINE_POLL_PERIOD;
        if(m->di.cmd_pending)next=earlier(next,m->di.pending_cycles?m->di.pending_cycles:1);
        if(m->ipc.interrupt_scheduled)next=earlier(next,distance(m->ipc.interrupt_at,cursor));
        if(WIILLR_IOS_NATIVE) {
            if(m->ios.phase && !m->ipc.y)next=earlier(next,distance(m->ios.ready_at,cursor));
            if(!m->ios.phase && (m->ipc.x&1))next=1;
            if(m->ios.bluetooth && !m->ipc.y)
                next=earlier(next,wii_bluetooth_deadline(m->ios.bluetooth,cursor));
        }
        return next;
    }
    default:return UINT64_MAX;
    }
}

static bool advance(void* user,u32 domain,u64 elapsed,u64 target) {
    WiiMachine* m=user;CPUState* cpu=m->cpu;
    for(unsigned i=0;i<GCN_DOMAIN_COUNT;++i)if(domain==(1u<<i)) {
        if(elapsed>m->domain_max_interval[i])m->domain_max_interval[i]=elapsed;
        const u64 due=m->domains.due[i];
        if(due!=UINT64_MAX && target>due && target-due>m->domain_max_lateness)
            m->domain_max_lateness=target-due;
    }
    if(domain!=GCN_DOMAIN_TIME &&
       !gcn_domains_observe(&m->domains,GCN_DOMAIN_TIME,target))return false;
    const unsigned prior=gcn_cost_enter(GCN_COST_TIME);
    bool ok=true;
    switch(domain) {
    case GCN_DOMAIN_TIME: {
        const u64 delta=elapsed+m->tb_phase;
        const u32 ticks=(u32)(delta/12);
        m->tb_phase=(u32)(delta%12);m->last_cycles=target;
        cpu->timebase+=ticks;ppc_decrementer_tick(cpu,ticks);
        break;
    }
    case GCN_DOMAIN_GPU:
        gcn_cost_enter(GCN_COST_FIFO);
        while(m->cp.gp_read_enable && gcn_cp_fifo_rw_distance(&m->cp)>=32 &&
              !gcn_cp_at_breakpoint(&m->cp)) {
            u32 available;const u32 address=gcn_cp_fifo_read_pointer(&m->cp);
            const u8* data=gcn_mem_resolve(cpu,address,&available);
            if(!data || available<32 || !m->gx_fifo || !m->gx_fifo(m->gx_user,data,32)) {
                ok=false;break;
            }
            gcn_cp_gpu_consume_chunk(&m->cp);
        }
        break;
    case GCN_DOMAIN_VIDEO:
        if(!gcn_domains_observe(&m->domains,GCN_DOMAIN_GPU,target)){ok=false;break;}
        gcn_cost_enter(GCN_COST_VIDEO);gcn_vi_tick(target);break;
    case GCN_DOMAIN_AUDIO: {
        const bool clock=wii_hollywood_audio_clock(&m->hollywood);
        const u32 rate=(m->ai.control&GCN_AI_CR_AIDFR)?32000u:48000u;
        gcn_cost_enter(GCN_COST_DSP);
        ok=wii_dsp_tick(&m->dsp,(u32)elapsed,clock?(u32)(729000000ull*8/rate):0);
        const u32 csr=m->dsp.registers.csr;
        gcn_pi_set_interrupt(&m->pi,GCN_PI_INT_DSP,
            ((csr&0x80u)&&(csr&0x100u)) || ((csr&0x20u)&&(csr&0x40u)) ||
            ((csr&GCN_DSP_CSR_AIDINT)&&(csr&GCN_DSP_CSR_AIDMASK)));
        gcn_cost_enter(GCN_COST_AI);if(clock)gcn_ai_tick((u32)elapsed);
        break;
    }
    case GCN_DOMAIN_IO:
        gcn_cost_enter(GCN_COST_DI);gcn_di_tick(cpu,(u32)elapsed);
        gcn_cost_enter(GCN_COST_IOS);
        if(WIILLR_IOS_NATIVE)ok=wii_ios_native_tick(&m->ios,&m->ipc,cpu);
        gcn_cost_enter(GCN_COST_IPC);wii_ipc_tick(&m->ipc,target);
        gcn_pi_set_interrupt(&m->pi,0x4000,wii_ipc_ppc_interrupt(&m->ipc));break;
    default:ok=false;break;
    }
    gcn_cost_leave(prior);
    return ok && !cpu->halted;
}

static void observe_time(CPUState* cpu,void* user,bool changed) {
    WiiMachine* m=user;
    if(changed)gcn_domains_changed(&m->domains,GCN_DOMAIN_TIME);
    else if(!gcn_domains_observe(&m->domains,GCN_DOMAIN_TIME,cpu->cycles))cpu->halted=true;
}
#endif

void wii_machine_scheduler_init(WiiMachine* m,CPUState* cpu) {
    m->cpu=cpu;
#if WIILLR_SCHED_DEADLINE
    if(!gcn_domains_init_mask(&m->domains,cpu->cycles,GCN_DOMAIN_WII_ALL,
                             advance,domain_deadline,m))cpu->halted=true;
    ppc_timing_set_observer(cpu,observe_time,m);
#else
    ppc_timing_set_observer(NULL,NULL,NULL);
#endif
}

u64 wii_machine_deadline(WiiMachine* m,u64 now) {
#if WIILLR_SCHED_DEADLINE
    return now+earlier(WII_MACHINE_POLL_PERIOD,gcn_domains_deadline(&m->domains,now));
#else
    (void)m;return now+64;
#endif
}

void wii_machine_observe_register(WiiMachine* m,CPUState* cpu,u32 physical) {
#if WIILLR_SCHED_DEADLINE
    const u32 p=physical&~0x00800000u;
    u32 mask=0;
    if(p>=0x0c000000u && p<0x0c002000u)mask=GCN_DOMAIN_GPU;
    else if(p>=0x0c002000u && p<0x0c003000u)mask=GCN_DOMAIN_VIDEO;
    else if(p>=0x0c003000u && p<0x0c004000u)mask=GCN_DOMAIN_WII_ALL;
    else if(p>=0x0c005000u && p<0x0c006000u)mask=GCN_DOMAIN_AUDIO;
    else if(p>=0x0c008000u && p<0x0c008400u)mask=GCN_DOMAIN_GPU;
    else if(p>=0x0d006c00u && p<0x0d007000u)mask=GCN_DOMAIN_AUDIO;
    else if(p>=0x0d006000u && p<0x0d006c00u)mask=GCN_DOMAIN_IO;
    else if(p>=0x0d000000u && p<0x0d000040u)mask=GCN_DOMAIN_IO;
    else if(p>=0x0d000180u && p<0x0d0001d4u)mask=GCN_DOMAIN_AUDIO;
    if(mask) {
        if(!gcn_domains_observe(&m->domains,mask,cpu->cycles))cpu->halted=true;
        /* Reads of mail/status registers can mutate state too. Revoke the
         * selected deadline after observation and before the actual access. */
        gcn_domains_changed(&m->domains,mask);
    }
#else
    (void)m;(void)cpu;(void)physical;
#endif
}

bool wii_machine_service_domains(WiiMachine* m,CPUState* cpu) {
#if WIILLR_SCHED_DEADLINE
    return gcn_domains_credit(&m->domains,cpu->cycles);
#else
    (void)m;(void)cpu;return true;
#endif
}
