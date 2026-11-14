/* SPDX-License-Identifier: MIT */
#include "wiillr/network.h"
#include "memory/memory.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <cerrno>
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
#ifdef _WIN32
using HostSocket=SOCKET;using SockLen=int;
constexpr HostSocket invalid=INVALID_SOCKET;
void close_socket(HostSocket s){closesocket(s);}
bool nonblocking(HostSocket s){u_long on=1;return ioctlsocket(s,FIONBIO,&on)==0;}
int socket_error(){return WSAGetLastError();}
#define HOST_ERROR(x) WSA##x
struct SocketRuntime {bool ok;SocketRuntime(){WSADATA d{};ok=WSAStartup(MAKEWORD(2,2),&d)==0;}~SocketRuntime(){if(ok)WSACleanup();}};
#else
using HostSocket=int;using SockLen=socklen_t;
constexpr HostSocket invalid=-1;
void close_socket(HostSocket s){close(s);}
bool nonblocking(HostSocket s){const int f=fcntl(s,F_GETFL,0);return f>=0 && fcntl(s,F_SETFL,f|O_NONBLOCK)==0;}
int socket_error(){return errno;}
#define HOST_ERROR(x) x
struct SocketRuntime {bool ok=true;};
#endif
/* Wii SO errors are an ABI, not host errno/WSA values. */
int net_error(int e){
    switch(e){
    case 0:return 0;
    case HOST_ERROR(EWOULDBLOCK):return -6;
    case HOST_ERROR(EINPROGRESS):return -26;
    case HOST_ERROR(EALREADY):return -7;
    case HOST_ERROR(EBADF):case HOST_ERROR(ENOTSOCK):return -8;
    case HOST_ERROR(EACCES):return -2;
    case HOST_ERROR(EADDRINUSE):return -3;
    case HOST_ERROR(EADDRNOTAVAIL):return -4;
    case HOST_ERROR(EAFNOSUPPORT):return -5;
    case HOST_ERROR(ECONNABORTED):return -13;
    case HOST_ERROR(ECONNREFUSED):return -14;
    case HOST_ERROR(ECONNRESET):return -15;
    case HOST_ERROR(EHOSTUNREACH):return -23;
    case HOST_ERROR(EINVAL):return -28;
    case HOST_ERROR(EISCONN):return -30;
    case HOST_ERROR(EMSGSIZE):return -35;
    case HOST_ERROR(ENETDOWN):return -38;
    case HOST_ERROR(ENETUNREACH):return -40;
    case HOST_ERROR(ENOBUFS):return -42;
    case HOST_ERROR(ENOTCONN):return -56;
    case HOST_ERROR(ETIMEDOUT):return -76;
    default:return -29;
    }
}
u32 be32(const u8* p){return u32(p[0])<<24|u32(p[1])<<16|u32(p[2])<<8|p[3];}
u16 be16(const u8* p){return u16(u16(p[0])<<8|p[1]);}
void store32(u8* p,u32 v){p[0]=u8(v>>24);p[1]=u8(v>>16);p[2]=u8(v>>8);p[3]=u8(v);}
void store16(u8* p,u16 v){p[0]=u8(v>>8);p[1]=u8(v);}
u8* memory(CPUState* cpu,u32 address,u32 size){
    const u32 p=gcn_reservation_key(address);
    if(!(p<cpu->ram_size || (p>=0x10000000u && p-0x10000000u<cpu->mem2_size)))return nullptr;
    u32 available=0;u8* data=gcn_mem_resolve(cpu,address,&available);
    return data && available>=size?data:nullptr;
}
struct Buffer {
    u32 address;std::vector<u8> bytes;std::vector<std::pair<u32,u32>> writes;
    void put(u32 offset,const void* src,u32 size){std::memcpy(bytes.data()+offset,src,size);writes.emplace_back(offset,size);}
    void word(u32 offset,u32 v){u8 b[4];store32(b,v);put(offset,b,4);}
};
std::atomic<unsigned> resolver_count=0;
struct Resolver {
    std::shared_ptr<SocketRuntime> runtime;std::atomic<bool> done=false;u32 ip=0;
    ~Resolver(){resolver_count.fetch_sub(1,std::memory_order_relaxed);}
};
struct Request {
    u32 message,command,fd,op;std::vector<Buffer> in,out;
    Clock::time_point start=Clock::now();std::shared_ptr<Resolver> resolver;
    bool cancelled=false,wireless=false;
};
struct Socket {HostSocket host=invalid;bool nonblock=false,connecting=false;};
bool sockaddr_in_from(const u8* data,sockaddr_in* address){
    if(data[0]<8 || data[1]!=2)return false;
    *address={};address->sin_family=AF_INET;
    std::memcpy(&address->sin_port,data+2,2);std::memcpy(&address->sin_addr,data+4,4);return true;
}
void sockaddr_out(Buffer& out,const sockaddr_in& address){
    u8 b[8]={8,2};std::memcpy(b+2,&address.sin_port,2);std::memcpy(b+4,&address.sin_addr,4);out.put(0,b,8);
}
struct Interface {u32 ip=0,mask=0,broadcast=0,dns[2]{};};
Interface host_interface(){
    Interface result;
    /* UDP connect selects a host route without sending a datagram. A local
     * interface is not a successful Wii Internet connection test. */
    HostSocket probe=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(probe==invalid)return result;
    sockaddr_in route{};route.sin_family=AF_INET;route.sin_port=htons(9);route.sin_addr.s_addr=htonl(0xc0000201u);
    sockaddr_in local{};SockLen length=sizeof local;
    if(connect(probe,reinterpret_cast<sockaddr*>(&route),sizeof route)==0 &&
       getsockname(probe,reinterpret_cast<sockaddr*>(&local),&length)==0)result.ip=ntohl(local.sin_addr.s_addr);
    close_socket(probe);
    if(!result.ip || (result.ip>>24)==127){result.ip=0;return result;}
#ifdef _WIN32
    ULONG size=16384;std::vector<u8> adapters(size);
    auto* first=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapters.data());
    ULONG rc=GetAdaptersAddresses(AF_INET,0,nullptr,first,&size);
    if(rc==ERROR_BUFFER_OVERFLOW && size<=1048576){adapters.resize(size);first=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(adapters.data());rc=GetAdaptersAddresses(AF_INET,0,nullptr,first,&size);}
    if(rc==NO_ERROR)for(auto* a=first;a;a=a->Next)for(auto* u=a->FirstUnicastAddress;u;u=u->Next){
        if(u->Address.lpSockaddr->sa_family!=AF_INET)continue;
        const auto* addr=reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
        if(ntohl(addr->sin_addr.s_addr)!=result.ip)continue;
        const u32 prefix=u->OnLinkPrefixLength;result.mask=prefix && prefix<=32?0xffffffffu<<(32-prefix):0;
        unsigned n=0;for(auto* d=a->FirstDnsServerAddress;d && n<2;d=d->Next)
            if(d->Address.lpSockaddr->sa_family==AF_INET)result.dns[n++]=ntohl(reinterpret_cast<const sockaddr_in*>(d->Address.lpSockaddr)->sin_addr.s_addr);
    }
#else
    ifaddrs* first=nullptr;
    if(getifaddrs(&first)==0){
        for(auto* a=first;a;a=a->ifa_next)if(a->ifa_addr && a->ifa_netmask && a->ifa_addr->sa_family==AF_INET &&
            ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr)==result.ip)
            result.mask=ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
        freeifaddrs(first);
    }
    std::ifstream config("/etc/resolv.conf");std::string key,value;unsigned n=0;
    while(n<2 && config>>key){std::getline(config,value);if(key!="nameserver")continue;
        const auto begin=value.find_first_not_of(" \t");if(begin==std::string::npos)continue;value=value.substr(begin);value=value.substr(0,value.find_first_of(" \t#\r"));
        in_addr ip{};if(inet_pton(AF_INET,value.c_str(),&ip)==1)result.dns[n++]=ntohl(ip.s_addr);}
#endif
    result.broadcast=result.ip|~result.mask;return result;
}
}
struct WiiNetwork {
    std::shared_ptr<SocketRuntime> runtime=std::make_shared<SocketRuntime>();
    std::array<Socket,24> sockets{};std::list<Request> pending;std::array<u8,6> mac{};
    Clock::time_point next_poll{};Interface cached_interface{};Clock::time_point interface_at{};
    s32 wireless_fd=-1;u32 wireless_mode=0;bool wireless_active=false;
    ~WiiNetwork(){for(auto& s:sockets)if(s.host!=invalid)close_socket(s.host);}
    Interface current_interface(){const auto now=Clock::now();if(now>=interface_at){cached_interface=host_interface();interface_at=now+std::chrono::seconds(1);}return cached_interface;}
};
namespace {
constexpr s32 waiting=INT32_MIN;
constexpr s32 wd_invalid=s32(0x80008000u),wd_parameter=s32(0x80008001u),wd_unavailable=s32(0x80008002u);
std::array<u8,6> virtual_bssid(const WiiNetwork& net){auto mac=net.mac;mac[0]=(mac[0]|2)&0xfe;mac[5]^=0x80;return mac;}
/* The retained SDK WD_Scan supplies one 0x4e-byte ScanInfo and one result
 * vector. Its BSS consumer reads rates at2e/30, channel36 and advances by
 * length*2 (the descriptor length is halfwords, not bytes). The 64-byte record
 * below has no vendor/AOSS information element and names only our endpoint. */
s32 execute_wireless(WiiNetwork& net,Request& r){
    if(net.wireless_fd<0 || u32(net.wireless_fd)!=r.fd)return wd_invalid;
    if(r.op==0x1001 || r.op==0x1003){
        if(!r.in.empty() || !r.out.empty())return wd_parameter;
        return r.op==0x1001?s32(net.wireless_mode):s32(net.wireless_active && net.current_interface().ip);
    }
    if(r.op==0x1002){
        if(r.in.size()!=1 || !r.out.empty() || r.in[0].bytes.size()<4)return wd_parameter;
        const u32 state=be32(r.in[0].bytes.data());if(state>1)return wd_parameter;
        net.wireless_active=state!=0;return 0;
    }
    if(r.op==0x100e){
        if(!r.in.empty() || r.out.size()!=1 || r.out[0].bytes.size()<0x90)return wd_parameter;
        std::array<u8,0x90> info{};std::memcpy(info.data(),net.mac.data(),6);
        const bool available=net.current_interface().ip!=0;store16(info.data()+6,available?2:0);
        /* Channel1 only; no Nintendo DS radio capability. Firmware target is
         * USA. This is the virtual driver identity, not a host radio version. */
        std::memcpy(info.data()+10,"US",2);info[14]=1;info[15]=u8(available);
        const char version[]="wiillr host IPv4";std::memcpy(info.data()+16,version,sizeof version);
        r.out[0].put(0,info.data(),u32(info.size()));return 0;
    }
    if(r.op==0x100a){
        if(r.in.size()!=1 || r.out.size()!=1 || r.in[0].bytes.size()!=0x4e || r.out[0].bytes.size()<2)return wd_parameter;
        const auto& scan=r.in[0].bytes;const u16 ssid_length=be16(scan.data()+12);
        if(ssid_length>32 || be16(scan.data()+10)>1)return wd_parameter;
        const auto bssid=virtual_bssid(net);bool any=true,exact=true;
        for(unsigned n=0;n<6;++n){any=any && scan[4+n]==0xff;exact=exact && scan[4+n]==bssid[n];}
        bool match=(any || exact) && (be16(scan.data())&2) && net.current_interface().ip;
        if(ssid_length){
            if(ssid_length!=6)match=false;
            else {const char ssid[]="wiillr";for(unsigned n=0;n<6;++n)
                if((scan[14+n]&scan[46+n])!=(u8(ssid[n])&scan[46+n]))match=false;}
        }
        if(match && r.out[0].bytes.size()<0x42)return wd_parameter;
        const u8 count[2]={0,u8(match)};r.out[0].put(0,count,2);
        if(match){
            std::array<u8,0x40> entry{};store16(entry.data(),0x20);store16(entry.data()+2,0xffff);
            std::memcpy(entry.data()+4,bssid.data(),bssid.size());store16(entry.data()+10,6);std::memcpy(entry.data()+12,"wiillr",6);
            store16(entry.data()+0x2c,1); /* infrastructure, open */
            store16(entry.data()+0x2e,0x000f);store16(entry.data()+0x30,0x0fff);
            store16(entry.data()+0x32,100);store16(entry.data()+0x34,1);store16(entry.data()+0x36,1);
            r.out[0].put(2,entry.data(),u32(entry.size()));
        }return 0;
    }
    if(r.op==0x8000 || r.op==0x8001){
        /* There are no raw radio frames/notifications in the host-IP
         * transport. A valid receive stays pending until descriptor close;
         * no event payload or radio association is invented. */
        if(r.out.empty())return wd_parameter;
        return waiting;
    }
    return wd_unavailable; /* notably no AOSS vendor pairing/802.11 raw frames */
}
bool supported(u32 command,u32 op,bool wireless){
    if(wireless)return command==7 && ((op>=0x1000 && op<=0x1010) || op==0x8000 || op==0x8001);
    if(command==6)switch(op){case 2:case 3:case 4:case 5:case 6:case 7:case 14:case 15:case 16:case 17:case 21:case 31:return true;}
    if(command==7)switch(op){case 12:case 13:case 26:case 27:case 28:return true;}
    return false;
}
s32 execute(WiiNetwork& net,Request& r){
    for(auto& b:r.out)b.writes.clear();
    if(r.cancelled)return r.wireless?wd_invalid:-11;
    if(r.wireless)return execute_wireless(net,r);
    if(r.command==6){
        auto& in=r.in[0].bytes;auto& out=r.out[0];
        if(r.op==31)return net.runtime->ok?0:-38;
        if(r.op==16)return s32(net.current_interface().ip);
        if(r.op==17 || r.op==21){
            if(in.empty() || in.size()>256 || !std::memchr(in.data(),0,in.size()))return -28;
            if((r.op==17 && out.bytes.size()!=0x460) || (r.op==21 && out.bytes.size()<4))return -28;
            if(!r.resolver){
                unsigned count=resolver_count.load(std::memory_order_relaxed);
                do {if(count>=4)return -6;}while(!resolver_count.compare_exchange_weak(count,count+1,std::memory_order_relaxed));
                try{r.resolver=std::make_shared<Resolver>();}catch(...){resolver_count.fetch_sub(1,std::memory_order_relaxed);throw;}
                r.resolver->runtime=net.runtime;
                const std::string name(reinterpret_cast<const char*>(in.data()));const auto state=r.resolver;
                /* The resolver owns only host data. Destruction/IOS reset may
                 * discard its result without joining a slow system resolver or
                 * retaining guest RAM. At most four resolver jobs exist in the
                 * entire process, including jobs discarded by an IOS reset. */
                std::thread([state,name]{addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;addrinfo* list=nullptr;
                    if(getaddrinfo(name.c_str(),nullptr,&hints,&list)==0){if(list)state->ip=ntohl(reinterpret_cast<sockaddr_in*>(list->ai_addr)->sin_addr.s_addr);freeaddrinfo(list);}state->done.store(true,std::memory_order_release);}).detach();
            }
            if(!r.resolver->done.load(std::memory_order_acquire))return waiting;
            if(!r.resolver->ip)return r.op==21?0:-1;
            if(r.op==21){out.word(0,r.resolver->ip);return 1;}
            std::array<u8,0x460> bytes{};const u32 address=out.address;
            store32(bytes.data(),address+0x10);store32(bytes.data()+4,address+0x344);bytes[9]=2;bytes[11]=4;
            store32(bytes.data()+12,address+0x340);std::memcpy(bytes.data()+0x10,in.data(),in.size());
            store32(bytes.data()+0x110,r.resolver->ip);store32(bytes.data()+0x340,address+0x110);out.put(0,bytes.data(),u32(bytes.size()));return 0;
        }
        if(in.size()<4)return -28;
        const u32 fd=be32(in.data());
        if(r.op==15){
            if(in.size()<12)return -28;
            const u32 type=be32(in.data()+4),protocol=be32(in.data()+8);
            if(fd!=2 || (type!=1 && type!=2) || (protocol && protocol!=(type==1?6u:17u)))return -5;
            unsigned slot=0;while(slot<net.sockets.size() && net.sockets[slot].host!=invalid)++slot;
            if(slot==net.sockets.size())return -33;
            HostSocket s=socket(AF_INET,type==1?SOCK_STREAM:SOCK_DGRAM,int(protocol));
            if(s==invalid)return net_error(socket_error());
            if(!nonblocking(s)){const auto error=net_error(socket_error());close_socket(s);return error;}
#ifdef SO_NOSIGPIPE
            int one=1;setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
            net.sockets[slot]={s,false,false};return s32(slot);
        }
        if(fd>=net.sockets.size() || net.sockets[fd].host==invalid)return -8;
        auto& s=net.sockets[fd];
        if(r.op==3){
            close_socket(s.host);s={};
            for(auto& p:net.pending)if(&p!=&r){
                const bool socket_op=(p.command==6 && (p.op==2 || p.op==4 || p.op==5 || p.op==6 || p.op==7 || p.op==14)) ||
                    (p.command==7 && (p.op==12 || p.op==13));
                const unsigned input=p.command==7 && p.op==13?1:0;
                if(!p.wireless && socket_op && p.in.size()>input && p.in[input].bytes.size()>=4 && be32(p.in[input].bytes.data())==fd)p.cancelled=true;
            }
            return 0;
        }
        if(r.op==5){if(in.size()<12)return -28;const u32 command=be32(in.data()+4);if(command==3)return s.nonblock?4:0;if(command!=4 || (be32(in.data()+8)&~4u))return -28;s.nonblock=(be32(in.data()+8)&4)!=0;return 0;}
        if(r.op==14){if(in.size()<8 || be32(in.data()+4)>2)return -28;return shutdown(s.host,int(be32(in.data()+4)))==0?0:net_error(socket_error());}
        if(r.op==6 || r.op==7){if(out.bytes.size()<8)return -28;sockaddr_in a{};SockLen size=sizeof a;
            const int rc=r.op==6?getpeername(s.host,reinterpret_cast<sockaddr*>(&a),&size):getsockname(s.host,reinterpret_cast<sockaddr*>(&a),&size);
            if(rc<0)return net_error(socket_error());
            sockaddr_out(out,a);return 0;}
        if(in.size()<16)return -28;
        sockaddr_in address{};if(!sockaddr_in_from(in.data()+8,&address))return -5;
        if(r.op==2)return bind(s.host,reinterpret_cast<sockaddr*>(&address),sizeof address)==0?0:net_error(socket_error());
        if(r.op==4){
            if(s.connecting){fd_set write,error;FD_ZERO(&write);FD_ZERO(&error);FD_SET(s.host,&write);FD_SET(s.host,&error);timeval zero{};
                const int ready=select(int(s.host)+1,nullptr,&write,&error,&zero);if(ready<0)return net_error(socket_error());if(!ready)return s.nonblock?-26:waiting;
                int e=0;SockLen length=sizeof e;if(getsockopt(s.host,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&e),&length)<0)e=socket_error();s.connecting=false;return net_error(e);}
            if(connect(s.host,reinterpret_cast<sockaddr*>(&address),sizeof address)==0)return 0;
            const int e=net_error(socket_error());if(e==-6 || e==-26 || e==-7){s.connecting=true;return s.nonblock?-26:waiting;}return e;
        }
    }else{
        if(r.op==26)return net.runtime->ok?0:-38;
        if(r.op==27){for(auto& s:net.sockets){if(s.host!=invalid)close_socket(s.host);s={};}for(auto& p:net.pending)if(&p!=&r && !p.wireless)p.cancelled=true;return 0;}
        if(r.op==28){
            if(r.in.size()!=1 || r.in[0].bytes.size()<8 || r.out.size()!=2 || r.out[1].bytes.size()<4)return -28;
            const auto& in=r.in[0].bytes;if(be32(in.data())!=0xfffe)return -51;
            const u32 option=be32(in.data()+4);auto& out=r.out[0];const auto iface=net.current_interface();u32 size=4;
            switch(option){
            case 0x1003:if(out.bytes.size()<4)return -28;out.word(0,0);break;
            case 0x1004:if(out.bytes.size()<6)return -28;out.put(0,net.mac.data(),6);size=6;break;
            case 0x1005:if(out.bytes.size()<4)return -28;out.word(0,iface.ip?1:0);break;
            case 0x4002:if(out.bytes.size()<4)return -28;out.word(0,iface.ip?1:0);break;
            case 0x4003:if(out.bytes.size()<12)return -28;out.word(0,iface.ip);out.word(4,iface.mask);out.word(8,iface.broadcast);size=12;break;
            case 0xb003:if(out.bytes.size()<8)return -28;if(!iface.dns[0])return -38;out.word(0,iface.dns[0]);out.word(4,iface.dns[1]);size=8;break;
            default:return -63;
            }r.out[1].word(0,size);return 0;
        }
        const bool sending=r.op==13;
        if((sending && (r.in.size()!=2 || !r.out.empty())) || (!sending && (r.in.size()!=1 || r.out.empty() || r.out.size()>2)))return -28;
        const auto& args=r.in[sending?1:0].bytes;if(args.size()<(sending?12u:8u))return -28;
        const u32 fd=be32(args.data()),flags=be32(args.data()+4);
        if(fd>=net.sockets.size() || net.sockets[fd].host==invalid)return -8;
        if(flags&~7u)return -28;
        auto& s=net.sockets[fd];const bool block=!s.nonblock && !(flags&4);
        int host_flags=(flags&1?MSG_OOB:0)|(!sending && (flags&2)?MSG_PEEK:0);
#ifdef MSG_NOSIGNAL
        host_flags|=MSG_NOSIGNAL;
#endif
        int count;
        if(sending){
            const bool destination=be32(args.data()+8)!=0;sockaddr_in address{};
            if(destination && (args.size()<20 || !sockaddr_in_from(args.data()+12,&address)))return -28;
            const auto& data=r.in[0].bytes;
            count=int(sendto(s.host,reinterpret_cast<const char*>(data.data()),int(data.size()),host_flags,
                destination?reinterpret_cast<sockaddr*>(&address):nullptr,destination?sizeof address:0));
        }else{
            if(r.out.size()==2 && r.out[1].bytes.size()<8)return -28;
            auto& out=r.out[0];sockaddr_in address{};SockLen size=sizeof address;
            count=int(recvfrom(s.host,reinterpret_cast<char*>(out.bytes.data()),int(out.bytes.size()),host_flags,reinterpret_cast<sockaddr*>(&address),&size));
            if(count>=0){out.writes.emplace_back(0,u32(count));if(r.out.size()==2)sockaddr_out(r.out[1],address);}
        }
        if(count>=0)return count;
        const int e=net_error(socket_error());return block && e==-6?waiting:e;
    }
    return -63;
}
bool publish(CPUState* cpu,Request& r){
    for(auto& out:r.out)if(!memory(cpu,out.address,u32(out.bytes.size())))return false;
    for(const auto& out:r.out)for(const auto [offset,size]:out.writes)for(u32 n=0;n<size;++n)
        mem_write8_cia(cpu,out.address+offset+n,out.bytes[offset+n],cpu->pc);
    return true;
}
bool capture(CPUState* cpu,u32 address,u32 size,Buffer* buffer){
    if(size>65536)return false;
    buffer->address=address;
    if(!size)return true;
    const u8* source=memory(cpu,address,size);if(!source)return false;
    buffer->bytes.assign(source,source+size);return true;
}
}
extern "C" WiiNetwork* wii_network_create(const u8 mac[6]){try{auto* net=new WiiNetwork;if(mac)std::memcpy(net->mac.data(),mac,6);return net;}catch(...){return nullptr;}}
extern "C" void wii_network_destroy(WiiNetwork* net){delete net;}
extern "C" bool wii_network_link(WiiNetwork* net){try{return net && net->runtime->ok && net->current_interface().ip;}catch(...){return false;}}
static int submit(WiiNetwork* net,CPUState* cpu,u32 message,s32* result,bool wireless){
    if(!net || !cpu || !result)return 0;
    try{
        const u8* raw=memory(cpu,message,32);if(!raw)return 0;
        Request r{};r.message=message;r.command=be32(raw);r.fd=be32(raw+8);r.op=be32(raw+12);r.wireless=wireless;
        if(!supported(r.command,r.op,wireless))return 0;
        *result=-4;
        if(net->pending.size()>=24){*result=-2;return 1;}
        for(const auto& p:net->pending)if(p.message==message){*result=-2;return 1;}
        if(r.command==6){
            r.in.resize(1);r.out.resize(1);
            if(!capture(cpu,be32(raw+16),be32(raw+20),&r.in[0]) || !capture(cpu,be32(raw+24),be32(raw+28),&r.out[0]))return 1;
        }else{
            const u32 inputs=be32(raw+16),outputs=be32(raw+20);
            if(inputs>2 || outputs>2)return 1;
            const u8* vectors=memory(cpu,be32(raw+24),(inputs+outputs)*8);if(!vectors && inputs+outputs)return 1;
            r.in.resize(inputs);r.out.resize(outputs);
            for(u32 n=0;n<inputs+outputs;++n)if(!capture(cpu,be32(vectors+n*8),be32(vectors+n*8+4),n<inputs?&r.in[n]:&r.out[n-inputs]))return 1;
        }
        net->pending.push_back(std::move(r));auto at=std::prev(net->pending.end());
        *result=execute(*net,*at);if(*result==waiting)return 2;
        if(!publish(cpu,*at))*result=-4;
        net->pending.erase(at);return 1;
    }catch(...){net->pending.remove_if([message](const Request& r){return r.message==message;});*result=-49;return 1;}
}
extern "C" int wii_network_submit(WiiNetwork* net,CPUState* cpu,u32 message,s32* result){return submit(net,cpu,message,result,false);}
extern "C" int wii_network_wireless_submit(WiiNetwork* net,CPUState* cpu,u32 message,s32* result){return submit(net,cpu,message,result,true);}
extern "C" s32 wii_network_wireless_open(WiiNetwork* net,u32 fd,u32 flags){
    if(!net || !wii_network_link(net))return -6;
    if(fd>=24)return wd_invalid;
    if((flags&0xffff)!=3 || (flags&~0x10003u))return wd_unavailable;
    if(net->wireless_fd>=0)return wd_unavailable;
    net->wireless_fd=s32(fd);net->wireless_mode=3;net->wireless_active=false;return 0;
}
extern "C" s32 wii_network_wireless_close(WiiNetwork* net,u32 fd){
    if(!net || net->wireless_fd<0 || u32(net->wireless_fd)!=fd)return wd_invalid;
    for(auto& r:net->pending)if(r.wireless && r.fd==fd)r.cancelled=true;
    net->wireless_fd=-1;net->wireless_mode=0;net->wireless_active=false;return 0;
}
extern "C" bool wii_network_poll(WiiNetwork* net,CPUState* cpu,u32* message,u32* command,s32* result){
    if(!net || net->pending.empty() || Clock::now()<net->next_poll)return false;
    net->next_poll=Clock::now()+std::chrono::milliseconds(1);
    for(auto at=net->pending.begin();at!=net->pending.end();++at){
        s32 value;
        try{value=execute(*net,*at);}catch(...){value=-49;}
        if(value==waiting && (at->wireless || Clock::now()-at->start<std::chrono::seconds(30)))continue;
        if(value==waiting)value=-76;
        if(!publish(cpu,*at))value=-4;
        *message=at->message;*command=at->command;*result=value;net->pending.erase(at);return true;
    }return false;
}
extern "C" void wii_network_close_device(WiiNetwork* net,u32 fd){if(net)for(auto& p:net->pending)if(!p.wireless && p.fd==fd)p.cancelled=true;}
