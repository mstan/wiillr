/* SPDX-License-Identifier: MIT
 * Original image/device implementation. Protocol references (not copied code):
 * devkitPro/libogc ef718e2d7df5e5b1750a9e1a67c17350bdd30d3e,
 * libogc/{wiisd,usb,usbstorage}.c; SD/USB request field layouts and BOT framing.
 * Dolphin f7a0bf5bd6c09233d56ff765cc2823da1586d4d4 SDIOSlot0.cpp:
 * IOS SD status bits, event values, and controller R2 word ordering.
 */
#include "peripherals_internal.h"
#include "memory/memory.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace {
constexpr int invalid=-4, io_error=-5, no_device=-6, locked=-13;
constexpr u32 max_transfer=16*1024*1024;
void error(char* out,size_t n,const char* text){if(out && n){std::strncpy(out,text,n-1);out[n-1]=0;}}
u16 le16(const u8* p){return u16(p[0]|u16(p[1])<<8);}
u32 le32(const u8* p){return p[0]|u32(p[1])<<8|u32(p[2])<<16|u32(p[3])<<24;}
void putle32(u8* p,u32 x){for(unsigned i=0;i<4;i++)p[i]=u8(x>>(8*i));}
void putbe32(u8* p,u32 x){for(unsigned i=0;i<4;i++)p[i]=u8(x>>(24-8*i));}
struct Image {
 std::string path;u64 bytes=0;bool read_only=true,can_write=false;
#ifdef _WIN32
 HANDLE file=INVALID_HANDLE_VALUE;
 ~Image(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
#else
 int file=-1;
 ~Image(){if(file>=0)close(file);}
#endif
 bool flush(){
  if(read_only)return true;
#ifdef _WIN32
  return FlushFileBuffers(file)!=0;
#else
  return fsync(file)==0;
#endif
 }
 bool transfer(u64 offset,void* buffer,size_t count,bool write){
  if(offset>bytes || count>bytes-offset || (write && read_only))return false;
#ifdef _WIN32
  LARGE_INTEGER at;at.QuadPart=LONGLONG(offset);if(!SetFilePointerEx(file,at,nullptr,FILE_BEGIN))return false;
  DWORD done=0;return (write?WriteFile(file,buffer,DWORD(count),&done,nullptr):ReadFile(file,buffer,DWORD(count),&done,nullptr)) && done==count;
#else
  size_t done=0;while(done<count){const ssize_t n=write?pwrite(file,static_cast<u8*>(buffer)+done,count-done,off_t(offset+done)):pread(file,static_cast<u8*>(buffer)+done,count-done,off_t(offset+done));if(n<0 && errno==EINTR)continue;if(n<=0)return false;done+=size_t(n);}return true;
#endif
 }
};
std::shared_ptr<Image> open_image(const char* name,bool ro,char* out,size_t n){
 if(!name || !*name){error(out,n,"Choose an image file.");return {};}
 std::error_code ec;const auto path=std::filesystem::u8path(name);
 if(std::filesystem::is_symlink(std::filesystem::symlink_status(path,ec)) || ec || !std::filesystem::is_regular_file(path,ec) || ec){error(out,n,"The image must be an existing regular file, not a link or device.");return {};}
 auto image=std::make_shared<Image>();image->read_only=ro;const auto canonical=std::filesystem::canonical(path,ec).u8string();image->path.assign(reinterpret_cast<const char*>(canonical.data()),canonical.size());
 if(ec || image->path.empty()){error(out,n,"The image path could not be resolved.");return {};}
#ifdef _WIN32
 image->file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);image->can_write=image->file!=INVALID_HANDLE_VALUE;
 if(!image->can_write && ro)image->file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
 if(image->file==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(image->file,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) || !GetFileSizeEx(image->file,&size) || size.QuadPart<0){error(out,n,"The image is unavailable, in use, or cannot be opened with this access.");return {};}
 image->bytes=u64(size.QuadPart);
#else
 image->file=open(path.c_str(),O_RDWR|O_CLOEXEC|O_NOFOLLOW);image->can_write=image->file>=0;if(!image->can_write && ro)image->file=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st{};
 if(image->file<0 || fstat(image->file,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0 || flock(image->file,LOCK_EX|LOCK_NB)!=0){error(out,n,"The image is unavailable, in use, or cannot be opened with this access.");return {};}
 image->bytes=u64(st.st_size);
#endif
 if(image->bytes<1024*1024 || image->bytes%512 || image->bytes/512>0xffffffffull){error(out,n,"Use a raw image of at least 1 MiB, with a whole number of 512-byte sectors (maximum 2 TiB).");return {};}
 return image;
}
struct Bulk {
 enum Phase{command,data_in,data_out,status} phase=command;
 u32 tag=0,total=0,done=0;u64 offset=0;u8 result=0,sense=0,asc=0;bool disk=false;
 std::vector<u8> data;
 void reset(){phase=command;tag=total=done=0;offset=0;result=0;disk=false;data.clear();}
};
struct Port {WiiUsbKind kind=WII_USB_NONE;std::shared_ptr<Image> image;Bulk bulk;u8 configuration=0,idle=0,protocol=1,leds=0;u64 generation=1;bool media_loaded=true;};
u8* memory(CPUState* cpu,u32 address,u32 count){u32 available;const u32 key=gcn_reservation_key(address);if(!(key<cpu->ram_size || (key>=0x10000000u && key-0x10000000u<cpu->mem2_size)))return nullptr;u8* p=gcn_mem_resolve(cpu,address,&available);return p && count<=available?p:nullptr;}
void publish(CPUState* cpu,u32 address,const u8* data,u32 bytes){for(u32 i=0;i<bytes;i++)mem_write8_cia(cpu,address+i,data[i],cpu->pc);}
struct Vector {u32 address,size;u8* data;};
bool vectors(CPUState* cpu,const u8* r,unsigned count,std::array<Vector,8>& v){
 if(count>8)return false;const u8* table=memory(cpu,read_be32(r+24),count*8);if(!table)return false;
 for(unsigned i=0;i<count;i++){v[i].address=read_be32(table+8*i);v[i].size=read_be32(table+8*i+4);if(v[i].size>max_transfer)return false;v[i].data=v[i].size?memory(cpu,v[i].address,v[i].size):nullptr;if(v[i].size && !v[i].data)return false;}return true;
}
}
struct WiiPeripherals {
 Port usb[2];std::shared_ptr<Image> sd;bool sd_initialized=false,sd_selected=false,sd_app=false;u32 block_length=512;std::array<u32,64> hcr{};
 struct Pending {u32 message=0,command=0,op=0,port=0,output=0,size=0;u64 generation=0;bool cancelled=false;u8 filter_class=0;} pending[24];
 WiiKeyboardReport keyboard{};u64 keyboard_serial=1,last_keyboard[24]{},last_keyboard_cycles[24]{};
 u64 fd_generation[24]{};u8 fd_device[24]{};
 u64 keyboard_changes=1,disk_changes=0;
};
extern "C" void wii_peripherals_defaults(WiiPeripheralsConfig* c){if(c){*c={};c->usb[0].kind=WII_USB_KEYBOARD;}}
extern "C" WiiPeripherals* wii_peripherals_create(void){auto* p=new(std::nothrow) WiiPeripherals;if(p)p->usb[0].kind=WII_USB_KEYBOARD;return p;}
extern "C" void wii_peripherals_destroy(WiiPeripherals* p){delete p;}
extern "C" void wii_peripherals_get(const WiiPeripherals* p,WiiPeripheralsConfig* c){
 wii_peripherals_defaults(c);if(!p || !c)return;
 for(unsigned i=0;i<2;i++)c->usb[i]={p->usb[i].kind,p->usb[i].image?p->usb[i].image->path.c_str():nullptr,p->usb[i].image?p->usb[i].image->read_only:false};
 c->sd={p->sd?p->sd->path.c_str():nullptr,p->sd?p->sd->read_only:false};
}
extern "C" bool wii_peripherals_flush(WiiPeripherals* p,char* out,size_t n){if(!p)return true;for(auto& port:p->usb)if(port.image && !port.image->flush()){error(out,n,"The USB image could not be flushed; it is still attached.");return false;}if(p->sd && !p->sd->flush()){error(out,n,"The SD image could not be flushed; it is still attached.");return false;}error(out,n,"");return true;}
extern "C" bool wii_peripherals_configure_commit(WiiPeripherals* p,const WiiPeripheralsConfig* c,WiiPeripheralsCommit commit,void* user,char* out,size_t n){
 if(!p || !c){error(out,n,"Peripheral configuration is unavailable.");return false;}
 const WiiPeripheralsConfig requested=*c;c=&requested;
 try {
  Port ports[2];std::shared_ptr<Image> sd;std::vector<std::string> paths;
  auto attach=[&](const char* path,bool ro)->std::shared_ptr<Image>{
   if(!path || !*path){error(out,n,"Choose an image file.");return {};}std::error_code ec;const auto target=std::filesystem::u8path(path);if(std::filesystem::is_symlink(std::filesystem::symlink_status(target,ec)) || ec){error(out,n,"The image must not be a link.");return {};}const auto canonical=std::filesystem::canonical(target,ec).u8string();const std::string normalized(reinterpret_cast<const char*>(canonical.data()),canonical.size());
   if(ec){error(out,n,"The image file does not exist.");return {};}
   if(std::find(paths.begin(),paths.end(),normalized)!=paths.end()){error(out,n,"One image cannot be attached to more than one slot.");return {};}
   paths.push_back(normalized);for(auto& old:p->usb)if(old.image && old.image->path==normalized){if(!ro && !old.image->can_write){error(out,n,"This image cannot be reopened for writing while attached; eject it first.");return {};}return old.image;}
   if(p->sd && p->sd->path==normalized){if(!ro && !p->sd->can_write){error(out,n,"This image cannot be reopened for writing while attached; eject it first.");return {};}return p->sd;}
   return open_image(path,ro,out,n);
  };
  for(unsigned i=0;i<2;i++){
   if(c->usb[i].kind<WII_USB_NONE || c->usb[i].kind>WII_USB_MASS_STORAGE){error(out,n,"Unknown USB device type.");return false;}
   ports[i].kind=c->usb[i].kind;
   if(ports[i].kind==WII_USB_MASS_STORAGE){ports[i].image=attach(c->usb[i].image_path,c->usb[i].read_only);if(!ports[i].image)return false;}
   else if(c->usb[i].image_path && *c->usb[i].image_path){error(out,n,"Only mass storage accepts an image file.");return false;}
  }
  if(c->sd.image_path && *c->sd.image_path){sd=attach(c->sd.image_path,c->sd.read_only);if(!sd)return false;
   unsigned mult=0;const u64 sectors=sd->bytes/512;while(sectors/(1ull<<(mult+2))>4096 && mult<7)++mult;
   if(sd->bytes>32ull*1024*1024*1024 || (sd->bytes>2ull*1024*1024*1024?sd->bytes%(512*1024):sectors%(1ull<<(mult+2)))){error(out,n,"SD images must be at most 32 GiB and aligned to SD card capacity units.");return false;}}
  if(!wii_peripherals_flush(p,out,n))return false;
  if(commit && !commit(user,out,n))return false;
  /* Everything below is noexcept: no file opens, allocation, validation, or
   * fallible flush may occur after the persisted selection is committed. */
  const bool same_sd=p->sd==sd;
  for(unsigned i=0;i<2;i++)if(p->usb[i].kind!=ports[i].kind || p->usb[i].image!=ports[i].image){if(p->usb[i].kind==WII_USB_KEYBOARD || ports[i].kind==WII_USB_KEYBOARD)++p->keyboard_changes;if(p->usb[i].kind==WII_USB_MASS_STORAGE || ports[i].kind==WII_USB_MASS_STORAGE)++p->disk_changes;ports[i].generation=p->usb[i].generation+1;p->usb[i]=std::move(ports[i]);}
  p->sd=std::move(sd);for(unsigned i=0;i<2;i++)if(p->usb[i].image)p->usb[i].image->read_only=c->usb[i].read_only;if(p->sd)p->sd->read_only=c->sd.read_only;if(!same_sd){p->sd_initialized=false;p->sd_selected=false;p->sd_app=false;p->block_length=512;p->hcr.fill(0);}error(out,n,"");return true;
 }catch(...){error(out,n,"The peripheral configuration could not be allocated.");return false;}
}
extern "C" bool wii_peripherals_configure(WiiPeripherals* p,const WiiPeripheralsConfig* c,char* out,size_t n){return wii_peripherals_configure_commit(p,c,nullptr,nullptr,out,n);}
extern "C" bool wii_peripherals_eject(WiiPeripherals* p,unsigned slot,char* out,size_t n){
 if(!p || slot>2){error(out,n,"Unknown peripheral slot.");return false;}
 auto image=slot==2?p->sd:p->usb[slot].image;if(image && !image->flush()){error(out,n,"The image could not be flushed; it is still attached.");return false;}
 if(slot==2){p->sd.reset();p->sd_initialized=p->sd_selected=p->sd_app=false;}else {if(p->usb[slot].kind==WII_USB_KEYBOARD)++p->keyboard_changes;if(p->usb[slot].kind==WII_USB_MASS_STORAGE)++p->disk_changes;const u64 next=p->usb[slot].generation+1;p->usb[slot]=Port{};p->usb[slot].generation=next;}error(out,n,"");return true;
}
extern "C" bool wii_peripherals_keyboard(const WiiPeripherals* p){return !p || p->usb[0].kind==WII_USB_KEYBOARD || p->usb[1].kind==WII_USB_KEYBOARD;}
extern "C" bool wii_peripherals_sd_present(const WiiPeripherals* p){return p && bool(p->sd);}
extern "C" void wii_peripherals_keyboard_input(WiiPeripherals* p,const WiiKeyboardReport* r){if(p && r && (p->keyboard.connected!=r->connected || p->keyboard.modifiers!=r->modifiers || std::memcmp(p->keyboard.keys,r->keys,6))){p->keyboard=*r;++p->keyboard_serial;}}
extern "C" void wii_peripherals_open(WiiPeripherals* p,u32 fd,u8 device){if(p && fd<24){p->fd_device[fd]=device;p->fd_generation[fd]=device>=16 && device<=17?p->usb[device-16].generation:0;p->last_keyboard[fd]=0;}}
extern "C" void wii_peripherals_close(WiiPeripherals* p,u32 fd){if(p && fd<24){if(p->pending[fd].message)p->pending[fd].cancelled=true;p->last_keyboard[fd]=0;p->fd_device[fd]=0;p->fd_generation[fd]=0;}}
extern "C" bool wii_peripherals_poll(WiiPeripherals* p,CPUState* cpu,u32* message,u32* command,s32* result){
 if(!p)return false;
 for(unsigned fd=0;fd<24;fd++){
  auto& wait=p->pending[fd];if(!wait.message)continue;bool ready=false;s32 status=0;
  if(wait.cancelled){ready=true;status=invalid;}
  else if(wait.op==28)ready=wait.generation!=(wait.filter_class==0?p->keyboard_changes+p->disk_changes:wait.filter_class==3?p->keyboard_changes:wait.filter_class==8?p->disk_changes:0);
  else if(wait.op==26)ready=wait.generation!=p->usb[wait.port].generation || p->usb[wait.port].kind==WII_USB_NONE;
  else if(wait.op==2){
   if(p->usb[wait.port].kind!=WII_USB_KEYBOARD || wait.generation!=p->usb[wait.port].generation){ready=true;status=no_device;}
   else if(p->last_keyboard[fd]!=p->keyboard_serial || (p->usb[wait.port].idle && cpu->cycles-p->last_keyboard_cycles[fd]>=u64(p->usb[wait.port].idle)*2916000)){ready=true;u8 report[8]={};if(p->keyboard.connected){report[0]=p->keyboard.modifiers;std::memcpy(report+2,p->keyboard.keys,6);}if(!memory(cpu,wait.output,8))status=invalid;else{publish(cpu,wait.output,report,8);p->last_keyboard[fd]=p->keyboard_serial;p->last_keyboard_cycles[fd]=cpu->cycles;status=8;}}
  }
  if(ready){*message=wait.message;*command=wait.command;*result=status;wait={};return true;}
 }
 return false;
}

namespace {
u32 sd_ocr(const WiiPeripherals& p){return p.sd?(0x80ff8000u|(p.sd->bytes>2ull*1024*1024*1024?0x40000000u:0)):0;}
int sd_command(WiiPeripherals& p,CPUState* cpu,const u8* input,u32 output,u32 output_size,u32 data_address,u32 data_size,s32* result){
 if(output_size && (output_size<4 || !memory(cpu,output,output_size)))return 1;
 const u32 cmd=read_be32(input),arg=read_be32(input+12),blocks=read_be32(input+16),bsize=read_be32(input+20);
 if(!p.sd){*result=no_device;return 1;}
 std::array<u8,16> response{};u32 word=0;const bool app=p.sd_app;p.sd_app=false;
 switch(cmd){
 case 0:p.sd_selected=false;break;
 case 2:case 10:putbe32(response.data(),0x15776969);putbe32(response.data()+4,0x6c6c7200);putbe32(response.data()+8,u32(p.sd->bytes/512));putbe32(response.data()+12,0x01000101);break;
 case 3:word=0x00010000;break;
 case 7:if(arg && arg!=0x00010000)return 1;p.sd_selected=arg!=0;word=p.sd_selected?0x700:0x900;break;
 case 8:if((arg&0xf00)!=0x100)return 1;word=arg;break;
 case 9:{
  /* 128-bit SD CSD, with the IOS controller's reversed-word/right8 R2 layout. */
  std::array<u8,16> csd{};auto bits=[&](unsigned hi,unsigned lo,u32 value){for(unsigned bit=lo;bit<=hi;bit++)if(value&(1u<<(bit-lo)))csd[(127-bit)/8]|=u8(1u<<(bit%8));};
  const bool hc=p.sd->bytes>2ull*1024*1024*1024;bits(127,126,hc?1:0);bits(119,112,0x0e);bits(103,96,0x32);bits(95,84,0x1b5);bits(83,80,9);bits(46,46,1);bits(45,39,127);bits(28,26,2);bits(25,22,9);bits(0,0,1);
  if(hc)bits(69,48,u32(p.sd->bytes/(512*1024)-1));else{u64 sectors=p.sd->bytes/512;unsigned mult=0;while(sectors/(1ull<<(mult+2))>4096 && mult<7)++mult;if(sectors%(1ull<<(mult+2)))return 1;bits(73,62,u32(sectors/(1ull<<(mult+2))-1));bits(49,47,mult);}
  for(unsigned i=0;i<4;i++){u32 x=read_be32(csd.data()+i*4);x=(x>>8)|(i?u32(csd[i*4-1])<<24:0);putbe32(response.data()+(3-i)*4,x);}break;}
 case 12:case 13:word=0x900;break;
 case 16:if(arg!=512)return 1;p.block_length=arg;word=0x900;break;
 case 55:p.sd_app=true;word=0x920;break;
 case 41:if(!app)return 1;p.sd_initialized=true;word=sd_ocr(p);break;
 case 6:if(!app || (arg!=0 && arg!=2))return 1;word=0x920;break;
 case 17:case 18:case 24:case 25:{
  if(!blocks || bsize!=512 || u64(blocks)*bsize>max_transfer || ((cmd==17 || cmd==24) && blocks!=1))return 1;
  const u32 size=blocks*bsize;const u32 address=data_address?data_address:read_be32(input+24);
  if(data_address && data_size!=size)return 1;u8* guest=memory(cpu,address,size);if(!guest)return 1;
  const u64 offset=p.sd->bytes>2ull*1024*1024*1024?u64(arg)*512:arg;if(offset%512 || offset>p.sd->bytes || size>p.sd->bytes-offset)return 1;
  if(output_size && u64(gcn_reservation_key(output))<u64(gcn_reservation_key(address))+size && u64(gcn_reservation_key(address))<u64(gcn_reservation_key(output))+output_size)return 1;
  const bool write=cmd==24 || cmd==25;if(write && p.sd->read_only){*result=locked;return 1;}
  std::vector<u8> bytes(size);if(write)std::memcpy(bytes.data(),guest,size);
  if(!p.sd->transfer(offset,bytes.data(),size,write) || (write && !p.sd->flush())){*result=io_error;return 1;}
  if(!write)publish(cpu,address,bytes.data(),size);word=0x900;break;}
 default:return 0;
 }
 if(cmd!=2 && cmd!=9 && cmd!=10)putbe32(response.data(),word);
 if(output_size)publish(cpu,output,response.data(),std::min(output_size,16u));*result=0;return 1;
}
int sd_submit(WiiPeripherals& p,CPUState* cpu,const u8* r,s32* result){
 const u32 command=read_be32(r),op=read_be32(r+12);if(command!=6 && command!=7)return 0;
 if(command==7){std::array<Vector,8> v;if(op!=7 || read_be32(r+16)!=2 || read_be32(r+20)!=1 || !vectors(cpu,r,3,v) || v[0].size<36 || (v[2].size && v[2].size<4))return 1;return sd_command(p,cpu,v[0].data,v[2].address,v[2].size,v[1].address,v[1].size,result);}
 const u32 in_size=read_be32(r+20),out=read_be32(r+24),out_size=read_be32(r+28);const u8* in=in_size?memory(cpu,read_be32(r+16),in_size):nullptr;
 if(in_size && !in)return 1;if(out_size && !memory(cpu,out,out_size))return 1;
 if(op==7){if(in_size<36)return 1;return sd_command(p,cpu,in,out,out_size,0,0,result);}
 if(op==0x0b || op==4 || op==12){if(in_size || out_size<4)return 1;u32 value=0;
  if(p.sd){if(op==4){p.sd_initialized=true;p.sd_selected=false;value=0x00010000;}else if(op==12)value=sd_ocr(p);else value=1u|0x10000u|(p.sd->read_only?4u:0)|(p.sd->bytes>2ull*1024*1024*1024?0x100000u:0);}
  mem_write32_cia(cpu,out,value,cpu->pc);*result=0;return 1;}
 if(op==6){if(in_size!=4 || out_size)return 1;const u32 clock=read_be32(in);if(clock && (clock&(clock-1)))return 1;*result=0;return 1;}
 if(op==1 || op==2){if(in_size<24 || (op==2?out_size<4:out_size!=0))return 1;const u32 reg=read_be32(in),width=read_be32(in+12);if(reg>=64 || (width!=1 && width!=2 && width!=4))return 1;
  const u32 mask=width==1?255u:width==2?65535u:0xffffffffu;
  if(op==2)mem_write32_cia(cpu,out,p.hcr[reg]&mask,cpu->pc);else {u32 v=read_be32(in+16)&mask;if(reg==0x2c && (v&1))v|=2;if(reg==0x2f && v)v=0;p.hcr[reg]=v;}*result=0;return 1;}
 return 0;
}
void scsi_start(Port& port,const u8* cbw){
 Bulk& b=port.bulk;b.reset();b.tag=le32(cbw+4);b.total=le32(cbw+8);const u8* cmd=cbw+15;const bool in=(cbw[12]&0x80)!=0;u32 need=0;
 auto fail=[&](u8 key,u8 asc){b.result=1;b.sense=key;b.asc=asc;b.phase=b.total?(in?Bulk::data_in:Bulk::data_out):Bulk::status;};
 if(cbw[13]!=0 || !cbw[14] || cbw[14]>16 || (cbw[12]&0x7f) || b.total>max_transfer){fail(5,0x24);return;}
 if((!port.image || !port.media_loaded) && cmd[0]!=3 && cmd[0]!=0x12 && cmd[0]!=0x1b){fail(2,0x3a);return;}
 switch(cmd[0]){
 case 0:break;
 case 3:need=std::min<u32>(cmd[4],18);b.data.assign(18,0);b.data[0]=0x70;b.data[2]=b.sense;b.data[7]=10;b.data[12]=b.asc;break;
 case 0x12:need=std::min<u32>(cmd[4],36);b.data.assign(36,0);b.data[1]=0x80;b.data[2]=4;b.data[3]=2;b.data[4]=31;std::memcpy(b.data.data()+8,"wiillr  Image Disk      1.0 ",28);break;
 case 0x1a:need=std::min<u32>(cmd[4],4);b.data={3,0,u8(port.image->read_only?0x80:0),0};break;
 case 0x1b:if(!port.image){fail(2,0x3a);return;}if((cmd[4]&3)==2){if(!port.image->flush()){fail(3,0x0c);return;}port.media_loaded=false;}else if((cmd[4]&3)==3)port.media_loaded=true;break;
 case 0x1e:break;
 case 0x25:need=8;b.data.resize(8);putbe32(b.data.data(),u32(port.image->bytes/512-1));putbe32(b.data.data()+4,512);break;
 case 0x28:case 0x2a:{
  const u32 blocks=(u32(cmd[7])<<8)|cmd[8];const u64 offset=u64(read_be32(cmd+2))*512;need=blocks*512;
  if(need!=b.total || need>max_transfer || offset>port.image->bytes || need>port.image->bytes-offset || in!=(cmd[0]==0x28)){fail(5,0x21);return;}
  if(cmd[0]==0x2a && port.image->read_only){fail(7,0x27);return;}b.offset=offset;b.disk=true;if(cmd[0]==0x2a)b.data.resize(need);break;}
 case 0x35:if(!port.image->flush()){fail(3,0x0c);return;}break;
 default:fail(5,0x20);return;
 }
 if(need!=b.total || (need && in!=(cmd[0]!=0x2a))){fail(5,0x24);return;}
 b.phase=b.total?(in?Bulk::data_in:Bulk::data_out):Bulk::status;
}
int bulk(Port& port,CPUState* cpu,u32 address,u32 length,u8 endpoint,s32* result){
 Bulk& b=port.bulk;u8* guest=memory(cpu,address,length);if(!length || !guest)return 1;
 if(endpoint==2 && b.phase==Bulk::command){if(length!=31 || le32(guest)!=0x43425355)return 1;const u8 sense=b.sense,asc=b.asc;scsi_start(port,guest);if(guest[15]==3 && b.data.size()>=18){b.data[2]=sense;b.data[12]=asc;b.sense=b.asc=0;}*result=31;return 1;}
 if(endpoint==0x81 && b.phase==Bulk::status){if(length!=13)return 1;u8 csw[13]={};putle32(csw,0x53425355);putle32(csw+4,b.tag);putle32(csw+8,b.result?b.total:b.total-b.done);csw[12]=b.result;publish(cpu,address,csw,13);b.phase=Bulk::command;b.data.clear();*result=13;return 1;}
 if((endpoint==0x81 && b.phase!=Bulk::data_in) || (endpoint==2 && b.phase!=Bulk::data_out) || (endpoint!=0x81 && endpoint!=2) || length>b.total-b.done)return 1;
 std::vector<u8> data(length,0);
 if(!b.result){
  if(b.disk){if(!port.image || !port.media_loaded){*result=no_device;return 1;}
   if(endpoint==2){if(port.image->read_only){*result=locked;return 1;}std::memcpy(b.data.data()+b.done,guest,length);if(b.done+length==b.total && (!port.image->transfer(b.offset,b.data.data(),b.total,true) || !port.image->flush())){*result=io_error;return 1;}}
   else if(!port.image->transfer(b.offset+b.done,data.data(),length,false)){*result=io_error;return 1;}}
  else if(endpoint==0x81){if(u64(b.done)+length>b.data.size())return 1;std::memcpy(data.data(),b.data.data()+b.done,length);}
 }
 if(endpoint==0x81)publish(cpu,address,data.data(),length);b.done+=length;if(b.done==b.total)b.phase=Bulk::status;*result=s32(length);return 1;
}
int usb_submit(WiiPeripherals& p,CPUState* cpu,const u8* r,u32 message,u8 device,s32* result){
 const u32 command=read_be32(r),op=read_be32(r+12),fd=read_be32(r+8);if(fd>=24)return 1;
 if(device>=16 && (p.fd_device[fd]!=device || p.fd_generation[fd]!=p.usb[device-16].generation)){*result=no_device;return 1;}
 if(command==6 && device>=16 && op==26 && !read_be32(r+20) && !read_be32(r+28)){
  if(p.pending[fd].message){*result=-2;return 1;}p.pending[fd]={message,command,op,u32(device-16),0,0,p.usb[device-16].generation,false};return 2;}
 if(command==6 && device>=16 && (op==5 || op==6 || op==29) && !read_be32(r+20) && !read_be32(r+28)){if(p.usb[device-16].kind==WII_USB_NONE){*result=no_device;return 1;}if(op==29)p.usb[device-16].bulk.reset();*result=0;return 1;}
 if(command!=7)return 0;const u32 inputs=read_be32(r+16),outputs=read_be32(r+20);std::array<Vector,8> v;
 if(inputs>8 || outputs>8-inputs || !vectors(cpu,r,inputs+outputs,v))return 1;
 if(device==15){
  if(op==28 && inputs==1 && !outputs && v[0].size==1){if(p.pending[fd].message){*result=-2;return 1;}const u8 cls=v[0].data[0];const u64 generation=cls==0?p.keyboard_changes+p.disk_changes:cls==3?p.keyboard_changes:cls==8?p.disk_changes:0;p.pending[fd]={message,command,op,0,0,0,generation,false,cls};return 2;}
  if(op!=12 || inputs!=2 || outputs!=2 || v[0].size!=1 || v[1].size!=1 || v[2].size<1)return 1;u32 count=0;const unsigned max=std::min<unsigned>(v[0].data[0],v[3].size/8);const u8 cls=v[1].data[0];
  std::vector<u8> list(v[3].size,0);for(unsigned i=0;i<2 && count<max;i++)if(p.usb[i].kind!=WII_USB_NONE && (!cls || cls==(p.usb[i].kind==WII_USB_KEYBOARD?3:8))){putbe32(list.data()+count*8,i+1);list[count*8+4]=0x12;list[count*8+5]=0x09;list[count*8+6]=0x57;list[count*8+7]=u8(0x40+i);++count;}
  if(v[3].size)publish(cpu,v[3].address,list.data(),v[3].size);const u8 byte=u8(count);publish(cpu,v[2].address,&byte,1);*result=0;return 1;
 }
 Port& port=p.usb[device-16];if(port.kind==WII_USB_NONE){*result=no_device;return 1;}
 if(op==2 && port.kind==WII_USB_KEYBOARD){if(inputs!=2 || outputs!=1 || v[0].size!=1 || v[0].data[0]!=0x81 || v[1].size!=2 || v[1].data[0]!=0 || v[1].data[1]!=8 || v[2].size!=8)return 1;if(p.pending[fd].message){*result=-2;return 1;}p.pending[fd]={message,command,op,u32(device-16),v[2].address,8,port.generation,false};return 2;}
 if(op==1 && port.kind==WII_USB_MASS_STORAGE){if(inputs!=2 || outputs!=1 || v[0].size!=1 || v[1].size!=2 || (u32(v[1].data[0])<<8|v[1].data[1])!=v[2].size)return 1;return bulk(port,cpu,v[2].address,v[2].size,v[0].data[0],result);}
 if(op==0){
  if(inputs!=6 || outputs!=1 || v[0].size!=1 || v[1].size!=1 || v[2].size!=2 || v[3].size!=2 || v[4].size!=2 || v[5].size!=1 || le16(v[4].data)!=v[6].size)return 1;
  const u8 type=v[0].data[0],request=v[1].data[0];const u16 value=le16(v[2].data);std::vector<u8> data;
  if(type==0x80 && request==6){
   if((value>>8)==1){data={18,1,0,2,0,0,0,64,9,0x12,u8(0x40+device-16),0x57,0,1,0,0,0,1};}
   else if((value>>8)==2){data=port.kind==WII_USB_MASS_STORAGE?std::vector<u8>{9,2,32,0,1,1,0,0x80,50,9,4,0,0,2,8,6,0x50,0,7,5,0x81,2,64,0,0,7,5,2,2,64,0,0}:std::vector<u8>{9,2,34,0,1,1,0,0x80,50,9,4,0,0,1,3,1,1,0,9,0x21,0x11,1,0,1,0x22,63,0,7,5,0x81,3,8,0,10};}
   else return 1;
  }else if(type==0x81 && request==6 && value==0x2200 && port.kind==WII_USB_KEYBOARD)data={5,1,9,6,0xa1,1,5,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x95,1,0x75,8,0x81,1,0x95,5,0x75,1,5,8,0x19,1,0x29,5,0x91,2,0x95,1,0x75,3,0x91,1,0x95,6,0x75,8,0x15,0,0x25,0x65,5,7,0x19,0,0x29,0x65,0x81,0,0xc0};
  else if(type==0x21 && port.kind==WII_USB_KEYBOARD && request==0x0a && !(value&255) && !v[6].size)port.idle=u8(value>>8);
  else if(type==0x21 && port.kind==WII_USB_KEYBOARD && request==0x0b && value<=1 && !v[6].size)port.protocol=u8(value);
  else if(type==0x21 && port.kind==WII_USB_KEYBOARD && request==9 && value==0x200 && v[6].size==1)port.leds=v[6].data[0]&31;
  else if(type==0xa1 && port.kind==WII_USB_KEYBOARD && (request==2 || request==3) && v[6].size==1)data={request==3?port.protocol:port.idle};
  else if(type==0xa1 && port.kind==WII_USB_KEYBOARD && request==1 && value==0x100 && v[6].size==8){data.assign(8,0);if(p.keyboard.connected){data[0]=p.keyboard.modifiers;std::memcpy(data.data()+2,p.keyboard.keys,6);}}
  else if(type==0xa1 && request==0xfe && port.kind==WII_USB_MASS_STORAGE && v[6].size==1)data={0};
  else if(type==0x21 && request==0xff && port.kind==WII_USB_MASS_STORAGE && !v[6].size)port.bulk.reset();
  else if(type==0 && request==9 && value<=1 && !v[6].size)port.configuration=u8(value);
  else if(type==0x80 && request==8 && v[6].size==1)data={port.configuration};
  else if(type==2 && request==1 && !v[6].size){} /* clear endpoint halt */
  else return 1;
  const u32 size=std::min<u32>(v[6].size,u32(data.size()));if(size)publish(cpu,v[6].address,data.data(),size);*result=s32((type&0x80)?size:v[6].size);return 1;
 }
 return 0;
}
}
extern "C" int wii_peripherals_submit(WiiPeripherals* p,CPUState* cpu,const u8* r,u32 message,u8 device,s32* result){
 if(!p || !cpu || !r || !result)return 0;*result=invalid;
 try{return device==13?sd_submit(*p,cpu,r,result):device>=15 && device<=17?usb_submit(*p,cpu,r,message,device,result):0;}catch(...){*result=io_error;return 1;}
}
