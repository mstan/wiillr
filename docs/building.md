# Building and releasing wiillr

## Choose the workflow first

- **Run Wii Menu:** download the Windows preview from
  [Wii Menu Recomp releases](https://github.com/mstan/wii-menu-recomp/releases),
  extract the complete archive and follow the [application setup guide](wii-menu.md).
  The launcher imports your own supported Wii Menu 4.3U NAND backup or extracted
  system folder. No compiler or build arguments are needed to run the archive.
- **Use the developer tools:** download a
  [wiillr developer bundle](https://github.com/mstan/wiillr/releases/tag/wiillr-v0.1.0)
  and follow the [developer guide](developer-tools.md). Its inspection commands
  do not require rebuilding the native application.
- **Compile from source:** use the instructions below. Native Menu application
  builds belong to the [Wii Menu consumer](https://github.com/mstan/wii-menu-recomp),
  which pins this engine and selects its presentation provider.

The public checkout is not currently a self-contained source build of the native
Menu application. Its qualified generated PowerPC catalog is a private maintainer
input. Native developer tools do not need that catalog, but still need their
pinned native component sources. Some component repositories are private;
corresponding component source snapshots accompany the applicable release
archives. A public source checkout alone does not supply every dependency.

## Build arguments

These arguments belong to the source-build scripts, not the application's
startup launcher. Paths below are relative to the current working directory
unless absolute; run examples from the relevant repository root.

| Argument | Meaning | Current behavior |
| --- | --- | --- |
| `--product` | Which executable/package to build | Required by the engine driver; exact choices appear below. The Wii Menu consumer selects its own product. |
| `--version` | Label for the output package | Required by the engine driver. The Menu consumer defaults to the version in its `product.json`. It does not select firmware or change source revisions. |
| `--inputs-dir` | Root of a verified developer build-input bundle | Required for Menu/game builds; optional for developer tools when component sources are already available. This is not a NAND, WAD or game-image folder. |
| `--build-dir` | Working directory for CMake caches, objects and staged binaries | Required today. Choose a separate directory for each host/toolchain/product/configuration. |
| `--output-dir` | Destination for finished ZIP/tar archives and manifests | Required today. It is separate from the build working directory. |
| `--jobs` | Maximum parallel build jobs | Engine default: 1. Menu consumer default: 2. |
| `--arch` | Native target architecture | Defaults from the host. Supported names: `x86_64` and `arm64`; `universal` requires macOS. |
| `--no-package` | Compile/stage without creating a release archive | Off by default. |

The scripts create their build/output directories as needed. **Build and output
directories do not have automatic defaults yet.** Using `build/...` and `dist`
in the examples is an explicit choice, not an implemented default.

### Product names

The engine accepts these exact product selectors:

| Selector | Result |
| --- | --- |
| `wiillr` | Developer CLI, native discovery/setup tools and PowerPC compiler; no generated title catalog required |
| `wii-menu` | Base Menu application preset; use the separate Menu consumer for its adaptive provider and product packaging |
| `mega-man-9` | Internal game-consumer preset; its application downloads remain private |

A product is a build preset, not a filename, game ID, firmware revision or
version number. In the Wii Menu consumer, omit `--product`: its `product.json`
already selects `wii-menu`.

### Package versions

The version is your output release label. **It does not have to match another
title's version**, Wii Menu 4.3U, an IOS version, the input archive's date or a
Git tag. The checkout and its input specification determine source/content
compatibility independently of this label.

Use a simple value such as `0.1.0` or `0.1.1-local.1`. Packaging accepts a label
that starts with an ASCII letter or digit and then contains only ASCII letters,
digits, `.`, `+`, `_` or `-`. Spaces, slashes and empty labels are rejected.
This is filename validation, not strict Semantic Versioning.

Omit a leading `v` in normal use: archive names already add one, such as
`wiillr-windows-x86_64-v0.1.0.zip`. Passing `--version` does not check out a
different source revision or rewrite the engine's CMake project version.
Platform-specific bundle metadata may require additional conventions;
native macOS application packaging remains unqualified.

The build directory records the product, architecture, input location and
version. Changing one currently requires a different build directory. Choosing
a new package version does not make an incompatible input bundle valid.

## What is a valid build input`

There are three different input concepts:

| Input | Used by | Example |
| --- | --- | --- |
| Runtime system/game data | Application launcher | Your supported NAND backup or extracted system folder |
| Inspection/preparation input | Developer CLI | `wiillr inputs nand ...` reads NAND metadata; `prepare` handles original code/captures |
| Verified build-input bundle | `tools/build.py --inputs-dir ...` | An extracted maintainer bundle containing `inputs.json`, sources and a prepared catalog |

A typical native application build bundle has this shape:

~~~text
wii-build-inputs/
  inputs.json
  deps/
    gc-wii-core/
    powerpc-recomp-core/
    arm-recomp-core/
    recomp-ui/
    ...other pinned providers...
  catalog/
    prepared.json
    generated/
  LICENSES/
  source/               # source archives/notices, when listed by the manifest
~~~

For this checkout, validation checks:

1. `inputs.json` has schema 1 and a nonempty map of relative filenames to SHA-256
   digests. Every listed file must exist, remain inside the bundle and match.
2. Application source providers match the revisions in
   [tools/build-inputs.json](../tools/build-inputs.json).
3. The prepared catalog receipt matches that specification's exact expected
   hash, uses Broadway with preserved PC namespaces, and lists matching generated
   source hashes.

Renaming a NAND folder or placing a WAD beside `inputs.json` cannot create a
valid bundle. A boot DOL alone also does not cover all later-loaded code.
The existing [discovery/preparation workflow](developer-tools.md#prepare-a-static-catalog)
is separate from building, and a newly prepared catalog is not automatically a
qualified release input.

Frozen catalog bundles remain private. If you do not have the matching bundle,
use the prebuilt application; these instructions do not imply it is a public
download. Maintainers with access can extract the current frozen archive into
a new directory:

~~~sh
python tools/release_inputs.py wiillr-release-inputs-20261115-3.zip /path/to/wii-build-inputs --sha256 c00f1a49ff909fd2fe2e754808a68868564413fc3e8749be7a396502fe6e64d7
~~~

This verifies the archive before extraction. The build driver then verifies its
manifest and catalog before configuring CMake. Verification is a prerequisite,
not a guarantee that the host toolchain is installed or that any bundle matches
every published preview. This frozen example belongs to the earlier 0.1.0
release inputs. The adaptive preview uses separately retained provider inputs;
a fresh end-to-end rebuild through the consumer wrappers with a matching
release-specific bundle has not been qualified. Published archive qualification
and source-build reproducibility are separate checks.

## Prerequisites

Use Python 3.10 or newer, Git, CMake 3.25 or newer, Ninja and a C/C++ compiler with
a C++23 standard library providing `std::expected`. Initialize the pinned
component sources, or provide their exact source trees through an input bundle.
Do not put firmware, NAND, console keys or generated title code in Git.

Windows uses the MinGW-w64 toolchain. Put its `bin` directory first in PATH, or
set `CMAKE` to the intended CMake executable. Linux uses GCC 13 or newer, or
Clang with a compatible C++23 library. Clang 18 with Ubuntu's libstdc++13 does
not provide the required `std::expected`; use GCC 13 for that installation.
macOS uses current Xcode command-line tools.

Native application builds additionally need SDL3, fmt, zlib, xxHash and the
pinned DSP/GX providers described by the input specification. Windows application
packaging needs the licensed Visual C++ x64 runtime files: set
`WIILLR_REDIST_DIR` to the installed Visual Studio
`VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT` directory. Files are copied
from that explicit SDK location. The tool-only product does not need this
graphics dependency.

## Local commands

### Developer tools from the engine repository

These examples assume the pinned native component sources are available.
They do not require a generated game catalog. Add `--inputs-dir` if your
component sources are supplied through a verified bundle.

Windows PowerShell:

~~~powershell
./tools/build-windows.ps1 --product wiillr --version 0.1.0 --build-dir build/wiillr-windows-x86_64 --output-dir dist --jobs 2
~~~

Linux:

~~~sh
bash tools/build-linux.sh --product wiillr --version 0.1.0 --build-dir build/wiillr-linux-x86_64 --output-dir dist --jobs 2
~~~

macOS:

~~~sh
bash tools/build-macos.sh --product wiillr --version 0.1.0 --build-dir build/wiillr-macos --output-dir dist --jobs 2
~~~

### Wii Menu from its consumer repository

The consumer chooses its product and default package version. A maintainer
with matching build inputs can run, for example:

~~~powershell
./tools/build-windows.ps1 --inputs-dir "C:/wii-build-inputs" --build-dir build/windows-x86_64 --output-dir dist --jobs 2
~~~

Use the consumer's Linux/macOS wrappers with the same arguments and an
appropriate input path/build directory. An optional `--version 0.1.1-local.1`
changes the package label. The consumer's README records which platform archives
have actually been qualified.

Both drivers support `python tools/build.py --help`. Their wrappers build/package
locally; they do not open input/save dialogs or launch a game.

## Common errors

| Message or symptom | Check |
| --- | --- |
| Missing `inputs.json` | `--inputs-dir` must point directly to the extracted build bundle, not to your NAND, its parent folder or the ZIP itself. |
| Changed build input / provider revision differs / catalog is not qualified | Match the bundle to this checkout. Preserve frozen receipts; changing their names or hashes does not qualify different code. |
| Build directory belongs to a different configuration | Choose a new build directory for the requested product/version/input configuration. Keep existing outputs until the replacement is verified. |
| Invalid release version | Use the filename-safe label described above. This validation currently occurs during packaging. |
| Missing compiler, CMake package or `std::expected` | Check the native toolchain and prerequisites; the version label cannot resolve dependency errors. |

## Distribution and verification

Windows uses ZIP, Linux uses portable tar archives, and macOS applications use
an application bundle in ZIP. macOS builds use ad-hoc signing where available;
they are not Developer ID signed or notarized. Native macOS archive/bundle
validation remains unverified.

Existing Linux archives were built on Ubuntu 24.04 with glibc 2.39 and retain the
host's glibc and graphics drivers. Older distributions may need a native build.
Other-distribution gameplay and hardware performance are not qualified.

Packages include runtime libraries and applicable source/license notices.
Publication requires dependency-closure and package smoke checks; a successful
component build does not establish another platform's gameplay qualification.

The engine's `tools/build_release.py` is a maintainer batch driver with its own
`--products` selector. It is separate from the single-product `tools/build.py`;
use each command's help for its exact arguments. Optional `--common-key` is
restricted to the internal game product and is never auto-discovered or needed
for a Menu build.

## Potential workflow improvements

These are proposals, not current CLI behavior:

- Freeze release-specific input/provider identities and qualify a fresh rebuild
  using the documented commands, including the adaptive Menu preview.
- Default build directories by product, host and architecture, with `dist` for
  archives. Keep explicit path overrides for large builds on another drive.
- Default the engine product to developer tools and derive version labels from
  authoritative project metadata; consumers already own product/version metadata.
- Add a validation-only preflight that reports missing sources, wrong bundle
  revisions and invalid version labels before compilation.
- Separate package labels from compile-cache identity so changing a release
  label can reuse compatible objects.
- Separate dependency-source setup from private title catalogs, and document a
  reproducible public developer-tools setup before presenting a one-command
  native application build.
