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
only the boot DOL is insufficient. These inputs stay outside Git. Release jobs
obtain the frozen inputs from the same private repository's release assets.

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

The manual release workflow builds the requested products and platforms, retains
checksums and publishes only after its required jobs succeed. It does not run
on every source push. Product tags keep Menu, Mega Man 9 and developer-tool
releases separate. Release notes state any remaining platform limitations.
