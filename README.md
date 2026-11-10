# wiillr

Wii low-level static recompiler, sharing GameCube compiler and device foundations
with gcnllr. Initial targets are the original Startup Menu and Mega Man 9.

Bring-up is in progress. No Wii boot or gameplay compatibility is claimed yet.
Native acceptance requires zero interpreter execution and unresolved native
misses. Development discovery and maintained LLE references remain separate
from accepted native execution. HLE/LLE service selection happens at build time.

The CMake build consumes pinned shared PowerPC, machine/device and DSP packages.
Its current products include the compiler, reference contract tools and a
bounded Wii discovery executable. The discovery build interprets original
guest code with explicit counters and content-identified misses; it is not an
accepted native menu or game runtime. Initialize
dependencies with `git submodule update --init --recursive` before configuring.

`tools/wii.py` verifies the NAND title's boot content against its TMD and runs
`wiillr-discover` from a documented IOS4 v3 NAND-title memory boundary. The
original PPC entry code initializes the processor and installs its own vectors.
Boot0/1/2 and IOS execution are outside this development boundary. Required
unimplemented device/service operations stop execution. No successful IOS
responses or captured machine initialization are substituted in the default
provider-free build. `WIILLR_IOS=native-development` selects an experimental
native resource/IPC provider with explicit identity and operation accounting.
It allocates/releases STM descriptors and holds event-hook requests until a
modeled hardware event occurs; unsupported service operations stop
before acknowledgement. Original ARM execution and a functioning LLE service
counterpart are not yet qualified for this native route.
The native disc resource borrows the shared device for register queries,
interrupt controls and resets. It does not replace drive state with canned
successful reads.
With `--nand-profile`, the launcher creates or reuses a verified writable copy
of the supplied NAND and checks that original files stay unchanged. Native FS
operations use the adapted Dolphin host backend with persistent metadata,
caller permissions and profile confinement. UID/GID come from the supplied
UID table and boot TMD. Supported ES queries use that boot context; unimplemented
operations still stop discovery. This development backend requires C++23 and
retains its upstream GPL license in `third_party/dolphin-ios-fs`.

The machine reuses the shared PI, VI, MI, AI, EXI, SI and DI implementations.
The GPIO AV encoder bus clocks real I2C transactions into a register bank;
unaddressed devices NACK, and reads of unimplemented register state stop.
Analog output conversion and register-driven presentation are not yet qualified.
CP/PE and the gather pipe feed the same shared native GX service using explicit
MEM1/MEM2 callbacks. `WIILLR_GX=native-development` and `--gx-library` enable
that development composition; the default build stops when it needs a renderer.
Audio DMA uses shared paced transfers and records retired PCM, while audible
output is still unattached. These components do not establish menu compatibility.
Wii clock selection preserves the existing GameCube defaults. The DSP engine is
selected at build time with `WIILLR_DSP`; development interpretation and static
AOT catalogs remain separate builds. DSP ROMs/coefficient data must be supplied
locally. Rendering, audio delivery, IOS services and title compatibility remain
in progress.

`wiillr-starlet-discover` loads the original IOS4 ARM ELF at its kernel-entry
boundary using the pinned ARM core. Its development bus covers big-endian
MEM1/MEM2/SRAM, CP15 cache maintenance and ARMv5 section/page translation with
domain/access checks. PPC execution is excluded from this isolated route;
programmed PPC protection bounds still need enforcement in a combined machine.
Unimplemented hardware and exception routing stop discovery. It does not
establish a working IOS service provider or accepted static ARM execution.

## Inspect private inputs

Supply firmware, NAND, keys and title content locally. `private/` is excluded
from Git. Original assets and native saves must remain unmodified; runtime NAND
writes will use isolated profiles.

```powershell
python tools/wii_inputs.py nand private/firmware/nand-import/extracted_nand
python tools/wii_inputs.py wad 'private/firmware/Wii Startup Menu.wad'
python -m unittest discover -s tests
```

Inspection reports title identities, required IOS and content integrity. Parsing
a TMD does not verify its signature; reading a WAD does not decrypt or validate
its encrypted contents. These checks do not establish boot compatibility.

Format references: [title metadata](https://wiibrew.org/wiki/Title_metadata),
[WAD layout](https://wiibrew.org/wiki/WAD).
