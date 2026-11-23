/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/nand_setup.h"
#include "wiillr/nand_controller.hpp"
#include "wiillr/aes128.hpp"
#include "wiillr/fs.h"
#include "nand_setup_fixture.h"
#include <chrono>
#include <iostream>
#include <memory>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#endif
namespace fs=std::filesystem;using namespace nand_fixture;
static void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static Bytes read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static std::string text(const fs::path& p){const auto b=p.generic_u8string();return {b.begin(),b.end()};}
struct Owned {
    fs::path root,parent;
    Owned(const char* p):parent(fs::canonical(p)){root=parent/("nand-setup-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));require(fs::create_directory(root),"owned fixture tree");}
    ~Owned(){try{if(fs::canonical(root).parent_path()!=parent)return;for(const auto& e:fs::recursive_directory_iterator(root))if(e.is_symlink() || e.path().filename()==".git")return;fs::remove_all(root);}catch(...){}}
};
static void raw(const fs::path& path){
    {std::ofstream file(path,std::ios::binary);}
#ifdef _WIN32
    HANDLE handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);require(handle!=INVALID_HANDLE_VALUE,"sparse fixture handle");
    DWORD used{};const bool sparse=DeviceIoControl(handle,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&used,nullptr)!=0;CloseHandle(handle);require(sparse,"bounded sparse raw fixture");
#endif
    std::fstream output(path,std::ios::binary|std::ios::in|std::ios::out);output.seekp(0x210003ff);output.put(0);
    Bytes super(0x40000,255);std::copy_n("SFFS",4,super.begin());put(super,4,7);
    struct Node {std::string name;bool dir;uint16_t first=0xffff,next=0xffff;Bytes data;};
    std::vector<Node> nodes={{"/",true}};std::map<std::string,uint16_t> ids={{"",0}};std::map<uint16_t,std::vector<uint16_t>> children;
    for(const auto& [name,data]:files()){
        fs::path parent;uint16_t owner=0;
        const fs::path entry(name);for(const auto& component:entry.parent_path()){
            parent/=component;const auto key=parent.generic_string();if(!ids.contains(key)){const uint16_t id=uint16_t(nodes.size());ids[key]=id;nodes.push_back({component.string(),true});children[owner].push_back(id);}owner=ids.at(key);
        }
        const uint16_t id=uint16_t(nodes.size());nodes.push_back({entry.filename().string(),false,0xffff,0xffff,data});children[owner].push_back(id);
    }
    for(const auto& [id,list]:children){nodes[id].first=list.front();for(size_t i=1;i<list.size();++i)nodes[list[i-1]].next=list[i];}
    const std::array<uint8_t,16> key={1,3,5,7,9,11,13,15,2,4,6,8,10,12,14,16};const auto aes=wiillr::aes128_expand(key);uint16_t cluster=1;
    const auto logical=[&](uint32_t offset,const Bytes& data){
        for(size_t at=0;at<data.size();at+=0x800){Bytes page(0x840,255);std::copy_n(data.begin()+at,0x800,page.begin());for(unsigned i=0;i<4;++i)put(page,0x830+i*4,wiillr::NandController::ecc(page.data()+i*512));output.seekp(uint64_t(offset+at)/0x800*0x840);output.write((const char*)page.data(),page.size());}
    };
    for(unsigned i=0;i<nodes.size();++i){auto& n=nodes[i];if(!n.dir && !n.data.empty()){
        n.first=cluster;for(size_t at=0;at<n.data.size();at+=0x4000){Bytes encrypted(0x4000);const auto count=std::min(n.data.size()-at,encrypted.size());std::copy_n(n.data.begin()+at,count,encrypted.begin());std::array<uint8_t,16> iv{};
            for(size_t b=0;b<encrypted.size();b+=16){std::array<uint8_t,16> block{};for(unsigned k=0;k<16;++k)block[k]=encrypted[b+k]^iv[k];wiillr::aes128_encrypt(aes,block);std::copy(block.begin(),block.end(),encrypted.begin()+b);iv=block;}
            logical(uint32_t(cluster)*0x4000,encrypted);put(super,12+cluster*2,at+0x4000<n.data.size()?cluster+1:0xfffb,2);++cluster;
        }
      }
      const size_t at=0x1000c+32*i;std::fill_n(super.begin()+at,32,0);require(n.name.size()<=12,"raw filename width");std::copy(n.name.begin(),n.name.end(),super.begin()+at);
      super[at+12]=uint8_t(0xfc|(n.dir?2:1));super[at+13]=2;put(super,at+14,n.first,2);put(super,at+16,n.next,2);put(super,at+18,n.data.size());put(super,at+22,4096);put(super,at+26,1,2);
    }
    logical(0x1fc00000,super);Bytes footer(0x400);std::copy_n("BackupMii v1",12,footer.begin());std::copy(key.begin(),key.end(),footer.begin()+0x158);output.seekp(0x21000000);output.write((const char*)footer.data(),footer.size());require(bool(output),"raw fixture publication");
}
int main(int argc,char** argv){try{
    require(argc==2,"workspace argument");Owned own(argv[1]);const auto source=own.root/fs::u8path("console Ω");folder(source);const auto original=files();
    WiiNandSetupInfo info{};char error[1024]{};
    const auto inspect=[&](const fs::path& p,const char* keys=nullptr){return wii_nand_setup_inspect(text(p).c_str(),keys,&info,error,sizeof error);};
    const auto import=[&](const fs::path& p,const fs::path& dest){return wii_nand_setup_import(text(p).c_str(),nullptr,text(dest).c_str(),&info,error,sizeof error);};
    require(inspect(own.root)==WII_NAND_SETUP_MISSING && std::string(error).find("NAND root")!=std::string::npos,"containing folder refuses before broad inventory");
    require(inspect(source)==WII_NAND_SETUP_READY,error);require(info.ready && info.menu_version==513 && info.ios_profile==80 && info.uid==4096 && std::string(info.region_label)=="USA","identity and region");
    const auto copy=own.root/"managed";require(import(source,copy)==WII_NAND_SETUP_READY,error);
    for(const auto& [name,data]:original)require(read(source/name)==data && read(copy/name)==data,"source and managed native data exact");
    write(copy/"title/00000001/00000002/data/native-save",bytes("new player progress"));require(import(source,copy)==WII_NAND_SETUP_DEST_EXISTS,"no replacement or merge");require(read(copy/"title/00000001/00000002/data/native-save")==bytes("new player progress"),"preserve existing progress");
    require(import(source,source/"nested")==WII_NAND_SETUP_INVALID && !fs::exists(source/"nested"),"reject source-contained import");
    auto bad=original.at("title/00000001/00000002/content/00000000.app");bad.back()^=1;write(source/"title/00000001/00000002/content/00000000.app",bad);
    require(import(source,own.root/"corrupt")==WII_NAND_SETUP_INVALID && !fs::exists(own.root/"corrupt"),"content mismatch never publishes");
    require(std::string(error).find("Installed system content is damaged")!=std::string::npos,"actual content integrity failure is distinct");
    write(source/"title/00000001/00000002/content/00000000.app",original.at("title/00000001/00000002/content/00000000.app"));
    const auto* menu_tmd="title/00000001/00000002/content/title.tmd";
    auto metadata=original.at(menu_tmd);
    put(metadata,0x1dc,1,2);write(source/menu_tmd,metadata);
    require(inspect(source)==WII_NAND_SETUP_UNSUPPORTED && !info.ready && info.menu_version==1,"unsupported Startup metadata cannot become ready");
    require(std::string(error).find("contains Wii Menu v1.")!=std::string::npos,"unsupported Menu version reports observed revision");
    put(metadata,0x1dc,513,2);put(metadata,0x184,0x700000050ull,8);write(source/menu_tmd,metadata);
    require(inspect(source)==WII_NAND_SETUP_UNSUPPORTED && !info.ready && info.menu_version==513,"same-version NAND with a different IOS title cannot become ready");
    require(std::string(error).find("declares IOS title 0000000700000050")!=std::string::npos,"unsupported IOS reports the full title identity");
    write(source/menu_tmd,original.at(menu_tmd));
    fs::remove(source/"ticket/00000001/00000050.tik");require(inspect(source)==WII_NAND_SETUP_MISSING,"missing IOS ticket");write(source/"ticket/00000001/00000050.tik",original.at("ticket/00000001/00000050.tik"));
    fs::remove(source/"ticket/00000001/00000050.tik");fs::remove(source/"title/00000001/00000050/content/title.tmd");fs::remove(source/"title/00000001/00000050/content/00000000.app");
    require(inspect(source)==WII_NAND_SETUP_READY && std::string(info.details).find("native IOS80")!=std::string::npos,"native provider needs no original IOS firmware");
    for(const auto& [name,data]:original)if(name.find("00000050")!=std::string::npos)write(source/name,data);
    Bytes truncated(6);write(source/"sys/uid.sys",truncated);require(inspect(source)==WII_NAND_SETUP_INVALID,"truncated UID table");write(source/"sys/uid.sys",original.at("sys/uid.sys"));
    auto bad_sc=original.at("shared2/sys/SYSCONF");bad_sc[22]=0x65;write(source/"shared2/sys/SYSCONF",bad_sc);
    require(inspect(source)==WII_NAND_SETUP_INVALID,"malformed required setting type refuses without replacement");write(source/"shared2/sys/SYSCONF",original.at("shared2/sys/SYSCONF"));
    const auto wrapper=own.root/"wrapper";
    folder(wrapper/"nand");
    require(inspect(wrapper)==WII_NAND_SETUP_READY && info.source_kind==WII_NAND_SOURCE_EXTRACTED,"known immediate NAND wrapper detected");
    folder(wrapper/"Wii");
    require(inspect(wrapper)==WII_NAND_SETUP_INVALID && !info.ready,"ambiguous wrapper refuses without choosing a system");
    const auto wrong_size=own.root/"wrong-size.bin";write(wrong_size,bytes("wrong image"));
    require(inspect(wrong_size)==WII_NAND_SETUP_UNSUPPORTED && !info.ready,"unsupported raw geometry cannot become ready");
    const std::string size_error=error;
    require(size_error.find("11 bytes")!=std::string::npos && size_error.find("553648128")!=std::string::npos &&
            size_error.find("553649152")!=std::string::npos && size_error.find("spare/ECC")!=std::string::npos,
            "image diagnostic reports observed bytes and both supported layouts");
    const auto raw_path=own.root/"nand.bin";raw(raw_path);require(inspect(raw_path)==WII_NAND_SETUP_READY,error);require(info.source_kind==WII_NAND_SOURCE_BOOTMII,"BootMii source identity");
    write(own.root/"keys.bin",Bytes(128,1));require(inspect(raw_path)==WII_NAND_SETUP_READY,"valid embedded backup keys take priority over an unselected sibling");fs::remove(own.root/"keys.bin");
    const auto raw_copy=own.root/"decrypted";require(import(raw_path,raw_copy)==WII_NAND_SETUP_READY,error);for(const auto& [name,data]:original)require(read(raw_copy/name)==data,"raw AES/filesystem decode exact");
    std::unique_ptr<WiiFs,decltype(&wii_fs_unmount)> mounted(wii_fs_mount(text(raw_copy).c_str(),0,0),wii_fs_unmount);require(bool(mounted),"imported native mount");std::array<uint8_t,64> in{};std::copy_n("/sys/uid.sys",12,in.begin());std::array<uint8_t,74> out{};int32_t result=-1;
    require(wii_fs_ioctl(mounted.get(),-1,6,in.data(),in.size(),out.data(),out.size(),&result) && result==0 && out[2]==0x10 && out[3]==0 && out[5]==1 && out[70]==3 && out[71]==3 && out[72]==3 && out[73]==2,"raw owner/permissions/attributes preserved");mounted.reset();
    const auto keys=own.root/"otp.bin";Bytes wrong(128,1);write(keys,wrong);const auto keytext=text(keys);require(inspect(raw_path,keytext.c_str())==WII_NAND_SETUP_INVALID,"mismatching explicit keys reject");
    {std::fstream f(raw_path,std::ios::binary|std::ios::in|std::ios::out);f.seekp(0x840*8);f.put('X');}
    require(inspect(raw_path)==WII_NAND_SETUP_INVALID,"uncorrectable used-page ECC rejects");
    raw(raw_path);fs::resize_file(raw_path,0x21000000);
    require(inspect(raw_path)==WII_NAND_SETUP_NEEDS_KEYS && info.source_kind==WII_NAND_SOURCE_BOOTMII,"raw NAND without footer requests explicit keys");
    Bytes otp(128);const uint8_t key[]={1,3,5,7,9,11,13,15,2,4,6,8,10,12,14,16};std::copy_n(key,16,otp.begin()+0x58);write(keys,otp);
    require(inspect(raw_path,keytext.c_str())==WII_NAND_SETUP_READY,error);
    const auto sibling_keys=own.root/"keys.bin";write(sibling_keys,otp);
    require(inspect(raw_path)==WII_NAND_SETUP_READY,"matching sibling key backup detected");
    require(inspect(own.root)==WII_NAND_SETUP_READY && info.source_kind==WII_NAND_SOURCE_BOOTMII,"backup folder detected without recursive discovery");
    write(sibling_keys,wrong);
    require(inspect(raw_path)==WII_NAND_SETUP_INVALID,"incorrect sibling key backup is never silently accepted");
    fs::remove(sibling_keys);

    // A valid-ECC but cyclic allocation must still be rejected before import.
    {std::fstream f(raw_path,std::ios::binary|std::ios::in|std::ios::out);const uint64_t page=uint64_t(0x1fc00000)/0x800*0x840;Bytes b(0x840);f.seekg(page);f.read((char*)b.data(),b.size());put(b,14,1,2);for(unsigned i=0;i<4;++i)put(b,0x830+i*4,wiillr::NandController::ecc(b.data()+i*512));f.seekp(page);f.write((const char*)b.data(),b.size());}
    require(inspect(raw_path,keytext.c_str())==WII_NAND_SETUP_INVALID,"valid-ECC cyclic allocation rejects");
    require(!fs::exists(own.root/"corrupt"),"all rejected imports remain unpublished");
    std::cout<<"Extracted and BootMii import, strict identity, content/ECC/keys, metadata, native-data preservation and atomic refusal passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
