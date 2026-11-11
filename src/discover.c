/* SPDX-License-Identifier: MIT */
/* Bounded original-code runner. Discovery cannot qualify native CPU acceptance;
 * the separately built static PPC target stops on a miss. Native IOS reports its
 * identity and stops at operations it does not implement. */
#include "wiillr/dol.h"
#include "wiillr/machine.h"
#include "wiillr/boot.h"
#include "wiillr/gx.h"
#include "wiillr/control.h"
#include "debug/cost_ring.h"
#include "host/host_audio.h"
#include "memory/memory.h"
#include "cpu/interpreter.h"
#if WIILLR_PPC_STATIC
#include "wiillr/native.h"
#endif
#include "debug/rings.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static int dump(const char* path, const void* bytes, size_t size) {
    /* The Windows CRT used by MinGW does not support fopen's C11 x mode. */
#ifdef _WIN32
    int fd = _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    FILE* stream = fd < 0 ? NULL : _fdopen(fd, "wb");
    if (fd >= 0 && !stream) _close(fd);
#else
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0666);
    FILE* stream = fd < 0 ? NULL : fdopen(fd, "wb");
    if (fd >= 0 && !stream) close(fd);
#endif
    if (!stream) return 0;
    int ok = fwrite(bytes, 1, size, stream) == size;
    if (fclose(stream)) ok = 0;
    return ok;
}

typedef struct {u64 at;WiiBluetoothInput input;} InputEvent;
static int load_inputs(InputEvent* events,unsigned capacity) {
    const char* path=getenv("WIILLR_INPUT_SCRIPT");if(!path)return 0;
    FILE* file=fopen(path,"rb");if(!file)return -1;
    unsigned count=0;char line[192];bool valid=true;
    while(fgets(line,sizeof line,file)) {
        if(line[0]=='#' || line[0]=='\n' || line[0]=='\r')continue;
        unsigned long long at;unsigned buttons,present=1,pairing=0;char extra;
        const int fields=sscanf(line,"%llu %x %u %u %c",&at,&buttons,&present,&pairing,&extra);
        if((fields<2 || fields>4) || count==capacity || present>1 || pairing>1 || (buttons&~0x9f1fu) ||
           (count && at<events[count-1].at)){valid=false;break;}
        events[count++]=(InputEvent){at,{(u16)buttons,{512,512,616},512,384,false,present!=0,pairing!=0}};
    }
    if(ferror(file))valid=false;fclose(file);return valid?(int)count:-1;
}
int main(int argc, char** argv) {
    InputEvent inputs[1024];const int input_count=load_inputs(inputs,1024);
    if(input_count<0){fprintf(stderr,"invalid virtual remote input script\n");return 2;}
    u64 sync_at=0;bool sync_pending=false;
    const char* sync_text=getenv("WIILLR_SYNC_CYCLES");
    if(sync_text) {
        char* tail;errno=0;sync_at=strtoull(sync_text,&tail,10);
        if(errno || !*sync_text || *tail){fprintf(stderr,"invalid Sync-button cycle\n");return 2;}
        sync_pending=true;
    }
    u32 ios_profile=4;
    if(argc>=3 && !strcmp(argv[argc-2],"--ios-profile")) {
        if(!strcmp(argv[argc-1],"4"))ios_profile=4;
        else if(!strcmp(argv[argc-1],"37") && WIILLR_IOS_NATIVE)ios_profile=37;
        else if(!strcmp(argv[argc-1],"80") && WIILLR_IOS_NATIVE)ios_profile=80;
        else return 2;
        argc-=2;
    }
    const char *gx_library=NULL,*gx_data=NULL,*gx_cache=NULL;
    if(argc>=9 && !strcmp(argv[argc-4],"--gx")) {
        gx_library=argv[argc-3];gx_data=argv[argc-2];gx_cache=argv[argc-1];argc-=4;
    }
    if (argc != 5 && argc != 7 && argc != 8 && argc != 10) {
#if WIILLR_PPC_STATIC
        fprintf(stderr,"usage: wiillr-native ORIGINAL-DOL MAX-CYCLES MEM1-OUTPUT STOP-PC [DSP-ROM DSP-COEF] [NAND-PROFILE UID GID] [--gx LIB DATA CACHE]\n");
#else
        fprintf(stderr, "usage: wiillr-discover ORIGINAL-DOL MAX-STEPS MEM1-OUTPUT STOP-PC [DSP-ROM DSP-COEF] [NAND-PROFILE UID GID]\n"
                        "Discovery only; all guest instructions are interpreted. STOP-PC=0 disables.\n");
#endif
        return 2;
    }
    char* end;
    errno = 0;
    u64 limit = strtoull(argv[2], &end, 0);
    if (errno || *end || !limit || limit > 500000000000ull) return 2;
    errno = 0;
    unsigned long stop = strtoul(argv[4], &end, 0);
    if (errno || *end || stop > 0xfffffffful || (stop & 3)) return 2;
    FILE* input = fopen(argv[1], "rb");
    if (!input) return 2;
    if (fseek(input, 0, SEEK_END)) { fclose(input); return 2; }
    long length = ftell(input);
    if (length < 0x100 || length > 0x5800000 || fseek(input, 0, SEEK_SET)) {
        fclose(input); return 2;
    }
    u8* image = malloc((size_t)length);
    if (!image) { fclose(input); return 2; }
    size_t got = fread(image, 1, (size_t)length, input);
    fclose(input);
    CPUState cpu;
    if (!cpu_init(&cpu)) { free(image); return 2; }
    u32 entry;
    if (got != (size_t)length || !cpu_alloc_mem2(&cpu, WII_MEM2_SIZE) ||
        !wii_native_ios_boot_memory(&cpu,ios_profile) ||
        !wii_dol_load(&cpu, image, got, &entry)) {
        fprintf(stderr, "invalid or unsupported title image\n");
        cpu_free(&cpu); free(image); return 2;
    }
    free(image);
    printf("{\"boot_ios_profile\":%u,\"original_ARM_boot\":false}\n",ios_profile);
    gcn_rings_init();
    gcn_interpreter_capture_begin();
    /* IOS NAND-title handoff: physical entry, translation disabled. The
     * original entry stub initializes the PPC; no captured state is restored. */
    cpu.pc = entry;
    cpu.spr[287] = PPC_BROADWAY_PVR;
    WiiMachine machine;
    wii_machine_init(&machine, &cpu);
    if(argc==8 || argc==10) {
        errno=0;unsigned long uid=strtoul(argv[argc-2],&end,10);
        if(errno || *end || uid>0xfffffffful) {cpu_free(&cpu);return 2;}
        errno=0;unsigned long gid=strtoul(argv[argc-1],&end,10);
        if(errno || *end || gid>0xfffful) {cpu_free(&cpu);return 2;}
#if WIILLR_IOS_NATIVE
        machine.ios.fs=wii_fs_mount(argv[argc-3],(u32)uid,(u16)gid);
        if(!machine.ios.fs) {fprintf(stderr,"invalid copied NAND profile\n");cpu_free(&cpu);return 2;}
#else
        fprintf(stderr,"filesystem needs the build-selected native IOS provider\n");cpu_free(&cpu);return 2;
#endif
    }
    WiiGx gx={0};
    if(gx_library) {
#if WIILLR_GX_NATIVE
        if(!wii_gx_start(&gx,&machine,&cpu,gx_library,gx_data,gx_cache)) {
            fprintf(stderr,"native GX start failed\n");wii_fs_unmount(machine.ios.fs);cpu_free(&cpu);return 2;
        }
#else
        fprintf(stderr,"GX needs the build-selected native provider\n");wii_fs_unmount(machine.ios.fs);cpu_free(&cpu);return 2;
#endif
    }
    u8 irom[8192], coef[4096];
    if (argc == 7 || argc==10) {
        FILE* rom=fopen(argv[5], "rb");
        FILE* co=fopen(argv[6], "rb");
        bool loaded=rom && co && fread(irom,1,sizeof irom,rom)==sizeof irom &&
            fread(coef,1,sizeof coef,co)==sizeof coef;
        if (rom) fclose(rom);
        if (co) fclose(co);
        if (!loaded || !wii_dsp_start(&machine.dsp,&cpu,irom,coef)) {wii_gx_stop(&gx);wii_fs_unmount(machine.ios.fs);cpu_free(&cpu);return 2;}
    }
    gcn_interpreter_set_cache_loop_batch_enabled(false);
    if(machine.dsp.engine && gcn_host_audio_start(32000u)) {
        machine.dsp.output_sink=gcn_host_audio_submit_be_s16;machine.dsp.output_rate=32000u;
    }
#if WIILLR_PPC_STATIC
    wii_native_install();
    const GcnOverlayModule* native_module=wii_native_module();
#else
    const GcnOverlayModule* native_module=NULL;
#endif
    u64 steps = 0, exceptions = 0;unsigned input_next=0;bool input_pending=false;
    WiiBluetoothInput input_state={0};
    input_state.accel[0]=input_state.accel[1]=512;input_state.accel[2]=616;
    WiiControl* control=wii_control_start(&cpu,&machine,&gx,&input_state,&input_pending,native_module);
    if(getenv("WIILLR_CONTROL_PORT") && !control){wii_dsp_stop(&machine.dsp);wii_gx_stop(&gx);wii_fs_unmount(machine.ios.fs);cpu_free(&cpu);return 2;}
#if GCN_COST_RING_ENABLED
    gcn_cost_init(cpu.cycles);
#endif
    while (
#if WIILLR_PPC_STATIC
           cpu.cycles < limit &&
#else
           steps < limit &&
#endif
           !cpu.halted && (!stop || cpu.pc != stop)) {
        u32 cia = cpu.pc;
        if(!wii_control_poll(control))continue;
        if(wii_control_quit_requested(control))break;
        if(sync_pending && cpu.cycles>=sync_at && machine.ios.bluetooth) {
            if(!wii_bluetooth_press_sync(machine.ios.bluetooth)){cpu.halted=true;break;}
            fprintf(stderr,"console Sync button cycles=%llu\n",(unsigned long long)cpu.cycles);
            sync_pending=false;
        }
        while(input_next<(unsigned)input_count && inputs[input_next].at<=cpu.cycles) {
            input_state=inputs[input_next++].input;input_pending=true;
        }
        if(input_pending && machine.ios.bluetooth) {
            if(!wii_bluetooth_set_input(machine.ios.bluetooth,&input_state)){cpu.halted=true;break;}
            fprintf(stderr,"virtual remote input cycles=%llu buttons=%04x present=%u\n",
                    (unsigned long long)cpu.cycles,input_state.buttons,input_state.present);
            input_pending=false;
        }
#if WIILLR_PPC_STATIC
#if GCN_COST_RING_ENABLED
        gcn_cost_begin_turn(cpu.cycles,read_be32(cpu.ram+0xe4));
        gcn_cost_owner_boundary(GCN_COST_OWNER_EXECUTION);
        const unsigned turn_scope=gcn_cost_enter(GCN_COST_DISPATCH);
        const u64 field_before=machine.fields;
#endif
        cpu.cycle_deadline=wii_control_deadline(control,wii_machine_deadline(&machine,cpu.cycles));
#if WIILLR_SCHED_DEADLINE
        if(limit<cpu.cycle_deadline)cpu.cycle_deadline=limit;
        if(input_next<(unsigned)input_count && inputs[input_next].at<cpu.cycle_deadline)
            cpu.cycle_deadline=inputs[input_next].at;
        if(sync_pending && sync_at<cpu.cycle_deadline)cpu.cycle_deadline=sync_at;
#endif
        do {
#if GCN_COST_RING_ENABLED
        gcn_cost_thread_boundary(cpu.cycles,read_be32(cpu.ram+0xe4));
#endif
        cia=cpu.pc;
        if(!wii_native_call(&cpu,cia)) {
            gcn_interpreter_note_native_miss(&cpu);
#if WIILLR_PPC_DISCOVERY
            if(!gcn_interpreter_step(&cpu))break;
#else
            fprintf(stderr,"static PPC miss pc=%08x cycles=%llu\n",cia,(unsigned long long)cpu.cycles);
            cpu.halted=true;break;
#endif
        }
        ++steps;
#if WIILLR_SCHED_DEADLINE
        } while(cpu.cycles<cpu.cycle_deadline && !cpu.halted && !cpu.exception &&
                (!stop || cpu.pc!=stop));
#else
        } while(0);
#endif
#else
#if GCN_COST_RING_ENABLED
        gcn_cost_begin_turn(cpu.cycles,read_be32(cpu.ram+0xe4));
        gcn_cost_owner_boundary(GCN_COST_OWNER_EXECUTION);
        const unsigned turn_scope=gcn_cost_enter(GCN_COST_GUEST);
        const u64 field_before=machine.fields;
#endif
        gcn_interpreter_note_native_miss(&cpu);
        if (!gcn_interpreter_step(&cpu)) break;
        ++steps;
#endif
#if GCN_COST_RING_ENABLED
        gcn_cost_owner_boundary(GCN_COST_OWNER_DEVICES);
#endif
        gcn_cost_enter(GCN_COST_TIME);
        wii_machine_tick(&machine, &cpu);
#if GCN_COST_RING_ENABLED
        gcn_cost_owner_boundary(GCN_COST_OWNER_GRAPHICS);
#endif
        gcn_cost_enter(GCN_COST_GRAPHICS);
        if(!wii_gx_tick(&gx)) {fprintf(stderr,"native GX failed\n");cpu.halted=true;}
#if GCN_COST_RING_ENABLED
        gcn_cost_leave(turn_scope);
        gcn_cost_end_turn(cpu.cycles);
        if(machine.fields!=field_before)gcn_cost_finish_field(machine.fields,cpu.cycles);
#endif
        if(gx.quit)break;
        if (cpu.exception) {
            if (exceptions < 8)
                fprintf(stderr, "exception delivered=%08x vector=%08x instruction_pc=%08x\n",
                        cpu.exception, cpu.pc, cia);
            ++exceptions;
            /* This is the dispatcher latch, not architectural SRR/MSR. The
             * guest-installed vector must execute normally, including rfi. */
            cpu.exception = 0;
        }
    }
    int saved = dump(argv[3], cpu.ram, cpu.ram_size);
    wii_control_stop(control);
    printf("{\"ios_provider\":\"%s\",\"native_operations\":%llu,\"ARM_interpreter_instructions\":0,\"stm_event_waiting\":%s}\n",
           WIILLR_IOS_NATIVE ? "native-development":"none",(unsigned long long)machine.ios.operations,
           machine.ios.event_waiting ? "true":"false");
    if (machine.ipc.x & 1) {
        u32 available;
        const u8* command=gcn_mem_resolve(&cpu,machine.ipc.ppc_message,&available);
        if(command && available>=32) {
            printf("{\"ios_command\":%u,\"ios_fd\":%u,\"ios_arg0\":%u,\"ios_arg1\":%u}\n",
                   read_be32(command),read_be32(command+8),read_be32(command+12),read_be32(command+16));
            u32 path_address=read_be32(command+12);
            if(read_be32(command)==1) {
                const u8* path=gcn_mem_resolve(&cpu,path_address,&available);
                if(path && memchr(path,0,available<64 ? available:64))
                    fprintf(stderr,"IOS open path=%s\n",(const char*)path);
            }
        }
    }
    printf("{\"exceptions\":%llu,\"vi_fields\":%llu,\"mmio_reads\":%llu,\"mmio_writes\":%llu,\"last_unsupported\":%u}\n",
           (unsigned long long)exceptions,(unsigned long long)machine.fields,
           (unsigned long long)machine.mmio_reads,(unsigned long long)machine.mmio_writes,
           machine.unsupported_address);
    printf("{\"ave_transactions\":%llu,\"ave_register_writes\":%llu,\"ave_failed\":%s}\n",
           (unsigned long long)machine.ave.transactions,(unsigned long long)machine.ave.writes,
           machine.ave.failed ? "true":"false");
    printf("{\"ppc_msr\":%u,\"ppc_lr\":%u,\"pi_cause\":%u,\"pi_mask\":%u,\"pe_token_pending\":%d,\"pe_finish_pending\":%d,\"gp_staged\":%u,\"cp_distance\":%u,\"vi_control\":%u,\"vi_irq\":%d,\"vi_halfline\":%u,\"vi_di\":[%u,%u,%u,%u]}\n",
           cpu.msr,cpu.lr,machine.pi.reg[0],machine.pi.reg[1],machine.pe.signal_token_interrupt,
           machine.pe.signal_finish_interrupt,machine.gp.count,gcn_cp_fifo_rw_distance(&machine.cp),
           machine.vi.reg[GCN_VI_DCR>>1],machine.vi.irq_level,machine.vi.half_line_count,
           (u32)machine.vi.reg[0x18]<<16|machine.vi.reg[0x19],(u32)machine.vi.reg[0x1a]<<16|machine.vi.reg[0x1b],
           (u32)machine.vi.reg[0x1c]<<16|machine.vi.reg[0x1d],(u32)machine.vi.reg[0x1e]<<16|machine.vi.reg[0x1f]);
    GcnHostAudioStats audio_stats;gcn_host_audio_get_stats(&audio_stats);
    printf("{\"audio_dma_frames\":%llu,\"audio_dma_nonzero_bytes\":%llu,\"audio_output_open\":%s,\"audio_muted\":%s,\"audio_rate\":%u,\"audio_capture_frames\":%llu,\"audio_capture_signal_frames\":%llu,\"audio_capture_peak\":%u,\"audio_capture_failed\":%s}\n",
           (unsigned long long)machine.dsp.audio_frames,(unsigned long long)machine.dsp.audio_nonzero_bytes,
           audio_stats.device_open?"true":"false",audio_stats.output_muted?"true":"false",audio_stats.sample_rate,
           (unsigned long long)audio_stats.capture_frames,(unsigned long long)audio_stats.capture_signal_frames,
           audio_stats.capture_peak,audio_stats.capture_failed?"true":"false");
    gcn_host_audio_stop();
    if (machine.dsp.engine) {
        GcnDspEngineStats stats=gcn_dsp_engine_stats(machine.dsp.engine);
        printf("{\"dsp_provider\":%u,\"native\":%llu,\"interpreter\":%llu,\"misses\":%llu,\"failed\":%u}\n",
               stats.provider,(unsigned long long)stats.native_instructions,
               (unsigned long long)stats.interpreter_instructions,(unsigned long long)stats.misses,stats.failed);
        wii_dsp_stop(&machine.dsp);
    }
    wii_ios_native_destroy(&machine.ios);
    wii_fs_unmount(machine.ios.fs);
    if(gx.api) {
        GcnllrNativeGxStatus status={0};gx.api->drain();gx.api->status(&status);
        printf("{\"gx_fifo_bytes\":%llu,\"gx_draws\":%llu,\"gx_display_copies\":%llu,\"gx_presents\":%llu,\"gx_failed\":%u}\n",
            (unsigned long long)status.fifo_bytes,(unsigned long long)status.draws,
            (unsigned long long)status.display_copies,(unsigned long long)status.presents,status.failed);
        if(gx.scanout_width && gx.scanout_height) {
            const u32 size=gx.scanout_stride*(gx.scanout_height-1)+gx.scanout_width*2;
            u32 available;const u8* pixels=gcn_mem_resolve(&cpu,gx.scanout_address,&available);
            char capture[4096];const int n=snprintf(capture,sizeof capture,"%s/scanout.yuy2",gx_data);
            const int captured=pixels && available>=size && n>0 && n<(int)sizeof capture && dump(capture,pixels,size);
            printf("{\"scanout_address\":%u,\"width\":%u,\"height\":%u,\"stride\":%u,\"scanout_saved\":%s}\n",
                gx.scanout_address,gx.scanout_width,gx.scanout_height,gx.scanout_stride,captured?"true":"false");
        }
    }
    wii_gx_stop(&gx);
#if WIILLR_PPC_STATIC
    wii_native_summary();
#if WIILLR_PPC_DISCOVERY
    const char* execution="static-native-with-discovery";
#else
    const char* execution="static-native-PPC";
#endif
#else
    const char* execution="discovery-interpreter";
#endif
    printf("{\"execution\":\"%s\",\"entry\":%u,\"pc\":%u,"
           "\"steps\":%llu,\"interpreter_instructions\":%llu,\"unique_native_misses\":%llu,"
           "\"cycles\":%llu,\"halted\":%s,\"stop_reached\":%s,\"mem1_saved\":%s}\n",
           execution, entry, cpu.pc, (unsigned long long)steps,
           (unsigned long long)gcn_interpreter_instruction_count(),
           (unsigned long long)gcn_interpreter_unique_miss_count(),
           (unsigned long long)cpu.cycles, cpu.halted ? "true":"false",
           stop && cpu.pc == stop ? "true":"false", saved ? "true":"false");
    gcn_exi_free(&machine.exi);
    gcn_di_free(&machine.di);
    cpu_free(&cpu);
    return !saved ? 2 : cpu.halted ? 1 : 0;
}
