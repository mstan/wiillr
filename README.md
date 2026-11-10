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
responses or captured machine initialization are substituted.

The machine reuses the shared PI, VI, MI, AI, EXI, SI and DI implementations.
Wii clock selection preserves the existing GameCube defaults. The DSP engine is
selected at build time with `WIILLR_DSP`; development interpretation and static
AOT catalogs remain separate builds. DSP ROMs/coefficient data must be supplied
locally. Rendering, audio delivery, IOS services and title compatibility remain
in progress.

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
