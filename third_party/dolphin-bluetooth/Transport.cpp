// SPDX-License-Identifier: GPL-2.0-or-later
// wiillr adaptation of Dolphin's asynchronous Bluetooth transport.
#include "BTEmu.h"
#include "wiillr/bluetooth.h"
#include <cstdlib>

namespace IOS::HLE {
SQueuedEvent::SQueuedEvent(u32 n,u16 handle):size(n),connection_handle(handle) {
  if(n>sizeof buffer)throw std::runtime_error("Bluetooth event overflow");
}
BluetoothEmuDevice::BluetoothEmuDevice(CPUState* cpu):m_system(cpu) {
  for(u8 n=0;n<MAX_BBMOTES;n++)
    m_wiimotes[n]=std::make_unique<WiimoteDevice>(this,bdaddr_t{0x11,0x02,0x19,0x79,0,n},n);
  ResetController();
}
BluetoothEmuDevice::~BluetoothEmuDevice()=default;
void BluetoothEmuDevice::ResetController() {
  m_scan_enable=0;m_event_queue.clear();m_acl_pool.Clear();m_acl_endpoint.reset();
  std::fill(std::begin(m_packet_count),std::end(m_packet_count),0);
  for(auto& remote:m_wiimotes)remote->Reset();
  controller_config={{HCI_CMD_WRITE_UNIT_CLASS,{0,0,0}},
    {HCI_CMD_WRITE_LOCAL_NAME,std::vector<u8>(248)},
    {HCI_CMD_WRITE_PIN_TYPE,{0}},{HCI_CMD_WRITE_PAGE_TIMEOUT,{0,0x20}},
    {HCI_CMD_WRITE_SCAN_ENABLE,{0}},{HCI_CMD_WRITE_INQUIRY_MODE,{0}},
    {HCI_CMD_WRITE_PAGE_SCAN_TYPE,{0}},{HCI_CMD_WRITE_INQUIRY_SCAN_TYPE,{0}}};
}
bool BluetoothEmuDevice::ReadConfiguration(u16 opcode) {
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
  case 0x0c23:case 0x0c14:case 0x0c09:case 0x0c17:case 0x0c19:case 0x0c42:case 0x0c44:case 0x0c46:break;
  default:throw std::runtime_error("Unimplemented Bluetooth HCI command");
  }
  if(data[2]!=parameters)throw std::runtime_error("Wrong HCI parameter length");
  if((opcode&0xfc00)==0x0c00 && parameters && opcode!=HCI_CMD_READ_STORED_LINK_KEY && opcode!=HCI_CMD_DELETE_STORED_LINK_KEY)
    controller_config[opcode]=std::vector<u8>(data+3,data+command.length);
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
      WiimoteEmu::DesiredWiimoteState state;
      const auto next=remote->PrepareInput(&state);remote->UpdateInput(next,state);
    }
    if(std::ranges::any_of(m_packet_count,[](u32 n){return n!=0;}))SendEventNumberOfCompletedPackets();
  }
}
bool BluetoothEmuDevice::Poll(u32* message,s32* result) {
  Update();
  if(m_kernel.completions.empty() || m_kernel.completions.front().ready>m_system.memory.cpu->cycles)return false;
  const auto completion=m_kernel.completions.front();m_kernel.completions.pop_front();
  *message=completion.message;*result=completion.result;return true;
}
}
struct WiiBluetooth {explicit WiiBluetooth(CPUState* cpu):device(cpu){} IOS::HLE::BluetoothEmuDevice device;};
extern "C" WiiBluetooth* wii_bluetooth_create(CPUState* cpu) {
  try{return new WiiBluetooth(cpu);}catch(const std::exception& e){std::fprintf(stderr,"Bluetooth init: %s\n",e.what());return nullptr;}
}
extern "C" void wii_bluetooth_destroy(WiiBluetooth* bt){delete bt;}
extern "C" bool wii_bluetooth_submit(WiiBluetooth* bt,u32 message) {
  try{bt->device.Submit(message);return true;}catch(const std::exception& e){std::fprintf(stderr,"Bluetooth stopped: %s\n",e.what());return false;}
}
extern "C" bool wii_bluetooth_poll(WiiBluetooth* bt,u32* message,s32* result,bool* ready) {
  try{*ready=bt->device.Poll(message,result);return true;}catch(const std::exception& e){std::fprintf(stderr,"Bluetooth update stopped: %s\n",e.what());return false;}
}
