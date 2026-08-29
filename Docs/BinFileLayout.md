# BIN File Layout

The `.bin` extension is used by more than one format. `SpiceBin` currently recognizes an indexed UI-layout family; files that do not match this structure must remain unclassified until their own layout is known.

## Encoding

Known indexed UI-layout files use big-endian numeric fields. A file may be stored raw or wrapped in AKLZ; all offsets below refer to the decoded payload.

## Indexed UI-Layout Header

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `recordCount` | Number of top-level records. |
| `0x04` | `recordCount * 4` | `recordOffsets` | Offsets relative to the start of the record-data area. |
| `0x04 + recordCount * 4` | variable | `recordData` | Top-level records and their associated tables. |

For record `i`:

```text
dataBase = 0x04 + recordCount * 4
recordAddress = dataBase + recordOffsets[i]
```

## Top-Level Record

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `fixedDataTableOffset` | File-form pointer to the fixed-data table. |
| `0x04` | 4 | `elementTableOffset` | File-form pointer to the element table. |
| `0x08` | 4 | `elementCount` | Number of `0x34`-byte element records. |
| `0x0C` | 4 | `layoutWidth` | Floating-point width or X extent; exact name is provisional. |
| `0x10` | 4 | `layoutHeight` | Floating-point height or Y extent; exact name is provisional. |
| `0x14` | 4 | `baseX` | Base X position. |
| `0x18` | 4 | `baseY` | Base Y position. |

The first two fields are serialized offsets that are rebased when the file is loaded. They are not portable runtime pointers.

## Element Record

Each element is `0x34` bytes.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `fixedDataIndex` | Selects a `0x14`-byte fixed-data record. |
| `0x04` | 4 | `dstLeft` | Destination rectangle left coordinate. |
| `0x08` | 4 | `dstTop` | Destination rectangle top coordinate. |
| `0x0C` | 4 | `dstRight` | Destination rectangle right coordinate. |
| `0x10` | 4 | `dstBottom` | Destination rectangle bottom coordinate. |
| `0x14` | 4 | `packedTint` | Packed color and alpha channels. |
| `0x18` | `0x1C` | `unknown` | Opaque element data that must be preserved. |

## Fixed-Data Record

Each fixed-data record is `0x14` bytes.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `contextSelector` | Selects an entry in an external texture or material context. |
| `0x04` | 4 | `sourceLeft` | Source or UV rectangle left coordinate. |
| `0x08` | 4 | `sourceTop` | Source or UV rectangle top coordinate. |
| `0x0C` | 4 | `sourceRight` | Source or UV rectangle right coordinate. |
| `0x10` | 4 | `sourceBottom` | Source or UV rectangle bottom coordinate. |

The indexed layout does not contain its texture images. The selector is meaningful only with the material context supplied by a companion asset or enclosing container, so it must not be treated as a direct ordinal into extracted texture files.

## Other BIN Families

`field/wmaparea.BIN` is a known fixed-size world-map area table and does not use the indexed UI-layout structure. Its per-entry schema is not yet established.
