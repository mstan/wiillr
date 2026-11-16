# Building and releasing wiillr

The repository produces three separate packages:

| Product | Build selection | Contents |
| --- | --- | --- |
| Wii Menu Recomp | `wii-menu` | Wii Menu launcher, native runtime and graphics services |
| Mega Man 9 Recomp | `mega-man-9` | Separate game launcher, WAD/DLC import and game mods |
| wiillr | `wiillr` | Developer CLI, input/preparation scripts, native discovery tools and PowerPC compiler |

The platform wrappers use the same Python build driver. They build and package;
they do not launch a game or open input/save dialogs. Use a fresh build directory
for another compiler, architecture or provider configuration.

## Prerequisites

Use Python 3.10 or newer, Git, CMake and Ninja, plus a C/C++ compiler. Native
game builds additionally need SDL3, fmt, zlib, xxHash and the pinned DSP/GX donor
sources described by the build-input manifest. Windows uses the MinGW-w64
toolchain; Linux uses GCC or Clang; macOS uses Xcode command-line tools.

Initialize the pinned submodules. The machine and DSP source repositories are
private; release maintainers may instead materialize their exact source archives
from the private build-input asset. No firmware, NAND or console key belongs in
the source checkout or in a source commit.

The developer tool builds without a title catalog. Game builds require a
qualified private generated PowerPC catalog and its matching preparation receipt.
The combined Menu/MM9 catalog preserves captured overlay variants; regenerating
only the boot DOL is insufficient. These inputs stay outside Git. Maintainers
can obtain the frozen input bundle from the same private repository's release
assets. Download [the frozen maintainer inputs](https://github.com/mstan/wiillr/releases/tag/release-inputs-20261115-2),
then extract them into a new directory:

```sh
python tools/release_inputs.py wiillr-release-inputs-20261115-2.zip /path/to/wiillr-inputs --sha256 dce873548de9e01d2c94e7763b491b41f67573e703c2a3476724b67b3f0cb432
```

The bundle contains `inputs.json`, `deps/` with the exact source providers,
and `catalog/` with its generated code and preparation receipt. The build driver
checks every file and the qualified catalog identity before configuring CMake.

## Commands

Select `wiillr`, `wii-menu` or `mega-man-9`. The examples use version `0.1.0`;
choose the version you are building.

Windows PowerShell:

```powershell
./tools/build-windows.ps1 --product wii-menu --version 0.1.0 --inputs-dir G:/wiillr-inputs --build-dir G:/wiillr-build --output-dir G:/wiillr-dist --jobs 2
```

Linux:

```sh
bash tools/build-linux.sh --product mega-man-9 --version 0.1.0 --inputs-dir "$HOME/wiillr-inputs" --build-dir "$HOME/wiillr-build" --output-dir "$HOME/wiillr-dist" --jobs 2
```

macOS:

```sh
bash tools/build-macos.sh --product wiillr --version 0.1.0 --build-dir "$HOME/wiillr-build" --output-dir "$HOME/wiillr-dist" --jobs 2
```

`python tools/build.py --help` lists shared options. Match `--arch` to the host
toolchain; cross compilation is not implied by a package filename.
`--common-key` explicitly supplies the optional private 16-byte MM9 packaging
input; it is never discovered automatically. On Windows, put the MinGW-w64
`bin` directory first in PATH, or set `CMAKE` to its absolute CMake executable.

## Distribution and verification

Windows packages use ZIP, Linux packages use a portable tar archive, and macOS
games use an application bundle in ZIP. macOS builds use ad-hoc signing where
available; they are not Developer ID signed or notarized. Follow your system's
normal approval flow for locally built applications.

Packages contain their runtime libraries, launcher assets and license/source
notices. Builds must pass the platform dependency-closure check and package
smoke check before publication. Native discovery is a development tool with
visible interpreter/miss accounting; it does not establish native gameplay
acceptance. Component portability checks do not establish another platform's
gameplay or performance qualification.

Builds run locally. To build all three products while compiling the combined
game runtime once, use `python tools/build_release.py` with `--version`,
`--inputs-dir`, `--build-dir` and `--output-dir`. `--products tools` or `games`
selects a smaller set. The script verifies packaged CLI commands, graphics
library loading and empty-profile startup without opening a game window.
It writes checksums beside the archives. Private MM9 packaging can pass the
optional `--common-key` file explicitly.

Product tags keep Menu, Mega Man 9 and developer-tool releases separate. Release
notes state which platform archives were built and tested. macOS build scripts
and instructions are included; a finished Mac archive is not part of this
milestone's delivery, and final native bundle validation remains unverified.
