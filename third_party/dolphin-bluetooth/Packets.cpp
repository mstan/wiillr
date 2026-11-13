// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "BTEmu.h"
#include <cstdio>
#include <cstdlib>
namespace IOS::HLE {
void BluetoothEmuDevice::SendToDevice(u16 connection_handle, u8* data, u32 size)
{
  WiimoteDevice* wiimote = AccessWiimote(connection_handle);
  if (wiimote == nullptr || connection_handle!=0x100+wiimote->GetNumber() ||
      !wiimote->IsConnected() || !wiimote->IsSourceValid())
    throw std::runtime_error("Unknown Bluetooth connection handle");

  DEBUG_LOG_FMT(IOS_WIIMOTE, "Send ACL Packet to ConnectionHandle {:#06x}", connection_handle);
  IncDataPacket(connection_handle);
  wiimote->ExecuteL2capCmd(data, size);
}

void BluetoothEmuDevice::IncDataPacket(u16 connection_handle)
{
  m_packet_count[GetWiimoteNumberFromConnectionHandle(connection_handle)]++;
}

// Here we send ACL packets to CPU. They will consist of header + data.
// The header is for example 07 00 41 00 which means size 0x0007 and channel 0x0041.
void BluetoothEmuDevice::SendACLPacket(const bdaddr_t& source, const u8* data, u32 size)
{
  if(size>ACL_PKT_SIZE)throw std::runtime_error("Bluetooth ACL packet overflow");
  const u16 connection_handle = GetConnectionHandle(source);

  DEBUG_LOG_FMT(IOS_WIIMOTE, "ACL packet from {:x} ready to send to stack...", connection_handle);

  if (m_acl_endpoint && !m_hci_endpoint && m_event_queue.empty())
  {
    DEBUG_LOG_FMT(IOS_WIIMOTE, "ACL endpoint valid, sending packet to {:08x}",
                  m_acl_endpoint->ios_request.address);

    auto& system = GetSystem();
    auto& memory = system.GetMemory();

    std::vector<u8> bytes(sizeof(hci_acldata_hdr_t)+size);
    auto* header=reinterpret_cast<hci_acldata_hdr_t*>(bytes.data());
    header->con_handle = HCI_MK_CON_HANDLE(connection_handle, HCI_PACKET_START, HCI_POINT2POINT);
    header->length = size;

    // Write the packet to the buffer
    memcpy(reinterpret_cast<u8*>(header) + sizeof(hci_acldata_hdr_t), data, header->length);
    m_acl_endpoint->FillBuffer(bytes.data(),u32(bytes.size()));

    GetEmulationKernel().EnqueueIPCReply(m_acl_endpoint->ios_request,
                                         sizeof(hci_acldata_hdr_t) + size);
    m_acl_endpoint.reset();
  }
  else
  {
    DEBUG_LOG_FMT(IOS_WIIMOTE, "ACL endpoint not currently valid, queuing...");
    m_acl_pool.Store(data, size, connection_handle);
  }
}

// These messages are sent from the Wii Remote to the game, for example RequestConnection()
// or ConnectionComplete().
//
// Our IOS is so efficient that we could fill the buffer immediately
// rather than enqueue it to some other memory and this will do good for StateSave
void BluetoothEmuDevice::AddEventToQueue(const SQueuedEvent& event)
{
  if(m_event_queue.size()>=256)throw std::runtime_error("Bluetooth event queue overflow");
  if(event.size>=3 && event.buffer[0]==0xff && event.buffer[2]==8 && std::getenv("WIILLR_IOS_TRACE"))
    std::fprintf(stderr,"Bluetooth vendor Sync queued cycles=%llu event=%02x subtype=%02x size=%u hci_read=%u scan_enable=%02x\n",
      static_cast<unsigned long long>(m_system.memory.cpu->cycles),unsigned(event.buffer[0]),unsigned(event.buffer[2]),
      unsigned(event.size),unsigned(bool(m_hci_endpoint)),unsigned(m_scan_enable));
  DEBUG_LOG_FMT(IOS_WIIMOTE, "HCI event {:x} completed...",
                ((hci_event_hdr_t*)event.buffer)->event);

  if (m_hci_endpoint)
  {
    if (m_event_queue.empty())  // fast path :)
    {
      DEBUG_LOG_FMT(IOS_WIIMOTE, "HCI endpoint valid, sending packet to {:08x}",
                    m_hci_endpoint->ios_request.address);
      m_hci_endpoint->FillBuffer(event.buffer, event.size);

      // Send a reply to indicate HCI buffer is filled
      GetEmulationKernel().EnqueueIPCReply(m_hci_endpoint->ios_request, event.size);
      if(event.size>=3 && event.buffer[0]==0xff && event.buffer[2]==8 && std::getenv("WIILLR_IOS_TRACE"))
        std::fprintf(stderr,"Bluetooth vendor Sync delivered path=direct cycles=%llu event=%02x subtype=%02x size=%u\n",
          static_cast<unsigned long long>(m_system.memory.cpu->cycles),unsigned(event.buffer[0]),unsigned(event.buffer[2]),unsigned(event.size));
      m_hci_endpoint.reset();
    }
    else  // push new one, pop oldest
    {
      DEBUG_LOG_FMT(IOS_WIIMOTE, "HCI endpoint not currently valid, queueing ({})...",
                    m_event_queue.size());
      m_event_queue.push_back(event);
      const SQueuedEvent& queued_event = m_event_queue.front();
      DEBUG_LOG_FMT(IOS_WIIMOTE,
                    "HCI event {:x} "
                    "being written from queue ({}) to {:08x}...",
                    ((hci_event_hdr_t*)queued_event.buffer)->event, m_event_queue.size() - 1,
                    m_hci_endpoint->ios_request.address);
      m_hci_endpoint->FillBuffer(queued_event.buffer, queued_event.size);

      // Send a reply to indicate HCI buffer is filled
      GetEmulationKernel().EnqueueIPCReply(m_hci_endpoint->ios_request, queued_event.size);
      if(queued_event.size>=3 && queued_event.buffer[0]==0xff && queued_event.buffer[2]==8 && std::getenv("WIILLR_IOS_TRACE"))
        std::fprintf(stderr,"Bluetooth vendor Sync delivered path=queued cycles=%llu event=%02x subtype=%02x size=%u\n",
          static_cast<unsigned long long>(m_system.memory.cpu->cycles),unsigned(queued_event.buffer[0]),unsigned(queued_event.buffer[2]),unsigned(queued_event.size));
      m_hci_endpoint.reset();
      m_event_queue.pop_front();
    }
  }
  else
  {
    DEBUG_LOG_FMT(IOS_WIIMOTE, "HCI endpoint not currently valid, queuing ({})...",
                  m_event_queue.size());
    m_event_queue.push_back(event);
  }
}

void BluetoothEmuDevice::ACLPool::Store(const u8* data, const u16 size, const u16 conn_handle)
{
  if (m_queue.size() >= 100)
  {
    // Many simultaneous exchanges of ACL packets tend to cause the queue to fill up.
    ERROR_LOG_FMT(IOS_WIIMOTE, "ACL queue size reached 100 - current packet will be dropped!");
    throw std::runtime_error("Bluetooth ACL queue overflow");
  }

  DEBUG_ASSERT_MSG(IOS_WIIMOTE, size < ACL_PKT_SIZE, "ACL packet too large for pool");

  m_queue.push_back(Packet());
  auto& packet = m_queue.back();

  std::copy_n(data, size, packet.data);
  packet.size = size;
  packet.conn_handle = conn_handle;
}

void BluetoothEmuDevice::ACLPool::WriteToEndpoint(const USB::V0BulkMessage& endpoint)
{
  auto& packet = m_queue.front();

  const u8* const data = packet.data;
  const u16 size = packet.size;
  const u16 conn_handle = packet.conn_handle;

  DEBUG_LOG_FMT(IOS_WIIMOTE, "ACL packet being written from queue to {:08x}",
                endpoint.ios_request.address);

  auto& system = m_ios.GetSystem();
  auto& memory = system.GetMemory();

  std::vector<u8> bytes(sizeof(hci_acldata_hdr_t)+size);
  auto* header=reinterpret_cast<hci_acldata_hdr_t*>(bytes.data());
  header->con_handle = HCI_MK_CON_HANDLE(conn_handle, HCI_PACKET_START, HCI_POINT2POINT);
  header->length = size;

  // Write the packet to the buffer
  std::copy_n(data, size, (u8*)header + sizeof(hci_acldata_hdr_t));
  endpoint.FillBuffer(bytes.data(),u32(bytes.size()));

  m_queue.pop_front();

  m_ios.EnqueueIPCReply(endpoint.ios_request, sizeof(hci_acldata_hdr_t) + size);
}
}  // namespace IOS::HLE
