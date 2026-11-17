# Wii Menu Recomp

Run the original Wii Menu with a native desktop launcher, mouse pointing,
configurable Wii Remotes and image-backed Wii peripherals.

## Highlights

- The original channel grid, Wii Settings, Message Board and Save Data screens.
- A dedicated launcher with NAND import and a managed writable system profile.
- Mouse pointing, four Wii Remote assignments, USB keyboard typing and USB/SD images.
- Widescreen **Fit to screen**, internal-resolution controls and a consistent
  window when entering a supported channel and returning through HOME.

![Wii Menu launcher with NAND import, four player assignments, USB ports and SD Card](images/wii-menu-launcher.png)

*The startup launcher keeps system import, controller assignments and Wii
Peripherals together.*

## First launch

1. Extract the complete package and open **Wii Menu**.
2. Choose **Import NAND backup** for a BootMii nand.bin, or **Import extracted
   folder** for an extracted NAND root. The folder picker also recognizes a
   containing folder with one nand.bin or one immediate nand/Wii root. It does
   not search your drive for system files.
3. Supply your own **Wii Menu 4.3U (USA)** system files. A BootMii backup can use
   its embedded keys; **Console keys** accepts the matching `keys.bin` or
   128-byte OTP backup when needed. When a backup has no appended keys, a
   matching keys.bin beside nand.bin is detected automatically. An explicitly
   selected key file takes priority. An extracted folder needs no separate key.
4. Wait for **Wii system files are ready**, then choose **Launch**.

Import verifies the supported Menu content and installed metadata before launch.
It creates a managed writable copy. Your source backup, saves and system settings
are preserved. Importing another system creates a separate profile rather than
merging over existing progress. Other Menu revisions are rejected explicitly.
No system files, games, NAND backups or console keys are included.

![Original Wii Menu channel grid with Mega Man 9 installed](images/wii-menu-running.png)

*The original Menu with an installed Mega Man 9 channel; channel content is
supplied by the player.*

The native IOS80 provider does not require the original IOS80 executable.
Original IOS LLE is a separate development build and is not qualified by this
release's native-provider checks.

## Channel system dependencies

Booting the Menu does not establish that every installed channel has all of its
original system dependencies. Channel startup may also request the ticket, TMD
and content of its required IOS version. For example, a Menu-only profile without
IOS37 can show a supported channel's banner but return to the Menu when Start
requests that dependency. Import a complete compatible backup from your own
console when those files are available; Menu import does not invent missing IOS
licenses or firmware. Existing native saves remain part of the managed profile.

For a stopped test copy, developers can use the verified
[dependency installer](building.md#adding-an-original-ios-dependency) to merge an
original IOS title from a separate NAND source. Keep original IOS LLE/OTP
qualification separate from the native service provider.

## Controls and peripherals

Use the mouse as the pointer, left click for A and right click for B. Keyboard
**A** is the default HOME binding; change it in **Settings → Controller**.
**F1** opens settings during play. Controller mappings and window geometry persist.

The launcher has separate Player 1–4 Wii Remote assignments. **Wii Peripherals**
below them provides two USB slots and one SD slot:

| Slot | Choices |
| --- | --- |
| USB 1 / USB 2 | Disconnected, host keyboard, raw mass-storage image |
| SD Card | No card, raw SD image |

USB 1 defaults to Keyboard. Typing through this peripheral is independent of
keyboard buttons assigned to a Wii Remote. Choose an existing formatted raw
image for storage; the launcher does not create or format images. New selections
start read only. Clear **Read only** to allow guest writes. **Eject** flushes and
detaches the selected image. Settings survive restart.

## Display, mods and channels

Fresh settings use widescreen and **Fit to screen**. **Mods** contains Wii system
options, including internal resolution; game-specific Mega Man 9 mods belong in
its [separate launcher](mega-man-9.md). Higher internal resolutions increase GPU
cost and do not create new detail in bitmap artwork.

Development builds with the native Menu layout provider also offer **Adaptive
channel grid (experimental)**. Enable it in Mods and select **Fit to screen**,
then resize the window. The grid keeps three rows and adjusts its columns,
including two in portrait and up to six in wide desktop windows. The consumer's
viewport is capped at 21:9; wider windows retain their size with side margins.
It preserves
the original channel order and saves; channel rearrangement is unavailable while
this experiment is enabled. Other Menu screens retain their original layouts.
Portrait desktop testing does not establish Android platform support.

The consumer also offers **Expanded channel library (experimental)** in Mods,
off by default. Choose 2x through 128x the original 48 positions and restart.
Overflow placement and the last viewed position are saved separately in
`.wiillr-channel-library` in the managed NAND folder. The enabled state and
capacity use `channel-library.cfg` in the application's preferences. Native
game saves and the stock channel-order format remain unchanged. Turning the
feature off preserves stored overflow placements for a later re-enable.
See the consumer's [library guide](https://github.com/mstan/wii-menu-recomp/blob/main/docs/channel-library.md)
for ordering, shortcuts and limits.

An unconfigured network profile starts as wired Connection 1 with automatic
IP/DNS. Existing connection settings are preserved. Host IPv4 sockets and DNS
are available; AOSS, SSL, online WiiConnect24 and Nintendo service availability
are outside the release's qualified feature set.

Installed supported channels retain their original Menu entry and HOME return
flow. Wii Menu → Mega Man 9 → HOME → Wii Menu preserves the window and pointer.
This package does not install channel content for you or provide arbitrary Wii
title compatibility.

Known native-title limitation: HOME during Mega Man 9's early startup can stop
the renderer on an unsupported modal fade. Its title-screen HOME return has
passed qualification; early-startup return remains unresolved.

## Updates and building

Replace the application package while retaining your per-user preferences and
managed NAND. Do not copy a fresh template over existing native saves.
`WIILLR_USER_DIR` can select an explicit profile location.

See [building and releases](building.md) for Windows, Linux and macOS commands.
See [developer tools](developer-tools.md) for input inspection and native code
preparation, and [Menu source and annotation references](wii-menu-references.md)
for the versioned decompilation, symbols and system documentation. wiillr is independent software and is not affiliated with Nintendo.
