/* SPDX-License-Identifier: MIT */
#include "wiillr/fs.h"
#include "FS.h"
#include <fstream>
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef CreateFile
#undef CreateDirectory
#endif
namespace fs=std::filesystem;
namespace nand=IOS::HLE::FS;
struct WiiFs {
  fs::path root;
  nand::HostFileSystem backend;
  uint32_t uid;
  uint16_t gid;
  uint32_t content_handles=0;
  WiiFs(const fs::path& p,uint32_t u,uint16_t g):root(p),backend(p.generic_string()),uid(u),gid(g){}
};
static int32_t result(nand::ResultCode code){return code==nand::ResultCode::Success?0:-100-int32_t(code);}
template<class T> static int32_t result(const nand::Result<T>& r){return r?int32_t(*r):result(r.error());}
static bool reparse(const fs::path& p) {
#ifdef _WIN32
  const DWORD a=GetFileAttributesW(p.c_str());
  return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
  return fs::is_symlink(fs::symlink_status(p));
#endif
}
static void check_path(WiiFs* s,const std::string& path) {
  if(!nand::IsValidPath(path))throw std::invalid_argument("invalid NAND path");
  if(path=="/")return;
  fs::path host=s->root;
  for(const auto& component:SplitString(path.substr(1),'/')) {
    if(component.empty() || component=="." || component==".." || component.back()=='.' || component.back()==' ' ||
       component.size()>nand::MaxFilenameLength)throw std::invalid_argument("invalid NAND component");
    std::string stem=component.substr(0,component.find('.'));
    std::ranges::transform(stem,stem.begin(),[](unsigned char c){return char(std::toupper(c));});
    if(stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL" ||
       (stem.size()==4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3]>='1' && stem[3]<='9'))
      throw std::invalid_argument("reserved host component");
    host/=Common::EscapePath(component);
    if(reparse(host))throw std::invalid_argument("NAND reparse point");
  }
}
static std::string path_param(WiiFs* s,const uint8_t* p,size_t n) {
  if(!p || n<64 || !std::memchr(p,0,64))throw std::invalid_argument("invalid path buffer");
  std::string path(reinterpret_cast<const char*>(p));check_path(s,path);return path;
}
static uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
static void put32(uint8_t* p,uint32_t v){for(int i=3;i>=0;--i){p[i]=uint8_t(v);v>>=8;}}
extern "C" WiiFs* wii_fs_mount(const char* profile,uint32_t uid,uint16_t gid) {
  try {
    const fs::path p=fs::absolute(profile);
    if(!fs::is_directory(p) || reparse(p))return nullptr;
    std::ifstream marker(p/".wiillr-nand-profile");std::string magic;std::getline(marker,magic);
    if(magic!="wiillr-copied-nand-v1")return nullptr;
    for(auto part=p;!part.empty() && part!=part.root_path();part=part.parent_path())if(reparse(part))return nullptr;
    for(const auto& e:fs::recursive_directory_iterator(p))if(reparse(e.path()))return nullptr;
    return new WiiFs(p,uid,gid);
  } catch(const std::exception& e){std::fprintf(stderr,"NAND mount: %s\n",e.what());return nullptr;}
}
extern "C" void wii_fs_unmount(WiiFs* s){delete s;}
extern "C" bool wii_fs_bt_read(void* user,uint8_t* data,uint32_t capacity,uint32_t* size) {
  try {
    const auto path=static_cast<WiiFs*>(user)->root/".wiillr-bluetooth";
    if(reparse(path))return false;
    if(!fs::exists(path)){*size=0;return true;}
    const auto bytes=fs::file_size(path);if(bytes>capacity || bytes>512)return false;
    std::ifstream file(path,std::ios::binary);file.read(reinterpret_cast<char*>(data),bytes);
    if(!file)return false;*size=uint32_t(bytes);return true;
  }catch(...){return false;}
}
extern "C" bool wii_fs_bt_write(void* user,const uint8_t* data,uint32_t size) {
  try {
    const auto path=static_cast<WiiFs*>(user)->root/".wiillr-bluetooth";
    const auto temporary=static_cast<WiiFs*>(user)->root/".wiillr-bluetooth.tmp";
    if(size>512 || reparse(path) || reparse(temporary) || fs::exists(temporary))return false;
    {std::ofstream file(temporary,std::ios::binary);file.write(reinterpret_cast<const char*>(data),size);file.close();if(!file)return false;}
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    fs::rename(temporary,path);return true;
#endif
  }catch(...){return false;}
}
extern "C" bool wii_fs_boot_title(WiiFs* s,uint64_t* title) {
  try {
    File::IOFile users((s->root/"sys/uid.sys").generic_string(),"rb");
    if(!users || users.GetSize()%12 || users.GetSize()>0x100000)return false;
    uint8_t record[12];uint64_t found=0;unsigned matches=0;
    while(users.ReadArray(record,12))if(be32(record+8)==s->uid) {found=uint64_t(be32(record))<<32|be32(record+4);++matches;}
    if(matches!=1)return false;
    char path[80];std::snprintf(path,sizeof path,"/title/%08x/%08x/content/title.tmd",uint32_t(found>>32),uint32_t(found));
    check_path(s,path);
    File::IOFile tmd(s->root.generic_string()+path,"rb");
    uint8_t signature[4],header[0xa4];if(!tmd || !tmd.ReadArray(signature,4))return false;
    const uint32_t kind=be32(signature),base=kind==0x10000?0x240:kind==0x10001?0x140:kind==0x10002?0x80:0;
    if(!base || !tmd.Seek(base,File::SeekOrigin::Begin) || !tmd.ReadArray(header,sizeof header))return false;
    const uint64_t tmd_title=uint64_t(be32(header+0x4c))<<32|be32(header+0x50);
    if(found!=tmd_title || uint16_t(header[0x58]<<8|header[0x59])!=s->gid)return false;
    *title=found;return true;
  }catch(...){return false;}
}
extern "C" int32_t wii_es_ticket_view(WiiFs* s,const uint8_t* ticket,uint32_t size,uint8_t* view) {
  try {
    std::array<uint8_t,0x2a4> installed{};
    if(!size) {
      uint64_t title;if(!wii_fs_boot_title(s,&title))return -1017;
      char path[80];std::snprintf(path,sizeof path,"/ticket/%08x/%08x.tik",uint32_t(title>>32),uint32_t(title));
      check_path(s,path);File::IOFile file(s->root.generic_string()+path,"rb");
      if(!file)return -1028; /* ES_NO_TICKET */
      if(file.GetSize()!=installed.size() || !file.ReadArray(installed.data(),installed.size()))return -1017;
      if((uint64_t(be32(installed.data()+0x1dc))<<32|be32(installed.data()+0x1e0))!=title)return -1017;
      ticket=installed.data();size=uint32_t(installed.size());
    }
    if(!ticket || !view || size!=0x2a4 || be32(ticket)!=0x10001 || ticket[0x1bc]!=0)return -1017;
    /* RSA2048 v0 view: four-byte version/padding, then the unchanged ticket
     * fields starting at ticket_id. Encrypted title-key bytes are excluded.
     * This query does not establish a valid signature or usable key selector. */
    std::memset(view,0,4);view[0]=ticket[0x1bc];
    std::memmove(view+4,ticket+0x1d0,0xd4);return 0;
  }catch(const std::exception&){return -117;}
}
extern "C" bool wii_es_unlimited_ticket(WiiFs* s,uint64_t id,int32_t* status) {
  std::array<uint8_t,0xd8> view{};
  *status=wii_es_ticket_view(s,nullptr,0,view.data());if(*status)return true;
  if((uint64_t(be32(view.data()+4))<<32|be32(view.data()+8))!=id){*status=-1028;return true;}
  for(unsigned i=0;i<8;i++)if(be32(view.data()+0x98+i*8))return false;
  *status=0;return true;
}
extern "C" int32_t wii_fs_open(WiiFs* s,const char* path,uint32_t mode) {
  try {
    if(std::getenv("WIILLR_IOS_TRACE"))std::fprintf(stderr,"NAND open mode=%u path=%s\n",mode,path);
    check_path(s,path);if(mode<1 || mode>3)return -101;
    const auto metadata=s->backend.GetMetadata(s->uid,s->gid,path);
    if(!metadata)return result(metadata.error());
    const auto allowed=s->uid==0?3:uint8_t(metadata->uid==s->uid?metadata->modes.owner:metadata->gid==s->gid?metadata->modes.group:metadata->modes.other);
    if((allowed&mode)!=mode)return -102;
    auto opened=s->backend.OpenFile(s->uid,s->gid,path,nand::Mode(mode));
    return opened?int32_t(opened->Release()):result(opened.error());
  }catch(const std::invalid_argument&){return -101;}catch(const std::exception&){return -117;}
}
extern "C" int32_t wii_fs_close(WiiFs* s,uint32_t fd){try{return result(s->backend.Close(fd));}catch(...){return -117;}}
extern "C" int32_t wii_fs_read(WiiFs* s,uint32_t fd,uint8_t* p,uint32_t n){try{return result(s->backend.ReadBytesFromFile(fd,p,n));}catch(...){return -117;}}
extern "C" int32_t wii_fs_write(WiiFs* s,uint32_t fd,const uint8_t* p,uint32_t n){try{return result(s->backend.WriteBytesToFile(fd,p,n));}catch(...){return -117;}}
extern "C" int32_t wii_fs_seek(WiiFs* s,uint32_t fd,uint32_t at,uint32_t mode){try{return result(s->backend.SeekFile(fd,at,nand::SeekMode(mode)));}catch(...){return -117;}}
extern "C" int32_t wii_fs_read_directory(WiiFs* s,const uint8_t* input,uint8_t* names,uint32_t maximum,uint32_t* count) {
  try {
    const auto path=path_param(s,input,64);
    if(std::getenv("WIILLR_IOS_TRACE"))std::fprintf(stderr,"NAND readdir path=%s maximum=%u listing=%u\n",path.c_str(),maximum,names!=nullptr);
    auto entries=s->backend.ReadDirectory(s->uid,s->gid,path);if(!entries)return result(entries.error());
    for(const auto& name:*entries)if(name.empty() || name.size()>12)return -101;
    if(!names){*count=uint32_t(entries->size());return 0;}
    if(maximum>UINT32_MAX/13)return -101;
    *count=std::min(maximum,uint32_t(entries->size()));
    for(uint32_t i=0;i<*count;i++) {
      const auto& name=(*entries)[i];std::memset(names,0,13);
      std::memcpy(names,name.data(),name.size());names+=name.size()+1;
    }
    return 0;
  }catch(const std::invalid_argument&){return -101;}catch(const std::exception&){return -117;}
}
extern "C" int32_t wii_fs_directory_usage(WiiFs* s,const uint8_t* input,uint32_t* clusters,uint32_t* inodes) {
  try {
    const auto path=path_param(s,input,64);
    const auto stats=s->backend.GetDirectoryStats(path);if(!stats)return result(stats.error());
    *clusters=stats->used_clusters;*inodes=stats->used_inodes;return 0;
  }catch(const std::invalid_argument&){return -101;}catch(const std::exception&){return -117;}
}
extern "C" int32_t wii_es_content_open(WiiFs* s,uint32_t index) {
  try {
    uint64_t title;if(index>0xffff || !wii_fs_boot_title(s,&title))return -1017;
    char prefix[80];std::snprintf(prefix,sizeof prefix,"/title/%08x/%08x/content/",uint32_t(title>>32),uint32_t(title));
    File::IOFile tmd(s->root.generic_string()+prefix+"title.tmd","rb");
    uint8_t signature[4],header[0xa4];if(!tmd || !tmd.ReadArray(signature,4))return -106;
    const uint32_t kind=be32(signature),base=kind==0x10000?0x240:kind==0x10001?0x140:kind==0x10002?0x80:0;
    if(!base || !tmd.Seek(base,File::SeekOrigin::Begin) || !tmd.ReadArray(header,sizeof header))return -1017;
    const uint32_t count=uint32_t(header[0x9e])<<8|header[0x9f];
    if(count>4096)return -1017;
    std::array<uint8_t,36> selected{},entry{};unsigned matches=0;
    for(uint32_t i=0;i<count;i++) {
      if(!tmd.ReadArray(entry.data(),entry.size()))return -1017;
      if((uint32_t(entry[4])<<8|entry[5])==index){selected=entry;++matches;}
    }
    if(matches!=1)return -1017;
    std::string path;
    if(selected[6]&0x80) {
      File::IOFile map((s->root/"shared1/content.map").generic_string(),"rb");
      if(!map || map.GetSize()%28)return -106;
      uint8_t record[28];unsigned shared_matches=0;
      while(map.ReadArray(record,sizeof record))if(!std::memcmp(record+8,selected.data()+16,20)) {
        for(unsigned i=0;i<8;i++)if(!std::isxdigit(record[i]))return -1017;
        path="/shared1/"+std::string(reinterpret_cast<char*>(record),8)+".app";++shared_matches;
      }
      if(shared_matches!=1)return -106;
    } else {
      char name[16];std::snprintf(name,sizeof name,"%08x.app",be32(selected.data()));path=std::string(prefix)+name;
    }
    check_path(s,path);
    // ES reads installed title content as the kernel. The active title/UID
    // was checked above; these handles are distinct from ordinary PPC files.
    auto file=s->backend.OpenFile(0,0,path,nand::Mode::Read);
    if(!file)return result(file.error());
    auto status=file->GetStatus();
    const uint64_t expected=uint64_t(be32(selected.data()+8))<<32|be32(selected.data()+12);
    if(!status || status->size!=expected)return -1017;
    const uint32_t handle=file->Release();
    if(handle>=32){s->backend.Close(handle);return -109;}
    s->content_handles|=1u<<handle;return int32_t(handle);
  }catch(const std::exception&){return -117;}
}
static bool content_handle(WiiFs* s,uint32_t handle){return handle<32 && (s->content_handles&(1u<<handle));}
extern "C" int32_t wii_es_content_read(WiiFs* s,uint32_t handle,uint8_t* data,uint32_t size) {
  return content_handle(s,handle)?wii_fs_read(s,handle,data,size):-1017;
}
extern "C" int32_t wii_es_content_seek(WiiFs* s,uint32_t handle,uint32_t offset,uint32_t mode) {
  return content_handle(s,handle) && mode<=2?wii_fs_seek(s,handle,offset,mode):-1017;
}
extern "C" int32_t wii_es_content_close(WiiFs* s,uint32_t handle) {
  if(!content_handle(s,handle))return -1017;
  const int32_t result=wii_fs_close(s,handle);if(!result)s->content_handles&=~(1u<<handle);return result;
}
extern "C" bool wii_fs_ioctl(WiiFs* s,int32_t fd,uint32_t command,const uint8_t* in,uint32_t ni,uint8_t* out,uint32_t no,int32_t* r) {
  *r=-101;
  try {
    if(command==11) {
      if(fd<0 || no<8 || !out)return true;
      auto status=s->backend.GetFileStatus(uint32_t(fd));if(!status){*r=result(status.error());return true;}
      put32(out,status->size);put32(out+4,status->offset);*r=0;return true;
    }
    if(fd>=0)return false;
    if(command==2) {
      if(no<28 || !out)return true;
      auto stats=s->backend.GetNandStats();if(!stats){*r=result(stats.error());return true;}
      const uint32_t values[]={stats->cluster_size,stats->free_clusters,stats->used_clusters,stats->bad_clusters,stats->reserved_clusters,stats->free_inodes,stats->used_inodes};
      for(unsigned i=0;i<7;++i)put32(out+4*i,values[i]);*r=0;return true;
    }
    if(command==3 || command==5 || command==9) {
      if(ni<74 || !in)return true;
      const auto path=path_param(s,in+6,ni-6);
      if(in[70]>3 || in[71]>3 || in[72]>3)return true;
      const nand::Modes modes{nand::Mode(in[70]),nand::Mode(in[71]),nand::Mode(in[72])};
      if(command==3)*r=result(s->backend.CreateDirectory(s->uid,s->gid,path,in[73],modes));
      if(command==9)*r=result(s->backend.CreateFile(s->uid,s->gid,path,in[73],modes));
      if(command==5)*r=result(s->backend.SetMetadata(s->uid,path,be32(in),uint16_t(in[4]<<8|in[5]),in[73],modes));
      return true;
    }
    if(command==6) {
      if(no<74 || !out)return true;const auto path=path_param(s,in,ni);
      auto m=s->backend.GetMetadata(s->uid,s->gid,path);if(!m){*r=result(m.error());return true;}
      std::memset(out,0,74);put32(out,m->uid);out[4]=uint8_t(m->gid>>8);out[5]=uint8_t(m->gid);
      out[70]=uint8_t(m->modes.owner);out[71]=uint8_t(m->modes.group);out[72]=uint8_t(m->modes.other);out[73]=m->attribute;*r=0;return true;
    }
    if(command==7){*r=result(s->backend.Delete(s->uid,s->gid,path_param(s,in,ni)));return true;}
    if(command==8){if(ni<128)return true;auto a=path_param(s,in,ni);auto b=path_param(s,in+64,ni-64);*r=result(s->backend.Rename(s->uid,s->gid,a,b));return true;}
    return false;
  } catch(const std::invalid_argument&){return true;}catch(const std::exception& e){std::fprintf(stderr,"NAND operation: %s\n",e.what());*r=-117;return true;}
}
