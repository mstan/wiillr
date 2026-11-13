// SPDX-License-Identifier: MIT
#include <wiillr/wad.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
using Bytes=std::vector<uint8_t>;
constexpr uint64_t title=0x0001000577523945ull;
constexpr uint64_t base_title=0x0001000157523945ull;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static void put(Bytes& b,size_t at,uint64_t value,unsigned size){check(at+size<=b.size(),"fixture extent");for(unsigned n=size;n;n--){b[at+n-1]=uint8_t(value);value>>=8;}}
static void write(const fs::path& path,const Bytes& value){fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(value.data()),value.size());check(bool(f),"fixture write");}
static void text(const fs::path& path,const std::string& value){write(path,Bytes(value.begin(),value.end()));}
static Bytes read(const fs::path& path){std::ifstream f(path,std::ios::binary);check(bool(f),"fixture read");return {std::istreambuf_iterator<char>(f),{}};}
static std::string utf8(const fs::path& path){const auto value=path.u8string();return {value.begin(),value.end()};}
static std::map<std::string,Bytes> snapshot(const fs::path& root){std::map<std::string,Bytes> r;for(const auto& e:fs::recursive_directory_iterator(root))if(e.is_regular_file())r.emplace(e.path().lexically_relative(root).generic_string(),read(e.path()));return r;}
static std::array<uint8_t,16> key(){return {0x13,0x25,0x37,0x49,0x51,0x63,0x75,0x87,0x99,0xab,0xbd,0xcf,0xd1,0xe3,0xf5,0x07};}
static Bytes encrypt(Bytes value,const std::array<uint8_t,16>& key,std::array<uint8_t,16> iv){value.resize((value.size()+15)&~size_t(15));Bytes result(value.size());mbedtls_aes_context a;mbedtls_aes_init(&a);check(!mbedtls_aes_setkey_enc(&a,key.data(),128),"fixture AES key");if(!value.empty())check(!mbedtls_aes_crypt_cbc(&a,MBEDTLS_AES_ENCRYPT,value.size(),iv.data(),value.data(),result.data()),"fixture AES encryption");mbedtls_aes_free(&a);return result;}
struct Fixture {Bytes wad,ticket,tmd;std::array<Bytes,3> content;size_t content_offset{};};
static Fixture make_wad(uint64_t id=title,uint64_t ios=0x100000025ull,uint16_t boot=511){
    Fixture f;f.ticket.resize(0x2a4);f.tmd.resize(0x1e4+3*36);
    f.content={Bytes{1,2,3,4,5,6,7},Bytes{},Bytes(73,0x5a)};
    put(f.ticket,0,0x10001,4);put(f.ticket,0x1dc,id,8);f.ticket[0x1f1]=141;
    put(f.tmd,0,0x10001,4);put(f.tmd,0x184,ios,8);put(f.tmd,0x18c,id,8);
    put(f.tmd,0x198,12344,2);put(f.tmd,0x1dc,id==0x0001000157523945ull?1:0,2);put(f.tmd,0x1de,3,2);put(f.tmd,0x1e0,boot,2);
    const std::array<uint8_t,16> title_key{0x21,0x32,0x43,0x54,0x65,0x76,0x87,0x98,0xa9,0xba,0xcb,0xdc,0xed,0xfe,0x0f,0x10};
    std::array<uint8_t,16> iv{};for(unsigned n=0;n<8;n++)iv[n]=uint8_t(id>>(56-8*n));
    const auto wrapped=encrypt(Bytes(title_key.begin(),title_key.end()),key(),iv);std::copy(wrapped.begin(),wrapped.end(),f.ticket.begin()+0x1bf);
    Bytes encrypted;
    for(unsigned n=0;n<3;n++){
        const auto at=0x1e4+n*36;put(f.tmd,at,n,4);put(f.tmd,at+4,n,2);put(f.tmd,at+6,n==2?0x8001:0x4001,2);put(f.tmd,at+8,f.content[n].size(),8);
        std::array<uint8_t,20> hash;check(!mbedtls_sha1_ret(f.content[n].data(),f.content[n].size(),hash.data()),"fixture SHA1");std::copy(hash.begin(),hash.end(),f.tmd.begin()+at+16);
        iv.fill(0);iv[1]=uint8_t(n);const auto chunk=encrypt(f.content[n],title_key,iv);encrypted.insert(encrypted.end(),chunk.begin(),chunk.end());encrypted.resize((encrypted.size()+63)&~size_t(63));
    }
    f.wad.resize(64);put(f.wad,0,32,4);put(f.wad,4,0x49730000,4);put(f.wad,16,f.ticket.size(),4);put(f.wad,20,f.tmd.size(),4);put(f.wad,24,encrypted.size(),4);
    for(const auto* section:{&f.ticket,&f.tmd}){f.wad.insert(f.wad.end(),section->begin(),section->end());f.wad.resize((f.wad.size()+63)&~size_t(63));}
    f.content_offset=f.wad.size();f.wad.insert(f.wad.end(),encrypted.begin(),encrypted.end());return f;
}
static bool linked(const fs::path& path){
#ifdef _WIN32
    const auto a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}
struct Root {
    fs::path path,parent;
    explicit Root(const fs::path& workspace):parent(fs::canonical(workspace)){path=parent/("wad-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));check(fs::create_directory(path),"owned test directory");}
    ~Root(){try{if(fs::canonical(path).parent_path()!=parent)return;for(const auto& e:fs::recursive_directory_iterator(path))if(linked(e.path()) || e.path().filename()==".git")return;fs::remove_all(path);}catch(...) {}}
};
int main(int argc,char** argv){
 try{
    check(argc==2,"test workspace argument");Root root(argv[1]);const auto profile=root.path/fs::path(u8"copied NAND \u03bb"),wad=root.path/fs::path(u8"DLC pack \u03bb.wad"),common=root.path/fs::path(u8"common key \u03bb.bin");
    text(profile/".wiillr-nand-profile","wiillr-copied-nand-v1\n");Bytes users(12);put(users,0,0x0001000157523945ull,8);put(users,8,4107,4);write(profile/"sys/uid.sys",users);
    text(profile/"title/00010001/57523945/data/save.dat","native progress");text(profile/"ticket/00010001/57523945.tik","prior imported license");write(profile/"shared1/content.map",{});
    const auto k=key();write(common,Bytes(k.begin(),k.end()));const auto fixture=make_wad();write(wad,fixture.wad);
    char error[512];WiiWadInstallResult result{};
    auto install=[&](uint64_t expected=title){return wii_wad_install(utf8(wad).c_str(),utf8(common).c_str(),utf8(profile).c_str(),expected,&result,error,sizeof error);};
    auto pristine=snapshot(profile);
    // Synthetic base content cannot equal the production boot allowlist. It
    // must be rejected even when all ticket/TMD/content hashes are consistent.
    WiiWadBaseInstallResult base_result{};
    auto install_base=[&](uint64_t expected=base_title){return wii_wad_install_base(utf8(wad).c_str(),utf8(common).c_str(),utf8(profile).c_str(),expected,&base_result,error,sizeof error);};
    const auto base=make_wad(base_title,0x100000025ull,0);write(wad,base.wad);
    check(!install_base() && std::string(error).find("Unsupported Mega Man 9 base boot")!=std::string::npos,"internally valid noncanonical base boot rejected");
    check(snapshot(profile)==pristine && !base_result.installed.title && !base_result.ios_title && !*base_result.boot_path,"base attestation fails before any profile publication");
    check(!install(base_title) && snapshot(profile)==pristine,"DLC-only API cannot silently import a base title");
    check(!install_base(title) && snapshot(profile)==pristine,"base API rejects an expected DLC title");
    check(!install_base(0x00010001574d3845ull) && snapshot(profile)==pristine,"base API rejects another expected game");
    const auto wrong_ios=make_wad(base_title,0x100000024ull,0);write(wad,wrong_ios.wad);
    check(!install_base() && std::string(error).find("IOS37")!=std::string::npos && snapshot(profile)==pristine,"base IOS mismatch rejected before mutation");
    for(unsigned boot:{511u,1u}) {
        const auto invalid_boot=make_wad(base_title,0x100000025ull,uint16_t(boot));write(wad,invalid_boot.wad);
        check(!install_base() && std::string(error).find("absent or empty")!=std::string::npos && snapshot(profile)==pristine,"missing or empty base boot cannot be installed");
    }
    auto bad_base=base.wad;bad_base[base.content_offset]^=1;write(wad,bad_base);
    check(!install_base() && std::string(error).find("hash verification")!=std::string::npos && snapshot(profile)==pristine,"corrupt base contents fail before canonical policy/publication");
    auto inconsistent_base=base.wad;inconsistent_base[64+0x1e3]^=1;write(wad,inconsistent_base);
    check(!install_base() && snapshot(profile)==pristine,"inconsistent base ticket/TMD identities cannot publish");
    write(wad,fixture.wad);
    for(const size_t size:{size_t(0),size_t(31),size_t(63),size_t(64+0x2a3),fixture.content_offset-1,fixture.wad.size()-1}){
        write(wad,Bytes(fixture.wad.begin(),fixture.wad.begin()+size));check(!install(),"truncated WAD rejected");check(snapshot(profile)==pristine && !result.title,"truncation cannot write profile");
    }
    auto corrupt=fixture.wad;corrupt[fixture.content_offset]^=1;write(wad,corrupt);check(!install(),"corrupt encrypted content rejected");check(snapshot(profile)==pristine,"all content hashes checked before mutation");
    auto ticket_version=fixture.wad;ticket_version[64+0x1bc]=1;write(wad,ticket_version);check(!install(),"unsupported ticket version rejected before installation");check(snapshot(profile)==pristine,"unsupported ticket cannot change profile");
    write(wad,fixture.wad);check(!install(0x00010005774d3845ull),"different game DLC rejected");check(snapshot(profile)==pristine,"mismatched title cannot write profile");
    write(common,Bytes(16,0));check(!install(),"wrong common key rejected by content hashes");check(snapshot(profile)==pristine,"wrong key cannot write profile");write(common,Bytes(k.begin(),k.end()));
    check(install(),error);check(result.title==title && result.uid==4108 && result.contents==3 && result.ticket_common_key_index==141,"installed identity and original selector reported");
    const auto content=profile/"title/00010005/77523945/content";
    check(read(content/"title.tmd")==fixture.tmd && read(profile/"ticket/00010005/77523945.tik")==fixture.ticket,"original license and TMD are byte exact");
    check(read(content/"00000000.app")==fixture.content[0] && fs::file_size(content/"00000001.app")==0 && read(profile/"shared1/00000000.app")==fixture.content[2],"ordinary, empty and shared plaintext contents installed");
    check(read(profile/"sys/uid.sys").size()==24 && read(profile/"shared1/content.map").size()==28,"UID and shared map extended");
    check(read(profile/"title/00010001/57523945/data/save.dat")==pristine.at("title/00010001/57523945/data/save.dat") && read(profile/"ticket/00010001/57523945.tik")==pristine.at("ticket/00010001/57523945.tik"),"existing native progress and imported licenses preserved");
    const auto installed=snapshot(profile);check(install(),error);check(snapshot(profile)==installed,"reimport is idempotent");
    text(content/"00000000.app","conflicting installed bytes");const auto conflict=snapshot(profile);check(!install(),"conflicting installed content rejected");check(snapshot(profile)==conflict,"conflict does not overwrite files or metadata");write(content/"00000000.app",fixture.content[0]);
    const auto before_lock=snapshot(profile);
#ifdef _WIN32
    const auto lock_path=profile/".wiillr-wad-import.lock";HANDLE lock=CreateFileW(lock_path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);check(lock!=INVALID_HANDLE_VALUE,"fixture profile lock");check(!install(),"concurrent importer rejected");CloseHandle(lock);
#else
    const int lock=::open((profile/".wiillr-wad-import.lock").c_str(),O_CREAT|O_RDWR,0600);check(lock>=0 && !flock(lock,LOCK_EX|LOCK_NB),"fixture profile lock");check(!install(),"concurrent importer rejected");::close(lock);
#endif
    check(snapshot(profile)==before_lock,"locked profile is unchanged");
    const auto alias=root.path/"profile-link";std::error_code ec;fs::create_directory_symlink(profile,alias,ec);
    if(!ec){check(!wii_wad_install(utf8(wad).c_str(),utf8(common).c_str(),utf8(alias).c_str(),title,&result,error,sizeof error),"linked profile rejected");fs::remove(alias);std::cout<<"Linked profile rejection passed\n";}
    else std::cout<<"Linked profile check unavailable on this host: "<<ec.message()<<'\n';
    check(read(wad)==fixture.wad && read(common)==Bytes(k.begin(),k.end()),"original WAD and key remain unchanged");
    for(const auto& e:fs::directory_iterator(profile))check(!e.path().filename().string().starts_with(".wiillr-import-"),"owned staging cleaned");
    std::cout<<"Native WAD validation, decryption, shared/empty contents, persistence, idempotence and failure contracts passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
