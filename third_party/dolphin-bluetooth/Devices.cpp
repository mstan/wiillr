// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "BTEmu.h"
namespace IOS::HLE {
bool BluetoothEmuDevice::RemoteConnect(WiimoteDevice& wiimote)
{
  // If page scan is disabled the controller will not see this connection request.
  if (!(m_scan_enable & HCI_PAGE_SCAN_ENABLE))
    return false;

  SendEventRequestConnection(wiimote);
  return true;
}

bool BluetoothEmuDevice::RemoteDisconnect(const bdaddr_t& address)
{
  return SendEventDisconnect(GetConnectionHandle(address), 0x13);
}

WiimoteDevice* BluetoothEmuDevice::AccessWiimoteByIndex(std::size_t index)
{
  if (index < MAX_BBMOTES)
    return m_wiimotes[index].get();

  return nullptr;
}

u16 BluetoothEmuDevice::GetConnectionHandle(const bdaddr_t& address)
{
  // Handles are normally generated per connection but HLE allows fixed values for each remote.
  return 0x100 + address.back();
}

u32 BluetoothEmuDevice::GetWiimoteNumberFromConnectionHandle(u16 connection_handle)
{
  // Fixed handle values are generated in GetConnectionHandle.
  return connection_handle & 0xff;
}

WiimoteDevice* BluetoothEmuDevice::AccessWiimote(const bdaddr_t& address)
{
  // Fixed bluetooth addresses are generated in WiimoteDevice::WiimoteDevice.
  const auto wiimote = AccessWiimoteByIndex(address.back());

  if (wiimote && wiimote->GetBD() == address)
    return wiimote;

  return nullptr;
}

WiimoteDevice* BluetoothEmuDevice::AccessWiimote(u16 connection_handle)
{
  const auto wiimote =
      AccessWiimoteByIndex(GetWiimoteNumberFromConnectionHandle(connection_handle));

  if (wiimote)
    return wiimote;

  ERROR_LOG_FMT(IOS_WIIMOTE, "Can't find Wiimote by connection handle {:02x}", connection_handle);
  PanicAlertFmtT("Can't find Wii Remote by connection handle {0:02x}", connection_handle);

  return nullptr;
}

}  // namespace IOS::HLE
