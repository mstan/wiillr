// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
using SOCKET=int;
constexpr int INVALID_SOCKET=-1,SOCKET_ERROR=-1;
#define closesocket close
#endif
extern "C" {
#include "wiillr/control.h"
#include "memory/memory.h"
#include "cpu/interpreter.h"
#include "cpu/aot_module.h"
#include "host/host_audio.h"
#include "debug/cost_ring.h"
}
#include <bit>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <cerrno>

struct WiiControl {
  CPUState* cpu;WiiMachine* machine;WiiGx* gx;WiiBluetoothInput* input;bool* input_pending;
  const GcnOverlayModule* native_module;
  SOCKET listener=INVALID_SOCKET,client=INVALID_SOCKET;
  std::string rx,tx;size_t sent=0;u64 last_poll=0,stop_at=0;bool paused=false,quit=false;
  ~WiiControl(){if(client!=INVALID_SOCKET)closesocket(client);if(listener!=INVALID_SOCKET)closesocket(listener);
#ifdef _WIN32
    WSACleanup();
#endif
  }
  static void nonblocking(SOCKET socket) {
#ifdef _WIN32
    u_long enabled=1;if(ioctlsocket(socket,FIONBIO,&enabled))throw std::runtime_error("nonblocking socket");
#else
    if(fcntl(socket,F_SETFL,O_NONBLOCK)<0)throw std::runtime_error("nonblocking socket");
#ifdef SO_NOSIGPIPE
    const int enabled=1;
    if(setsockopt(socket,SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof enabled)<0)throw std::runtime_error("socket SIGPIPE suppression");
#endif
#endif
  }
  static bool would_block() {
#ifdef _WIN32
    return WSAGetLastError()==WSAEWOULDBLOCK;
#else
    return errno==EAGAIN || errno==EWOULDBLOCK;
#endif
  }
  static int transmit(SOCKET socket,const char* data,int size) {
#ifdef _WIN32
    return send(socket,data,size,0);
#elif defined(MSG_NOSIGNAL)
    return send(socket,data,size,MSG_NOSIGNAL);
#else
    return send(socket,data,size,0);
#endif
  }
  void disconnect(){closesocket(client);client=INVALID_SOCKET;rx.clear();tx.clear();sent=0;}
  static u64 number(const std::string& text,int base) {
    if(text.empty() || text[0]=='-')throw std::runtime_error("invalid number");
    size_t end;const auto result=std::stoull(text,&end,base);if(end!=text.size())throw std::runtime_error("invalid number");return result;
  }
  u8* memory(u64 address,u64 size) {
    if(address>UINT32_MAX || size>65536 || size>UINT32_MAX-address)throw std::runtime_error("invalid RAM range");
    const u32 physical=u32(address)&0x1fffffffu;
    if(!(physical<cpu->ram_size || (physical>=0x10000000u && physical-0x10000000u<cpu->mem2_size)))throw std::runtime_error("RAM required");
    u32 available;auto* bytes=gcn_mem_resolve(cpu,u32(address),&available);
    if(!bytes || available<size)throw std::runtime_error("short RAM range");return bytes;
  }
  static std::string hex(const u8* bytes,size_t size) {
    static constexpr char digits[]="0123456789abcdef";std::string out(size*2,'0');
    for(size_t n=0;n<size;n++){out[n*2]=digits[bytes[n]>>4];out[n*2+1]=digits[bytes[n]&15];}return out;
  }
  struct Scanout {
    const u8* bytes;
    u32 width,height,stride;
    size_t size;
  };
  Scanout scanout() {
    u32 address,width,height,stride;
    if(!gx->api || !gx->api->drain() || !gcn_vi_xfb_info(&address,&width,&height,&stride) ||
       !width || !height || width>1024 || height>1024 || stride<width*2)
      throw std::runtime_error("frame unavailable");
    const u64 size=u64(stride)*height;
    const u32 physical=address&0x1fffffffu;
    u32 available;
    if(!(physical<cpu->ram_size || (physical>=0x10000000u && physical-0x10000000u<cpu->mem2_size)))
      throw std::runtime_error("frame RAM required");
    auto* bytes=gcn_mem_resolve(cpu,address,&available);
    if(!bytes || size>available || size>2097152)
      throw std::runtime_error("invalid frame range");
    return {bytes,width,height,stride,size_t(size)};
  }
  std::string fingerprint() {
    if(!paused)throw std::runtime_error("pause before fingerprinting");
    if(gx->api && !gx->api->drain())throw std::runtime_error("GX drain failed");
    GcnHostAudioStats audio{};
    gcn_host_audio_get_stats(&audio);
    const auto dsp=machine->dsp.engine?gcn_dsp_engine_stats(machine->dsp.engine):GcnDspEngineStats{};
    GcnllrNativeGxStatus graphics{};
    if(gx->api)gx->api->status(&graphics);
    std::ostringstream out;
    out << "{\"ok\":true,\"cycles\":" << cpu->cycles
        << ",\"pc\":" << cpu->pc << ",\"mem1_bytes\":" << cpu->ram_size
        << ",\"mem2_bytes\":" << cpu->mem2_size
        << ",\"mem1_fnv1a64\":\"" << std::hex << gcn_aot_fnv1a64(cpu->ram,cpu->ram_size)
        << "\",\"mem2_fnv1a64\":\"" << gcn_aot_fnv1a64(cpu->mem2,cpu->mem2_size)
        << "\",\"pcm_fnv1a64\":\"" << audio.pcm_hash << "\"" << std::dec
        << ",\"pcm_frames\":" << audio.emulated_frames << ",\"scanout\":";
    try {
      const auto frame=scanout();
      out << "{\"width\":" << frame.width << ",\"height\":" << frame.height
          << ",\"stride\":" << frame.stride << ",\"fnv1a64\":\"" << std::hex
          << gcn_aot_fnv1a64(frame.bytes,frame.size) << "\"}" << std::dec;
    } catch(const std::runtime_error&) {
      out << "null";
    }
    out << ",\"gpr\":[";
    for(unsigned i=0;i<32;++i)out << (i?",":"") << cpu->gpr[i];
    out << "],\"fpr_bits\":[";
    for(unsigned i=0;i<32;++i)out << (i?",":"") << std::bit_cast<u64>(cpu->fpr[i]);
    out << "],\"lr\":" << cpu->lr << ",\"ctr\":" << cpu->ctr
        << ",\"cr\":" << cpu->cr << ",\"xer\":" << cpu->xer
        << ",\"msr\":" << cpu->msr << ",\"fpscr\":" << cpu->fpscr
        << ",\"srr0\":" << cpu->srr0 << ",\"srr1\":" << cpu->srr1
        << ",\"timebase\":" << cpu->timebase
        << ",\"fields\":" << machine->fields
        << ",\"mmio_reads\":" << machine->mmio_reads
        << ",\"mmio_writes\":" << machine->mmio_writes
        << ",\"ios_operations\":" << machine->ios.operations
        << ",\"ppc_interpreter\":" << gcn_interpreter_instruction_count()
        << ",\"ppc_misses\":" << gcn_interpreter_unique_miss_count()
        << ",\"dsp_native\":" << dsp.native_instructions
        << ",\"dsp_interpreter\":" << dsp.interpreter_instructions
        << ",\"dsp_misses\":" << dsp.misses
        << ",\"gx_fifo_bytes\":" << graphics.fifo_bytes
        << ",\"gx_draws\":" << graphics.draws
        << ",\"gx_display_copies\":" << graphics.display_copies
        << ",\"gx_texture_copies\":" << graphics.texture_copies;
    if(native_module) {
      out << ",\"native_dispatches\":" << native_module->dispatches
          << ",\"content_hashes\":" << native_module->hashes
          << ",\"invalidations\":" << native_module->invalidations;
    }
    out << "}";
    return out.str();
  }
  std::string command(const std::string& line) {
    std::istringstream stream(line);std::vector<std::string> args;std::string word;
    while(stream>>word)args.push_back(word);if(args.empty())throw std::runtime_error("command required");
    if(args[0]=="status" && args.size()==1) {
      const auto dsp=machine->dsp.engine?gcn_dsp_engine_stats(machine->dsp.engine):GcnDspEngineStats{};
      return "{\"ok\":true,\"cycles\":"+std::to_string(cpu->cycles)+",\"pc\":"+std::to_string(cpu->pc)+
        ",\"fields\":"+std::to_string(machine->fields)+",\"paused\":"+(paused?"true":"false")+
        ",\"halted\":"+(cpu->halted?"true":"false")+",\"gpio_out\":"+std::to_string(machine->gpio_out)+
        ",\"ppc_interpreter\":"+std::to_string(gcn_interpreter_instruction_count())+
        ",\"ppc_misses\":"+std::to_string(gcn_interpreter_unique_miss_count())+",\"dsp_native\":"+std::to_string(dsp.native_instructions)+
        ",\"dsp_interpreter\":"+std::to_string(dsp.interpreter_instructions)+",\"dsp_misses\":"+std::to_string(dsp.misses)+"}";
    }
    if(args[0]=="pause" && args.size()==1){paused=true;stop_at=0;return "{\"ok\":true}";}
    if(args[0]=="fingerprint" && args.size()==1)return fingerprint();
    if(args[0]=="scheduler" && args.size()==1) {
      const auto& domains=machine->domains;
      std::ostringstream out;
      out << "{\"ok\":true,\"provider\":\"" << (WIILLR_SCHED_DEADLINE?"deadline":"lockstep")
          << "\",\"cap\":" << (WIILLR_SCHED_DEADLINE?WII_MACHINE_EVENT_CAP:64)
          << ",\"poll_period\":" << (WIILLR_SCHED_DEADLINE?WII_MACHINE_POLL_PERIOD:64)
          << ",\"max_event_lateness\":" << machine->domain_max_lateness
          << ",\"failed\":" << (domains.failed?"true":"false")
          << ",\"credits\":" << domains.credits << ",\"observations\":" << domains.observations
          << ",\"services\":[";
      for(unsigned i=0;i<GCN_DOMAIN_COUNT;++i)out << (i?",":"") << domains.services[i];
      out << "],\"max_service_intervals\":[";
      for(unsigned i=0;i<GCN_DOMAIN_COUNT;++i)out << (i?",":"") << machine->domain_max_interval[i];
      out << "]}";return out.str();
    }
    if(args[0]=="cost" && args.size()<=2) {
      const auto limit=args.size()==2?number(args[1],10):120;
      if(!limit || limit>GCN_COST_RING_CAPACITY)throw std::runtime_error("invalid field limit");
      using File=std::unique_ptr<FILE,decltype(&std::fclose)>;
      File file(std::tmpfile(),std::fclose);
      if(!file)throw std::runtime_error("cost query buffer unavailable");
      gcn_cost_write_json(file.get(),u32(limit));
      if(std::ferror(file.get()) || std::fseek(file.get(),0,SEEK_SET))throw std::runtime_error("cost query failed");
      std::string data;
      char buffer[4096];
      while(const auto size=std::fread(buffer,1,sizeof buffer,file.get()))data.append(buffer,size);
      if(std::ferror(file.get()))throw std::runtime_error("cost query failed");
      return "{\"ok\":true,\"cost\":"+data+"}";
    }
    if(args[0]=="clock" && args.size()==2) {
      const auto epoch=number(args[1],10);
      if(!paused || cpu->cycles || epoch>UINT32_MAX)throw std::runtime_error("clock requires a fresh paused boot");
      machine->ios.kd_utc=epoch;
      machine->ios.kd_utc_set=true;
      machine->ios.kd_utc_cycles=UINT64_MAX;
      return "{\"ok\":true}";
    }
    if(args[0]=="until" && args.size()==2) {
      const auto target=number(args[1],10);
      if(target<=cpu->cycles || target-cpu->cycles>50000000000ull)
        throw std::runtime_error("invalid cycle target");
      stop_at=target;
      paused=false;
      return "{\"ok\":true}";
    }
    if(args[0]=="run" && args.size()<=2) {
      const auto budget=args.size()==2?number(args[1],10):0;
      if(budget>50000000000ull || budget>UINT64_MAX-cpu->cycles)throw std::runtime_error("invalid cycle budget");
      stop_at=budget?cpu->cycles+budget:0;paused=false;return "{\"ok\":true}";
    }
    if(args[0]=="quit" && args.size()==1){quit=true;paused=false;return "{\"ok\":true}";}
    if(args[0]=="sync" && args.size()==1) {
      if(!machine->ios.bluetooth || !wii_bluetooth_press_sync(machine->ios.bluetooth))throw std::runtime_error("Bluetooth unavailable");
      return "{\"ok\":true}";
    }
    if(args[0]=="input" && (args.size()==2 || args.size()==7)) {
      auto value=*input;const auto buttons=number(args[1],16);if(buttons&~0x9f1fu)throw std::runtime_error("invalid buttons");value.buttons=u16(buttons);
      if(args.size()==7) {
        const auto x=number(args[2],10),y=number(args[3],10),point=number(args[4],10),present=number(args[5],10),pair=number(args[6],10);
        if(x>1023 || y>767 || point>1 || present>1 || pair>1)throw std::runtime_error("invalid pointer/presence");
        value.pointer_x=u16(x);value.pointer_y=u16(y);value.pointing=point;value.present=present;value.pairing=pair;
      }
      *input=value;*input_pending=true;return "{\"ok\":true}";
    }
    if(args[0]=="read" && args.size()==3) {
      const auto address=number(args[1],16),size=number(args[2],10);auto* data=memory(address,size);
      return "{\"ok\":true,\"data\":\""+hex(data,size)+"\"}";
    }
    if(args[0]=="write" && args.size()==3) {
      const auto address=number(args[1],16);const auto& value=args[2];
      if(value.size()%2 || value.size()>512)throw std::runtime_error("invalid write extent");memory(address,value.size()/2);
      std::vector<u8> bytes;for(size_t n=0;n<value.size();n+=2)bytes.push_back(u8(number(value.substr(n,2),16)));
      for(size_t n=0;n<bytes.size();n++)mem_write8_cia(cpu,u32(address+n),bytes[n],cpu->pc);return "{\"ok\":true}";
    }
    if(args[0]=="frame" && args.size()==1) {
      const auto frame=scanout();
      return "{\"ok\":true,\"width\":"+std::to_string(frame.width)+",\"height\":"+std::to_string(frame.height)+",\"stride\":"+
        std::to_string(frame.stride)+",\"data\":\""+hex(frame.bytes,frame.size)+"\"}";
    }
    throw std::runtime_error("unknown command or arguments");
  }
  void pump() {
    if(client==INVALID_SOCKET){client=accept(listener,nullptr,nullptr);if(client!=INVALID_SOCKET){nonblocking(client);rx.clear();tx.clear();sent=0;}}
    if(client==INVALID_SOCKET)return;
    if(sent<tx.size()) {
      const int count=transmit(client,tx.data()+sent,int(std::min<size_t>(65536,tx.size()-sent)));
      if(count==SOCKET_ERROR && !would_block()){disconnect();return;}
      if(count>0)sent+=count;
      if(sent<tx.size())return;tx.clear();sent=0;
    }
    char bytes[1024];const int count=recv(client,bytes,sizeof bytes,0);
    if(count==0 || (count==SOCKET_ERROR && !would_block())){disconnect();return;}
    if(count>0)rx.append(bytes,count);
    const auto end=rx.find('\n');
    if(end!=std::string::npos) {
      try{tx=command(rx.substr(0,end))+"\n";}catch(const std::exception&){tx="{\"ok\":false,\"error\":\"invalid or unavailable command\"}\n";}
      rx.erase(0,end+1);
      const int written=transmit(client,tx.data(),int(std::min<size_t>(65536,tx.size())));
      if(written==SOCKET_ERROR && !would_block()){disconnect();return;}
      if(written>0)sent=written;
    }
    if(rx.size()>1024){closesocket(client);client=INVALID_SOCKET;rx.clear();}
  }
};
extern "C" WiiControl* wii_control_start(CPUState* cpu,WiiMachine* machine,WiiGx* gx,WiiBluetoothInput* input,bool* pending,
                                        const GcnOverlayModule* native_module) {
  const char* text=std::getenv("WIILLR_CONTROL_PORT");if(!text)return nullptr;
  auto control=std::make_unique<WiiControl>();
  try {
    const auto port=WiiControl::number(text,10);if(!port || port>65535)throw std::runtime_error("port");
#ifdef _WIN32
    WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Winsock");
#endif
    control->listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(control->listener==INVALID_SOCKET)throw std::runtime_error("socket");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(u16(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(control->listener,reinterpret_cast<sockaddr*>(&address),sizeof address)==SOCKET_ERROR || listen(control->listener,1)==SOCKET_ERROR)throw std::runtime_error("listen");
    WiiControl::nonblocking(control->listener);control->cpu=cpu;control->machine=machine;control->gx=gx;control->input=input;control->input_pending=pending;
    control->native_module=native_module;
    control->paused=std::getenv("WIILLR_CONTROL_PAUSED") && std::string(std::getenv("WIILLR_CONTROL_PAUSED"))=="1";
    std::fprintf(stderr,"wiillr loopback control port=%u\n",unsigned(port));return control.release();
  }catch(const std::exception& e){std::fprintf(stderr,"wiillr control: %s\n",e.what());return nullptr;}
}
extern "C" bool wii_control_poll(WiiControl* control) {
  if(!control)return true;
  if(control->stop_at && control->cpu->cycles>=control->stop_at){control->paused=true;control->stop_at=0;}
  if(!control->paused && control->cpu->cycles-control->last_poll<65536)return true;
  control->last_poll=control->cpu->cycles;
  try{control->pump();}catch(...){control->cpu->halted=true;return true;}
  if(control->paused){if(control->gx->api && control->gx->api->service)control->gx->api->service();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  return !control->paused;
}
extern "C" void wii_control_stop(WiiControl* control){delete control;}
extern "C" u64 wii_control_deadline(const WiiControl* control,u64 deadline) {
  return control && control->stop_at?std::min(deadline,control->stop_at):deadline;
}
extern "C" bool wii_control_quit_requested(WiiControl* control){return control && control->quit;}
