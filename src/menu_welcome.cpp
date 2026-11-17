/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wiillr/menu_welcome.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

/* Format references, not imported implementations:
 * koopthekoopa/wii-ipl: RIPL_BoardRecord.h, iplCdbManager.cpp,
 * RevoEX/{CDBAttr,CDBConv,CDBId,CDBRecordKey}.c. In particular cdb.conf is
 * INSIDE CDB:, contains a single BE counter, and belongs to this transaction.
 * WiiBrew VFF and AndrewPiroli/WiiVFF document the FAT16 envelope.
 * Original implementation below; no Nintendo/decomp code is included. */
namespace {
using Bytes=std::vector<uint8_t>;
struct Refusal { WiiMenuWelcomeResult reason; };
[[noreturn]] void refuse(WiiMenuWelcomeResult r){throw Refusal{r};}
void require(bool b){if(!b)refuse(WII_MENU_WELCOME_INVALID);}
uint16_t le16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
uint32_t le32(const uint8_t* p){return uint32_t(le16(p))|uint32_t(le16(p+2))<<16;}
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
void put16(uint8_t* p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
void putle32(uint8_t* p,uint32_t v){put16(p,uint16_t(v));put16(p+2,uint16_t(v>>16));}
void put32(uint8_t* p,uint32_t v){for(int i=3;i>=0;--i){p[i]=uint8_t(v);v>>=8;}}
void put64(uint8_t* p,uint64_t v){put32(p,uint32_t(v>>32));put32(p+4,uint32_t(v));}
uint32_t crc32(const uint8_t* p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(int b=0;b<8;b++)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
using Name=std::array<uint8_t,11>;
Name name(const std::string& s){
    Name out;out.fill(' ');auto dot=s.find('.');auto a=s.substr(0,dot),b=dot==s.npos?"":s.substr(dot+1);
    require(!a.empty() && a.size()<=8 && b.size()<=3);
    for(size_t i=0;i<a.size();i++)out[i]=uint8_t(std::toupper(static_cast<unsigned char>(a[i])));
    for(size_t i=0;i<b.size();i++)out[8+i]=uint8_t(std::toupper(static_cast<unsigned char>(b[i])));
    return out;
}
const Name receipt=name("WIILLR.WEL");
constexpr char receipt_data[]="wiillr-menu-welcome-v1\n";
struct Entry {size_t at;uint16_t cluster;uint32_t size;bool directory;};
class Volume {
public:
    Bytes data;size_t cluster_size,fat_size,root,data_begin,clusters;
    std::vector<bool> owned;
    explicit Volume(const uint8_t* p,size_t n):data(p,p+n){
        require(n>=0x1020 && n<=64u*1024u*1024u && !memcmp(p,"VFF ",4));
        if(le16(p+4)!=0xfffe || le16(p+6)!=0x0001)refuse(WII_MENU_WELCOME_UNSUPPORTED);
        const size_t volume=be32(p+8);cluster_size=(size_t(p[12])<<8|p[13])*16;
        if(cluster_size!=512)refuse(WII_MENU_WELCOME_UNSUPPORTED);
        require(volume<=n && n-volume<=32 && volume%cluster_size==0);
        const size_t count=volume/cluster_size;
        if(count<=0xff5 || count>=0xfff5)refuse(WII_MENU_WELCOME_UNSUPPORTED);
        fat_size=(count*2+cluster_size-1)&~(cluster_size-1);root=32+2*fat_size;data_begin=root+4096;
        require(data_begin<=n && fat_size/2>=2);
        clusters=std::min((n-data_begin)/cluster_size,fat_size/2-2);
        require(clusters>0 && !memcmp(p+32,p+32+fat_size,fat_size));
        /* FAT[0] embeds the media byte, not a data-chain EOC. Nintendo VFF
         * uses F0; fixed-media FAT16 may use F8..FF. FAT[1] remains reserved. */
        require((fat(0)==0xfff0 || fat(0)>=0xfff8) && fat(1)>=0xfff8);
        owned.assign(clusters+2,false);
        validate_dir(0,0);
        /* Lost allocated chains are retained, never recycled. Their pointers
         * still must be bounded; free clusters cannot be owned by a live file. */
        for(size_t c=2;c<clusters+2;c++){
            auto v=fat(uint16_t(c));require(v==0 || v==0xfff7 || v>=0xfff8 || (v>=2 && v<clusters+2));
        }
        std::vector<uint8_t> color(clusters+2);
        for(size_t c=2;c<clusters+2;c++){
            if(color[c] || !fat(uint16_t(c)) || fat(uint16_t(c))==0xfff7)continue;
            size_t at=c;std::vector<size_t> path;
            while(at<0xfff8){
                require(at>=2 && at<clusters+2 && color[at]!=1);
                if(color[at]==2)break;
                require(fat(uint16_t(at)) && fat(uint16_t(at))!=0xfff7);
                color[at]=1;path.push_back(at);at=fat(uint16_t(at));
            }
            for(auto a:path)color[a]=2;
        }
    }
    uint16_t fat(uint16_t c)const{return le16(data.data()+32+size_t(c)*2);}
    void setfat(uint16_t c,uint16_t v){put16(data.data()+32+size_t(c)*2,v);put16(data.data()+32+fat_size+size_t(c)*2,v);}
    size_t offset(uint16_t c)const{require(c>=2 && c<clusters+2);return data_begin+(c-2)*cluster_size;}
    std::vector<uint16_t> chain(uint16_t first)const{
        std::vector<uint16_t> out;std::vector<bool> seen(clusters+2,false);
        while(first<0xfff8){require(first>=2 && first<clusters+2 && !seen[first]);seen[first]=true;out.push_back(first);first=fat(first);}
        return out;
    }
    std::vector<size_t> slots(uint16_t dir)const{
        std::vector<size_t> out;
        if(!dir){for(size_t a=root;a<root+4096;a+=32)out.push_back(a);}
        else for(auto c:chain(dir))for(size_t a=offset(c);a<offset(c)+cluster_size;a+=32)out.push_back(a);
        return out;
    }
    void validate_dir(uint16_t dir,unsigned depth){
        require(depth<32);
        if(dir)for(auto c:chain(dir)){require(!owned[c]);owned[c]=true;}
        std::vector<Name> names;
        unsigned lfn_count=0,lfn_remaining=0;uint8_t lfn_sum=0;std::vector<uint16_t> lfn;
        for(auto a:slots(dir)){
            const auto* p=data.data()+a;if(!p[0]){require(!lfn_count);break;}
            if(p[0]==0xe5){lfn_count=lfn_remaining=0;continue;}
            if(p[11]==0x0f){
                const unsigned ordinal=p[0]&31;
                require(ordinal && ordinal<=20 && !(p[0]&0xa0) && !p[12] && !le16(p+26));
                if(p[0]&0x40){require(!lfn_count);lfn_count=lfn_remaining=ordinal;lfn.assign(ordinal*13,0xffff);lfn_sum=p[13];}
                require(lfn_count && ordinal==lfn_remaining && p[13]==lfn_sum);
                constexpr unsigned positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
                for(unsigned i=0;i<13;i++)lfn[(ordinal-1)*13+i]=le16(p+positions[i]);
                --lfn_remaining;continue;
            }
            if(lfn_count){
                uint8_t sum=0;for(unsigned i=0;i<11;i++)sum=uint8_t(((sum&1)?128:0)+(sum>>1)+p[i]);
                require(!lfn_remaining && lfn_sum==sum);
                const auto end=std::find(lfn.begin(),lfn.end(),0);const size_t length=size_t(end-lfn.begin());
                require(length>13*(lfn_count-1) && length<=255 && std::find(lfn.begin(),end,0xffff)==end);
                if(end!=lfn.end())require(std::all_of(end+1,lfn.end(),[](uint16_t c){return c==0xffff;}));
                lfn_count=0;
            }
            require(!(p[11]&0xc0) && le16(p+20)==0);
            if(p[0]=='.')continue;
            Name nm;memcpy(nm.data(),p,11);require(std::find(names.begin(),names.end(),nm)==names.end());names.push_back(nm);
            if(p[11]&8){require(!dir && !(p[11]&16));continue;}
            const auto c=le16(p+26);const auto size=le32(p+28);
            if(p[11]&16){require(c>=2);validate_dir(c,depth+1);}
            else if(size || c){auto cs=chain(c);require(!cs.empty() && uint64_t(cs.size())*cluster_size>=size);
                for(auto cc:cs){require(!owned[cc]);owned[cc]=true;}}
        }
        require(!lfn_count);
    }
    bool find(uint16_t dir,const Name& nm,Entry& entry)const{
        for(auto a:slots(dir)){
            const auto* p=data.data()+a;if(!p[0])break;if(p[0]!=0xe5 && p[11]!=0x0f && !memcmp(p,nm.data(),11)){
                require(!(p[11]&8));entry={a,le16(p+26),le32(p+28),bool(p[11]&16)};return true;}
        }return false;
    }
    bool config(Entry& entry)const{
        /* cdb.conf exceeds 8.3. Resolve its actual FAT long name; do not assume
         * a host-generated short alias used by some other formatter. */
        size_t previous=0;
        for(auto a:slots(0)){
            const auto* p=data.data()+a;if(!p[0])break;
            if(p[0]!=0xe5 && p[11]!=0x0f && previous){
                const auto* l=data.data()+previous;
                if(l[0]==0x41 && l[11]==0x0f){
                    uint8_t sum=0;for(unsigned i=0;i<11;i++)sum=uint8_t(((sum&1)?128:0)+(sum>>1)+p[i]);
                    constexpr unsigned positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
                    std::string text;bool ascii=true;
                    for(auto pos:positions){const auto c=le16(l+pos);if(!c)break;if(c>127){ascii=false;break;}text+=char(std::toupper(c));}
                    if(ascii && text=="CDB.CONF"){
                        require(l[13]==sum && !l[12] && !le16(l+26) && !(p[11]&25));
                        entry={a,le16(p+26),le32(p+28),false};return true;
                    }
                }
            }
            previous=p[0]!=0xe5 && p[11]==0x0f?a:0;
        }return false;
    }
    Bytes read(const Entry& e)const{
        require(!e.directory);Bytes out;out.reserve(e.size);
        if(e.size)for(auto c:chain(e.cluster)){size_t n=std::min(cluster_size,size_t(e.size)-out.size());out.insert(out.end(),data.begin()+offset(c),data.begin()+offset(c)+n);if(out.size()==e.size)break;}
        require(out.size()==e.size);return out;
    }
    uint16_t allocate(){
        for(size_t c=2;c<clusters+2;c++)if(!fat(uint16_t(c))){
            setfat(uint16_t(c),0xffff);std::fill_n(data.begin()+offset(uint16_t(c)),cluster_size,0);return uint16_t(c);}
        refuse(WII_MENU_WELCOME_FULL);
    }
    size_t empty(uint16_t dir,bool long_name=false){
        /* Never reuse deleted entries: leave their recoverable metadata alone. */
        const auto entries=slots(dir);
        for(size_t i=0;i<entries.size();i++)if(data[entries[i]]==0){
            /* Keep the directory's end marker. Never expose stale metadata
             * that was unreachable beyond the previous end marker. */
            const size_t needed=long_name?2:1;
            if(i+needed>entries.size())refuse(WII_MENU_WELCOME_FULL);
            for(size_t j=1;j<needed;j++)require(entries[i+j]==entries[i]+32*j && !data[entries[i+j]]);
            if(i+needed<entries.size() && data[entries[i+needed]])refuse(WII_MENU_WELCOME_UNSUPPORTED);
            return entries[i];
        }
        if(!dir)refuse(WII_MENU_WELCOME_FULL);
        if(long_name)refuse(WII_MENU_WELCOME_UNSUPPORTED);
        auto cs=chain(dir);auto c=allocate();setfat(cs.back(),c);return offset(c);
    }
    Entry add(uint16_t dir,const Name& nm,const Bytes& bytes,bool isdir=false,const char* long_name=nullptr){
        Entry prior;require(!find(dir,nm,prior));auto a=empty(dir,long_name);uint16_t first=0,last=0;
        for(size_t done=0;done<bytes.size();done+=cluster_size){auto c=allocate();if(last)setfat(last,c);else first=c;last=c;
            std::copy_n(bytes.begin()+done,std::min(cluster_size,bytes.size()-done),data.begin()+offset(c));}
        if(long_name){
            require(strlen(long_name)<13);auto* p=data.data()+a;std::fill_n(p,32,0xff);p[0]=0x41;p[11]=0x0f;p[12]=0;put16(p+26,0);
            uint8_t sum=0;for(auto c:nm)sum=uint8_t(((sum&1)?128:0)+(sum>>1)+c);p[13]=sum;
            constexpr unsigned positions[]={1,3,5,7,9,14,16,18,20,22,24,28,30};
            for(size_t i=0;i<=strlen(long_name);i++)put16(p+positions[i],uint8_t(long_name[i]));
            a+=32;
        }
        auto* p=data.data()+a;std::fill_n(p,32,0);memcpy(p,nm.data(),11);p[11]=isdir?16:32;put16(p+26,first);putle32(p+28,isdir?0:uint32_t(bytes.size()));
        return {a,first,uint32_t(bytes.size()),isdir};
    }
    uint16_t directory(uint16_t parent,const std::string& label){
        Entry e;if(find(parent,name(label),e)){require(e.directory);return e.cluster;}
        Bytes b(cluster_size);std::fill_n(b.begin(),11,' ');b[0]='.';b[11]=16;
        std::fill_n(b.begin()+32,11,' ');b[32]='.';b[33]='.';b[43]=16;put16(b.data()+58,parent);
        e=add(parent,name(label),b,true);put16(data.data()+offset(e.cluster)+26,e.cluster);return e.cluster;
    }
    void replace_counter(const Entry& e,uint32_t value){require(!e.directory && e.size==4);auto cs=chain(e.cluster);require(!cs.empty());put32(data.data()+offset(cs[0]),value);}
};
std::string digits(unsigned v,unsigned width){auto s=std::to_string(v);require(s.size()<=width);return std::string(width-s.size(),'0')+s;}
std::string hex(uint32_t v){const char* d="0123456789ABCDEF";std::string s(8,'0');for(int i=7;i>=0;i--){s[size_t(i)]=d[v&15];v>>=4;}return s;}
Bytes memo(uint32_t seconds,uint64_t id,uint32_t counter){
    constexpr char title[]="Welcome aboard!";
    constexpr char body[]="wiillr brought to you by\nMatthew \"Gamemaster\" Stanley.";
    const size_t title_size=((sizeof title-1+4)*2)&~size_t(7),body_size=((sizeof body-1+4)*2)&~size_t(7);
    const size_t rbr=0x400,head=0x148,title_at=head,body_at=head+title_size;
    Bytes out(0x400+((head+title_size+body_size+31)&~size_t(31)));
    memcpy(out.data(),"CDBFILE",7);out[7]=2;put64(out.data()+8,id);
    constexpr char desc[]="Wii Menu welcome";out[0x10]=sizeof desc;memcpy(out.data()+0x14,desc,sizeof desc);
    put32(out.data()+0x70,counter);put32(out.data()+0x7c,seconds);
    /* Plain NAND records have no SD signature/encryption fields. */
    memcpy(out.data()+rbr,"RI_5",4);put64(out.data()+rbr+0x10,uint64_t(seconds)*60750000);
    out[rbr+0x11b]=1; /* local memo, no reply */
    put32(out.data()+rbr+0x11c,uint32_t(title_at));put32(out.data()+rbr+0x120,uint32_t(body_at));
    for(size_t i=0;i<sizeof title-1;i++)out[rbr+title_at+i*2+1]=uint8_t(title[i]);
    for(size_t i=0;i<sizeof body-1;i++)out[rbr+body_at+i*2+1]=uint8_t(body[i]);
    put32(out.data()+rbr+0x140,crc32(out.data()+rbr,0x140));return out;
}
bool reparse(const std::filesystem::path& p){
#ifdef _WIN32
    const auto a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return std::filesystem::is_symlink(std::filesystem::symlink_status(p));
#endif
}
bool write_new(const std::filesystem::path& p,const Bytes& data){
#ifdef _WIN32
    const auto file=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool ok=WriteFile(file,data.data(),DWORD(data.size()),&written,nullptr) && written==data.size() && FlushFileBuffers(file);
    const bool closed=CloseHandle(file)!=0;return ok && closed;
#else
    const int file=open(p.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(file<0)return false;
    size_t done=0;while(done<data.size()){const auto n=write(file,data.data()+done,data.size()-done);if(n<=0){close(file);return false;}done+=size_t(n);}
    const bool ok=fsync(file)==0;const bool closed=close(file)==0;return ok && closed;
#endif
}
}
extern "C" WiiMenuWelcomeResult wii_menu_welcome_image(uint8_t* p,size_t n,uint64_t seconds,uint64_t id,bool enabled){
    if(!enabled)return WII_MENU_WELCOME_DISABLED;
    if(!p || seconds>UINT32_MAX || !id || n>64u*1024u*1024u)return WII_MENU_WELCOME_INVALID;
    try {
        Volume v(p,n);Entry e;
        if(v.find(0,receipt,e)){
            const auto b=v.read(e);require(b.size()==sizeof receipt_data-1 && !memcmp(b.data(),receipt_data,b.size()));
            return WII_MENU_WELCOME_PRESENT;
        }
        Entry counter;uint32_t next=0;const bool had_counter=v.config(counter);
        if(had_counter){auto b=v.read(counter);require(b.size()==4 && be32(b.data())!=UINT32_MAX);next=be32(b.data())+1;}
        using namespace std::chrono;
        const auto point=sys_days{year{2000}/1/1}+std::chrono::seconds{seconds};
        const auto day=floor<days>(point);const year_month_day ymd{day};const hh_mm_ss tod{point-day};
        auto dir=v.directory(0,digits(unsigned(int(ymd.year())),4));
        dir=v.directory(dir,digits(unsigned(ymd.month())-1,2)); /* OSCalendarTime.mon is zero-based */
        dir=v.directory(dir,digits(unsigned(ymd.day()),2));dir=v.directory(dir,digits(unsigned(tod.hours().count()),2));
        dir=v.directory(dir,digits(unsigned(tod.minutes().count()),2));dir=v.directory(dir,"WELC_00");dir=v.directory(dir,"TXT");
        std::string filename;
        for(unsigned serial=0;serial<1000;serial++){filename=hex(uint32_t(seconds))+"."+digits(serial,3);if(!v.find(dir,name(filename),e))break;if(serial==999)refuse(WII_MENU_WELCOME_FULL);}
        v.add(dir,name(filename),memo(uint32_t(seconds),id,next));
        if(had_counter)v.replace_counter(counter,next);else {Bytes b(4);put32(b.data(),next);v.add(0,name("CDB~1.CON"),b,false,"cdb.conf");}
        const auto flag=v.add(0,receipt,Bytes(receipt_data,receipt_data+sizeof receipt_data-1));v.data[flag.at+11]=6; /* hidden/system mod state */
        /* Validate the resulting graph before committing even one caller byte. */
        Volume check(v.data.data(),v.data.size());(void)check;
        memcpy(p,v.data.data(),n);return WII_MENU_WELCOME_CREATED;
    }catch(const Refusal& e){return e.reason;}catch(...){return WII_MENU_WELCOME_IO_ERROR;}
}
extern "C" WiiMenuWelcomeResult wii_menu_welcome(const char* profile,uint64_t seconds,uint64_t id,bool enabled){
    if(!enabled)return WII_MENU_WELCOME_DISABLED;
    if(!profile)return WII_MENU_WELCOME_INVALID;
    namespace fs=std::filesystem;
    try {
        const auto root=fs::absolute(fs::path(profile));
        for(auto p=root;!p.empty();p=p.parent_path()){require(!reparse(p));if(p==p.root_path())break;}
        std::ifstream marker(root/".wiillr-nand-profile");std::string magic;std::getline(marker,magic);require(magic=="wiillr-copied-nand-v1");
        auto target=root;
        for(const char* part:{"title","00000001","00000002","data","cdb.vff"}){target/=part;require(!reparse(target));}
        if(!fs::exists(target))return WII_MENU_WELCOME_NOT_READY;
        require(fs::is_regular_file(target));const auto count=fs::file_size(target);require(count>=0x1020 && count<=64u*1024u*1024u);
        Bytes data(size_t(count),uint8_t{});
        {std::ifstream f(target,std::ios::binary);f.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size()));if(!f)return WII_MENU_WELCOME_IO_ERROR;}
        const auto result=wii_menu_welcome_image(data.data(),data.size(),seconds,id,true);if(result!=WII_MENU_WELCOME_CREATED)return result;
        const auto temp=fs::path(target.native()+fs::path(".wiillr-welcome.tmp").native());
        if(fs::exists(temp) || reparse(temp))return WII_MENU_WELCOME_IO_ERROR;
        /* Caller owns the exclusive, pre-mount profile boundary. Never replace
         * a store changed since our snapshot. A stale temp fails closed. */
        {std::ifstream original(target,std::ios::binary);Bytes current(size_t(count),uint8_t{});original.read(reinterpret_cast<char*>(current.data()),std::streamsize(current.size()));
         if(!original || fs::file_size(target)!=count)return WII_MENU_WELCOME_IO_ERROR;
         auto expected=current;const auto check=wii_menu_welcome_image(expected.data(),expected.size(),seconds,id,true);
         if(check!=WII_MENU_WELCOME_CREATED || expected!=data)return WII_MENU_WELCOME_IO_ERROR;}
        if(!write_new(temp,data))return WII_MENU_WELCOME_IO_ERROR;
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return WII_MENU_WELCOME_IO_ERROR;
#else
        fs::rename(temp,target);
#endif
        return WII_MENU_WELCOME_CREATED;
    }catch(const Refusal& e){return e.reason;}catch(...){return WII_MENU_WELCOME_IO_ERROR;}
}
