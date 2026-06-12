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
- `planning/Analysis/2026-06-12_sst_type8_type9_semantics`
- `planning/Analysis/2026-06-12_sst_type8_type9_runtime_consumers`
- `planning/Analysis/2026-06-12_sst_type3_type4_type10_runtime_consumers`
- `planning/Analysis/2026-06-12_sst_type2_runtime_consumers`
- `planning/Analysis/2026-06-12_sst_type2_runtime_structure`
- `planning/Analysis/2026-06-12_sst_type11_boundary`
- `planning/Analysis/2026-06-12_sst_type11_runtime_structure`
- `planning/Analysis/2026-06-12_sst_type6_type7_code_paths`
- `<local-analysis>\Analyses\20260611_1448_battle_ui`
- `<local-analysis>\Analyses\20260611_0843_script_inst_ops`

## Scope

`.sml` and `.sst` are paired battle-stage files under `battle/`, normally named
`sNNN.sml` and `sNNN.sst`. They should be parsed together when higher-level
meaning is needed:

- `.sml` owns a table of embedded MLD payloads.
- `.sst` owns per-stage command blocks that reference runtime objects/models.
- `FUN_8000cb44` walks both files by the same top-level record index.

`SpiceSstSml` currently provides read-only parsing only. It does not export,
repack, or wire these formats into `SpiceFileParsing` top-level dispatch.

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
modeled SST-to-SML links are stable across US, EU, and JP in the local battle
corpora.

## Game-Side Load Path

Evidence level: `US+Gekko`

Important functions:

| Function | Address | Current role |
| --- | ---: | --- |
| `SetupBattle_8000abcc` | `8000abcc` | Builds `s%03d.sml` and `s%03d.sst` paths. |
| `Battle::Setup::loadBattleStageFiles` | `8000c9f8` | Loads SML into the first buffer and SST into the second. |
| `FUN_8000cb44` | `8000cb44` | Joins SML and SST by top-level record index. |
| `FUN_8000c8ac` | `8000c8ac` | Consumes one SML record and copies its embedded MLD payload. |
| `FUN_8000c7c0` | `8000c7c0` | Walks SST command blocks and patches command payload pointers. |
| `FUN_8000c19c` | `8000c19c` | Dispatches most patched SST command records. |
| `FUN_8000bb28` | `8000bb28` | Downstream type `0` runtime-row setup; first confirmed semantic reads of copied type `0` data. |

`FUN_8000cb44` reads the SML record count from SML `+0x04`, patches SML record
`+0x04` by adding the SML base address, then calls `FUN_8000c8ac`.

For SST, the same function patches each top-level SST record `+0x0c` by adding
the SST base address, then passes the command block to `FUN_8000c7c0`.

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
| `0x00` | 4 | `rawWord0` | Likely model/index/name component. Preserve raw. |
| `0x04` | 4 | `embeddedMldOffset` | Base-relative file offset of embedded MLD payload. Patched to a runtime pointer by the game. |
| `0x08` | 4 | `embeddedMldSize` | Embedded MLD payload byte size. |
| `0x0C` | 4 | `rawWord12` | Unknown. Preserve raw. |

`FUN_8000c8ac` formats `s%02d%02d.mld`, allocates `embeddedMldSize` bytes,
copies from `embeddedMldOffset`, and passes the copied payload to
`loadTextures_801db124` as MLD data.

Across US, EU, and JP:

- embedded MLD payloads per region: `1285`
- embedded MLD size range: `384..1052140`
- unique embedded MLD hashes: `922`
- out-of-bounds embedded MLD spans: `0`

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
| `0x00` | 4 | `rawWord0` | Unknown top-level field. Preserve raw. |
| `0x04` | 4 | `rawWord4` | High halfword of record 0 matches record count. Preserve raw. |
| `0x08` | 4 | `rawWord8` | Unknown top-level field. Preserve raw. |
| `0x0C` | 4 | `commandBlockOffset` | Base-relative SST command-block offset. Patched to a runtime pointer by the game. |

The SML and SST top-level record counts agree for every US, EU, and JP
same-stem battle pair checked so far.

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
| `0` | One command appears first in every command block and aggregate count equals the SML top-level record count. `FUN_8000c19c` copies the full `0x4c` payload as words into a runtime row; it does not directly interpret fields with `lha`, `lhz`, or `lfs`. Runtime consumers read `+0x16` as a signed lookup/resource index and `+0x18` as a signed battle object class selector. Corpus probing shows `+0x16` is always `0`, `+0x18` raw words are `selector << 16`, `+0x34/+0x38/+0x3c` are always `0x3f800000`, and `+0x44` is either `0` or `0x02000000`; keep transform/flag names provisional until downstream consumers are traced. |
| `1` | Two `0x68` subrecords. `FUN_8006b774` reads each subrecord at `+0x00`, `+0x02`, `+0x08`, and `+0x64`. Observed first subrecord active byte is `4`; second is `-1`. |
| `2` | Creates child type `4` / `FUN_8000e0d8`. `+0x00 i16` model index, `+0x02 u16` node traversal lookup key. Payload `+0x04..+0x40` is copied to child-local `+0x0c..+0x48`; local `+0x04` is forced to `-1`. Current evidence identifies this as a model-data point/vertex coordinate deformation effect: helper code scans model chunks `0x20..0x37`, snapshots 3-float coordinate triples, computes per-point distance weights, and writes selected X/Y/Z components back into the model-data coordinate array. |
| `3` | `+0x00 i16` model index, `+0x02 u16` lookup key, `+0x04/+0x06 i16` parameters. |
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
- resolved links to SML records: `478` per region
- unresolved links: `0`
- observed nonzero command arguments: `0`
- unsupported command types: `0`
- type `0` appears exactly once in every command block and is always the first
  command: `1285/1285` command blocks per region
- type `1` appears exactly once per battle stage: `136/136` stages per region

In the current corpus every observed model-index candidate is `0`, so the
field role is Gekko-backed but its higher-level semantics still need runtime
object context.

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
  `11` all resolve to SML record `0` in the current corpus
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
- `8000bcd0` stores the runtime object pointer in a table indexed by the same
  top-level row index

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
| `+0x1c..+0x26` | Sparse raw halfwords in corpus, but only copy evidence in this pass. |
| `+0x34/+0x38/+0x3c` | Strong scale/default triplet candidate by corpus bit pattern, but current Gekko evidence is raw word copy only. |
| `+0x44` | Raw flag-word candidate by corpus values, but current Gekko evidence is raw word copy only. |

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
  MLD/resource-side data hanging off the loaded SML/MLD runtime object
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

### Type 8/9 Command Correlation Pass

Evidence level: `US+Gekko`, `US/EU/JP stable` for corpus patterns.

The focused type `8`/`9` pass is tracked locally under:

```text
planning/Analysis/2026-06-12_sst_type8_type9_semantics/
planning/Analysis/2026-06-12_sst_type8_type9_runtime_consumers/
```

Generated local-only artifacts are under:

```text
SpiceSstSml/research/results/20260612_095327_type8_type9_command_correlation/
SpiceSstSml/research/results/20260612_102035_type8_type9_command_correlation/
tools/ghidra/analyses/20260612_100000_sst_type8_type9_semantics/
tools/ghidra/analyses/20260612_103000_sst_type8_type9_runtime_consumers/
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

Both type `8` and type `9` model-index fields resolve in every observed row. In
the current corpus every observed model index is `0`, so these commands attach
behavior or child state from the stage's first SML record to other command
blocks. This is stable corpus behavior, but does not yet prove whether `0` is a
hard-coded stage root, effect source, or some other battle-local model object.

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
| `+0x0c` | mostly `0`; smaller nonzero values occur; consumed as final-frame hold duration |
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
- child type `10` (`FUN_8000d8fc`) reads the type `9 +0x08` value from child
  parameter block `+0x04`, calls `FUN_800120d0`, and writes an orientation/Y
  angle at the attached runtime model/object pointer `+0x2c`.

Current interpretation: types `8` and `9` are battle child/menu setup commands,
not static geometry records. Type `8` creates a child type `9` UV/texture
coordinate animation over a node selected by ordinal traversal of the selected
model object's node tree. Type `9` creates a child type `10` orientation updater
for an attached runtime model/object.

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
| `+0x20 f32` | local `+0x28` | Maximum distance/range for `FUN_80230e48`; values `300.0`, `1400.0`. |
| `+0x24 u32` | local `+0x2c` | Zero on disk; becomes helper-allocated source-coordinate snapshot buffer pointer. |
| `+0x28 u32` | local `+0x30` | Packed control word. Byte `0` controls which center-vector components are seeded from source coordinates in `FUN_80230634`; observed byte `0` values are `0`, `1`, `2`. Byte `1` selects the target coordinate in `FUN_8000b320`: `0`/other = Y, `1` = X, `2` = Z. |
| `+0x30/+0x34 f32` | local `+0x3c/+0x40` | Optional Y min/max filter; no filter when equal. Both are zero in current corpus. |
| `+0x38/+0x3c f32` | local `+0x44/+0x48` | Optional X/Z radial bounds filter; no filter when equal. Both are zero in current corpus. |

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
| model-index candidate links resolved | `183` |

Callback/runtime mapping:

| SST type | Setup path | Runtime child type/callback | Current role |
| ---: | --- | --- | --- |
| `3` | Inline branch in `FUN_8000c19c` | child type `5`, `FUN_8000e02c` | Creates a child that advances a local counter/limit and calls `FUN_802307ec` while active. |
| `4` | `FUN_8000c014` | child type `6`, raw range `8000df50..8000e02c` | Creates per-frame signed deltas and applies them to object offsets `+0x28/+0x2c/+0x30`. |
| `10` | `FUN_8000bf48` | child type `11`, `FUN_8000d734` | Creates vector interpolation/oscillation state and delegates application/clamping to `FUN_8000d488`. |

Type `3` direct evidence:

- `+0x00` is read as signed model index and bounds-checked against the active
  model/object count.
- `+0x02` is read as an unsigned lookup/node traversal key and passed through
  `FUN_8006c9ac`.
- `+0x04` and `+0x06` are copied into child-local data, but the currently
  exported child callback reads its own counter/limit fields rather than these
  original payload offsets. Keep them unresolved until another consumer path is
  identified.
- Corpus values are stable across regions: model index is always `0`;
  lookup keys range from `2` through `9`.

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
- model-index candidate resolution to SML records

It intentionally does not model:

- embedded MLD internals as native `SpiceSstSml` structures
- runtime child/menu object structures created by SST command consumers
- type `11` trailing bytes as part of the structural walker payload
- stable semantic names for corpus-only command payload fields
- export/repackaging
- production CLI dispatch through `SpiceFileParsing`
- regional differences beyond reporting/validation metadata

## Representative Stage Facts

Representative US examples from the command-payload probe:

| Stem | SML records | SML decoded size | Command types |
| --- | ---: | ---: | --- |
| `s001` | `10` | `1825396` | `{0: 10, 1: 1}` |
| `s002` | `5` | `810676` | `{0: 5, 1: 1, 10: 1}` |
| `s006` | `28` | `2846288` | `{0: 28, 1: 1, 8: 9}` |
| `s021` | `4` | `498096` | `{0: 4, 1: 1, 11: 1}` |
| `s150` | `9` | `731056` | `{0: 9, 1: 1, 3: 1}` |

## Open Questions

- What do SML record `+0x00` and `+0x0c` mean, and are they directly related to
  the embedded payload function name, MLD entry identity, or SST top-level
  record index?
- What do SST top-level record `+0x00`, `+0x04`, and `+0x08` mean beyond the
  observed count-bearing high halfword in record `0`?
- Are `NMDM` payloads motion/model data blocks, and how do they relate to the
  embedded MLD motion address list and the runtime motion lookup helpers?
- What is the exact per-entry `texturesPointer` substructure for embedded SML
  payloads, especially in the common case where per-entry texture names are
  empty but the archive-level texture table contains `GCIX`/`GVRT` chunks?
- For type `0`, which copied fields after `+0x18` are later consumed as
  transforms, flags, or runtime state? Current direct evidence names `+0x16`
  and `+0x18`; `+0x34/+0x38/+0x3c` and `+0x44` remain corpus-backed only.
- For type `8`, are the texture-coordinate animation field names exact enough
  to expose as parser-facing semantic metadata, or should they remain
  conservative UV/tile/frame descriptors until the selected node/model-part
  structure is named?
- For type `9`, are corpus-only payload fields `+0x02`, `+0x04`, and `+0x0a`
  read by another path, or are they padding/unused for this command?
- For type `3`, are payload `+0x04` and `+0x06` consumed by another
  callback/global path, or are they copied but unused for the currently
  observed child type `5` behavior?
- For type `2`, what are the formal engine names for the model-data chunks
  `0x20..0x37`, the stride lookup tables at `802f9340`/`802dae90`, and packed
  control byte `0` modes `0..4`?
- For type `10`, what exact runtime vector is targeted through
  `FUN_8006b6f4(0)` and related global state, and should mode `2` eventually
  be named oscillation/bounce?
- For type `11`, what are the formal engine names for the target vector object
  and related ramp object, and is trailing `+0x24` consumed outside the currently
  traced child type `12` path?
- For code-supported but corpus-absent types `6` and `7`, are these unused
  battle effects, region/build leftovers, or commands used by non-battle/custom
  data not present in the current local corpora?
- Where exactly is the runtime object/resource table built from SML embedded
  MLD records, and how does it relate to the model pointer table read by
  type `8`/`9` consumers?

## Next Milestones

1. Add an optional research/debug path that passes extracted SML embedded MLD
   payloads to `SpiceMLD` and reports header, index-entry, archive texture
   count, and marker-order summaries without making `SpiceSstSml` depend on
   `SpiceMLD` in the core parser.
2. Trace the loader path that converts SML embedded MLD entries into the runtime
   object/resource table used by SST command consumers, especially the table
   read by type `8`/`9` model-index paths.
3. Add an optional research/JSON dump over `SpiceSstSml` parse results so the
   evidence level, provisional status, raw payload bytes, model-index links,
   and type `11` consumer windows can be inspected without writing a C++
   harness or committing local corpus outputs.
4. Add Ghidra annotations for the type `2` helper path: label the located
   model-data chunk pointer, source-coordinate snapshot buffer, distance/weight
   buffer, and target coordinate selector before promoting those names into the
   parser metadata.
5. Add Ghidra annotations for the type `11` child type `12` local block:
   target vector pointer, related ramp object pointer, vector delta fields,
   ramp state, axis selector, cycle count, and frame counter.
