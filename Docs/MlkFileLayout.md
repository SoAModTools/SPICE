# MLK File Layout

MLK is a battle-resource container whose records usually contain embedded MLD-like payloads.

## Encoding

Known files are AKLZ-wrapped. After decompression, numeric fields are big-endian and all offsets are relative to the decoded MLK buffer.

## Header

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `headerWord0` | Preserved header word; exact meaning unknown. |
| `0x04` | 2 | `recordCount` | Signed record-table count. |
| `0x06` | 2 | `headerWord1Low` | Preserved header halfword, commonly `0xFFFF`. |
| `0x08` | variable | `records` | Start of the record table. |

## Record Table

Each record is `0x10` bytes.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `key` | Resource key. |
| `0x04` | 4 | `payloadOffset` | Decoded-buffer-relative payload offset. |
| `0x08` | 4 | `payloadSize` | Payload size in bytes. |
| `0x0C` | 4 | `rawWord12` | Unresolved record metadata. |

The table normally contains `recordCount` rows and is followed by the bounded payload spans. Payloads may be empty, AKLZ-wrapped, MLD-like, Ninja chunks, `POF0`, or unknown. Their internal layouts belong to the corresponding payload format rather than to MLK.

Some anomalous files have a damaged or variant header count while the first payload offset implies a coherent table boundary. This fallback is useful for classification but is not a second confirmed layout.
