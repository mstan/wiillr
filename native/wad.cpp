// SPDX-License-Identifier: GPL-3.0-or-later
#include <wiillr/wad.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>
#include <mbedtls/platform_util.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace fs=std::filesystem;
namespace {
using Bytes=std::vector<uint8_t>;
using View=std::span<const uint8_t>;
constexpr size_t max_wad=256u*1024*1024,max_metadata=4u*1024*1024;
void need(bool yes,const char* error){if(!yes)throw std::runtime_error(error);}
uint16_t u16(View v,size_t n){need(n<=v.size() && v.size()-n>=2,"Truncated WAD metadata");return uint16_t(v[n])<<8|v[n+1];}
uint32_t u32(View v,size_t n){return uint32_t(u16(v,n))<<16|u16(v,n+2);}
uint64_t u64(View v,size_t n){return uint64_t(u32(v,n))<<32|u32(v,n+4);}
size_t aligned(size_t n,size_t alignment){need(n<=max_wad,"WAD section is too large");return (n+alignment-1)&~(alignment-1);}
View sub(View v,size_t at,size_t size){need(at<=v.size() && size<=v.size()-at,"Truncated WAD section");return v.subspan(at,size);}
Bytes bytes(View v){return {v.begin(),v.end()};}
void append(Bytes& b,uint64_t value,unsigned count){for(unsigned n=count;n;n--)b.push_back(uint8_t(value>>((n-1)*8)));}
std::string hex(uint64_t value,unsigned count){char text[17];std::snprintf(text,sizeof text,"%0*llx",int(count),static_cast<unsigned long long>(value));return text;}
fs::path utf8_path(const char* text){std::u8string value;for(;*text;text++)value.push_back(static_cast<char8_t>(static_cast<unsigned char>(*text)));return fs::path(value);}
bool linked(const fs::path& p) {
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(p.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(p));
#endif
}
void chain(const fs::path& p){for(auto at=fs::absolute(p);!at.empty();at=at.parent_path()){need(!linked(at),"Linked WAD/profile paths are unsupported");if(at==at.root_path())break;}}
Bytes read(const fs::path& path,size_t limit) {
    chain(path);need(fs::is_regular_file(path),"A required WAD/profile file is missing");
    const auto size=fs::file_size(path);need(size<=limit,"WAD/profile file is too large");
    Bytes result(static_cast<size_t>(size));std::ifstream file(path,std::ios::binary);
    need(bool(file) && (result.empty() || bool(file.read(reinterpret_cast<char*>(result.data()),result.size()))),"Cannot read the WAD/profile file");
    need(file.peek()==std::char_traits<char>::eof() && !file.bad(),"WAD/profile file changed while reading");return result;
}
size_t signature_base(View v) {
    switch(u32(v,0)){case 0x10000:return 0x240;case 0x10001:return 0x140;case 0x10002:return 0x80;default:throw std::runtime_error("Unsupported WAD signature type");}
}
struct Secret {
    std::array<uint8_t,16> data{};
    ~Secret(){mbedtls_platform_zeroize(data.data(),data.size());}
};
Bytes decrypt(View input,const Secret& key,std::array<uint8_t,16> iv) {
    need(input.size()%16==0,"Unaligned encrypted WAD content");Bytes output(input.size());
    mbedtls_aes_context aes;mbedtls_aes_init(&aes);
    const int setup=mbedtls_aes_setkey_dec(&aes,key.data.data(),128);
    const int result=setup?setup:input.empty()?0:mbedtls_aes_crypt_cbc(&aes,MBEDTLS_AES_DECRYPT,input.size(),iv.data(),input.data(),output.data());
    mbedtls_aes_free(&aes);need(!result,"WAD decryption failed");return output;
}
std::array<uint8_t,20> digest(View input) {
    std::array<uint8_t,20> result{};need(!mbedtls_sha1_ret(input.data(),input.size(),result.data()),"WAD content hash failed");return result;
}
struct ProfileLock {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit ProfileLock(const fs::path& root) {
        handle=CreateFileW((root/".wiillr-wad-import.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        need(handle!=INVALID_HANDLE_VALUE,"Another DLC import is using this profile");
    }
    ~ProfileLock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
#else
    int handle=-1;
    explicit ProfileLock(const fs::path& root) {
        handle=::open((root/".wiillr-wad-import.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
        if(handle<0)throw std::runtime_error("Cannot lock the copied profile");
        if(flock(handle,LOCK_EX|LOCK_NB)){::close(handle);handle=-1;throw std::runtime_error("Another DLC import is using this profile");}
    }
    ~ProfileLock(){if(handle>=0)::close(handle);}
#endif
};
struct Stage {
    fs::path root;std::vector<fs::path> files;
    explicit Stage(const fs::path& profile) {
        for(unsigned n=0;n<16;n++) {
            const auto candidate=profile/(".wiillr-import-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(n));
            if(fs::create_directory(candidate)){root=candidate;return;}
        }
        throw std::runtime_error("Cannot stage the DLC import");
    }
    ~Stage(){for(const auto& p:files){std::error_code e;fs::remove(p,e);}std::error_code e;fs::remove(root,e);}
};
void publish(const fs::path& temporary,const fs::path& target,bool replace) {
#ifdef _WIN32
    need(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0)),"Cannot publish a DLC file; retry the import");
#else
    if(replace)fs::rename(temporary,target);
    else {fs::create_hard_link(temporary,target);fs::remove(temporary);}
#endif
}
struct Content {uint32_t id;uint16_t index,flags;std::array<uint8_t,20> sha;Bytes plain;};
struct Output {Bytes value,previous;bool existed{},mutable_metadata{};};
WiiWadBaseInstallResult install(const fs::path& wad_path,const fs::path& key_path,const fs::path& profile_path,uint64_t expected,bool base) {
    need(base?expected==UINT64_C(0x0001000157523945):expected>>32==0x00010005,
        base?"The base importer requires the supported USA Mega Man 9 title":"The importer requires the expected DLC title");
    chain(profile_path);const auto root=fs::canonical(profile_path);need(fs::is_directory(root),"A copied NAND profile is required");
    const auto marker=read(root/".wiillr-nand-profile",64);
    const std::string magic(marker.begin(),marker.end());need(magic=="wiillr-copied-nand-v1\n" || magic=="wiillr-copied-nand-v1\r\n","Install DLC only into a marked copied NAND profile");
    for(const auto& item:fs::recursive_directory_iterator(root))need(!linked(item.path()) && item.path().filename()!=".git","Linked or repository content is unsupported in a NAND profile");
    const auto raw=read(wad_path,max_wad);View data(raw);
    need(u32(data,0)==32 && (u32(data,4)>>16==0x4973 || u32(data,4)>>16==0x6962) && !u32(data,12),"Unsupported installable WAD header");
    std::array<View,5> section;size_t offset=64;
    const std::array<unsigned,5> sizes={8,16,20,24,28};
    for(unsigned n=0;n<5;n++){const auto amount=u32(data,sizes[n]);section[n]=sub(data,offset,amount);offset+=aligned(amount,64);}
    const auto ticket=section[1],tmd=section[2];
    need(ticket.size()<=max_metadata && tmd.size()<=max_metadata,"WAD metadata is too large");
    const auto tb=signature_base(ticket),mb=signature_base(tmd);
    sub(ticket,tb,0xb2);sub(tmd,mb,0xa4);
    need(tb==0x140 && ticket.size()==0x2a4 && ticket[tb+0x7c]==0,"Unsupported DLC ticket format; expected one RSA2048 version-0 ticket");
    const auto title=u64(tmd,mb+0x4c);need(title==expected && u64(ticket,tb+0x9c)==title,"DLC belongs to a different game or has inconsistent title identities");
    WiiWadBaseInstallResult result{};result.ios_title=u64(tmd,mb+0x44);
    result.group_id=u16(tmd,mb+0x58);result.title_version=u16(tmd,mb+0x9c);result.boot_index=u16(tmd,mb+0xa0);
    if(base)need(result.ios_title==UINT64_C(0x0000000100000025),"Supported Mega Man 9 base requires IOS37");
    const auto count=u16(tmd,mb+0x9e);need(count && count<=1024,"Unsupported WAD content count");sub(tmd,mb+0xa4,size_t(count)*36);
    Secret common,title_key;chain(key_path);need(fs::is_regular_file(key_path) && fs::file_size(key_path)==16,"DLC import requires the supplied 16-byte Wii common key");
    {std::ifstream file(key_path,std::ios::binary);need(bool(file.read(reinterpret_cast<char*>(common.data.data()),16)) && file.peek()==std::char_traits<char>::eof(),"Cannot read the supplied Wii common key");}
    std::array<uint8_t,16> iv{};for(unsigned n=0;n<8;n++)iv[n]=uint8_t(title>>(56-n*8));
    auto unwrapped=decrypt(sub(ticket,tb+0x7f,16),common,iv);std::copy(unwrapped.begin(),unwrapped.end(),title_key.data.begin());mbedtls_platform_zeroize(unwrapped.data(),unwrapped.size());
    std::vector<Content> contents;std::set<uint32_t> ids;std::set<uint16_t> indexes;offset=0;
    for(unsigned n=0;n<count;n++) {
        const auto at=mb+0xa4+n*36;Content item{u32(tmd,at),u16(tmd,at+4),u16(tmd,at+6),{}, {}};
        need(ids.insert(item.id).second && indexes.insert(item.index).second,"Duplicate WAD content ID or index");
        const auto length=u64(tmd,at+8);need(length<=max_wad,"WAD content is too large");std::copy_n(tmd.data()+at+16,20,item.sha.begin());
        iv.fill(0);iv[0]=uint8_t(item.index>>8);iv[1]=uint8_t(item.index);
        const auto amount=aligned(size_t(length),16);item.plain=decrypt(sub(section[3],offset,amount),title_key,iv);item.plain.resize(size_t(length));
        need(digest(item.plain)==item.sha,"DLC content hash verification failed; no files were installed");
        offset+=aligned(amount,64);contents.push_back(std::move(item));
    }
    if(base) {
        const auto boot=std::find_if(contents.begin(),contents.end(),[&](const Content& c){return c.index==result.boot_index;});
        need(boot!=contents.end() && !boot->plain.empty(),"Base WAD boot index is absent or empty");
        // This is an identity allowlist, not signature authentication. Do not
        // publish internally consistent content from another executable/revision.
        constexpr std::array<uint8_t,20> supported_boot{
            0x58,0x85,0x8c,0x85,0x66,0xa5,0x5e,0xde,0xec,0xc6,
            0xef,0x91,0x82,0xd7,0x6b,0x61,0xc3,0x89,0x71,0x42};
        need(boot->sha==supported_boot && digest(boot->plain)==supported_boot,"Unsupported Mega Man 9 base boot content");
        result.boot_content_id=boot->id;
    }
    // No profile writes occur before every content and title identity passes.
    ProfileLock lock(root);
    const auto prefix="title/"+hex(title>>32,8)+"/"+hex(uint32_t(title),8);
    std::map<std::string,Output> outputs;
    const auto add=[&](const std::string& path,Bytes value,bool mutable_metadata=false) {
        const auto [it,inserted]=outputs.emplace(path,Output{std::move(value),{},false,mutable_metadata});
        need(inserted,"Duplicate WAD output path");
    };
    add(prefix+"/content/title.tmd",bytes(tmd));add("ticket/"+hex(title>>32,8)+"/"+hex(uint32_t(title),8)+".tik",bytes(ticket));
    const auto map_path=root/"shared1/content.map";Bytes mapping=fs::exists(map_path)?read(map_path,max_metadata):Bytes{};
    need(mapping.size()%28==0,"Truncated shared content map");
    std::map<std::array<uint8_t,20>,std::string> shared;std::set<std::string> occupied;
    for(size_t at=0;at<mapping.size();at+=28) {
        std::string name(mapping.begin()+at,mapping.begin()+at+8);std::array<uint8_t,20> hash;std::copy_n(mapping.data()+at+8,20,hash.begin());
        need(std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='0'&&c<='9') || (c>='a'&&c<='f') || (c>='A'&&c<='F');}),"Invalid shared content name");
        need(shared.emplace(hash,name).second && occupied.insert(name).second,"Duplicate shared content mapping");
    }
    bool mapping_changed=false;
    for(auto& item:contents) {
        std::string content_path;
        if(item.flags&0x8000) {
            auto found=shared.find(item.sha);
            if(found==shared.end()) {
                uint64_t number=0;while(number<=UINT32_MAX && occupied.contains(hex(number,8)))number++;
                need(number<=UINT32_MAX,"Shared content names are exhausted");const auto name=hex(number,8);occupied.insert(name);
                mapping.insert(mapping.end(),name.begin(),name.end());mapping.insert(mapping.end(),item.sha.begin(),item.sha.end());
                found=shared.emplace(item.sha,name).first;mapping_changed=true;
            }
            const auto name="shared1/"+found->second+".app";
            content_path=name;
            if(auto previous=outputs.find(name);previous!=outputs.end())need(previous->second.value==item.plain,"Conflicting shared WAD content");
            else add(name,std::move(item.plain));
        } else {content_path=prefix+"/content/"+hex(item.id,8)+".app";add(content_path,std::move(item.plain));}
        if(base && item.index==result.boot_index) {
            need(content_path.size()<sizeof result.boot_path,"Base boot content path is too long");
            std::snprintf(result.boot_path,sizeof result.boot_path,"%s",content_path.c_str());
        }
    }
    if(mapping_changed)add("shared1/content.map",std::move(mapping),true);
    Bytes uids=read(root/"sys/uid.sys",max_metadata);need(uids.size()%12==0,"Truncated NAND UID table");
    std::set<uint64_t> titles;std::set<uint32_t> users;uint32_t uid=0,next=0xfff;bool have_uid=false;
    for(size_t at=0;at<uids.size();at+=12) {
        const auto id=u64(uids,at);const auto user=u32(uids,at+8);
        need(titles.insert(id).second && users.insert(user).second,"Duplicate NAND UID identity");next=std::max(next,user);
        if(id==title){uid=user;have_uid=true;}
    }
    if(!have_uid){need(next!=UINT32_MAX,"NAND UID space is exhausted");uid=next+1;append(uids,title,8);append(uids,uid,4);add("sys/uid.sys",std::move(uids),true);}
    for(auto& [name,output]:outputs) {
        const auto target=root/fs::path(name);chain(target);output.existed=fs::exists(target);
        if(output.existed){output.previous=read(target,max_wad);need(output.mutable_metadata || output.previous==output.value,"Conflicting installed DLC content; existing files were retained");}
    }
    Stage stage(root);unsigned serial=0;
    for(const auto& [name,output]:outputs) {
        const auto target=root/fs::path(name);chain(target);
        if(fs::exists(target)) {
            const auto now=read(target,max_wad);
            if(now==output.value)continue;
            need(output.mutable_metadata && output.existed && now==output.previous,"Profile changed during DLC import; retry");
        } else need(!output.existed,"Profile changed during DLC import; retry");
        fs::create_directories(target.parent_path());chain(target.parent_path());
        const auto temporary=stage.root/std::to_string(serial++);stage.files.push_back(temporary);
        {std::ofstream file(temporary,std::ios::binary);need(bool(file) && (output.value.empty() || bool(file.write(reinterpret_cast<const char*>(output.value.data()),output.value.size()))),"Cannot stage DLC content");file.close();need(!file.fail(),"Cannot finish staging DLC content");}
        publish(temporary,target,output.existed);need(read(target,max_wad)==output.value,"Published DLC content did not verify");
    }
    chain(root/prefix/"data");fs::create_directories(root/prefix/"data");
    result.installed={title,uid,count,ticket[tb+0xb1]};return result;
}
}
extern "C" bool wii_wad_install(const char* wad_path,const char* common_key_path,const char* copied_nand,
    uint64_t expected_title,WiiWadInstallResult* result,char* error,size_t capacity) {
    if(result)*result={};if(error && capacity)*error=0;
    try {
        need(wad_path && *wad_path && common_key_path && *common_key_path && copied_nand && *copied_nand,"Missing WAD, common-key or copied-profile path");
        const auto installed=install(utf8_path(wad_path),utf8_path(common_key_path),utf8_path(copied_nand),expected_title,false);
        if(result)*result=installed.installed;return true;
    }catch(const std::exception& e){if(error && capacity)std::snprintf(error,capacity,"%s",e.what());return false;}
}
extern "C" bool wii_wad_install_base(const char* wad_path,const char* common_key_path,const char* copied_nand,
    uint64_t expected_title,WiiWadBaseInstallResult* result,char* error,size_t capacity) {
    if(result)*result={};if(error && capacity)*error=0;
    try {
        need(wad_path && *wad_path && common_key_path && *common_key_path && copied_nand && *copied_nand,"Missing WAD, common-key or copied-profile path");
        const auto installed=install(utf8_path(wad_path),utf8_path(common_key_path),utf8_path(copied_nand),expected_title,true);
        if(result)*result=installed;return true;
    }catch(const std::exception& e){if(error && capacity)std::snprintf(error,capacity,"%s",e.what());return false;}
}
