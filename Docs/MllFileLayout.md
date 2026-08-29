# MLL File Layout

MLL is a named-member container. Inner payload structures are owned by their respective formats.

## Encoding

Known MLL files are big-endian after optional AKLZ decompression. Offsets refer to the decoded container.

## Container Header

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `headerWord0` | Preserved word, normally `0x0000FFFF`. |
| `0x04` | 4 | `countWord` | High U16 is the member count; low U16 is normally `0xFFFF`. |
| `0x08` | `memberCount * 0x20` | `memberRecords` | Named member table. |

## Member Record

Each member record is `0x20` bytes.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | `0x14` | `name` | Fixed-width, usually null-terminated ASCII member name. |
| `0x14` | 4 | `payloadOffset` | Absolute decoded-file offset. |
| `0x18` | 4 | `payloadSize` | Payload size in bytes. |
| `0x1C` | 4 | `rawWord1C` | Preserved word, normally `0xFFFFFFFF`. |

The normal payload area begins immediately after the member table. Known containers tightly pack members in record order, although each record remains authoritative for its own span.

## Member Payloads

Members can contain MLD files, indexed BIN layouts, Ninja chunks, `POF0`, AKLZ-wrapped data, empty data, or unknown bytes. The outer MLL record supplies only the name and payload bounds. Interpret MLD members according to [MldFileLayout.md](MldFileLayout.md) and BIN members according to [BinFileLayout.md](BinFileLayout.md); inner offsets are relative to the member payload, not the MLL container.
