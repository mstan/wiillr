# wiillr

Shared Wii runtime, host-native HLE services and static recompilation tools.

Application identity, original-code bindings, game-specific presentation and
release configuration belong to pinned consumers:

- [Wii Menu Recomp](https://github.com/mstan/wii-menu-recomp)
- [Mega Man 9 Recomp](https://github.com/mstan/MegaMan9Recomp)

The engine supplies disc/WAD/NAND handling, native saves, input, diagnostics,
session/window controls, guarded memory/code operations and shared NW4R codecs.
Consumers supply product, native-hook and launcher policies through the CMake
interfaces. Original on-disk inputs remain read-only. Valid guarded in-memory
image/RAM writes, overlays and hooks remain supported.

Use [developer tools](docs/developer-tools.md) for inspection and preparation.
See [building](docs/building.md) and [native runtime](docs/native-runtime.md)
for supported source routes and service contracts. All builds default to HLE;
missing required HLE sources are reported explicitly.

Build metadata records actual source roots and exact revisions, including
verified offline providers; the selected graph is not inferred from gitlinks.
No firmware, keys, NAND, generated guest code or user saves are committed.

The runtime and build tools use [GPLv3 or later](LICENSE). Imported components
retain their terms; see [licensing](LICENSING.md). The independent Menu annotation
utility lives in its consumer and retains its separate license.
