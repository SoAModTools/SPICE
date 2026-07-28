# BIN File Layout

This living document describes promoted `.bin` file layout information for the SPICE `SpiceBin` project and how parsers/exporters should use it.

## Scope

`SpiceBin` owns parsing and layout documentation for `.bin` payloads. Keep detailed research notes, copied samples, decoded tables, Ghidra outputs, generated images, local scripts, and other evidence under `SpiceBin/research/`; that folder is intentionally local-only and gitignored.

Use this tracked document for durable file-layout conclusions that are ready to drive parser and exporter behavior. Use `Docs/BinFileProgress.md` for current research status, runtime trace summaries, companion asset hypotheses, open questions, and investigation workflow notes. Use `Docs/BinFileRecords.md` for per-file one-to-one record identities.

## Format Families

The `.bin` extension is not one single confirmed binary format. The currently promoted layout family is the HRSBin-style indexed UI/render layout table. `field/wmaparea.BIN` is a known counterexample: it is consumed as a fixed-size world-map area/menu table and should not be parsed as an indexed UI/render layout table.

## HRSBin-Style Indexed UI/Render Layout Tables

This family is big-endian. It appears both as loose files and as embedded members inside MLL archives.

### Runtime Object

Runtime evidence identifies three entry points into the same in-memory indexed object:

| Function | Source kind | Behavior |
| --- | --- | --- |
| `FUN_801d6998(object, payload)` | Already-loaded member payload | Initializes the 0x14 indexed object directly from a payload pointer. |
| `FUN_801d6b58(object, path)` | Loose file path | Loads a path-backed payload, stores the loaded payload pointer at object `+0x10`, and initializes the indexed object. |
| `FUN_801d6a74(object, cacheIndex)` | Battle preload/cache index | Resolves a battle cache payload, stores the payload pointer at object `+0x10`, and initializes the indexed object. |

The shared runtime object layout is:

| Object offset | Size | Runtime field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `recordBase` | `payload + 0x04 + count * 4`. |
| `0x04` | 4 | `recordCount` | Top-level record count from payload `+0x00`. |
| `0x08` | 4 | `recordOffsets` | Pointer to the top-level offset table at payload `+0x04`. |
| `0x0C` | 4 | `recordScratch` | Allocated `recordCount * 4` scratch/working pointer table, zeroed by the initializer. |
| `0x10` | 4 | `payloadBase` | Loaded or member-local payload base. |

For every top-level record, the runtime initializer overwrites record `+0x00` with `recordBase` and rebases record `+0x04` by adding `payloadBase`. Exporters should write file-form offsets, not runtime-fixed absolute pointers.

### File Header

The top-level file shape is:

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `count` | Number of top-level records. |
| `0x04` | `count * 4` | `recordOffsets` | Big-endian offsets relative to `dataBaseOffset`. |
| `0x04 + count * 4` | variable | `recordData` | Record data area. |

For record `i`, the record address is:

```text
dataBaseOffset = 0x04 + count * 4
recordOffset = dataBaseOffset + recordOffsets[i]
```

### Top-Level Record

Known top-level record fields:

| Offset in record | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `fixedDataTablePointer` | Fixed-data table pointer after runtime fixup. File-form samples commonly equal `dataBaseOffset` before fixup. |
| `0x04` | 4 | `elementTablePointer` | Element table pointer after runtime fixup. File-form samples point inside the payload before fixup. |
| `0x08` | 4 | `elementCount` | Number of 0x34-byte element records. |
| `0x0C` | 4 | `layoutWidth` | Big-endian float layout width or X extent. Name remains provisional. |
| `0x10` | 4 | `layoutHeight` | Big-endian float layout height or Y extent. Name remains provisional. |
| `0x14` | 4 | `baseX` | Base X or mutable layout X offset. |
| `0x18` | 4 | `baseY` | Base Y or mutable layout Y offset. |

### Element Record

Element records are currently understood as 0x34 bytes:

| Offset in element | Size | Field | Runtime use |
| --- | ---: | --- | --- |
| `0x00` | 4 | `fixedDataIndex` | Indexes a 0x14-byte fixed-data record through `fixedDataTablePointer + fixedDataIndex * 0x14`. |
| `0x04` | 4 | `dstLeft` | Destination quad left/local X. Added to caller/record base X before vertex submit. |
| `0x08` | 4 | `dstTop` | Destination quad top/local Y. Added to caller/record base Y before vertex submit. |
| `0x0C` | 4 | `dstRight` | Destination quad right/local X. Added to caller/record base X before vertex submit. |
| `0x10` | 4 | `dstBottom` | Destination quad bottom/local Y. Added to caller/record base Y before vertex submit. |
| `0x14` | 4 | `packedTint` | Treated as packed signed-byte tint/color/alpha channels and scaled by caller alpha/color factors before rendering. |
| `0x18..0x30` | 24 | `preservedTail` | Preserve-only opaque bytes. Checked render/descriptor paths do not read these serialized bytes, but exporters must preserve them until each slot is proven. |

The current Ghidra drill-down checked PPC disassembly around the known 0x34-byte element loops. No checked serialized-element consumer reads `+0x18..+0x30`, but this is not proof that no unidentified function can inspect those bytes.

Several field-side helper functions operate on runtime clone/control objects that are larger than the serialized top-level record header:

| Function | Runtime clone fields | Meaning |
| --- | --- | --- |
| `FUN_801c3618(clone, a, b)` | Writes shorts at clone `+0x46` and `+0x44`. | Stores runtime state used by field-side update tests. |
| `FUN_801c3624(clone)` | Reads clone `+0x46`. | Returns whether that runtime state is non-zero. |
| `FUN_801c368c(x, y, clone, steps, state)` | Writes clone `+0x0C`, `+0x10`, `+0x14`, `+0x18`, `+0x42`, and `+0x48`. | Sets target extents/interpolation deltas and runtime state. |
| `FUN_801c377c(y, clone, steps, state)` | Writes clone `+0x10`, `+0x18`, `+0x0C`, `+0x14`, `+0x42`, and `+0x48`. | One-axis variant of the same clone interpolation setup. |
| `FUN_801c37d4(x, clone, steps, state)` | Writes clone `+0x0C`, `+0x14`, `+0x10`, `+0x18`, `+0x42`, and `+0x48`. | One-axis variant of the same clone interpolation setup. |

Those clone offsets are runtime fields and should not be added to the serialized 0x34-byte element schema.

### Fixed Data Record

Fixed-data records are currently understood as 0x14 bytes:

| Offset in fixed data | Size | Field | Runtime use |
| --- | ---: | --- | --- |
| `0x00` | 4 | `contextEntryIndex` / material selector | First fixed-data word. Direct submitter paths do not read this word and receive texture/render state from the active context. Descriptor/queue submitter paths copy this word into a transient descriptor short and later pass it to `FUN_80298fe4`, which indexes the active context entry table. Preserve it as an engine context selector, not as an ordinal into extracted GVR files. |
| `0x04` | 4 | `sourceLeft` | Source/UV rectangle left coordinate. |
| `0x08` | 4 | `sourceTop` | Source/UV rectangle top coordinate. |
| `0x0C` | 4 | `sourceRight` | Source/UV rectangle right coordinate. |
| `0x10` | 4 | `sourceBottom` | Source/UV rectangle bottom coordinate. |

### Render Interpretation

`FUN_801d68c4` returns a top-level record pointer from an initialized object. Direct draw callers iterate `record.elementCount`, read each 0x34-byte element, use `element.fixedDataIndex` to select a fixed-data record, build four vertices from `record.baseX/baseY` plus element destination coordinates, copy UVs from fixed-data offsets `0x04..0x10`, scale `element.packedTint`, and submit a four-vertex quad.

In checked direct submitter paths, texture selection comes from the active render context rather than fixed-data word `0x00`. In descriptor/queue submitter paths, fixed-data word `0x00` selects a context entry only after the active context pointer has been set. Parser/exporter models should therefore store the raw selector and allow companion texture/material context metadata to be provided separately.

## Companion Context Metadata

HRSBin-style `.bin` payloads describe layout and texture-reference metadata; they do not contain embedded GVR texture chunks in the checked loose samples. A descriptive editor/exporter should layer companion-bank metadata on top of the parsed `.bin` model instead of treating the `.bin` payload as self-contained image data.

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

The `slotMapping` distinction matters because extracted texture lists based only on GCIX/GVRT offsets may not match the engine-facing material slot order. Until SpiceMLD/SpiceGvm expose exact engine slot lists, editor previews should make the active companion-bank mapping explicit and preserve unresolved material-slot values.

Companion-bank resolution should use the strongest available source, in this order:

1. Known runtime consumer mapping from Ghidra traces.
2. Archive/container context, when the `.bin` is an embedded MLL member with a local material bank.
3. Explicit editor/exporter sidecar metadata supplied by the user or project.
4. Filename/path heuristics only as a low-confidence preview fallback.

## Fixed World-Map Area Table

`field/wmaparea.BIN` is a fixed-size world-map area/menu table, not an HRSBin-style indexed UI/render layout table. The exact per-entry schema is not promoted here yet.

Known promoted constraints:

| Property | Value |
| --- | --- |
| Family | Fixed world-map area/menu table. |
| Indexed HRSBin-style probe | Negative. |
| US/EU decoded size | `0x7e0` bytes. |
| JP decoded size | `0x7e0` bytes after AKLZ decode. |
| Exporter handling | Preserve raw table until the per-entry schema is promoted. |

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

`BinFile::indexedTableProbe` stores the same probe result after `parseBytes` or `parseFile`. Archive parsers such as `SpiceMll` should use `SpiceBin` for this inner payload classification instead of duplicating `.bin` layout logic.

Scan a loose `.bin` corpus from the command line:

```text
SpiceBinCorpusScan <input_file_or_dir> <output_dir>
```

The scanner accepts a single `.bin` file or a directory tree, decodes AKLZ-wrapped inputs before probing, and writes:

- `bin_corpus_files.csv`
- `bin_corpus_indexed_tables.csv`

## Known Fields

The HRSBin-style indexed UI/render layout table field map above is the current promoted field map. Names for `layoutWidth`, `layoutHeight`, `baseX`, and `baseY` remain provisional until deeper consumer naming confirms whether they should be called dimensions, clipping extents, or mutable layout offsets.
