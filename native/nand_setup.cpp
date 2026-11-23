/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/nand_setup.h"
#include "wiillr/aes128.hpp"
#include "wiillr/sha1.hpp"
#include "wiillr/nand_controller.hpp"
#include "wiillr/fs.h"
#include "wiillr/menu_profile.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <bit>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace fs=std::filesystem;
namespace {
using Bytes=std::vector<uint8_t>;using View=std::span<const uint8_t>;
struct Failure:std::runtime_error {WiiNandSetupStatus status;Failure(WiiNandSetupStatus s,const std::string& m):runtime_error(m),status(s){}};
void need(bool b,const std::string& m,WiiNandSetupStatus s=WII_NAND_SETUP_INVALID){if(!b)throw Failure(s,m);}
uint16_t u16(View v,size_t n){need(n<=v.size() && v.size()-n>=2,"Truncated system metadata");return uint16_t(v[n])<<8|v[n+1];}
uint32_t u32(View v,size_t n){return uint32_t(u16(v,n))<<16|u16(v,n+2);}
uint64_t u64(View v,size_t n){return uint64_t(u32(v,n))<<32|u32(v,n+4);}
std::string hex(uint64_t v,unsigned n=8){char b[17];std::snprintf(b,sizeof b,"%0*llx",int(n),(unsigned long long)v);return b;}
fs::path path(const char* p){need(p && *p,"Select a Wii NAND backup or extracted system folder");std::u8string t;for(;*p;++p)t.push_back(char8_t(uint8_t(*p)));return fs::path(t);}
std::string utf8(const fs::path& p){const auto s=p.generic_u8string();return {s.begin(),s.end()};}
bool linked(const fs::path& p){
#ifdef _WIN32
 const auto a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
 return fs::is_symlink(fs::symlink_status(p));
#endif
}
bool reserved_name(const std::string& name){
 std::string stem=name.substr(0,name.find('.'));std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return char(std::toupper(c));});
 return stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL" || (stem.size()==4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3]>='1' && stem[3]<='9');
}
void chain(const fs::path& p){for(auto q=fs::absolute(p);;q=q.parent_path()){need(!linked(q),"Linked system paths are unsupported");if(q==q.root_path())break;}}
bool within(const fs::path& p,const fs::path& root){auto a=p.begin();for(const auto& b:root){if(a==p.end())return false;
#ifdef _WIN32
 if(CompareStringOrdinal(a->c_str(),int(a->native().size()),b.c_str(),int(b.native().size()),TRUE)!=CSTR_EQUAL)return false;
#else
 if(*a!=b)return false;
#endif
 ++a;}return true;}
Bytes read_file(const fs::path& p,uint64_t max=64u*1024*1024){chain(p);need(fs::is_regular_file(p),"Missing system file: "+utf8(p.filename()),WII_NAND_SETUP_MISSING);const auto n=fs::file_size(p);need(n<=max,"System file is too large");Bytes b(static_cast<size_t>(n));std::ifstream f(p,std::ios::binary);need(bool(f) && (!n || bool(f.read((char*)b.data(),n))) && f.peek()==EOF,"Cannot read the complete system file",WII_NAND_SETUP_IO_ERROR);return b;}
struct Hash {
 std::array<uint32_t,5> h={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};std::array<uint8_t,64> tail{};size_t used=0;uint64_t size=0;
 void add(View b){size+=b.size();for(auto v:b){tail[used++]=v;if(used==64){wiillr::sha1_compress(h,tail.data());used=0;}}}
 std::array<uint8_t,20> finish(){const auto bits=size*8;tail[used++]=0x80;if(used>56){std::fill(tail.begin()+used,tail.end(),0);wiillr::sha1_compress(h,tail.data());used=0;}std::fill(tail.begin()+used,tail.end(),0);for(unsigned i=0;i<8;++i)tail[63-i]=uint8_t(bits>>(i*8));wiillr::sha1_compress(h,tail.data());std::array<uint8_t,20> b{};for(unsigned i=0;i<20;++i)b[i]=uint8_t(h[i/4]>>(24-8*(i%4)));return b;}
};
std::array<uint8_t,20> digest(View b){Hash h;h.add(b);return h.finish();}
std::string hash_text(View b){std::string s;for(auto v:b){s.push_back("0123456789abcdef"[v>>4]);s.push_back("0123456789abcdef"[v&15]);}return s;}
struct Entry {std::string name;bool directory=false;uint64_t size=0;uint16_t cluster=0;uint32_t uid=0;uint16_t gid=0;uint8_t mode=0,attribute=0;fs::file_time_type modified{};};
struct Source {
 fs::path origin;std::vector<Entry> entries;virtual ~Source()=default;
 virtual Bytes read(const std::string& name)=0;virtual void unchanged()=0;virtual WiiNandSourceKind kind()const=0;
 bool has(const std::string& n)const{return std::any_of(entries.begin(),entries.end(),[&](const Entry& e){return !e.directory && e.name==n;});}
};
struct Folder:Source {
 explicit Folder(const fs::path& p){origin=fs::canonical(p);chain(origin);uint64_t total=0;
  // Reject a containing folder (including Downloads or a drive root) before
  // walking any unrelated files. These are required native Menu dependencies.
  for(const char* name:{"title/00000001/00000002/content/title.tmd","ticket/00000001/00000002.tik","sys/uid.sys","shared2/sys/SYSCONF"}){
   const auto file=origin/name;chain(file);need(fs::is_regular_file(file),"Choose the NAND root containing title, ticket, sys and shared2",WII_NAND_SETUP_MISSING);
  }
  for(const auto& f:fs::recursive_directory_iterator(origin)){need(!linked(f.path()) && f.path().filename()!=".git","Linked or repository content cannot be imported");need(f.is_directory() || f.is_regular_file(),"Unsupported entry in the NAND folder");Entry e;e.name=utf8(f.path().lexically_relative(origin));e.directory=f.is_directory();e.size=e.directory?0:f.file_size();e.modified=f.last_write_time();total+=e.size;need(entries.size()<131072 && total<=uint64_t(2)*1024*1024*1024,"The selected folder is not a supported Wii NAND tree");entries.push_back(e);}
  std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.name<b.name;});}
 Bytes read(const std::string& n)override{need(has(n),"Missing system file: "+n,WII_NAND_SETUP_MISSING);return read_file(origin/fs::u8path(n));}
 void unchanged()override{for(const auto& e:entries){const auto p=origin/fs::u8path(e.name);need(fs::exists(p) && !linked(p) && fs::last_write_time(p)==e.modified && (e.directory || fs::file_size(p)==e.size),"The source changed. Close other programs using this NAND and try again.",WII_NAND_SETUP_IO_ERROR);}}
 WiiNandSourceKind kind()const override{return WII_NAND_SOURCE_EXTRACTED;}
};
constexpr uint64_t bootmii_image_bytes = 0x21000000;
constexpr uint64_t bootmii_image_with_keys_bytes = bootmii_image_bytes + 0x400;

struct Raw:Source {
 std::ifstream file;uint64_t raw_size;fs::file_time_type modified;Bytes super;wiillr::Aes128Key aes{};std::map<std::string,size_t> files;
 Bytes physical(uint64_t at,size_t n){Bytes b(n);file.clear();file.seekg(at);need(bool(file.read((char*)b.data(),n)),"Cannot read NAND backup",WII_NAND_SETUP_IO_ERROR);return b;}
 Bytes logical(uint32_t at,size_t n,bool ecc=true){Bytes b(n);size_t done=0;while(done<n){const uint32_t offset=at+uint32_t(done),page=offset/0x800;auto raw=physical(uint64_t(page)*0x840,0x840);if(ecc)for(unsigned i=0;i<4;++i)need(wiillr::NandController::ecc(raw.data()+i*512)==u32(raw,0x830+i*4),"NAND backup has an ECC error; obtain a verified backup before importing");const auto amount=std::min(n-done,size_t(0x800-offset%0x800));std::copy_n(raw.data()+offset%0x800,amount,b.data()+done);done+=amount;}return b;}
 explicit Raw(const fs::path& p,const char* keys){
  origin=fs::canonical(p);chain(origin);raw_size=fs::file_size(origin);modified=fs::last_write_time(origin);
  if(raw_size!=bootmii_image_bytes && raw_size!=bootmii_image_with_keys_bytes)
   throw Failure(WII_NAND_SETUP_UNSUPPORTED,"The selected file is "+std::to_string(raw_size)+
    " bytes. Choose a BootMii nand.bin with spare/ECC data ("+std::to_string(bootmii_image_bytes)+
    " bytes, or "+std::to_string(bootmii_image_with_keys_bytes)+" with appended keys), or an extracted NAND folder.");
  file.open(origin,std::ios::binary);need(bool(file),"Cannot open the NAND backup",WII_NAND_SETUP_IO_ERROR);
  Bytes otp;if(raw_size==bootmii_image_with_keys_bytes){auto footer=physical(bootmii_image_bytes,0x400);need(std::memcmp(footer.data(),"BackupMii v1",12)==0,"The appended NAND key backup is not BootMii v1");otp={footer.begin()+0x100,footer.begin()+0x180};}
  if(keys && *keys){auto b=read_file(path(keys),0x400);if(b.size()==0x400){need(std::memcmp(b.data(),"BackupMii v1",12)==0,"Unsupported key-backup format");b=Bytes(b.begin()+0x100,b.begin()+0x180);}need(b.size()==128,"Use a BootMii keys.bin or a 128-byte OTP backup");need(otp.empty() || b==otp,"The explicit key backup does not match this NAND");otp=std::move(b);}
  need(!otp.empty(),"This NAND needs console keys. Choose its matching keys.bin in Console keys, or place keys.bin beside nand.bin.",WII_NAND_SETUP_NEEDS_KEYS);std::array<uint8_t,16> key{};std::copy_n(otp.data()+0x58,16,key.data());need(std::any_of(key.begin(),key.end(),[](uint8_t b){return b!=0 && b!=255;}),"The NAND key backup is empty or invalid");aes=wiillr::aes128_expand(key);std::fill(key.begin(),key.end(),0);std::fill(otp.begin(),otp.end(),0);
  bool found=false;uint32_t generation=0,selected=0;for(unsigned i=0;i<16;++i){auto b=logical(0x1fc00000+i*0x40000,0x40000,false);if(std::memcmp(b.data(),"SFFS",4))continue;const auto seq=u32(b,4);if(!found || int32_t(seq-generation)>0){generation=seq;selected=0x1fc00000+i*0x40000;super=std::move(b);found=true;}}
  need(found,"No valid Wii filesystem superblock was found");super=logical(selected,0x40000);need((super[0x1000c+12]&3)==2,"Invalid NAND root directory");Entry root;root.directory=true;root.mode=super[0x1000c+12];root.attribute=super[0x1000c+13];root.uid=u32(super,0x1000c+22);root.gid=u16(super,0x1000c+26);entries.push_back(root);std::set<unsigned> visited,clusters;std::set<std::string> paths;
  auto walk=[&](auto&& self,uint16_t index,const std::string& parent,unsigned depth)->void {need(depth<32 && index<0x17ff && visited.insert(index).second,"NAND directory tree is cyclic or invalid");const size_t at=0x1000c+size_t(index)*32;View row(super.data()+at,32);std::string name((const char*)row.data(),std::find(row.begin(),row.begin()+12,0)-row.begin());need(!name.empty() && name!="." && name!=".." && name.back()!='.' && name.back()!=' ',"Unsupported NAND filename");for(unsigned char c:name)need(c>=32 && c<127 && std::string("\\/:*?\"<>|").find(char(c))==std::string::npos,"Unsupported NAND filename");need(!reserved_name(name),"NAND filename cannot be represented safely on the host");Entry e;e.name=parent.empty()?name:parent+"/"+name;need(e.name.size()+1<64,"NAND path exceeds the IOS limit");std::string folded=e.name;std::transform(folded.begin(),folded.end(),folded.begin(),[](unsigned char c){return char(std::tolower(c));});need(paths.insert(folded).second,"NAND names collide on the host filesystem");e.mode=row[12];e.attribute=row[13];e.directory=(e.mode&3)==2;need(e.directory || (e.mode&3)==1,"Unknown NAND node type");e.cluster=u16(row,14);e.size=e.directory?0:u32(row,18);e.uid=u32(row,22);e.gid=u16(row,26);entries.push_back(e);if(!e.directory){files[e.name]=entries.size()-1;auto c=e.cluster;uint64_t left=e.size;while(left){need(c<0x7f00 && clusters.insert(c).second,"NAND file allocation is cyclic or overlapping");left-=std::min(left,uint64_t(0x4000));c=u16(super,12+size_t(c)*2);}need(!e.size || c==0xfffb || c==0xffff,"NAND file allocation has an invalid terminator");}else{auto c=e.cluster;while(c!=0xffff){need(c<0x17ff,"Invalid NAND child index");const auto next=u16(super,0x1000c+size_t(c)*32+16);self(self,c,e.name,depth+1);c=next;}}};
  auto c=u16(super,0x1000c+14);while(c!=0xffff){need(c<0x17ff,"Invalid NAND root child");const auto next=u16(super,0x1000c+size_t(c)*32+16);walk(walk,c,"",0);c=next;}
 }
 ~Raw(){std::fill(aes.rounds.begin(),aes.rounds.end(),0);}
 Bytes read(const std::string& n)override{need(files.contains(n),"Missing system file: "+n,WII_NAND_SETUP_MISSING);const auto& e=entries[files.at(n)];need(e.size<=64u*1024*1024,"NAND file exceeds the supported size");Bytes b;auto c=e.cluster;while(b.size()<e.size){auto x=logical(uint32_t(c)*0x4000,0x4000);std::array<uint8_t,16> previous{};for(size_t at=0;at<x.size();at+=16){std::array<uint8_t,16> block{},encrypted{};std::copy_n(x.data()+at,16,encrypted.data());block=encrypted;wiillr::aes128_decrypt(aes,block);for(unsigned k=0;k<16;++k)x[at+k]=block[k]^previous[k];previous=encrypted;}const auto amount=std::min(x.size(),size_t(e.size-b.size()));b.insert(b.end(),x.begin(),x.begin()+amount);c=u16(super,12+size_t(c)*2);}return b;}
 void unchanged()override{need(fs::file_size(origin)==raw_size && fs::last_write_time(origin)==modified,"The source NAND backup changed during import",WII_NAND_SETUP_IO_ERROR);}
 WiiNandSourceKind kind()const override{return WII_NAND_SOURCE_BOOTMII;}
};
bool extracted_root(const fs::path& p){
 if(!fs::is_directory(p))return false;
 for(const char* name:{"title/00000001/00000002/content/title.tmd","ticket/00000001/00000002.tik","sys/uid.sys","shared2/sys/SYSCONF"})
  if(!fs::is_regular_file(p/name))return false;
 return true;
}
std::unique_ptr<Source> open(const char* source,const char* keys){
 auto p=path(source);chain(p);need(fs::exists(p),"The selected NAND source does not exist",WII_NAND_SETUP_MISSING);
 if(fs::is_directory(p)){
  if(extracted_root(p))return std::make_unique<Folder>(p);
  // Inspect only known immediate children; never search a containing drive or
  // Downloads tree for backups or keys.
  std::vector<fs::path> candidates;
  for(const char* name:{"nand","Wii"})if(extracted_root(p/name))candidates.push_back(p/name);
  if(fs::is_regular_file(p/"nand.bin"))candidates.push_back(p/"nand.bin");
  need(candidates.size()<=1,"This folder contains multiple Wii systems. Choose the exact NAND folder or nand.bin to import.",WII_NAND_SETUP_INVALID);
  need(!candidates.empty(),"Choose nand.bin, its containing folder, or the NAND root containing title, ticket, sys and shared2.",WII_NAND_SETUP_MISSING);
  p=candidates.front();chain(p);
  if(fs::is_directory(p))return std::make_unique<Folder>(p);
 }
 need(fs::is_regular_file(p),"Choose a NAND folder or BootMii backup");
 std::string companion;
 if((!keys || !*keys) && fs::file_size(p)==bootmii_image_bytes){
  const auto sibling=p.parent_path()/"keys.bin";
  if(fs::is_regular_file(sibling)){chain(sibling);companion=utf8(sibling);keys=companion.c_str();}
 }
 return std::make_unique<Raw>(p,keys);
}
struct Content {uint32_t id;uint16_t index,flags;uint64_t size;std::array<uint8_t,20> hash;};
struct Title {uint64_t id,ios;uint16_t version,gid,boot;std::vector<Content> content;};
Title tmd(Source& s,uint64_t id){const auto b=s.read("title/"+hex(id>>32)+"/"+hex(uint32_t(id))+"/content/title.tmd");const auto sig=u32(b,0);const size_t o=sig==0x10000?0x240:sig==0x10001?0x140:sig==0x10002?0x80:0;need(o && b.size()>=o+0xa4,"Invalid installed title metadata");Title t{u64(b,o+0x4c),u64(b,o+0x44),u16(b,o+0x9c),u16(b,o+0x58),u16(b,o+0xa0),{}};const auto count=u16(b,o+0x9e);need(t.id==id && count && count<=512 && b.size()==o+0xa4+count*36,"Invalid installed content table");std::set<unsigned> ids,indices;for(unsigned i=0;i<count;++i){const auto at=o+0xa4+i*36;Content c{u32(b,at),u16(b,at+4),u16(b,at+6),u64(b,at+8),{}};std::copy_n(b.data()+at+16,20,c.hash.data());need(ids.insert(c.id).second && indices.insert(c.index).second,"Duplicate installed content identity");t.content.push_back(c);}need(indices.contains(t.boot),"Boot content is missing from the title metadata");return t;}
std::string content_path(Source& s,const Title& t,const Content& c){if(c.flags&0x8000){const auto m=s.read("shared1/content.map");need(m.size()%28==0,"Invalid shared content map");std::string found;for(size_t at=0;at<m.size();at+=28)if(std::equal(c.hash.begin(),c.hash.end(),m.begin()+at+8)){need(found.empty(),"Duplicate shared content mapping");found=std::string((const char*)m.data()+at,8);need(std::all_of(found.begin(),found.end(),[](unsigned char x){return std::isxdigit(x);}),"Invalid shared content filename");}need(!found.empty(),"Required shared system content is missing",WII_NAND_SETUP_MISSING);return "shared1/"+found+".app";}return "title/"+hex(t.id>>32)+"/"+hex(uint32_t(t.id))+"/content/"+hex(c.id)+".app";}
void verify_title(Source& s,const Title& t){for(const auto& c:t.content){auto b=s.read(content_path(s,t,c));need(b.size()==c.size && digest(b)==c.hash,"Installed system content is damaged or uses the wrong NAND keys");}const auto ticket=s.read("ticket/"+hex(t.id>>32)+"/"+hex(uint32_t(t.id))+".tik");need(!ticket.empty() && ticket.size()%0x2a4==0,"Invalid installed system ticket");for(size_t at=0;at<ticket.size();at+=0x2a4)need(u32(ticket,at)==0x10001 && ticket[at+0x1bc]==0 && u64(ticket,at+0x1dc)==t.id,"System ticket does not match its title");}
void validate(Source& s,WiiNandSetupInfo& out){out.source_kind=s.kind();for(const auto& e:s.entries){if(!e.directory){++out.files;out.bytes+=e.size;}}const auto menu=tmd(s,0x100000002ull);out.menu_version=menu.version;out.ios_profile=uint32_t(menu.ios);out.gid=menu.gid;std::snprintf(out.menu_label,sizeof out.menu_label,menu.version==1?"Startup Menu (v1)":menu.version==513?"Wii Menu 4.3U":"Wii Menu v%u",menu.version);
 const auto& boot=*std::find_if(menu.content.begin(),menu.content.end(),[&](const Content& c){return c.index==menu.boot;});const auto boot_path=content_path(s,menu,boot);std::snprintf(out.boot_relative,sizeof out.boot_relative,"%s",boot_path.c_str());
 auto setting=s.read("title/00000001/00000002/data/setting.txt");need(setting.size()==256,"Console region settings are malformed");uint32_t key=0x73b5dbfa;for(auto& c:setting){c^=uint8_t(key);key=std::rotl(key,1);}std::string text((char*)setting.data(),setting.size());std::string area;const auto at=text.find("AREA=");if(at!=std::string::npos)area=text.substr(at+5,text.find_first_of("\r\n\0",at+5)-(at+5));need(area=="USA" || area=="JPN" || area=="EUR" || area=="KOR","Console region settings are unsupported",WII_NAND_SETUP_UNSUPPORTED);std::snprintf(out.region_label,sizeof out.region_label,"%s",area=="USA"?"USA":area=="JPN"?"Japan":area=="EUR"?"Europe":"Korea");
 if(menu.version!=513)
  throw Failure(WII_NAND_SETUP_UNSUPPORTED,"The selected NAND contains Wii Menu v"+std::to_string(menu.version)+
   ". This release requires the original Wii Menu 4.3U (v513).");
 if(menu.ios!=0x100000050ull)
  throw Failure(WII_NAND_SETUP_UNSUPPORTED,"The selected Wii Menu v513 declares IOS title "+hex(menu.ios,16)+
   ". This release requires Wii IOS80 (0000000100000050).");
 const auto hash=hash_text(boot.hash);
 bool supported=hash=="26116613f624061ba99c8d1a299aaa6efa85670d";
#ifdef WIILLR_NAND_SETUP_TESTING
 // A ROM-free, fixed synthetic DOL identity exists only in component tests.
 supported=supported || hash=="97225ce7a70ec083ff7b0a4fa2c3df55b7620aad";
#endif
 need(supported,"The selected Wii Menu v513 declares a different boot content SHA-1 in title.tmd than this build requires. Choose a backup containing the original Wii Menu 4.3U boot content.",WII_NAND_SETUP_UNSUPPORTED);
 need(area=="USA","The Menu and console region do not match",WII_NAND_SETUP_UNSUPPORTED);verify_title(s,menu);
 // The release supplies native IOS80. Original IOS firmware is not an HLE
 // launch dependency; if any installation is supplied, reject partial/damaged
 // data instead of claiming that it was validated for original-IOS startup.
 const bool original_ios=std::any_of(s.entries.begin(),s.entries.end(),[](const Entry& e){return !e.directory && (e.name.starts_with("title/00000001/00000050/content/") || e.name=="ticket/00000001/00000050.tik");});
 if(original_ios){const auto ios=tmd(s,menu.ios);need(ios.version==6944,"The supplied original IOS80 revision is unsupported",WII_NAND_SETUP_UNSUPPORTED);verify_title(s,ios);}

 auto uids=s.read("sys/uid.sys");need(!uids.empty() && uids.size()%12==0,"Invalid console title identities");std::set<uint64_t> titles;std::set<uint32_t> users;unsigned matches=0;for(size_t i=0;i<uids.size();i+=12){auto id=u64(uids,i);auto uid=u32(uids,i+8);need(titles.insert(id).second && users.insert(uid).second,"Duplicate console title identity");if(id==menu.id){out.uid=uid;++matches;}}need(matches==1 && out.uid,"The Wii Menu identity is missing",WII_NAND_SETUP_MISSING);
 const auto sc=s.read("shared2/sys/SYSCONF");size_t aspect=0,bias=0;
 need(wii_menu_profile_fields(sc.data(),sc.size(),&aspect,&bias) && sc[aspect]<=1,"Console settings are missing or malformed");

need(!s.read("sys/cert.sys").empty(),"Console certificates are missing",WII_NAND_SETUP_MISSING);s.unchanged();out.ready=true;out.status=WII_NAND_SETUP_READY;std::snprintf(out.details,sizeof out.details,original_ios?"Wii Menu and console data are ready for native IOS80. Supplied original IOS80 content also passed integrity checks; original-IOS startup is not qualified.":"Wii Menu and console data are ready for the included native IOS80 provider. Original IOS80 firmware is not required or installed; original-IOS startup is not qualified.");}
struct Staging {
 fs::path root;std::vector<fs::path> created;bool published=false;
 ~Staging(){if(!published)for(auto i=created.rbegin();i!=created.rend();++i){std::error_code error;fs::remove(*i,error);}}
 void directory(const fs::path& p){if(fs::exists(p))return;directory(p.parent_path());need(fs::create_directory(p),"Cannot create managed system storage",WII_NAND_SETUP_IO_ERROR);created.push_back(p);}
 void write(const fs::path& p,View b){directory(p.parent_path());need(!fs::exists(p),"Import destination changed",WII_NAND_SETUP_IO_ERROR);created.push_back(p);std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());f.close();need(!f.fail(),"Cannot write the managed system copy",WII_NAND_SETUP_IO_ERROR);}
 void fill(const fs::path& p,View b){need(std::find(created.begin(),created.end(),p)!=created.end() && !linked(p) && fs::is_regular_file(p) && fs::file_size(p)==0,"Import staging file changed",WII_NAND_SETUP_IO_ERROR);std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());f.close();need(!f.fail(),"Cannot write the managed system copy",WII_NAND_SETUP_IO_ERROR);}
};
void restore_metadata(WiiFs* mounted,const Entry& e){
 std::array<uint8_t,74> request{};for(unsigned i=0;i<4;++i)request[i]=uint8_t(e.uid>>(24-8*i));request[4]=uint8_t(e.gid>>8);request[5]=uint8_t(e.gid);
 const auto name="/"+e.name;need(name.size()<64,"NAND path exceeds the IOS limit");std::copy(name.begin(),name.end(),request.begin()+6);
 request[70]=(e.mode>>6)&3;request[71]=(e.mode>>4)&3;request[72]=(e.mode>>2)&3;request[73]=e.attribute;int32_t result=-1;
 need(wii_fs_ioctl(mounted,-1,5,request.data(),request.size(),nullptr,0,&result) && result==0,"Cannot preserve imported NAND permissions",WII_NAND_SETUP_IO_ERROR);
}
WiiNandSetupStatus perform(const char* source,const char* keys,const char* destination,WiiNandSetupInfo* info,char* error,size_t capacity){WiiNandSetupInfo out{};try{out.source_kind=fs::is_directory(path(source))?WII_NAND_SOURCE_EXTRACTED:WII_NAND_SOURCE_BOOTMII;auto s=open(source,keys);validate(*s,out);if(destination){const auto dest=fs::absolute(path(destination)).lexically_normal();chain(dest);need(!fs::exists(dest),"This managed NAND already exists; choose a new system profile",WII_NAND_SETUP_DEST_EXISTS);need(!within(fs::weakly_canonical(dest),s->origin) && !within(s->origin,fs::weakly_canonical(dest)),"Source and managed system storage must be separate");Staging stage;stage.root=dest.parent_path()/(".nand-import-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));stage.directory(stage.root);
  const std::string marker="wiillr-copied-nand-v1\n";
  const auto write_marker=[&](){stage.write(stage.root/".wiillr-nand-profile",View((const uint8_t*)marker.data(),marker.size()));};
  const bool raw=s->kind()==WII_NAND_SOURCE_BOOTMII;
  std::unique_ptr<WiiFs,decltype(&wii_fs_unmount)> mounted(nullptr,wii_fs_unmount);
  if(raw){
   write_marker();stage.created.push_back(stage.root/"tmp");stage.created.push_back(stage.root/"fst.bin");
   const auto text=utf8(stage.root);mounted.reset(wii_fs_mount(text.c_str(),0,0));
   need(bool(mounted),"Cannot initialize imported NAND metadata",WII_NAND_SETUP_IO_ERROR);
  }
  for(const auto& e:s->entries){
   const auto target=stage.root/fs::u8path(e.name);
   if(e.directory)stage.directory(target);
   else if(e.name!=".wiillr-nand-profile")stage.write(target,raw?Bytes{}:s->read(e.name));
   if(raw){
    // The native filesystem forbids changing a nonempty file's UID. Restore
    // original ownership on our new empty file, then copy its immutable bytes.
    restore_metadata(mounted.get(),e);if(!e.directory)stage.fill(target,s->read(e.name));
   }
  }
  if(!raw)write_marker();mounted.reset();s->unchanged();
  Folder copy(stage.root);WiiNandSetupInfo checked{};validate(copy,checked);
  need(!fs::exists(dest),"The managed NAND destination was created by another import",WII_NAND_SETUP_DEST_EXISTS);std::error_code publish_error;fs::rename(stage.root,dest,publish_error);need(!publish_error,"Cannot publish the managed NAND copy",WII_NAND_SETUP_IO_ERROR);
  stage.published=true;
 }if(info)*info=out;if(error && capacity)*error=0;return WII_NAND_SETUP_READY;
 }catch(const Failure& e){out.ready=false;out.status=e.status;std::snprintf(out.details,sizeof out.details,"%s",e.what());if(info)*info=out;if(error && capacity)std::snprintf(error,capacity,"%s",e.what());return e.status;}catch(const std::exception&){out.ready=false;out.status=WII_NAND_SETUP_IO_ERROR;const char* m="Cannot read or publish system storage. Check permissions and free space.";std::snprintf(out.details,sizeof out.details,"%s",m);if(info)*info=out;if(error && capacity)std::snprintf(error,capacity,"%s",m);return out.status;}}
}
extern "C" WiiNandSetupStatus wii_nand_setup_inspect(const char* s,const char* k,WiiNandSetupInfo* i,char* e,size_t n){return perform(s,k,nullptr,i,e,n);}
extern "C" WiiNandSetupStatus wii_nand_setup_import(const char* s,const char* k,const char* d,WiiNandSetupInfo* i,char* e,size_t n){if(!d || !*d){if(e && n)std::snprintf(e,n,"Choose a new managed system destination");if(i){*i={};i->status=WII_NAND_SETUP_INVALID;}return WII_NAND_SETUP_INVALID;}return perform(s,k,d,i,e,n);}
