# STD File Layout

`SpiceStd` imports STD bytes into a platform-neutral `StdDocument`. Platform and compression are explicit I/O policy: Dreamcast output is little-endian, GameCube output is big-endian, and either may be raw or AKLZ-compressed. AKLZ is therefore not evidence of platform or byte order.

## Action-Row Layout

The header is `0x10` bytes followed by one or more `0x18`-byte rows. The serialized row count and total span are derived when writing.

| Offset | Size | Document field |
| --- | ---: | --- |
| `0x00` | 2 | `rawCommandLow` |
| `0x02` | 2 | `rawCommandHigh` |
| `0x04` | 4 | `rawLoaderContextWord` |
| `0x08` | 4 | Derived row count |
| `0x0C` | 4 | `rawRowTablePointerWord` |

Each row stores an action ID, row type, callback index, flags, and secondary key. Fields at `+0x06`, `+0x0E`, `+0x10`, and `+0x14` retain neutral raw names until their callback-specific semantics are established. Stable row IDs belong to the document and are not serialized.

## Entry-Table Layout

The `0x10`-byte header contains a record count including the terminator, kind `4`, two preserved raw words, and the decoded span after the header. Ordinary `0x10`-byte records contain signed location and opcode fields, a raw word at `+0x04`, a derived payload size, and a derived payload offset relative to decoded `+0x10`.

The last record is a distinct terminator whose location is negative. Its other three words are preserved as raw data and do not describe a payload. A terminator-only table is valid.

Payloads are owned entities referenced by stable IDs. The payload area independently records payload order and explicit opaque gaps or trailing fragments. Writing rebuilds all physical offsets and sizes. Combined type `0x0003002A` with size `0x24` is decoded as `StdActionViewPayload`; every other payload remains bounded opaque bytes.

## Opaque Preservation

Genuinely unrecognized nonempty input may be imported as top-level opaque content only when byte order is caller-specified. Opaque content requires the matching import receipt for output and may only be emitted with the same byte order. Top-level opaque bytes must remain identical; recognized documents may edit or relocate receipt-backed bounded opaque payloads and fragments.
