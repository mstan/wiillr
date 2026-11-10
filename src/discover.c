/* SPDX-License-Identifier: MIT */
/* Bounded original-code runner. Discovery cannot qualify native CPU acceptance;
 * the separately built static PPC target stops on a miss. Native IOS reports its
 * identity and stops at operations it does not implement. */
#include "wiillr/dol.h"
#include "wiillr/machine.h"
#include "wiillr/boot.h"
#include "wiillr/gx.h"
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

int main(int argc, char** argv) {
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
    if (errno || *end || !limit || limit > 5000000000ull) return 2;
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
        !wii_ios4_boot_memory(&cpu) ||
        !wii_dol_load(&cpu, image, got, &entry)) {
        fprintf(stderr, "invalid or unsupported title image\n");
        cpu_free(&cpu); free(image); return 2;
    }
    free(image);
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
#if WIILLR_PPC_STATIC
    wii_native_install();
#endif
    u64 steps = 0, exceptions = 0;
    while (
#if WIILLR_PPC_STATIC
           cpu.cycles < limit &&
#else
           steps < limit &&
#endif
           !cpu.halted && (!stop || cpu.pc != stop)) {
        u32 cia = cpu.pc;
#if WIILLR_PPC_STATIC
        cpu.cycle_deadline=cpu.cycles+64;
        if(!wii_native_call(&cpu,cia)) {
            gcn_interpreter_note_native_miss(&cpu);
            fprintf(stderr,"static PPC miss pc=%08x cycles=%llu\n",cia,(unsigned long long)cpu.cycles);
            cpu.halted=true;break;
        }
#else
        gcn_interpreter_note_native_miss(&cpu);
        if (!gcn_interpreter_step(&cpu)) break;
#endif
        ++steps;
        wii_machine_tick(&machine, &cpu);
        if(!wii_gx_tick(&gx)) {fprintf(stderr,"native GX failed\n");cpu.halted=true;}
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
    printf("{\"audio_dma_frames\":%llu,\"audio_dma_nonzero_bytes\":%llu,\"audio_output\":\"unattached\"}\n",
           (unsigned long long)machine.dsp.audio_frames,(unsigned long long)machine.dsp.audio_nonzero_bytes);
    if (machine.dsp.engine) {
        GcnDspEngineStats stats=gcn_dsp_engine_stats(machine.dsp.engine);
        printf("{\"dsp_provider\":%u,\"native\":%llu,\"interpreter\":%llu,\"misses\":%llu,\"failed\":%u}\n",
               stats.provider,(unsigned long long)stats.native_instructions,
               (unsigned long long)stats.interpreter_instructions,(unsigned long long)stats.misses,stats.failed);
        wii_dsp_stop(&machine.dsp);
    }
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
    const char* execution="static-native-PPC";
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
