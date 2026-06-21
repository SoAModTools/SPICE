# MLL File Layout

This document captures the currently known Skies of Arcadia Legends `.mll`
container layout as implemented in SPICE. It is intentionally conservative:
fields below are either parsed/exported by current code, confirmed by regional
corpus scans, or supported by current Ghidra handler evidence.

Primary implementation references:

- `SpiceMll/MllParser.cpp`
- `SpiceMll/MllModel.h`
- `SpiceMll/MllArchiveIr.h`
- `SpiceMll/MllBinaryExporter.h`
- `SpiceMll/MllCorpus.cpp`
- `SpiceMll/MllCorpusScanMain.cpp`
- `SpiceMll/StandaloneMldTextureScan.cpp`
- `SpiceTests/test_mll_parser.cpp`
- `SpiceTests/test_mll_corpus_export.cpp`

## Platform and Endian

MLL files are GameCube data and current SPICE parsing treats all numeric fields
described here as big-endian after optional AKLZ decompression.

MLL input may be AKLZ-wrapped. `MllParser::parseFile` detects AKLZ,
decompresses it, and parses the decoded bytes. `MllFile::rawSize` records the
input byte size, `decodedSize` records the post-decompression byte size, and
`sourceWasCompressedAklz` records whether AKLZ wrapping was present.

`MllFile::originalDecodedBytes` preserves the full decoded byte stream. This is
the byte surface used by the safe exporter and archive IR.

## Top-Level Container

The decoded MLL container starts with an 8-byte prefix followed by a fixed-stride
member record table.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `headerWord0` | Observed as `0x0000ffff` in supported US/EU/JP files. Preserved by parser/exporter. |
| `0x04` | 4 | `countWord` | High `u16` is the member count in supported files. Low `u16` is observed as `0xffff`. |
| `0x08` | `memberCount * 0x20` | member records | Fixed 0x20-byte records. |
| `memberTableEnd` | variable | payload bytes | Member payloads are addressed by absolute decoded-file offsets stored in records. |

Current supported table shape:

- `recordsOffset` is `0x08`.
- `recordStride` is `0x20`.
- `memberTableEndOffset` is `0x08 + memberCount * 0x20`.
- The first payload offset equals `memberTableEndOffset`.
- Member payload spans are in bounds and do not overlap the member table.
- All supported corpus files use the header count at `+0x04`.

The parser can also infer a provisional count from the first member offset when
the header-count hypothesis does not fit. That fallback is for research
diagnostics only; fallback parses are not considered the fully supported schema.

## Member Records

Each member record starts at `0x08 + index * 0x20`.

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 0x14 | `name` | Fixed-width member name bytes. Names are generally null-terminated ASCII. |
| `0x14` | 4 | `payloadOffset` | Absolute decoded-file offset of this member payload. |
| `0x18` | 4 | `payloadSize` | Payload size in bytes. |
| `0x1C` | 4 | `rawWord1c` | Observed as `0xffffffff` in supported regional corpus files. |

The runtime helper evidence matches this table shape when viewed from the
container base:

- member payload pointer is read from `base + index * 0x20 + 0x1c`, matching
  file record `+0x14` because records begin at file offset `0x08`.
- member size is read from `base + index * 0x20 + 0x20`, matching file record
  `+0x18`.
- total loaded size is computed as `8 + count * 0x20 + sum(member sizes)`.

## Payload Packing

Current supported files are tightly packed after the member table:

- member 0 payload starts at `memberTableEndOffset`.
- each following payload starts immediately after the previous payload.
- payload offsets are recomputed by the exporter when payload resizing is
  explicitly allowed.

The default exporter rejects source layouts with gaps between payloads. This is
intentional: the safe no-op export path currently guarantees byte-identical
decoded rebuilds only for the tight-packing shape validated in the corpus.

## Payload Kinds

MLL is a container format. Member payloads are independently classified by
lightweight probes.

Current top-level payload kinds:

| Kind | Current recognition |
| --- | --- |
| `Empty` | Zero-byte payload. |
| `AklzCompressed` | Payload itself begins with AKLZ wrapping. |
| `IndexedBin` | Payload has a strong `SpiceBin` indexed-table probe. This can override weak MLD-header overlap for `.bin` members. |
| `MldFile` | Payload has a plausible embedded MLD header plus stronger MLD evidence, such as plausible index-entry shape, explicit `.mld` member name, or texture-table/embedded-GVR evidence. |
| `NinjaChunk` | Payload starts with a recognized Ninja chunk signature. |
| `Pof0` | Payload starts with `POF0`. |
| `Unknown` | No current classifier matched. |

Classifier output is routing evidence, not a complete semantic type. In
particular, named `.bin` members can look MLD-like to static probes while still
being consumed by indexed table runtime handlers.

## MLD-Like Member Payloads

Most known MLL members are MLD-like payloads. Runtime callers commonly copy a
selected member into a fresh allocation and pass that copied payload to the MLD
texture/model load path.

The MLD-like payload header follows the general MLD layout described in
`Docs/MldFileLayout.md`, with a top-level texture table offset at payload
`+0x10`.

For MLL-contained MLD-like payloads, the texture archive surface has the same
0x2c record layout also validated in standalone MLD files:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | `textureCount` | Number of texture records. |
| `+0x04 + i * 0x2c` | 0x10 | `name` | Null-terminated texture name field. |
| `+0x14 + i * 0x2c` | 4 | `word10` | Observed zero in regular inline MLL texture rows. |
| `+0x18 + i * 0x2c` | 4 | `word14` | Observed zero in regular inline MLL texture rows. |
| `+0x1c + i * 0x2c` | 4 | `word18` | Observed zero in regular inline MLL texture rows. |
| `+0x20 + i * 0x2c` | 4 | `word1c` | Observed zero in regular inline MLL texture rows. |
| `+0x24 + i * 0x2c` | 4 | `word20` | Texture-load descriptor slot; observed zero in file-form rows, overwritten during load. |
| `+0x28 + i * 0x2c` | 4 | `word24` | File-form descriptor flags/type word; observed `0x80000000` in MLL inline rows. |
| `+0x2c + i * 0x2c` | 4 | `word28` | Source byte size of the corresponding appended GCIX/GVRT blob. |

After the table, padding aligns the appended texture byte stream to a 0x20-byte
boundary. The appended stream is a sequence of paired GCIX/GVRT texture blobs in
table-record order. Current probes parse those texture blobs through SpiceGvm
and record source size, global index, image format, dimensions, and decode
status.

Standalone MLD validation shows additional texture-table variants, especially
`word24=0` character/effect-style rows. Those are general MLD behavior and
should not be treated as MLL-specific unless seen in MLL corpus data.

## Indexed `.bin` Member Payloads

Named `.bin` members are inner payloads owned by `SpiceBin`, not by the MLL
container format. MLL stores only the member name, payload offset, payload size,
and the opaque payload bytes. Runtime evidence shows selected `.bin` payloads
are copied into member-local buffers before being passed to indexed table
handlers, so outer MLL offsets are not part of the inner `.bin` address space.

Current MLL parsing delegates indexed `.bin` probing to `SpiceBin` when either:

- the member name ends with `.bin`, case-insensitive.
- the payload kind is otherwise unknown.

See `Docs/BinFileLayout.md` for the indexed UI/render layout table schema and
the `SpiceBin` parser/probe API.

## Archive IR

`MllArchiveIrBuilder` converts a parsed `MllFile` into an intermediate
container representation intended for safe inspection and export tooling.

The IR preserves:

- source path and AKLZ state.
- raw and decoded sizes.
- supported header/table metadata.
- the full decoded byte stream.
- each member's raw 0x14-byte name field.
- each member's payload offset, size, raw `+0x1c` word, payload kind, and a
  compact probe summary.

`MllArchiveIr::payloadBytes(index)` returns a span over the IR-owned decoded
bytes and throws if the member index or span is invalid.

The IR deliberately keeps probe summaries compact. Full MLD, GVR, or `.bin`
payload parsing should happen in the appropriate downstream parser once the
member payload has been selected.

## Export and Rebuild

`MllBinaryExporter` is a conservative container rebuilder.

Default decoded export behavior:

- requires a parsed, supported MLL file.
- uses `originalDecodedBytes` as the source byte surface.
- preserves header words and raw member names.
- preserves payload bytes unless a replacement is supplied.
- requires tightly packed source payloads.
- requires `rawWord1c == 0xffffffff` by default.
- rejects payload resizing by default.

`MllExportOptions` controls the limited editable surface:

| Option | Default | Meaning |
| --- | --- | --- |
| `compressAklz` | `false` | Write the rebuilt decoded bytes through AKLZ compression. |
| `allowPayloadResize` | `false` | Permit replacement payloads whose size differs from the original. Offsets are recomputed when enabled. |
| `requireRawWord1cSentinel` | `true` | Reject records whose raw `+0x1c` word is not `0xffffffff`. |
| `payloadReplacements` | empty | Optional replacement payloads keyed by member index. |

The exporter has two entry points:

- `exportFile(file, options)` uses `file.originalDecodedBytes`.
- `exportDecoded(file, originalDecodedBytes, options)` uses an explicit decoded
  byte span.

Current corpus validation confirms byte-identical decoded no-op rebuilds and
AKLZ-compressed reparse round-trips for all known regional MLL files present on
the local corpus:

- US: 11 files
- EU: 27 files
- JP: 10 files

## Regional Validation

Regional scans confirm the same top-level member-table shape in all known
US/EU/JP `.mll` files:

- US: 11 files, 96 members, 92 MLD-like, 4 unknown.
- EU: 27 files, 326 members, 319 MLD-like, 7 unknown.
- JP: 10 files, 91 members, 87 MLD-like, 4 unknown.

All scanned files are supported, have normal table shapes, and produce no
scanner warnings or errors.

The 10 files common to US, EU, and JP have identical member names at matching
indexes. EU includes additional language/variant MLL files that are referenced
by EU game-side path selection code.

## MLL and MLK Boundary

Current evidence supports keeping `.mll` and `.mlk` as separate container
contracts. They share downstream MLD texture/model loading behavior, but not
the same wrapper structure.

MLL:

- field/menu/ending indexed member container.
- 8-byte prefix.
- named 0x20-byte records.
- zero-based member index lookup.
- selected members can be copied or passed into MLD or indexed `.bin` handlers.

MLK:

- BCHARA/BEFF character/effect resource table.
- different 0x10-byte record structure.
- payload pointer and size fields are patched/used differently.

Shared handling begins after a member payload is selected and identified as
MLD-like. A future MLK parser should start from MLK-specific handler evidence,
not from the MLL member-table model.

## Current Open Questions

- Should indexed `.bin` record `+0x0c/+0x10` be named simply width/height or
  more specifically clipping/layout extents after deeper consumer naming?
- Which surrounding MLK/resource manifests supply texture bytes for no-inline
  standalone MLD `word24=0` reference tables?
- Is `rawWord1c` always a sentinel for MLL records, or does any future corpus
  show a meaningful non-`0xffffffff` variant?
