/* SPDX-License-Identifier: MIT */
#include "wiillr/fs.h"
#include "FS.h"
#include <fstream>
#include <cstdlib>
#include <random>
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
  const uint32_t boot_uid;
  const uint16_t boot_gid;
  uint32_t content_handles=0;
  WiiFs(const fs::path& p,uint32_t u,uint16_t g):root(p),backend(p.generic_string()),uid(u),gid(g),boot_uid(u),boot_gid(g){}
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
static bool profile_read(WiiFs* s,const char* name,uint8_t* data,uint32_t capacity,uint32_t* size) {
  try {
    const auto path=s->root/name;
    if(reparse(path))return false;
    if(!fs::exists(path)){*size=0;return true;}
    const auto bytes=fs::file_size(path);if(bytes>capacity || bytes>512)return false;
    std::ifstream file(path,std::ios::binary);file.read(reinterpret_cast<char*>(data),bytes);
    if(!file)return false;*size=uint32_t(bytes);return true;
  }catch(...){return false;}
}
static bool profile_write(WiiFs* s,const char* name,const uint8_t* data,uint32_t size) {
  try {
    const auto path=s->root/name;
    const auto temporary=s->root/(std::string(name)+".tmp");
    if(size>512 || reparse(path) || reparse(temporary) || fs::exists(temporary))return false;
    {std::ofstream file(temporary,std::ios::binary);file.write(reinterpret_cast<const char*>(data),size);file.close();if(!file)return false;}
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    fs::rename(temporary,path);return true;
#endif
  }catch(...){return false;}
}
extern "C" bool wii_fs_bt_read(void* user,uint8_t* data,uint32_t capacity,uint32_t* size) {
  return profile_read(static_cast<WiiFs*>(user),".wiillr-bluetooth",data,capacity,size);
}
extern "C" bool wii_fs_bt_write(void* user,const uint8_t* data,uint32_t size) {
  return profile_write(static_cast<WiiFs*>(user),".wiillr-bluetooth",data,size);
}
extern "C" bool wii_fs_network_mac(WiiFs* s,uint8_t mac[6]) {
  try {
    if(!s || !mac)return false;
    uint8_t data[6];uint32_t size=0;
    if(!profile_read(s,".wiillr-network-mac",data,sizeof data,&size))return false;
    if(!size) {
      std::random_device random;for(auto& byte:data)byte=uint8_t(random());
      data[0]=(data[0]&0xfc)|2;
      if(!profile_write(s,".wiillr-network-mac",data,sizeof data))return false;
    }else if(size!=sizeof data || (data[0]&3)!=2)return false;
    std::memcpy(mac,data,sizeof data);return true;
  }catch(...){return false;}
}
extern "C" bool wii_fs_boot_title(WiiFs* s,uint64_t* title) {
  try {
    File::IOFile users((s->root/"sys/uid.sys").generic_string(),"rb");
    if(!users || users.GetSize()%12 || users.GetSize()>0x100000)return false;
    uint8_t record[12];uint64_t found=0;unsigned matches=0;
    while(users.ReadArray(record,12))if(be32(record+8)==s->boot_uid) {found=uint64_t(be32(record))<<32|be32(record+4);++matches;}
    if(matches!=1)return false;
    char path[80];std::snprintf(path,sizeof path,"/title/%08x/%08x/content/title.tmd",uint32_t(found>>32),uint32_t(found));
    check_path(s,path);
    File::IOFile tmd(s->root.generic_string()+path,"rb");
    uint8_t signature[4],header[0xa4];if(!tmd || !tmd.ReadArray(signature,4))return false;
    const uint32_t kind=be32(signature),base=kind==0x10000?0x240:kind==0x10001?0x140:kind==0x10002?0x80:0;
    if(!base || !tmd.Seek(base,File::SeekOrigin::Begin) || !tmd.ReadArray(header,sizeof header))return false;
    const uint64_t tmd_title=uint64_t(be32(header+0x4c))<<32|be32(header+0x50);
    if(found!=tmd_title || uint16_t(header[0x58]<<8|header[0x59])!=s->boot_gid)return false;
    *title=found;return true;
  }catch(...){return false;}
}
extern "C" int32_t wii_es_titles(WiiFs* s,uint8_t* output,uint32_t capacity,uint32_t* count) {
  try {
    if(!s || !count || (capacity && !output))return -1017;
    const auto hex_part=[](const std::string& name) {
      return name.size()==8 && std::ranges::all_of(name,[](unsigned char c){return std::isxdigit(c);});
    };
    const auto types=s->backend.ReadDirectory(0,0,"/title");
    if(!types)return result(types.error());
    std::vector<uint64_t> titles;
    for(const auto& high:*types) {
      if(!hex_part(high))continue;
      const auto prefix="/title/"+high;
      const auto lows=s->backend.ReadDirectory(0,0,prefix);if(!lows)continue;
      for(const auto& low:*lows) {
        if(!hex_part(low))continue;
        const auto path=prefix+"/"+low;check_path(s,path);
        if(!s->backend.ReadDirectory(0,0,path))continue;
        titles.push_back(uint64_t(std::stoul(high,nullptr,16))<<32|std::stoul(low,nullptr,16));
      }
    }
    if(titles.size()>UINT32_MAX)return -1017;
    std::ranges::sort(titles);
    *count=uint32_t(titles.size());
    for(uint32_t i=0;output && i<std::min(capacity,*count);++i) {
      put32(output+i*8,uint32_t(titles[i]>>32));put32(output+i*8+4,uint32_t(titles[i]));
    }
    return 0;
  }catch(const std::exception&){return -117;}
}
extern "C" int32_t wii_es_delete_title(WiiFs* s,uint64_t title) {
  try {
    if(!s || (uint32_t(title>>32)==1 && uint32_t(title)<=0x101))return -1017;
    char path[40];std::snprintf(path,sizeof path,"/title/%08x/%08x",uint32_t(title>>32),uint32_t(title));
    check_path(s,path);
    std::ifstream marker(s->root/".wiillr-nand-profile");std::string magic;std::getline(marker,magic);
    if(magic!="wiillr-copied-nand-v1")return -102;
    for(auto p=s->root;!p.empty() && p!=p.root_path();p=p.parent_path())if(reparse(p))return -102;
    const auto root=fs::weakly_canonical(s->root),target=fs::weakly_canonical(s->root/std::string(path+1));
    const auto contained=[](const fs::path& p,const fs::path& base) {
      const auto relative=p.lexically_relative(base);
      if(relative.empty() || relative.is_absolute())return false;
      for(const auto& part:relative)if(part=="..")return false;
      return true;
    };
    if(target==root || !contained(target,root))return -102;
    if(fs::exists(target)) {
      if(!fs::is_directory(target))return -1017;
      for(const auto& e:fs::recursive_directory_iterator(target))
        if(reparse(e.path()) || e.path().filename()==".git" || !contained(fs::weakly_canonical(e.path()),target))return -102;
    }
    return result(s->backend.Delete(0,0,path));
  }catch(const std::exception&){return -117;}
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
extern "C" int32_t wii_es_ticket_views(WiiFs* s,uint64_t title,uint8_t* output,uint32_t capacity,uint32_t* count) {
  try {
    if(!s || !count || (capacity && !output))return -1017;
    char path[80];std::snprintf(path,sizeof path,"/ticket/%08x/%08x.tik",uint32_t(title>>32),uint32_t(title));
    check_path(s,path);File::IOFile file(s->root.generic_string()+path,"rb");
    if(!file){*count=0;return 0;}
    const auto size=file.GetSize();if(!size || size>0x100000 || size%0x2a4)return -1017;
    std::vector<uint8_t> bytes(size);if(!file.ReadArray(bytes.data(),bytes.size()))return -117;
    const uint32_t total=uint32_t(size/0x2a4);
    for(uint32_t i=0;i<total;i++) {
      const auto* ticket=bytes.data()+i*0x2a4;
      if(be32(ticket)!=0x10001 || ticket[0x1bc]!=0 ||
         (uint64_t(be32(ticket+0x1dc))<<32|be32(ticket+0x1e0))!=title)return -1017;
    }
    *count=total;
    for(uint32_t i=0;output && i<std::min(capacity,total);i++) {
      const auto status=wii_es_ticket_view(s,bytes.data()+i*0x2a4,0x2a4,output+i*0xd8);
      if(status)return status;
    }
    return 0;
  }catch(const std::exception&){return -117;}
}
struct InstalledTmd {std::vector<uint8_t> bytes;uint32_t base=0,count=0;};
static int32_t read_installed_tmd(WiiFs* s,uint64_t title,InstalledTmd& tmd) {
    char path[80];std::snprintf(path,sizeof path,"/title/%08x/%08x/content/title.tmd",uint32_t(title>>32),uint32_t(title));
    check_path(s,path);File::IOFile file(s->root.generic_string()+path,"rb");if(!file)return -106;
    if(file.GetSize()<4 || file.GetSize()>0x100000)return -1017;
    auto& bytes=tmd.bytes;bytes.resize(file.GetSize());if(!file.ReadArray(bytes.data(),bytes.size()))return -117;
    const auto kind=be32(bytes.data());tmd.base=kind==0x10000?0x240u:kind==0x10001?0x140u:kind==0x10002?0x80u:0u;
    const auto base=tmd.base;
    if(!base || bytes.size()<base+0xa4)return -1017;
    const auto* header=bytes.data()+base;
    tmd.count=uint32_t(header[0x9e])<<8|header[0x9f];
    if(tmd.count>4096 || bytes.size()<base+0xa4+tmd.count*36 ||
       (uint64_t(be32(header+0x4c))<<32|be32(header+0x50))!=title)return -1017;
    return 0;
}
extern "C" uint32_t wii_fs_uid(WiiFs* s){return s?s->uid:0;}
extern "C" int32_t wii_es_set_uid(WiiFs* s,uint32_t caller_uid,uint64_t title) {
  try {
    if(!s)return -1017;
    check_path(s,"/sys/uid.sys");
    uint32_t menu_uid=0,target_uid=0,next_uid=0x1000;
    std::vector<uint8_t> records;
    {
      File::IOFile file((s->root/"sys/uid.sys").generic_string(),"rb");
      if(!file || file.GetSize()>0x100000 || file.GetSize()%12)return -1009;
      records.resize(file.GetSize());if(!file.ReadArray(records.data(),records.size()))return -1009;
    }
    std::map<uint32_t,uint64_t> uids;std::map<uint64_t,uint32_t> titles;
    for(size_t i=0;i<records.size();i+=12) {
      const auto* record=records.data()+i;const auto uid=be32(record+8);
      const uint64_t id=uint64_t(be32(record))<<32|be32(record+4);
      if(!uids.emplace(uid,id).second || !titles.emplace(id,uid).second || uid==UINT32_MAX)return -1009;
      next_uid=std::max(next_uid,uid+1);
      if(id==0x0000000100000002ull)menu_uid=uid;
      if(id==title)target_uid=uid;
    }
    // Only the system-menu identity that opened this ES handle may change
    // PPC credentials. The active ES title remains the booted title.
    if(!menu_uid || caller_uid!=menu_uid)return -1017;
    InstalledTmd tmd;const auto status=read_installed_tmd(s,title,tmd);if(status)return status;
    if(!target_uid) {
      if(titles.contains(title))return -1009;
      uint8_t record[12];put32(record,uint32_t(title>>32));put32(record+4,uint32_t(title));put32(record+8,next_uid);
      const auto file=s->backend.OpenFile(0,0,"/sys/uid.sys",nand::Mode::ReadWrite);
      if(!file || !file->Seek(0,nand::SeekMode::End) || !file->Write(record,sizeof record))return -1009;
      target_uid=next_uid;
    }
    const auto* header=tmd.bytes.data()+tmd.base;
    s->uid=target_uid;s->gid=uint16_t(header[0x58])<<8|header[0x59];return 0;
  }catch(const std::exception&){return -117;}
}
extern "C" int32_t wii_es_tmd_view(WiiFs* s,uint64_t title,uint8_t* output,uint32_t capacity,uint32_t* size) {
  try {
    if(!s || !size || (capacity && !output))return -1017;
    InstalledTmd tmd;const auto status=read_installed_tmd(s,title,tmd);if(status)return status;
    const auto* header=tmd.bytes.data()+tmd.base;const auto count=tmd.count,required=0x5c+count*16;
    if(output) {
      if(capacity<required)return -1017;
      std::memcpy(output,header+0x40,0x58);std::memcpy(output+0x58,header+0x9c,4);
      for(uint32_t i=0;i<count;i++)std::memcpy(output+0x5c+i*16,header+0xa4+i*36,16);
    }
    *size=required;return 0;
  }catch(const std::exception&){return -117;}
}
extern "C" int32_t wii_es_stored_contents(WiiFs* s,uint64_t title,uint8_t* output,uint32_t capacity,uint32_t* count) {
  try {
    if(!s || !count || (capacity && !output))return -1017;
    InstalledTmd tmd;const auto status=read_installed_tmd(s,title,tmd);if(status)return status;
    std::vector<uint32_t> stored;
    for(uint32_t i=0;i<tmd.count;i++) {
      const auto* entry=tmd.bytes.data()+tmd.base+0xa4+i*36;
      std::string path;
      if(entry[6]&0x80) {
        check_path(s,"/shared1/content.map");
        File::IOFile map((s->root/"shared1/content.map").generic_string(),"rb");
        if(!map)continue;if(map.GetSize()%28)return -1017;
        uint8_t record[28];unsigned matches=0;
        while(map.ReadArray(record,sizeof record))if(!std::memcmp(record+8,entry+16,20)) {
          for(unsigned n=0;n<8;n++)if(!std::isxdigit(record[n]))return -1017;
          path="/shared1/"+std::string(reinterpret_cast<char*>(record),8)+".app";++matches;
        }
        if(!matches)continue;if(matches!=1)return -1017;
      } else {
        char name[80];std::snprintf(name,sizeof name,"/title/%08x/%08x/content/%08x.app",uint32_t(title>>32),uint32_t(title),be32(entry));path=name;
      }
      check_path(s,path);auto file=s->backend.OpenFile(0,0,path,nand::Mode::Read);
      if(file)stored.push_back(be32(entry));
    }
    *count=uint32_t(stored.size());
    for(uint32_t i=0;output && i<std::min(capacity,*count);i++)put32(output+i*4,stored[i]);
    return 0;
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
static int32_t open_installed_content(WiiFs* s,uint64_t title,uint32_t index) {
  try {
    if(index>0xffff)return -1017;
    char prefix[80];std::snprintf(prefix,sizeof prefix,"/title/%08x/%08x/content/",uint32_t(title>>32),uint32_t(title));
    InstalledTmd tmd;const auto tmd_status=read_installed_tmd(s,title,tmd);if(tmd_status)return tmd_status;
    std::array<uint8_t,36> selected{};unsigned matches=0;
    for(uint32_t i=0;i<tmd.count;i++) {
      const auto* entry=tmd.bytes.data()+tmd.base+0xa4+i*36;
      if((uint32_t(entry[4])<<8|entry[5])==index){std::memcpy(selected.data(),entry,36);++matches;}
    }
    if(matches!=1)return -1017;
    std::string path;
    if(selected[6]&0x80) {
      check_path(s,"/shared1/content.map");
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
    // ES content access uses the kernel identity. The caller must have checked
    // the active title or an actual installed ticket view before this helper.
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
extern "C" int32_t wii_es_content_open(WiiFs* s,uint32_t index) {
  uint64_t title;if(!wii_fs_boot_title(s,&title))return -1017;
  return open_installed_content(s,title,index);
}
extern "C" int32_t wii_es_title_content_open(WiiFs* s,uint64_t title,const uint8_t* view,uint32_t index) {
  try {
    if(!view || index>0xffff)return -1017;
    uint32_t count=0;auto status=wii_es_ticket_views(s,title,nullptr,0,&count);if(status)return status;
    if(!count)return -1028;
    std::vector<uint8_t> views(count*0xd8);status=wii_es_ticket_views(s,title,views.data(),count,&count);if(status)return status;
    bool found=false;
    for(uint32_t i=0;i<count;i++)if(!std::memcmp(view,views.data()+i*0xd8,0xd8))found=true;
    if(!found)return -1017;
    // Matching an installed view is a metadata/access check, not signature
    // authentication. Offline installation verifies content hashes separately.
    return open_installed_content(s,title,index);
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
