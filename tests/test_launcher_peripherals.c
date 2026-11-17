/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/launcher_peripherals.h"
#include "wiillr/peripherals.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static CPUState cpu;static WiiIpc ipc;static WiiIosNative ios;static u32 next_message=0x4000,last_message;
static void word(u32 p,u32 x){mem_write32_cia(&cpu,p,x,0);}
static void retire(u32 x){wii_ipc_write(&ipc,4,x,cpu.cycles);wii_ipc_write(&ipc,0x30,0x40000000,cpu.cycles);}
static s32 request(u32 cmd,u32 fd,u32 op,u32 a,u32 as,u32 b,u32 bs){
 last_message=next_message;next_message+=64;CHECK(next_message<0x10000);memset(cpu.ram+last_message,0,64);word(last_message,cmd);word(last_message+8,fd);word(last_message+12,op);word(last_message+16,a);word(last_message+20,as);word(last_message+24,b);word(last_message+28,bs);wii_ipc_write(&ipc,0,last_message,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==2);retire(0x32);cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu) && ipc.y==1 && ipc.arm_message==last_message);const s32 result=(s32)mem_read32_cia(&cpu,last_message+4,0);retire(0x34);return result;
}
static s32 open_disk(void){strcpy((char*)cpu.ram+0x1000,"/dev/usb/oh0/1209/5741");return request(1,0,0x1000,0,0,0,0);}
static s32 bulk(u32 fd,u8 endpoint,u32 size){cpu.ram[0x21000]=endpoint;cpu.ram[0x21020]=(u8)(size>>8);cpu.ram[0x21021]=(u8)size;word(0x20000,0x21000);word(0x20004,1);word(0x20008,0x21020);word(0x2000c,2);word(0x20010,0x24000);word(0x20014,size);return request(7,fd,1,2,1,0x20000,0);}
static void cbw(u32 bytes,int input,u8 command){u8* p=cpu.ram+0x24000;memset(p,0,31);memcpy(p,"USBC",4);p[4]=7;for(int n=0;n<4;n++)p[8+n]=(u8)(bytes>>(n*8));p[12]=input?0x80:0;p[14]=10;p[15]=command;if(command==0x2a){p[20]=2;p[23]=1;}}
static s32 disk_capacity(u32 fd){cbw(8,1,0x25);s32 r=bulk(fd,2,31);if(r!=31)return r;r=bulk(fd,0x81,8);if(r!=8)return r;const u32 size=mem_read32_cia(&cpu,0x24004,0);CHECK(bulk(fd,0x81,13)==13 && cpu.ram[0x2400c]==0);return (s32)size;}
static void write_sector(u32 fd,int readonly){cbw(512,0,0x2a);CHECK(bulk(fd,2,31)==31);memset(cpu.ram+0x24000,0x5a,512);CHECK(bulk(fd,2,512)==512);CHECK(bulk(fd,0x81,13)==13 && cpu.ram[0x2400c]==readonly);}
static void image(const char* path){FILE* f=fopen(path,"wb");CHECK(f && !fseek(f,2*1024*1024-1,SEEK_SET) && fputc(0,f)!=EOF && !fclose(f));}
static void check_sector(const char* path,int written){FILE* f=fopen(path,"rb");CHECK(f && !fseek(f,1024,SEEK_SET));for(int i=0;i<512;i++)CHECK(fgetc(f)==(written?0x5a:0));CHECK(!fclose(f));}
static RecompLauncherCPeripheralConfig selection(int kind,int readonly,const char* path){RecompLauncherCPeripheralConfig c={0};c.kind=kind;c.read_only=readonly;if(path)CHECK(SDL_snprintf(c.image_path,sizeof c.image_path,"%s",path)<(int)sizeof c.image_path);return c;}
/* This is the public 4f93 UI contract: model storage is committed only after
 * the provider accepts the edit. No renderer or input subsystem is started. */
static int choose(RecompLauncherCGameInfo* g,int slot,RecompLauncherCPeripheralConfig c){RecompLauncherCPeripheral* p=&g->peripherals[slot];p->error[0]=0;const int ok=g->peripheral_apply(g->peripheral_ctx,slot,&c,p->error,sizeof p->error);if(ok){p->config=c;p->ready=1;}return ok;}
static void join(char* out,size_t n,const char* directory,const char* leaf){CHECK(SDL_snprintf(out,n,"%s/%s",directory,leaf)<(int)n);}
int main(int argc,char** argv){
 CHECK(argc==2);char directory[1024],second[1024],a[1024],b[1024],sd[1024],missing1[1024],missing2[1024],cfg[1200],saved[1200],text[4096];
 CHECK(SDL_snprintf(directory,sizeof directory,"%s/launcher-peripherals-%llu",argv[1],(unsigned long long)SDL_GetPerformanceCounter())<(int)sizeof directory);CHECK(SDL_CreateDirectory(directory));join(a,sizeof a,directory,"USB A.img");join(b,sizeof b,directory,"USB B.img");join(sd,sizeof sd,directory,"SD card.img");image(a);image(b);image(sd);join(missing1,sizeof missing1,directory,"missing1.img");join(missing2,sizeof missing2,directory,"missing2.img");join(cfg,sizeof cfg,directory,"peripherals.cfg");
 RecompLauncherCGameInfo game={0};CHECK(wii_launcher_peripherals_prepare(&game,directory));CHECK(game.num_peripherals==3 && game.peripherals[0].config.kind==1 && game.peripherals[1].config.kind==0 && game.peripherals[2].config.kind==0);CHECK(game.peripherals[0].ready && game.peripherals[1].ready && game.peripherals[2].ready);
 CHECK(!choose(&game,1,selection(1,1,NULL)) && game.peripherals[1].config.kind==0);CHECK(choose(&game,1,selection(2,1,a)));size_t before_size;char* before=SDL_LoadFile(cfg,&before_size);CHECK(before && strstr(before,"USB2 2 1 "));SDL_free(before);
 CHECK(wii_launcher_peripherals_prepare(&game,directory) && game.peripherals[1].config.kind==2 && !strcmp(game.peripherals[1].config.image_path,a));CHECK(cpu_init(&cpu));wii_ipc_init(&ipc);wii_ios_native_init(&ios);CHECK(wii_launcher_peripherals_bind(&ios));s32 disk=open_disk();CHECK(disk>=0 && disk_capacity((u32)disk)==512);
 before=SDL_LoadFile(cfg,&before_size);CHECK(before);CHECK(!choose(&game,1,selection(2,1,missing1)));CHECK(!choose(&game,2,selection(1,1,a)));size_t after_size;char* after=SDL_LoadFile(cfg,&after_size);CHECK(after && after_size==before_size && !memcmp(before,after,before_size));SDL_free(before);SDL_free(after);CHECK(disk_capacity((u32)disk)==512);
 write_sector((u32)disk,1);check_sector(a,0);CHECK(choose(&game,1,selection(2,0,a)));CHECK(disk_capacity((u32)disk)==512);write_sector((u32)disk,0);check_sector(a,1);CHECK(choose(&game,1,selection(0,1,NULL)));CHECK(disk_capacity((u32)disk)==-6);
 wii_launcher_peripherals_unbind();wii_ios_native_destroy(&ios);join(second,sizeof second,directory,"repair");CHECK(SDL_CreateDirectory(second));join(cfg,sizeof cfg,second,"peripherals.cfg");const int n=SDL_snprintf(text,sizeof text,"USB1 0 1 \nUSB2 2 1 %s\nSD 1 1 %s\n",missing1,missing2);CHECK(n>0 && SDL_SaveFile(cfg,text,(size_t)n));CHECK(wii_launcher_peripherals_prepare(&game,second));CHECK(!game.peripherals[1].ready && !game.peripherals[2].ready);
 CHECK(choose(&game,1,selection(2,1,b)) && game.peripherals[1].ready && !game.peripherals[2].ready);CHECK(choose(&game,2,selection(1,1,sd)));CHECK(wii_launcher_peripherals_prepare(&game,second) && game.peripherals[1].ready && game.peripherals[2].ready);CHECK(game.peripherals[0].config.kind==0 && !strcmp(game.peripherals[1].config.image_path,b) && !strcmp(game.peripherals[2].config.image_path,sd));wii_ios_native_init(&ios);wii_ipc_init(&ipc);CHECK(wii_launcher_peripherals_bind(&ios));disk=open_disk();CHECK(disk>=0 && disk_capacity((u32)disk)==512);
 /* Actual SDL rename failure after a valid edit must preserve the live
  * attachment AND its existing guest descriptor, not just copied labels. */
 join(saved,sizeof saved,second,"peripherals.saved.cfg");CHECK(SDL_RenamePath(cfg,saved) && SDL_CreateDirectory(cfg));const RecompLauncherCPeripheralConfig old=game.peripherals[1].config;CHECK(!choose(&game,1,selection(2,1,a)));CHECK(!memcmp(&old,&game.peripherals[1].config,sizeof old));const s32 capacity=disk_capacity((u32)disk);fprintf(stderr,"save-failure existing USB descriptor capacity=%d expected=512\n",capacity);CHECK(capacity==512);
 wii_launcher_peripherals_unbind();wii_ios_native_destroy(&ios);cpu_free(&cpu);puts("launcher peripheral persistence/real backend/rollback contracts passed");return 0;
}
