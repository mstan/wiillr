Dolphin Bluetooth protocol transport, GPL-2.0-or-later.

Local donor: https://github.com/chrissotraidis/RecompCore/tree/338a3dbcdd384142f0fe40db44e0cd1d1b0c61e4/Source/Core/Core/IOS/USB/Bluetooth

The donor retains Dolphin's implementation and copyright/license notices.

Adapted packet serialization, HCI commands and L2CAP/HID channel management.
Original copyright, GPL and embedded BSD notices are retained. The combined
binary is subject to these licenses. Original wiillr interfaces remain MIT.

Adaptations remove Dolphin global machine, UI, input, netplay, movie and save-state
dependencies. Memory and asynchronous replies use explicit consumer interfaces.
Only implemented commands are admitted; unknown and vendor firmware operations
stop execution. Reset clears modeled connections and queues while preserving
outstanding USB receive transfers. No SYSCONF is
generated or overwritten by constructing the device. Controller report generation
is a separate interface. This is source reuse, not a Dolphin executable launch.
The native controller advertises firmware revision0 rather than claiming to run
the donor's obsolete Broadcom ROM0x40a7. Broadcom firmware uploads are unsupported;
the native provider owns the controller behavior. Pairing storage starts empty,
then preserves keys installed through actual guest commands in the consumer's
isolated profile. Reset retains them. Wrong PINs and mismatched link keys fail
authentication. Native virtual endpoints agree a fresh remote unit key after
checking the console-address PIN; Broadcom radio cryptography is not executed.

`Remote.cpp`/`Remote.h` adapt the base report, EEPROM, calibration and I2C behavior
from the same donor's `Core/HW/WiimoteEmu/EmuSubroutines.cpp` and input-report
implementation. They retain Dolphin's GPL notices. The bounded source implements
buttons, acceleration, extended infrared and reports 0x30/0x31/0x33. Unsupported
extensions NACK; speaker audio and other report modes stop explicitly.

Unmodified donor file SHA-256 identities:
- `hci.h`: `7eb577e0b615e99fcfedbccb93988a92543ae3019c7cd45713ddd23918c0266b`
- `hci_commands.h`: `cfb766a4b3bffe6feec8d6a8915e51bcd22854b0d899cddee7bdadc29a5b0093`
- `hci_events.h`: `6947214e84de31c531461b7fef12caee3286557bf3762138c29ca99e963aac32`
- `l2cap.h`: `fa01ff9fa5e9e9a87a0192144e15d3974baf71ccb3f2a2fdd53f15f7eec025b5`
- `WiimoteHIDAttr.h`: `71697989e5a66094588e49c8e5fe5c1b16931fd16896e6f07be124927f580a34`
- `WiimoteHIDAttr.cpp`: `058abf66767eea9ff484f8d21cb2a6aaca397a09fb137a1c5b2f789ac6e0ef26`
- `WiimoteDevice.h`: `d4648a967b9baea1486a3e0956a13c52de75928e932b7d811f5c54c134642dc6`
- `WiimoteDevice.cpp`: `8045fba19a2be65a16a4e960c8abf136300dcf2e72ca6cbaef3ca3ce13c8ea9b`
- `BTEmuHCI.cpp`: `0aaa1c54bb8e945c8d37eeef5c4baf76d80aa69d223307820e8f872eab46d87c`
