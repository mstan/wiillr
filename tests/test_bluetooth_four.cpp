// SPDX-License-Identifier: MIT
#include "BTEmu.h"
#include "l2cap.h"
#include <set>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
  CPUState cpu{};CHECK(cpu_init(&cpu));auto* bt=wii_bluetooth_create(&cpu,nullptr,nullptr);CHECK(bt);
  auto put=[&](u32 p,u32 v){mem_write32_cia(&cpu,p,v,0);};
  auto vec=[&](u32 p,u32 i,u32 a,u32 n){put(p+8*i,a);put(p+8*i+4,n);};
  auto usb=[&](u32 request,u32 vectors,u32 operation,u8 endpoint,u32 buffer,u16 size) {
    put(request,7);put(request+12,operation);put(request+16,2);put(request+20,1);put(request+24,vectors);
    vec(vectors,0,vectors+0x80,1);vec(vectors,1,vectors+0x82,2);vec(vectors,2,buffer,size);
    cpu.ram[vectors+0x80]=endpoint;cpu.ram[vectors+0x82]=u8(size>>8);cpu.ram[vectors+0x83]=u8(size);
    return wii_bluetooth_submit(bt,request);
  };
  bool hci_pending=false;
  auto post_hci=[&]() {if(hci_pending)return true;hci_pending=usb(0x3000,0x3100,2,0x81,0x3300,256);return hci_pending;};
  auto receive=[&](u32 wanted) {
    for(unsigned attempt=0;attempt<2000;++attempt) {
      u32 message;s32 result;bool ready;
      if(!wii_bluetooth_poll(bt,&message,&result,&ready))return std::vector<u8>{};
      if(ready) {
        if(message==0x3000) {
          hci_pending=false;
          if(wanted!=0x3000 && !post_hci())return std::vector<u8>{};
        }
        if(message==wanted && result>0) {
          const u32 address=wanted==0x3000?0x3300:0x6300;
          return std::vector<u8>(cpu.ram+address,cpu.ram+address+result);
        }
      } else cpu.cycles+=6000;
    }
    return std::vector<u8>{};
  };
  auto event=[&]() {if(!post_hci())return std::vector<u8>{};return receive(0x3000);};
  auto command=[&](u16 opcode,const std::vector<u8>& args) {
    const u16 size=u16(args.size()+3);put(0x2000,7);put(0x200c,0);put(0x2010,6);put(0x2014,1);put(0x2018,0x2100);
    const u32 sizes[]={1,1,2,2,2,1};for(unsigned i=0;i<6;++i)vec(0x2100,i,0x2200+4*i,sizes[i]);
    cpu.ram[0x2200]=0x20;cpu.ram[0x2210]=u8(size);cpu.ram[0x2211]=u8(size>>8);
    vec(0x2100,6,0x2300,size);cpu.ram[0x2300]=u8(opcode);cpu.ram[0x2301]=u8(opcode>>8);cpu.ram[0x2302]=u8(args.size());
    std::copy(args.begin(),args.end(),cpu.ram+0x2303);return wii_bluetooth_submit(bt,0x2000);
  };
  auto send_acl=[&](u16 handle,u16 cid,const std::vector<u8>& payload) {
    std::vector<u8> bytes(8+payload.size());const u16 length=u16(payload.size()+4),flags=HCI_MK_CON_HANDLE(handle,HCI_PACKET_START,HCI_POINT2POINT);
    bytes[0]=u8(flags);bytes[1]=u8(flags>>8);bytes[2]=u8(length);bytes[3]=u8(length>>8);
    bytes[4]=u8(payload.size());bytes[5]=u8(payload.size()>>8);bytes[6]=u8(cid);bytes[7]=u8(cid>>8);
    std::copy(payload.begin(),payload.end(),bytes.begin()+8);std::copy(bytes.begin(),bytes.end(),cpu.ram+0x5300);
    return usb(0x5000,0x5100,1,2,0x5300,u16(bytes.size()));
  };
  auto signal=[&](u16 handle,u8 code,u8 id,const std::vector<u8>& params) {
    std::vector<u8> payload{code,id,u8(params.size()),u8(params.size()>>8)};payload.insert(payload.end(),params.begin(),params.end());
    return send_acl(handle,1,payload);
  };
  auto acl=[&]() {
    if(!post_hci() || !usb(0x6000,0x6100,1,0x82,0x6300,256))return std::vector<u8>{};
    return receive(0x6000);
  };
  std::array<WiiBluetoothInput,4> inputs;
  for(unsigned i=0;i<4;++i) {
    inputs[i]={u16(1u<<i),{512,512,616},u16(200+140*i),384,false,true,false};
    CHECK(wii_bluetooth_set_input_indexed(bt,i,&inputs[i]));
    std::vector<u8> params{0x11,2,0x19,0x79,0,u8(i),0x18,0xcc,1,0,0,0,1};
    CHECK(command(HCI_CMD_CREATE_CON,params));const auto status=event();CHECK(!status.empty() && status[0]==HCI_EVENT_COMMAND_STATUS);
    const auto connected=event();CHECK(connected.size()>=5 && connected[0]==HCI_EVENT_CON_COMPL && !connected[2]);
    const u16 handle=u16(connected[3]|connected[4]<<8);CHECK(handle==0x100+i);
    // Negotiate a genuine HID interrupt channel through ordinary ACL/L2CAP.
    CHECK(signal(handle,L2CAP_CONNECT_REQ,1,{0x13,0,0x40,0}));auto reply=acl();
    CHECK(reply.size()>=20 && reply[8]==L2CAP_CONNECT_RSP && !reply[16]);
    const u16 cid=u16(reply[12]|reply[13]<<8);
    CHECK(signal(handle,L2CAP_CONFIG_REQ,2,{u8(cid),u8(cid>>8),0,0,1,2,0xff,0}));
    bool request=false,response=false;
    for(unsigned n=0;n<4 && (!request || !response);++n) {
      reply=acl();CHECK(reply.size()>=14);
      if(reply[8]==L2CAP_CONFIG_REQ) {
        request=true;CHECK(signal(handle,L2CAP_CONFIG_RSP,reply[9],{u8(cid),u8(cid>>8),0,0,0,0}));
      } else {CHECK(reply[8]==L2CAP_CONFIG_RSP);response=true;}
    }
    CHECK(request && response);
    CHECK(send_acl(handle,cid,{0xa2,0x12,6,0x30}));reply=acl();
    CHECK(reply.size()>=14 && reply[8]==0xa1 && reply[9]==0x22);
  }
  std::set<unsigned> seen;
  for(unsigned count=0;count<4;++count) {
    cpu.cycles+=729000000u/Wiimote::UPDATE_FREQ;const auto report=acl();
    CHECK(report.size()==12 && report[8]==0xa1 && report[9]==0x30);
    const unsigned index=HCI_CON_HANDLE(u16(report[0]|report[1]<<8))-0x100;
    CHECK(index<4 && report[10]==(1u<<index) && !report[11]);seen.insert(index);
  }
  CHECK(seen.size()==4); // Continuous P1 cannot consume every guest read.
  // Once warm, all four due reports can drain without another 5ms wait each.
  cpu.cycles+=729000000u/Wiimote::UPDATE_FREQ;seen.clear();
  for(unsigned count=0;count<4;++count) {
    const auto report=acl();CHECK(report.size()==12 && report[9]==0x30);
    seen.insert(HCI_CON_HANDLE(u16(report[0]|report[1]<<8))-0x100);
  }
  CHECK(seen.size()==4);
  // No guest bulk reads: do not accumulate stale held-button samples.
  for(unsigned n=0;n<8;++n) {
    cpu.cycles+=729000000u/Wiimote::UPDATE_FREQ;u32 message;s32 result;bool ready;
    CHECK(wii_bluetooth_poll(bt,&message,&result,&ready) && !ready);
  }
  inputs[2].buttons=0x10;CHECK(wii_bluetooth_set_input_indexed(bt,2,&inputs[2]));
  bool latest=false;
  for(unsigned n=0;n<4;++n) {
    const auto report=acl();CHECK(report.size()==12 && report[9]==0x30);
    const unsigned index=HCI_CON_HANDLE(u16(report[0]|report[1]<<8))-0x100;
    if(index==2){CHECK(report[10]==0x10);latest=true;}
  }
  CHECK(latest);wii_bluetooth_destroy(bt);cpu_free(&cpu);return 0;
}
