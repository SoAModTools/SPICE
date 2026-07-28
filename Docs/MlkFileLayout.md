# MLK File Layout

This document describes the promoted MLK decoded container, record-table, count-selection, and payload-classification layouts implemented by SPICE. Runtime evidence, filename hypotheses, scanner surfaces, annotations, and open questions live in `Docs/MlkFileProgress.md`.

## Decoded File Layout

All corpus files observed so far are AKLZ-compressed on disc. `SpiceMlk` decodes AKLZ first and interprets the layout below from decoded bytes.

All integer fields are big-endian.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `headerWord0` | Preserved raw header word. Often category-looking. No confirmed semantic name yet. |
| `0x04` | 2 | `recordCount` | Signed halfword loop bound used by observed runtime walkers. |
| `0x06` | 2 | `headerWord1Low` | Preserved raw header halfword. Often `0xffff` in normal files. |
| `0x08` | variable | record table | Start of 0x10-byte MLK records. |

`MlkScanResult::headerWords` stores the first four decoded 32-bit words for triage. Because the record table begins at `0x08`, `headerWords[2]` and `headerWords[3]` are the first record's key and payload offset in normal files, not separate header fields.

## Record Table

Each observed MLK record is 0x10 bytes.

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `key` | Numeric resource key. Runtime duplicate-key validation compares this field. `MLK::LoadMlkEmbeddedMldRecord_8006e244` uses it to derive a resource label. |
| `0x04` | 4 | `payloadOffset` | Decoded-buffer-relative embedded payload offset. Runtime walkers patch this in place to an absolute pointer. |
| `0x08` | 4 | `payloadSize` | Embedded payload byte size used by the loader copy/parse path. |
| `0x0c` | 4 | `rawWord12` | Preserved unresolved metadata. Keep this name until a runtime consumer explains it. |

Scanner checks per record:

- payload span is in bounds
- payload overlaps the record table
- key duplicates an earlier record key
- payload signature
- payload kind
- embedded MLD header plausibility

## Record Count Selection

The primary count source is the signed halfword at decoded offset `0x04`, named `header-u16-at-0x04` in corpus output.

The scanner also computes an inferred count from the first payload offset:

```text
(firstPayloadOffset - 0x08) / 0x10
```

This inferred count is used only when the header-count table would extend beyond the decoded file and the first-payload-offset count is internally valid. When neither count is usable, the scan is marked `unresolved`.

Current count-source values:

- `header-u16-at-0x04`: normal runtime-observed count source.
- `first-payload-offset`: fallback for a malformed header-count table with a plausible first-payload boundary.
- `unresolved`: table bounds cannot be made coherent.

## Payload Classification

`SpiceMlk` currently classifies payloads as:

- `empty`
- `unknown`
- `aklz`
- `mld`
- `ninja-chunk`
- `pof0`

Most useful records are bounded `mld` payloads. The scanner treats a payload as plausible embedded MLD when the MLD-style header has a sane entry count, a bounded index table, and offsets that are zero or within the payload.

Embedded MLD probe fields:

- entry count
- index table offset
- function-parameter offset
- real-data offset
- texture table offset

The corpus path can run a lightweight embedded MLD parse and report entry counts, diagnostics, texture archive presence, object/ground/motion/texture reference counts, and sampled function names.
