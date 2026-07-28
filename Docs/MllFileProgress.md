# MLL File Progress

This document tracks MLL research status, implementation references, archive/export tooling notes, validation, boundaries, and open questions that are not strictly file layout.

## References And Context

This document captures the currently known Skies of Arcadia Legends `.mll` container layout as implemented in SPICE. It is intentionally conservative: fields below are either parsed/exported by current code, confirmed by regional corpus scans, or supported by current Ghidra handler evidence.

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

## Archive IR

`MllArchiveIrBuilder` converts a parsed `MllFile` into an intermediate container representation intended for safe inspection and export tooling.

The IR preserves:

- source path and AKLZ state.
- raw and decoded sizes.
- supported header/table metadata.
- the full decoded byte stream.
- each member's raw 0x14-byte name field.
- each member's payload offset, size, raw `+0x1c` word, payload kind, and a compact probe summary.

`MllArchiveIr::payloadBytes(index)` returns a span over the IR-owned decoded bytes and throws if the member index or span is invalid.

The IR deliberately keeps probe summaries compact. Full MLD, GVR, or `.bin` payload parsing should happen in the appropriate downstream parser once the member payload has been selected.

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
- `exportDecoded(file, originalDecodedBytes, options)` uses an explicit decoded byte span.

Current corpus validation confirms byte-identical decoded no-op rebuilds and AKLZ-compressed reparse round-trips for all known regional MLL files present on the local corpus:

- US: 11 files
- EU: 27 files
- JP: 10 files

## Regional Validation

Regional scans confirm the same top-level member-table shape in all known US/EU/JP `.mll` files:

- US: 11 files, 96 members, 92 MLD-like, 4 unknown.
- EU: 27 files, 326 members, 319 MLD-like, 7 unknown.
- JP: 10 files, 91 members, 87 MLD-like, 4 unknown.

All scanned files are supported, have normal table shapes, and produce no scanner warnings or errors.

The 10 files common to US, EU, and JP have identical member names at matching indexes. EU includes additional language/variant MLL files that are referenced by EU game-side path selection code.

## MLL and MLK Boundary

Current evidence supports keeping `.mll` and `.mlk` as separate container contracts. They share downstream MLD texture/model loading behavior, but not the same wrapper structure.

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

Shared handling begins after a member payload is selected and identified as MLD-like. A future MLK parser should start from MLK-specific handler evidence, not from the MLL member-table model.

## Current Open Questions

- Should indexed `.bin` record `+0x0c/+0x10` be named simply width/height or more specifically clipping/layout extents after deeper consumer naming?
- Which surrounding MLK/resource manifests supply texture bytes for no-inline standalone MLD `word24=0` reference tables?
- Is `rawWord1c` always a sentinel for MLL records, or does any future corpus show a meaningful non-`0xffffffff` variant?
