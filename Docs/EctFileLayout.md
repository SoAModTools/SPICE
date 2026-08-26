# ECT File Layout

This document describes the canonical encounter-table representation implemented by `SpiceEct` for Dreamcast Skies of Arcadia and GameCube Skies of Arcadia Legends.

## Platform Encoding

Dreamcast and GameCube ECT files contain the same semantic records. Platform differences are limited to byte order and the conventional outer encoding:

- Dreamcast files are raw and store numeric fields little-endian.
- GameCube files are AKLZ-compressed and store decoded numeric fields big-endian.

Parsing detects AKLZ automatically. An AKLZ input is decoded as GameCube byte order; a raw input is decoded as Dreamcast byte order. The resulting `EctFile` does not retain its source platform, endian, compression state, source offsets, or source bytes.

Writing requires a target platform. Dreamcast output is always raw little-endian data. GameCube output is always big-endian data wrapped in AKLZ.

## Editable Intermediate Representation

`EctFile` contains a typed variant of:

- `EctFlatContent` for ordinary ECT files.
- `EctOverworldContent` for the special `A099A.ECT` indexed overworld file.

Each encounter table owns its stage, overall encounter rate, and exactly 32 encounter records. Each encounter record owns an encounter ID and encounter rate. An overworld index entry owns its title and exactly eight encounter tables.

The two layouts cannot coexist in one `EctFile`. Indexed record offsets, sizes, fixed words, and other serialized bookkeeping are rebuilt by the writer and are not part of the editable model.

## Flat ECT Layout

All ECT files except `A099A.ECT` are flat sequences of `0x84`-byte encounter tables. A flat file must be nonempty and its decoded size must be a multiple of `0x84`.

Each table has this layout:

| Offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 2 | Stage |
| `0x02` | 2 | Overall encounter rate |
| `0x04` | `32 * 4` | Encounter records |

Each encounter record contains:

| Offset | Size | Field |
| --- | ---: | --- |
| `+0x00` | 2 | Encounter ID |
| `+0x02` | 2 | Encounter rate |

## A099A.ECT Overworld Layout

`A099A.ECT` is an indexed container. `EctParser::parseFile` selects this layout only when the case-insensitive basename is exactly `A099A.ECT`. Byte-only callers select `EctLayout::OverworldIndexed` explicitly.

The decoded header is four 16-bit words:

| Offset | Size | Value |
| --- | ---: | --- |
| `0x00` | 2 | `0` |
| `0x02` | 2 | `0xFFFF` |
| `0x04` | 2 | Index entry count |
| `0x06` | 2 | `0xFFFF` |

The header is followed by `entryCount` records of `0x20` bytes:

| Record offset | Size | Field |
| --- | ---: | --- |
| `0x00` | `0x14` | Printable ASCII title, optionally zero-padded |
| `0x14` | 4 | Absolute payload offset |
| `0x18` | 4 | Payload size, canonically `0x420` |
| `0x1C` | 4 | Canonical trailing word `0xFFFFFFFF` |

Every payload contains eight consecutive `0x84` encounter tables. Entries whose titles begin with `dam` use the same structure and are parsed normally; they are not skipped or treated as opaque data.

The writer preserves entry order and titles, emits the canonical header and record constants, places payloads contiguously after the index, and recalculates every payload offset.

## Corpus Validation

The US Dreamcast and GameCube corpora contain the same 35 ECT filenames. After GameCube AKLZ decompression and endian conversion, all 35 pairs are byte-identical. The observed `A099A.ECT` contains 135 index entries and 1,080 encounter tables, including 95 `dam*` entries.
