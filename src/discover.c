/* SPDX-License-Identifier: MIT */
/* Bounded original-code runner. Discovery cannot qualify native CPU acceptance;
 * the separately built static PPC target stops on a miss. Native IOS reports its
 * identity and stops at operations it does not implement. */
#include "wiillr/dol.h"
#include "wiillr/machine.h"
#include "wiillr/boot.h"
#include "wiillr/gx.h"
#include "wiillr/gx_output.h"
#include "wiillr/control.h"
#ifdef RECOMP_LAUNCHER
#include "wiillr/launcher.h"
#include "wiillr/package.h"
#include <SDL3/SDL.h>
#ifdef _WIN32
/* Reuse SDL's supported Windows entry shim for the GUI subsystem. Explicit
 * command-line QA still receives its inherited stdout/stderr pipes. */
#undef SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#endif
#endif
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
static bool validate_launch(void* user,const WiiTitleLaunch* launch) {
    u32 entry;
    return launch->ios>>32==1 && (launch->ios==0x100000004ull ||
        launch->ios==0x100000025ull || launch->ios==0x100000050ull) &&
        wii_dol_validate(user,launch->image,launch->size,&entry);
}
static bool bootstrap_launch(CPUState* cpu,WiiMachine* m,WiiGx* gx,const char* profile,
    const char* library,const char* data,const char* cache,const u8* irom,const u8* coef) {
    WiiTitleLaunch launch=m->ios.launch;memset(&m->ios.launch,0,sizeof m->ios.launch);
    const u64 cycles=cpu->cycles,timebase=cpu->timebase,fields=m->fields;
    const u64 reads=m->mmio_reads,writes=m->mmio_writes,operations=m->ios.operations+1;
    const u64 gather_writes=m->gx_cpu_gather_writes,gather_bytes=m->gx_cpu_gather_bytes;
    const u64 gather_rejected=m->gx_cpu_gather_rejected;
    const u64 utc=m->ios.kd_utc,utc_cycles=m->ios.kd_utc_cycles;
    const bool utc_set=m->ios.kd_utc_set;
    void (*sink)(void*,const u8*,u32)=m->dsp.output_sink;
    void* sink_user=m->dsp.output_user;const u32 rate=m->dsp.output_rate;
    const bool dsp=m->dsp.engine!=NULL;
    if(gx->api && !gx->api->drain()){wii_es_release_launch(&launch);return false;}
    GcnllrNativeGxStatus graphics={0};if(gx->api)gx->api->status(&graphics);
    GcnDspEngineStats sound={0};if(dsp)sound=gcn_dsp_engine_stats(m->dsp.engine);
    printf("{\"native_title_retire\":1,\"cycles\":%llu,\"gx_fifo_bytes\":%llu,"
           "\"gx_cpu_gather_writes\":%llu,\"gx_cpu_gather_rejected\":%llu,\"gx_failed\":%u,"
           "\"dsp_interpreter\":%llu,\"dsp_misses\":%llu,\"dsp_failed\":%u}\n",
        (unsigned long long)cycles,(unsigned long long)graphics.fifo_bytes,
        (unsigned long long)gather_writes,(unsigned long long)gather_rejected,graphics.failed,
        (unsigned long long)sound.interpreter_instructions,(unsigned long long)sound.misses,sound.failed);
    /* The old title never receives a success reply: ES_Launch bootstraps PPC.
     * Retire all old GPU ownership before replacing RAM, descriptors or DSP. */
    wii_gx_stop(gx);if(cpu->halted){wii_es_release_launch(&launch);return false;}
    wii_dsp_stop(&m->dsp);wii_ios_native_destroy(&m->ios);wii_fs_unmount(m->ios.fs);
    cpu_reset(cpu);cpu->cycles=cycles;cpu->timebase=timebase;cpu->spr[287]=PPC_BROADWAY_PVR;
    u32 entry;
    bool ok=wii_native_ios_boot_memory(cpu,(u32)launch.ios) &&
        wii_dol_load(cpu,launch.image,launch.size,&entry);
    wii_machine_init(m,cpu);
    m->fields=fields;m->mmio_reads=reads;m->mmio_writes=writes;
    m->gx_cpu_gather_writes=gather_writes;m->gx_cpu_gather_bytes=gather_bytes;
    m->gx_cpu_gather_rejected=gather_rejected;
    m->vi.now_cycles=m->vi.last_cycles=m->vi.ticks_last_line_start=cycles;
    m->ios.operations=operations;m->ios.kd_utc=utc;m->ios.kd_utc_cycles=utc_cycles;m->ios.kd_utc_set=utc_set;
    m->ios.validate_launch=validate_launch;m->ios.launch_user=cpu;
    m->ios.fs=wii_fs_mount(profile,launch.uid,launch.gid);ok=ok && m->ios.fs;
    if(ok && library)ok=wii_gx_start(gx,m,cpu,library,data,cache);
#ifdef RECOMP_LAUNCHER
    if(ok && library)ok=wii_launcher_window(gx);
#endif
    if(ok && dsp)ok=wii_dsp_start(&m->dsp,cpu,irom,coef);
    m->dsp.output_sink=sink;m->dsp.output_user=sink_user;m->dsp.output_rate=rate;
#if WIILLR_PPC_STATIC
    if(ok)wii_native_install();
#endif
    if(ok)cpu->pc=entry;
    printf("{\"native_title_bootstrap\":\"%016llx\",\"ios\":%u,\"cycles\":%llu,\"success\":%s}\n",
        (unsigned long long)launch.title,(u32)launch.ios,(unsigned long long)cycles,ok?"true":"false");
    fflush(stdout);
    wii_es_release_launch(&launch);return ok;
}
#ifdef RECOMP_LAUNCHER
static WiiPackage* package;
static char package_error[512];
static void close_package(void){wii_package_close(package);package=NULL;}
static bool package_validate(const char* image,const char* nand,unsigned uid,unsigned gid) {
    char selected[32768];return wii_launcher_start(image,nand,uid,gid,false,true,selected,sizeof selected)==0;
}
static bool package_request(int argc,char** argv) {
    return argc==1 || (argc==2 && (!strcmp(argv[1],"--launcher") ||
        !strcmp(argv[1],"--launcher-settings") || !strcmp(argv[1],"--launcher-check")));
}
#endif
static int run(int argc, char** argv) {
    bool launcher=false,launcher_force=false,launcher_check=false;
#ifdef RECOMP_LAUNCHER
    if(package_request(argc,argv)) {
        const char* action=argc==1?"--launcher":argv[1];
        const bool check=!strcmp(action,"--launcher-check");
        const char* user=getenv("WIILLR_USER_DIR");char* allocated=NULL;
        if(!user || !*user){allocated=SDL_GetPrefPath("RetroPortingToolKit","Mega Man 9");user=allocated;}
        package=wii_package_open(SDL_GetBasePath(),user,package_validate,package_error,sizeof package_error);
        SDL_free(allocated);
        if(!package){fprintf(stderr,"launcher: %s\n",package_error);return 2;}
        atexit(close_package);
        if(!check) {
            FILE* log=fopen(wii_package_log(package),"wb");
            if(!log){snprintf(package_error,sizeof package_error,"Cannot create the game log in the preferences folder");return 2;}
            fclose(log);
            if(!freopen(wii_package_log(package),"ab",stdout) || !freopen(wii_package_log(package),"ab",stderr))return 2;
        }
        wii_package_arguments(package,argv[0],action,&argc,&argv);
    }
    if(argc>1 && (!strcmp(argv[1],"--launcher") ||
        !strcmp(argv[1],"--launcher-settings") || !strcmp(argv[1],"--launcher-check"))) {
        launcher=true;launcher_force=!strcmp(argv[1],"--launcher-settings");
        launcher_check=!strcmp(argv[1],"--launcher-check");
        for(int i=1;i<argc;++i)argv[i]=argv[i+1];
        --argc;
    }
#endif
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
        fprintf(stderr,"usage: wiillr-native [--launcher|--launcher-settings|--launcher-check] ORIGINAL-DOL MAX-CYCLES MEM1-OUTPUT STOP-PC [DSP-ROM DSP-COEF] [NAND-PROFILE UID GID] [--gx LIB DATA CACHE] [--ios-profile 4|37|80]\n");
#else
        fprintf(stderr, "usage: wiillr-discover ORIGINAL-DOL MAX-STEPS MEM1-OUTPUT STOP-PC [DSP-ROM DSP-COEF] [NAND-PROFILE UID GID]\n"
                        "Discovery only; all guest instructions are interpreted. STOP-PC=0 disables.\n");
#endif
        return 2;
    }
    char* end;
    errno = 0;
    u64 limit = strtoull(argv[2], &end, 0);
    const char* visible_window=getenv("WIILLR_WINDOW");
    /* Allow a visible interactive session up to 24 hours of Broadway time. */
    const u64 maximum_limit=launcher || (visible_window && !strcmp(visible_window,"1")) ?
        62985600000000ull:500000000000ull;
    if (errno || *end || !limit || limit > maximum_limit) return 2;
    errno = 0;
    unsigned long stop = strtoul(argv[4], &end, 0);
    if (errno || *end || stop > 0xfffffffful || (stop & 3)) return 2;
#ifdef RECOMP_LAUNCHER
    char launcher_image[32768];
    if(launcher) {
        if((argc!=8 && argc!=10) || (!launcher_check && !gx_library))return 2;
        errno=0;unsigned long uid=strtoul(argv[argc-2],&end,10);
        if(errno || !*argv[argc-2] || *end || uid>0xfffffffful)return 2;
        errno=0;unsigned long gid=strtoul(argv[argc-1],&end,10);
        if(errno || !*argv[argc-1] || *end || gid>0xfffful)return 2;
        int action=wii_launcher_start(argv[1],argv[argc-3],(unsigned)uid,(unsigned)gid,
            launcher_force,launcher_check,launcher_image,sizeof launcher_image);
        if(action || launcher_check)return action==1?0:action;
        argv[1]=launcher_image;
    }
#endif
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
#ifdef RECOMP_LAUNCHER
    if(launcher && (got!=(size_t)length || !wii_launcher_verify(image,got))) {
        fprintf(stderr,"launcher: boot content changed after selection\n");free(image);return 2;
    }
#endif
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
        machine.ios.validate_launch=validate_launch;machine.ios.launch_user=&cpu;
#else
        fprintf(stderr,"filesystem needs the build-selected native IOS provider\n");cpu_free(&cpu);return 2;
#endif
    }
    WiiGx gx={0};
    if(gx_library) {
#if WIILLR_GX_NATIVE || WIILLR_GX_SOFTWARE
        if(!wii_gx_start(&gx,&machine,&cpu,gx_library,gx_data,gx_cache)
#ifdef RECOMP_LAUNCHER
            || !wii_launcher_window(&gx)
#endif
           ) {
            fprintf(stderr,"native GX start failed\n");wii_gx_stop(&gx);wii_fs_unmount(machine.ios.fs);cpu_free(&cpu);return 2;
        }
#else
        fprintf(stderr,"GX needs a build-selected provider\n");wii_fs_unmount(machine.ios.fs);cpu_free(&cpu);return 2;
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
            fprintf(stderr,"static PPC miss pc=%08x cycles=%llu lr=%08x ctr=%08x sp=%08x r3=%08x r4=%08x r12=%08x r30=%08x r31=%08x\n",cia,(unsigned long long)cpu.cycles,cpu.lr,cpu.ctr,cpu.gpr[1],cpu.gpr[3],cpu.gpr[4],cpu.gpr[12],cpu.gpr[30],cpu.gpr[31]);
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
        if(machine.ios.launch_pending && !cpu.halted) {
            if(!bootstrap_launch(&cpu,&machine,&gx,argv[argc-3],gx_library,gx_data,gx_cache,irom,coef)) {
                fprintf(stderr,"native title bootstrap failed\n");cpu.halted=true;
            }
            input_pending=true;
            continue;
        }
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
    /* Kernel file I/O cannot fault guarded resident pages back into RAM.
     * A complete snapshot is an explicit CPU observer of every MEM1 byte. */
    const bool dump_requested=argv[3] && *argv[3];
    int saved = !dump_requested || (wii_gx_output_prepare(&gx,0,cpu.ram_size) && dump(argv[3], cpu.ram, cpu.ram_size));
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
        printf("{\"gx_fifo_bytes\":%llu,\"gx_draws\":%llu,\"gx_display_copies\":%llu,\"gx_presents\":%llu,\"gx_failed\":%u,\"gx_cpu_gather_writes\":%llu,\"gx_cpu_gather_bytes\":%llu,\"gx_cpu_gather_rejected\":%llu,\"gx_native_owner\":\"%s\"}\n",
            (unsigned long long)status.fifo_bytes,(unsigned long long)status.draws,
            (unsigned long long)status.display_copies,(unsigned long long)status.presents,status.failed,
            (unsigned long long)machine.gx_cpu_gather_writes,(unsigned long long)machine.gx_cpu_gather_bytes,
            (unsigned long long)machine.gx_cpu_gather_rejected,machine.gx_native_owner?machine.gx_native_owner:"");
        if(dump_requested && gx.scanout_width && gx.scanout_height) {
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
           stop && cpu.pc == stop ? "true":"false", dump_requested && saved ? "true":"false");
    gcn_exi_free(&machine.exi);
    gcn_di_free(&machine.di);
    cpu_free(&cpu);
    return !saved ? 2 : cpu.halted ? 1 : 0;
}
int main(int argc,char** argv) {
#if WIILLR_IOS_LLE
    /* The native title handoff cannot stand in for original kernel startup. */
    fprintf(stderr,"Original IOS requires a firmware context; the native NAND-title handoff is unavailable in this build.\n");
    return 2;
#endif
    const int result=run(argc,argv);
#ifdef RECOMP_LAUNCHER
    const char* hidden=getenv("GCNLLR_RENDER_HIDDEN");const char* test=getenv("LNG_TEST_HIDDEN");
    const bool check=argc==2 && !strcmp(argv[1],"--launcher-check");
    if(result && package_request(argc,argv) && !check &&
       !(hidden && !strcmp(hidden,"1")) && !(test && !strcmp(test,"1")))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Mega Man 9",
            *package_error?package_error:"Mega Man 9 could not start. See game.log in the game preferences folder for details.",NULL);
#endif
    return result;
}
