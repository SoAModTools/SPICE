# SST/SML Battle-Stage Document and File Layout

SML and SST are an intrinsic battle-stage pair. The public SpiceSstSml contract imports both files into one mutable `SstSmlDocument`; independent SML or SST documents are not supported. The document is currently read-only with respect to persistence, so SpiceSstSml intentionally provides no writer.

## Public workflow

Use `SstSmlDocumentImporter::importFile(path)` with either member of a side-by-side, same-stem pair, or use `importBytes(smlBytes, sstBytes)` when both byte spans are already available. Import is all-or-nothing: malformed input, a missing member, count disagreement, byte-order disagreement, invalid spans, or incomplete byte ownership returns diagnostics without a partial document.

The importer automatically recognizes raw and AKLZ-wrapped inputs and structurally detects little- or big-endian numeric encoding. Callers cannot force a source platform or byte order. Source paths, SHA-256 hashes, raw and decoded sizes, wrapper kind, and detected byte order live in the two-member `SstSmlDocumentImportReceipt`, not in the document.

`SstSmlDocumentValidator` checks intrinsic document identities, member shape, command payload ownership, typed field projections, sentinels, body ownership, and keyed embedded-MLD receipts when one is supplied. A valid document reports `Valid` readiness; this is structural validity, not a promise that an SST/SML pair writer exists.

## Canonical document

`SstSmlDocument` owns the stage ID, paired header sentinels, ordered stage members, and independent SML and SST body layouts. Every stage member owns one SML record and one same-index SST record. IDs are typed, nonzero, unique within their entity category, and stable while the document is edited in memory; repeated imports and cross-document correspondence are consumer concerns.

Each SML record owns its encoded resource index metadata, reserved word, and one strict embedded-resource variant. A successfully decoded resource owns a canonical platform-neutral `MldDocument`; a failed nested decode owns the complete payload as `SmlOpaqueEmbeddedResource`. Successful nested imports do not duplicate source bytes in the stage document. Their copyable `MldImportReceipt` values are keyed by `SmlEmbeddedResourceId` in the parent import receipt for source-faithful secondary materialization.

Each SST record owns its encoded record index, later-record metadata where applicable, and one command block. A command block owns command records, logical sentinel fields, an optional identified 9x9 terrain entity, and an optional identified opaque tail. Each supported command owns typed generic fields, canonical specialized placement or lighting entities where applicable, and offset-addressed opaque fragments for every uncovered payload byte. It does not retain a duplicate full payload buffer. The validator requires those semantic and opaque ranges to cover the recognized payload exactly once. Unknown command payload boundaries are not guessed; undecoded remaining bytes stay in the explicit opaque block tail.

The document contains no source path, source platform, compression flag, byte order, decoded offsets, hashes, diagnostics, evidence labels, histograms, or runtime context.

## Analysis and secondary projections

`SstSmlDocumentAnalyzer` derives embedded-resource header inspection, command field evidence, field scope, command histograms, active-row runtime research, and same-member local-slot links from a document plus its receipt. These observations are not document state.

The existing embedded-MLD extraction, command-map JSON, stage-annotation template, and combined Blender IR workflows remain secondary tools. They now consume the canonical document, receipt, and analysis rather than exposing parser-private result structures. Combined Blender IR projects decoded resources directly through `MldBlenderIrProjector`; opaque resources are preserved for extraction and reported as unavailable for semantic projection. A constructed decoded MLD can be projected without a receipt, while byte extraction requires either its keyed import receipt or an explicit fallback MLD target.

## SML binary layout

The decoded SML header contains four adjacent U16 fields followed by a `0x10`-byte record table.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `stageId` | Battle-stage identifier. |
| `0x02` | 2 | `stageHeaderSentinel` | Normally `0xFFFF`. |
| `0x04` | 2 | `recordCount` | Number of paired stage members. |
| `0x06` | 2 | `recordCountSentinel` | Normally `0xFFFF`. |
| `0x08` | `recordCount * 0x10` | `records` | SML record table. |

Each SML record is `0x10` bytes:

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `embeddedResourceIndexWord` | Encoded embedded-resource slot metadata. |
| `0x04` | 4 | `embeddedResourceOffset` | File-relative payload offset; represented by document body order rather than stored as provenance. |
| `0x08` | 4 | `embeddedResourceSize` | Payload size; derived from the owned payload in the document. |
| `0x0C` | 4 | `reservedWord` | Normally `0xFFFFFFFF`; preserved logically. |

## SST binary layout

SST top-level records begin at decoded offset `0x00`, have a `0x10`-byte stride, and use the same count as SML. Record 0 repeats the paired stage and count header. Later records replace those two words with the preceding unaligned block length and a normally `0xFFFFFFFF` reserved word.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `stageHeaderOrPreviousBlockLength` | Paired U16 stage header in record 0; previous block length later. |
| `0x04` | 4 | `countHeaderOrReservedWord` | Paired U16 count header in record 0; normally `0xFFFFFFFF` later. |
| `0x08` | 4 | `recordIndexWord` | Encoded record index metadata. |
| `0x0C` | 4 | `commandBlockOffset` | File-relative block offset; represented by document body order. |

Each command block contains a U32 command count, `commandCount` fixed `0x10`-byte command records, one `0x10`-byte negative-type sentinel record, sequential command payloads whose sizes are determined by recognized type, and a tail extending to the next block or end of file. The first block tail begins with the 81-byte 9x9 terrain source when all preceding payload boundaries are known; otherwise undecidable bytes remain opaque.

Each command record contains signed I16 type and argument fields followed by three preserved U32 words. The final word is runtime storage in the loaded representation and is not an on-disk payload pointer. Recognized structural payload sizes are `0x4C`, `0xD0`, `0x44`, `0x08`, `0x18`, `0x00`, `0x10`, `0x14`, `0x14`, `0x0C`, `0x18`, and `0x18` for types 0 through 11 respectively.
