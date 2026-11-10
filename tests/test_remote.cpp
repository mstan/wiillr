// SPDX-License-Identifier: MIT
#include "Remote.h"
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
  WiimoteCommon::NativeRemote remote;
  std::vector<std::vector<u8>> replies;
  remote.SetInterruptCallback([&](u8 kind,const u8* data,u32 size){if(kind==0xa1)replies.emplace_back(data,data+size);});
  auto send=[&](std::initializer_list<u8> data){remote.InterruptDataOutput(data.begin(),u32(data.size()));};
  send({0x15,0});CHECK(replies.back()==std::vector<u8>({0x20,0,0,0,0,0,0x69}));
  send({0x17,0,0,0,0x16,0,10});
  CHECK(replies.back().size()==22 && replies.back()[0]==0x21 && replies.back()[3]==0x90 && replies.back()[5]==0x16);
  const auto calibration=replies.back();
  CHECK(calibration[6]==0x80 && calibration[10]==0x9a);
  u8 sum=0x55;for(unsigned i=6;i<15;i++)sum+=calibration[i];CHECK(sum==calibration[15]);
  send({0x17,0,0,0x17,0x70,0,16});CHECK(replies.back()[3]==0xf8);
  send({0x16,0,0,1,0,3,0x12,0x34,0x56});CHECK(replies.back()==std::vector<u8>({0x22,0,0,0x16,0}));
  send({0x17,0,0,1,0,0,3});CHECK(replies.back()[6]==0x12 && replies.back()[8]==0x56);
  send({0x17,4,0xa4,0,0,0,6});CHECK(replies.back()[3]==0xf7); // no extension attached
  WiiBluetoothInput input{0x0801,{512,512,616},512,384,false,true};remote.SetInput(input);
  WiimoteEmu::DesiredWiimoteState state;remote.PrepareInput(&state,WiimoteCommon::HIDWiimote::SensorBarState::Disabled);
  send({0x12,6,0x30});remote.Update(state);
  CHECK(replies.back()==std::vector<u8>({0x30,1,8}));
  send({0x12,6,0x31});remote.Update(state);CHECK(replies.back()==std::vector<u8>({0x31,1,8,0x80,0x80,0x9a}));
  send({0x17,0,0,0,0x16,0,32});CHECK(replies.back()[5]==0x16);
  remote.Update(state);CHECK(replies.back()[0]==0x21 && replies.back()[5]==0x26);
  bool rejected=false;try{send({0x12,6,0x3e});}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
  return 0;
}
