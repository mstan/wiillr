# wiillr

Wii low-level static recompiler, sharing GameCube compiler and device foundations
with gcnllr. Initial targets are the original Startup Menu and Mega Man 9.

Bring-up is in progress. The original Startup Menu reaches its Startup Disc
prompt with live rendering in discovery and static PPC development builds.
Input, audio delivery, title lifecycle and Mega Man 9 gameplay remain unqualified.
Native acceptance requires zero interpreter execution and unresolved native
misses. Development discovery and maintained LLE references remain separate
from accepted native execution. HLE/LLE service selection happens at build time.

The CMake build consumes pinned shared PowerPC, machine/device and DSP packages.
Its current products include the compiler, reference contract tools and a
bounded Wii discovery executable. The discovery build interprets original
guest code with explicit counters and content-identified misses; it is not an
accepted native menu or game runtime. An optional `wiillr-native` target runs
compiled, content-verified PPC pages and stops at the first native miss. It
preserves physical, cached and uncached execution PCs; captured code is never
used to initialize the machine. This alone does not qualify its IOS/DSP paths.
Initialize
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
Active-title ES content open/read/seek/close resolve real TMD indices and shared
content mappings. Reads publish their bytes through the CPU memory interface.
`--native-ios37` explicitly selects an experimental IOS37 memory/service boundary
for titles requesting it; it executes no original IOS37 ARM firmware. The title's
requested IOS is retained, and unsupported requests still stop execution.
Ticket views preserve the installed metadata without claiming authentication.
Usage-limit queries support unrestricted tickets and reject unimplemented
restricted-ticket accounting. STM slot-illumination settings update the modeled
GPIO state; network RTC synchronization retains the guest counter without
inventing UTC or persistent clock data.
The native Bluetooth transport adapts licensed Dolphin/RecompCore HCI and
L2CAP code through explicit RAM and asynchronous IPC interfaces. Controller
configuration supports readback/reset. A virtual base Wii Remote supplies buttons,
acceleration and infrared reports through the actual HID channels. Console Sync
and remote pairing use guest discovery, PIN exchange and persistent controller
key storage beside the copied NAND; an existing SYSCONF is not overwritten.
The native virtual endpoints model unit-key agreement rather than Broadcom radio
cryptography. Only report modes 0x30, 0x31 and 0x33 are implemented; extensions
NACK and speaker playback remains unsupported. Firmware uploads and unimplemented USB
operations stop explicitly. Provenance and licenses are in
`third_party/dolphin-bluetooth`.
For bounded development input, `WIILLR_INPUT_SCRIPT` names sorted lines containing
`guest-cycle button-mask-hex [present [pairing]]`; `WIILLR_SYNC_CYCLES` schedules
one console Sync press. These feed device input without changing guest flags.
`--interactive` opens a normal window with normal audio; automated runs remain
hidden and silent. `--audio-output muted` exercises the real host device without
speaker output. `--window-title` sets the clean game caption. Visible controls
use arrows/left stick for the sideways D-pad, X/South for 2, Z/East for 1,
Enter/Start for Plus, C/West for A and A/right shoulder for HOME. Hold D/right
trigger to enter virtual-remote pairing and press the console Sync button.
I/J/K/L or the right stick move the virtual pointer. Physical-device ownership
and the owner-facing interaction route still require qualification.

The machine reuses the shared PI, VI, MI, AI, EXI, SI and DI implementations.
The GPIO AV encoder bus clocks real I2C transactions into a register bank;
unaddressed devices NACK, and reads of unimplemented register state stop.
Analog output conversion and register-driven presentation are not yet qualified.
CP/PE and the gather pipe feed the same shared native GX service using explicit
MEM1/MEM2 callbacks. `WIILLR_GX=native-development` and `--gx-library` enable
that development composition; the default build stops when it needs a renderer.
Audio DMA uses shared paced transfers and records retired PCM. The shared host
sink supports opt-in output and bounded WAV capture for the exercised 32 kHz
route. A rate change with this sink attached stops explicitly. Headless runs
remain muted. These components do not establish menu compatibility.
Wii clock selection preserves the existing GameCube defaults. The DSP engine is
selected at build time with `WIILLR_DSP`; development interpretation and static
AOT catalogs remain separate builds. DSP ROMs/coefficient data must be supplied
locally. Rendering, audio delivery, IOS services and title compatibility remain
in progress.

Experimental `WIILLR_DSP=hle` builds reuse the pinned DSPHLE implementation
through the same shared engine contract. Configure `WIILLR_DSP_DONOR_SOURCE`
with the RecompCore checkout and `WIILLR_DSP_HOST_SOURCE` with its BlueWake host
adapter checkout; `WIILLR_DSP_FMT_SOURCE` optionally supplies matching fmt
sources. The build derives its console-aware adapter without editing either
donor. Wii MEM2 is supported and unknown DSP tasks stop explicitly. This
selection requires its own title/audio qualification; the separately selected
AOT and development interpreter remain available as LLE references.

`tools/prepare_ppc.py` validates an original-title discovery's input, journal
and code-page identities before using the shared Broadway compiler. Pass its
private `generated` directory as `WIILLR_PPC_GENERATED_DIR` to build the native
target, then select `--ppc-mode static` in `tools/wii.py`. That mode's bounded
budget counts guest cycles; discovery counts interpreted instructions.
The separate `wiillr-native-discover` target permits counted interpreter misses
for bring-up (`--ppc-mode mixed-discovery`). It cannot establish native acceptance.
Repeat `--capture` during preparation to combine verified loader and later title
code variants with matching title/profile identities.
`WIILLR_CONTROL_PORT` enables a loopback-only, nonblocking development interface;
`WIILLR_CONTROL_PAUSED=1` parks before the first instruction. `tools/wii_control.py`
provides `status`, `pause`, `run [guest-cycles]`, `input mask [x y pointing present
pairing]`, `sync`, `read hex-address byte-count`, `write hex-address hex-bytes`,
`frame` and `quit`. Frame capture drains actual GPU work without advancing guest
time; `--frame-output` optionally saves a PNG using Pillow. RAM commands reject
MMIO and out-of-range requests. A pause preserves guest time and device state.

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

`tools/install_wad.py` can prepare verified decrypted contents in a marked NAND
copy using a separately supplied private common-key file. Its optional Python
dependency is listed in `tools/requirements-inputs.txt`. It preserves existing
native progress and original ticket bytes. Signatures remain unverified, and an
explicit offline key-index override is recorded separately from the ticket's
selector. Offline preparation does not establish native ES import or launch.

Format references: [title metadata](https://wiibrew.org/wiki/Title_metadata),
[WAD layout](https://wiibrew.org/wiki/WAD).
