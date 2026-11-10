// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// wiillr's bounded base Wii Remote report source, adapted from EmuSubroutines.
#pragma once
#include "port.h"
#include "wiillr/bluetooth.h"
namespace WiimoteCommon {
class NativeRemote final:public HIDWiimote {
public:
  NativeRemote();
  void SetInput(const WiiBluetoothInput& value){input=value;}
  void EventLinked() override;
  void EventUnlinked() override{EventLinked();}
  void PrepareInput(WiimoteEmu::DesiredWiimoteState*,SensorBarState) override;
  void Update(const WiimoteEmu::DesiredWiimoteState&) override;
  void InterruptDataOutput(const u8*,u32) override;
  ButtonData GetCurrentlyPressedButtons() override{return {u16(input.buttons&ButtonData::BUTTON_MASK)};}
private:
  void Emit(const u8* data,u32 size){if(callback)callback(0xa1,data,size);}
  void Ack(u8 report,u8 error=0);
  bool ReadReply();
  u8 Read(u8 space,u8 slave,u16 address,u8* data,u16 size);
  u8 Write(u8 space,u8 slave,u16 address,const u8* data,u16 size);
  WiiBluetoothInput input{0,{512,512,616},512,384,false,false};
  WiimoteEmu::DesiredWiimoteState state;
  std::array<u8,0x1700> eeprom{};
  std::array<u8,256> camera{},speaker_registers{};
  u16 read_address=0,read_size=0,last_buttons=0;
  u8 read_space=0,read_slave=0,mode=0x30,leds=0;
  bool continuous=false,first_report=true,ir=false,ir_clock=false,speaker=false,mute=false,rumble=false;
};
}
