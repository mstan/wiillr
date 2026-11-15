// SPDX-License-Identifier: MIT
#include "BTEmu.h"
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
int main() {
  CPUState cpu;CHECK(cpu_init(&cpu) && cpu_alloc_mem2(&cpu,0x4000000));cpu.spr[287]=PPC_BROADWAY_PVR;
  std::vector<u8> eeprom;
  WiiBluetoothStore store{&eeprom,
    [](void* user,u8* data,u32 capacity,u32* size) {const auto& saved=*static_cast<std::vector<u8>*>(user);if(saved.size()>capacity)return false;
      *size=u32(saved.size());std::copy(saved.begin(),saved.end(),data);return true;},
    [](void* user,const u8* data,u32 size) {*static_cast<std::vector<u8>*>(user)=std::vector<u8>(data,data+size);return true;}};
  IOS::HLE::WiimoteDevice::LinkKeyType saved_key{};
  CHECK(!wii_bluetooth_pair_first_remote(nullptr));
  auto* api=wii_bluetooth_create(&cpu,nullptr,nullptr);CHECK(api && wii_bluetooth_pair_first_remote(api));
  wii_bluetooth_destroy(api);
  {
    IOS::HLE::BluetoothEmuDevice bt(&cpu,nullptr,&store);
    auto put=[&](u32 p,u32 v){mem_write32_cia(&cpu,p,v,0);};
    auto vector=[&](u32 base,u32 n,u32 p,u32 size){put(base+n*8,p);put(base+n*8+4,size);};
    auto command=[&](u16 op,std::vector<u8> data) {
      const u32 size=u32(data.size())+3;
      put(0x2000,7);put(0x200c,0);put(0x2010,6);put(0x2014,1);put(0x2018,0x2100);
      const u32 sizes[]={1,1,2,2,2,1};for(u32 i=0;i<6;i++)vector(0x2100,i,0x2200+i*4,sizes[i]);
      cpu.ram[0x2200]=0x20;cpu.ram[0x2210]=u8(size);cpu.ram[0x2211]=u8(size>>8);
      vector(0x2100,6,0x2300,size);cpu.ram[0x2300]=u8(op);cpu.ram[0x2301]=u8(op>>8);cpu.ram[0x2302]=u8(data.size());
      std::copy(data.begin(),data.end(),cpu.ram+0x2303);bt.Submit(0x2000);
    };
    auto begin_event=[&]() {
      put(0x3000,7);put(0x300c,2);put(0x3010,2);put(0x3014,1);put(0x3018,0x3100);
      vector(0x3100,0,0x3200,1);vector(0x3100,1,0x3202,2);vector(0x3100,2,0x3300,256);
      cpu.ram[0x3200]=0x81;cpu.ram[0x3202]=1;cpu.ram[0x3203]=0;bt.Submit(0x3000);
    };
    auto poll_event=[&]() {
      u32 message;s32 result;
      for(unsigned attempt=0;attempt<4;attempt++) {
        while(bt.Poll(&message,&result))if(message==0x3000)return std::vector<u8>(cpu.ram+0x3300,cpu.ram+0x3300+result);
        cpu.cycles+=6000;
      }
      return std::vector<u8>{};
    };
    auto event=[&]() {
      begin_event();auto result=poll_event();
      if(result.empty())throw std::runtime_error("Missing Bluetooth test event");
      return result;
    };
    auto* remote=bt.AccessWiimoteByIndex(0);CHECK(!remote->HasLinkKey() && !remote->IsPairing());
    bt.PairFirstRemote();bt.PairFirstRemote();CHECK(remote->IsPairing() && eeprom.empty());
    begin_event();CHECK(poll_event().empty()); // No input source or scan readiness.
    command(HCI_CMD_WRITE_SCAN_ENABLE,{2});CHECK(poll_event()[0]==HCI_EVENT_COMMAND_COMPL);
    begin_event();CHECK(poll_event().empty()); // Scan/read alone cannot pair an absent source.
    WiiBluetoothInput input{0,{512,512,616},512,384,false,true,false};bt.SetInput(input);
    CHECK(remote->IsPairing() && !remote->HasLinkKey() && eeprom.empty());
    const std::vector<u8> sync{0xff,1,8};CHECK(poll_event()==sync);
    bt.SetInput(input);bt.PairFirstRemote();begin_event();CHECK(poll_event().empty()); // One Sync only.
    command(HCI_CMD_RESET,{});CHECK(poll_event()[0]==HCI_EVENT_COMMAND_COMPL);
    begin_event();CHECK(poll_event().empty()); // Reset clears scan readiness and rearms.
    command(HCI_CMD_WRITE_SCAN_ENABLE,{2});CHECK(poll_event()[0]==HCI_EVENT_COMMAND_COMPL);
    CHECK(event()==sync);
    remote->Activate(true);bt.SetInput(input);remote->Update();
    CHECK(event()[0]==HCI_EVENT_CON_REQ); // Repeated pairing input must retain a pending connection.
    CHECK(remote->EventConnectionRequest());CHECK(remote->IsConnected());
    bt.SetInput(input);bt.SetInput(input);CHECK(remote->IsConnected() && remote->IsPairing());
    const std::vector<u8> bd{0x11,2,0x19,0x79,0,0};
    auto start_auth=[&]() {
      command(HCI_CMD_AUTH_REQ,{0,1});auto status=event();auto request=event();
      return status[0]==HCI_EVENT_COMMAND_STATUS && request[0]==HCI_EVENT_LINK_KEY_REQ;
    };
    auto request_pin=[&]() {
      command(HCI_CMD_LINK_KEY_NEG_REP,bd);auto complete=event();auto pin=event();
      return complete[0]==HCI_EVENT_COMMAND_COMPL && pin[0]==HCI_EVENT_PIN_CODE_REQ;
    };
    CHECK(start_auth() && request_pin());
    auto pin=bd;pin.resize(23);pin[6]=6; // wrong all-zero PIN
    command(HCI_CMD_PIN_CODE_REP,pin);CHECK(event()[0]==HCI_EVENT_COMMAND_COMPL);
    auto rejected=event();CHECK(rejected[0]==HCI_EVENT_AUTH_COMPL && rejected[2]==5 && !remote->HasLinkKey());
    CHECK(start_auth() && request_pin());
    const u8 console_pin[]={0x11,2,0x19,0x79,0,0xff};std::copy(std::begin(console_pin),std::end(console_pin),pin.begin()+7);
    command(HCI_CMD_PIN_CODE_REP,pin);CHECK(event()[0]==HCI_EVENT_COMMAND_COMPL);
    const auto notification=event();CHECK(notification[0]==HCI_EVENT_LINK_KEY_NOTIFICATION && notification[24]==2);
    auto accepted=event();CHECK(accepted[0]==HCI_EVENT_AUTH_COMPL && !accepted[2] && remote->HasLinkKey());
    const auto key=remote->GetLinkKey();CHECK(std::equal(key.begin(),key.end(),notification.begin()+8));
    bt.SetInput(input);CHECK(remote->IsPairing()); // Key notification alone does not finish policy.
    std::vector<u8> write_keys{1};write_keys.insert(write_keys.end(),bd.begin(),bd.end());write_keys.insert(write_keys.end(),key.begin(),key.end());
    auto wrong_keys=write_keys;wrong_keys[7]^=1;
    command(HCI_CMD_WRITE_STORED_LINK_KEY,wrong_keys);CHECK(event()[0]==HCI_EVENT_COMMAND_COMPL);
    bt.SetInput(input);CHECK(remote->IsPairing()); // Require the actual remote key, not any stored key.
    command(HCI_CMD_WRITE_STORED_LINK_KEY,write_keys);CHECK(event()[0]==HCI_EVENT_COMMAND_COMPL);
    bt.SetInput(input);CHECK(!remote->IsPairing() && eeprom.size()==48 && eeprom[15]==3);
    bt.ResetController();CHECK(remote->HasLinkKey() && remote->GetLinkKey()==key);
    input.pairing=false;bt.SetInput(input);CHECK(remote->EventConnectionRequest());
    CHECK(start_auth());auto reply=bd;reply.insert(reply.end(),key.begin(),key.end());reply[6]^=1;
    command(HCI_CMD_LINK_KEY_REP,reply);event();rejected=event();CHECK(rejected[2]==5);
    CHECK(start_auth());reply[6]^=1;command(HCI_CMD_LINK_KEY_REP,reply);event();accepted=event();CHECK(!accepted[2]);
    command(HCI_CMD_WRITE_LINK_POLICY_SETTINGS,{0,1,5,0});CHECK(event()[0]==HCI_EVENT_COMMAND_COMPL);
    command(HCI_CMD_READ_LINK_POLICY_SETTINGS,{0,1});const auto policy=event();CHECK(policy.size()==10 && policy[8]==5 && !policy[9]);
    command(HCI_CMD_WRITE_STORED_LINK_KEY,write_keys);const auto written=event();CHECK(written.size()==7 && !written[5] && written[6]==1);
    command(HCI_CMD_READ_STORED_LINK_KEY,{0,0,0,0,0,0,1});const auto keys=event();CHECK(keys[0]==HCI_EVENT_RETURN_LINK_KEYS && keys[2]==1);
    CHECK(std::equal(key.begin(),key.end(),keys.begin()+9));CHECK(event()[8]==1);saved_key=key;
  }
  {
    IOS::HLE::BluetoothEmuDevice reopened(&cpu,nullptr,&store);
    auto* remote=reopened.AccessWiimoteByIndex(0);CHECK(remote->HasLinkKey() && remote->GetLinkKey()==saved_key);
    reopened.PairFirstRemote();WiiBluetoothInput input{0,{512,512,616},512,384,false,true,false};reopened.SetInput(input);
    CHECK(!remote->IsPairing());
    CHECK(reopened.stored_keys.size()==1 && reopened.stored_keys.at(remote->GetBD())==saved_key);
    reopened.ResetController();CHECK(reopened.stored_keys.size()==1);
  }
  // Either half of previously loaded key state prevents automatic first-use pairing.
  for(u8 flags:{1,2}) {
    const auto saved=eeprom;eeprom[15]=flags;
    IOS::HLE::BluetoothEmuDevice partial(&cpu,nullptr,&store);partial.PairFirstRemote();
    WiiBluetoothInput input{0,{512,512,616},512,384,false,true,false};partial.SetInput(input);
    CHECK(!partial.AccessWiimoteByIndex(0)->IsPairing());eeprom=saved;
  }
  {
    IOS::HLE::BluetoothEmuDevice unpaired(&cpu,nullptr);
    WiiBluetoothInput input{0,{512,512,616},512,384,false,true,false};unpaired.SetInput(input);
    auto* remote=unpaired.AccessWiimoteByIndex(0);CHECK(remote->EventConnectionAccept());remote->Update();
    // A refused incoming HID connection must disconnect, preserving the
    // guest's security decision while leaving a usable machine/provider.
    u8 rejected[]={12,0,1,0,3,2,8,0,0x40,0,0x40,0,3,0,0,0};
    remote->ExecuteL2capCmd(rejected,sizeof rejected);CHECK(!remote->IsConnected());
  }
  {
    // Four indexed host sources produce independent real HID wire reports;
    // assigning them alone must not fabricate four connected controllers.
    IOS::HLE::BluetoothEmuDevice four(&cpu,nullptr);
    std::array<WiiBluetoothInput,4> input;
    std::array<std::vector<u8>,4> reports;
    std::array<unsigned,4> counts{};
    for(unsigned i=0;i<4;++i) {
      input[i]={u16(1u<<i),{u16(512+4*i),512,616},u16(200+140*i),u16(200+60*i),true,true,false};
      four.PairRemote(i);four.SetInput(i,input[i]);
      auto* device=four.AccessWiimoteByIndex(i);
      CHECK(device->IsSourceValid() && !device->IsConnected() && device->IsPairing());
      CHECK(device->GetBD().back()==i);
      auto& source=*four.input_sources[i];
      source.SetInterruptCallback([&,i](u8 kind,const u8* bytes,u32 size) {
        if(kind==0xa1){reports[i].assign(bytes,bytes+size);++counts[i];}
      });
      const u8 ir[]={0x13,4},clock[]={0x1a,4},enable[]={0x16,4,0xb0,0,0x30,1,8},mode[]={0x16,4,0xb0,0,0x33,1,3},report[]={0x12,6,0x33};
      source.InterruptDataOutput(ir,sizeof ir);source.InterruptDataOutput(clock,sizeof clock);
      source.InterruptDataOutput(enable,sizeof enable);source.InterruptDataOutput(mode,sizeof mode);source.InterruptDataOutput(report,sizeof report);
      WiimoteEmu::DesiredWiimoteState desired;
      source.PrepareInput(&desired,WiimoteCommon::HIDWiimote::SensorBarState::Enabled);source.Update(desired);
      CHECK(reports[i].size()==18 && reports[i][0]==0x33 && reports[i][1]==(1u<<i) && !reports[i][2]);
      CHECK(!std::all_of(reports[i].begin()+6,reports[i].end(),[](u8 b){return b==0xff;}));
      if(i)CHECK(!std::equal(reports[i].begin()+6,reports[i].end(),reports[i-1].begin()+6));
    }
    const auto before=counts;input[2].buttons=0;four.SetInput(2,input[2]);
    WiimoteEmu::DesiredWiimoteState desired;
    four.input_sources[2]->PrepareInput(&desired,WiimoteCommon::HIDWiimote::SensorBarState::Enabled);four.input_sources[2]->Update(desired);
    CHECK(!reports[2][1] && !reports[2][2] && counts[2]==before[2]+1);
    CHECK(counts[0]==before[0] && counts[1]==before[1] && counts[3]==before[3]);
    input[1].present=false;four.SetInput(1,input[1]);CHECK(!four.AccessWiimoteByIndex(1)->IsSourceValid());
    CHECK(four.AccessWiimoteByIndex(0)->IsSourceValid() && four.AccessWiimoteByIndex(2)->IsSourceValid() && four.AccessWiimoteByIndex(3)->IsSourceValid());
    bool invalid=false;try{four.SetInput(4,input[0]);}catch(const std::runtime_error&){invalid=true;}CHECK(invalid);
    CHECK(!four.AccessWiimoteByIndex(4)->IsSourceValid()); // Balance board is not Remote5.
    auto* api=wii_bluetooth_create(&cpu,nullptr,nullptr);CHECK(api);
    for(unsigned i=0;i<4;++i)CHECK(wii_bluetooth_pair_remote(api,i) && wii_bluetooth_set_input_indexed(api,i,&input[i]));
    CHECK(!wii_bluetooth_pair_remote(api,4) && !wii_bluetooth_set_input_indexed(api,4,&input[0]));
    CHECK(!wii_bluetooth_set_input_indexed(api,0,nullptr));wii_bluetooth_destroy(api);
  }
  cpu_free(&cpu);return 0;
}
