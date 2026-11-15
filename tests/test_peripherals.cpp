/* SPDX-License-Identifier: MIT */
#include "wiillr/peripherals.h"
#include "memory/memory.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x);}while(0)
namespace {
struct Commit {bool accept=false;unsigned calls=0;};
bool commit_selection(void* user,char* error,size_t capacity){auto* commit=static_cast<Commit*>(user);++commit->calls;if(!commit->accept && error && capacity)std::snprintf(error,capacity,"Fixture persistence was refused.");return commit->accept;}
struct Fixture {
 CPUState cpu{};WiiIpc ipc{};WiiIosNative ios{};u32 next=0x4000,last=0;bool waiting=false;
 Fixture(){CHECK(cpu_init(&cpu));cpu.spr[287]=PPC_BROADWAY_PVR;wii_ipc_init(&ipc);wii_ios_native_init(&ios);}
 ~Fixture(){wii_ios_native_destroy(&ios);cpu_free(&cpu);}
 void retire(u32 control){wii_ipc_write(&ipc,4,control,cpu.cycles);wii_ipc_write(&ipc,0x30,0x40000000,cpu.cycles);}
 void word(u32 p,u32 v){mem_write32_cia(&cpu,p,v,0);}
 u32 word(u32 p){return mem_read32_cia(&cpu,p,0);}
 bool start(u32 cmd,u32 fd,u32 op,u32 a,u32 as,u32 b,u32 bs){
  last=next;next+=64;CHECK(next<0x10000);std::memset(cpu.ram+last,0,64);word(last,cmd);word(last+4,0x7f7f7f7f);word(last+8,fd);word(last+12,op);word(last+16,a);word(last+20,as);word(last+24,b);word(last+28,bs);
  wii_ipc_write(&ipc,0,last,cpu.cycles);wii_ipc_write(&ipc,4,0x31,cpu.cycles);CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));CHECK(ipc.y==2);retire(0x32);cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));waiting=ipc.y!=1;if(!waiting){CHECK(ipc.arm_message==last && word(last)==8);retire(0x34);}return !waiting;
 }
 s32 result(){return s32(word(last+4));}
 s32 open(const char* name){std::strcpy(reinterpret_cast<char*>(cpu.ram+0x1000),name);CHECK(start(1,0,0x1000,0,0,0,0));return result();}
 bool ioctl(u32 fd,u32 op,u32 in,u32 size,u32 out,u32 outsize){return start(6,fd,op,in,size,out,outsize);}
 void vector(unsigned i,u32 address,u32 size){word(0x20000+i*8,address);word(0x20004+i*8,size);}
 bool ioctlv(u32 fd,u32 op,u32 in,u32 out){return start(7,fd,op,in,out,0x20000,0);}
 s32 poll(u32 message){cpu.cycles+=6000;CHECK(wii_ios_native_tick(&ios,&ipc,&cpu));CHECK(ipc.y==1 && ipc.arm_message==message && word(message)==8);s32 value=s32(word(message+4));retire(0x34);return value;}
 void sd_request(u32 cmd,u32 arg,u32 blocks=0,u32 address=0){std::memset(cpu.ram+0x18000,0,36);word(0x18000,cmd);word(0x18004,(cmd==24 || cmd==25)?2:3);word(0x18008,1);word(0x1800c,arg);word(0x18010,blocks);word(0x18014,blocks?512:0);word(0x18018,address);word(0x1801c,address?1:0);}
 s32 sd(u32 fd,u32 cmd,u32 arg,u32 blocks=0,u32 address=0){sd_request(cmd,arg,blocks,address);CHECK(ioctl(fd,7,0x18000,36,0x19000,16));return result();}
 s32 bulk(u32 fd,u8 endpoint,u32 size){cpu.ram[0x21000]=endpoint;cpu.ram[0x21020]=u8(size>>8);cpu.ram[0x21021]=u8(size);vector(0,0x21000,1);vector(1,0x21020,2);vector(2,0x24000,size);CHECK(ioctlv(fd,1,2,1));return result();}
 s32 control(u32 fd,u8 type,u8 op,u16 value,u16 length){
  const u32 sizes[6]={1,1,2,2,2,1};for(unsigned i=0;i<6;i++){std::memset(cpu.ram+0x21000+i*32,0,32);vector(i,0x21000+i*32,sizes[i]);}cpu.ram[0x21000]=type;cpu.ram[0x21020]=op;cpu.ram[0x21040]=u8(value);cpu.ram[0x21041]=u8(value>>8);cpu.ram[0x21080]=u8(length);cpu.ram[0x21081]=u8(length>>8);vector(6,0x24000,length);CHECK(ioctlv(fd,0,6,1));return result();
 }
 void cbw(u32 length,bool input,u8 command,u32 sector=0,u16 blocks=0){u8* p=cpu.ram+0x24000;std::memset(p,0,31);p[0]='U';p[1]='S';p[2]='B';p[3]='C';p[4]=0x42;for(unsigned i=0;i<4;i++)p[8+i]=u8(length>>(8*i));p[12]=input?0x80:0;p[14]=10;p[15]=command;p[17]=u8(sector>>24);p[18]=u8(sector>>16);p[19]=u8(sector>>8);p[20]=u8(sector);p[22]=u8(blocks>>8);p[23]=u8(blocks);}
 void csw(u32 fd,u8 status){CHECK(bulk(fd,0x81,13)==13);CHECK(!std::memcmp(cpu.ram+0x24000,"USBS",4));CHECK(cpu.ram[0x24004]==0x42 && cpu.ram[0x2400c]==status);}
};
void image(const std::filesystem::path& path){std::ofstream f(path,std::ios::binary);CHECK(f);f.seekp(2*1024*1024-1);f.put(0);CHECK(f);}
std::array<u8,512> sector(const std::filesystem::path& path,unsigned n){std::array<u8,512> out{};std::ifstream f(path,std::ios::binary);f.seekg(n*512);f.read(reinterpret_cast<char*>(out.data()),out.size());CHECK(f.good());return out;}
}
int main(int argc,char** argv){try{
 const auto dir=(argc>1?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path())/("wiillr-peripherals-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(dir));
 const auto sdfile=dir/"SD card.img",usbfile=dir/"USB disk.img",otherfile=dir/"other.img";image(sdfile);image(usbfile);image(otherfile);const auto sdpath=sdfile.string(),usbpath=usbfile.string(),otherpath=otherfile.string();char error[256];
 Fixture f;WiiPeripheralsConfig c;wii_peripherals_defaults(&c);CHECK(c.usb[0].kind==WII_USB_KEYBOARD && c.usb[1].kind==WII_USB_NONE && !c.sd.image_path);CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));
 const s32 sd=f.open("/dev/sdio/slot0");CHECK(sd>=0);CHECK(f.ioctl(sd,0x0b,0,0,0x19000,4) && !f.result() && !f.word(0x19000));
 /* A genuine pending insertion event completes only after attachment. */
 f.sd_request(0x40,1);CHECK(!f.ioctl(sd,7,0x18000,36,0,0));const u32 insertion=f.last;
 c.sd.image_path=sdpath.c_str();c.usb[1]={WII_USB_MASS_STORAGE,usbpath.c_str(),false};CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));CHECK(f.poll(insertion)==1);
 CHECK(f.ioctl(sd,0x0b,0,0,0x19000,4) && !f.result() && f.word(0x19000)==0x10001);CHECK(f.ioctl(sd,4,0,0,0x19000,4) && !f.result() && f.word(0x19000)==0x10000);
 CHECK(!f.sd(sd,9,0x10000));CHECK(!f.sd(sd,7,0x10000));CHECK(!f.sd(sd,16,512));
 const std::array<u8,512> empty{};std::array<u8,512> pattern;for(unsigned i=0;i<512;i++)pattern[i]=u8(i*29+7);std::memcpy(f.cpu.ram+0x40000,pattern.data(),512);
 CHECK(!f.sd(sd,24,7*512,1,0x40000));CHECK(sector(sdfile,7)==pattern);std::memset(f.cpu.ram+0x40000,0,512);CHECK(!f.sd(sd,17,7*512,1,0x40000));CHECK(!std::memcmp(f.cpu.ram+0x40000,pattern.data(),512));
 /* Vector transport reaches the same image; all DMA extents are checked
  * before touching either media or the response guard. */
 f.sd_request(18,7*512,1,0x40000);f.vector(0,0x18000,36);f.vector(1,0x40000,512);f.vector(2,0x19000,16);CHECK(f.ioctlv(sd,7,2,1) && !f.result());
 f.word(0x19000,0xabcdef01);CHECK(f.sd(sd,24,2*1024*1024,1,0x40000)<0);CHECK(f.word(0x19000)==0xabcdef01);CHECK(sector(sdfile,7)==pattern);
 f.sd_request(24,8*512,1,0x817fff00);CHECK(f.ioctl(sd,7,0x18000,36,0x19000,16) && f.result()<0);CHECK(sector(sdfile,8)==empty);
 WiiPeripheralsConfig current;CHECK(wii_ios_native_get_peripherals(&f.ios,&current));std::string retained=current.sd.image_path;
 WiiPeripheralsConfig bad=c;const std::string missing=(dir/"missing.img").string();bad.sd.image_path=missing.c_str();CHECK(!wii_ios_native_configure_peripherals(&f.ios,&bad,error,sizeof error));CHECK(wii_ios_native_get_peripherals(&f.ios,&current) && current.sd.image_path==retained);
 bad=c;bad.usb[1].image_path=sdpath.c_str();CHECK(!wii_ios_native_configure_peripherals(&f.ios,&bad,error,sizeof error));CHECK(!f.sd(sd,17,7*512,1,0x40000));
 c.sd.read_only=true;CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));CHECK(f.ioctl(sd,0x0b,0,0,0x19000,4) && f.word(0x19000)==0x10005);CHECK(f.sd(sd,24,8*512,1,0x40000)==-13);CHECK(sector(sdfile,8)==empty);c.sd.read_only=false;CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));
 /* USB enumeration is derived from configured ports, with an actual bulk
  * disk and sector count. Full CBW/data/CSW writes persist and read back. */
 const s32 host=f.open("/dev/usb/oh0"),disk=f.open("/dev/usb/oh0/1209/5741");CHECK(host>=0 && disk>=0);f.cpu.ram[0x21000]=8;f.cpu.ram[0x21020]=8;f.vector(0,0x21000,1);f.vector(1,0x21020,1);f.vector(2,0x21100,1);f.vector(3,0x21200,64);CHECK(f.ioctlv(host,12,2,2) && !f.result());CHECK(f.cpu.ram[0x21100]==1 && f.word(0x21200)==2 && f.word(0x21204)==0x12095741);
 CHECK(f.control(disk,0x80,6,0x100,18)==18 && f.cpu.ram[0x24000]==18 && f.cpu.ram[0x24008]==9 && f.cpu.ram[0x2400a]==0x41);
 CHECK(f.control(disk,0x80,6,0x200,32)==32 && f.cpu.ram[0x24002]==32 && f.cpu.ram[0x2400e]==8 && f.cpu.ram[0x24010]==0x50);
 CHECK(f.control(disk,0,9,1,0)==0 && f.control(disk,0x80,8,0,1)==1 && f.cpu.ram[0x24000]==1);
 f.cbw(8,true,0x25);CHECK(f.bulk(disk,2,31)==31);CHECK(f.bulk(disk,0x81,8)==8 && f.word(0x24000)==4095 && f.word(0x24004)==512);f.csw(disk,0);
 f.cbw(512,false,0x2a,11,1);CHECK(f.bulk(disk,2,31)==31);std::memcpy(f.cpu.ram+0x24000,pattern.data(),512);CHECK(f.bulk(disk,2,512)==512);f.csw(disk,0);CHECK(sector(usbfile,11)==pattern);
 f.cbw(512,true,0x28,11,1);CHECK(f.bulk(disk,2,31)==31);CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));CHECK(f.bulk(disk,0x81,512)==512 && !std::memcmp(f.cpu.ram+0x24000,pattern.data(),512));f.csw(disk,0); /* identical apply preserves active CBW */
 Commit transaction;auto candidate=c;candidate.usb[1].image_path=missing.c_str();CHECK(!wii_ios_native_configure_peripherals_commit(&f.ios,&candidate,commit_selection,&transaction,error,sizeof error) && transaction.calls==0);
 candidate=c;candidate.usb[1].image_path=otherpath.c_str();candidate.usb[0].kind=WII_USB_NONE;candidate.sd.image_path=nullptr;CHECK(!wii_ios_native_configure_peripherals_commit(&f.ios,&candidate,commit_selection,&transaction,error,sizeof error) && transaction.calls==1);CHECK(wii_ios_native_get_peripherals(&f.ios,&current) && current.usb[0].kind==WII_USB_KEYBOARD && current.sd.image_path);
 f.cbw(512,true,0x28,11,1);CHECK(f.bulk(disk,2,31)==31 && f.bulk(disk,0x81,512)==512 && !std::memcmp(f.cpu.ram+0x24000,pattern.data(),512));f.csw(disk,0); /* rejected save preserves the existing device handle */
 candidate=c;candidate.usb[1].read_only=true;CHECK(!wii_ios_native_configure_peripherals_commit(&f.ios,&candidate,commit_selection,&transaction,error,sizeof error) && transaction.calls==2);CHECK(wii_ios_native_get_peripherals(&f.ios,&current) && !current.usb[1].read_only);f.cbw(512,false,0x2a,14,1);CHECK(f.bulk(disk,2,31)==31);std::memcpy(f.cpu.ram+0x24000,pattern.data(),512);CHECK(f.bulk(disk,2,512)==512);f.csw(disk,0);CHECK(sector(usbfile,14)==pattern);
 /* An incomplete USB write is staged, not published as a partial sector;
  * a real BOT reset discards it without changing media. */
 f.cbw(512,false,0x2a,13,1);CHECK(f.bulk(disk,2,31)==31);std::memcpy(f.cpu.ram+0x24000,pattern.data(),128);CHECK(f.bulk(disk,2,128)==128 && sector(usbfile,13)==empty);CHECK(f.control(disk,0x21,0xff,0,0)==0 && sector(usbfile,13)==empty);
 const s32 rawkbd=f.open("/dev/usb/oh0/1209/5740");CHECK(rawkbd>=0);CHECK(f.control(rawkbd,0x81,6,0x2200,63)==63 && f.cpu.ram[0x24000]==5 && f.cpu.ram[0x2403e]==0xc0);
 WiiKeyboardReport rawkeys{};rawkeys.connected=true;rawkeys.modifiers=2;rawkeys.keys[0]=4;CHECK(wii_ios_native_keyboard_input(&f.ios,&rawkeys)==1);CHECK(f.control(rawkbd,0xa1,1,0x100,8)==8 && f.cpu.ram[0x24000]==2 && f.cpu.ram[0x24002]==4);
 f.cpu.ram[0x21000]=0x81;f.cpu.ram[0x21020]=0;f.cpu.ram[0x21021]=8;f.vector(0,0x21000,1);f.vector(1,0x21020,2);f.vector(2,0x30000,8);CHECK(f.ioctlv(rawkbd,2,2,1) && f.result()==8 && f.cpu.ram[0x30002]==4);
 CHECK(!f.ioctlv(rawkbd,2,2,1));const u32 unchanged_keys=f.last;CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));f.cpu.cycles+=6000;CHECK(wii_ios_native_tick(&f.ios,&f.ipc,&f.cpu) && !f.ipc.y);rawkeys.keys[0]=5;CHECK(wii_ios_native_keyboard_input(&f.ios,&rawkeys)==1);CHECK(f.poll(unchanged_keys)==8 && f.cpu.ram[0x30002]==5);
 CHECK(!f.ioctlv(rawkbd,2,2,1));const u32 raw_remove=f.last;c.usb[0]={WII_USB_MASS_STORAGE,otherpath.c_str(),false};CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));CHECK(f.poll(raw_remove)==-6);const s32 disk1=f.open("/dev/usb/oh0/1209/5740");CHECK(disk1>=0);f.cbw(512,true,0x28,11,1);CHECK(f.bulk(disk1,2,31)==31 && f.bulk(disk1,0x81,512)==512 && !std::memcmp(f.cpu.ram+0x24000,empty.data(),512));f.csw(disk1,0);
 c.usb[0]={WII_USB_KEYBOARD,nullptr,false};CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));CHECK(f.bulk(disk1,2,31)==-6);rawkeys.modifiers=0;rawkeys.keys[0]=0;CHECK(wii_ios_native_keyboard_input(&f.ios,&rawkeys)==1);
 c.usb[1].read_only=true;CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));f.cbw(512,false,0x2a,12,1);CHECK(f.bulk(disk,2,31)==31);std::memcpy(f.cpu.ram+0x24000,pattern.data(),512);CHECK(f.bulk(disk,2,512)==512);f.csw(disk,1);CHECK(sector(usbfile,12)==empty);
 /* Disconnect notification stays pending across identical apply, and fires
  * for the real port removal. It does not complete unrelated SD waits. */
 CHECK(!f.ioctl(disk,26,0,0,0,0));const u32 removal=f.last;CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));f.cpu.cycles+=6000;CHECK(wii_ios_native_tick(&f.ios,&f.ipc,&f.cpu) && !f.ipc.y);CHECK(wii_ios_native_eject_peripheral(&f.ios,1,error,sizeof error));CHECK(!f.poll(removal));CHECK(f.open("/dev/usb/oh0/1209/5741")==-6);
 /* Existing high-level keyboard defaults remain connected, and removing
  * the configured keyboard produces a disconnect through its real queue. */
 f.cpu.ram[0x21000]=3;f.vector(0,0x21000,1);CHECK(!f.ioctlv(host,28,1,0));const u32 classwait=f.last;CHECK(wii_ios_native_configure_peripherals(&f.ios,&c,error,sizeof error));f.cpu.cycles+=6000;CHECK(wii_ios_native_tick(&f.ios,&f.ipc,&f.cpu) && !f.ipc.y);CHECK(wii_ios_native_eject_peripheral(&f.ios,1,error,sizeof error));f.cpu.cycles+=6000;CHECK(wii_ios_native_tick(&f.ios,&f.ipc,&f.cpu) && !f.ipc.y); /* disk changes cannot wake a keyboard-class hook */
 const s32 keyboard=f.open("/dev/usb/kbd");CHECK(keyboard>=0);WiiKeyboardReport keys{};keys.connected=true;CHECK(wii_ios_native_keyboard_input(&f.ios,&keys)==1);CHECK(f.ioctl(keyboard,0,0,0,0x30000,16) && !f.result() && f.word(0x30000)==0);CHECK(!f.ioctl(keyboard,0,0,0,0x30000,16));const u32 keywait=f.last;CHECK(wii_ios_native_eject_peripheral(&f.ios,0,error,sizeof error));CHECK(!f.poll(classwait));CHECK(!f.poll(keywait) && f.word(0x30000)==1);
 f.sd_request(0x40,2);CHECK(!f.ioctl(sd,7,0x18000,36,0,0));const u32 sdremove=f.last;CHECK(wii_ios_native_get_peripherals(&f.ios,&current));candidate=current;candidate.sd.image_path=nullptr;CHECK(!wii_ios_native_configure_peripherals_commit(&f.ios,&candidate,commit_selection,&transaction,error,sizeof error));f.cpu.cycles+=6000;CHECK(wii_ios_native_tick(&f.ios,&f.ipc,&f.cpu) && !f.ipc.y);CHECK(wii_ios_native_get_peripherals(&f.ios,&current) && current.sd.image_path);transaction.accept=true;CHECK(wii_ios_native_configure_peripherals_commit(&f.ios,&candidate,commit_selection,&transaction,error,sizeof error));CHECK(f.poll(sdremove)==2);CHECK(wii_ios_native_flush_peripherals(&f.ios,error,sizeof error));CHECK(sector(sdfile,7)==pattern && sector(usbfile,11)==pattern);
 /* Distinct instances have independent defaults and handles; malformed
  * image files and folders never replace or truncate an attachment. */
 WiiIosNative other;wii_ios_native_init(&other);CHECK(wii_ios_native_get_peripherals(&other,&current) && current.usb[0].kind==WII_USB_KEYBOARD && !current.sd.image_path);c={};c.sd.image_path=otherpath.c_str();CHECK(wii_ios_native_configure_peripherals(&other,&c,error,sizeof error));CHECK(wii_ios_native_get_peripherals(&f.ios,&current) && !current.sd.image_path);const std::string folder=dir.string();bad=c;bad.sd.image_path=folder.c_str();CHECK(!wii_ios_native_configure_peripherals(&other,&bad,error,sizeof error));CHECK(std::filesystem::file_size(otherfile)==2*1024*1024);wii_ios_native_destroy(&other);
 std::puts("peripheral image / SDIO / USB BOT / lifecycle contracts passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
