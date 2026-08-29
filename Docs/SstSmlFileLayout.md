# SST/SML File Layout

SML and SST are paired battle-stage files. SML indexes embedded MLD resources; SST associates each resource slot with a command block and stage metadata.

## Encoding

Known numeric fields are big-endian after optional AKLZ decompression. Both raw and AKLZ-wrapped files exist.

## SML Header and Records

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `rawHeader0` | High halfword normally contains the stage ID; low halfword is normally `0xFFFF`. |
| `0x04` | 4 | `recordCountWord` | High halfword is the record count; low halfword is normally `0xFFFF`. |
| `0x08` | `recordCount * 0x10` | `records` | SML record table. |

Each SML record is `0x10` bytes:

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `embeddedMldResourceIndex` | Low signed halfword identifies the embedded resource slot. |
| `0x04` | 4 | `embeddedMldOffset` | File-relative offset of the embedded MLD payload. |
| `0x08` | 4 | `embeddedMldSize` | Payload size in bytes. |
| `0x0C` | 4 | `reservedSentinel` | Normally `0xFFFFFFFF`; preserve raw. |

Each bounded payload is an MLD-like resource interpreted according to [MldFileLayout.md](MldFileLayout.md). The SML wrapper itself is not an MLD file.

## SST Top-Level Records

SST records begin at file offset `0x00`, have a `0x10`-byte stride, and match the SML record count for the same stage.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `stageIdOrPreviousBlockLength` | Stage word in record 0; previous unaligned block length in later records. |
| `0x04` | 4 | `recordCountOrSentinel` | Count word in record 0; normally `0xFFFFFFFF` later. |
| `0x08` | 4 | `topLevelRecordIndex` | Record index. |
| `0x0C` | 4 | `commandBlockOffset` | File-relative command-block offset. |

Later command-block offsets are normally the eight-byte-aligned end implied by the preceding record’s block length.

## SST Command Block

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `commandCount` | Number of command records. |
| `0x04` | `commandCount * 0x10` | `commandRecords` | Fixed-width command table. |
| following | `0x10` | `sentinel` | A command record whose signed type is negative. |
| following | variable | `payloadPool` | Command payloads packed in record order. |
| following | variable | `postCommandTail` | Bytes up to the next top-level block. |

The first block’s tail can begin with an 81-byte 9x9 battle-grid terrain source. Additional tail bytes remain opaque.

## SST Command Record

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `type` | Signed command type. |
| `0x02` | 2 | `argument` | Command subkey. |
| `0x04` | 4 | `rawWord4` | Preserved metadata. |
| `0x08` | 4 | `rawWord8` | Preserved metadata. |
| `0x0C` | 4 | `onDiskWord12` | Runtime storage slot, not a serialized payload offset. |

## Command Payload Spans

Payloads are sequential; their sizes are determined by command type.

| Type | Size | Status |
| ---: | ---: | --- |
| `0` | `0x4C` | Observed |
| `1` | `0xD0` | Observed |
| `2` | `0x44` | Observed |
| `3` | `0x08` | Observed |
| `4` | `0x18` | Observed |
| `5` | `0x00` | Walker-recognized |
| `6` | `0x10` | Code-supported, not observed |
| `7` | `0x14` | Code-supported, not observed |
| `8` | `0x14` | Observed |
| `9` | `0x0C` | Observed |
| `10` | `0x18` | Observed |
| `11` | `0x18` | Observed; additional following bytes may be consumed separately |
