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

The root build's `WIILLR_MM9_RENDERER` defaults to `none`. `oracle` prepares the
logical image at the verified entry and compares the real RGB565 target after
the original compositor completes. It leaves original rendering active and
stops on an unsupported input or material image mismatch. A memory dump taken
after the original frame has retired its sprite pool or scroll tables is not an
input oracle. The focused tests cover independent tile/color, ordering,
transparency, scroll, line-limit and rejection contracts.

`reference` retains the same provider-present bit and attested entry label but
does no native provider startup, compositing or draw work. It runs the original
bodies and is the matched performance reference; `oracle` computes both paths
and is a correctness check only.

`declared-slice` installs the complete compositor and matching final-screen
hooks. The native compositor publishes the real RGB565 target, preserves the
atlas copy-clear regions through typed EFB clears, performs the caller-visible
display copy and completion, retires scroll/sprite controls, and retains the
original mutex-protected queue operation. It omits internal atlases and
scanline draw construction. The shared Frame API must expose the full physical
528-row Wii EFB independently of VI's displayed height. Unsupported final-screen
families are counted and retain their original body. `strict` stops on any
unsupported hook input; backend failures stop in both modes. These are
experimental build selections: meaningful same-entry image, route and runtime
performance gates remain required before promotion.

The title-specific boundary is WR9E `0x800e7308` (extent `0x108`), which owns the
tile atlas, background/sprite scanline compositor and final screen copy. Its
original bytes have FNV-1a-64 `20a0940bded73216`. Layout facts come from the CC0
[Mega Man 9 decompilation](https://github.com/arceuss/mm9decomp), revision
`3c57949bc239cb9c1836e8c33d585cac8dd9d3ca`. The supplied content-1 LZ11 stream
expands to its exact original DOL SHA-1
`8a7c0c2daa65fb3dcd5176922335cf3b999cd940`. This is a different frontend from
the Menu's NW4R layout provider.

`mm9_screen` replaces the queued-quad family of `0x800d5e50`: it reads the
queued logical screen object, preserves odd rectangle sizes, UVs, depth and the
original packed RGB565 brightness, and submits a retained native quad using the
real texture identity, including the compositor's `0x0039b2c0` target.
Base-level I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8/CMPR resources are supported;
paletted or mipmapped resources reject explicitly. Its caller supplies the
proven typed camera and inherited blend/depth state and owns submission
ordering. Geometry is retained per queued object. Geometry, resource identity, brightness,
cache ownership and failure contracts are tested separately. The hook uses the
caller's SDK projection/viewport and drains machine-owned prior work before
native submission. Function identities are invalidated by executable writes.
A bounded derived entry label covers same-page compiled calls that do not
visit external dispatch; the immutable prepared catalog and original source
remain intact.

The exercised WR9E boot/gameplay replay has 3,374 byte-identical logical
compositor outputs and 7,415 native queued draws with no unsupported calls or
backend failures. Final scanout, full PCM and selected caller-visible buffers
match the original. Two earlier scanouts have small shading precision
differences (RGB8 maximum 4 and 3; mean absolute differences 0.0391 and 0.0198).
The matched normal draw-slice timing replay passed three exact output/audio
pairs: median throughput improved 82.0% and CPU time per field fell 50.3% on
the measured host. The AOT-DSP reference ran at 27.1–27.5 fields/s and the
candidate at 47.8–52.3 fields/s. These results do not meet the wider campaign's
throughput target or establish per-thread phase budgets.

`WIILLR_MM9_FRAME=native` extends that provider into one scoped frame owner
inside the attested original frame orchestration. It supplies typed camera,
clear, display-copy and completion operations, submits the complete ordered
quad queues, and omits internal palette-cache/TLUT materialization. The original
input, logical palette changes, VRAM transfers, game/pause logic and VI handoff
continue to execute. Texture initialization preserves the real caller-visible
texture-object fields without issuing graphics commands. A populated separate
overlay draw list, unsupported input or failed native submission stops visibly;
an owned frame cannot resume an original path whose setup it omitted.

The 43-billion-cycle functional replay completes 3,374 owned frames and 7,415
native draws with zero owned-frame FIFO bytes, zero interpreter execution,
zero unresolved native misses and no unsupported cases or backend failures.
Only 1,792 bootstrap FIFO bytes remain outside that boundary. Final video,
the complete 1,806,344-frame stereo PCM stream and the checked caller buffers
match the original exactly. The two earlier pictures are byte-identical to the
draw-slice candidate, retaining its bounded shading precision differences.
This frame-owner result is functional evidence; its separate normal timing
comparison remains required before a performance claim.
