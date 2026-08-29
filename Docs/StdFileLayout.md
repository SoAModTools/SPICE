# STD File Layout

STD data is big-endian after AKLZ decompression. Two related serialized forms are known: action-row tables named like `%s_STD` and entry/payload tables named like `%s0_STD`.

## Action-Row Table

The header is `0x10` bytes and rows begin immediately afterward.

| Header offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `commandLow` | Low half of the combined table kind. |
| `0x02` | 2 | `commandHigh` | High half of the combined table kind. |
| `0x04` | 4 | `loaderContextWord` | Preserved source word. |
| `0x08` | 4 | `rowCount` | Number of `0x18`-byte action rows. |
| `0x0C` | 4 | `rowTablePointer` | Preserved source word. |

The known action table kind is `(commandHigh << 16) | commandLow == 0x00010000`.

Each action row is `0x18` bytes:

| Row offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 2 | Signed action ID |
| `0x02` | 2 | Signed row type |
| `0x04` | 2 | Callback index |
| `0x06` | 2 | Callback-local ordinal |
| `0x08` | 4 | Flags |
| `0x0C` | 2 | Secondary key |
| `0x0E` | 2 | Callback-local auxiliary parameter |
| `0x10` | 4 | Callback-local timing or gate value |
| `0x14` | 4 | Callback-local progress value |

Fields after the callback index are interpreted by the selected callback family and otherwise remain opaque.

## Entry/Payload Table

The header is `0x10` bytes.

| Header offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `recordCountIncludingSentinel` | Entry count including the terminating row. |
| `0x02` | 2 | `kind` | Table kind. |
| `0x04` | 4 | `reserved0` | Preserved. |
| `0x08` | 4 | `reserved1` | Preserved. |
| `0x0C` | 4 | `decodedSpanMinusHeader` | Decoded file size minus `0x10`. |

Entry records start at `0x10`, have a `0x10`-byte stride, and end at the first negative `locationCode`.

| Entry offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `locationCode` | Signed location or type ID; negative terminates the table. |
| `0x02` | 2 | `opcode` | Signed type group. |
| `0x04` | 4 | `field2` | Preserved type-specific word. |
| `0x08` | 4 | `payloadSize` | Payload size in bytes. |
| `0x0C` | 4 | `payloadOffset` | Offset relative to decoded file `+0x10`. |

The combined payload type is:

```text
(opcode << 16) | locationCode
```

Payloads are bounded by the entry’s size and offset. Unknown payload bodies and unused header fields must be preserved.
