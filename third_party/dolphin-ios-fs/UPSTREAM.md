Dolphin IOS host filesystem backend, GPL-2.0-or-later.

Source: https://github.com/dolphin-emu/dolphin/tree/70c5a6ad0e62ce32d922dd2478d465e9f0f84424/Source/Core/Core/IOS/FS

Imported files: FileSystem.h, FileSystemCommon.cpp, HostBackend/FS.h,
HostBackend/FS.cpp, HostBackend/File.cpp. Copyright notices and COPYING are
retained. Original wiillr bindings use GPL-3.0-or-later; linking this backend produces
a combined work subject to the upstream GPL license.

Adaptations: flattened includes; portable standard-library host bindings;
removed global user-directory selection, Dolphin save-state/movie integration,
formatting specialization and interactive retry dialogs. Save-state support is
not exposed. FileHandle move assignment, overflowing read bounds, directory-use
component boundaries and host deletion error propagation are corrected. RAII
close runs in release builds too. File data, metadata,
directory ordering and IOS result semantics retain the upstream implementation.
The wiillr adapter checks caller permissions and confines operations to a marked,
copied NAND profile. C++23 is required for this optional development backend.

NWC24Identity.cpp adapts only NWC24MakeUserID from
https://github.com/dolphin-emu/dolphin/blob/70c5a6ad0e62ce32d922dd2478d465e9f0f84424/Source/Core/Core/IOS/Network/KD/NetKDRequest.cpp
(GPL-2.0-or-later, copyright 2017 Dolphin Emulator Project). Config layout and
checksum semantics are documented in NWC24Config.h/.cpp at the same revision.
wiillr supplies an explicitly virtual persistent chip ID for offline profiles;
this does not provision console certificates or register an online account.
