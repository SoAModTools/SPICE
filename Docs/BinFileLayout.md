# BIN File Layout

This living document tracks the current understanding of Skies of Arcadia Legends `.bin` payloads and how the SPICE `SpiceBin` project should expose them.

## Scope

`SpiceBin` owns parsing and layout documentation for `.bin` payloads. Keep detailed research notes, copied samples, decoded tables, local scripts, and other evidence under `SpiceBin/research/`; that folder is intentionally local-only and gitignored.

Use this tracked document for durable conclusions that are ready to be shared with the repo.

## Current Status

The `.bin` extension is not one proven single format yet. Current evidence
supports at least one important family: indexed UI/render layout tables, probably
the HRSBin-style layout family. These payloads appear both as named members
inside MLL archives and as standalone loose files.

The parser surface preserves the original bytes, records diagnostics, and runs
the indexed UI/render layout probe described below. It is still a scaffold for
research and downstream schema work, not a claim that every `.bin` file has been
decoded.

Do not classify every `.bin` file as this indexed layout family. The current
negative example is `field/wmaparea.BIN`, which did not match the indexed-table
probe used for HRSBin-style files and is now traced as a fixed-size world-map
area table.

Working usage hypothesis: the known `.bin` files may all be UI/menu-adjacent
resources, even when they do not share the same binary family. The HRSBin-style
indexed files describe UI/render layout elements. `battle/HrsBinCW.bin` is now
confirmed as battle command/menu/window layout data, and
`battle/HrsBinPCWin.bin` is now confirmed as battle player-character status
window layout data for the panel between the SP gauge and command wheel.
`field/hrs_bend.bin` is now confirmed as the battle-end screen layout payload,
paired with the
`field/ts000110.gvr` battle-end sprite sheet. The field X-menu opened while
moving around in-game is now traced to `/field/HrsBin_Status.mll`, which embeds
four HRSBin-style indexed layout members and one texture/model bank. The loose
`field/HRSBin.bin` remains a valid 34-record HRSBin-style layout, but its
runtime consumer is not proven by the current Ghidra trace. A follow-up
read-only decompile-term search found the known `/field/HrsBin_Hakken.mll`,
`/field/HrsBin_sbp.mll`, and `/field/HrsBin_Status.mll` loaders, but zero
matches for `HRSBin.bin` or `HRSBin.mld` in the DOL.
Further static checks found the same negative pattern: the loose-file indexed
object wrapper is only referenced by the battle-end result-screen loader for
`/field/HRS_BEND.BIN`, and a US corpus search found `HRSBin.bin`/`HRSBin.mld`
only as extracted directory/TOC metadata, not as a script-side load reference.
A direct caller audit of the lower-level `loadFileFromPath@801cc3c4` found 17
callsites across 13 functions. The field literal loads in that pass were
`/field/ts000110.gvr`, `/field/wmaparea.bin`, and `/field/wmaparea.BIN`; the
visible constructed field path was `sr_%03d%c.tec`. No audited direct load path
named loose `field/HRSBin.bin` or `field/HRSBin.mld`.
For the confirmed field X-menu banks in `HrsBin_Status.mll`, every record
reached by the current Ghidra accessor scans now has a promoted one-to-one row
in `Docs/BinFileRecords.md`. The remaining serialized embedded records are
tracked there as explicit no-discovered-consumer rows, not as editor-facing
keys.
The current Ghidra annotation handoff for this controller and its status-bank
globals is `planning/Analysis/2026-06-20_field_x_menu_bin_annotation/20260620_field_x_menu_bin_annotation_guide.txt`.
`field/wmaparea.BIN` is a world-map area/menu table consumed by world-map
callers, even though its binary structure is a separate non-indexed family.

Texture trend: current loose HRSBin-style `.bin` samples do not contain embedded
GVR texture chunks. Instead, their fixed-data tables look like texture-index
plus normalized UV/source-rectangle metadata, while nearby MLD/MLL companion
assets contain the actual GCIX/GVRT texture chunks. This suggests `.bin` files
control how UI sprites are drawn from companion texture assets rather than
storing sprite pixels directly.

Companion-bank resolution is context-sensitive. Fixed-data word `0x00` should
not be treated as a direct ordinal into extracted GVR files by archive offset.
The checked direct draw path does not read that word at all; it receives
texture/material state through the active render context. The checked
descriptor/queue path preserves this word as a short and later passes it to
`FUN_80298fe4`, which indexes the currently active context entry list. This is
a runtime context-entry selector, not an extracted-GVR ordinal. The companion
bank can be identified from the consumer context for some files, but not from
the `.bin` payload alone. Parser/exporter UI should therefore support an
explicit companion-bank hint or sidecar when the runtime context is not known.

The current texture-resolution model is: fixed-data offsets `0x04..0x10` are
normalized UV/source coordinates, while the visible source texture is selected
through the active render/material context. Direct submitters bind texture state
through `FUN_80299040` and `FUN_80297dc8`; the latter resolves a material id in
the global material registry and uploads the registered texture descriptor. The
descriptor/queue submitter path also sets the active context through
`FUN_80299040`, then calls `FUN_80298fe4(fixedDataWord0)`; that helper reads
`activeContext[fixedDataWord0]`, follows the context entry to its registered
texture descriptor, and binds it with `FUN_8029976c`.
The common queued submitter wrappers `FUN_801d52a8` and `FUN_801d5210` prove
that many queued status draws use a different active context from the direct
`DAT_80347568` path: both load the context argument from
`DAT_80347614->+4->+0x20` before calling their lower submitters. `FUN_801d66d4`
initializes `DAT_80347614` by loading `/field/Sprite00.mld` and registering its
`TKStringList`; `FUN_801d668c` later unregisters/frees the same context. This
explains why queued `StaSprite00.bin` records, including compact numeric rows
such as `StaSprite00.bin 0x0c`, can be correctly identified by consumer
evidence while still previewing incorrectly against the default field/UI atlas
or the embedded `HrsBin_Status.mll` member-4 GVR list. A focused Sprite00
diagnostic now proves one concrete selector mapping: compact digit records
`StaSprite00.bin 0x05..0x0e` all use fixed-data word `1`, and their UV grid
renders as digits `0..9` against `/field/Sprite00.mld` texture entry `1`
(`ts000112`).
MLD/TK texture loads register GVR/GVRT payloads into that engine registry via
`FUN_80227af0`, `FUN_80227810`, and `FUN_80227d5c`. A descriptive previewer
therefore needs a material-context manifest mapping runtime material ids to
source texture assets. Extracted GCIX/GVRT order alone is not sufficient.
The registry itself is initialized by `FUN_80298544` as `DAT_80347ed4` plus
`DAT_80347ed0` entries with a 0x18-byte stride, and the direct binder compares
the requested material id against the descriptor pointer at registry entry
`+0x0c`. The initializer xref is `FUN_80299078`, which calls
`FUN_80298544(materialRegistry, materialCount)` and then initializes the
companion context-entry storage through `FUN_8029833c`. `FUN_80299078` is called
from startup `FUN_801dc62c` as
`FUN_80299078(DAT_803475ec, 0x400, DAT_803475e8, 0x400)` after allocating
0x6000 bytes for material entries and 0x12000 bytes for context-entry storage,
so these are global engine tables populated by texture registration over time.
Context entries are 0x0c-byte slots; `FUN_80298a04` links a context slot to a
material by storing `DAT_80347ed4 + materialIndex * 0x18` at context entry
`+0x08`, which is the pointer later read by `FUN_80298fe4(selector)`. A correct
preview manifest therefore needs two layers: `materialId/source texture ->
registry entry`, and `contextEntryIndex -> registry entry`. `DAT_80347568`, the
field/status active context pointer used by the checked direct draw helpers,
currently has only two observed writes and both set it to `&DAT_80311894`, the
default field/UI context tied to material id `0x1869e` and `/field/ts000110.gvr`.

Current high-confidence companion-bank examples:

| `.bin` payload | Companion bank | Evidence |
| --- | --- | --- |
| `battle/HrsBinPCWin.bin` | `battle/PCWindow.mld` | Battle preload setup initializes the PCWin indexed object from cache index 2 and loads `PCWindow.mld` from nearby cache index 3. JP `HrsBinPCWin.bin` records `1..6` align with playable-character name atlas rectangles when projected onto the JP PCWindow/HRSBin-style katakana texture: record `2` composes `アイカ` and record `3` composes `ファイナ`, both reusing the `イ` glyph from the top row. |
| `battle/HrsBinCW.bin` | `battle/command.mld`, possibly `battle/btlcursor.mld` | Battle preload setup loads `command.mld` immediately before initializing the CW indexed object, and the texture contents match battle command/menu UI art. |
| `field/hrs_bend.bin` | `field/ts000110.gvr` through the default field/UI material bank | Runtime draw helpers pass the default field/UI bank initialized from `ts000110.gvr`; visual inspection identifies the sheet as the battle-end sprite sheet. |
| `field/HrsBin_Status.mll` members `0..3` | Embedded member 4 `ts0009.mld` plus active field/menu texture state, with `/field/HrsBin_sbp.mll` preloaded and `/field/Sprite00.mld` loaded at menu open | `menu_listener@801920ac` loads `/field/HrsBin_Status.mll`; `FUN_801926b8` loads member 4 through `loadTextures_801db124` and initializes members `0..3` through `FUN_801d6998`; `FUN_801d66d4` then loads `/field/Sprite00.mld` and registers its `TKStringList`. The normal area-load state machine loads `/field/HrsBin_sbp.mll` via `FUN_8012a39c -> FUN_8012aa44 -> FUN_8012ab84` before `DAT_803473e8` is set and before the listener accepts X-menu input. The direct record draw path uses the current render context (`DAT_80347568`) rather than treating fixed-data word `0x00` as an ordinal into member 4's extracted GVR list. The queued wrapper paths through `FUN_801d52a8` and `FUN_801d5210` instead supply the active context from `DAT_80347614->+4->+0x20`, tying queued status records to the `/field/Sprite00.mld` context. `FUN_800ff348` render-queue copies are mode-sensitive: mode `0`/`1` entries drain through direct-context submitters, while other modes drain through queued selector submitters. Object payloads created with `FUN_8019a15c` use the same split: slot groups `0` and `1` are later drawn by `FUN_80199bc4` through direct wrapper `FUN_801d49e4` and `DAT_80347568`, slot group `2` is drawn through queued wrapper `FUN_801d5210`, and the primary `param[0x11]` record pointer is drawn through the direct wrapper. Current proven/visually supported mappings show checked Sprite00 selectors `0`, `1`, `4`, `5`, and `6` line up with same-numbered Sprite00 texture entries, with compact digit records `StaSprite00.bin 0x05..0x0e` proven as digits `0..9` on selector `1` (`ts000112`). |
| `field/wanted.mll` members `0..1` | Member 2 texture bank plus runtime poster MLD `field/wanted_%02d{a,b}.mld` | Wanted viewer state `19` calls `FUN_801866b4`, which loads `/field/wanted.mll`, registers member 2 through `loadTextures_801db124` and `FUN_80227810`, and initializes members `0` and `1` through `FUN_801d6998` into `DAT_803470dc` and `DAT_803470d8`. `FUN_80185c00` also loads and frees `field/wanted_%02d{a,b}.mld` as the current poster texture while the viewer is active. Record-level mapping for these two wanted `.bin` members is still pending. |
| Embedded HRSBin-style MLL members | Member/container-local MLL material bank | MLL payloads carry nearby or embedded texture payloads and are passed into the same indexed object initializer from member-local buffers. |

Current proven/visually supported Sprite00 context selector mappings for
`field/HrsBin_Status.mll` status records:

| Fixed-data word0 selector | `/field/Sprite00.mld` texture | Preview evidence |
| ---: | --- | --- |
| `0` | `ts000111` | Stat labels and companion stat fragments such as `Attack`, `Defense`, `Will`, `Quick`, `Power`, `Vigor`, `Spirit`, and `Agile`. |
| `1` | `ts000112` | Compact digit records `0x05..0x0e` render as `0..9`; also moon labels, selector strips, detail markers, and some option/value labels. |
| `4` | `ts000116` | Options choice panel dynamic bar fragments. |
| `5` | `ts000124` | `MAXHP`, `MAXMP`, `Rank`, `Next`, and `MAXSpirit` labels. |
| `6` | `ts000201` | Options choice panel active selector icon. |
| `4,5` | `ts000116` and `ts000124` | Multi-element `Limit`/ship weapon stat extra label record uses both selectors. |

The same selector rule also resolves `StaDeco.bin` wide digit records
`0x04..0x0d`: all use selector `4` and render as digits `0..9` on
`Sprite00.mld` texture `ts000116`.
The queued `StaDeco.bin` common frame/deco/accent records also resolve through
the Sprite00 context: records `0x00`, `0x01`, `0x02`, `0x03`, and `0x0e..0x11`
render coherently as frame/rule/portrait-deco/selector-accent art against
Sprite00 selectors `1..4`.

For the direct submitter path, the current audit proves 17 non-Sprite rows use
the default `/field/ts000110.gvr` context through immediate direct submitters,
9 more through `FUN_800ff348` render-queue copies that drain as direct mode
`0`/`1`, and 9 more through branch-selected direct draws. It also proves 53
non-Sprite rows use the same context through object slot groups `0` and `1`,
later drawn by `FUN_80199bc4` through `FUN_801d49e4`, plus 24 rows stored as
the object's primary `param[0x11]` record pointer and drawn by that same direct
renderer. These wrappers pass `&DAT_80347568` to their lower renderers; the
material initializer functions set `DAT_80347568 = &DAT_80311894` and register
material id `0x1869e` from `/field/ts000110.gvr`. The disassembly-level trace
of `FUN_8019ae84` resolves the remaining active selector pair: for selector
slot `0`, stack entry 2 is `StaPaper.bin 0x69` and is the direct pass; for
selector slot `1`, stack entry 3 is `StaPaper.bin 0x69` and is again the
direct pass. The paired `StaSprite00.bin 0x4f` icon stays on the Sprite00
selector context. The material-aware status sheet now has all 204 keyed rows
material-backed and no geometry-only fallbacks.

Supporting local artifacts are
`SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_context_mapping_audit.md`,
`status_preview_material_diagnostics/status_preview_context_mapping_audit.tsv`,
and
`status_preview_material_diagnostics/HrsBin_Status_material_aware_contact_sheet.png`.
The Sprite00 selector grouping sheet is
`status_preview_material_diagnostics/stasprite00_selector_context_groups.png`.
The direct-context candidate sheet for non-Sprite00 rows is
`status_preview_material_diagnostics/non_sprite00_ts000110_direct_context_candidates.png`.

Recommended exporter metadata shape:

```json
{
  "binFile": "battle/HrsBinPCWin.bin",
  "companionTextureBank": {
    "kind": "mld",
    "path": "battle/PCWindow.mld",
    "slotMapping": "engineMaterialSlots",
    "source": "knownRuntimeConsumer"
  }
}
```

The `slotMapping` distinction matters: current research has found cases where
extracted texture lists based only on GCIX/GVRT offsets do not match the
engine-facing material slot order. Until SpiceMLD/SpiceGvm expose the exact
engine slot list, editor previews should make the active companion-bank mapping
explicit and preserve unresolved material-slot values.

Companion-bank resolution should use the strongest available source, in this
order:

1. Known runtime consumer mapping from Ghidra traces.
2. Archive/container context, when the `.bin` is an embedded MLL member with a
   local material bank.
3. Explicit editor/exporter sidecar metadata supplied by the user or project.
4. Filename/path heuristics only as a low-confidence preview fallback.

Current `SpiceBin` parsing does not implement companion-bank metadata yet. It
preserves the raw material-slot value from the fixed-data table. A descriptive
editor/exporter should layer companion-bank metadata on top of the parsed `.bin`
instead of rewriting the `.bin` model to pretend the bank is self-contained.

## Known Families

### Indexed UI/Render Layout Tables

This family is validated by existing MLL documentation, the `SpiceBin` indexed
table probe, parser tests, planning-analysis notes, and Ghidra runtime traces.
The format is big-endian.

Runtime evidence now shows three entry points into the same in-memory indexed
object:

| Function | Source kind | Behavior |
| --- | --- | --- |
| `FUN_801d6998(object, payload)` | Already-loaded member payload | Initializes the 0x14 indexed object directly from a payload pointer. MLL callers use this after `FUN_801e4984` returns a member-local payload. |
| `FUN_801d6b58(object, path)` | Loose file path | Calls `loadFileFromPath(path)`, stores that loaded payload at object `+0x10`, then performs the same indexed-object initialization as `FUN_801d6998`. |
| `FUN_801d6a74(object, cacheIndex)` | Battle preload/cache index | Calls `FUN_8022aca8(cacheIndex)`, stores the returned battle-cache payload at object `+0x10`, then performs the same indexed-object initialization as `FUN_801d6998`. |

The shared runtime object layout is:

| Object offset | Size | Runtime field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `recordBase` | `payload + 0x04 + count * 4`. |
| `0x04` | 4 | `recordCount` | Top-level record count from payload `+0x00`. |
| `0x08` | 4 | `recordOffsets` | Pointer to the top-level offset table at payload `+0x04`. |
| `0x0C` | 4 | `recordScratch` | Allocated `recordCount * 4` scratch/working pointer table, zeroed by the initializer. |
| `0x10` | 4 | `payloadBase` | Loaded or member-local payload base. |

For every top-level record, both runtime initializers perform the same fixups:
record `+0x00` is overwritten with `recordBase`, and record `+0x04` is rebased
by adding `payloadBase`. Exporters should therefore write file-form offsets, not
runtime-fixed absolute pointers.

The top-level file shape is:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `count` | Number of top-level records. |
| `0x04` | `count * 4` | `recordOffsets` | Big-endian offsets relative to `dataBaseOffset`. |
| `0x04 + count * 4` | variable | `recordData` | Record data area. |

For record `i`, the record address is:

```text
dataBaseOffset = 0x04 + count * 4
recordOffset = dataBaseOffset + recordOffsets[i]
```

Known top-level record fields:

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `fixedDataTablePointer` | Fixed-data table pointer after runtime fixup. File-form samples commonly equal `dataBaseOffset` before fixup. |
| `0x04` | 4 | `elementTablePointer` | Element table pointer after runtime fixup. File-form samples point inside the payload before fixup. |
| `0x08` | 4 | `elementCount` | Number of 0x34-byte element records. |
| `0x0C` | 4 | `layoutWidth` | Big-endian float layout width or X extent. Name remains provisional. |
| `0x10` | 4 | `layoutHeight` | Big-endian float layout height or Y extent. Name remains provisional. |
| `0x14` | 4 | `baseX` | Base X or mutable layout X offset. |
| `0x18` | 4 | `baseY` | Base Y or mutable layout Y offset. |

Element records are currently understood as 0x34 bytes:

| Offset in element | Size | Field | Runtime use |
| --- | ---: | --- | --- |
| `0x00` | 4 | `fixedDataIndex` | Indexes a 0x14-byte fixed-data record through `fixedDataTablePointer + fixedDataIndex * 0x14`. |
| `0x04` | 4 | `dstLeft` | Destination quad left/local X. Added to caller/record base X before vertex submit. |
| `0x08` | 4 | `dstTop` | Destination quad top/local Y. Added to caller/record base Y before vertex submit. |
| `0x0C` | 4 | `dstRight` | Destination quad right/local X. Added to caller/record base X before vertex submit. |
| `0x10` | 4 | `dstBottom` | Destination quad bottom/local Y. Added to caller/record base Y before vertex submit. |
| `0x14` | 4 | `packedTint` | Treated as packed signed-byte tint/color/alpha channels and scaled by caller alpha/color factors before rendering. |
| `0x18..0x30` | 24 | `preservedTail` | Preserve-only opaque bytes. Checked PPC disassembly for the known render/descriptor paths uses serialized element fields only through `+0x14`; exporters must preserve the remaining bytes until each slot is proven. |

The current Ghidra drill-down checked the PPC disassembly around the known
`0x34` element loops, rather than relying only on decompiler output. No checked
serialized-element consumer reads `+0x18..+0x30`. This is not a proof that no
unidentified DOL function can ever inspect those bytes, so editors should expose
the range as raw preserved data only.

Do not confuse the serialized element unknown range with cloned runtime
draw/control objects. Several field-side helper functions operate on clone
objects that are larger than the serialized top-level record header:

| Function | Runtime clone fields | Meaning |
| --- | --- | --- |
| `FUN_801c3618(clone, a, b)` | Writes shorts at clone `+0x46` and `+0x44`. | Stores runtime state used by field-side update tests. |
| `FUN_801c3624(clone)` | Reads clone `+0x46`. | Returns whether that runtime state is non-zero. |
| `FUN_801c368c(x, y, clone, steps, state)` | Writes clone `+0x0C`, `+0x10`, `+0x14`, `+0x18`, `+0x42`, and `+0x48`. | Sets target extents/interpolation deltas and runtime state. |
| `FUN_801c377c(y, clone, steps, state)` | Writes clone `+0x10`, `+0x18`, `+0x0C`, `+0x14`, `+0x42`, and `+0x48`. | One-axis variant of the same clone interpolation setup. |
| `FUN_801c37d4(x, clone, steps, state)` | Writes clone `+0x0C`, `+0x14`, `+0x10`, `+0x18`, `+0x42`, and `+0x48`. | One-axis variant of the same clone interpolation setup. |

Those offsets are runtime clone/control fields. They should not be added to the
serialized 0x34-byte element schema.

Fixed-data records are currently understood as 0x14 bytes:

| Offset in fixed data | Size | Field | Runtime use |
| --- | ---: | --- | --- |
| `0x00` | 4 | `contextEntryIndex` / material selector | First fixed-data word. The checked direct submitter family (`FUN_801d4a2c`, `FUN_801d4c9c`, `FUN_801d4f0c` -> `FUN_801d4f54`) does not read this word; it receives texture/render state through the active context and reads fixed-data offsets `0x04..0x10` as UV/source coordinates. The descriptor submitter family (`FUN_801d52a8` -> `FUN_801d5a50`, plus `FUN_801d538c`, `FUN_801d56c8`, and `FUN_801d5dd8`) uses `FUN_801d61f4` to copy this word into a transient 0x14-byte descriptor as a short. `FUN_80005d98` later passes that short to `FUN_80298fe4`, which indexes the active context entry table and binds that entry's texture descriptor. Preserve it as an engine context selector; do not model it as the ordinal of an extracted GVR by file offset. |
| `0x04` | 4 | `sourceLeft` | Source/UV rectangle left coordinate. |
| `0x08` | 4 | `sourceTop` | Source/UV rectangle top coordinate. |
| `0x0C` | 4 | `sourceRight` | Source/UV rectangle right coordinate. |
| `0x10` | 4 | `sourceBottom` | Source/UV rectangle bottom coordinate. |

Ghidra consumer evidence confirms the high-level render use. `FUN_801d68c4`
returns a top-level record pointer from an initialized object. Direct draw
callers then iterate `record.elementCount`, read each 0x34-byte element, use
`element.fixedDataIndex` to select a fixed-data record, build four vertices from
`record.baseX/baseY` plus element destination coordinates, copy UVs from the
fixed-data rectangle at offsets `0x04..0x10`, scale `element.packedTint`, and
submit a four-vertex quad. In the checked direct submitter paths
(`FUN_801d4a2c`, `FUN_801d4c9c`, and `FUN_801d4f54`), texture selection is not
made from fixed-data word `0x00`; it comes from the active render context.

The descriptor-queue render helpers (`FUN_801d52a8` -> `FUN_801d5a50`,
`FUN_801d538c`, `FUN_801d56c8`, `FUN_801d5dd8`, and the shared descriptor
builder `FUN_801d61f4`) convert each element/fixed-data pair into 0x14-byte
transient render descriptors before drawing. Those descriptors contain element
width/height, half extents, scaled UV/source coordinates, and a short copied
from fixed-data word `0x00`. The draw consumer `FUN_80005d98` calls
`FUN_80299040(param_2[6])` to set the active context and
`FUN_80298fe4((int)psVar2[8])` to bind the descriptor selected by that copied
word. This independently supports the same interpretation: the `.bin` payload
is layout and texture-reference metadata, not image pixels. It also proves that
fixed-data word `0x00` is not sufficient without the runtime active context. In
direct submitter paths, the caller/context selects the material; in queued
submitter paths, fixed-data word `0x00` selects a context entry only after the
active context pointer has been set.

Known loose US files:

| File | Raw size | Decoded/scan size | AKLZ | Indexed probe |
| --- | ---: | ---: | --- | --- |
| `battle/HrsBinCW.bin` | 5233 | 21788 | yes | yes |
| `battle/HrsBinPCWin.bin` | 1606 | 5704 | yes | yes |
| `field/HRSBin.bin` | 1066 | 3876 | yes | yes |
| `field/hrs_bend.bin` | 4489 | 17508 | yes | yes |
| `field/wmaparea.BIN` | 2016 | 2016 | no | no |

Known embedded MLL `.bin` members include:

- `field/HrsBin_Hakken.mll`: `HrsBin_Hakken2.bin`
- `field/HrsBin_sbp.mll`: `HrsBin_sbp_hrs3.bin`
- `field/HrsBin_Status.mll`: `StaCard.bin`
- `field/HrsBin_Status.mll`: `StaDeco.bin`
- `field/HrsBin_Status.mll`: `StaPaper.bin`
- `field/HrsBin_Status.mll`: `StaSprite00.bin`
- `field/hrs_wmap.mll`: `hrswmap0.bin`
- `field/hrs_wmap.mll`: `hrswmap1.bin`
- `field/hrs_wmap.mll`: third indexed member loaded by the world-map setup
- `field/wanted.mll`: members 0 and 1, initialized by the wanted viewer into
  `DAT_803470dc` and `DAT_803470d8`

These are member-local payloads. MLL runtime evidence indicates selected member
payloads are copied into separate member-local buffers before they are passed to
MLD or indexed `.bin` handlers. Outer MLL member offsets should therefore not be
treated as part of the inner `.bin` address space.

### Regional Loose Corpus Validation

The US, EU, and JP loose-file scans all found the same family split: five loose
`.bin` files, four AKLZ-decoded indexed HRSBin-style positives, and one
non-indexed `field/wmaparea.BIN`. No decode errors or indexed-probe warnings
were reported.

| Region | File | Raw size | Decoded size | Indexed record count | Notes |
| --- | --- | ---: | ---: | ---: | --- |
| US | `battle/HrsBinCW.bin` | 5233 | 21788 | 104 | Battle command/window layout. |
| EU | `battle/HrsBinCW.bin` | 5281 | 21972 | 104 | Same record count, larger decoded data. |
| JP | `battle/HrsBinCW.bin` | 5157 | 21696 | 104 | Same record count, smaller decoded data. |
| US/EU | `battle/HrsBinPCWin.bin` | 1606 | 5704 | 41 | US and EU match in scan metadata. |
| JP | `battle/HrsBinPCWin.bin` | 1637 | 6032 | 41 | Same record count, larger decoded data and different offsets. |
| US/EU/JP | `field/HRSBin.bin` | 1066 | 3876 | 34 | Scan metadata matches across all three regions. |
| US/EU/JP | `field/hrs_bend.bin` | 4489 | 17508 | 63 | Scan metadata matches across all three regions. |
| US/EU | `field/wmaparea.BIN` | 2016 | 2016 | n/a | Non-indexed, uncompressed in US/EU. |
| JP | `field/wmaparea.BIN` | 454 | 2016 | n/a | Non-indexed after AKLZ decode. |

Exporter implication: record counts and the family split are stable across
regions, but offsets, decoded sizes, and data content can differ. Preserve
region-specific data rather than normalizing to US.

Known runtime path references:

- MLL embedded payloads use `FUN_801d6998` through paths such as
  `/field/HrsBin_Hakken.mll`, `/field/hrs_wmap.mll`,
  `/field/HrsBin_sbp.mll`, `/field/HrsBin_Status.mll`, and
  `/field/wanted.mll`.
- The field X-menu controller is `menu_listener@801920ac`. It opens on
  `RawControllers[DAT_80347154]->newPresses & 0x400`, loads
  `/field/HrsBin_Status.mll`, calls `FUN_801926b8`, and creates child callback
  `FUN_80191ec8`. `FUN_801926b8` loads member 4 (`ts0009.mld`) as the
  texture/model bank and initializes members 0 (`StaCard.bin`), 1
  (`StaDeco.bin`), 2 (`StaPaper.bin`), and 3 (`StaSprite00.bin`) as indexed
  layout objects. It then calls `FUN_801d66d4`, which loads
  `/field/Sprite00.mld` and registers that MLD's `TKStringList`. The root state
  callback `FUN_8018394c` routes selected values
  `0..3` to party/status state `3`, selected value `4` to ship-equipment state
  `0xb`, and selected value `5` to More-menu state `2`; the More-menu
  dispatcher `FUN_801851c8` routes value `2` to wanted viewer state `0x13`,
  which loads `/field/wanted.mll`, initializes its member 0 and 1 indexed BIN
  objects, registers member 2 as a texture bank, and dynamically loads
  `field/wanted_%02d{a,b}.mld` for the current poster texture.
- The state callback dispatcher `FUN_80195f24` calls common renderer
  `FUN_80195f9c`, then dispatches `PTR_LAB_802ebbb4[DAT_80347110]`. Current
  callback-table and write audits support normal reachable X-menu states
  `0..19`; entry `20` is null. Wider table-slice entries after the null are
  adjacent selector/control helper code, not proven normal menu states. A
  bounded transitive call audit found state-specific material changes in state
  `19` (`FUN_80185c00`, wanted viewer) and options states `16/17`. The wanted
  viewer performs dynamic MLD/MLL asset setup; options helper calls
  `FUN_800c8ec0` / `FUN_800c8f3c` register or unregister the `TKStringList` for
  stage worksheet target `29999` when that worksheet is present, but current
  evidence does not show a new `.bin` object initialized there. A deeper audit
  also found that common-renderer numeric fields call
  `FUN_80195f9c -> FUN_8019a374 -> FUN_8019aa18 -> FUN_801d5210`, while the
  common queued submitter wrapper `FUN_801d52a8` follows the same
  `DAT_80347614->+4->+0x20` context path. This is the already-loaded
  `/field/Sprite00.mld` context, not another state-specific asset family.
- A whole-program xref audit of `FUN_801d68c4` found 569 status-bank accessor
  calls. Local PowerPC argument backtracking resolved every status-bank record
  index and found the same 204 unique reached records as the focused pass: 12
  `StaCard`, 18 `StaDeco`, 101 `StaPaper`, and 73 `StaSprite00` records. Every
  reached status-bank record now has a promoted one-to-one row in
  `Docs/BinFileRecords.md`.
- Direct non-accessor references to the four field status globals are limited to
  lifecycle functions: `FUN_801926b8` initializes the MLL member banks and
  `FUN_801925d8` frees/clears them. No alternate consumer has been found for
  the remaining serialized-but-unmapped status records in current Ghidra
  evidence. `Docs/BinFileRecords.md` lists those records explicitly as
  serialized records with no discovered consumer so exporters can preserve them
  without assigning misleading semantic keys.
- A fresh structural scan of the known indexed-object entry points did not find
  a loose `field/HRSBin.bin` consumer. `FUN_801d6998`, the embedded/member
  initializer, has 19 current xrefs and covers the known MLL member callers.
  `FUN_801d6b58`, the standalone loose-file wrapper, has one current xref:
  `FUN_800e3594` for `/field/HRS_BEND.BIN`. The same scan found no DOL string
  for `HRSBin.bin` or `HRSBin.mld`.
- A lower-level direct caller audit of `loadFileFromPath@801cc3c4` exported 17
  callsites in 13 unique caller functions. It found direct field loads for
  `/field/ts000110.gvr`, `/field/wmaparea.bin`, and `/field/wmaparea.BIN`,
  plus one constructed field path for `sr_%03d%c.tec`. It did not find a direct
  loose `field/HRSBin.bin` or `field/HRSBin.mld` load path.
- The loose `field/hrs_bend.bin` path appears in the executable as
  `/field/HRS_BEND.BIN` and uses `FUN_801d6b58(DAT_80346dcc + 0x15f8, path)`,
  the loose-file wrapper around the same indexed-object initializer. Field-side
  functions then use that indexed object as a template source for runtime
  clone/control records.
- `field/wmaparea.BIN` appears through `/field/wmaparea.bin` and
  `/field/wmaparea.BIN`, but those callers use `loadFileFromPath` and direct
  copies/state setup, not the indexed-object initializers. `FUN_800ee51c`
  copies exactly `0x7e0` bytes into `DAT_80346e14 + 0x47c`; `FUN_801a686c`
  stores the loaded pointer at `*(param_1 + 0x24) + 0x204`; `FUN_800ed8c4`
  maps current sky-map/player-position state to an area id.
- The battle strings `/battle/hrsbincw.bin` and `/battle/hrsbinpcwin.bin` are
  entries 1 and 2 in the 16-entry battle preload/cache table rooted at
  `PTR_s__battle_command_mld_802f91f8`. `FUN_8022a854` iterates that table,
  opens each path, copies each loaded file into ARAM-backed cache state, and
  `FUN_8022abec`/`FUN_8022aca8` later resolve requested battle resource names
  back to cached RAM buffers. `FUN_801d67dc` initializes `DAT_80347628` from
  cache index 1 with `FUN_801d6a74`, so `DAT_80347628` is the indexed object for
  `battle/HrsBinCW.bin`. It initializes `DAT_8034762c` from cache index 2 with
  `FUN_801d6a74`, so `DAT_8034762c` is the indexed object for
  `battle/HrsBinPCWin.bin`. The same function also loads `DAT_8034760c` from
  cache index 0 (`/battle/command.mld`) and `DAT_80347610` from cache index 3
  (`/battle/pcwindow.mld`) as the nearby texture/model companions.
- `field/HRSBin.bin` exists in the US field directory and probes positive as an
  indexed layout after AKLZ decode, but current Ghidra searches do not prove a
  direct DOL string or runtime loader path for `/field/HRSBin.bin`. The latest
  focused decompile-term search found 3 `HrsBin*` loader matches:
  `/field/HrsBin_Hakken.mll`, `/field/HrsBin_sbp.mll`, and
  `/field/HrsBin_Status.mll`; it found 0 matches for `HRSBin.bin` and
  `HRSBin.mld`. The follow-up `loadFileFromPath` caller audit likewise found
  no direct loose `HRSBin.bin` or `HRSBin.mld` load.

Current usage hypotheses:

`Docs/BinFileRecords.md` is the record-identity authority for assessed files.
Rows without promoted one-to-one keys should remain explicitly unmapped or
provisional until supported by consumer evidence, runtime screenshots, or proof
that the serialized record is unused.

| File or family | Hypothesized purpose |
| --- | --- |
| HRSBin-style indexed layout files | UI/render layout elements. |
| `field/HrsBin_Status.mll` embedded members | Confirmed field X-menu/status-menu indexed layout banks. Members 0..3 are `StaCard.bin`, `StaDeco.bin`, `StaPaper.bin`, and `StaSprite00.bin`; member 4 `ts0009.mld` is loaded with the menu. Normal field entry loads `/field/HrsBin_sbp.mll` before the X-menu can open, so many common records likely depend on already-active field/menu texture state rather than a direct member-4 GVR ordinal. The wanted-list route reached through More-menu state `2` additionally loads `field/wanted_%02d{a,b}.mld` at runtime for poster/viewer assets. |
| `field/HRSBin.bin` | Loose 34-record HRSBin-style layout with identified companion `field/HRSBin.mld`; not currently proven to be the field X-menu record bank. |
| `battle/HrsBinCW.bin` | Confirmed battle command/menu/window UI layout: command windows, list rows, category/weapon/item/spell icons, cursor/arrow pieces, and party selection/status panels. Exact record names remain provisional. |
| `battle/HrsBinPCWin.bin` | Confirmed battle player-character status window layout: base panel, character-specific name/portrait pieces, Lv/HP/MP stat rows, dynamic HP gauge, runtime-colored element marker, and status-condition icons. The action-select battle menu thread toggles current-character vs all-character display through worksheet byte 0. Record names are promoted in `BinFileRecords.md`. |
| `field/hrs_bend.bin` | Confirmed battle-end result screen indexed UI layout. Draw-side evidence routes its quads through the default field/UI material bank initialized from `field/ts000110.gvr`, and visual inspection identifies that texture as the battle-end sprite sheet. Promoted record names live in `Docs/BinFileRecords.md` and cover the main backdrop, EXP/stat/magic result rows, level/rank-up badges, moon icons, and dropped-item popup pieces. |
| `field/wmaparea.BIN` | World-map area/menu table tied to world-map flow. Separate fixed-size table family from HRSBin-style indexed files. |

### Field X-Menu Root Controller

The initial field X-menu selector/stat carousel is controlled by
`FUN_80183d90`. The callback checks controller code `7` for rightward carousel
movement and controller code `6` for leftward movement. The current carousel
slot is stored in the root object field `+0x44`; the previous slot is copied to
`+0x48` before animation. Code `7` increments `+0x44` and wraps `5 -> 0`, while
code `6` decrements it and wraps `-1 -> 4`. The same input updates bottom
page-toggle field `+0x14` with a two-state wrap. The five root stat windows are
therefore a fixed `0..4` loop:

| Slot | Root stat-window role |
| ---: | --- |
| `0` | Attack, Defense, Will, Magic Defense |
| `1` | Hit %, Dodge %, Quick |
| `2` | Weapon and armor equipment |
| `3` | Accessory equipment |
| `4` | Total EXP and next/needed EXP |

`FUN_80199024` allocates these five child objects for each active party member,
and `FUN_8018c00c` installs the per-slot record payloads. The promoted
record-level keys and object ranges live in `Docs/BinFileRecords.md`.

The root selector's accept path is separate from the left/right carousel logic.
In `FUN_8018394c`, selected values `0..3` enter the party/status page hub
(`DAT_80347110 = 3`), selected value `4` enters the ship-equipment chooser
(`DAT_80347110 = 0xb`), and selected value `5` enters the second-page More menu
(`DAT_80347110 = 2`). More-menu dispatcher `FUN_801851c8` then maps values
`0..4` to options/settings state `15`, crew assignment state `13`, wanted viewer
state `19`, options description/apply state `18`, and back-to-root state `1`.
Route-level evidence is summarized in
`SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/xmenu_route_and_asset_trace.md`.
The wanted viewer path is the only currently identified X-menu subpath that
loads a new MLD after the status menu is open: `FUN_80185c00` formats
`field/wanted_%02d{a,b}.mld`, loads it with `FUN_801db244`, registers its
`TKStringList` through `FUN_80227810`, and frees that MLD on exit.

### Battle Command Wheel

The dynamic rotating battle command wheel is now traced to `FUN_80025624`, the
top battle command-menu display/animation child callback. This callback consumes
`DAT_80347628`, the indexed object initialized from `battle/HrsBinCW.bin`, and
clones records through `FUN_801d68c4` and `FUN_801d497c`.

The command wheel's visual pieces are serialized in `HrsBinCW.bin`, but the
short rotation animation is runtime code, not a separate serialized animation
table. The transition is driven by:

| Runtime field/global | Meaning |
| --- | --- |
| command worksheet `field_0x5` | Signed selected command index. Normal values are `0..6`; `FUN_80025624` writes `-1` during the transition so input is ignored. |
| command worksheet `field_0x6` | Movement request written by the input controller and consumed by `FUN_80025624`. |
| `80346b40` | Transition direction delta, `-1`, `0`, or `+1`. |
| `80346b3c` | Transition countdown, initialized to `3` and decremented each callback pass. |
| `80346b4c` | Old selected command index captured when transition starts. |
| `80302a00` | Temporary float position table for the seven orbiting command slots. |

During a transition, `FUN_80025624` draws both the primary selected command
record and an adjacent old/next selected command record with separate alpha
factors. When the countdown reaches zero, it commits
`field_0x5 = oldIndex + direction`, wraps into `0..6`, and clears
`80346b40`.

The battle command id order is:

| Command id | UI command | Normal icon record | Selected icon record | Text label record | Key material slots |
| ---: | --- | ---: | ---: | ---: | --- |
| 0 | Focus | `0x0d` | `0x17` | `0x34` | icon slot `22`, selected background slot `6`, text slot `15` |
| 1 | Magic | `0x0f` | `0x19` | `0x36` | icon slot `24`, selected background slot `6`, text slot `15` |
| 2 | S-Move | `0x11` | `0x1b` | `0x38` | icon slot `26`, selected background slot `6`, text slot `15` |
| 3 | Attack | `0x0b` | `0x15` | `0x32` | icon slot `20`, selected background slot `6`, text slot `15` |
| 4 | Block/Guard | `0x0c` | `0x16` | `0x33` | icon slot `21`, selected background slot `6`, text slot `15` |
| 5 | Item | `0x0e` | `0x18` | `0x35` | icon slot `23`, selected background slot `6`, text slot `15` |
| 6 | Run | `0x10` | `0x1a` | `0x37` | icon slot `25`, selected background slot `6`, text slot `15` |
| 6 alt | Crew/alternate Run state | n/a | `0x1c` | `0x39` | icon slot `19`, selected background slot `6`, text slot `15` |

`FUN_80026b20` selects the highlighted records. The alternate command-id `6`
path is used when `DAT_80346b98 == 1` and worksheet `field_0x8 == 0`; the text
source rectangles line up with the `Crew` label on the `command.mld` text sheet.

Exporter/editor implication: expose these records as command-wheel parts when
the active context is `battle/HrsBinCW.bin`, but keep the transition countdown,
direction, and alpha layering as runtime behavior rather than serialized `.bin`
fields.

### Battle Command/Menu Record Identity

Editor/exporter implication: do not advertise `HrsBinCW.bin` as only a command
wheel layout. It should be presented as the battle command/menu layout bank,
with the exact command-wheel records named, the other high-confidence groups
named by group role, and the remaining category/list markers kept provisional
until the worksheet/list producer is traced and labeled.

A full one-to-one draft mapping now exists under the ignored research folder:
`SpiceBin/research/2026-06-20_hrsbin_cw_record_mapping/HrsBinCW_record_mapping_draft.md`.
It assigns a unique key to all 104 records and carries a confidence flag for
each name. After the 2026-06-20 Ghidra follow-up, the draft has no unmapped or
low-confidence records: 50 rows are high confidence and 54 are medium
confidence.

Promoted record groups and one-to-one record annotations live in
`Docs/BinFileRecords.md`. Keep this file focused on binary layout, runtime use,
and companion assets; use the records document for per-file record grouping,
record naming, and human annotations.

Current companion texture evidence:

| `.bin` or family | Embedded GVRs in decoded `.bin` | Likely companion texture asset(s) | Companion texture evidence |
| --- | ---: | --- | --- |
| `field/HRSBin.bin` | 0 | `field/HRSBin.mld` | 17 parsed GCIX/GVRT textures: mostly 32x32 CMPR plus 64x64 and 128x128 RGB5A3 UI sheets/icons. |
| `battle/HrsBinCW.bin` | 0 | `battle/command.mld`, possibly `battle/btlcursor.mld` | `FUN_801d67dc` loads `command.mld` immediately before initializing the CW indexed object. `command.mld` has 31 parsed GCIX/GVRT textures with battle command labels, icons, numbers, windows, and cursor/menu pieces; `btlcursor.mld` has 3 cursor-effect textures. |
| `battle/HrsBinPCWin.bin` | 0 | `battle/PCWindow.mld` | `FUN_801d67dc` loads `PCWindow.mld` immediately after initializing the PCWin indexed object. `PCWindow.mld` has 23 parsed GCIX/GVRT textures with player names, portraits, HP/MP text, arrows, skull/status icons, and window pieces. |
| `field/HrsBin_Status.mll` embedded indexed members | Member-local | Member 4 `ts0009.mld` plus active `DAT_80347568` field/menu texture context; `/field/HrsBin_sbp.mll` is normally preloaded; `/field/Sprite00.mld` is loaded at menu open; wanted viewer branch loads `/field/wanted.mll` and `field/wanted_%02d{a,b}.mld` | Confirmed field X-menu record banks. Member 4 contains 22 parsed 64x64 CMPR GVR textures that match portrait-style records, but many `StaCard`, `StaPaper`, and `StaSprite00` rows rely on the active render context instead. The checked direct draw path receives `&DAT_80347568`, whose current ref dump shows assignment to the default field texture object `DAT_80311894`; do not render these rows by treating fixed-data word `0x00` as the ordinal of the extracted member-4 GVR list. The normal area-load flow sets `DAT_803473e8 = 1` only after `FUN_8012ab84` has loaded `HrsBin_sbp.mll`, and the listener requires that flag before accepting X-menu input. More-menu value `2` enters state `19`, where `FUN_80185c00` and helper `FUN_801866b4` load and later free wanted-viewer `.mll`/`.mld` assets. Queued status records route through `FUN_801d52a8`/`FUN_801d5210` and use the `/field/Sprite00.mld` context at `DAT_80347614->+4->+0x20`. |
| Other embedded HRSBin-style members in MLLs | Member-specific | `field/HrsBin_Hakken.mll`, `field/HrsBin_sbp.mll` | These MLLs contain parsed GVR texture sets matching discovery/status/battle-style UI art. |
| `field/hrs_bend.bin` | 0 | `field/ts000110.gvr` | `FUN_800e3594` loads `/field/HRS_BEND.BIN` into `DAT_80346dcc + 0x15f8`. Its HRS draw helpers call `FUN_801d4f0c`, which passes `&DAT_80347568` to the lower quad renderer. `FUN_80227068` initializes `DAT_80347568` from `/field/ts000110.gvr` with texture/material id `0x1869e`. The US corpus file decodes as one AKLZ-wrapped `1024x512` RGB5A3 sheet, visually identified as the battle-end sprite sheet. |
| `field/wmaparea.BIN` | 0 | World-map state/assets, not HRSBin-style textures | Does not use the HRSBin-style indexed layout probe. It is a fixed 0x7e0-byte world-map area table, not a texture layout table. |

## Research Workflow

1. Copy raw `.bin` samples into `SpiceBin/research/` or another ignored evidence folder.
2. Keep source provenance beside the sample: region, disc path, archive/member name, extraction command, and timestamp.
3. Record tentative offsets and field interpretations in local research notes until they have been validated against multiple files.
4. Promote stable conclusions into this document with enough context to reproduce the interpretation.
5. Validate US files first when correlating with Ghidra, because the repo-local Ghidra project is based on the US `main.dol`; compare EU and JP files afterward and document deltas.

## C++ Usage

Include the project umbrella header:

```cpp
#include "SpiceBin/SpiceBin.h"
```

Parse a file from disk:

```cpp
const spice::bin::BinFile bin = spice::bin::parseFile(path);
if (!bin.ok()) {
    for (const spice::bin::BinDiagnostic& diagnostic : bin.diagnostics) {
        // Inspect diagnostic.severity and diagnostic.message.
    }
}
```

Parse bytes that were already extracted from an archive:

```cpp
std::vector<std::uint8_t> payload = ...;
spice::bin::BinFile bin = spice::bin::parseBytes(std::move(payload), "archive.mll/member.bin");
```

Inspect only the indexed-table probe without taking ownership of the bytes:

```cpp
std::span<const std::uint8_t> payload = ...;
spice::bin::BinIndexedTableProbe probe = spice::bin::probeIndexedTable(payload);
```

`BinFile::indexedTableProbe` stores the same probe result after `parseBytes` or
`parseFile`. Archive parsers such as `SpiceMll` should use `SpiceBin` for this
inner payload classification instead of duplicating `.bin` layout logic.

Scan a loose `.bin` corpus from the command line:

```text
SpiceBinCorpusScan <input_file_or_dir> <output_dir>
```

The scanner accepts a single `.bin` file or a directory tree, decodes AKLZ-wrapped
inputs before probing, and writes:

- `bin_corpus_files.csv`
- `bin_corpus_indexed_tables.csv`

## Known Fields

The indexed UI/render layout table field map above is the current promoted
field map. Names for `layoutWidth`, `layoutHeight`, `baseX`, and `baseY` remain
provisional until deeper consumer naming confirms whether they should be called
dimensions, clipping extents, or mutable layout offsets.

## Open Questions

- What should this family be named in `SpiceBin`: `HrsBin`, `IndexedBin`, or a more domain-specific UI/layout name?
- Which standalone and embedded `.bin` payloads should be first-class fixtures?
- What is the exact per-entry schema of `field/wmaparea.BIN`, which is not this
  indexed layout family and is consumed as a raw fixed-size world-map area/menu
  table?
- Should the high-confidence record keys currently promoted in
  `Docs/BinFileRecords.md` become stable public exporter field names, or should
  `SpiceBin` add a separate versioned key/alias layer before editor exposure?
- Is `field/HRSBin.bin` unused/leftover, loaded by a constructed path, loaded
  from script/archive data, or referenced indirectly by a system that does not
  retain the literal path in the DOL?
- Can runtime file-open instrumentation or a script/archive resource-name
  source audit identify any consumer for loose `field/HRSBin.bin`, now that
  static Ghidra, direct `loadFileFromPath`, indexed-wrapper, and US corpus
  searches do not show one?
- How are fixed-data texture indices mapped to companion MLD/MLL texture slots
  in each runtime context?
- For regional exporter validation, which differences are pure content changes
  and which, if any, require region-specific semantic labels?
- Which indexed `.bin` member subformats deserve exporters rather than raw preservation?
