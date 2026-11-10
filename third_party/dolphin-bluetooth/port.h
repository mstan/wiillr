// SPDX-License-Identifier: MIT
#pragma once
#include "types.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
extern "C" {
#include "cpu/cpu.h"
#include "memory/memory.h"
}
#define DEBUG_LOG_FMT(...) do {} while(0)
#define INFO_LOG_FMT(...) do {} while(0)
#define ERROR_LOG_FMT(...) do {} while(0)
#define WARN_LOG_FMT(...) do {} while(0)
#define DEBUG_ASSERT(x) do {if(!(x))throw std::runtime_error("Bluetooth assertion: " #x);}while(0)
#define ASSERT(x) DEBUG_ASSERT(x)
#define DEBUG_ASSERT_MSG(category,x,...) DEBUG_ASSERT(x)
#define ASSERT_MSG(category,x,...) DEBUG_ASSERT(x)
#define PanicAlertFmt(...) throw std::runtime_error("Unsupported Bluetooth operation")
#define PanicAlertFmtT(...) throw std::runtime_error("Unsupported Bluetooth operation")
constexpr unsigned MAX_BBMOTES=5,WIIMOTE_BALANCE_BOARD=4;
namespace Wiimote {constexpr u64 UPDATE_FREQ=200;}
namespace Common {
inline u16 swap16(u16 v){return u16(v>>8|v<<8);}
inline u32 swap32(u32 v){return (v>>24)|((v>>8)&0xff00)|((v<<8)&0xff0000)|(v<<24);}
inline u16 swap16(const void* p){const auto* b=static_cast<const u8*>(p);return u16(b[0]<<8|b[1]);}
inline u32 swap32(const void* p){const auto* b=static_cast<const u8*>(p);return u32(b[0])<<24|u32(b[1])<<16|u32(b[2])<<8|b[3];}
}
namespace WiimoteCommon {struct ButtonData {u16 hex=0;static constexpr u16 BUTTON_MASK=0x9f1f;};}
namespace WiimoteEmu {struct DesiredWiimoteState {WiimoteCommon::ButtonData buttons;};}
namespace WiimoteCommon {
constexpr u8 HID_TYPE_HANDSHAKE=0,HID_TYPE_SET_REPORT=5,HID_TYPE_DATA=10;
constexpr u8 HID_HANDSHAKE_SUCCESS=0,HID_PARAM_INPUT=1,HID_PARAM_OUTPUT=2;
constexpr u32 MAX_PAYLOAD=23;
class HIDWiimote {
public:
  virtual ~HIDWiimote()=default;
  enum class SensorBarState:bool {Disabled,Enabled};
  virtual void EventLinked()=0;virtual void EventUnlinked()=0;
  virtual void PrepareInput(WiimoteEmu::DesiredWiimoteState*,SensorBarState)=0;
  virtual void Update(const WiimoteEmu::DesiredWiimoteState&)=0;
  virtual void InterruptDataOutput(const u8*,u32)=0;
  virtual ButtonData GetCurrentlyPressedButtons()=0;
  void SetWiimoteDeviceIndex(u8 value){device_index=value;}
  void SetInterruptCallback(std::function<void(u8,const u8*,u32)> cb){callback=std::move(cb);}
protected:
  std::function<void(u8,const u8*,u32)> callback;
  u8 device_index=0;
};
}
namespace IOS::HLE {
class Memory {
public:
  explicit Memory(CPUState* cpu):cpu(cpu){}
  u8* GetPointerForRange(u32 address,u32 size) {
    const u32 p=gcn_reservation_key(address);u32 available=0;
    if(!(p<cpu->ram_size || (p>=0x10000000 && p-0x10000000<cpu->mem2_size)))
      throw std::runtime_error("Bluetooth buffer is not RAM");
    auto* ptr=gcn_mem_resolve(cpu,address,&available);
    if(!ptr || size>available)throw std::runtime_error("Bluetooth buffer range");
    return ptr;
  }
  u8 Read_U8(u32 p){return *GetPointerForRange(p,1);}
  u16 Read_U16(u32 p){return Common::swap16(GetPointerForRange(p,2));}
  u32 Read_U32(u32 p){return Common::swap32(GetPointerForRange(p,4));}
  void CopyFromEmu(void* dst,u32 p,u32 size){std::memcpy(dst,GetPointerForRange(p,size),size);}
  void CopyToEmu(u32 p,const void* src,u32 size){
    GetPointerForRange(p,size);const auto* bytes=static_cast<const u8*>(src);
    for(u32 i=0;i<size;i++)mem_write8_cia(cpu,p+i,bytes[i],cpu->pc);
  }
  CPUState* cpu;
};
struct System {explicit System(CPUState* cpu):memory(cpu){} Memory& GetMemory(){return memory;} Memory memory;};
struct Request {u32 address=0;};
struct Completion {u32 message;s32 result;u64 ready;};
struct EmulationKernel {
  explicit EmulationKernel(System& s):system(s){}
  System& GetSystem(){return system;}
  void EnqueueIPCReply(const Request& request,s32 result) {
    if(completions.size()>=64)throw std::runtime_error("Bluetooth completion overflow");
    completions.push_back({request.address,result,system.memory.cpu->cycles+6000});
  }
  System& system;std::deque<Completion> completions;
};
namespace USB {
struct V0Message {
  V0Message(EmulationKernel& ios,Request request,u32 data,u32 length):ios_request(request),data_address(data),length(length),kernel(&ios){}
  void FillBuffer(const void* data,u32 size)const {
    if(size>length)throw std::runtime_error("Bluetooth endpoint overflow");
    kernel->system.memory.CopyToEmu(data_address,data,size);
  }
  Request ios_request;u32 data_address,length;u8 endpoint=0;EmulationKernel* kernel;
};
using V0CtrlMessage=V0Message;using V0BulkMessage=V0Message;using V0IntrMessage=V0Message;
}
}
