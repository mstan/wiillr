/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Maintained DSP engine with shared ARAM and paced audio-DMA state machines.
 * PCM retirement is counted; the consumer may attach the shared host sink. */
#include "wiillr/dsp.h"
#include "cpu/native_code.h"
#include "memory/memory.h"
#include "debug/cost_ring.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if WIILLR_DSP_TRACE
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
static unsigned trace_left;
static u16 trace_cpu_high,trace_dsp_high;
static void trace_event(const WiiDsp* d,const char* event,u32 value) {
    if(!trace_left)return;
    --trace_left;
    char line[192];
    const int size=snprintf(line,sizeof line,"[wii-dsp-contract] {\"event\":\"%s\",\"cycles\":%llu,\"value\":%u,\"csr\":%u}\n",
        event,(unsigned long long)d->cpu->cycles,value,d->registers.csr);
    if(size<=0 || size>=(int)sizeof line){trace_left=0;return;}
    /* One low-level write keeps the record whole beside foreign GPU loggers. */
#ifdef _WIN32
    if(_write(_fileno(stderr),line,(unsigned)size)!=size)trace_left=0;
#else
    if(write(fileno(stderr),line,(size_t)size)!=size)trace_left=0;
#endif
}
#else
#define trace_event(...) ((void)0)
#endif

static bool prepare(void* user, u32 offset, u32 size, bool writing) {
    WiiDsp* d = user;
    (void)writing;
    if(offset>=0x10000000u && offset-0x10000000u<=d->cpu->mem2_size)
        return size<=d->cpu->mem2_size-(offset-0x10000000u);
    return offset <= d->cpu->ram_size && size <= d->cpu->ram_size - offset;
}
static bool written(void* user, u32 offset, u32 size) {
    WiiDsp* d = user;
    if (!prepare(user, offset, size, true)) return false;
    if (d->cpu->reserve_valid) {
        u32 reservation = gcn_reservation_key(d->cpu->reserve_addr) & ~31u;
        if (size && offset < reservation + 32u && reservation < (u64)offset + size)
            d->cpu->reserve_valid = false;
    }
    gcn_native_code_invalidate(offset, size);
    return true;
}
static u64 timebase(void* user) { return ((WiiDsp*)user)->cpu->timebase; }
static void interrupt(void* user) {
    WiiDsp* d=user;d->registers.csr |= GCN_DSP_CSR_DSPINT;
    trace_event(d,"irq",GCN_DSP_CSR_DSPINT);
}
static void audio(void* user,const u8* samples,u32 bytes) {
    WiiDsp* d=user;d->audio_frames+=bytes/4;
    for(u32 n=0;n<bytes;n++)if(samples[n])++d->audio_nonzero_bytes;
    if(d->output_sink)d->output_sink(d->output_user,samples,bytes);
}

bool wii_dsp_start(WiiDsp* d, CPUState* cpu, const u8* irom, const u8* coef) {
    memset(d, 0, sizeof *d);
    gcn_dsp_reset_registers(&d->registers);
    d->cpu = cpu;
#if WIILLR_DSP_TRACE
    trace_left=512;trace_cpu_high=trace_dsp_high=0;
#endif
    d->registers.audio_sink=audio;d->registers.audio_user=d;
    d->aram = calloc(1, 0x1000000);
    if (!d->aram) return false;
    GcnDspEngineConfig config = {0};
    config.mem1=cpu->ram; config.mem1_size=cpu->ram_size;
    config.aram=d->aram; config.aram_size=0x1000000;
    config.irom=irom; config.irom_size=irom?8192:0;
    config.coef=coef; config.coef_size=coef?4096:0;
    config.user=d; config.prepare=prepare; config.written=written;
    config.timebase=timebase; config.interrupt=interrupt;
    config.wii=true; config.mem2=cpu->mem2; config.mem2_size=cpu->mem2_size;
    d->engine = gcn_dsp_engine_create(&config);
    if (!d->engine) { free(d->aram); d->aram=NULL; return false; }
    return true;
}
void wii_dsp_stop(WiiDsp* d) {
    if (d->engine) gcn_dsp_engine_destroy(d->engine);
    free(d->aram);
    d->engine=NULL; d->aram=NULL;
}
static bool read16(WiiDsp* d, u32 offset, u16* value) {
    if (offset <= 6) {
        const bool ok=gcn_dsp_engine_read_mail(d->engine,offset<4,(offset&2)!=0,value);
#if WIILLR_DSP_TRACE
        if(ok && offset==4)trace_dsp_high=*value;
        if(ok && offset==6 && (trace_dsp_high&0x8000u))
            trace_event(d,"dsp-mail",(u32)trace_dsp_high<<16|*value);
#endif
        return ok;
    }
    if (offset == GCN_DSP_CSR) {
        u16 control;
        if (!gcn_dsp_engine_read_control(d->engine, &control)) return false;
        *value=(control & GCN_DSP_CONTROL_MASK) | d->registers.csr |
               (d->registers.dma_active ? GCN_DSP_CSR_DMA:0);
        return true;
    }
    if(offset>=GCN_DSP_AID_START_HI && offset<=GCN_DSP_AID_BLOCKS_LEFT) {
        *value=gcn_dsp_aid_read16(&d->registers,offset);return true;
    }
    *value=gcn_dsp_reg_read16(&d->registers, offset);
    return true;
}
bool wii_dsp_read(WiiDsp* d, u32 offset, u8 size, u64* value) {
    if (!d->engine || (size != 2 && size != 4) || (offset & (size-1)) || offset+size > GCN_DSP_SIZE)
        return false;
    const unsigned previous=gcn_cost_enter(GCN_COST_DSP);
    u16 high, low;
    bool ok=read16(d, offset, &high);
    if (ok && size == 2) *value=high;
    else if(ok) {
        ok=read16(d, offset+2, &low);
        if(ok)*value=(u32)high<<16 | low;
    }
    gcn_cost_leave(previous);
    return ok;
}
static bool write16(WiiDsp* d, u32 offset, u16 value) {
    if (offset < 4) {
        const bool ok=gcn_dsp_engine_write_mail(d->engine,offset==2,value);
#if WIILLR_DSP_TRACE
        if(ok && offset==0)trace_cpu_high=value;
        if(ok && offset==2)trace_event(d,"cpu-mail",(u32)trace_cpu_high<<16|value);
#endif
        return ok;
    }
    if (offset == 4 || offset == 6) return false; /* DSP-owned mailbox */
    if (offset == GCN_DSP_CSR) {
        if (!gcn_dsp_engine_write_control(d->engine, value & GCN_DSP_CONTROL_MASK)) return false;
        /* Interrupt masks are writable; interrupt status is W1C. */
        d->registers.csr=(d->registers.csr & 0xa8u & ~value) | (value & 0x150u);
        trace_event(d,"control",value);
        return true;
    }
    if(offset>=GCN_DSP_AID_START_HI && offset<=GCN_DSP_AID_BLOCKS_LEFT) {
        gcn_dsp_aid_write16(&d->registers,offset,value,true);return true;
    }
    gcn_dsp_reg_write16(&d->registers, offset, value);
    if(offset==GCN_DSP_AR_CNT_LO)
        return gcn_dsp_aram_transfer(&d->registers,d->cpu,d->cpu->mem2,d->cpu->mem2_size);
    return true;
}
bool wii_dsp_write(WiiDsp* d, u32 offset, u8 size, u64 value) {
    if (!d->engine || (size != 2 && size != 4) || (offset & (size-1)) || offset+size > GCN_DSP_SIZE)
        return false;
    /* HLE tasks can mix synchronously while receiving a mailbox write.  The
     * outer device tick alone therefore does not cover the DSP's host work. */
    const unsigned previous=gcn_cost_enter(GCN_COST_DSP);
    const bool ok=size == 2 ? write16(d, offset, (u16)value) :
        write16(d, offset, (u16)(value >> 16)) && write16(d, offset+2, (u16)value);
    gcn_cost_leave(previous);
    return ok;
}
bool wii_dsp_tick(WiiDsp* d, u32 cpu_cycles,u32 audio_period) {
    if(audio_period && d->output_sink && d->output_rate!=(u32)(729000000ull*8/audio_period)) {
        fprintf(stderr,"host audio sample-rate change unsupported rate=%u\n",(unsigned)(729000000ull*8/audio_period));return false;
    }
    if(audio_period && !gcn_dsp_aid_advance(&d->registers,d->cpu,cpu_cycles,audio_period)) {
        fprintf(stderr,"invalid audio DMA address=%08x\n",d->registers.aid_cur_addr);return false;
    }
    gcn_dsp_aram_advance(&d->registers,cpu_cycles);
    u64 cycles=(u64)cpu_cycles+d->cycle_phase;
    d->cycle_phase=(u32)(cycles%6);
    return !d->engine || !cycles || cycles < 6 || gcn_dsp_engine_run(d->engine, (u32)(cycles/6));
}
