// SPDX-License-Identifier: GPL-3.0-or-later
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
  // Real extended IR reports require the sensor bar, both enable reports and
  // camera initialization. The image contains two distinct sensor-bar dots.
  input.pointing=true;remote.SetInput(input);
  remote.PrepareInput(&state,WiimoteCommon::HIDWiimote::SensorBarState::Enabled);
  send({0x13,4});send({0x1a,4});
  send({0x16,4,0xb0,0,0x30,1,8});send({0x16,4,0xb0,0,0x33,1,3});
  send({0x12,6,0x33});remote.Update(state);
  CHECK(replies.back()==std::vector<u8>({0x33,1,8,0x80,0x80,0x9a,0x9c,0x80,0x55,0x64,0x80,0x65,0xff,0xff,0xff,0xff,0xff,0xff}));
  remote.PrepareInput(&state,WiimoteCommon::HIDWiimote::SensorBarState::Disabled);remote.Update(state);
  CHECK(std::all_of(replies.back().begin()+6,replies.back().end(),[](u8 b){return b==0xff;}));
  remote.PrepareInput(&state,WiimoteCommon::HIDWiimote::SensorBarState::Enabled);
  send({0x1a,0});remote.Update(state);
  CHECK(std::all_of(replies.back().begin()+6,replies.back().end(),[](u8 b){return b==0xff;}));
  bool rejected=false;try{send({0x12,6,0x3e});}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
  // Nunchuk hotplug, physical calibration and active-low six-byte payload.
  input.nunchuk=wii_nunchuk_neutral();remote.SetInput(input);remote.Update(state);
  CHECK(replies.back()[0]==0x20 && (replies.back()[3]&2));
  send({0x17,4,0xa4,0,0xfa,0,6});
  CHECK(std::equal(replies.back().begin()+6,replies.back().begin()+12,std::array<u8,6>{0,0,0xa4,0x20,0,0}.begin()));
  send({0x17,4,0xa4,0,0x20,0,16});
  CHECK(replies.back()[6]==0x80 && replies.back()[10]==0xb3 && replies.back()[14]==0xe0 && replies.back()[15]==0x20);
  sum=0x55;for(unsigned n=6;n<20;n++)sum+=replies.back()[n];
  CHECK(replies.back()[20]==sum && replies.back()[21]==u8(sum+0x55));
  // Known first-party zero-key wire identity: FE FE 9A 1E FE FE.
  send({0x16,4,0xa4,0,0xf0,1,0xaa});send({0x17,4,0xa4,0,0xfa,0,6});
  CHECK(std::equal(replies.back().begin()+6,replies.back().begin()+12,std::array<u8,6>{0xfe,0xfe,0x9a,0x1e,0xfe,0xfe}.begin()));
  send({0x16,4,0xa4,0,0xf0,1,0x55});send({0x16,4,0xa4,0,0xfb,1,0});
  input.nunchuk.stick_x=0x31;input.nunchuk.stick_y=0xf3;input.nunchuk.buttons=1;
  input.nunchuk.accel[0]=513;input.nunchuk.accel[1]=514;input.nunchuk.accel[2]=715;
  remote.SetInput(input);send({0x12,6,0x35});remote.Update(state);
  CHECK(replies.back().size()==22 && replies.back()[0]==0x35);
  CHECK(std::equal(replies.back().begin()+6,replies.back().begin()+12,std::array<u8,6>{0x31,0xf3,0x80,0x80,0xb2,0xe6}.begin()));
  send({0x1a,4});send({0x16,4,0xb0,0,0x33,1,1});send({0x12,6,0x37});remote.Update(state);
  CHECK(replies.back().size()==22 && replies.back()[0]==0x37);
  CHECK(std::equal(replies.back().begin()+6,replies.back().begin()+11,std::array<u8,5>{0x9c,0x80,0x56,0x64,0x80}.begin()));
  CHECK(std::equal(replies.back().begin()+16,replies.back().end(),std::array<u8,6>{0x31,0xf3,0x80,0x80,0xb2,0xe6}.begin()));
  input.nunchuk.attached=false;remote.SetInput(input);remote.Update(state);
  CHECK(replies.back()[0]==0x20 && !(replies.back()[3]&2));
  remote.Update(state);CHECK(std::all_of(replies.back().begin()+16,replies.back().end(),[](u8 b){return b==0xff;}));
  return 0;
}
