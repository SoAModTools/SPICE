# SST/SML File Layout

This document describes the promoted SST/SML serialized layouts for paired Skies of Arcadia Legends battle-stage files. Runtime traces, semantic investigation passes, parser-boundary notes, representative stage facts, open questions, and milestones live in `Docs/SstSmlFileProgress.md`.

## Compression and Endian

Most corpus files are AKLZ-wrapped. Current parser code detects AKLZ, parses the decompressed payload, and records whether the input was compressed.

All currently modeled numeric fields are big-endian.

The known battle corpus has one raw/not-AKLZ sample in prior probes (`s006` in the original EU sample set); the parser accepts both raw and AKLZ-wrapped input.

## SML Layout

Evidence level: `US+Gekko`, `US/EU/JP stable`

SML starts with a small header, then a `0x10`-byte record table at file offset `0x08`.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `rawHeader0` | Preserved raw. In samples this encodes the stage id in the high halfword and `0xffff` in the low halfword. |
| `0x04` | 4 | `recordCountWord` | High halfword matches the top-level record count; low halfword is commonly `0xffff` in samples. |
| `0x08` | `recordCount * 0x10` | `records` | SML record table. |

Current parser note: `SpiceSstSml` exposes `recordCount` from this word for the confirmed SML table walk. Keep `rawHeader0` and record raw fields intact.

### SML Record

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `embeddedMldResourceIndex` | Low signed halfword is used as the second number in generated `s%02d%02d.mld` resource names. It equals the record index in every US/EU/JP battle row checked. High halfword is `0`. |
| `0x04` | 4 | `embeddedMldOffset` | Base-relative file offset of embedded MLD payload. Patched to a runtime pointer by the game. |
| `0x08` | 4 | `embeddedMldSize` | Embedded MLD payload byte size. |
| `0x0C` | 4 | `reservedSentinel` | Always `0xffffffff` in the current US/EU/JP battle corpora; no direct read in the audited load path. Preserve raw. |

`FUN_8000c8ac` formats `s%02d%02d.mld`, allocates `embeddedMldSize` bytes, copies from `embeddedMldOffset`, and passes the copied payload to `loadTextures_801db124` as MLD data.

Direct Gekko evidence:

- `8000c8d4`: reads SML record `+0x00`.
- `8000c8e0`: sign-extends the low halfword from that word.
- `8000c8e8`: passes it to `sprintf("s%02d%02d.mld", stageId, recordValue)`.
- `8000c900`, `8000c908`, and `8000c910`: read `+0x08`, patched `+0x04`, and `+0x08` for allocation and copy.
- no direct read of record `+0x0c` was found in `FUN_8000cb44` or `FUN_8000c8ac`.

Across US, EU, and JP:

- embedded MLD payloads per region: `1285`
- embedded MLD size range: `384..1052140`
- unique embedded MLD hashes: `922`
- out-of-bounds embedded MLD spans: `0`
- SML record `+0x00` equals the record index in `1285/1285` rows per region
- SML record `+0x0c` is `0xffffffff` in `1285/1285` rows per region

### Embedded MLD Payloads

Evidence level: `US corpus`, `US/EU/JP stable`; parser smoke test confirms `SpiceMLD` can parse extracted embedded payloads. Treat this as embedded-payload evidence, not evidence that a whole `.sml` file is itself an `.mld`.

Each SML record points at one embedded MLD-like payload. A whole `.sml` file is an SML wrapper/table and does not parse as an MLD when merely renamed to `.mld`; a smoke test with `s001.sml` produced `entry_count: 0` through the MLD entry-list path. Extracting `s001` record `0`'s embedded payload and parsing it as `.mld` produced an intelligible one-entry MLD result:

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

## SST Layout

Evidence level: `US+Gekko`, `US/EU/JP stable`

SST top-level records start at file offset `0x00`. Do not start the SST table at `0x08`; that is the SML record-table pattern.

The high halfword at SST file offset `0x04` matches the SML record count for the same stem. The first top-level SST record overlaps that count-bearing word, so the parser preserves the raw record fields as well.

### SST Top-Level Record

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `stageIdOrPreviousBlockLength` | In record `0`, high halfword is the stage id and low halfword is `0xffff`. In records `1+`, this is the previous command block's unaligned byte length. |
| `0x04` | 4 | `recordCountOrSentinel` | In record `0`, high halfword is the top-level record count and low halfword is `0xffff`; in records `1+`, this is `0xffffffff` in all checked battle rows. |
| `0x08` | 4 | `topLevelRecordIndex` | Equals the top-level record index in every checked battle row. |
| `0x0C` | 4 | `commandBlockOffset` | Base-relative SST command-block offset. Patched to a runtime pointer by the game. |

The SML and SST top-level record counts agree for every US, EU, and JP same-stem battle pair checked so far.

`FUN_8000cb44` only needs SST top-level `+0x0c` at runtime:

- `8000cc3c`: reads the current top-level record's `+0x0c` command-block offset.
- `8000cc44..8000cc48`: patches that offset to a runtime pointer in place.
- `8000cc50`: passes the patched command-block pointer to `FUN_8000c7c0`.

The other top-level fields are still useful structural metadata:

- record `0 +0x00` follows the same stage-id-word shape as SML header `+0x00`
- record `0 +0x04` is the count-bearing word read at file offset `+0x04`
- records `1+ +0x04` are `0xffffffff`
- all `+0x08` words equal the record index
- for every US/EU/JP nonzero top-level record checked, `blockOffset[i] == align8(blockOffset[i - 1] + topRecord[i].stageIdOrPreviousBlockLength)`

## SST Command Block

Evidence level: `US+Gekko`, `US/EU/JP stable`

Given a command block at `block`:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | `commandCount` | Number of command records. |
| `+0x04` | `commandCount * 0x10` | `commandRecords` | Fixed-width command record table. |
| `+0x04 + commandCount * 0x10` | `0x10` | `sentinel` | Sentinel command record; signed type is negative. |
| `+0x04 + (commandCount + 1) * 0x10` | variable | `payloadPool` | Packed command payloads in command-record order. |

`FUN_8000c7c0` walks command records, assigns each record a runtime payload pointer into the packed payload pool, then advances by the Gekko-derived payload span for that command type.

### SST Post-Command Tail

After the packed command payload pool, bytes up to the next top-level command block offset are now preserved as `postCommandTailBytes`. This tail is structural for every command block, but only top-level record `0` currently has a proved semantic consumer.

`Battle::Stage::JoinSmlSstRecords_8000cb44` calls `SST::Command::WalkBlocks_8000c7c0` with `recordIndex * 2 + 1`. `WalkBlocks_8000c7c0` writes `DAT_80309e80` only when that argument is `1`, so the stored source pointer is derived from record `0` only. The pointer is the command block base plus the command count word, command records, sentinel record, and packed command payload spans; in parser terms this is record `0` `payloadEndOffset`.

`Battle::Setup::setupBattleGrid_800840bc` consumes that pointer as an 81-byte `9x9` battle-grid terrain source. Current parser/export metadata therefore exposes `battleGridTerrainSource9x9` only for record `0` when at least 81 post-command tail bytes are available. Any bytes after the first 81 remain padding/unknown tail bytes; for `s001`, the 81-byte terrain source is followed by seven `0xff` bytes before the next command block.

The optional game-internal `11x11` mapped grid is not modeled in `SpiceSstSml`; consumers that need the internal border-expanded grid should map the exported raw `9x9` source themselves.

### SST Command Record

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `type` | Signed command type. |
| `0x02` | 2 | `argument` | Arg/subkey. Observed corpus value is always `0`. |
| `0x04` | 4 | `rawWord4` | Unknown/reserved in current samples. Preserve raw. |
| `0x08` | 4 | `rawWord8` | Unknown/reserved in current samples. Preserve raw. |
| `0x0C` | 4 | `onDiskWord12` | Overwritten at runtime with payload pointer. Do not treat as an on-disk payload offset. |

The on-disk `+0x0c` field is a runtime storage slot, not a file pointer. This is confirmed by both the SST block walker and the separate SoAInvestigate battle UI analysis, where command consumers load `record + 0x0c` after the walker has patched it.

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

Evidence level: field offsets/read widths are `US+Gekko`; semantic names are provisional unless noted.

| Type | Current field model |
| ---: | --- |
| `0` | One command appears first in every command block and aggregate count equals the SML top-level record count. `FUN_8000c19c` copies the full `0x4c` payload as words into a runtime row; setup consumers read `+0x16` as a signed lookup/resource index and `+0x18` as a signed battle object class selector. The callback selected through `FUN_800300c4` now proves selector `3` consumes `+0x1c/+0x20/+0x24` as transform vector floats, `+0x28/+0x2c/+0x30` as signed rotation/angle words, `+0x34/+0x38/+0x3c` as scale floats, and `+0x44` as a render/model action byte. Names remain provisional outside the directly traced selector path. |
| `1` | Stage lighting/render-environment setup command. `FUN_8006b774` walks up to `32` `0x68`-byte subrecords until a negative first byte at `+0x00`; current on-disk payload holds two structural rows, one active and one sentinel. `+0x02 i16` is the class/menu selector, `+0x04 u32` is a flag word, `+0x08 i16` is the runtime slot id, `+0x0c/+0x10/+0x14` are a light direction/position vector, `+0x30..+0x44` are per-slot and global/ambient RGB triplets, and `+0x48/+0x4c` are attenuation/spot scalar fields. `FUN_8006bdb4` copies each accepted row into child-local data at `+0x28`, and `FUN_8006b774` stores that copied row pointer in the four-slot runtime table used by later model-index commands. |
| `2` | Creates child type `4` / `FUN_8000e0d8`. `+0x00 i16` model index, `+0x02 u16` node traversal lookup key. Payload `+0x04..+0x40` is copied to child-local `+0x0c..+0x48`; local `+0x04` is forced to `-1`. Current evidence identifies this as a model-data point/vertex coordinate deformation effect: helper code scans model chunks `0x20..0x37`, snapshots 3-float coordinate triples, computes per-point distance weights, and writes selected X/Y/Z components back into the model-data coordinate array. |
| `3` | Creates child type `5` / `FUN_8000e02c`. `+0x00 i16` model index, `+0x02 u16` node/model-data traversal lookup key, `+0x04/+0x06 i16` signed texture-coordinate delta pair. `FUN_80230920` applies the delta pair to coordinate halfwords inside selected texture-bearing strip polychunks and wraps crossing values by `0x200`, then flushes the affected data and invalidates the vertex cache. Current evidence supports a one-shot strip UV fixup, not a frame-driven scroll command. |
| `4` | `+0x00 i16` model index, `+0x04 u32` raw/flag, `+0x08/+0x0c/+0x10 f32`, `+0x14` raw/reserved. |
| `6` | Code-supported but corpus-absent. Setup validates `+0x00 i16` model index, creates child type `7`, stores payload `+0x04 f32` as a step/scalar, stores `+0x08 i16` as a gate/mode halfword, and stores the selected runtime object pointer. Child callback `8000dec8` only applies the scalar when the copied halfword is `1`, then adds or subtracts it from the linked runtime object `+0x20` float around a constant threshold. |
| `7` | Code-supported but corpus-absent. Setup validates `+0x00 i16` model index, creates child type `8`, stores payload `+0x04 f32` as an amplitude/scalar, stores payload `+0x08 f32` as a phase step, initializes a phase accumulator from `FLOAT_80348114`, and stores the selected runtime object pointer. Child callback `FUN_8000ddfc` advances the phase, computes a sine-scaled value, and writes the integer result to the linked runtime object at `+0x20`. |
| `8` | Child/menu setup command. `+0x00 i16` model index, `+0x02 u16` node traversal ordinal/lookup key passed to `FUN_8006c9ac`, halfword parameters at `+0x04`, `+0x06`, `+0x08`, `+0x0a`, `+0x0c` copied into child-local parameter data. |
| `9` | Child/menu setup command. `+0x00 i16` model index, `+0x08 i16` parameter copied into child-local parameter data; direct Gekko evidence also stores the selected runtime model/object pointer into that child data. |
| `10` | `+0x00 i16` model index, `+0x04 u32`, `+0x08/+0x0c/+0x10 f32`, `+0x14 i16` required nonzero value. |
| `11` | Structural walker span is `0x18`. `FUN_8000be28` reads inside-span fields `+0x00`, `+0x04`, `+0x06`, `+0x08`, `+0x0c`, and `+0x10`, then also reads trailing fields `+0x20`, `+0x22`, and `+0x24` when present before the next command block. The only corpus instance has a real trailing `0x10`-byte region after the walker payload. Child type `12` / `FUN_8000cffc` uses these values as a runtime vector motion controller over offsets `+0x1c/+0x20/+0x24` with optional fade/ramp state on a related object at `+0x10/+0x50`. Keep walker size `0x18`, and expose trailing consumer bytes separately in research summaries. |
