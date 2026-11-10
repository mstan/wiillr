Dolphin IOS host filesystem backend, GPL-2.0-or-later.

Source: https://github.com/dolphin-emu/dolphin/tree/70c5a6ad0e62ce32d922dd2478d465e9f0f84424/Source/Core/Core/IOS/FS

Imported files: FileSystem.h, FileSystemCommon.cpp, HostBackend/FS.h,
HostBackend/FS.cpp, HostBackend/File.cpp. Copyright notices and COPYING are
retained. Original wiillr bindings remain MIT; linking this backend produces
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
