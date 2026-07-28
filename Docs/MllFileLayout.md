# MLL File Layout

This document describes the promoted MLL container and member-payload layouts implemented by SPICE. Research status, archive/export tooling notes, regional validation, and open questions live in `Docs/MllFileProgress.md`.

## Platform and Endian

MLL files are GameCube data and current SPICE parsing treats all numeric fields described here as big-endian after optional AKLZ decompression.

MLL input may be AKLZ-wrapped. `MllParser::parseFile` detects AKLZ, decompresses it, and parses the decoded bytes. `MllFile::rawSize` records the input byte size, `decodedSize` records the post-decompression byte size, and `sourceWasCompressedAklz` records whether AKLZ wrapping was present.

`MllFile::originalDecodedBytes` preserves the full decoded byte stream. This is the byte surface used by the safe exporter and archive IR.

## Top-Level Container

The decoded MLL container starts with an 8-byte prefix followed by a fixed-stride member record table.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `headerWord0` | Observed as `0x0000ffff` in supported US/EU/JP files. Preserved by parser/exporter. |
| `0x04` | 4 | `countWord` | High `u16` is the member count in supported files. Low `u16` is observed as `0xffff`. |
| `0x08` | `memberCount * 0x20` | member records | Fixed 0x20-byte records. |
| `memberTableEnd` | variable | payload bytes | Member payloads are addressed by absolute decoded-file offsets stored in records. |

Current supported table shape:

- `recordsOffset` is `0x08`.
- `recordStride` is `0x20`.
- `memberTableEndOffset` is `0x08 + memberCount * 0x20`.
- The first payload offset equals `memberTableEndOffset`.
- Member payload spans are in bounds and do not overlap the member table.
- All supported corpus files use the header count at `+0x04`.

The parser can also infer a provisional count from the first member offset when the header-count hypothesis does not fit. That fallback is for research diagnostics only; fallback parses are not considered the fully supported schema.

## Member Records

Each member record starts at `0x08 + index * 0x20`.

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 0x14 | `name` | Fixed-width member name bytes. Names are generally null-terminated ASCII. |
| `0x14` | 4 | `payloadOffset` | Absolute decoded-file offset of this member payload. |
| `0x18` | 4 | `payloadSize` | Payload size in bytes. |
| `0x1C` | 4 | `rawWord1c` | Observed as `0xffffffff` in supported regional corpus files. |

The runtime helper evidence matches this table shape when viewed from the container base:

- member payload pointer is read from `base + index * 0x20 + 0x1c`, matching file record `+0x14` because records begin at file offset `0x08`.
- member size is read from `base + index * 0x20 + 0x20`, matching file record `+0x18`.
- total loaded size is computed as `8 + count * 0x20 + sum(member sizes)`.

## Payload Packing

Current supported files are tightly packed after the member table:

- member 0 payload starts at `memberTableEndOffset`.
- each following payload starts immediately after the previous payload.
- payload offsets are recomputed by the exporter when payload resizing is explicitly allowed.

The default exporter rejects source layouts with gaps between payloads. This is intentional: the safe no-op export path currently guarantees byte-identical decoded rebuilds only for the tight-packing shape validated in the corpus.

## Payload Kinds

MLL is a container format. Member payloads are independently classified by lightweight probes.

Current top-level payload kinds:

| Kind | Current recognition |
| --- | --- |
| `Empty` | Zero-byte payload. |
| `AklzCompressed` | Payload itself begins with AKLZ wrapping. |
| `IndexedBin` | Payload has a strong `SpiceBin` indexed-table probe. This can override weak MLD-header overlap for `.bin` members. |
| `MldFile` | Payload has a plausible embedded MLD header plus stronger MLD evidence, such as plausible index-entry shape, explicit `.mld` member name, or texture-table/embedded-GVR evidence. |
| `NinjaChunk` | Payload starts with a recognized Ninja chunk signature. |
| `Pof0` | Payload starts with `POF0`. |
| `Unknown` | No current classifier matched. |

Classifier output is routing evidence, not a complete semantic type. In particular, named `.bin` members can look MLD-like to static probes while still being consumed by indexed table runtime handlers.

## MLD-Like Member Payloads

Most known MLL members are MLD-like payloads. Runtime callers commonly copy a selected member into a fresh allocation and pass that copied payload to the MLD texture/model load path.

The MLD-like payload header follows the general MLD layout described in `Docs/MldFileLayout.md`, with a top-level texture table offset at payload `+0x10`.

For MLL-contained MLD-like payloads, the texture archive surface has the same 0x2c record layout also validated in standalone MLD files:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | `textureCount` | Number of texture records. |
| `+0x04 + i * 0x2c` | 0x10 | `name` | Null-terminated texture name field. |
| `+0x14 + i * 0x2c` | 4 | `word10` | Observed zero in regular inline MLL texture rows. |
| `+0x18 + i * 0x2c` | 4 | `word14` | Observed zero in regular inline MLL texture rows. |
| `+0x1c + i * 0x2c` | 4 | `word18` | Observed zero in regular inline MLL texture rows. |
| `+0x20 + i * 0x2c` | 4 | `word1c` | Observed zero in regular inline MLL texture rows. |
| `+0x24 + i * 0x2c` | 4 | `word20` | Texture-load descriptor slot; observed zero in file-form rows, overwritten during load. |
| `+0x28 + i * 0x2c` | 4 | `word24` | File-form descriptor flags/type word; observed `0x80000000` in MLL inline rows. |
| `+0x2c + i * 0x2c` | 4 | `word28` | Source byte size of the corresponding appended GCIX/GVRT blob. |

After the table, padding aligns the appended texture byte stream to a 0x20-byte boundary. The appended stream is a sequence of paired GCIX/GVRT texture blobs in table-record order. Current probes parse those texture blobs through SpiceGvm and record source size, global index, image format, dimensions, and decode status.

Standalone MLD validation shows additional texture-table variants, especially `word24=0` character/effect-style rows. Those are general MLD behavior and should not be treated as MLL-specific unless seen in MLL corpus data.

## Indexed `.bin` Member Payloads

Named `.bin` members are inner payloads owned by `SpiceBin`, not by the MLL container format. MLL stores only the member name, payload offset, payload size, and the opaque payload bytes. Runtime evidence shows selected `.bin` payloads are copied into member-local buffers before being passed to indexed table handlers, so outer MLL offsets are not part of the inner `.bin` address space.

Current MLL parsing delegates indexed `.bin` probing to `SpiceBin` when either:

- the member name ends with `.bin`, case-insensitive.
- the payload kind is otherwise unknown.

See `Docs/BinFileLayout.md` for the indexed UI/render layout table schema and the `SpiceBin` parser/probe API.
