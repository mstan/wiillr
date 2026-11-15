/* SPDX-License-Identifier: MIT */
#include "wiillr/fs.h"
#include "FS.h"
#include "NWC24Identity.h"
#include <fstream>
#include "wiillr/network_profile.h"
#include "wiillr/menu_profile.h"
#include <cstdlib>
#include <random>
#include <sstream>
#include <bit>
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
    auto mounted=std::make_unique<WiiFs>(p,uid,gid);
    // IOS creates this public volatile staging directory at startup. Nintendo's
    // NAND safe-write path stages files here before renaming them into place.
    const nand::Modes temporary_modes{nand::Mode::ReadWrite,nand::Mode::ReadWrite,nand::Mode::ReadWrite};
    const auto created=mounted->backend.CreateDirectory(0,0,"/tmp",0,temporary_modes);
    if((created!=nand::ResultCode::Success && created!=nand::ResultCode::AlreadyExists) ||
       !fs::is_directory(p/"tmp") ||
       mounted->backend.SetMetadata(0,"/tmp",0,0,0,temporary_modes)!=nand::ResultCode::Success)return nullptr;
    return mounted.release();
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
static constexpr char menu_mod_settings[]="wiillr-menu-mods-v1\nwelcome-letter=1\n";
extern "C" bool wii_fs_menu_welcome_enabled(WiiFs* s,bool* enabled) {
  if(!s || !enabled)return false;
  uint8_t data[sizeof menu_mod_settings];uint32_t size=0;
  if(!profile_read(s,".wiillr-menu-mods",data,sizeof data,&size))return false;
  if(!size){std::error_code error;if(fs::exists(s->root/".wiillr-menu-mods",error) || error)return false;*enabled=true;return true;}
  constexpr size_t flag=sizeof menu_mod_settings-3;
  if(size!=sizeof menu_mod_settings-1 || (data[flag]!='0' && data[flag]!='1'))return false;
  auto expected=std::to_array(menu_mod_settings);expected[flag]=char(data[flag]);
  if(std::memcmp(data,expected.data(),size))return false;
  *enabled=data[flag]=='1';return true;
}
extern "C" bool wii_fs_menu_welcome_set_enabled(WiiFs* s,bool enabled) {
  bool prior;if(!wii_fs_menu_welcome_enabled(s,&prior))return false;
  auto data=std::to_array(menu_mod_settings);data[sizeof menu_mod_settings-3]=enabled?'1':'0';
  return profile_write(s,".wiillr-menu-mods",reinterpret_cast<const uint8_t*>(data.data()),data.size()-1);
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
extern "C" int32_t wii_fs_wc24_generate_id(WiiFs* s,uint64_t* id,uint32_t* stage) {
  try {
    if(!s || !id || !stage)return -3;
    constexpr char config_path[]="/shared2/wc24/nwc24msg.cfg";
    constexpr char backup_path[]="/shared2/wc24/nwc24msg.cbk";
    check_path(s,config_path);check_path(s,backup_path);
    std::array<uint8_t,1024> config{};
    {
      auto file=s->backend.OpenFile(12,12,config_path,nand::Mode::Read);
      if(!file)return -16;
      const auto info=file->GetStatus();
      if(!info || info->size!=config.size())return -18;
      const auto got=file->Read(config.data(),config.size());if(!got || *got!=config.size())return -18;
    }
    uint32_t checksum=0;for(unsigned n=0;n<1020;n+=4)checksum+=be32(config.data()+n);
    if(be32(config.data())!=0x57634366 || checksum!=be32(config.data()+1020) ||
       be32(config.data()+16)>31 || be32(config.data()+20)>2)return -14;
    if(be32(config.data()+4)!=8)return -27;
    const uint64_t previous_id=uint64_t(be32(config.data()+8))<<32|be32(config.data()+12);
    const auto previous_stage=be32(config.data()+20);
    if(previous_stage) {
      if(!previous_id || previous_id>9999999999999999ull)return -14;
      *id=previous_id;*stage=previous_stage;return previous_stage==1?-35:-36;
    }
    constexpr char settings_path[]="/title/00000001/00000002/data/setting.txt";
    check_path(s,settings_path);std::array<uint8_t,256> settings{};
    {
      auto file=s->backend.OpenFile(12,12,settings_path,nand::Mode::Read);
      if(!file)return -16;
      const auto info=file->GetStatus();
      if(!info || info->size!=settings.size())return -18;
      const auto got=file->Read(settings.data(),settings.size());if(!got || *got!=settings.size())return -18;
    }
    std::string decoded;uint32_t key=0x73b5dbfa;
    for(const auto byte:settings) {
      if(!byte)break;const char c=char(byte^uint8_t(key));
      if(c!='\r')decoded+=c;key=(key<<1)|(key>>31);
    }
    const auto value=[&](const char* name) {
      std::string found,line;std::istringstream lines(decoded);unsigned count=0;
      while(std::getline(lines,line))if(line.starts_with(std::string(name)+"=")) {
        found=line.substr(std::strlen(name)+1);++count;
      }
      return count==1?found:std::string{};
    };
    const auto area=value("AREA"),model=value("MODEL");
    constexpr std::pair<const char*,uint8_t> areas[]={{"JPN",0},{"USA",1},{"EUR",2},{"AUS",2},
      {"BRA",1},{"TWN",3},{"ROC",3},{"KOR",4},{"HKG",5},{"ASI",5},{"LTN",1},{"SAF",2},{"CHN",6}};
    uint8_t area_code=7;for(const auto& [name,code]:areas)if(area==name)area_code=code;
    const uint8_t hardware=model.starts_with("RVL")?1:model.starts_with("RVD")?2:
      (model.starts_with("RVT") || model.starts_with("RVV"))?0:7;
    if(area_code==7 || hardware==7)return -1;
    // A copied offline profile has its own virtual chip identity. This is not
    // an imported console key/certificate or an authenticated network identity.
    uint8_t chip_bytes[4];uint32_t chip_size=0;
    if(!profile_read(s,".wiillr-console-id",chip_bytes,sizeof chip_bytes,&chip_size))return -18;
    if(!chip_size) {
      std::random_device random;uint32_t chip=0;
      for(unsigned attempts=0;!chip && attempts<4;attempts++)chip=uint32_t(random());
      if(!chip)return -19;
      put32(chip_bytes,chip);
      if(!profile_write(s,".wiillr-console-id",chip_bytes,sizeof chip_bytes))return -19;
    }else if(chip_size!=sizeof chip_bytes || !be32(chip_bytes))return -14;
    uint64_t generated=0;
    const auto generated_status=wii_nwc24_make_id(&generated,be32(chip_bytes),uint16_t(be32(config.data()+16)),hardware,area_code);
    if(generated_status)return generated_status;
    put32(config.data()+8,uint32_t(generated>>32));put32(config.data()+12,uint32_t(generated));
    put32(config.data()+16,(be32(config.data()+16)+1)&31);put32(config.data()+20,1);
    checksum=0;for(unsigned n=0;n<1020;n+=4)checksum+=be32(config.data()+n);
    put32(config.data()+1020,checksum);
    const nand::Modes modes{nand::Mode::ReadWrite,nand::Mode::ReadWrite,nand::Mode::ReadWrite};
    // Write the backup before the active configuration, as separate real NAND
    // writes. A failed write is never reported as successful ID generation.
    for(const char* path:{backup_path,config_path}) {
      auto file=s->backend.CreateAndOpenFile(12,12,path,modes);
      if(!file)return -16;
      const auto wrote=file->Write(config.data(),config.size());
      if(!wrote || *wrote!=config.size())return -19;
    }
    *id=generated;*stage=1;return 0;
  }catch(const std::exception&){return -19;}
}
extern "C" int32_t wii_fs_network_config(WiiFs* s,uint8_t* data) {
  try {
    if(!s || !data)return -1017;
    const std::string path="/shared2/sys/net/02/config.dat";check_path(s,path);
    std::array<uint8_t,WIILLR_NETWORK_CONFIG_SIZE> bytes{};
    const auto host=s->root/path.substr(1);
    if(fs::exists(host)) {
      auto file=s->backend.OpenFile(0,0,path,nand::Mode::Read);
      if(!file)return -117;
      const auto info=file->GetStatus();if(!info || info->size!=bytes.size())return -117;
      const auto got=file->Read(bytes.data(),bytes.size());if(!got || *got!=bytes.size())return -117;
    }
    if(wii_network_seed_config(bytes.data(),bytes.size())) {
      // Provision only a missing/old empty generated default. Existing owner
      // connections, including disabled profiles and credentials, stay exact.
      const nand::Modes modes{nand::Mode::ReadWrite,nand::Mode::ReadWrite,nand::Mode::ReadWrite};
      const auto created=s->backend.CreateFullPath(0,0,path,0,modes);if(created!=nand::ResultCode::Success)return result(created);
      const auto file=s->backend.CreateAndOpenFile(0,0,path,modes);
      if(!file)return -117;
      const auto wrote=file->Write(bytes.data(),bytes.size());if(!wrote || *wrote!=bytes.size())return -117;
    }
    std::memcpy(data,bytes.data(),bytes.size());return 0;
  }catch(const std::exception&){return -117;}
}
extern "C" int32_t wii_fs_network_config_write(WiiFs* s,const uint8_t* data) {
  try {
    if(!s || !data)return -1017;
    const std::string path="/shared2/sys/net/02/config.dat";check_path(s,path);
    const auto host=s->root/path.substr(1);
    // Preserve malformed existing data for an explicit repair operation.
    if(fs::exists(host) && (!fs::is_regular_file(host) || fs::file_size(host)!=WIILLR_NETWORK_CONFIG_SIZE))return -117;
    const nand::Modes modes{nand::Mode::ReadWrite,nand::Mode::ReadWrite,nand::Mode::ReadWrite};
    const auto created=s->backend.CreateFullPath(0,0,path,0,modes);if(created!=nand::ResultCode::Success)return result(created);
    // Reuse the backend's shared open file so other live NAND handles observe
    // the write in IOS order. A separate host replacement would orphan them.
    auto file=s->backend.CreateAndOpenFile(0,0,path,modes);if(!file)return -117;
    const auto wrote=file->Write(data,WIILLR_NETWORK_CONFIG_SIZE);
    return wrote && *wrote==WIILLR_NETWORK_CONFIG_SIZE?0:-117;
  }catch(const std::exception&){return -117;}
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
  try {
    if(!s || !status)return false;
    // The menu queries installed channel tickets before launching them. Ticket
    // IDs are independent of the active boot title, and one file can hold many.
    check_path(s,"/ticket");
    const auto groups=s->backend.ReadDirectory(0,0,"/ticket");
    if(!groups){*status=result(groups.error());return true;}
    const auto hex_name=[](const std::string& name) {
      return name.size()==8 && std::ranges::all_of(name,[](unsigned char c){return std::isxdigit(c);});
    };
    bool found=false;
    for(const auto& group:*groups) {
      if(!hex_name(group))continue;
      const std::string directory="/ticket/"+group;check_path(s,directory);
      const auto files=s->backend.ReadDirectory(0,0,directory);
      if(!files){*status=result(files.error());return true;}
      for(const auto& name:*files) {
        if(name.size()!=12 || name.substr(8)!=".tik" || !hex_name(name.substr(0,8)))continue;
        const auto title=std::stoull(group+name.substr(0,8),nullptr,16);
        uint32_t count=0;*status=wii_es_ticket_views(s,title,nullptr,0,&count);if(*status)return true;
        std::vector<uint8_t> views(count*0xd8);
        *status=wii_es_ticket_views(s,title,views.data(),count,&count);if(*status)return true;
        for(uint32_t n=0;n<count;n++) {
          const auto* view=views.data()+n*0xd8;
          if((uint64_t(be32(view+4))<<32|be32(view+8))!=id)continue;
          found=true;
          // No usage counters apply only when the real ticket has no limits.
          // Restricted tickets still require an implemented accounting service.
          for(unsigned i=0;i<8;i++)if(be32(view+0x98+i*8))return false;
        }
      }
    }
    *status=found?0:-1028;return true;
  }catch(const std::exception&){if(status)*status=-117;return true;}
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

static std::array<uint8_t,20> content_sha1(const std::vector<uint8_t>& bytes) {
  uint32_t h[]{0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
  const size_t padded=(bytes.size()+9+63)&~size_t(63);
  for(size_t offset=0;offset<padded;offset+=64) {
    std::array<uint8_t,64> block{};
    for(size_t n=0;n<64;n++) {
      const size_t at=offset+n;
      if(at<bytes.size())block[n]=bytes[at];else if(at==bytes.size())block[n]=0x80;
    }
    if(offset+64==padded) {
      uint64_t bits=uint64_t(bytes.size())*8;
      for(int n=63;n>=56;--n){block[n]=uint8_t(bits);bits>>=8;}
    }
    uint32_t w[80];for(unsigned n=0;n<16;n++)w[n]=be32(block.data()+4*n);
    for(unsigned n=16;n<80;n++)w[n]=std::rotl(w[n-3]^w[n-8]^w[n-14]^w[n-16],1);
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
    for(unsigned n=0;n<80;n++) {
      const uint32_t f=n<20 ? (b&c)|(~b&d):n<40 ? b^c^d:n<60 ? (b&c)|(b&d)|(c&d):b^c^d;
      const uint32_t k=n<20?0x5a827999:n<40?0x6ed9eba1:n<60?0x8f1bbcdc:0xca62c1d6;
      const uint32_t t=std::rotl(a,5)+f+e+k+w[n];e=d;d=c;c=std::rotl(b,30);b=a;a=t;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
  }
  std::array<uint8_t,20> hash{};for(unsigned n=0;n<5;n++)put32(hash.data()+4*n,h[n]);return hash;
}
extern "C" WiiMenuWelcomeResult wii_fs_menu_welcome(WiiFs* s,uint32_t rtc_seconds) {
  try {
    bool enabled;if(!wii_fs_menu_welcome_enabled(s,&enabled))return WII_MENU_WELCOME_INVALID;
    if(!enabled)return WII_MENU_WELCOME_DISABLED;
    uint64_t title=0;if(!wii_fs_boot_title(s,&title) || title!=0x100000002ull)return WII_MENU_WELCOME_INVALID;
    check_path(s,"/title/00000001/00000002/data/cdb.vff");
    if(!fs::exists(s->root/"title/00000001/00000002/data/cdb.vff"))return WII_MENU_WELCOME_NOT_READY;
    check_path(s,"/shared2/sys/SYSCONF");const auto settings=s->root/"shared2/sys/SYSCONF";
    if(!fs::is_regular_file(settings) || fs::file_size(settings)!=16384)return WII_MENU_WELCOME_INVALID;
    std::array<uint8_t,16384> config{};
    {std::ifstream f(settings,std::ios::binary);f.read(reinterpret_cast<char*>(config.data()),config.size());if(!f)return WII_MENU_WELCOME_IO_ERROR;}
    uint32_t local_seconds;if(!wii_menu_profile_local_seconds(config.data(),config.size(),rtc_seconds,&local_seconds))return WII_MENU_WELCOME_INVALID;
    check_path(s,"/title/00000001/00000002/data/nocopy/cdbwiiid.dat");
    const auto identity=s->root/"title/00000001/00000002/data/nocopy/cdbwiiid.dat";
    std::array<uint8_t,8> id{};
    if(fs::exists(identity)) {
      if(!fs::is_regular_file(identity) || fs::file_size(identity)!=40)return WII_MENU_WELCOME_INVALID;
      std::ifstream f(identity,std::ios::binary);f.read(reinterpret_cast<char*>(id.data()),id.size());if(!f)return WII_MENU_WELCOME_IO_ERROR;
    } else {
      std::vector<uint8_t> input(9);if(!wii_fs_network_mac(s,input.data()))return WII_MENU_WELCOME_IO_ERROR;
      input[6]=0x75;input[7]=0x79;input[8]=0x79;const auto hash=content_sha1(input);std::copy_n(hash.begin(),id.size(),id.begin());
    }
    const uint64_t wii_id=uint64_t(be32(id.data()))<<32|be32(id.data()+4);
    return wii_menu_welcome(s->root.string().c_str(),local_seconds,wii_id,true);
  }catch(const std::exception&){return WII_MENU_WELCOME_IO_ERROR;}
}
extern "C" bool wii_es_verify_content(WiiFs* s,uint64_t title,uint32_t index) {
  try {
    InstalledTmd tmd;if(read_installed_tmd(s,title,tmd))return false;
    const uint8_t* record=nullptr;
    for(uint32_t n=0;n<tmd.count;n++) {
      const auto* candidate=tmd.bytes.data()+tmd.base+0xa4+n*36;
      if((uint32_t(candidate[4])<<8|candidate[5])==index)record=candidate;
    }
    if(!record)return false;
    const uint64_t size=uint64_t(be32(record+8))<<32|be32(record+12);
    if(size>64*1024*1024)return false;
    std::vector<uint8_t> bytes(size);
    const int32_t handle=open_installed_content(s,title,index);if(handle<0)return false;
    const auto actual=wii_es_content_read(s,uint32_t(handle),bytes.data(),uint32_t(size));
    wii_es_content_close(s,uint32_t(handle));
    const auto hash=content_sha1(bytes);
    return actual==int64_t(size) && !std::memcmp(hash.data(),record+16,hash.size());
  }catch(...){return false;}
}
extern "C" void wii_es_release_launch(WiiTitleLaunch* launch) {
  if(!launch)return;std::free(launch->image);*launch={};
}
extern "C" bool wii_es_prepare_launch(WiiFs* s,uint64_t title,const uint8_t* view,WiiTitleLaunch* out,int32_t* status) {
  if(!status)return false;*status=-1017;
  try {
    if(!s || !view || !out || out->image)return true;
    uint32_t count=0;*status=wii_es_ticket_views(s,title,nullptr,0,&count);if(*status)return true;
    if(!count){*status=-1028;return true;}
    std::vector<uint8_t> views(count*0xd8);*status=wii_es_ticket_views(s,title,views.data(),count,&count);if(*status)return true;
    bool found=false;for(uint32_t n=0;n<count;n++)if(!std::memcmp(view,views.data()+n*0xd8,0xd8))found=true;
    if(!found){*status=-1017;return true;}
    for(unsigned n=0;n<8;n++)if(be32(view+0x98+n*8))return false; // usage accounting not implemented
    InstalledTmd tmd;*status=read_installed_tmd(s,title,tmd);if(*status)return true;
    const auto* header=tmd.bytes.data()+tmd.base;
    const uint32_t boot=uint32_t(header[0xa0])<<8|header[0xa1];
    const uint8_t* content=nullptr;
    for(uint32_t n=0;n<tmd.count;n++) {
      const auto* entry=header+0xa4+n*36;
      if((uint32_t(entry[4])<<8|entry[5])==boot){if(content){*status=-1017;return true;}content=entry;}
    }
    if(!content){*status=-1017;return true;}
    const uint64_t size=uint64_t(be32(content+8))<<32|be32(content+12);
    if(!size || size>64*1024*1024){*status=-1017;return true;}
    check_path(s,"/sys/uid.sys");File::IOFile users((s->root/"sys/uid.sys").generic_string(),"rb");
    const auto users_size=users?users.GetSize():0;
    if(!users || users_size%12 || users_size>0x100000){*status=-1009;return true;}
    std::map<uint32_t,uint64_t> uids;std::map<uint64_t,uint32_t> titles;uint8_t record[12];
    for(uint64_t n=0;n<users_size;n+=12) {
      if(!users.ReadArray(record,12)){*status=-1009;return true;}
      const uint64_t id=uint64_t(be32(record))<<32|be32(record+4);const auto uid=be32(record+8);
      if(!uids.emplace(uid,id).second || !titles.emplace(id,uid).second){*status=-1009;return true;}
    }
    if(!titles.contains(title)){*status=-1009;return true;}
    std::vector<uint8_t> image(size);const auto handle=open_installed_content(s,title,boot);
    if(handle<0){*status=handle;return true;}
    const auto got=wii_es_content_read(s,handle,image.data(),uint32_t(size));
    const auto closed=wii_es_content_close(s,handle);
    if(got!=int32_t(size) || closed){*status=-117;return true;}
    const auto hash=content_sha1(image);
    if(std::memcmp(hash.data(),content+16,20)){*status=-1022;return true;}
    auto* owned=static_cast<uint8_t*>(std::malloc(image.size()));if(!owned){*status=-117;return true;}
    std::memcpy(owned,image.data(),image.size());
    *out={title,uint64_t(be32(header+0x44))<<32|be32(header+0x48),titles.at(title),uint32_t(size),
          uint16_t(uint16_t(header[0x58])<<8|header[0x59]),owned};
    *status=0;return true;
  }catch(const std::exception&){*status=-117;return true;}
}
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
      if(std::getenv("WIILLR_IOS_TRACE"))std::fprintf(stderr,"NAND attributes op=%u uid=%u gid=%u path=%s modes=%u/%u/%u\n",
        command,s->uid,s->gid,path.c_str(),in[70],in[71],in[72]);
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
