# SST/SML File Layout

This document captures the current working reference for Skies of Arcadia
Legends battle `.sst` and `.sml` files. It is intentionally conservative:
layout fields are named only when Ghidra evidence, corpus validation, or current
parser behavior support the name. Unknown fields should remain preserved as raw
data until direct game-side usage is understood.

Primary implementation references:

- `SpiceSstSml/SmlParser.cpp`
- `SpiceSstSml/SstParser.cpp`
- `SpiceSstSml/BattleStageParser.cpp`
- `SpiceSstSml/SstSmlModel.h`
- `SpiceTests/test_sst_sml_parser.cpp`
- local-only research tools under ignored `SpiceSstSml/research/`

Primary research references:

Local-only analysis folders under `planning/Analysis/` are ignored by Git, but
they are listed here because they are the durable workspace summaries for this
research thread.

- `planning/Analysis/2026-06-11_sst_sml_ghidra_handlers`
- `planning/Analysis/2026-06-12_sst_payload_schema`
- `planning/Analysis/2026-06-12_sst_sml_joined_schema`
- `planning/Analysis/2026-06-12_sst_sml_command_payload_schema`
- `planning/Analysis/2026-06-12_sst_sml_command_payload_semantics`
- `planning/Analysis/2026-06-12_sst_type0_payload_semantics`
- `planning/Analysis/2026-06-12_sst_type0_runtime_consumers`
- `planning/Analysis/2026-06-12_sst_type0_class_callbacks`
- `planning/Analysis/2026-06-12_sst_type0_selector_source_audit`
- `planning/Analysis/2026-06-12_sst_type8_type9_semantics`
- `planning/Analysis/2026-06-12_sst_type8_type9_runtime_consumers`
- `planning/Analysis/2026-06-12_sst_type3_type4_type10_runtime_consumers`
- `planning/Analysis/2026-06-12_sst_type2_runtime_consumers`
- `planning/Analysis/2026-06-12_sst_type2_runtime_structure`
- `planning/Analysis/2026-06-12_sst_type11_boundary`
- `planning/Analysis/2026-06-12_sst_type11_runtime_structure`
- `planning/Analysis/2026-06-12_sst_type6_type7_code_paths`
- `planning/Analysis/2026-06-12_sml_runtime_table_trace`
- `planning/Analysis/2026-06-12_sst_type1_lighting_helpers`
- `planning/Analysis/2026-06-13_s062_sml_overlay_geometry`
- `planning/Analysis/2026-06-13_sst_battle_view_record_fields`
- `planning/Analysis/2026-06-14_sst_sml_post_refresh_symbol_map`
- `planning/Analysis/2026-06-14_stageworksheet_statebucket_trace`
- `planning/Analysis/2026-06-14_stageworksheet_state_callbacks`
- `planning/Analysis/2026-06-14_stageworksheet_bridge_8011622c`
- `planning/Analysis/2026-06-14_sst_sml_resource_to_model_row_binding`
- `planning/Analysis/2026-06-14_sst_sml_unresolved_runtime_links`
- `planning/Analysis/2026-06-14_sst_type1_type3_drilldown`
- `planning/Analysis/2026-06-14_sst_shared_model_data_helpers`
- `planning/Analysis/2026-06-14_sst_type2_writeback_path`
- `<local-analysis>\Analyses\20260611_1448_battle_ui`
- `<local-analysis>\Analyses\20260611_0843_script_inst_ops`

## Scope

`.sml` and `.sst` are paired battle-stage files under `battle/`, normally named
`sNNN.sml` and `sNNN.sst`. They should be parsed together when higher-level
meaning is needed:

- `.sml` owns a table of embedded MLD payloads.
- `.sst` owns per-stage command blocks that reference runtime objects/models.
- `FUN_8000cb44` walks both files by the same top-level record index.

Treat these files as battle-stage data first. Most command consumers live inside
the battle thread, battle camera/view, combatant worksheet, lighting, and
runtime model-object systems. Those battle-system paths are therefore expected
primary context for naming fields; they should not be dismissed as unrelated
just because they are not generic file-loader code. Conversely, do not apply
field names from non-battle systems unless direct dataflow shows the SST/SML
values reaching them.

`SpiceSstSml` currently provides read-only parsing only. `SpiceFileParsing`
has explicit research export flags for extracting embedded SML MLD payloads,
emitting per-entry or combined Blender IR, and writing SST/SML command maps. It
does not repack these formats or visualize SST command effects in Blender IR.
The combined SML Blender IR path applies same-index SST type `0` translation and
scale to each embedded entry's Blender IR instance transform when a same-stem
SST is present, because that view best matches in-game stage placement. Raw
embedded MLD export remains unchanged, and the old SML-only combined placement
can be requested with `--export-sml-combined-blender-ir-raw-placement`. Type `0`
rotation words are reported in diagnostics rather than applied because the
current US/EU/JP battle-stage corpus has all-zero rotation words and their angle
units remain provisional.

## Compression and Endian

Most corpus files are AKLZ-wrapped. Current parser code detects AKLZ, parses the
decompressed payload, and records whether the input was compressed.

All currently modeled numeric fields are big-endian.

The known battle corpus has one raw/not-AKLZ sample in prior probes (`s006` in
the original EU sample set); the parser accepts both raw and AKLZ-wrapped input.

## Region Policy

Use US files first for Ghidra-correlated schema work because the local Ghidra
project is based on `US_jahorta_main.dol`:

- US primary: `<local-corpus>\2002-12-19-gc-us-final_Skies_of_Arcadia_Legends`
- EU compatibility: `<local-corpus>\2003-03-05-gc-eu-final_Skies_of_Arcadia_Legends`
- JP compatibility: `<local-corpus>\2002-11-12-gc-jp-final_Eternal_Arcadia_Legends`

Current top-level layout, command-block layout, command payload spans, and
runtime-slot candidate fields are stable across US, EU, and JP in the local
battle corpora.

## Game-Side Load Path

Evidence level: `US+Gekko`

Important functions:

| Function | Address | Current role |
| --- | ---: | --- |
| `SetupBattle_8000abcc` | `8000abcc` | Builds `s%03d.sml` and `s%03d.sst` paths. |
| `Battle::Setup::loadBattleStageFiles` | `8000c9f8` | Loads SML into the first buffer and SST into the second. |
| `Battle::Stage::JoinSmlSstRecords_8000cb44` | `8000cb44` | Joins SML and SST by top-level record index. |
| `FUN_8000c8ac` | `8000c8ac` | Consumes one SML record and copies its embedded MLD payload. |
| `SST::Command::WalkBlocks_8000c7c0` | `8000c7c0` | Walks SST command blocks and patches command payload pointers. |
| `SST::Command::Dispatch_8000c19c` | `8000c19c` | Dispatches most patched SST command records. |
| `SST::Command::Type0::SetupRuntimeRow_8000bb28` | `8000bb28` | Downstream type `0` runtime-row setup; first confirmed semantic reads of copied type `0` data. |
| `FUN_8006b774` | `8006b774` | Type `1` setup path that populates the four-slot runtime table used by later model-index commands. |
| `FUN_8006bdb4` | `8006bdb4` | Creates the type `1` child/menu object and copies the `0x68` type `1` row into child-local data. |
| `FUN_8006b6f4` | `8006b6f4` | Reads one entry from the four-slot runtime table at `DAT_80309e88`. |
| `FUN_8006cf54` | `8006cf54` | Initializes model blocks inside a loaded MLD resource. |

The 2026-06-14 refresh from the remote Ghidra project adds source-of-truth
namespaces for the central SST/SML cluster. The focused namespace export at
`tools/ghidra/analyses/20260614_sst_sml_post_refresh_symbol_map/` found:

| Namespace prefix | Matching rows | SST/SML relevance |
| --- | ---: | --- |
| `SST` | `3` | `SST::Command` walker/dispatcher and type `0` setup. |
| `Battle::Stage` | `1` | Same-index SML/SST join. |
| `Battle::View` | `8` | Type `9` battle-view orientation source path. |
| `Battle::Action` | `1` | Battle action caller for view records. |
| `Battle::Turn` | `1` | Turn/action worksheet caller for view records. |
| `Battle::Run` | `13` | Battle thread/run loop context. |
| `StageWorksheet` | `2` | New trace anchor for stage state buckets. |
| `SceneTable` | `58` | Scene-table comparison anchors; not currently direct SST type `0` evidence. |
| `MLD` | `188` | Embedded SML MLD payload/runtime-resource context. |

`Battle::Stage::JoinSmlSstRecords_8000cb44` reads the SML record count from SML
`+0x04`, patches SML record `+0x04` by adding the SML base address, then calls
`FUN_8000c8ac`.

For SST, the same function patches each top-level SST record `+0x0c` by adding
the SST base address, then passes the command block to
`SST::Command::WalkBlocks_8000c7c0`.

## SML Layout

Evidence level: `US+Gekko`, `US/EU/JP stable`

SML starts with a small header, then a `0x10`-byte record table at file offset
`0x08`.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `rawHeader0` | Preserved raw. In samples this encodes the stage id in the high halfword and `0xffff` in the low halfword. |
| `0x04` | 4 | `recordCountWord` | High halfword matches the top-level record count; low halfword is commonly `0xffff` in samples. |
| `0x08` | `recordCount * 0x10` | `records` | SML record table. |

Current parser note: `SpiceSstSml` exposes `recordCount` from this word for the
confirmed SML table walk. Keep `rawHeader0` and record raw fields intact.

### SML Record

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `embeddedMldResourceIndex` | Low signed halfword is used as the second number in generated `s%02d%02d.mld` resource names. It equals the record index in every US/EU/JP battle row checked. High halfword is `0`. |
| `0x04` | 4 | `embeddedMldOffset` | Base-relative file offset of embedded MLD payload. Patched to a runtime pointer by the game. |
| `0x08` | 4 | `embeddedMldSize` | Embedded MLD payload byte size. |
| `0x0C` | 4 | `reservedSentinel` | Always `0xffffffff` in the current US/EU/JP battle corpora; no direct read in the audited load path. Preserve raw. |

`FUN_8000c8ac` formats `s%02d%02d.mld`, allocates `embeddedMldSize` bytes,
copies from `embeddedMldOffset`, and passes the copied payload to
`loadTextures_801db124` as MLD data.

Direct Gekko evidence:

- `8000c8d4`: reads SML record `+0x00`.
- `8000c8e0`: sign-extends the low halfword from that word.
- `8000c8e8`: passes it to `sprintf("s%02d%02d.mld", stageId, recordValue)`.
- `8000c900`, `8000c908`, and `8000c910`: read `+0x08`, patched `+0x04`, and
  `+0x08` for allocation and copy.
- no direct read of record `+0x0c` was found in `FUN_8000cb44` or
  `FUN_8000c8ac`.

Across US, EU, and JP:

- embedded MLD payloads per region: `1285`
- embedded MLD size range: `384..1052140`
- unique embedded MLD hashes: `922`
- out-of-bounds embedded MLD spans: `0`
- SML record `+0x00` equals the record index in `1285/1285` rows per region
- SML record `+0x0c` is `0xffffffff` in `1285/1285` rows per region

### Embedded MLD Payloads

Evidence level: `US corpus`, `US/EU/JP stable`; parser smoke test confirms
`SpiceMLD` can parse extracted embedded payloads. Treat this as embedded-payload
evidence, not evidence that a whole `.sml` file is itself an `.mld`.

Each SML record points at one embedded MLD-like payload. A whole `.sml` file is
an SML wrapper/table and does not parse as an MLD when merely renamed to
`.mld`; a smoke test with `s001.sml` produced `entry_count: 0` through the MLD
entry-list path. Extracting `s001` record `0`'s embedded payload and parsing it
as `.mld` produced an intelligible one-entry MLD result:

```json
{
  "entry_count": 1,
  "function": "s0100.nj",
  "object_count": 1,
  "ground_count": 0,
  "motion_count": 0,
  "function_parameters": [0],
  "object_addresses": [192],
  "textures_pointer": 156
}
```

The practical parser boundary is therefore:

```text
SML file
  -> SML table
     -> embedded payload 0: single-entry MLD-like payload
     -> embedded payload 1: single-entry MLD-like payload
     -> ...
```

### SML Runtime Layering and Overlay Geometry

Evidence level: `US corpus`, user visual annotation, current Blender IR export.

`s062` provides a useful example showing that an SML record is a runtime
render/effect group, not necessarily a clean spatial scene part. Entries can
spatially overlap and can represent fallback layers, addressable overlays, or
small visual-effect carriers.

The annotated `s062` arena has a central circle and concentric rings. Its SML
records are named sequentially from `s6200.nj` through `s6232.nj`, but the
runtime grouping is semantic rather than purely spatial:

| Entry range | Embedded MLD name(s) | Observed role |
| --- | --- | --- |
| `0` | `s6200.nj` | Central/ring base plus fallback fill patches. |
| `1..4` | `s6201.nj`..`s6204.nj` | Addressable nub overlay groups. Some are absent in-game, implying runtime enable/disable behavior. |
| `5` | `s6205.nj` | Large static shell/stage-detail group. |
| `6..8` | `s6206.nj`..`s6208.nj` | Flat glass/light planes. |
| `9..12` | `s6209.nj`..`s6212.nj` | Pipe/internal texture-effect geometry. |
| `13` | `s6213.nj` | Outer pipe meshes. |
| `14..17`, `20..32` | `s6214.nj`..`s6217.nj`, `s6220.nj`..`s6232.nj` | Sparkle/effect planes. |
| `18..19` | `s6218.nj`..`s6219.nj` | Untextured ring planes. |

Entry `0` has two meshes. Visual inspection confirms the second mesh contains
segmented ring fill patches that occupy gaps left when nub overlays are off.
Entries `1..4` are separate nub groups with their own type `0` selectors
`2..5`, making them likely addressable overlay slots rather than baked static
stage geometry.

This example is a warning for parser and research tooling: same-index SML/SST
correlation is structurally valid, but visual ownership can cross records.
Extra SST commands may intentionally target local model/object slot `0` inside
their own active SML/SST record while the visible effect relates to a broader
stage element.

The annotated `s044` damaged-homebase stage confirms that entry `0` can also be
ordinary primary stage geometry. After the MLD parser fix that restored record
`0` geometry, `s044` entry `0` resolves to the floor mesh with textures. Its SST
block contains types `[0, 1, 10]`, so this stage is a useful counterexample to
the earlier suspicion that record `0` was malformed or that this primary floor
geometry had to come from another already-loaded MLD.

The current `s044` annotation still leaves the elevator unresolved: the floor is
accounted for by SML entry `0`, but the elevator may come from an already-loaded
area MLD, another runtime resource, or a path outside the per-stage SML wrapper.

Across the US corpus, the embedded payload MLD headers and index entries match
the normal MLD layout closely:

| Field/structure | Observation |
| --- | --- |
| MLD header | `1285/1285` embedded payloads have plausible big-endian MLD headers. |
| `entryCount` | Always `1`. |
| `indexTableOffset` | `0x14` in `1204` payloads and `0x18` in `81` payloads. |
| Index entry size | Normal `0x68` entry shape. |
| Function name | Normal fixed-width entry function names such as `s0100.nj`. |
| `groundLinksPointer` | Counted U32 list, count `0` in all payloads. |
| `paramList2Pointer` | Counted U32 list, count `0` in all payloads. |
| `functionParametersPointer` | Counted U32 list, count `1` in all payloads. |
| `objectAddressesPointer` | Counted U32 list, count `1` in all payloads. |
| `groundAddressesPointer` | Counted U32 list, count `0` in `1204` payloads and `1` in `81`. |
| `motionAddressesPointer` | Counted U32 list, count `0` in `970`, `1` in `296`, and `2` in `19`. |
| `texturesPointer` | Per-entry texture-name/list structure, not the archive texture count. |
| Header `textureTableOffset` | Archive-level texture table; count at this offset equals `GCIX` count and `GVRT` count. |

The embedded payload data-block marker walk is also stable across US, EU, and
JP:

| Marker | Occurrences per region | Payloads hit | Notes |
| --- | ---: | ---: | --- |
| `NJCM` | `1285` | `1285` | Exactly one per embedded payload. |
| `NJTL` | `1284` | `1284` | Present in all normal textured payloads. |
| `NMDM` | `234` | `234` | Optional block in `69` stages. |
| `GCIX` | `6551` | `1284` | Texture index marker. |
| `GVRT` | `6551` | `1284` | Texture payload marker. |
| `GBIX` | `0` | `0` | Not present in the embedded SML MLD payload corpus. |
| `NJBM` | `0` | `0` | Not present. |
| `NJLI` | `0` | `0` | Not present. |
| `NJTX` | `0` | `0` | Not present. |

The dominant embedded-payload marker shapes are:

| Marker prefix | Payloads per region |
| --- | ---: |
| `NJTL > NJCM > GCIX > GVRT` | `466` |
| `NJTL > NJCM > GCIX > GVRT > GCIX > GVRT` | `91` |
| `NJTL > NJCM > GCIX > GVRT > GCIX > GVRT > GCIX > GVRT` | `493` |
| `NJTL > NJCM > NMDM > GCIX > GVRT` | `142` |
| `NJTL > NJCM > NMDM > GCIX > GVRT > GCIX > GVRT` | `23` |
| `NJTL > NJCM > NMDM > GCIX > GVRT > GCIX > GVRT > GCIX` | `69` |
| `NJCM` only | `1` |

The only payload missing `NJTL` and texture chunks is `s015`, record `9`:
payload size `384`, `NJCM` at payload `+0xd0`, texture table count `0`, and no
`GCIX`/`GVRT`.

For all textured embedded payloads:

- `NJTL` appears before `NJCM`.
- `NMDM`, when present, appears after `NJCM` and before the texture archive.
- `GCIX` and `GVRT` counts match exactly.
- Every observed `GVRT` marker is `16` bytes after its paired `GCIX`.
- `textureTableOffset` points before the first `GCIX`, not directly to it.
- The bytes between `textureTableOffset` and first `GCIX` have the normal MLD
  archive table shape: `4 + textureCount * 0x2c`.

Representative texture archive examples:

| Payload | `textureTableOffset` | Count at table | First `GCIX` | Gap |
| --- | ---: | ---: | ---: | ---: |
| `s001` record `0` | `0x1680` | `25` | `0x1ad0` | `0x450 = 4 + 25 * 0x2c` |
| `s001` record `1` | `0x03a0` | `1` | `0x03d0` | `0x30 = 4 + 1 * 0x2c` |
| `s001` record `5` | `0x41b60` | `57` | after table | `4 + 57 * 0x2c` |

This supports exposing embedded payload bytes to `SpiceMLD` in future code, but
the SML wrapper should remain owned by `SpiceSstSml`.

## Runtime Resource Tables

Evidence level: `US+Gekko`

`FUN_8000c8ac` does not hand the embedded MLD payload directly to later SST
command consumers. It copies the SML embedded payload, passes it through the
normal MLD load/texture-adjust path, and stores the resulting loaded MLD pointer
in a cached resource list rooted at `DAT_803473a0`.

The loaded MLD resource record is `0x44` bytes in the currently observed US
binary:

| Offset | Size | Current meaning |
| --- | ---: | --- |
| `+0x00` | 4 | Unknown key/state word. `FUN_8006d080` can copy this value from the previous list node into `DAT_8030a10c` when unlinking the tail. |
| `+0x04` | 2 | Refcount/state halfword. SML embedded loads through `FUN_8000c8ac` and direct embedded loads through `FUN_8006e244` store `0`; general named loads through `FUN_8006e924` store `1` and increment this field on cache hits. |
| `+0x08` | 4 | Loaded/adjusted MLD pointer returned by `loadTextures_801db124` or `FUN_801db244`. This is the pointer consumed by `FUN_8006cf54`. |
| `+0x0c` | 0x20 | Cached resource name such as `s0100.mld`; list lookup helpers compare against this string. |
| `+0x2c` | 4 | Optional owned allocation freed by `FUN_8006d080`; `FUN_8006d524` can free and clear it without releasing the whole resource. |
| `+0x30` | 4 | Optional pointer released by `FUN_8006d080` through `FUN_80009800`. |
| `+0x34` | 4 | Previous list pointer. New tail nodes store the prior tail here. |
| `+0x38` | 4 | Next list pointer. List walkers advance through this field. |
| `+0x3c` | 4 | Resource flag word. `FUN_8006e244` ORs in `0x20000000` after initializing object/list data; no direct SML wrapper meaning is proven. |
| `+0x40` | 4 | Unknown/padding within the `0x44` record. |

`FUN_8006cf54` receives one of these resource records, reads the loaded MLD
pointer at record `+0x08`, walks its `0x68` MLD index entries, and initializes
model blocks:

- entry `+0x14` is treated as an object-address list pointer
- object data whose marker starts with `NJCM` has its pointer advanced by `8`
  and is passed to `FUN_80075040`
- object data whose marker starts with `NJBM` is also advanced by `8`

This proves that SML embedded payloads become normal loaded MLD resources.
However, it does not prove that SST command model-index fields index SML
records directly at runtime.

The currently traced cache functions are:

| Function | Role |
| --- | --- |
| `FUN_8006eb04` | Allocates and zeroes the initial `0x44` resource-list head at `DAT_803473a0`. |
| `FUN_8000c8ac` | SML wrapper load path. Builds `s%02d%02d.mld`, copies the embedded SML payload, runs `loadTextures_801db124`, stores the result at resource `+0x08`, calls `FUN_8006cf54`, and records refcount/state `0`. |
| `FUN_8006e244` | Direct embedded/direct-pointer load path with similar copy/load/list insertion behavior. It additionally initializes per-entry object/list data and sets resource flag `+0x3c |= 0x20000000`. |
| `FUN_8006e924` / `FUN_8006ea70` | General named load wrapper. Reuses existing records by name and increments `+0x04`; otherwise fills a new record through `FUN_8006eb88` or a supplied MLD pointer. |
| `FUN_8006eb88` | General file/cache fill helper. Loads an MLD from crew cache, numeric preload table, injected `DAT_8030a204`, or `FUN_801db244(filepath)`, then calls `FUN_8006cf54`. |
| `FUN_8006e810` | Finds a resource record by numeric resource id converted to name through `FUN_8003dbac`. |
| `FUN_8006e898` | Tests whether a named resource is present in the list. |
| `FUN_8006eab4` | Tests a resource record's list membership and refcount/state relationship; current decompile suggests true only for refcount/state `1`. |
| `FUN_8006d080` | Releases a resource record: frees auxiliary fields, releases model/object state when requested, unlinks `+0x34/+0x38`, frees the loaded MLD pointer, then frees the record. |
| `FUN_8006d308` / `FUN_8006d524` | Higher-level cleanup passes that look up resource records by generated names and either release whole records or clear `+0x2c`. |

The SML load path is therefore a wrapper-to-MLD-cache bridge:

```text
SML record
  -> generated resource name s%02d%02d.mld
  -> copied embedded MLD bytes
  -> loadTextures_801db124
  -> DAT_803473a0 resource-list record +0x08
  -> FUN_8006cf54 MLD index-entry initialization
```

### Per-Record Active Model/Object Row

Evidence level: `US+Gekko`

The resource cache is now directly tied to the active per-SML/SST record row.
`Battle::Stage::JoinSmlSstRecords_8000cb44` passes
`DAT_80309e84 + recordIndex * 0x14 + 0x04` to `FUN_8006ea70`; `FUN_8006ea70`
wraps `FUN_8006e924`, which stores the selected `DAT_803473a0` resource-list
record through that output pointer. The active row therefore receives the
loaded MLD resource record pointer, not the raw loaded MLD pointer.

Known active-row fields:

| Offset | Size | Current meaning |
| --- | ---: | --- |
| `+0x00` | 4 | Local model/object slot table pointer. Written by SST command type `0`. |
| `+0x04` | 4 | Loaded MLD resource-list record pointer for the same SML record. |
| `+0x08` | 4 | Per-slot runtime pointer table. Written by SST command type `0` after the slot table bytes. |
| `+0x0c` | 1 | Local slot count byte, copied from loaded MLD header entry count. |
| `+0x10` | 4 | Secondary runtime model/effect buffer pointer. Written by type `0` setup and read by type `11`. |

Important direct instructions:

- `8000cc18..8000cc30`: `JoinSmlSstRecords_8000cb44` computes
  `DAT_80309e84 + index * 0x14 + 4` and calls `FUN_8006ea70`.
- `8006e994` / `8006ea54`: `FUN_8006e924` stores the selected resource record
  through the output pointer.
- `8000c1f0..8000c20c`: SST command type `0` reads
  `activeRow +0x04 -> resource +0x08 -> loadedMld +0x00` and stores the low
  byte as active row `+0x0c`.
- `8000c224..8000c240`: SST command type `0` allocates `slotCount * 0x50` and
  stores the base at active row `+0x00`.
- `8000c254..8000c264`: SST command type `0` stores
  `slotBase + slotCount * 0x4c` at active row `+0x08`, making an appended
  per-slot runtime pointer table.
- `8000c268..8000c2c8`: SST command type `0` copies each `0x4c` payload row,
  writes active row `+0x04` into copied slot `+0x10`, then calls
  `SST::Command::Type0::SetupRuntimeRow_8000bb28`.
- `8000bcd0`: `SetupRuntimeRow_8000bb28` writes the created child-local data
  pointer into `activeRow +0x08 + slotIndex * 4`.
- `8000bd78..8000bd9c`: `SetupRuntimeRow_8000bb28` allocates child-local
  secondary buffer `+0x4c` with size `0x54` and stores it at active row
  `+0x10`.
- `8000bda4..8000bda8`: `SetupRuntimeRow_8000bb28` calls `FUN_8022f00c` and
  `FUN_8022f030` after storing the secondary buffer pointer.
- `8000bde4..8000be10`: the optional selector-bucket path allocates a compact
  nested model-data buffer below active row `+0x10`, calls `FUN_8000b8e4`, and
  ORs `0x40000000` into secondary buffer `+0x50`.
- `8000befc..8000bf18`: SST type `11` reads active row `+0x08` first pointer
  and active row `+0x10`, then copies them into child type `12` local
  `+0x00/+0x04`.

Active row `+0x10` is runtime-local. It points at the secondary buffer created
while materializing a type `0` local model/object slot. The next helper hop
confirms model-tree/model-data mutation rather than on-disk parser structure:
`FUN_8022fce4` finds type `8` model chunks and rewrites a flag halfword, while
`FUN_8022f408` scans model chunk types `0x10..0x1f` and writes a supplied
pointer into chunk-specific fields depending on mode. Keep this field out of
stable parser-facing schema except as runtime context.

`Battle::Stage::JoinSmlSstRecords_8000cb44` allocates `recordCount * 0x2c`,
but every current exact `DAT_80309e84` reference in the exported US binary
addresses rows with `recordIndex * 0x14`. Treat the space beyond active row
`+0x10` as unresolved/reserved allocation width until an indirect consumer or
future trace explains the wider allocation.

The generic MLD/STD command-list path shares this active-row shape but is not
currently the SML/SST battle-stage path. `FUN_80067e8c` materializes direct
named MLD files, loads a matching `.STD` command list, and dispatches
`FUN_80067dfc`; one traced caller, `FUN_80030280`, builds combatant/direct MLD
names such as `MA_%03d.MLD`, `MB_%03d.MLD`, and `MG_%03d.MLD`. Its command `0`
materializer `FUN_8007e13c` allocates `slotCount * 0x4c` and calls
`FUN_8007e9e0`, whereas SST type `0` allocates `slotCount * 0x50`, appends the
active row `+0x08` pointer table, and writes active row `+0x10`. Treat these as
sibling materializer paths unless a later trace proves the generic STD path is
called from SML/SST stage setup.

Some later SST runtime paths call `FUN_8006b6f4(modelIndex)`, which reads a
separate four-slot runtime table rooted at `DAT_80309e88`. This table is
`0x40` bytes total: four `0x10`-byte slots initialized by `FUN_8006b71c`.
Do not confuse this table with the per-SML-entry model/object table rooted at
`DAT_80309e84`.

| Slot offset | Size | Current meaning |
| --- | ---: | --- |
| `+0x00` | 4 | Runtime row/object pointer returned by `FUN_8006b6f4`. |
| `+0x04` | 4 | Owner/source child pointer, used by `FUN_8006b5b8` for removal. |
| `+0x08` | 4 | Runtime state word, initialized to `0`. |
| `+0x0c` | 1 | Active flag. |

`FUN_8006b774`, reached from SST type `1`, is the confirmed table-population
path. It walks up to `32` `0x68`-byte type `1` subrecords until a negative first
byte. For each accepted row:

- row `+0x02` is read as a signed class/menu selector and must be in `0..3`
- row `+0x08` is read as a signed runtime slot id and must be `< 4`
- `FUN_8006bdb4(row+0x02, outChild, row)` creates the battle child/menu object
- `FUN_8006bdb4` copies the row bytes from `+0x00..+0x64` into child-local data
  at `child->data + 0x28..+0x8c`
- the runtime table slot `+0x00` is set to `child->data + 0x28`
- the runtime table slot `+0x04` is set to the child pointer
- the slot state word is cleared and the active flag is set

So the current best model is:

```text
SML record
  -> embedded MLD payload
  -> loaded MLD resource list at DAT_803473a0
  -> active row +0x04 resource pointer at DAT_80309e84 + recordIndex * 0x14
  -> SST type 0 materialized local model/object slot table at active row +0x00
  -> local model/object indices in SST types 2/3/4/8/9/10/11

SST type 1 row
  -> copied child-local runtime row
  -> four-slot runtime table at DAT_80309e88
  -> separate lighting/render-environment/runtime rows used by type 1 helpers
     and selected later consumers
```

The corpus fact that all observed model-index candidates are `0` remains useful
for structural validation, but parser-facing wording should treat these fields
as local model/object slot indices in the active SML/SST top-level record unless
a specific Gekko path proves use of the separate four-slot table. A direct
SML-record identity is not currently proven.

### Type 1 Runtime Row Consumer

Evidence level: `US+Gekko`, `US/EU/JP stable`

The callback selected by current type `1` rows is `FUN_8006b46c`. It reads the
child-local row at `child->data + 0x28` and calls `FUN_8006b8a8` while the child
state byte at child `+0x19` is `3`.

Direct row-field reads in `FUN_8006b46c` and `FUN_8006b8a8`:

| Row offset | Width | Current meaning |
| --- | ---: | --- |
| `+0x00` | `i8` | Row state / walker sentinel. Active corpus rows are `4`; the second structural row is `-1` and stops the row walk. |
| `+0x02` | `i16` | Class/menu selector passed to `FUN_8006bdb4`; corpus value is always `2`. |
| `+0x04` | `u32` | Flags. `0x40000000` gates render-light slot/global-color setup; `0x20000000` gates vector setup. |
| `+0x08` | `i16` | Runtime slot id. Corpus value is always `0`; helper calls use `slot + 1`. |
| `+0x0c/+0x10/+0x14` | `f32 x3` | Render-light direction or position vector, depending on row/applier case. Passed to layer-A helpers `FUN_8028708c` / `FUN_8028703c` and layer-B helpers `FUN_8028b63c` / `FUN_8028b5ec`. |
| `+0x30/+0x34/+0x38` | `f32 x3` | Per-slot RGB/intensity triplet. Clamped to byte color channels by `FUN_80287124` and `FUN_8028b6d4`. |
| `+0x3c/+0x40/+0x44` | `f32 x3` | Global/ambient RGB triplet. Clamped to byte color channels by `FUN_802871d8` and `FUN_8028b784`. |
| `+0x48` | `f32` | Passed to attenuation/spot-parameter helpers `FUN_80286fec` and `FUN_8028b59c` for nonzero slots; corpus value is always `30.0`. |
| `+0x4c` | `f32` | Multiplied by a constant before attenuation/spot-parameter helpers; corpus value is always `60.0`. |
| `+0x64` | `u32` | Last copied word; corpus value is always `0`. |

Corpus probe:

```text
SpiceSstSml/research/results/20260612_140000_type1_runtime_rows/
```

Across US, EU, and JP:

- paired stages scanned: `136` per region
- type `1` commands: `136` per region, exactly one per stage
- structural rows: `272` per region
- active rows: `136` per region
- inactive/sentinel rows: `136` per region
- class selector: always `2`
- runtime slot id: always `0`
- flag values: `0`, `0x40000000`, or `0xc0000000`

Current interpretation: type `1` is a stage-level lighting/render-environment
setup command that also seeds the four-slot table later read by model-index
commands. It is not currently evidence of a direct SML embedded-MLD record
lookup.

The helper family reached from `FUN_8006b8a8` is now traced far enough to rule
out the earlier camera/view wording. `FUN_801d9d2c` applies the same row shape as
a twelve-entry render-light configuration table:

- cases `2` and `7` call global/ambient color helpers `FUN_802871d8` and
  `FUN_8028b784`
- cases `3` and `8` configure directional slot `1` through
  `FUN_8028708c` / `FUN_8028b63c` plus slot-color helpers
- cases `4..6` and `9..11` configure positional/spot slots `2..4` through
  `FUN_8028703c` / `FUN_8028b5ec`, attenuation helpers
  `FUN_80286fec` / `FUN_8028b59c`, and slot-color helpers

The two helper groups write parallel `0x60`-byte slot records at `DAT_80344dec`
and `DAT_8034535c`. Those records contain enable words, type/mode words,
direction/position floats, clamped RGB(A) bytes, and attenuation/spot scalar
fields. The exact engine names for the two groups are still unknown, so docs and
parser metadata should use conservative layer-A/layer-B lighting terminology for
now.

The 2026-06-14 type `1` drill-down refreshed current Ghidra symbol names for
this helper cluster and did not find stronger source-of-truth names for the two
lighting groups. The relevant helper functions remain unnamed in the local
project, so the current parser/documentation names should stay conservative:

| Helper group | Enable helper | Direction/position helper | Color helper | Ambient/global helper | Attenuation/spot helper |
| --- | --- | --- | --- | --- | --- |
| layer A | `FUN_80287274` | `FUN_8028708c` / `FUN_8028703c` | `FUN_80287124` | `FUN_802871d8` | `FUN_80286fec` |
| layer B | `FUN_8028b820` | `FUN_8028b63c` / `FUN_8028b5ec` | `FUN_8028b6d4` | `FUN_8028b784` | `FUN_8028b59c` |

`FUN_8006b8a8` applies the active corpus row state `4` directly. For runtime
slot `0`, it configures slot `1` in both lighting layers and also applies
global/ambient RGB. For nonzero runtime slots, it configures positional/spot
slots through `runtimeSlotId + 1`. `FUN_801d9d2c` confirms the same twelve-entry
render-light table shape outside the SST type `1` path: cases `2..6` drive
layer A, and cases `7..11` drive layer B. This strengthens the
lighting/render-environment interpretation, but it still does not distinguish
formal engine names for the two layers.

Parser status: `SpiceSstSml` decodes type `1` payloads into
`SstType1LightingRow` metadata while preserving the original command payload
bytes. The decoded rows expose state, sentinel status, class selector, flags,
known flag-bit helpers, runtime slot id, light vector, slot RGB, global RGB,
attenuation/spot scalar pair, tail word, and raw row bytes.

## SST Layout

Evidence level: `US+Gekko`, `US/EU/JP stable`

SST top-level records start at file offset `0x00`. Do not start the SST table at
`0x08`; that is the SML record-table pattern.

The high halfword at SST file offset `0x04` matches the SML record count for the
same stem. The first top-level SST record overlaps that count-bearing word, so
the parser preserves the raw record fields as well.

### SST Top-Level Record

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `stageIdOrPreviousBlockLength` | In record `0`, high halfword is the stage id and low halfword is `0xffff`. In records `1+`, this is the previous command block's unaligned byte length. |
| `0x04` | 4 | `recordCountOrSentinel` | In record `0`, high halfword is the top-level record count and low halfword is `0xffff`; in records `1+`, this is `0xffffffff` in all checked battle rows. |
| `0x08` | 4 | `topLevelRecordIndex` | Equals the top-level record index in every checked battle row. |
| `0x0C` | 4 | `commandBlockOffset` | Base-relative SST command-block offset. Patched to a runtime pointer by the game. |

The SML and SST top-level record counts agree for every US, EU, and JP
same-stem battle pair checked so far.

`FUN_8000cb44` only needs SST top-level `+0x0c` at runtime:

- `8000cc3c`: reads the current top-level record's `+0x0c` command-block
  offset.
- `8000cc44..8000cc48`: patches that offset to a runtime pointer in place.
- `8000cc50`: passes the patched command-block pointer to `FUN_8000c7c0`.

The other top-level fields are still useful structural metadata:

- record `0 +0x00` follows the same stage-id-word shape as SML header `+0x00`
- record `0 +0x04` is the count-bearing word read at file offset `+0x04`
- records `1+ +0x04` are `0xffffffff`
- all `+0x08` words equal the record index
- for every US/EU/JP nonzero top-level record checked,
  `blockOffset[i] == align8(blockOffset[i - 1] + topRecord[i].stageIdOrPreviousBlockLength)`

## SST Command Block

Evidence level: `US+Gekko`, `US/EU/JP stable`

Given a command block at `block`:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | `commandCount` | Number of command records. |
| `+0x04` | `commandCount * 0x10` | `commandRecords` | Fixed-width command record table. |
| `+0x04 + commandCount * 0x10` | `0x10` | `sentinel` | Sentinel command record; signed type is negative. |
| `+0x04 + (commandCount + 1) * 0x10` | variable | `payloadPool` | Packed command payloads in command-record order. |

`FUN_8000c7c0` walks command records, assigns each record a runtime payload
pointer into the packed payload pool, then advances by the Gekko-derived payload
span for that command type.

### SST Command Record

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `type` | Signed command type. |
| `0x02` | 2 | `argument` | Arg/subkey. Observed corpus value is always `0`. |
| `0x04` | 4 | `rawWord4` | Unknown/reserved in current samples. Preserve raw. |
| `0x08` | 4 | `rawWord8` | Unknown/reserved in current samples. Preserve raw. |
| `0x0C` | 4 | `onDiskWord12` | Overwritten at runtime with payload pointer. Do not treat as an on-disk payload offset. |

The on-disk `+0x0c` field is a runtime storage slot, not a file pointer. This is
confirmed by both the SST block walker and the separate SoAInvestigate battle UI
analysis, where command consumers load `record + 0x0c` after the walker has
patched it.

## Command Payload Spans

Evidence level: `US+Gekko`, `US/EU/JP stable`

Payload spans come from the direct Gekko block walker in `FUN_8000c7c0`.

| Type | Span | Corpus status |
| ---: | ---: | --- |
| `0` | `0x4c` | corpus-present |
| `1` | `0xd0` | corpus-present |
| `2` | `0x44` | corpus-present |
| `3` | `0x08` | corpus-present |
| `4` | `0x18` | corpus-present |
| `5` | `0x00` | walker-supported, not treated as a modeled payload command |
| `6` | `0x10` | code-supported, corpus-absent |
| `7` | `0x14` | code-supported, corpus-absent |
| `8` | `0x14` | corpus-present |
| `9` | `0x0c` | corpus-present |
| `10` | `0x18` | corpus-present |
| `11` | `0x18` | corpus-present once |

Aggregate command type counts match across US, EU, and JP:

| Type | Count per region |
| ---: | ---: |
| `0` | `1285` |
| `1` | `136` |
| `2` | `3` |
| `3` | `87` |
| `4` | `92` |
| `6` | `0` |
| `7` | `0` |
| `8` | `194` |
| `9` | `97` |
| `10` | `4` |
| `11` | `1` |

## Command Payload Field Matrix

Evidence level: field offsets/read widths are `US+Gekko`; semantic names are
provisional unless noted.

| Type | Current field model |
| ---: | --- |
| `0` | One command appears first in every command block and aggregate count equals the SML top-level record count. `FUN_8000c19c` copies the full `0x4c` payload as words into a runtime row; setup consumers read `+0x16` as a signed lookup/resource index and `+0x18` as a signed battle object class selector. The callback selected through `FUN_800300c4` now proves selector `3` consumes `+0x1c/+0x20/+0x24` as transform vector floats, `+0x28/+0x2c/+0x30` as signed rotation/angle words, `+0x34/+0x38/+0x3c` as scale floats, and `+0x44` as a render/model action byte. Names remain provisional outside the directly traced selector path. |
| `1` | Stage lighting/render-environment setup command. `FUN_8006b774` walks up to `32` `0x68`-byte subrecords until a negative first byte at `+0x00`; current on-disk payload holds two structural rows, one active and one sentinel. `+0x02 i16` is the class/menu selector, `+0x04 u32` is a flag word, `+0x08 i16` is the runtime slot id, `+0x0c/+0x10/+0x14` are a light direction/position vector, `+0x30..+0x44` are per-slot and global/ambient RGB triplets, and `+0x48/+0x4c` are attenuation/spot scalar fields. `FUN_8006bdb4` copies each accepted row into child-local data at `+0x28`, and `FUN_8006b774` stores that copied row pointer in the four-slot runtime table used by later model-index commands. |
| `2` | Creates child type `4` / `FUN_8000e0d8`. `+0x00 i16` model index, `+0x02 u16` node traversal lookup key. Payload `+0x04..+0x40` is copied to child-local `+0x0c..+0x48`; local `+0x04` is forced to `-1`. Current evidence identifies this as a model-data point/vertex coordinate deformation effect: helper code scans model chunks `0x20..0x37`, snapshots 3-float coordinate triples, computes per-point distance weights, and writes selected X/Y/Z components back into the model-data coordinate array. |
| `3` | Creates child type `5` / `FUN_8000e02c`. `+0x00 i16` model index, `+0x02 u16` node/model-data traversal lookup key, `+0x04/+0x06 i16` signed texture-coordinate delta pair. `FUN_80230920` applies the delta pair to coordinate halfwords inside the selected model-data chunk and wraps crossing values by `0x200`, then flushes the affected data and invalidates the vertex cache. |
| `4` | `+0x00 i16` model index, `+0x04 u32` raw/flag, `+0x08/+0x0c/+0x10 f32`, `+0x14` raw/reserved. |
| `6` | Code-supported but corpus-absent. Setup validates `+0x00 i16` model index, creates child type `7`, stores payload `+0x04 f32` as a step/scalar, stores `+0x08 i16` as a gate/mode halfword, and stores the selected runtime object pointer. Child callback `8000dec8` only applies the scalar when the copied halfword is `1`, then adds or subtracts it from the linked runtime object `+0x20` float around a constant threshold. |
| `7` | Code-supported but corpus-absent. Setup validates `+0x00 i16` model index, creates child type `8`, stores payload `+0x04 f32` as an amplitude/scalar, stores payload `+0x08 f32` as a phase step, initializes a phase accumulator from `FLOAT_80348114`, and stores the selected runtime object pointer. Child callback `FUN_8000ddfc` advances the phase, computes a sine-scaled value, and writes the integer result to the linked runtime object at `+0x20`. |
| `8` | Child/menu setup command. `+0x00 i16` model index, `+0x02 u16` node traversal ordinal/lookup key passed to `FUN_8006c9ac`, halfword parameters at `+0x04`, `+0x06`, `+0x08`, `+0x0a`, `+0x0c` copied into child-local parameter data. |
| `9` | Child/menu setup command. `+0x00 i16` model index, `+0x08 i16` parameter copied into child-local parameter data; direct Gekko evidence also stores the selected runtime model/object pointer into that child data. |
| `10` | `+0x00 i16` model index, `+0x04 u32`, `+0x08/+0x0c/+0x10 f32`, `+0x14 i16` required nonzero value. |
| `11` | Structural walker span is `0x18`. `FUN_8000be28` reads inside-span fields `+0x00`, `+0x04`, `+0x06`, `+0x08`, `+0x0c`, and `+0x10`, then also reads trailing fields `+0x20`, `+0x22`, and `+0x24` when present before the next command block. The only corpus instance has a real trailing `0x10`-byte region after the walker payload. Child type `12` / `FUN_8000cffc` uses these values as a runtime vector motion controller over offsets `+0x1c/+0x20/+0x24` with optional fade/ramp state on a related object at `+0x10/+0x50`. Keep walker size `0x18`, and expose trailing consumer bytes separately in research summaries. |

### Type 6/7 Code-Supported Paths

Evidence level: `US+Gekko`, `code-supported corpus-absent`

Generated local-only artifacts:

```text
tools/ghidra/analyses/20260612_120000_sst_type6_type7_code_paths/
```

No type `6` or type `7` command appears in the current US, EU, or JP battle
corpora, but both are implemented in `FUN_8000c19c` and have direct child
callback evidence in the US binary.

Type `6` creates child type `7`. Its setup path uses payload `+0x00` as the
model index for the same runtime object table used by other model-linked SST
commands. It copies payload `+0x04` to child-local `+0x00`, copies payload
`+0x08` to child-local `+0x04`, and stores the linked runtime object pointer at
child-local `+0x08`. The child callback at raw range `8000dec8..8000df50`
reads child-local `+0x04` with `lha` and only continues when that value is `1`.
It then reads the linked runtime object `+0x20` as a float and adds or subtracts
child-local `+0x00` from that runtime float around `FLOAT_80348140`. This
supports a provisional "runtime float step" interpretation, but no parser-facing
semantic name should be promoted until a real payload is found.

Type `7` creates child type `8`. Its setup path uses payload `+0x00` as the
model index, copies payload `+0x04` to child-local `+0x00`, copies payload
`+0x08` to child-local `+0x04`, initializes child-local `+0x08` from
`FLOAT_80348114`, and stores the linked runtime object pointer at child-local
`+0x0c`. `FUN_8000ddfc` reads those fields as floats, advances the accumulator,
calls `sinf_80291930`, and writes `int(childLocal[0] * sin(...))` to the linked
runtime object at `+0x20`. This supports a provisional sine/oscillation effect
interpretation.

## SST/SML Join

Evidence level: `US+Gekko`, `US/EU/JP stable`

`FUN_8000cb44` walks SML and SST top-level records by the same index. The
current parser exposes this as `BattleStageParser::parsePair`.

Command types currently treated as carrying model-index candidates are:

```text
2, 3, 4, 6, 7, 8, 9, 10, 11
```

Across US, EU, and JP:

- checked model-index candidate links: `478` per region
- structurally resolvable candidate links under the current same-index
  correlation model: `478` per region
- unresolved structural links: `0`
- observed nonzero command arguments: `0`
- unsupported command types: `0`
- type `0` appears exactly once in every command block and is always the first
  command: `1285/1285` command blocks per region
- type `1` appears exactly once per battle stage: `136/136` stages per region

In the current corpus every observed model-index candidate is `0`. Direct
runtime evidence now shows these fields generally index local model/object slots
inside the active top-level SML/SST record. Some later command/runtime paths use
the separate four-slot table at `DAT_80309e88`, but that table should not be
conflated with the per-record model/object table rooted at `DAT_80309e84`.
The same-index SML link remains a useful parser validation/reporting aid because
command blocks and SML records are walked together by top-level index, but it
should not be promoted to a runtime identity without additional dataflow
evidence.

The `s062` overlay-geometry pass gives a concrete example of this caution:
type `3`, `8`, and `9` commands attached to visual/effect entries `6..12`,
`14..17`, and `20..32` all carry model index `0`. In the current command-map
report that resolves to runtime slot/model `0`, while the command block remains
attached to each entry's own SML record. For this stage, slot `0` corresponds to
entry `0`, the central arena base/fallback group and the only block with type
`1` setup.

## Command Payload Semantics Probe

Evidence level: `US corpus`, `US/EU/JP stable`; use as semantic guidance until
paired with direct Gekko naming evidence.

The first local-only command payload semantics pass was generated by
`SpiceSstSml/research/command_payload_semantics.py` into:

```text
SpiceSstSml/research/results/20260612_075441_command_payload_semantics/
```

That folder is ignored and should remain local-only. Durable conclusions from
that run:

- all three regions scan cleanly: `136` paired battle stems, `0` parse errors
- command type counts remain identical across US/EU/JP:
  `{0:1285, 1:136, 2:3, 3:87, 4:92, 8:194, 9:97, 10:4, 11:1}`
- type `0` is stronger than "frequent": it is one-per-command-block,
  first-in-block, and aggregate count equals SML record count in every stage
- type `1` is one-per-stage and appears in the block for top-level record `0`
  in representative stage samples
- model-index candidate fields for types `2`, `3`, `4`, `8`, `9`, `10`, and
  `11` all carry value `0` in the current corpus; current direct evidence reads
  this as local model/object slot `0` in the active record unless a specific
  command path proves use of the separate type `1` four-slot table
- top command-block patterns include `[0]`, `[0, 1]`, `[0, 8]`, `[0, 4]`,
  `[0, 8, 9]`, `[0, 3]`, `[0, 9]`, and `[0, 1, 10]`

The main parser should not promote corpus-only observations into final semantic
names. Since this first pass, direct Gekko work has clarified type `0` runtime
selectors and type `8`/`9` child setup behavior. Remaining command semantics
should be promoted only after the same direct-instruction plus corpus-validation
standard is met.

### Type 0 Payload Semantics Pass

Evidence level: `US+Gekko`, `US/EU/JP stable` for corpus patterns.

The focused type `0` pass is tracked under:

```text
planning/Analysis/2026-06-12_sst_type0_payload_semantics/
```

Generated local-only artifacts are under:

```text
tools/ghidra/analyses/20260612_080000_sst_type0_payload_semantics/
SpiceSstSml/research/results/20260612_081455_type0_joined_rows/
```

Direct Gekko evidence from `FUN_8000c19c`:

- `8000c268`: `lwz r29,0xc(r27)` loads the payload pointer patched into the
  command record by `FUN_8000c7c0`
- `8000c288` and `8000c28c`: copy payload words from `+0x00..+0x44`
- `8000c29c`: copies final payload word `+0x48`
- no direct `lha`, `lhz`, or `lfs` instruction interprets the type `0` payload
  inside this branch

Direct Gekko evidence from `FUN_8000bb28`:

- `8000bb6c`: `lha r26,0x16(r5)` reads a signed halfword lookup/resource-key
  candidate from the copied type `0` runtime row
- `8000bb70`: `lha r27,0x18(r5)` reads a signed category/class selector from
  the high halfword of raw word `+0x18`
- later loops copy the row into child/object setup data and copy updated setup
  data back into the runtime row
- `8000bcd0` stores the created runtime child-local pointer into
  `activeRow +0x08 + slotIndex * 4`

Corpus results across US, EU, and JP:

- `136` paired stages per region
- `1285` type `0` rows per region
- `0` missing or duplicate type `0` blocks
- `+0x34/+0x38/+0x3c` are `0x3f800000` for every row
- `+0x44` is `0x00000000` in `1194` rows and `0x02000000` in `91` rows
- tracked type `0` field patterns match exactly across US/EU/JP

Current field guidance:

| Field | Current interpretation |
| --- | --- |
| `+0x16` | Gekko-backed signed lookup/resource index. It is passed to `FUN_8006cf28` and sometimes `FUN_8006ceec`; corpus value is `0` for all US/EU/JP rows. |
| `+0x18` | Gekko-backed signed battle object class selector in the high halfword; not a float and not a proven SceneTable field. |
| `+0x1c/+0x20/+0x24` | Gekko-backed transform vector for selector callback `3`; later callback evidence loads these as `f32` values and passes them to `FUN_802924e0`. |
| `+0x28/+0x2c/+0x30` | Gekko-backed signed 32-bit rotation/angle components for selector callback `3`; values are scaled before `FUN_80292080`. Exact unit is still unresolved. |
| `+0x34/+0x38/+0x3c` | Gekko-backed scale triplet for selector callback `3`; corpus value is stable `1.0f` for every row. |
| `+0x44` | Gekko-backed render/model action byte for selector callback `3`; word patterns `0x00000000` and `0x02000000` correspond to byte values `0` and `2` at big-endian offset `+0x44`. Bytes `+0x45..+0x47` remain raw. |

### Type 0 Runtime Consumer Pass

Evidence level: `US+Gekko`, `US/EU/JP stable` for corpus patterns.

The focused runtime-consumer pass is tracked under:

```text
planning/Analysis/2026-06-12_sst_type0_runtime_consumers/
```

Generated local-only artifacts are under:

```text
tools/ghidra/analyses/20260612_090000_sst_type0_runtime_consumers/
SpiceSstSml/research/results/20260612_083825_type0_runtime_consumers/
```

Direct Gekko evidence:

- `FUN_8000bb28` reads `+0x16` with `lha r26,0x16(r5)` and passes it to
  `FUN_8006cf28`; selected `+0x18` classes also pass it to `FUN_8006ceec`
- `FUN_8006cf28` uses that value as a `0x68`-byte record index into the
  MLD/resource-side data hanging off the loaded SML/MLD runtime object. More
  specifically, it reads the loaded MLD pointer from the active row resource
  record, uses `+0x04` as the MLD index-table root, multiplies the lookup key
  by the normal `0x68` MLD index-entry stride, and reads entry `+0x14`.
- `FUN_8000bb28` reads `+0x18` with `lha r27,0x18(r5)` and uses it for class
  range checks and a switch that selects `FUN_8000f0a4` arguments `0..3`
- `FUN_8007e9e0` is an alternate setup path that reads the same `+0x16/+0x18`
  fields, passes `+0x16` to `FUN_8006cf28`, and uses `+0x18` directly as a
  `FUN_800300c4` callback selector
- no direct dataflow from `+0x16` or `+0x18` into SceneTable append/find helpers
  was found

Corpus results across US, EU, and JP:

- `+0x16` signed/unsigned value is always `0`
- `+0x18` low halfword is always `0`
- `+0x18` high-half selector counts are identical across regions
- `FUN_8000bb28` object class bucket counts are identical across regions:
  class `1` = `453`, class `2` = `389`, class `0` = `233`, class `3` = `210`

SceneTable comparison:

- `SceneTableEntry` fields are full-word `category`, `table_id`, `controller`,
  and `payload`
- script-side SceneTable posting builds `controller = command << 16 | ordinal`
- type `0 +0x18` is conceptually similar to a command/class selector, but should
  not be named as a SceneTable `command`, `category`, or `table_id` without
  direct dataflow evidence

### Type 0 Class Callback Pass

Evidence level: `US+Gekko`, `US/EU/JP stable` for already measured corpus
patterns.

The focused class-callback pass is tracked under:

```text
planning/Analysis/2026-06-12_sst_type0_class_callbacks/
```

Generated local-only artifacts are under:

```text
tools/ghidra/analyses/20260612_150000_sst_type0_class_callbacks/
```

`FUN_800300c4` indexes the callback table at `802dbf38` by the selector value:

```text
return (&PTR_FUN_802dbf38)[param_1];
```

The exported table currently maps selector indices `0..20` to callback
functions. The selector `3` callback is `FUN_80021cbc`, which gives the first
direct Gekko-backed transform interpretation of the copied type `0` runtime row:

| Offset | Width | Evidence |
| --- | --- | --- |
| `+0x1c` | `f32` | `80021d28 lfs f1,0x1c(r31)` passes X to `FUN_802924e0`. |
| `+0x20` | `f32` | `80021d30 lfs f2,0x20(r31)` passes Y to `FUN_802924e0`; later `FUN_8007e264(r31+0x20)` resets this copied local slot to a constant. |
| `+0x24` | `f32` | `80021d34 lfs f3,0x24(r31)` passes Z to `FUN_802924e0`. |
| `+0x28` | `i32-ish word` | `80021d3c lwz r6,0x28(r31)` is converted through signed integer-to-float math, scaled by `FLOAT_80348334`, and passed to `FUN_80292080`. |
| `+0x2c` | `i32-ish word` | `80021d44 lwz r4,0x2c(r31)` follows the same rotation/angle conversion path. |
| `+0x30` | `i32-ish word` | `80021d4c lwz r0,0x30(r31)` follows the same rotation/angle conversion path. |
| `+0x34` | `f32` | `80021dc8 lfs f1,0x34(r31)` passes scale X to `FUN_802923e0`. |
| `+0x38` | `f32` | `80021dd0 lfs f2,0x38(r31)` passes scale Y to `FUN_802923e0`. |
| `+0x3c` | `f32` | `80021dd4 lfs f3,0x3c(r31)` passes scale Z to `FUN_802923e0`. |
| `+0x16` | `i16` | `80021dec lha r6,0x16(r31)` passes the lookup/resource index to `FUN_8006ceec`. |
| `+0x44` | `u8` | `80021e08/80021e18 lbz ...,0x44(r31)` passes a byte to `FUN_80035380` or `FUN_80035140`. |

This pass also confirms that callback reads are against the copied child-local
row stored at thread `+0x24`. Earlier setup overwrites copied local offsets
`+0x00` and `+0x08` with runtime pointers, so callback reads at those offsets
must not be used as direct on-disk field semantics.

Other selector callbacks also load thread-local data and many enter larger
battle-combatant state machines. Current evidence is not yet clean enough to
promote additional on-disk type `0` fields from those callbacks; keep the parser
metadata limited to the selector `3` fields above until each path is traced with
the same direct source-pointer discipline.

### Type 0 Selector Source-Pointer Audit

Evidence level: `US+Gekko`, `US/EU/JP stable` for current selector buckets.

The source-pointer audit is tracked under:

```text
planning/Analysis/2026-06-12_sst_type0_selector_source_audit/
```

Derived local-only audit output is under:

```text
tools/ghidra/analyses/20260612_160000_sst_type0_selector_source_audit/
```

Audit result: no additional parser metadata should be promoted yet.

| Selector | Current source-pointer result | Parser impact |
| ---: | --- | --- |
| `0` | no-op callback | no field promotion |
| `1` | copied type `0` runtime row, but only copied row `+0x4c` secondary buffer and overwritten `+0x08` resource pointer are used | no field promotion |
| `2` | copied type `0` runtime row, but only copied row `+0x4c` secondary buffer and overwritten `+0x00/+0x08` runtime pointers are used | no field promotion |
| `3` | copied type `0` runtime row with direct reads from non-overwritten offsets | keep existing provisional field metadata |
| `4`, `5`, `11`, `12` | copied type `0` runtime row, but only runtime pointers or secondary buffers are used; currently absent from corpus buckets | no field promotion |
| `6`, `20` | no-op callbacks | no field promotion |
| `7`, `8`, `9`, `10`, `13`, `14`, `15`, `16`, `17`, `18`, `19` | battle-combatant worksheet callbacks; in these functions `thread +0x24` is not the copied SST row | no field promotion |

The key audit rule is that a raw `lwz ...,0x24(r3)` is not enough to prove a
type `0` field read. The callback's expected thread/data shape must be traced.
For the large battle-combatant callbacks, offset matches such as `+0x1c`,
`+0x24`, or `+0x30` are worksheet/global-state accesses and should not be folded
back into the SST type `0` schema.

### Type 8/9 Command Correlation Pass

Evidence level: `US+Gekko`, `US/EU/JP stable` for corpus patterns.

The focused type `8`/`9` pass is tracked locally under:

```text
planning/Analysis/2026-06-12_sst_type8_type9_semantics/
planning/Analysis/2026-06-12_sst_type8_type9_runtime_consumers/
planning/Analysis/2026-06-13_sst_type8_9_fire_effect_semantics/
planning/Analysis/2026-06-13_sst_type9_orientation_source/
planning/Analysis/2026-06-13_sst_battle_view_record_fields/
planning/Analysis/2026-06-14_sst_type8_9_flame_runtime_answer/
```

Generated local-only artifacts are under:

```text
SpiceSstSml/research/results/20260612_095327_type8_type9_command_correlation/
SpiceSstSml/research/results/20260612_102035_type8_type9_command_correlation/
SpiceSstSml/research/results/20260613_221148_type8_type9_command_correlation/
tools/ghidra/analyses/20260612_100000_sst_type8_type9_semantics/
tools/ghidra/analyses/20260612_103000_sst_type8_type9_runtime_consumers/
tools/ghidra/analyses/20260613_223000_sst_type9_orientation_source/
tools/ghidra/analyses/20260613_231500_sst_battle_view_record_fields/
```

The analysis reuses baseline `FUN_8000c19c` evidence from:

```text
tools/ghidra/analyses/20260612_000000_sst_sml_command_payload_schema/
```

Corpus results are identical across US, EU, and JP:

| Command/block fact | Count per region |
| --- | ---: |
| type `8` commands | `194` |
| type `9` commands | `97` |
| block pattern `[0, 8, 9]` | `174` |
| block pattern `[0, 8]` | `107` |
| block pattern `[0, 9]` | `10` |

Both type `8` and type `9` model-index fields resolve to a valid local
model/object slot in every observed row. In the current corpus every observed
model index is `0`, so these commands attach behavior or child state to slot `0`
of the active SML/SST top-level record. This is stable corpus behavior; it is
not a direct SML-record lookup and it is not the same as the separate type `1`
four-slot runtime table.

Direct Gekko evidence for type `8` in `FUN_8000c19c`:

- `8000c63c`: reads `payload +0x00` as signed model index and bounds-checks it
  against the active model count byte.
- `8000c650`: calls `FUN_8000f0a4(9)`, then `8000c660` calls
  `mkChildMenu_802268e8`.
- `8000c688`: reads `payload +0x02`; `8000c698` masks it to unsigned 16-bit.
- `8000c6a0`: calls `FUN_8006c9ac(modelObject, lookupKey)`.
- `8000c6a4`: stores the returned pointer at child data `+0x00`.
- `8000c6a8..8000c6b4`: allocates/fills `0x10` bytes behind child data
  `+0x4c`.
- `8000c6b8..8000c6f0`: reads signed halfwords from payload `+0x04`,
  `+0x06`, `+0x08`, `+0x0a`, and `+0x0c`, then stores them to child-local
  offsets `+0x00`, `+0x02`, `+0x04`, `+0x06`, and `+0x08`.

`FUN_8006c9ac` is a thin wrapper around `FUN_8006c9e4`. `FUN_8006c9e4` masks
the lookup key to unsigned 16-bit and recursively walks a runtime node tree
using pointers at `+0x2c` and `+0x30`, incrementing an ordinal counter until it
finds the requested node. This supports naming type `8 +0x02` as a node
traversal ordinal/lookup key.

Direct Gekko evidence for type `9` in `FUN_8000c19c`:

- `8000c708`: reads `payload +0x00` as signed model index and bounds-checks it
  against the active model count byte.
- `8000c71c`: calls `FUN_8000f0a4(10)`, then `8000c72c` calls
  `mkChildMenu_802268e8`.
- `8000c748..8000c754`: allocates/fills `0x08` bytes behind child data
  `+0x4c`.
- `8000c758..8000c760`: reads signed halfword `payload +0x08` and stores it at
  child-local offset `+0x04`.
- `8000c768..8000c788`: uses payload `+0x00` to load a runtime model/object
  pointer from the current model pointer table and stores it at child-local
  offset `+0x00`.

Type `8` corpus field distributions per region:

| Field | Values |
| --- | --- |
| `+0x02` | `2` in all `194` rows |
| `+0x04/+0x06` | `64` in `101`, `32` in `83`, `8` in `10`; consumed as texture tile/cell width and height |
| `+0x08` | `256` in `101`, `128` in `66`, `64` in `27`; consumed as atlas/page dimension for UV scaling |
| `+0x0a` | `16` in `172`, `4` in `17`, `64` in `5`; consumed as frame count |
| `+0x0c` | `0` in `148`, `2` in `22`, and other smaller/sparse values plus large values such as `60`, `150`, `160`, `170`, `190`; copied as the on-disk hold/delay duration |
| `+0x10` | `0` in `153`, `0x0000ffff` in `41`; no direct read in current Gekko window |

Type `9` corpus field distributions per region:

| Field | Values |
| --- | --- |
| `+0x08` | `2` in `57`, `1` in `22`, `0` in `10`, `3` in `8`; consumed as orientation mode/quadrant selector |
| `+0x02` | `2` in `90`, `0` in `7`; no direct read in current type `9` Gekko window |
| `+0x04` | always `0`; no direct read in current type `9` Gekko window |
| `+0x0a` | mostly `0`; nonzero values occur but no direct read in current type `9` Gekko window |

Runtime callback evidence:

- `FUN_8000f0a4` indexes callback table `PTR_FUN_802dafa8`.
- callback table entry `9` at `802dafcc` points to `FUN_8000db88`.
- callback table entry `10` at `802dafd0` points to `FUN_8000d8fc`.
- child type `9` (`FUN_8000db88`) reads the type `8` copied parameter block at
  child data `+0x4c`, updates halfword texture-coordinate ranges at the selected
  node/model-part data offsets `+0x1a`, `+0x1c`, `+0x20`, `+0x22`, `+0x26`,
  `+0x28`, `+0x2c`, and `+0x2e`, then calls `DCStoreRange` and
  `GXInvalidateVtxCache`.
- In that child-local parameter block, offsets `+0x0a` and `+0x0c` are
  runtime-only counters zero-filled by `alloc_fill_80061f58`: `+0x0a` is a hold
  counter and `+0x0c` is the current frame. They are not on-disk type `8`
  payload offsets. The on-disk type `8 +0x0c` value is copied to parameter
  `+0x08` and is read as the hold/delay duration.
- child type `10` (`FUN_8000d8fc`) reads the type `9 +0x08` value from child
  parameter block `+0x04`, calls `FUN_800120d0`, and writes an orientation/Y
  angle at the attached runtime model/object pointer `+0x2c`.
- `FUN_800120d0` copies the published battle view orientation vector from global
  state `80309ec8 +0x64` (`80309f2c`). The per-frame publisher
  `FUN_800149ac` is called from `Battle::ThreadMethods::runBattleThreads`
  before `Thread::run_threads_8022642c`, and it calls `FUN_80014808` to publish
  the active view record into global position/target/orientation vectors.

Current interpretation: types `8` and `9` are battle child/menu setup commands,
not static geometry records. Type `8` creates a child type `9` UV/texture
coordinate animation over a node selected by ordinal traversal of the selected
model object's node tree. Type `9` creates a child type `10` orientation updater
for an attached runtime model/object.

In `s062`, all type `8`/`9` commands occur on sparkle/effect-plane entries
`14..17` and `20..32`, and all target local model/object slot `0` in their
active records. Their repeated payload shape supports the current interpretation
that these are visual-effect commands layered on top of ordinary SML embedded
MLD geometry rather than separate static scene parts.

In the annotated `s044` damaged-homebase stage, entries `6..11` and `17..20`
are fire planes. Every one has command block shape `[0, 8, 9]`, type `0` class
selector `6`, and the same type `8` payload:

```text
rawHex = 0000000200400040010000100000000000000000
localModelIndex = 0
nodeLookupOrdinal = 2
tileWidth = 64
tileHeight = 64
texturePageSize = 256
frameCount = 16
frameHoldDuration = 0
rawWord10 = 0
```

That is a 4x4 texture-page animation: `256 / 64 = 4` tiles per axis and `16`
frames total. The fire SML entries have no embedded motion animation, so the
visible burning behavior is explained by SST type `8` UV animation.

The same `s044` fire-plane entries split type `9 +0x08` by plane axis:

| Entries | Type `9 +0x08` | Exported plane axis |
| --- | ---: | --- |
| `6`, `7`, `8`, `11`, `17`, `18`, `20` | `1` | X-constant |
| `9`, `10`, `19` | `2` | Z-constant |

This supports naming type `9 +0x08` as a battle-view/camera-facing
orientation/Y-angle mode. The conservative parser-facing name should remain
`orientationMode` or `viewOrientationMode`; the broader command should not be
renamed to a general-purpose billboard command until more non-fire type `9`
examples are visually checked.

The annotated `s006` flame records `13..21` are useful as the contrast case:
they are visually flame planes inside the floating rings and use type `8`
without type `9`. Type `8` still explains the burning texture animation. Any
camera-facing behavior there is not yet proved to come from type `9`; it may be
authored into the mesh, supplied by type `0` class behavior, or handled by a
separate runtime path.

The follow-up battle-view record trace confirms the runtime context for this
source:

| Function/field | Stable interpretation |
| --- | --- |
| `FUN_800149ac` | Per-frame active battle view selector/publisher; called from `Battle::ThreadMethods::runBattleThreads` before battle child callbacks run. |
| `FUN_80014808` | Publishes the active battle view record into global position/target/orientation vectors. |
| `FUN_80014ab8(1, record)` | Installs the primary active battle view record at global `80309ec8 +0x40`. |
| `FUN_80014ab8(0, record)` | Installs the secondary/fallback battle view record at global `80309ec8 +0x44`. |
| `FUN_80014a54(1/0, record)` | Clears the matching primary/secondary active battle view record. |
| active record `+0x20` | Position-like vector copied to global `+0x4c` after applying pending position offset `+0x70`. |
| active record `+0x2c` | Orientation/angle vector copied to global `+0x64` after applying pending orientation offset `+0x7c`. |
| active record `+0x38` | Target/look vector copied to global `+0x58`. |
| global `+0x70` | One-frame pending position offset set by `FUN_80012078`, then cleared after publish. |
| global `+0x7c` | One-frame pending orientation offset set by `FUN_80012020`, then cleared after publish. |

Several exact callers of `FUN_80014ab8(1, ...)` are battle turn/combatant
action paths in the current Ghidra naming, including `FUN_80051264` with
`Thread_BattleTurn`, `Battle_TurnWorksheet`, and
`Battle_CombatantInstructionWorksheet` decompile types. This reinforces that
the SST type `9` consumer belongs in battle-stage/battle-action context rather
than a generic static scene-layout context.

The highest-value caller classification currently looks like this:

| Caller | Current role | Relevant direct evidence |
| --- | --- | --- |
| `FUN_80051264` | Main battle turn action view controller for several action modes. | Installs `r31`/turn worksheet as primary active view record at `80051530`; updates `+0x20/+0x2c/+0x38`; clears the same record at `80051614`. |
| `FUN_8004e7f0` | Temporary combatant/effect view record controller. | Resolves two entries through `FUN_8006ceec`, builds vectors around combatant target helpers, installs `buffer` as primary active view record at `8004ebb8`, and clears/frees it at `8004ee14`. |
| `FUN_8005bf30` | One-frame battle view offset controller. | Uses a flag word to map an interpolated value to position/orientation axes, then calls `FUN_80012078` and `FUN_80012020`. |

Current remote-project labels after the 2026-06-14 refresh:

| Address | Current label |
| ---: | --- |
| `80014ab8` | `Battle::View::SetActiveRecord_80014ab8` |
| `80014a54` | `Battle::View::ClearActiveRecord_80014a54` |
| `800149ac` | `Battle::View::PublishActiveRecordForFrame_800149ac` |
| `80014808` | `Battle::View::PublishRecordVectors_80014808` |
| `800120d0` | `Battle::View::GetPublishedOrientation_800120d0` |
| `80012078` | `Battle::View::SetPendingPositionOffset_80012078` |
| `80012020` | `Battle::View::SetPendingOrientationOffset_80012020` |
| `80051264` | `Battle::Turn::UpdateActionViewRecord_80051264` |
| `8004e7f0` | `Battle::Action::UpdateTemporaryCombatantViewRecord_8004e7f0` |
| `8005bf30` | `Battle::View::UpdatePendingOffsets_8005bf30` |

### Type 2 Runtime Consumer Pass

Evidence level: `US+Gekko`, `US/EU/JP stable` for corpus patterns.

The focused type `2` pass is tracked locally under:

```text
planning/Analysis/2026-06-12_sst_type2_runtime_consumers/
```

Generated local-only artifacts are under:

```text
SpiceSstSml/research/results/20260612_105049_type2_command_correlation/
tools/ghidra/analyses/20260612_113000_sst_type2_runtime_consumers/
tools/ghidra/analyses/20260612_123000_sst_type2_runtime_structure/
```

Corpus results are identical across US, EU, and JP:

| Fact | Count/detail per region |
| --- | --- |
| type `2` commands | `3` |
| stages | `s008`, `s017`, `s018` |
| parse/probe errors | `0` |
| block pattern `[0, 3, 2]` | `2` |
| block pattern `[0, 2]` | `1` |

Direct Gekko setup evidence from `FUN_8000c19c`:

- `8000c314`: reads payload `+0x00` as signed model/object index.
- `8000c328..8000c32c`: calls `FUN_8000f0a4(4)` to select child type `4`.
- `8000c338`: calls `mkChildMenu_802268e8` under the battle root/menu.
- `8000c360..8000c378`: reads payload `+0x02`, masks it to unsigned 16-bit,
  and calls `FUN_8006c9ac` to select a runtime node/object.
- `8000c37c`: stores the selected runtime node/object pointer in child data
  `+0x00`.
- `8000c38c`: allocates a `0x4c` child-local parameter block at child data
  `+0x4c`.
- `8000c398..8000c414`: copies payload `+0x04..+0x40` into child-local
  `+0x0c..+0x48`.
- `8000c41c`: forces child-local `+0x04` to `0xffff`.

Important parser-facing implication: child-local fields `+0x00`, `+0x02`, and
`+0x04` are setup/runtime state in the type `2` path, not direct on-disk
payload fields. The on-disk payload begins with model index and lookup key, then
the copied parameter block.

Runtime callback evidence:

- child type `4` maps through callback table `PTR_FUN_802dafa8` entry
  `802dafb8` to `FUN_8000e0d8`.
- `FUN_8000e0d8` reads child data `+0x00` as selected runtime node/object
  pointer and child data `+0x4c` as the local parameter block.
- state `0` compares local `+0x00` current-frame counter against local `+0x02`
  wait/limit. Type `2` setup leaves `+0x02` zero in current rows, so it
  immediately advances to state `1`.
- state `1` calls `FUN_80230634(object, local + 0x0c)` and
  `FUN_80230280(object, local + 0x0c)`, then enters state `2`.
- state `2` optionally gates on local `+0x04` through `FUN_800179e4`, but type
  `2` setup forces `+0x04` to `-1`, so the gate is disabled in current rows.
  It then calls `FUN_8000b320(currentFrame, object, local + 0x0c)` and
  increments the current-frame counter.
- state `3` frees optional helper buffers at local `+0x20` and `+0x2c`, then
  marks the child for cleanup.

Helper context:

- `FUN_80230d10` searches the selected runtime model/object data for chunk ids.
  The type `2` helpers scan ids `0x20..0x37`.
- `FUN_802301a0` locates one of those model-data chunks and returns its chunk
  id, count, stride/control value, and coordinate-data pointer.
- `FUN_80230634` stores a per-point float buffer at parameter `+0x14`, which is
  child-local `+0x20`. It computes distance/weight values from model-data
  coordinate triples against a center vector.
- `FUN_80230280` stores a `count * 0x0c` buffer at parameter `+0x20`, which is
  child-local `+0x2c`. It snapshots source coordinate triples from the same
  model-data chunk.
- `FUN_80230e48` filters points by distance range, optional Y range, and
  optional X/Z radial bounds.
- `FUN_8000b320` uses the helper buffers, calls `sinf_80291930`, and writes
  selected X/Y/Z float components back into the model-data coordinate array.
  This is enough to call the target a model-data point/vertex coordinate array;
  the exact engine struct name is still unknown.

Stable corpus field guidance:

| Payload field | Local field | Current meaning |
| --- | --- | --- |
| `+0x00 i16` | n/a | Signed model/object index; always `0` in current corpus. |
| `+0x02 u16` | n/a | Node traversal lookup key; always `2` in current corpus. |
| `+0x04/+0x08/+0x0c f32` | local `+0x0c/+0x10/+0x14` | Center vector seed; all zero in current corpus. `FUN_80230634` may replace selected components from the first source coordinate depending on packed control byte `0`. |
| `+0x10 f32` | local `+0x18` | Sine phase/frequency scalar used by `FUN_8000b320`; values `4.0`, `6.0`, `8.5`. |
| `+0x14 f32` | local `+0x1c` | Amplitude/scale used in the sine displacement; values `0.8`, `1.5`, `2.0`. |
| `+0x18 u32` | local `+0x20` | Zero on disk; becomes helper-allocated distance/weight buffer pointer. |
| `+0x1c f32` | local `+0x24` | Minimum distance/range for `FUN_80230e48`; all zero in current corpus. |
| `+0x20 f32` | local `+0x28` | Maximum distance/range for `FUN_80230e48`; values `300.0`, `1400.0`. |
| `+0x24 u32` | local `+0x2c` | Zero on disk; becomes helper-allocated source-coordinate snapshot buffer pointer. |
| `+0x28 u32` | local `+0x30` | Packed control word. Byte `0` controls which center-vector components are seeded from source coordinates in `FUN_80230634`; observed byte `0` values are `0`, `1`, `2`. Byte `1` selects the target coordinate in `FUN_8000b320`: `0`/other = Y, `1` = X, `2` = Z. |
| `+0x30 f32` | local `+0x3c` | Falloff/weight-shape scalar used by `FUN_8000b320`; zero gives full weight. All zero in current corpus. |
| `+0x34/+0x38 f32` | local `+0x40/+0x44` | Optional Y min/max filter; no filter when equal. Both are zero in current corpus. |
| `+0x3c/+0x40 f32` | local `+0x48/+0x4c` | Optional X/Z radial bounds filter; no filter when equal. Both are zero in current corpus. |

Focused writeback evidence from `FUN_8000b320` confirms the exact target-axis
behavior:

- `FUN_8000b320` calls `FUN_802301a0` to locate a model-data chunk in the
  `0x20..0x37` family and receives its chunk id, point count, stride/control
  byte, and coordinate pointer.
- Each point uses the helper-allocated distance/weight buffer at copied local
  `+0x20` and the source-coordinate snapshot buffer at copied local `+0x2c`.
- Points outside the distance/Y/XZ filters are restored from the source
  coordinate snapshot instead of being displaced.
- Points inside the filters compute a sine displacement from distance, frame,
  phase scalar, displacement scale, and falloff, then write exactly one
  coordinate component back into the model-data coordinate array.
- The second byte of payload `+0x28` selects the written component:
  `1` writes X, `2` writes Z, and any other observed/current value writes Y.
  Current corpus values are `0`, `1`, and `2`.

Current interpretation: type `2` is a rare model-data point/vertex coordinate
deformation effect. Do not collapse it into type `4` despite the similar
"movement" flavor; type `2` uses child type `4`, helper-allocated point buffers,
and a sine update path over model-data coordinate triples, while SST type `4`
uses child type `6` and direct per-frame object-offset deltas.

### Type 11 Boundary Pass

Evidence level: `US+Gekko`, `US/EU/JP stable` for the single corpus instance.

The focused type `11` boundary pass is tracked locally under:

```text
planning/Analysis/2026-06-12_sst_type11_boundary/
```

Generated local-only artifacts are under:

```text
SpiceSstSml/research/results/20260612_111246_type11_boundary_probe/
tools/ghidra/analyses/20260612_114500_sst_type11_boundary/
tools/ghidra/analyses/20260612_124500_sst_type11_runtime_structure/
```

The result resolves the earlier over-read question. Type `11`'s structural
walker span remains `0x18`, because `FUN_8000c7c0` advances type `11` by
`0x18`. `FUN_8000be28` nevertheless performs real direct Gekko reads at
payload-relative `+0x20`, `+0x22`, and `+0x24`.

In the only known instance, `s021`, those extra reads land in trailing bytes
between the walker payload and the next command block:

| Boundary | Offset/detail |
| --- | --- |
| type `11` command | `s021`, block `1`, command `1` |
| command block offset | `0x1f0` |
| payload offset | `0x270` |
| walker span/end | `0x18` / `0x288` |
| Gekko-read trailing end | `0x296` |
| next command block offset | `0x298` |
| bytes from walker end to next block | `16` |
| bytes from Gekko-read end to next block | `2` |

US, EU, and JP are byte-identical for this instance.

The direct Gekko setup path:

- `8000be58`: reads payload `+0x00` as signed model/object index.
- `8000be6c`: reads trailing `+0x20` and requires it nonzero before creating
  the child.
- `8000be7c`: calls `FUN_8000f0a4(0x0c)`, selecting child type `12`.
- `8000beb8`: reads trailing `+0x22` and stores it at child-local `+0x26`.
- `8000bec0`: reads payload `+0x04` and stores it at child-local `+0x08`.
- `8000bec8`: reads trailing `+0x20` and stores it at child-local `+0x24`.
- `8000bed0`: reads payload `+0x06` and stores it at child-local `+0x0a`.
- `8000bed8`: reads trailing `+0x24` and stores it at child-local `+0x28`.
- `8000bee0..8000bef4`: reads payload floats `+0x08/+0x0c/+0x10` and stores
  them at child-local `+0x0c/+0x10/+0x14`.

Observed field values in `s021`:

| Source | Value | Destination/use |
| --- | --- | --- |
| payload `+0x04` | `-1` | child-local `+0x08` |
| payload `+0x06` | `0` | child-local `+0x0a` |
| payload `+0x08/+0x0c/+0x10` | `0.0`, `600.0`, `0.0` | vector components |
| trailing `+0x20` | `512` | required nonzero, child-local `+0x24` |
| trailing `+0x22` | `5120` | child-local `+0x26` |
| trailing `+0x24` | signed `-19456` | child-local `+0x28` |

Child type `12` maps through callback table entry `802dafd8` to
`FUN_8000cffc`. That callback uses the local block to compute vector deltas,
updates runtime vector fields at offsets `+0x1c/+0x20/+0x24`, and manipulates a
related runtime object flag/float at offsets `+0x50` and `+0x10`.

Refined child-local layout:

| Local offset | Source | Runtime role |
| --- | --- | --- |
| `+0x00` | active table `+0x08` first pointer | Runtime vector target. `FUN_8000cffc` updates target `+0x1c/+0x20/+0x24`. |
| `+0x04` | active table `+0x10` | Related runtime object. `FUN_8000cffc` sets/clears flag bit `0x80000000` at `+0x50` and ramps float `+0x10`. |
| `+0x08` | payload `+0x04` | Fade/ramp mode gate. If `0`, callback can compute ramp step from trailing `+0x22`; corpus value is `-1`, disabling that setup path. |
| `+0x0a` | payload `+0x06` | Repeat/cycle limit. `FUN_8000cee8` treats `-1` as infinite; corpus value is `0`, so the known instance completes after one cycle. |
| `+0x0c/+0x10/+0x14` | payload `+0x08/+0x0c/+0x10` | Target vector. Corpus value is `(0.0, 600.0, 0.0)`. |
| `+0x18/+0x1c/+0x20` | runtime state | Start/current reset vector. `FUN_8000cee8` copies this vector back to target `+0x1c/+0x20/+0x24` between cycles. |
| `+0x24` | trailing `+0x20` | Motion step/magnitude halfword. Required nonzero before child creation; corpus value is `512`. |
| `+0x26` | trailing `+0x22` | Ramp denominator/duration halfword used to compute local `+0x44` when fade/ramp mode allows it; corpus value is `5120`. |
| `+0x28` | trailing `+0x24` | Signed ramp/timing parameter copied for the child; corpus value is `-19456`. Direct downstream use is still not isolated. |
| `+0x2c/+0x30/+0x34` | computed in state `0` | Per-tick vector deltas added into target `+0x1c/+0x20/+0x24`. |
| `+0x38/+0x3c/+0x40` | computed in state `0` | Axis bounds/threshold values used to decide when movement has reached the selected axis target. |
| `+0x44` | computed in state `0` | Related-object float ramp step for object `+0x10`. |
| `+0x48` | runtime state | Ramp state: ramping up, at full value, ramping down. |
| `+0x4a` | computed in state `0` | Axis selector for completion checks: `0` = X, `1` = Y, `2` = Z. |
| `+0x4c` | runtime state | Completed cycle count. |
| `+0x4e` | runtime state | Per-cycle frame/tick counter compared against local `+0x24`. |

`FUN_8000cee8` is the child type `12` transition helper. It increments local
`+0x4c`, resets local `+0x4e`, optionally resets the related-object ramp state,
copies local start vector `+0x18/+0x1c/+0x20` back to target vector
`+0x1c/+0x20/+0x24`, and returns the child to state `2`; otherwise it moves the
child toward state `3` cleanup.

Current parser guidance:

- Keep type `11` command `payloadSize` as `0x18`.
- Do not change the generic command-block walker to advance type `11` by
  `0x28`; that would contradict `FUN_8000c7c0`.
- Research/semantic summaries may expose a separate type `11` trailing
  consumer window when bytes through `+0x24` are available before the next
  command block.
- Treat type `11` as a special runtime vector motion command with related-object
  fade/ramp state. Exact engine structure names remain provisional.

### Type 3/4/10 Runtime Consumer Pass

Evidence level: `US+Gekko`, `US/EU/JP stable` for corpus patterns.

The focused type `3`/`4`/`10` pass is tracked locally under:

```text
planning/Analysis/2026-06-12_sst_type3_type4_type10_runtime_consumers/
```

Generated local-only artifacts are under:

```text
SpiceSstSml/research/results/20260612_103237_type3_type4_type10_command_correlation/
tools/ghidra/analyses/20260612_104500_sst_type3_type4_type10_runtime_consumers/
```

Corpus results are identical across US, EU, and JP:

| Command/block fact | Count per region |
| --- | ---: |
| joined type `3`/`4`/`10` rows | `183` |
| parse/probe errors | `0` |
| type `3` commands | `87` |
| type `4` commands | `92` |
| type `10` commands | `4` |
| runtime-slot candidate links resolved | `183` |

Callback/runtime mapping:

| SST type | Setup path | Runtime child type/callback | Current role |
| ---: | --- | --- | --- |
| `3` | Inline branch in `FUN_8000c19c` | child type `5`, `FUN_8000e02c` | Creates a child that applies signed texture-coordinate deltas to a selected model-data chunk. |
| `4` | `FUN_8000c014` | child type `6`, raw range `8000df50..8000e02c` | Creates per-frame signed deltas and applies them to object offsets `+0x28/+0x2c/+0x30`. |
| `10` | `FUN_8000bf48` | child type `11`, `FUN_8000d734` | Creates vector interpolation/oscillation state and delegates application/clamping to `FUN_8000d488`. |

Type `3` direct evidence:

- `+0x00` is read as signed model index and bounds-checked against the active
  model/object count.
- `+0x02` is read as an unsigned lookup/node traversal key and passed through
  `FUN_8006c9ac`.
- `+0x04` and `+0x06` are read as signed halfwords, copied to child-local
  `+0x4c + 0x04/+0x06`, and passed as a two-halfword parameter block to
  `FUN_802307ec`.
- `FUN_802307ec` delegates to `FUN_8023080c`, which finds a model-data chunk
  through `FUN_80230d10` when needed and then calls `FUN_80230920`.
- `FUN_80230920` treats the two-halfword parameter block as signed deltas added
  to texture-coordinate halfwords inside model-data chunk entries. It wraps the
  affected coordinates back by `0x200` when they cross `+/-0x200`, then calls
  `DCStoreRange` and `GXInvalidateVtxCache`.
- Child-local `+0x00/+0x02` are a zero-initialized runtime delay/counter pair.
  Because current setup does not populate a nonzero delay, observed type `3`
  children advance to the apply state immediately.
- Corpus values are stable across regions: model index is always `0`;
  lookup keys range from `2` through `9`.
- Current interpretation: type `3` is a texture-coordinate offset command for a
  selected model-data chunk or node. It is distinct from type `8`: type `3`
  applies a signed two-halfword delta through the model-data helper path, while
  type `8` maintains a frame/counter-driven UV animation child.

Stable type `3` field guidance:

| Payload field | Local field | Current meaning |
| --- | --- | --- |
| `+0x00 i16` | n/a | Signed local model/object slot index; always `0` in current corpus. |
| `+0x02 u16` | n/a | Node/model-data traversal lookup key passed to `FUN_8006c9ac`; corpus range `2..9`. |
| `+0x04 i16` | child-local parameter `+0x04` | Signed texture-coordinate U/S delta applied by `FUN_80230920`; corpus range includes `-55..55`. |
| `+0x06 i16` | child-local parameter `+0x06` | Signed texture-coordinate V/T delta applied by `FUN_80230920`; corpus range includes `-10..17`. |

Type `4` direct evidence:

- `+0x00` is a signed model index.
- `+0x04` is copied into child-local `+0x0c` and controls whether the child
  callback adds or subtracts the computed deltas.
- `+0x08`, `+0x0c`, and `+0x10` are floats copied to child-local
  `+0x10/+0x14/+0x18`; setup converts them into integer per-frame deltas at
  child-local `+0x00/+0x04/+0x08`.
- The child callback applies those deltas to the attached runtime object at
  offsets `+0x28`, `+0x2c`, and `+0x30`.
- Corpus values currently populate only the middle float meaningfully, most
  often `960.0`, with smaller counts for `5.0`, `360.0`, and `120.0`.

Current interpretation: type `4` is a signed drift/offset command for an
attached runtime object. The exact coordinate names should wait until the
runtime object transform layout is named.

Type `10` direct evidence:

- `+0x00` is a signed model index.
- `+0x04` is copied as a mode/endpoint behavior word into child-local `+0x08`.
- `+0x08`, `+0x0c`, and `+0x10` are target vector floats.
- `+0x14` is a required nonzero signed duration/frame count.
- Child type `11` computes per-frame deltas at `+0x1c/+0x20/+0x24` and
  `FUN_8000d488` applies them to a runtime vector, clamps against target or
  original values, and for mode `2` toggles endpoint state at child-local
  `+0x34` while reversing delta signs.

Current interpretation: type `10` is a vector interpolation command. In the
observed corpus, mode `2` is likely an oscillating/bouncing endpoint mode, but
the parser-facing name should remain conservative until the target runtime
vector is identified.

### Type 2/8/9/10 Model-Index Resolution

Evidence level: `US+Gekko`, with `US/EU/JP stable` corpus support for observed
payload values.

The focused resolution packet is under:

```text
planning/Analysis/2026-06-13_sst_command_types_8_9_10_2_resolution/
```

The direct setup paths resolve the old ambiguity around payload `+0x00`.
For types `2`, `8`, `9`, and `10`, payload `+0x00` is not a global SML record
index. It is checked against the active top-level record's model/object count,
then used within that active record's runtime model/object row.

`FUN_8000cb44` establishes the active top-level record context:

- `8000cbb0`: stores the allocated per-record runtime table at `DAT_80309e84`.
- `8000cbc0`: stores the active top-level record index at `DAT_80309e7c`.
- `8000cbd8`: consumes the same-index SML record.
- `8000cc50`: walks the same-index SST command block.

The command setup paths then repeatedly follow this pattern:

```text
activeIndex = DAT_80309e7c
activeRow = DAT_80309e84 + activeIndex * 0x14
modelCount = activeRow + 0x0c
payloadModelIndex = payload + 0x00
payloadModelIndex is bounds-checked against modelCount
selected object/node is loaded from the active row, not from SML record 0
```

Representative Gekko anchors:

| Type | Active-row evidence | Payload-index evidence | Result |
| ---: | --- | --- | --- |
| `2` | `8000c304..8000c310` loads active index/table; `8000c31c` reads active row count. | `8000c314` reads `payload +0x00`; `8000c374` selects `payloadIndex * 0x4c` from the active row. | Local object plus node lookup, then deformation child setup. |
| `8` | `8000c62c..8000c638` loads active index/table; `8000c644` reads active row count. | `8000c63c` reads `payload +0x00`; `8000c69c` selects `payloadIndex * 0x4c` from the active row. | Local object plus node lookup, then UV animation child setup. |
| `9` | `8000c6f8..8000c704` loads active index/table; `8000c710` reads active row count. | `8000c708` reads `payload +0x00`; `8000c780..8000c788` stores the selected local model/object pointer in child data. | Local object pointer plus orientation-mode child setup. |
| `10` | `8000bf68..8000bf7c` loads active index/table and active row count. | `8000bf74` reads `payload +0x00` and `8000bf80` bounds-checks it. | Local object selector is validated; current exported setup window copies vector/duration data for child type `11`. |

This resolves the annotation-facing interpretation: if a command on SML entry
`N` has model index `0`, it targets slot `0` inside entry `N`'s loaded runtime
object data. It does not target SML entry `0`.

### Shared Model Node And Chunk Helpers

Evidence level: `US+Gekko`, with existing `US/EU/JP stable` command corpus
support for the observed payload values.

The focused shared-helper packet is under:

```text
planning/Analysis/2026-06-14_sst_shared_model_data_helpers/
tools/ghidra/analyses/20260614_sst_shared_model_data_helpers/
```

This pass resolves the next layer after command payload `+0x00`. The model
index selects a local model/object slot inside the active SML/SST top-level
record, while payload `+0x02` on types `2`, `3`, and `8` is a node ordinal
lookup within that selected runtime object. It is not itself a chunk id.

`FUN_8006c9ac` is the small public wrapper for the node lookup:

- `8006c9bc`: passes a stack halfword counter initialized to `0`.
- `8006c9c0`: passes a stack output pointer initialized to `0`.
- `8006c9cc`: calls `FUN_8006c9e4(rootNode, lookupKey, counter, outNode)`.
- `8006c9d4`: returns the resolved node pointer from the stack output slot.

`FUN_8006c9e4` masks the lookup key to unsigned 16-bit and walks the node tree
using node pointers at `+0x2c` and `+0x30`. The traversal checks the current
node against the current ordinal counter, increments the counter as it advances,
and writes the first matching node pointer to the output slot. Current evidence
therefore supports `nodeTraversalLookupKey` or `nodeOrdinal` for command payload
`+0x02`; `nodeTraversalLookupKey` remains the parser-facing name.

After the node is selected, model-data chunk lookup is separate:

- `FUN_80230d10(chunkId, node, outChunk)` reads `node +0x04` to access the
  node's model-data/attach container.
- For chunk ids `0x20..0x37`, it searches through the container pointer at
  offset `+0x00`, uses a two-halfword stride mode, and returns a pointer to the
  matching chunk header. This is the family used by type `2` deformation.
- For other chunk ids, it searches through the container pointer at offset
  `+0x04`, uses a one-halfword stride mode, and returns a pointer to the
  matching chunk header. This is the family used by type `3` texture-coordinate
  mutation.
- The chunk scanner compares only the low byte of the chunk/header id and stops
  at low byte `0xff`.
- For low-byte ids below `8`, it advances by one halfword. For ids `>= 8`, it
  advances by a header-derived count; ids `>= 0x10` use a larger counted skip.
  Exact engine names for these chunk classes are still unresolved.

Type `3` uses the second chunk family. `FUN_8023080c` accepts the selected node
or a wrapper around it, skips an `NJCM`-like four-character prefix by adding
`8`, treats an `NMDM`-like four-character prefix as already resolved, then scans
chunk ids `0x40..0xfe` while deliberately skipping ids `0x40`, `0x43`, and
`0x46`. When a matching chunk is found, it calls `FUN_80230920`.

`FUN_80230920` reads the matched chunk as texture-coordinate-bearing strip data:

- `80230944`: reads the chunk low-byte id from chunk `+0x00`.
- `80230948`: reads a packed count/control halfword from chunk `+0x04`.
- `8023095c`: extracts the high two bits of that halfword.
- `80230958`: masks the lower `0x3fff` bits as the outer loop count.
- `802309c4..80230a18`: adds the two signed command parameters to halfwords at
  per-corner offsets `+0x02` and `+0x04`.
- `802309e0..80230a50`: marks axes that cross `+/-0x200`.
- `80230af4..80230b4c`: applies the `0x200` wrap correction pass when needed.
- `80230bac`: flushes the touched range with `DCStoreRange` and invalidates the
  vertex cache.

This confirms type `3` as an immediate texture-coordinate offset/fixup command
for a selected node's texture-coordinate chunk. The axis names remain
conservative as `textureCoordinateDeltaU` and `textureCoordinateDeltaV`; they
could later be renamed to `S/T` if the engine naming or SA3D-side structures
make that convention clearer.

Type `8` is a distinct texture-coordinate animation path that also operates on
the resolved node. Its child callback `FUN_8000db88` does not call
`FUN_80230920`; instead it computes a frame window from the copied type `8`
parameters and writes halfword coordinate bounds directly at the selected
node/model-part data offsets `+0x1a`, `+0x1c`, `+0x20`, `+0x22`, `+0x26`,
`+0x28`, `+0x2c`, and `+0x2e`, then flushes and invalidates the vertex cache.
The two commands therefore share the same high-level target domain
("texture-coordinate data on a selected node") but have different runtime
contracts:

| Command | Node lookup | Chunk/target path | Runtime effect |
| ---: | --- | --- | --- |
| `3` | `FUN_8006c9ac` from payload `+0x02` | `FUN_8023080c` scans chunk ids `0x40..0xfe`, excluding `0x40/0x43/0x46` | One-shot signed coordinate delta with wrap correction. |
| `8` | `FUN_8006c9ac` from payload `+0x02` | Child callback writes coordinate bound halfwords on the selected node/model-part data | Frame/counter-driven texture tile animation. |
| `2` | `FUN_8006c9ac` from payload `+0x02` | `FUN_80230634`/`FUN_80230280` scan chunk ids `0x20..0x37` | Coordinate-triple snapshot and distance-weight deformation. |

Type `2` remains semantically separate from the texture-coordinate commands.
`FUN_8000e0d8` calls `FUN_80230634` and `FUN_80230280` with child-local data at
`+0x0c`. Both helpers scan chunk ids `0x20..0x37`. `FUN_80230634` allocates a
per-point float buffer and computes distance weights from 3-float coordinate
triples. `FUN_80230280` allocates a `count * 0x0c` snapshot buffer and copies
three 32-bit values per point using the stride table at `802f9340`. This
supports keeping type `2` named as model-data coordinate deformation rather than
texture-coordinate animation.

## Runtime Interpretation From SoAInvestigate

Evidence level: external Ghidra analysis reference; use as semantic guidance,
not as a replacement for direct SST/SML parser evidence.

The `<local-analysis>\Analyses\20260611_1448_battle_ui` investigation shows
that several SST command consumers create battle thread/menu children through
`mkChildMenu_802268e8` using the battle root at `803475a8`. The same windows
show command consumers reading `record + 0x0c` after the block walker has
patched that field to the runtime payload pointer.

This adds two useful interpretation points:

- SST command payloads are runtime setup records, not only static placement
  records.
- The model-index fields at payload `+0x00` are checked against a runtime table
  byte at a stage/object table offset like `activeIndex * 0x14 + 0x0c`, which
  reinforces the "model/runtime object index" interpretation.

The `<local-analysis>\Analyses\20260611_0843_script_inst_ops` investigation
adds MLD runtime context:

- MLD index entry `+0x1c` is `motionAddressesPointer`.
- Runtime object `+0x1c8` likely points to an MLD entry-derived resource/index
  structure.
- Helper `8008f8fc` looks up a motion/resource slot by reading a counted table
  at runtime structure `+0x1c`, then falls back through parent/linked objects at
  object `+0x1d0`.
- Zero entries in MLD motion address lists are meaningful empty slots.

This supports keeping `SpiceSstSml` independent from `SpiceMLD` for now while
preserving embedded MLD bytes and SST-to-SML links for a later semantic pass.

## Stage Worksheet State Buckets

Evidence level: `US+Gekko`

The focused trace under
`planning/Analysis/2026-06-14_stageworksheet_statebucket_trace/` inspected
`StageWorksheet::ComputeStateBucket_8011768c`,
`StageWorksheet::StateBucket_8030e414`, and the highest-value direct callers.

This is useful runtime context, but not direct SST/SML parser evidence.
`StageWorksheet::ComputeStateBucket_8011768c` does not read SML records, SST
command records, or copied SST type `0` rows. Its direct Gekko reads are:

- `zzOnShip_80347464`
- `zzCurrentArea_80311ac4`
- `StageWksht_PTR_80347450`
- `StageWksht_PTR_80347450 + 0x170`

The function returns bucket `0` immediately when `zzOnShip_80347464 == 1` or
`zzCurrentArea_80311ac4 >= 500`. If `StageWksht_PTR_80347450` is null, it
returns `-1`. Otherwise it reads worksheet halfword `+0x170` and maps state ids
to buckets:

| Worksheet `+0x170` state id | Returned bucket |
| --- | ---: |
| `1..4` | `0` |
| `0x16..0x19` | `0` |
| `5..9` | `1` |
| `0x0a..0x0e` | `2` |
| `0x0f..0x14` | `3` |
| `0x15` | `4` |
| `0` or other unmatched values | `-1` |

`StageWorksheet::StateBucket_8030e414` currently looks like a script/global
cached state rather than the primary live runtime control. The exact-address
scan found writes from `zzUnloadScript`, `SCPT::INST::INST048_802051d0`, and
`SCPT::INST::SetStageWorksheetStateBucket_8020a2c8`, but no reads.
`SCPT::INST::SetStageWorksheetStateBucket_8020a2c8` calls
`ComputeStateBucket` and stores the result, substituting `0` when the computed
value is `-1`.

Important caller behavior:

| Caller | Current role |
| --- | --- |
| `FUN_801177a4` | Enters bucket start states: `5`, `0x0a`, and `0x0f` based on event/interaction record classes. |
| `FUN_801179b8` | Performs follow-up transitions inside buckets, including special state `0x15` for bucket `4`. |
| `FUN_80119af4` | Main stage worksheet update path; dispatches `PTR_FUN_802e4f50[worksheet +0x170]`. |
| `FUN_80119e50` | Applies bucket-sensitive runtime vector/effect target fields; bucket `4` uses alternate target offsets. |
| `FUN_80118204`, `Step_Counter_800c1c24`, `MLD::kmodel_800e29cc` | Gate interaction, encounter/step, or controller/debug-like paths on bucket `0`. |

Current parser guidance:

- Do not promote any SST type `0` field to a StageWorksheet field from this
  trace alone.
- Do not treat `StageWorksheet::StateBucket_8030e414` as the live control read
  unless a later reader is found.
- `PTR_FUN_802e4f50`, the callback table indexed by worksheet `+0x170`, has
  now been traced as runtime context rather than direct parser evidence.

The follow-up trace under
`planning/Analysis/2026-06-14_stageworksheet_state_callbacks/` exported the
26-entry callback table at `PTR_FUN_802e4f50`. The callbacks operate on a
runtime MLD worksheet object. They explain transform, motion, collision, and
interaction behavior after a worksheet exists, but they do not directly consume
SML records, SST command records, or copied SST type `0` rows.

Callback-table state grouping:

| Worksheet `+0x170` state id | Bucket | Current role |
| --- | ---: | --- |
| `0` | `-1` before transition | Initialization into state `1`. |
| `1..4` | `0` | Normal worksheet update/collision variants. |
| `5..9` | `1` | Event/interaction path using runtime globals such as `DAT_8034741c`. |
| `0x0a..0x0e` | `2` | Follow-up event/interaction path using `DAT_8034741c`/`DAT_80347418`. |
| `0x0f..0x14` | `3` | Transform/collision/camera-facing path. |
| `0x15` | `4` | Large special interaction/platform-like path. |
| `0x16..0x19` | `0` | Late return/interpolation/camera-facing variants. |

Conservative worksheet field shape from the callback pass:

| Worksheet offset | Current interpretation |
| ---: | --- |
| `+0x170` | state id used for `PTR_FUN_802e4f50` dispatch |
| `+0x172` | substate/counter |
| `+0x16c/+0x16e` | transform mode/flags |
| `+0x38/+0x3c/+0x40` | position |
| `+0x44/+0x48/+0x4c` | rotation/angle-like fields |
| `+0x50/+0x54/+0x58` | scale-like fields |
| `+0xb4..+0xc8` | secondary transform offset/target fields |
| `+0xe8/+0xf0` | current/previous motion slot pointers |
| `+0x18c/+0x198/+0x19c/+0x1a0/+0x1a4/+0x1a8` | motion/interpolation state |
| `+0x1b8` | event collision selector |
| `+0x1bc` | interaction/collision object pointer |
| `+0x1c8` | MLD index-entry/resource pointer |
| `+0x1d0` | parent/linked worksheet pointer used by motion lookup fallback |

Important helper evidence:

- `FUN_801e7414` selects worksheet motion slots by calling
  `SceneTable::Cat6::LookupObjectMotionSlotThroughParents_8008f8fc`. That
  lookup reads an MLD index/resource pointer at worksheet `+0x1c8`, consults a
  counted motion list at resource offset `+0x1c`, and can fall back through
  parent/linked worksheet pointer `+0x1d0`.
- `FUN_801e5148` applies worksheet position, rotation, scale, current/previous
  motion slots, and interpolation state to the MLD runtime object.
- `FUN_80118aa4` and `FUN_80119744` tie worksheet transform work back to loaded
  MLD list/index data, including a `_DNJCMList`-like source.
- `FUN_8010e270` uses controller input and camera Y rotation to classify
  directional states.

Parser guidance after the callback pass:

- Do not promote StageWorksheet offsets as SST/SML parser fields unless a later
  bridge proves source dataflow from SML/SST-loaded records.
- Treat this as MLD worksheet runtime context that may affect battle-stage
  presentation indirectly.

The bridge trace under
`planning/Analysis/2026-06-14_stageworksheet_bridge_8011622c/` inspected
`FUN_8011622c`, `FUN_80119e50`, the direct callers, and the queued target
setter path. This reduced the likelihood that StageWorksheet is the direct
control path for SST/SML stage overlays.

`FUN_8011622c` is a runtime proxy resource swapper:

- direct callers are `MLD::objPlayerAct_80119ff4` and
  `MLD::playersub_8010e074`;
- the queued target global `DAT_80347400` is written by script instruction
  `SCPT::INST::INST137_80207c2c` through tiny setter `FUN_801165bc`;
- the queued target id is `param_1 & 0xffff`; sentinel `0x7fffffff` requests
  restore of a previous proxy swap;
- `FUN_8011622c` looks up the target object/thread through `FUN_801094c8`,
  reads the target object's worksheet from object `+0x24`, snapshots active
  proxy resource pointers, then installs target worksheet resource pointers
  into `StageWksht_PTR_80347450`;
- the copied resources include worksheet `+0xe4`, MLD index-entry `+0x1c` and
  `+0x10`, worksheet `+0x100`, and interaction/collision pointer `+0x1bc`;
- the collision/interaction object is rebound to the proxy transform fields
  `+0x38`, `+0x50`, and `+0x44`;
- the proxy is reset to state `+0x170 = 1`, substate `+0x172 = 0`, transform
  mode `+0x16c = 0`, and motion slot `1`;
- `FUN_8015eb8c(StageWksht_PTR_80347450, 0x0d, FUN_80119e50)` registers
  `FUN_80119e50` as a category 6 command callback.

`FUN_80119e50` is not a loader. It copies motion slot/interpolation values
between worksheet objects and uses `StageWorksheet::ComputeStateBucket_8011768c`
to choose current-vs-previous motion fields for bucket `4`.

Current StageWorksheet conclusion:

- This system is MLD/SceneTable/script runtime context.
- It may affect battle-stage presentation indirectly if a battle-stage SML
  entry becomes an MLD worksheet addressed by script, but this trace does not
  prove that source path.
- Do not use StageWorksheet fields as SST/SML parser metadata until a future
  trace proves source dataflow from loaded SML/SST records.

## Current Parser Boundary

`SpiceSstSml` currently models:

- optional AKLZ decompression
- SML header/count, `0x10` records, embedded MLD offset/size, raw fields, and
  bounds validation
- embedded MLD payload spans as MLD-like byte payloads suitable for handing to
  `SpiceMLD` after extraction
- SST top-level records, command-block offsets, command records, sentinel
  records, Gekko payload spans, raw payload bytes, conservative
  evidence-scoped field summaries, command consumer windows, and diagnostics
- type `11` trailing consumer metadata as a separate window when bytes through
  payload-relative `+0x24` are available before the next command block; the
  structural payload size remains `0x18`
- joined SML/SST count agreement
- command type histogram
- runtime-slot candidate validation, with same-index SML/SST correlation kept
  as structural context rather than a proven runtime lookup

It intentionally does not model:

- embedded MLD internals as native `SpiceSstSml` structures
- runtime child/menu object structures created by SST command consumers
- loaded MLD resource-list records, active row internals, or the four-slot
  runtime table as stable parser-facing structures
- type `11` trailing bytes as part of the structural walker payload
- stable semantic names for corpus-only command payload fields
- export/repackaging
- SST command-effect visualization in Blender IR
- regional differences beyond reporting/validation metadata

## Representative Stage Facts

Representative US examples from the command-payload probe:

| Stem | SML records | SML decoded size | Command types |
| --- | ---: | ---: | --- |
| `s001` | `10` | `1825396` | `{0: 10, 1: 1}` |
| `s002` | `5` | `810676` | `{0: 5, 1: 1, 10: 1}` |
| `s006` | `28` | `2846288` | `{0: 28, 1: 1, 8: 9}` |
| `s021` | `4` | `498096` | `{0: 4, 1: 1, 11: 1}` |
| `s044` | `22` | `2501128` | `{0: 22, 1: 1, 8: 10, 9: 10, 10: 1}` |
| `s062` | `33` | `1377815` | `{0: 33, 1: 1, 3: 23, 8: 17, 9: 17}` |
| `s150` | `9` | `731056` | `{0: 9, 1: 1, 3: 1}` |

`s062` is the current best annotated example for SML layering. Entry `0` is a
base/fallback arena group; entries `1..4` are addressable nub overlays; entries
`6..12`, `14..17`, and `20..32` are visual-effect carriers whose extra commands
target local model/object slot `0` in their active records.

`s044` is the current best annotated damaged-environment example. Entry `0` is
the floor; entries `1..5` contain damaged scene props; entries `6..11` and
`17..20` are fire planes with type `8`/`9` effect commands; entries `12..16`
are sky bands; and entry `21` is a cliff/wall piece. The floor is no longer an
open parse issue; the still-missing elevator is the remaining visible-stage
asset to explain.

## Open Questions

- Does any later system retain and inspect the original SML/SST top-level
  tables after `FUN_8000cb44` finishes? Current direct evidence resolves the
  loader meanings of SML record `+0x00/+0x0c` and SST top-level
  `+0x00/+0x04/+0x08`, but no later read of SML `+0x0c` or SST
  `+0x04/+0x08` has been found.
- Are `NMDM` payloads motion/model data blocks, and how do they relate to the
  embedded MLD motion address list and the runtime motion lookup helpers?
- What is the exact per-entry `texturesPointer` substructure for embedded SML
  payloads, especially in the common case where per-entry texture names are
  empty but the archive-level texture table contains `GCIX`/`GVRT` chunks?
- For type `0`, which copied fields after `+0x18` are later consumed as
  transforms, flags, or runtime state? Current direct evidence names `+0x16`
  and `+0x18`; `+0x34/+0x38/+0x3c` and `+0x44` remain corpus-backed only.
- For type `8`, what is the formal engine name and exact in-memory structure for
  the selected node/model-part whose texture-coordinate bound halfwords are
  updated? The payload fields and node-ordinal lookup are now Gekko-backed, but
  the target structure name remains provisional.
- For type `9`, are corpus-only payload fields `+0x02`, `+0x04`, and `+0x0a`
  read by another path, or are they padding/unused for this command?
- For type `9`, visual checks outside fire-plane/effect-plane stages are still
  needed before naming the whole command as billboard-like behavior. Direct
  evidence now ties `+0x08` to published battle view orientation, so the field
  name can remain `orientationMode` or `viewOrientationMode`.
- For type `3`, what is the formal engine name for the chunk ids `0x40..0xfe`
  modified by `FUN_80230920`, excluding ids `0x40`, `0x43`, and `0x46`, and
  which texture-coordinate axis naming convention should be used (`U/V`, `S/T`,
  or engine-specific names)?
- For type `2`, what are the formal engine names for the coordinate-triple
  chunk ids `0x20..0x37`, the stride lookup tables at `802f9340`/`802dae90`,
  and packed control byte `0` modes `0..4`?
- For type `10`, what exact runtime vector is targeted through
  `FUN_8006b6f4(0)` and related global state, and should mode `2` eventually
  be named oscillation/bounce?
- For type `11`, what are the formal engine names for the target vector object
  and related ramp object, and is trailing `+0x24` consumed outside the currently
  traced child type `12` path?
- For code-supported but corpus-absent types `6` and `7`, are these unused
  battle effects, region/build leftovers, or commands used by non-battle/custom
  data not present in the current local corpora?
- Why does `Battle::Stage::JoinSmlSstRecords_8000cb44` allocate
  `recordCount * 0x2c` for `DAT_80309e84` when every current direct active-row
  consumer addresses rows with stride `0x14`? Current exact-reference evidence
  finds no `0x2c`-stride consumer, so this is now a bounded allocation-width
  anomaly rather than an active-row layout claim.
- Which renderer/model subsystem formally owns the secondary runtime buffer at
  active row `+0x10` after `FUN_8022f00c` / `FUN_8022f030` mutate model-tree
  chunks, and what engine concept should that buffer be named?
- Can the generic MLD/STD command-list dispatcher path
  (`FUN_80067dfc` / `FUN_800090f8`) ever run during battle-stage setup through
  an indirect script/resource path? Direct evidence currently classifies it as
  a sibling path for direct named MLD/STD loads, not the primary SML/SST path.
- What is the exact relationship, if any, between the loaded MLD resource cache
  and the copied SST type `1` lighting/render-environment rows placed in the
  separate `DAT_80309e88` four-slot table? The current evidence keeps these as
  distinct runtime structures unless a future dataflow proves otherwise.
- In `s044`, where does the elevator geometry come from now that SML entry `0`
  is confirmed as the floor? Candidate sources are an already-loaded area MLD,
  a separate runtime resource loaded by script/state, or a stage element hidden
  behind an SST command path not yet linked to the visible elevator.
- What are the formal engine names for the type `1` lighting helper groups at
  `DAT_80344dec` and `DAT_8034535c`, and do those groups correspond to known
  engine concepts such as world/model lighting layers, material-light sets, or
  battle-specific render passes?
- In stages like `s062`, what game-state path enables/disables addressable SML
  overlay entries such as the nub groups `1..4`, and is this controlled through
  the type `0` selector buckets, a later runtime table, or battle script/state
  outside the SST command block?
- Does any SST command/type `0` setup path create or alter the active stage
  worksheet at `StageWksht_PTR_80347450`, or is StageWorksheet runtime state a
  field/script/MLD system outside direct battle-stage SST control? The
  `FUN_8011622c` bridge itself is now classified as script/SceneTable/MLD
  runtime context, but the broader source of SML-derived worksheet ids remains
  unresolved.

## Next Milestones

1. Trace the model/render helper names below active row `+0x10`, especially
   `FUN_8022f408`, `FUN_8022fce4`, `FUN_8022f6b8`, and `FUN_80230bd4`, then
   decide whether the secondary buffer should be annotated as model-effect,
   render-state, or object-control state.
2. Search for an indirect consumer that explains the `DAT_80309e84`
   `recordCount * 0x2c` allocation width. Until then, keep parser/research
   metadata limited to the proved `0x14` active-row stride.
3. Search for battle script or SceneTable category 6 entries that address
   SML-derived worksheet/object ids in annotated stages such as `s062` and
   `s044`. `SCPT::INST::INST137_80207c2c` is the script-level setter for
   StageWorksheet proxy target id, but the unresolved part is mapping those ids
   back to SML records.
4. Audit whether any indirect battle-stage setup path can call the generic
   MLD/STD command-list materializer `FUN_8007e13c`; current direct evidence
   says it is a sibling path for direct named MLD/STD resources.
5. Align parser/research metadata wording with the resolved local model/object
   slot interpretation for payload `+0x00` fields, while preserving same-index
   SML/SST context as structural evidence and keeping the separate type `1`
   four-slot runtime table distinct.
6. Trace and, if appropriate, propose annotations for the remaining unnamed
   type `1` lighting/render-environment helpers and globals:
   `DAT_80344dec`, `DAT_8034535c`, and their helper groups.
7. Compare type `3` against annotated stages with visible texture-coordinate
   offset behavior, especially repeated type `3` payloads in `s062`, to decide
   whether the command should be described as static UV offset, texture scroll
   setup, or a one-shot model-data coordinate fixup.
8. Export and visually annotate the three known type `2` stages (`s008`,
   `s017`, `s018`) to confirm which geometry is deformed and whether payload
   `+0x28` byte `1` matches the observed X/Y/Z displacement axis.
9. Trace the type `11` child type `12` local block: target vector pointer,
   related ramp object pointer, vector delta fields, ramp state, axis selector,
   cycle count, and frame counter.
