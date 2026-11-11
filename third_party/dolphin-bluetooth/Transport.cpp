// SPDX-License-Identifier: GPL-2.0-or-later
// wiillr adaptation of Dolphin's asynchronous Bluetooth transport.
#include "BTEmu.h"
#include "wiillr/bluetooth.h"
#include <cstdlib>

namespace IOS::HLE {
SQueuedEvent::SQueuedEvent(u32 n,u16 handle):size(n),connection_handle(handle) {
  if(n>sizeof buffer)throw std::runtime_error("Bluetooth event overflow");
}
BluetoothEmuDevice::BluetoothEmuDevice(CPUState* cpu,const u32* gpio,const WiiBluetoothStore* backend):m_system(cpu),gpio_out(gpio) {
  if(backend)store=*backend;
  for(u8 n=0;n<MAX_BBMOTES;n++)
    m_wiimotes[n]=std::make_unique<WiimoteDevice>(this,bdaddr_t{0x11,0x02,0x19,0x79,0,n},n);
  ResetController();
  LoadPairings();
}
BluetoothEmuDevice::~BluetoothEmuDevice()=default;
void BluetoothEmuDevice::LoadPairings() {
  if(!store.load)return;
  std::array<u8,512> bytes{};u32 size=0;
  if(!store.load(store.user,bytes.data(),u32(bytes.size()),&size))throw std::runtime_error("Bluetooth EEPROM read failed");
  if(!size)return;
  if(size<9 || std::memcmp(bytes.data(),"WIIBT001",8) || bytes[8]>MAX_BBMOTES || size!=9u+39u*bytes[8])
    throw std::runtime_error("Invalid Bluetooth EEPROM");
  std::map<bdaddr_t,bool> seen;
  for(unsigned n=0;n<bytes[8];n++) {
    const auto* record=bytes.data()+9+39*n;bdaddr_t bd{};std::copy_n(record,6,bd.begin());
    auto* remote=AccessWiimote(bd);
    if(!remote || seen.contains(bd) || !record[6] || (record[6]&~3))throw std::runtime_error("Invalid Bluetooth pairing record");
    seen[bd]=true;WiimoteDevice::LinkKeyType key{};
    if(record[6]&1){std::copy_n(record+7,16,key.begin());remote->SetLinkKey(key);}
    if(record[6]&2){std::copy_n(record+23,16,key.begin());stored_keys[bd]=key;}
  }
}
void BluetoothEmuDevice::SavePairings() {
  if(!store.save)return;
  std::vector<u8> bytes{'W','I','I','B','T','0','0','1',0};
  for(const auto& remote:m_wiimotes) {
    const auto at=stored_keys.find(remote->GetBD());
    if(!remote->HasLinkKey() && at==stored_keys.end())continue;
    ++bytes[8];const auto offset=bytes.size();bytes.resize(offset+39);
    std::copy(remote->GetBD().begin(),remote->GetBD().end(),bytes.begin()+offset);
    bytes[offset+6]=(remote->HasLinkKey()?1:0)|(at!=stored_keys.end()?2:0);
    if(remote->HasLinkKey())std::copy(remote->GetLinkKey().begin(),remote->GetLinkKey().end(),bytes.begin()+offset+7);
    if(at!=stored_keys.end())std::copy(at->second.begin(),at->second.end(),bytes.begin()+offset+23);
  }
  if(!store.save(store.user,bytes.data(),u32(bytes.size())))throw std::runtime_error("Bluetooth EEPROM write failed");
}
void BluetoothEmuDevice::SetInput(const WiiBluetoothInput& input) {
  if(input.buttons&~WiimoteCommon::ButtonData::BUTTON_MASK || input.pointer_x>1023 || input.pointer_y>767 ||
     std::ranges::any_of(input.accel,[](u16 v){return v>1023;}))throw std::runtime_error("Invalid virtual remote input");
  if(!input_source && input.present)input_source=std::make_unique<WiimoteCommon::NativeRemote>();
  if(input_source)input_source->SetInput(input);
  auto* current=m_wiimotes[0].get();
  current->SetPairing(input.pairing);
  if(input.present && !current->IsSourceValid())current->SetSource(input_source.get());
  if(!input.present && current->IsSourceValid())current->SetSource(nullptr);
}
void BluetoothEmuDevice::PressSync() {
  SQueuedEvent event(3,0);
  event.buffer[0]=0xff;event.buffer[1]=1;event.buffer[2]=8;
  AddEventToQueue(event);
}
void BluetoothEmuDevice::Close() {
  // Release the USB reads that still have no result. Completions already
  // posted to IOS keep their order, and the controller keeps its pairings.
  m_hci_endpoint.reset();
  m_acl_endpoint.reset();
  m_event_queue.clear();
  m_acl_pool.Clear();
  m_scan_enable = 0;
  m_last_ticks = 0;
  std::fill(std::begin(m_packet_count), std::end(m_packet_count), 0);
}
void BluetoothEmuDevice::ResetController() {
  // HCI reset does not cancel pending USB endpoint transfers. In particular,
  // the first ACL receive is submitted before reset by the original SDK.
  m_scan_enable=0;m_event_queue.clear();m_acl_pool.Clear();link_settings.clear();
  std::fill(std::begin(m_packet_count),std::end(m_packet_count),0);
  for(auto& remote:m_wiimotes)remote->Reset();
  controller_config={{HCI_CMD_WRITE_UNIT_CLASS,{0,0,0}},
    {HCI_CMD_WRITE_LOCAL_NAME,std::vector<u8>(248)},
    {HCI_CMD_WRITE_PIN_TYPE,{0}},{HCI_CMD_WRITE_PAGE_TIMEOUT,{0,0x20}},
    {HCI_CMD_WRITE_SCAN_ENABLE,{0}},{HCI_CMD_WRITE_INQUIRY_MODE,{0}},
    {HCI_CMD_WRITE_PAGE_SCAN_TYPE,{0}},{HCI_CMD_WRITE_INQUIRY_SCAN_TYPE,{0}}};
}
bool BluetoothEmuDevice::ReadConfiguration(u16 opcode,u32 parameters) {
  if(opcode==HCI_CMD_READ_LINK_POLICY_SETTINGS || opcode==HCI_CMD_READ_LINK_SUPERVISION_TIMEOUT) {
    const auto* input=m_system.memory.GetPointerForRange(parameters,2);
    const u16 handle=u16(input[0]|input[1]<<8);
    const auto& link=Link(handle);
    const u16 value=opcode==HCI_CMD_READ_LINK_POLICY_SETTINGS?link.policy:link.supervision_timeout;
    const u8 reply[]={0,u8(handle),u8(handle>>8),u8(value),u8(value>>8)};
    SendEventCommandComplete(opcode,reply,sizeof reply);return true;
  }
  const std::map<u16,u16> reads={{0x0c23,HCI_CMD_WRITE_UNIT_CLASS},{0x0c14,HCI_CMD_WRITE_LOCAL_NAME},
    {0x0c09,HCI_CMD_WRITE_PIN_TYPE},{0x0c17,HCI_CMD_WRITE_PAGE_TIMEOUT},{0x0c19,HCI_CMD_WRITE_SCAN_ENABLE},
    {0x0c42,HCI_CMD_WRITE_INQUIRY_SCAN_TYPE},{0x0c44,HCI_CMD_WRITE_INQUIRY_MODE},{0x0c46,HCI_CMD_WRITE_PAGE_SCAN_TYPE}};
  const auto at=reads.find(opcode);if(at==reads.end())return false;
  std::vector<u8> reply{0};const auto& value=controller_config.at(at->second);
  reply.insert(reply.end(),value.begin(),value.end());SendEventCommandComplete(opcode,reply.data(),u32(reply.size()));return true;
}
void BluetoothEmuDevice::ValidateCommand(const USB::V0CtrlMessage& command) {
  auto& mem=m_system.memory;
  const auto* data=mem.GetPointerForRange(command.data_address,command.length);
  if(command.length<3 || command.length!=u32(data[2])+3)throw std::runtime_error("Malformed HCI command");
  const u16 opcode=u16(data[0]|data[1]<<8);u32 parameters=0;
  if(std::getenv("WIILLR_IOS_TRACE"))std::fprintf(stderr,"Bluetooth HCI opcode=%04x parameters=%u\n",opcode,data[2]);
  switch(opcode) {
  case HCI_CMD_RESET:case HCI_CMD_READ_BUFFER_SIZE:case HCI_CMD_READ_LOCAL_VER:
  case HCI_CMD_READ_LOCAL_FEATURES:case HCI_CMD_READ_BDADDR:break;
  case HCI_CMD_WRITE_SCAN_ENABLE:
    parameters=1;if(command.length!=4 || data[3]>3)throw std::runtime_error("Unsupported HCI scan mode");break;
  case HCI_CMD_WRITE_UNIT_CLASS:parameters=3;break;
  case HCI_CMD_WRITE_LOCAL_NAME:parameters=248;break;
  case HCI_CMD_WRITE_PAGE_TIMEOUT:parameters=2;break;
  case HCI_CMD_WRITE_PIN_TYPE:case HCI_CMD_WRITE_PAGE_SCAN_TYPE:case HCI_CMD_WRITE_INQUIRY_SCAN_TYPE:
    parameters=1;if(command.length!=4 || data[3]>1)throw std::runtime_error("Unsupported HCI configuration");break;
  case HCI_CMD_WRITE_INQUIRY_MODE:
    parameters=1;if(command.length!=4 || data[3]>2)throw std::runtime_error("Unsupported inquiry mode");break;
  case HCI_CMD_HOST_BUFFER_SIZE:parameters=7;break;
  case HCI_CMD_SET_EVENT_FILTER:
    parameters=data[2];
    if(!((parameters==1 && data[3]==0) || (parameters==2 && data[3]==1 && data[4]==0)))
      throw std::runtime_error("Unimplemented HCI event filter");break;
  case HCI_CMD_READ_STORED_LINK_KEY:case HCI_CMD_DELETE_STORED_LINK_KEY:parameters=7;break;
  case HCI_CMD_ACCEPT_CON:parameters=7;break;
  case HCI_CMD_INQUIRY:
    parameters=5;
    if(command.length!=8 || data[3]>0x3f || data[4]!=0x8b || data[5]!=0x9e || !data[6] || data[6]>0x30)
      throw std::runtime_error("Unsupported Bluetooth inquiry");break;
  case HCI_CMD_INQUIRY_CANCEL:break;
  case HCI_CMD_CREATE_CON:parameters=13;break;
  case HCI_CMD_REMOTE_NAME_REQ:parameters=10;break;
  case HCI_CMD_DISCONNECT:parameters=3;break;
  case HCI_CMD_READ_REMOTE_FEATURES:case HCI_CMD_READ_REMOTE_VER_INFO:case HCI_CMD_READ_CLOCK_OFFSET:parameters=2;break;
  case HCI_CMD_CHANGE_CON_PACKET_TYPE:parameters=4;break;
  case HCI_CMD_WRITE_LINK_POLICY_SETTINGS:parameters=4;break;
  case HCI_CMD_READ_LINK_POLICY_SETTINGS:parameters=2;break;
  case HCI_CMD_WRITE_LINK_SUPERVISION_TIMEOUT:parameters=4;break;
  case HCI_CMD_READ_LINK_SUPERVISION_TIMEOUT:parameters=2;break;
  case HCI_CMD_AUTH_REQ:parameters=2;break;
  case HCI_CMD_LINK_KEY_REP:parameters=22;break;
  case HCI_CMD_LINK_KEY_NEG_REP:case HCI_CMD_PIN_CODE_NEG_REP:parameters=6;break;
  case HCI_CMD_PIN_CODE_REP:parameters=23;break;
  case HCI_CMD_WRITE_STORED_LINK_KEY:
    if(command.length<4 || !data[3] || data[3]>MAX_BBMOTES)throw std::runtime_error("Invalid Bluetooth stored-key count");
    parameters=1+22*data[3];break;
  case HCI_CMD_SNIFF_MODE:parameters=10;break;
  case 0x0c23:case 0x0c14:case 0x0c09:case 0x0c17:case 0x0c19:case 0x0c42:case 0x0c44:case 0x0c46:break;
  default:throw std::runtime_error("Unimplemented Bluetooth HCI command");
  }
  if(data[2]!=parameters)throw std::runtime_error("Wrong HCI parameter length");
  if((opcode&0xfc00)==0x0c00 && parameters && opcode!=HCI_CMD_READ_STORED_LINK_KEY && opcode!=HCI_CMD_DELETE_STORED_LINK_KEY)
    controller_config[opcode]=std::vector<u8>(data+3,data+command.length);
}
BluetoothEmuDevice::LinkSettings& BluetoothEmuDevice::Link(u16 handle) {
  auto* remote=AccessWiimote(handle);
  if(!remote || handle!=0x100+remote->GetNumber() || !remote->IsConnected())
    throw std::runtime_error("Unknown Bluetooth connection handle");
  return link_settings[handle];
}
void BluetoothEmuDevice::Submit(u32 message) {
  auto& mem=m_system.memory;
  const auto* request=mem.GetPointerForRange(message,32);
  const u32 operation=Common::swap32(request+12),ni=Common::swap32(request+16),no=Common::swap32(request+20);
  const u32 vector_address=Common::swap32(request+24);
  if(Common::swap32(request)!=7 || ni>6 || no!=1)throw std::runtime_error("Unsupported Bluetooth USB request");
  const auto* vectors=mem.GetPointerForRange(vector_address,(ni+no)*8);
  auto address=[&](u32 i){return Common::swap32(vectors+i*8);};
  auto size=[&](u32 i){return Common::swap32(vectors+i*8+4);};
  for(u32 i=0;i<ni+no;i++)if(size(i))mem.GetPointerForRange(address(i),size(i));
  if(std::getenv("WIILLR_IOS_TRACE"))std::fprintf(stderr,"Bluetooth USB operation=%u inputs=%u outputs=%u\n",operation,ni,no);
  if(operation==0) {
    if((ni!=5 && ni!=6) || size(0)!=1 || size(1)!=1 || size(2)!=2 || size(3)!=2 || size(4)!=2)
      throw std::runtime_error("Malformed USB control vectors");
    if(ni==6 && (size(5)!=1 || mem.Read_U8(address(5))!=0))throw std::runtime_error("Unsupported USB control flags");
    const u32 length=Common::swap16(mem.Read_U16(address(4)));
    if(mem.Read_U8(address(0))!=0x20 || mem.Read_U8(address(1))!=0 ||
       mem.Read_U16(address(2)) || mem.Read_U16(address(3)) || length!=size(ni))
      throw std::runtime_error("Unsupported USB control setup");
    ExecuteHCICommandMessage(USB::V0CtrlMessage(m_kernel,{message},address(ni),length));return;
  }
  if((operation==1 || operation==2) && ni==2 && size(0)==1 && size(1)==2) {
    const u8 endpoint=mem.Read_U8(address(0));const u32 length=mem.Read_U16(address(1));
    if(length!=size(ni))throw std::runtime_error("Malformed USB endpoint length");
    USB::V0Message endpoint_request(m_kernel,{message},address(ni),length);endpoint_request.endpoint=endpoint;
    if(operation==2 && endpoint==HCI_EVENT) {
      if(m_hci_endpoint)throw std::runtime_error("Duplicate Bluetooth event endpoint");
      m_hci_endpoint=std::make_unique<USB::V0IntrMessage>(endpoint_request);return;
    }
    if(operation==1 && endpoint==ACL_DATA_IN) {
      if(m_acl_endpoint)throw std::runtime_error("Duplicate Bluetooth ACL endpoint");
      m_acl_endpoint=std::make_unique<USB::V0BulkMessage>(endpoint_request);return;
    }
    if(operation==1 && endpoint==ACL_DATA_OUT) {
      auto* data=mem.GetPointerForRange(address(ni),length);
      if(length<8)throw std::runtime_error("Truncated Bluetooth ACL packet");
      const u16 handle=u16(data[0]|data[1]<<8),bytes=u16(data[2]|data[3]<<8);
      const u16 l2bytes=u16(data[4]|data[5]<<8);
      if(bytes+4!=length || l2bytes+4!=bytes || HCI_BC_FLAG(handle)!=HCI_POINT2POINT ||
         HCI_PB_FLAG(handle)!=HCI_PACKET_START)throw std::runtime_error("Unsupported ACL fragmentation");
      SendToDevice(HCI_CON_HANDLE(handle),data+4,bytes);
      m_kernel.EnqueueIPCReply({message},length);return;
    }
  }
  throw std::runtime_error("Unimplemented Bluetooth USB endpoint");
}
void BluetoothEmuDevice::Update() {
  if(!m_event_queue.empty() && m_hci_endpoint) {
    const auto& event=m_event_queue.front();m_hci_endpoint->FillBuffer(event.buffer,event.size);
    m_kernel.EnqueueIPCReply(m_hci_endpoint->ios_request,event.size);
    m_hci_endpoint.reset();m_event_queue.pop_front();
  }
  if(!m_acl_pool.IsEmpty() && m_acl_endpoint && m_event_queue.empty()) {
    m_acl_pool.WriteToEndpoint(*m_acl_endpoint);m_acl_endpoint.reset();
  }
  for(auto& remote:m_wiimotes)remote->Update();
  const auto now=m_system.memory.cpu->cycles;
  if(now-m_last_ticks>=729000000u/Wiimote::UPDATE_FREQ) {
    m_last_ticks=now;
    for(auto& remote:m_wiimotes) {
      if(remote->IsConnected()) {
        // The guest can stop posting bulk reads while it changes scenes. Do
        // not accumulate stale samples or advance a multi-packet HID read
        // until there is room to deliver its next report. HCI events and
        // solicited responses still drain ahead of the next sample.
        if(!m_acl_endpoint || !m_event_queue.empty() || !m_acl_pool.IsEmpty())continue;
        auto& link=Link(GetConnectionHandle(remote->GetBD()));
        if(link.mode==2 && now-link.last_input<u64(link.sniff_interval)*455625u)continue;
        link.last_input=now;
      }
      WiimoteEmu::DesiredWiimoteState state;
      const auto next=remote->PrepareInput(&state);remote->UpdateInput(next,state);
    }
    if(std::ranges::any_of(m_packet_count,[](u32 n){return n!=0;}))SendEventNumberOfCompletedPackets();
  }
}
u64 BluetoothEmuDevice::Deadline(u64 cycles) const {
  u64 next=m_last_ticks+729000000u/Wiimote::UPDATE_FREQ;
  if(!m_kernel.completions.empty())next=std::min(next,m_kernel.completions.front().ready);
  if((!m_event_queue.empty() && m_hci_endpoint) ||
     (!m_acl_pool.IsEmpty() && m_acl_endpoint && m_event_queue.empty()))return 1;
  return next>cycles?next-cycles:1;
}
bool BluetoothEmuDevice::Poll(u32* message,s32* result) {
  Update();
  if(m_kernel.completions.empty() || m_kernel.completions.front().ready>m_system.memory.cpu->cycles)return false;
  const auto completion=m_kernel.completions.front();m_kernel.completions.pop_front();
  *message=completion.message;*result=completion.result;return true;
}
}
struct WiiBluetooth {WiiBluetooth(CPUState* cpu,const u32* gpio,const WiiBluetoothStore* store):device(cpu,gpio,store){} IOS::HLE::BluetoothEmuDevice device;};
extern "C" WiiBluetooth* wii_bluetooth_create(CPUState* cpu,const u32* gpio,const WiiBluetoothStore* store) {
  try{return new WiiBluetooth(cpu,gpio,store);}catch(const std::exception& e){std::fprintf(stderr,"Bluetooth init: %s\n",e.what());return nullptr;}
}
extern "C" void wii_bluetooth_destroy(WiiBluetooth* bt){delete bt;}
extern "C" void wii_bluetooth_close(WiiBluetooth* bt){if(bt)bt->device.Close();}
extern "C" bool wii_bluetooth_set_input(WiiBluetooth* bt,const WiiBluetoothInput* input) {
  try{if(!bt || !input)return false;bt->device.SetInput(*input);return true;}
  catch(const std::exception& e){std::fprintf(stderr,"Bluetooth input: %s\n",e.what());return false;}
}
extern "C" bool wii_bluetooth_press_sync(WiiBluetooth* bt) {
  try{if(!bt)return false;bt->device.PressSync();return true;}
  catch(const std::exception& e){std::fprintf(stderr,"Bluetooth Sync: %s\n",e.what());return false;}
}
extern "C" bool wii_bluetooth_submit(WiiBluetooth* bt,u32 message) {
  try{bt->device.Submit(message);return true;}catch(const std::exception& e){std::fprintf(stderr,"Bluetooth stopped: %s\n",e.what());return false;}
}
extern "C" bool wii_bluetooth_poll(WiiBluetooth* bt,u32* message,s32* result,bool* ready) {
  try{*ready=bt->device.Poll(message,result);return true;}catch(const std::exception& e){std::fprintf(stderr,"Bluetooth update stopped: %s\n",e.what());return false;}
}
extern "C" u64 wii_bluetooth_deadline(const WiiBluetooth* bt,u64 cycles) {
  return bt?bt->device.Deadline(cycles):UINT64_MAX;
}
