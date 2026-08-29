# ECT File Layout

ECT files store encounter tables in one of two serialized layouts.

## Platform Encoding

Dreamcast files are raw little-endian data. GameCube files are big-endian after AKLZ decompression. Both platforms represent the same encounter-table fields.

## Encounter Table

An encounter table is `0x84` bytes.

| Offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 2 | Stage |
| `0x02` | 2 | Overall encounter rate |
| `0x04` | `32 * 4` | Encounter records |

Each encounter record is four bytes:

| Offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 2 | Encounter ID |
| `0x02` | 2 | Encounter rate |

## Flat Layout

Ordinary ECT files are a nonempty sequence of `0x84`-byte encounter tables with no additional index. The decoded file size is therefore a multiple of `0x84`.

## A099A.ECT Overworld Layout

`A099A.ECT` uses an indexed container.

| Offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 2 | Constant `0` |
| `0x02` | 2 | Constant `0xFFFF` |
| `0x04` | 2 | Index entry count |
| `0x06` | 2 | Constant `0xFFFF` |
| `0x08` | `entryCount * 0x20` | Index records |

Each index record is `0x20` bytes:

| Offset | Size | Field |
| --- | ---: | --- |
| `0x00` | `0x14` | Printable ASCII title, optionally zero-padded |
| `0x14` | 4 | Absolute payload offset |
| `0x18` | 4 | Payload size, normally `0x420` |
| `0x1C` | 4 | Constant `0xFFFFFFFF` |

Each payload contains eight consecutive encounter tables. Entry order and titles are part of the container; offsets address the decoded ECT file.
