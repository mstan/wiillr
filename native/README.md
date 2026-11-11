# Native Wii Menu pictures

The experimental `menu_picture` provider replaces the complete 4.3U NW4R
`Picture::DrawSelf` operation: it reads animated pane geometry, global alpha,
material values, texture coordinates and transforms and submits an owned native
quad to the shared host renderer. It never executes or constructs GX commands.
Unchanged geometry is retained while poses and texture memory remain live.

The initial family supports default flat or single-texture materials, vertex or
material color sources, alpha blending, unrotated texture scale/translation and
base-level textures. Unsupported material equations, filtering and layout cases
reject explicitly. Backend failures are distinct from unsupported families.
Call `forget_picture` at pane destruction; destroy the provider before shutting
down the shared GPU service. The caller owns view selection and earlier draw
ordering.

The root build's `WIILLR_MENU_PICTURES` defaults to `none`. `declared-slice`
hooks the content-verified original Picture function and counts unsupported
Picture calls that still use the original renderer. `strict` stops on those
calls. These modes cover this Picture boundary only; other pane classes and
frame operations remain original. Native submission failures stop in both
modes. The hook drains preceding gather/CP work on the machine owner before
submitting native draws, retains the original destructor for guest allocations
and invalidates its function attestation with executable-code invalidation.
Runtime and same-scanout comparison gates remain required before promotion.

`wii_menu_picture_sdk_view` reads the caller-established 4.3U SDK projection,
viewport and scissor for staged integration. A native layout caller can provide
these values directly. Whole-workload acceptance still requires native layout
setup, the remaining pane/material families, copies, fonts and actual route
qualification. Mega Man 9 uses a separate rendering boundary; its NintendoWare
audio code does not establish that it uses these layout classes.

Version annotations come from the CC0
[Wii Menu decompilation](https://github.com/koopthekoopa/wii-ipl), revision
`f4c09f4001c5a0b9af734ad1e3f80a54c5e5723c`, specifically `libs/NW4R` and
`config/43U`. Its original executable SHA-1 is
`26116613f624061ba99c8d1a299aaa6efa85670d`. Verify that binary before using its
object offsets or installing the `0x8152126c` draw boundary. This implementation
is original MIT code and includes no firmware or generated guest code.

Configure this directory with `GC_WII_CORE_DIR` pointing to a core with
`include/gcnllr/native_render.h` for the standalone library and focused contract
test. This component build does not establish in-game rendering or performance.

## Mega Man 9 compositor candidate

`mm9_compositor` prepares the whole logical 224-line game screen directly from
tile maps, C4 tiles, RGB555 palettes, layer priorities, per-line scroll and
sprite descriptors. It handles transparent palette zero, sprite flips and the
optional shared line limit, then returns the actual 256x240 tiled RGB565
texture. It constructs no GX commands and performs no GPU readback. Output is
atomic on an unsupported or unreadable input.

This is prepared code, not an installed live replacement. The entry/copy gate
must establish exact row and border sampling and preserve the final texture
publication, queue object, completion/display copies, scroll-control retirement
and sprite-pool effects before enabling a hook. A memory dump taken after the
original frame has retired its sprite pool or scroll tables is not an input
oracle. The focused tests cover the independent tile/color, ordering,
transparency, scroll, line-limit and rejection contracts.

The title-specific boundary is WR9E `0x800e7308` (extent `0x108`), which owns the
tile atlas, background/sprite scanline compositor and final screen copy. Its
original bytes have FNV-1a-64 `20a0940bded73216`. Layout facts come from the CC0
[Mega Man 9 decompilation](https://github.com/arceuss/mm9decomp), revision
`3c57949bc239cb9c1836e8c33d585cac8dd9d3ca`. The supplied content-1 LZ11 stream
expands to its exact original DOL SHA-1
`8a7c0c2daa65fb3dcd5176922335cf3b999cd940`. This is a different frontend from
the Menu's NW4R layout provider.

`mm9_screen` replaces the final-screen family of `0x800d5e50`: it reads the
queued logical screen object, preserves odd rectangle sizes, UVs, depth and the
original packed RGB565 brightness, and submits a retained native quad using the
real `0x0039b2c0` texture identity. Its caller supplies the proven typed camera
and inherited blend/depth state and owns submission ordering. Other queued
texture families reject explicitly. Geometry, resource identity, brightness,
cache ownership and failure contracts are tested separately; live entry and
frame-effect integration remain required.
