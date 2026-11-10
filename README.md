# wiillr

Wii low-level static recompiler, sharing GameCube compiler and device foundations
with gcnllr. Initial targets are the original Startup Menu and Mega Man 9.

Bring-up is in progress. No Wii boot or gameplay compatibility is claimed yet.
Native acceptance requires zero interpreter execution and unresolved native
misses. Development discovery and maintained LLE references remain separate
from accepted native execution. HLE/LLE service selection happens at build time.

The CMake build consumes pinned shared PowerPC, machine/device and DSP packages.
Its current products are the compiler and GameCube reference contract tools;
the Wii-specific machine executable is still being implemented. Initialize
dependencies with `git submodule update --init --recursive` before configuring.

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
