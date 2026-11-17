// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Remote.h"
#include <cstdlib>
namespace WiimoteCommon {
NativeRemote::NativeRemote() {
  const u16 lx=0x7f,ly=0x5d,hx=0x380,hy=0x2a2;
  std::array<u8,11> calibration{u8(lx),u8(ly),u8(((ly&0x300)>>2)|((lx&0x300)>>4)|((ly&0x300)>>6)|(hx>>8)),
    u8(hx),u8(ly),u8(hx),u8(hy),u8(((hy&0x300)>>2)|((hx&0x300)>>4)|((hy&0x300)>>6)|(lx>>8)),u8(lx),u8(hy),0};
  auto checksum=[](auto& bytes){u8 sum=0x55;for(size_t n=0;n+1<bytes.size();n++)sum+=bytes[n];bytes.back()=sum;};
  checksum(calibration);std::copy(calibration.begin(),calibration.end(),eeprom.begin());
  std::copy(calibration.begin(),calibration.end(),eeprom.begin()+11);
  std::array<u8,10> accel{0x80,0x80,0x80,0,0x9a,0x9a,0x9a,0,0,0};checksum(accel);
  std::copy(accel.begin(),accel.end(),eeprom.begin()+0x16);std::copy(accel.begin(),accel.end(),eeprom.begin()+0x20);
  EventLinked();
}
void NativeRemote::EventLinked() {
  mode=0x30;leds=0;continuous=false;first_report=true;ir=ir_clock=speaker=mute=rumble=false;
  read_size=0;last_buttons=0;camera.fill(0);speaker_registers.fill(0);
  extension_attached=false;ResetNunchuk();
}
void NativeRemote::ResetNunchuk() {
  // Physical Nunchuk identity/calibration, following Dolphin's Nunchuk.cpp.
  extension.fill(0);extension_key={};extension_key_dirty=true;
  const u8 id[]={0,0,0xa4,0x20,0,0};std::copy_n(id,6,extension.data()+0xfa);
  const u8 calibration[]={0x80,0x80,0x80,0,0xb3,0xb3,0xb3,0,0xe0,0x20,0x80,0xe0,0x20,0x80};
  std::copy_n(calibration,14,extension.data()+0x20);
  u8 sum=0x55;for(auto byte:calibration)sum+=byte;
  extension[0x2e]=sum;extension[0x2f]=u8(sum+0x55);
}
void NativeRemote::Status() {
  const u8 reply[]={0x20,u8(state.buttons.hex),u8(state.buttons.hex>>8),
    u8((leds<<4)|(input.nunchuk.attached?2:0)|(speaker?4:0)|(ir?8:0)),0,0,0x69};
  Emit(reply,sizeof reply);
}
u8 NativeRemote::ReadNunchuk(u16 address,u8* data,u16 size) {
  if(u32(address)+size>extension.size())return 8;
    if(address>=0x20 && std::getenv("WIILLR_IOS_TRACE"))std::fprintf(stderr,
      "Nunchuk remote=%u read=%04x size=%u encrypted=%u\n",unsigned(device_index),address,size,unsigned(extension[0xf0]==0xaa));
  const auto& nc=input.nunchuk;
  extension[0]=nc.stick_x;extension[1]=nc.stick_y;
  for(unsigned n=0;n<3;++n)extension[2+n]=u8(nc.accel[n]>>2);
  extension[5]=u8((~nc.buttons&3)|((nc.accel[0]&3)<<2)|((nc.accel[1]&3)<<4)|((nc.accel[2]&3)<<6));
  std::copy_n(extension.data()+address,size,data);
  if(extension[0xf0]==0xaa) {
    if(extension_key_dirty) {
      WiimoteEmu::KeyGen::ExtKeyData key;std::copy_n(extension.data()+0x40,key.size(),key.data());
      extension_key=WiimoteEmu::KeyGen1stParty().GenerateFromExtensionKeyData(key);extension_key_dirty=false;
    }
    extension_key.Encrypt(data,address,size);
  }
  return 0;
}
void NativeRemote::PrepareInput(WiimoteEmu::DesiredWiimoteState* target,SensorBarState bar) {
  target->buttons.hex=input.buttons&ButtonData::BUTTON_MASK;
  std::copy(std::begin(input.accel),std::end(input.accel),target->accel.begin());
  target->pointer_x=input.pointer_x;target->pointer_y=input.pointer_y;
  target->pointing=input.pointing && bar==SensorBarState::Enabled;
}
void NativeRemote::Ack(u8 report,u8 error) {
  const u8 bytes[]={0x22,u8(state.buttons.hex),u8(state.buttons.hex>>8),report,error};Emit(bytes,sizeof bytes);
}
u8 NativeRemote::Read(u8 space,u8 slave,u16 address,u8* data,u16 size) {
  if(!space){if(u32(address)+size>eeprom.size())return 8;std::copy_n(eeprom.data()+address,size,data);return 0;}
  if(space!=1 && space!=2)return 6;
  if(slave==0x50)return 8;
  if(slave==0x58 && ir){for(u16 n=0;n<size;n++)data[n]=camera[u8(address+n)];return 0;}
  if(slave==0x51){for(u16 n=0;n<size;n++)data[n]=speaker_registers[u8(address+n)];return 0;}
  if(slave==0x52 && input.nunchuk.attached)return ReadNunchuk(address,data,size);
  return 7; // actual I2C NACK: no extension/MotionPlus device attached
}
u8 NativeRemote::Write(u8 space,u8 slave,u16 address,const u8* data,u16 size) {
  if(!space){if(u32(address)+size>eeprom.size())return 8;std::copy_n(data,size,eeprom.data()+address);return 0;}
  if(space!=1 && space!=2)return 6;
  if(slave==0x50)return 8;
  if(slave==0x58 && ir){for(u16 n=0;n<size;n++)camera[u8(address+n)]=data[n];return 0;}
  if(slave==0x51 && u8(address)!=0){for(u16 n=0;n<size;n++)speaker_registers[u8(address+n)]=data[n];return 0;}
  if(slave==0x51)throw std::runtime_error("Wiimote speaker audio is not implemented");
  if(slave==0x52 && input.nunchuk.attached) {
    if(u32(address)+size>extension.size())return 8;
    std::copy_n(data,size,extension.data()+address);
    if(address<0x50 && u32(address)+size>0x40)extension_key_dirty=true;
    return 0;
  }
  return 7;
}
bool NativeRemote::ReadReply() {
  if(!read_size)return false;
  std::array<u8,22> reply{0x21,u8(state.buttons.hex),u8(state.buttons.hex>>8),0,u8(read_address>>8),u8(read_address)};
  const u16 count=std::min<u16>(16,read_size);
  // EEPROM validates the entire outstanding range, as observed on real remotes.
  u8 error=(!read_space && u32(read_address)+read_size>eeprom.size())?8:Read(read_space,read_slave,read_address,reply.data()+6,count);
  if(error){reply[3]=0xf0|error;read_size=0;}
  else{reply[3]=u8((count-1)<<4);read_address+=count;read_size-=count;}
  Emit(reply.data(),u32(reply.size()));return true;
}
void NativeRemote::InterruptDataOutput(const u8* bytes,u32 size) {
  if(size<2)throw std::runtime_error("Truncated Wiimote output report");
  const u8 report=bytes[0],flags=bytes[1];rumble=flags&1;
  switch(report) {
  case 0x10:return;
  case 0x11:leds=flags>>4;if(flags&2)Ack(report);return;
  case 0x12:
    if(size<3)throw std::runtime_error("Truncated Wiimote report-mode request");
    if(bytes[2]<0x30 || bytes[2]>0x37)throw std::runtime_error("Unimplemented Wiimote input mode");
    if(std::getenv("WIILLR_IOS_TRACE"))std::fprintf(stderr,"Wiimote remote=%u report-mode=%02x nunchuk=%u\n",
      unsigned(device_index),bytes[2],unsigned(input.nunchuk.attached));
    mode=bytes[2];continuous=flags&4;first_report=true;if(flags&2)Ack(report);return;
  case 0x13:ir=flags&4;if(flags&2)Ack(report);return;
  case 0x14:speaker=flags&4;if(flags&2)Ack(report);return;
  case 0x19:mute=flags&4;if(flags&2)Ack(report);return;
  case 0x1a:ir_clock=flags&4;if(flags&2)Ack(report);return;
  case 0x15: {
    Status();return;
  }
  case 0x16:
    if(size<6 || !bytes[5] || bytes[5]>16 || size<u32(6+bytes[5]))throw std::runtime_error("Malformed Wiimote memory write");
    Ack(report,Write((flags>>2)&3,bytes[2]>>1,u16(bytes[3]<<8|bytes[4]),bytes+6,bytes[5]));return;
  case 0x17:
    if(size<7)throw std::runtime_error("Truncated Wiimote memory read");
    if(read_size){Ack(report,4);return;}
    read_space=(flags>>2)&3;read_slave=bytes[2]>>1;read_address=u16(bytes[3]<<8|bytes[4]);read_size=u16(bytes[5]<<8|bytes[6]);
    ReadReply();return;
  default:throw std::runtime_error("Unimplemented Wiimote output report");
  }
}
void NativeRemote::Update(const WiimoteEmu::DesiredWiimoteState& desired) {
  state=desired;
  if(ReadReply())return;
  if(extension_attached!=input.nunchuk.attached) {
    extension_attached=input.nunchuk.attached;Status();return;
  }
  if(!continuous && !first_report && last_buttons==state.buttons.hex && mode==0x30)return;
  first_report=false;last_buttons=state.buttons.hex;
  std::array<u8,22> reply{};reply[0]=mode;reply[1]=u8(state.buttons.hex);reply[2]=u8(state.buttons.hex>>8);u32 count=3;
  if(mode==0x31 || mode==0x33 || mode==0x35 || mode==0x37) {
    reply[1]|=u8((state.accel[0]&3)<<5);reply[2]|=u8(((state.accel[1]>>1)&1)<<5)|u8(((state.accel[2]>>1)&1)<<6);
    for(unsigned n=0;n<3;n++)reply[3+n]=u8(state.accel[n]>>2);count=6;
  }
  if(mode==0x33) {
    std::fill(reply.begin()+6,reply.begin()+18,0xff);
    if(state.pointing && ir && ir_clock && camera[0x30]==8 && camera[0x33]==3) {
      for(unsigned n=0;n<2;n++) {
        const int x=std::clamp(int(state.pointer_x)+(n?100:-100),0,1023);const int y=state.pointer_y;
        reply[6+n*3]=u8(x);reply[7+n*3]=u8(y);reply[8+n*3]=u8(((x>>8)&3)<<4|((y>>8)&3)<<6|5);
      }
    }
    count=18;
  }
  if(mode==0x36 || mode==0x37) {
    // Basic IR: two ten-bit points packed into each five-byte group.
    std::fill_n(reply.data()+count,10,0xff);
    if(state.pointing && ir && ir_clock && camera[0x30]==8 && camera[0x33]==1) {
      const int x0=std::clamp(int(state.pointer_x)-100,0,1023),x1=std::clamp(int(state.pointer_x)+100,0,1023);
      const int y=state.pointer_y;
      reply[count]=u8(x0);reply[count+1]=u8(y);
      reply[count+2]=u8(((x0>>8)&3)<<4|((y>>8)&3)<<6|((x1>>8)&3)|((y>>8)&3)<<2);
      reply[count+3]=u8(x1);reply[count+4]=u8(y);
    }
    count+=10;
  }
  if(mode==0x32 || mode>=0x34) {
    const u32 bytes=mode==0x32?8:22-count;
    std::fill_n(reply.data()+count,bytes,0xff);
    if(input.nunchuk.attached && ReadNunchuk(0,reply.data()+count,u16(bytes)))
      throw std::runtime_error("Nunchuk report read failed");
    count+=bytes;
  }
  Emit(reply.data(),count);
}
}
