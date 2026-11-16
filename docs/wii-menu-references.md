# Wii Menu source and annotation references

## Decompilation and versioned names

[koopthekoopa/wii-ipl](https://github.com/koopthekoopa/wii-ipl) is a
work-in-progress decompilation with separate 4.3U, 4.3E, 4.3J and 4.3K targets.
Its CC0 source, NintendoWare headers, named functions/globals and object splits
are useful for understanding the original Menu. It requires original inputs;
the repository supplies no Menu assets or executable assembly.

wiillr's [native adapters](../native/README.md) already use its **4.3U**
annotations at revision `f4c09f4001c5a0b9af734ad1e3f80a54c5e5723c`.
The expected original DOL SHA-1 is `26116613f624061ba99c8d1a299aaa6efa85670d`.
Names and addresses from other versions must be verified independently.

Start with these pinned references:

| Reference | What it provides |
| --- | --- |
| [43U symbols](https://github.com/koopthekoopa/wii-ipl/blob/f4c09f4001c5a0b9af734ad1e3f80a54c5e5723c/config/43U/symbols.txt) | Function/global addresses, sizes and types |
| [43U splits](https://github.com/koopthekoopa/wii-ipl/blob/f4c09f4001c5a0b9af734ad1e3f80a54c5e5723c/config/43U/splits.txt) | Section ranges mapped to source modules |
| [Channel selection](https://github.com/koopthekoopa/wii-ipl/blob/f4c09f4001c5a0b9af734ad1e3f80a54c5e5723c/src/scene/channelSelect/iplChannelSelect.cpp) | Channel layout, visibility, page scrolling and selection |
| [Channel scene structure](https://github.com/koopthekoopa/wii-ipl/blob/f4c09f4001c5a0b9af734ad1e3f80a54c5e5723c/include/scene/channelSelect/iplChannelSelect.h) | Scene fields and their original offsets |

The decompilation is incomplete. Its README also describes an SEL-related
restriction on shifting code. A symbol map is not proof that a modified
executable can be relocated or rebuilt with identical behavior.

## Readable annotated disassembly

Like a verified disassembly overlay, [the annotation manifest](../annotations/wii-menu-43u.json)
pins the original executable and upstream metadata. It adds original explanatory
notes around the channel scene. The importer preserves aliases and separates
function/object annotations; names do not automatically create recompiler entries.

From a source checkout with Python and optional `capstone==5.0.7`:

```sh
python tools/annotate_wii_menu.py fetch --output private/wii-ipl-43u
python tools/annotate_wii_menu.py annotate --dol /path/to/original-main.dol --annotations private/wii-ipl-43u --output private/wii-menu-43u-listing
```

The first command downloads only the three pinned public metadata files and
checks their hashes. The second verifies your original DOL before writing:

- `wii-menu-43u.asm`: addressed instructions, exact symbol labels, source modules,
  branch-target names and explanatory comments.
- `symbols.json`: the full function/global index, including BSS variables.
- `channel-grid.md`: a concise map of the annotated channel-layout entry points.
- `listing.json`: executable identity, decoder version and coverage counts.

The listing is a reading aid, not a byte-exact reassembly authority like a
complete matching disassembly. Unclassified text and instructions unsupported
by the decoder remain explicit raw words. Data spans inside function ranges
stay data. Existing output directories are preserved. Keep these generated
outputs private: they contain original executable instructions. No game build,
runtime patch or NAND write occurs.

The original Menu places most executable bytes in an initialized section marked
as data in the DOL header. Verified function spans identify that code; the
importer does not assume the header's text/data flag settles classification.

## System behavior and formats

- [System Menu](https://wiibrew.org/wiki/System_Menu): title identity, startup
  files, channel launching and the SD Menu's copy-to-NAND behavior.
- [System Menu HTML](https://wiibrew.org/wiki/System_Menu/HTML): the local settings
  pages and their native `wiiSetting` bridge.
- [BS2 disc state machine](https://wiibrew.org/wiki/System_Menu/BS2): documented
  disc-handling states, explicitly based on **3.1U**; verify differences for 4.3U.

These are reverse-engineering references. The supported application behavior and
setup instructions remain in the [Wii Menu product guide](wii-menu.md).
