/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/menu_welcome.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
using Bytes=std::vector<uint8_t>;
static void check(bool b,const char* why){if(!b){fprintf(stderr,"welcome fixture: %s\n",why);std::abort();}}
static uint16_t rd16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
static uint32_t rd32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
static uint32_t rdl32(const uint8_t* p){return uint32_t(rd16(p))|uint32_t(rd16(p+2))<<16;}
static void wr16(uint8_t* p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
static void wr32(uint8_t* p,uint32_t v){for(int i=3;i>=0;i--){p[i]=uint8_t(v);v>>=8;}}
static void wrl32(uint8_t* p,uint32_t v){wr16(p,uint16_t(v));wr16(p+2,uint16_t(v>>16));}
constexpr size_t size=20*1024*1024,cs=512,fats=81920,root=32+2*fats,begin=root+4096;
static size_t cluster(unsigned c){return begin+(c-2)*cs;}
static void fat(Bytes& b,unsigned c,uint16_t v){wr16(b.data()+32+2*c,v);wr16(b.data()+32+fats+2*c,v);}
static void entry(Bytes& b,size_t at,const char* raw,unsigned c,unsigned n,uint8_t flags=32){
    memcpy(b.data()+at,raw,11);b[at+11]=flags;wr16(b.data()+at+26,uint16_t(c));wrl32(b.data()+at+28,n);
}
static Bytes fixture(bool existing=true){
    Bytes b(size);memcpy(b.data(),"VFF \xfe\xff\x01\0",8);wr32(b.data()+8,size);b[13]=32;
    fat(b,0,0xfff0);fat(b,1,0xffff);
    if(existing){
        /* Independent FAT LFN for cdb.conf and ordinary existing mail. Only
         * synthetic bytes are used, never a player's message or fixture. */
        auto* l=b.data()+root;std::fill_n(l,32,0xff);l[0]=0x41;l[11]=15;l[12]=0;wr16(l+26,0);
        uint8_t sum=0;for(char c:std::string("CDB~1   CON"))sum=uint8_t(((sum&1)?128:0)+(sum>>1)+uint8_t(c));l[13]=sum;
        const unsigned positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
        for(size_t i=0;i<9;i++)wr16(l+positions[i],uint16_t("cdb.conf"[i]));
        entry(b,root+32,"CDB~1   CON",2,4);fat(b,2,0xffff);wr32(b.data()+cluster(2),42);
        entry(b,root+64,"OLDMAIL TXT",3,400);fat(b,3,0xffff);
        for(unsigned n=0;n<cs;n++)b[cluster(3)+n]=uint8_t(n*17+9);
        /* Native timestamps/attributes and otherwise unused bytes must survive. */
        std::fill_n(b.begin()+root+64+12,14,0x55);wr16(b.data()+root+64+20,0);
    }return b;
}
static size_t find(const Bytes& b,unsigned dir,const char* raw){
    std::vector<unsigned> chain;if(!dir)chain.push_back(0);else for(unsigned c=dir;c<0xfff8;c=rd16(b.data()+32+c*2))chain.push_back(c);
    for(auto c:chain){size_t first=c?cluster(c):root,last=first+(c?cs:4096);
        for(size_t at=first;at<last;at+=32){if(!b[at])return 0;if(b[at]!=0xe5 && b[at+11]!=15 && !memcmp(b.data()+at,raw,11))return at;}}
    return 0;
}
static unsigned subdir(const Bytes& b,unsigned dir,const char* raw){const auto e=find(b,dir,raw);check(e && (b[e+11]&16),"expected date directory");return rd16(b.data()+e+26);}
static Bytes contents(const Bytes& b,size_t e){
    check(e!=0,"expected file");Bytes out;unsigned remaining=rdl32(b.data()+e+28),c=rd16(b.data()+e+26);
    while(remaining){check(c>=2 && c<0xfff8,"valid fixture chain");const unsigned n=std::min(unsigned(cs),remaining);out.insert(out.end(),b.begin()+cluster(c),b.begin()+cluster(c)+n);remaining-=n;c=rd16(b.data()+32+c*2);}return out;
}
static uint32_t crc(const uint8_t* p,unsigned count){
    /* Independent MSB-first implementation using the reflected input bytes. */
    auto reverse=[](uint32_t v,unsigned bits){uint32_t r=0;while(bits--){r=r*2+(v&1);v>>=1;}return r;};
    uint32_t value=~0u;for(unsigned i=0;i<count;i++){value^=reverse(p[i],8)<<24;for(unsigned n=0;n<8;n++)value=(value<<1)^((value&0x80000000)?0x04c11db7:0);}
    return ~reverse(value,32);
}
static std::string text(const Bytes& b,size_t offset){std::string s;for(size_t i=offset;i+1<b.size();i+=2){check(!b[i],"ASCII test text in UTF16BE");if(!b[i+1])return s;s+=char(b[i+1]);}check(false,"terminated text");return {};}
static void refuse_unchanged(Bytes b,WiiMenuWelcomeResult expected,uint64_t seconds,uint64_t id){
    const auto old=b;check(wii_menu_welcome_image(b.data(),b.size(),seconds,id,true)==expected,"expected refusal");check(b==old,"refusal preserved all bytes");
}
static Bytes read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});}
static void write(const std::filesystem::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));check(bool(f),"fixture write");}
int main(int argc,char** argv){
    check(argc==2,"disposable test directory argument required");
    constexpr uint64_t id=0x1122334455667788;
    using namespace std::chrono;
    const uint64_t seconds=uint64_t(duration_cast<std::chrono::seconds>(sys_days{year{2026}/11/14}-sys_days{year{2000}/1/1}).count())+12*3600+34*60+56;
    auto b=fixture(),old=b;
    check(wii_menu_welcome_image(nullptr,0,0,0,false)==WII_MENU_WELCOME_DISABLED,"disabled without profile/clock");
    check(wii_menu_welcome_image(b.data(),b.size(),seconds,id,true)==WII_MENU_WELCOME_CREATED,"first memo created");
    check(std::equal(old.begin()+root+64,old.begin()+root+96,b.begin()+root+64),"existing mail entry preserved byte-exact");
    check(std::equal(old.begin()+cluster(3),old.begin()+cluster(3)+cs,b.begin()+cluster(3)),"existing mail allocation preserved byte-exact");
    check(std::equal(old.begin(),old.begin()+32,b.begin()),"VFF header preserved");
    check(!memcmp(b.data()+32,b.data()+32+fats,fats),"both FATs agree");
    check(rd32(contents(b,root+32).data())==43,"existing CDB counter advanced exactly once");
    unsigned d=subdir(b,0,"2026       ");d=subdir(b,d,"10         ");d=subdir(b,d,"14         ");
    d=subdir(b,d,"12         ");d=subdir(b,d,"34         ");d=subdir(b,d,"WELC_00    ");d=subdir(b,d,"TXT        ");
    char filename[12];snprintf(filename,sizeof filename,"%08X000",unsigned(seconds));const auto mail=find(b,d,filename);auto record=contents(b,mail);
    check(!memcmp(record.data(),"CDBFILE\2",8) && rd32(record.data()+0x70)==43 && rd32(record.data()+0x7c)==seconds,"CDB header/date/id metadata");
    check(rd32(record.data()+8)==0x11223344 && rd32(record.data()+12)==0x55667788,"supplied CDB identity");
    check(!memcmp(record.data()+0x400,"RI_5",4) && rd32(record.data()+0x40c)==0,"actual local memo type");
    const uint64_t ticks=uint64_t(rd32(record.data()+0x410))<<32|rd32(record.data()+0x414);
    check(ticks==seconds*60750000,"Wii time ticks, independent of host date");
    check(rd32(record.data()+0x540)==crc(record.data()+0x400,0x140),"independent record CRC");
    check(text(record,0x400+rd32(record.data()+0x51c))=="Welcome aboard!","persisted title");
    const auto body=text(record,0x400+rd32(record.data()+0x520));
    check(body=="wiillr brought to you by\nMatthew \"Gamemaster\" Stanley.","exact two-line credit without forced word splits");
    auto flag=find(b,0,"WIILLR  WEL");check(flag && b[flag+11]==6,"hidden/system mod-owned state");
    auto saved=b;check(wii_menu_welcome_image(b.data(),b.size(),seconds+86400,id,true)==WII_MENU_WELCOME_PRESENT && b==saved,"restart/day change idempotence");
    b[mail]=0xe5;saved=b;check(wii_menu_welcome_image(b.data(),b.size(),seconds+86400,id,true)==WII_MENU_WELCOME_PRESENT && b==saved,"deleting welcome never resends it");
    auto empty=fixture(false);check(wii_menu_welcome_image(empty.data(),empty.size(),0,id,true)==WII_MENU_WELCOME_CREATED,"empty initialized VFF creates initial counter");
    check(rd32(contents(empty,find(empty,0,"CDB~1   CON")).data())==0,"initial counter zero");
    d=subdir(empty,0,"2000       ");d=subdir(empty,d,"00         ");(void)subdir(empty,d,"01         ");
    auto fixed=old;fat(fixed,0,0xfff8);
    check(wii_menu_welcome_image(fixed.data(),fixed.size(),seconds,id,true)==WII_MENU_WELCOME_CREATED,"FAT fixed-media descriptor also supported");
    auto bad=old;fat(bad,0,0xfff1);refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;fat(bad,0,0xfff7);refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;fat(bad,1,0xfff0);refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;bad[32+fats+4]^=1;refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;fat(bad,4,4);refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;entry(bad,root+96,"DUPMAIL TXT",3,400);refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;wr32(bad.data()+cluster(2),0xffffffff);refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;bad[root+13]^=1;refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;bad[root+32+11]|=1;refuse_unchanged(bad,WII_MENU_WELCOME_INVALID,seconds,id);
    bad=old;bad[6]=2;refuse_unchanged(bad,WII_MENU_WELCOME_UNSUPPORTED,seconds,id);
    bad=old;bad[13]=64;refuse_unchanged(bad,WII_MENU_WELCOME_UNSUPPORTED,seconds,id);
    bad=old;entry(bad,root+128,"STALE   TXT",0,0);refuse_unchanged(bad,WII_MENU_WELCOME_UNSUPPORTED,seconds,id);
    bad=old;for(unsigned c=4;c<(size-begin)/cs+2;c++)fat(bad,c,0xffff);refuse_unchanged(bad,WII_MENU_WELCOME_FULL,seconds,id);
    refuse_unchanged(old,WII_MENU_WELCOME_INVALID,0x100000000ull,id);
    refuse_unchanged(old,WII_MENU_WELCOME_INVALID,seconds,0);
    namespace fs=std::filesystem;const auto profile=fs::absolute(argv[1])/"welcome-synthetic-profile";
    check(!fs::exists(profile),"test-owned fresh directory only");fs::create_directories(profile/"title/00000001/00000002/data");
    {std::ofstream f(profile/".wiillr-nand-profile");f<<"wiillr-copied-nand-v1\n";}
    auto p=profile/"title/00000001/00000002/data/cdb.vff";
    check(wii_menu_welcome(profile.string().c_str(),seconds,id,true)==WII_MENU_WELCOME_NOT_READY,"missing store is not reformatted");
    write(p,old);write(profile/"unrelated-save",Bytes{1,7,9,4});
    {std::ofstream f(p.string()+".wiillr-welcome.tmp");f<<"prior transaction";}
    check(wii_menu_welcome(profile.string().c_str(),seconds,id,true)==WII_MENU_WELCOME_IO_ERROR && read(p)==old,"stale temp refusal preserves original");
    fs::remove(p.string()+".wiillr-welcome.tmp");
    check(wii_menu_welcome(profile.string().c_str(),seconds,id,true)==WII_MENU_WELCOME_CREATED,"atomic copied-profile provider");
    saved=read(p);check(wii_menu_welcome(profile.string().c_str(),seconds+86400,id,true)==WII_MENU_WELCOME_PRESENT && read(p)==saved,"persisted once across fresh API calls");
    check(read(profile/"unrelated-save")==Bytes({1,7,9,4}),"unrelated profile content unchanged");
    check(!fs::exists(p.string()+".wiillr-welcome.tmp"),"successful transaction leaves no temp");
    /* No recursive cleanup: these exact files/directories were created here. */
    fs::remove(p);fs::remove(profile/"unrelated-save");fs::remove(profile/".wiillr-nand-profile");
    for(auto q=p.parent_path();q!=profile.parent_path();q=q.parent_path())fs::remove(q);
    puts("welcome: real CDB envelope/date, preservation, refusal, hidden state and file idempotence passed");
}
