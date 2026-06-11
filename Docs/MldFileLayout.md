# MLD File Layout

This document captures the currently known MLD container layout as implemented in SPICE. It is intentionally conservative: fields below are either parsed/exported by current code or represented by current tests. Unknown padding and game-specific payload semantics should stay marked as unknown until confirmed from source data or reference code.

Primary implementation references:

- `SpiceMLD/Parsing/MldParser.cpp`
- `SpiceMLD/Model/IndexEntry.h`
- `SpiceMLD/Model/U32List.h`
- `SpiceMLD/Parsing/GrndParser.cpp`
- `SpiceMLD/Parsing/GobjParser.cpp`
- `SpiceMLD/Parsing/MldTextureArchiveParser.cpp`
- `SpiceMLD/Export/MldFileExporter.cpp`
- `SpiceTests/test_mld_endian.cpp`

## Platform and Endian

MLD files are parsed as either big-endian or little-endian. Current code treats:

- Big-endian as GameCube.
- Little-endian as Dreamcast.

Endian detection reads the top-level header both ways and chooses the interpretation whose entry count and index table bounds are plausible. If both are plausible, the parser chooses the one with the smaller entry count.

MLD input may be wrapped in AKLZ compression. `MldParser::parseFile` detects AKLZ, decompresses it, and parses the decompressed bytes. `MldFileExporter` can write AKLZ only for GameCube output.

## Top-Level Header

The top-level MLD header is 0x14 bytes. All numeric fields use the detected file endian.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `entryCount` | Number of 0x68-byte index entries. Must be nonzero and no more than 65536 in current validation. |
| `0x04` | 4 | `indexTableOffset` | Absolute file offset of the index entry table. |
| `0x08` | 4 | `functionParametersOffset` | Header-level pointer into function parameter data. Current code preserves it but primarily follows per-entry list pointers. |
| `0x0C` | 4 | `realDataOffset` | Header-level pointer to real payload data. In current fixtures this points at the first GRND block. |
| `0x10` | 4 | `textureTableOffset` | Absolute file offset of the texture archive/name table. |

The index table is considered valid when `indexTableOffset + entryCount * 0x68` fits inside the file.

## Index Entry Table

Each index entry is 0x68 bytes. The table starts at `header.indexTableOffset`; entry `i` starts at `indexTableOffset + i * 0x68`.

| Offset in entry | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `entryId` | Source entry identifier. |
| `0x04` | 4 | `tblId` | Table or script dispatch identifier. Content graph tests use this to connect MLD entries to SCT sections. |
| `0x08` | 4 | `groundLinksPointer` | Absolute pointer to a counted U32 list of linked ground IDs. |
| `0x0C` | 4 | `paramList2Pointer` | Absolute pointer to a counted U32 list. Semantics not fully named yet. |
| `0x10` | 4 | `functionParametersPointer` | Absolute pointer to a counted U32 list of function parameters. |
| `0x14` | 4 | `objectAddressesPointer` | Absolute pointer to a counted U32 list of object block addresses. |
| `0x18` | 4 | `groundAddressesPointer` | Absolute pointer to a counted U32 list of ground block addresses. |
| `0x1C` | 4 | `motionAddressesPointer` | Absolute pointer to a counted U32 list of motion block addresses. |
| `0x20` | 4 | `texturesPointer` | Pointer to entry texture list data. This may point directly at NJTL/GJTL data, a wrapper pointing to NJTL/GJTL at `+0x08`, or a simpler counted record table. |
| `0x24` | 0x14 | `fxnName` | Null-terminated or fixed-width ASCII function name. Non-printable bytes become `?` in parser output. |
| `0x38` | 0x0C | Unknown/padding | Preserved in `rawBytes`, not currently named by parser/exporter. |
| `0x44` | 4 | `position.x` | 32-bit float. |
| `0x48` | 4 | `position.y` | 32-bit float. |
| `0x4C` | 4 | `position.z` | 32-bit float. |
| `0x50` | 4 | `rotation.x` | 32-bit float interpreted as degrees in index-entry parsing. Converted to radians/quaternion for the model. |
| `0x54` | 4 | `rotation.y` | 32-bit float interpreted as degrees. |
| `0x58` | 4 | `rotation.z` | 32-bit float interpreted as degrees. |
| `0x5C` | 4 | `scale.x` | 32-bit float. |
| `0x60` | 4 | `scale.y` | 32-bit float. |
| `0x64` | 4 | `scale.z` | 32-bit float. |

The exporter rewrites these fields when changing target platform endian. Bytes not described above remain from `MldFile::originalBytes`.

## Counted U32 Lists

Many index entry fields point to the same simple list structure:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | `count` | Number of U32 values. Current parser rejects suspicious counts above 65536. |
| `+0x04` | `count * 4` | `values` | U32 values in file endian. |

The parser must read and preserve exactly `count` values. This matters for motion address lists: the game indexes this list by animation slot, so a zero value means "this model has no animation for this slot" rather than "remove this list entry." A motion list such as `[0, 0x1234, 0, 0x5678]` therefore has four animation slots and two present motion payload addresses. `motionCount` is only the count of nonzero values and must not be used as the motion list length.

Zero-valued object, ground, and motion addresses are ignored only when building unique payload-address sets for extraction/classification. The original counted lists still retain their zero slots.

## Entry Texture Lists

Per-entry `texturesPointer` has multiple observed/currently supported forms.

### Direct or Wrapped NJTL/GJTL

The parser recognizes `NJTL` and `GJTL` tags at `texturesPointer`. If the tag is not at `texturesPointer`, it also checks a wrapper pointer at `texturesPointer + 0x08`.

For an NJTL/GJTL block, current parsing treats:

| Relative offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | tag | `NJTL` or `GJTL`, read as ASCII bytes. |
| `0x04` | 4 | blockSize | Payload size after the 8-byte block header. |
| `0x0C` | 4 | textureCount | Number of texture records. |
| payload `0x08 + i * 0x0C` | 4 | namePointer | Offset inside the NJTL/GJTL payload to a null-terminated texture name. |

### Simple Counted Texture Record Table

If no NJTL/GJTL tag is present, current code also accepts a simpler table when:

- `texturesPointer + 0x04` is a count no larger than 4096.
- The table fits in the file as `8 + count * 12` bytes.

In that form, each 12-byte record begins with a file-absolute name pointer.

## Texture Archive Table

The top-level `header.textureTableOffset` points to an MLD texture archive surface. Current code first reads a local name table:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | `count` | Number of texture-name records. Must be nonzero and no more than 4096. |
| `+0x04 + i * 0x2C` | 0x20 | name | Fixed-width ASCII texture name. |
| `+0x24 + i * 0x2C` | 0x0C | unknown record tail | Present in the 44-byte stride, not yet named in current model. |

After that, the parser delegates to the GVM parser starting at `textureTableOffset` and maps discovered GVR chunks into `MldTextureArchive` entries. Each mapped texture records archive offset, GVR source size, global index, pixel/data format, dimensions, image data range, optional decoded RGBA8, and diagnostics.

## Raw Data Blocks and Address Ownership

Payload blocks are discovered by following the address lists from index entries:

- `objectAddressesPointer` values are object candidates.
- `groundAddressesPointer` values are ground candidates.
- `motionAddressesPointer` values are motion candidates. The full counted list is retained as the animation-slot table; zero slots are skipped only when discovering concrete motion payload blocks.
- `texturesPointer` values are texture-list candidates.

Current raw block classification is tag-based:

| Tag | Kind | Notes |
| --- | --- | --- |
| `GRND` | Ground | Has a declared size at block offset `+0x04`. |
| `GOBJ` | Object | Has a declared size at block offset `+0x04`. |
| `NJCM`, `GJCM`, `NJTL`, `GJTL` | Ninja | Preserved/extracted, but legacy Ninja parsing has been removed from MLD parsing. |
| Other | Unknown | Preserved as unknown object/ground depending on owner list. |

For `GRND` and `GOBJ`, block size is normally read from the block header at `+0x04` in the detected MLD endian. The spatial extraction path can also probe little-endian and big-endian sizes and, if no plausible declared size is found, fall back to the distance to the next candidate address.

## GRND Blocks

GRND blocks are decoded as collision/walk surface geometry. Current decoder requirements:

- Block starts with ASCII `GRND`.
- Declared size is at `+0x04`.
- Minimum useful decoded block size is 0x2C.
- Current parser treats `0x10` as the start of an inner header.

Known GRND inner layout:

| Relative offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | tag | `GRND`. |
| `0x04` | 4 | declaredSize | Size of the GRND block. |
| `0x10` | 4 | `relTriangleSets` | Signed relative pointer. Base is `0x10`. |
| `0x14` | 4 | `relQuadRegistry` | Signed relative pointer. Base is `0x10`. |
| `0x20` | 2 | `gridX` | Grid width/count value. |
| `0x22` | 2 | `gridZ` | Grid depth/count value. |
| `0x24` | 2 | `cellSizeX` | Cell size in X. |
| `0x26` | 2 | `cellSizeZ` | Cell size in Z. |
| `0x28` | 2 | `triangleSetCount` | Number of triangle-set headers. |
| `0x2A` | 2 | `quadCellCount` | Number of quad-grid cells. |

Triangle-set headers are 0x18 bytes each. Current decoder names these fields:

| Offset in triangle set | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x0C` | 4 | `vertexRel` | Signed relative pointer. Base is `setOffset + 0x0C`. |
| `0x10` | 4 | `streamRel` | Signed relative pointer. Base is `setOffset + 0x10`. |
| `0x14` | 4 | `declaredTriangleCount` | Declared triangle count. Current stream length is mostly inferred from stream-to-vertex span. |

Triangle stream entries are 4 bytes:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `+0x00` | 2 | `floatIndex` | Index into the float stream. Vertex byte offset is `vertexBlockOffset + floatIndex * 4`. |
| `+0x02` | 2 | `flags` | Signed value. A negative flag on the third stream entry reverses triangle winding. |

Each vertex referenced by a triangle stream entry is read as six floats at `vertexBlockOffset + floatIndex * 4`: position XYZ followed by normal XYZ, for 24 bytes total.

The quad registry starts at `quadRegistryOffset`. Current code skips 4 bytes, then reads `quadCellCount` records of 8 bytes:

| Offset in quad cell | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `refCount` | Number of triangle references for this grid cell. |
| `0x04` | 4 | `relRefList` | Signed relative pointer. Base is this field address. |

Each triangle reference is 4 bytes: U16 triangle-set index plus U16 triangle stream index. The decoder deduplicates `(triangleSet, triangleIndex)` references across grid cells before emitting mesh triangles.

## GOBJ Blocks

GOBJ blocks are decoded as object node trees with stream mesh payloads.

Current decoder requirements:

- Block starts with ASCII `GOBJ`.
- Declared size is at `+0x04`.
- Root node is at relative offset `0x10`.
- Node size is 0x34 bytes.

Known GOBJ node layout:

| Offset in node | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `attachRel` | Signed relative pointer from node offset to an attach record. Zero means no attach. |
| `0x08` | 12 | position | Three 32-bit floats. |
| `0x14` | 12 | rotation | Three BAMS32 values converted to radians/quaternion. |
| `0x20` | 12 | scale | Three 32-bit floats. |
| `0x2C` | 4 | `childRel` | Signed relative pointer. Base is `nodeOffset + 0x2C`. |
| `0x30` | 4 | `siblingRel` | Signed relative pointer. Current code also uses `nodeOffset + 0x2C` as the base. |

Known GOBJ attach/mesh layout:

| Relative offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `attach + 0x10` | 4 | `vertexRel` | Signed relative pointer from `attach + 0x10` to the vertex chunk. |
| `attach + 0x10 + 76` | variable | poly stream | 4-byte records read until the vertex chunk. |
| `vertexOffset + 0x00` | 4 | vertex header 1 | Current parser requires low byte `0x29`. |
| `vertexOffset + 0x04` | 4 | vertex header 2 | High 16 bits are interpreted as vertex count. |

Poly stream entries are U16 `floatIndex` plus U16 `flags`. `0xFFFF` in either half is a separator. Unsupported/control records break the current run. For valid stream entries, a float index is accepted when it is at least 2 and aligned as `(floatIndex - 2) % 6 == 0`. Position is read at `vertexOffset + floatIndex * 4`; normal is read from the same vertex bucket at `vertexOffset + ((bucket * 6) + 5) * 4`.

## NJ/Ninja Blocks

Current MLD parsing no longer has the older legacy NJCM/NJTL model parser. It still preserves and extracts NJ-like blocks for migration/parity work:

- `NJTL`/`GJTL` texture-list blocks.
- `NJCM`/`GJCM` object/model blocks.
- Motion blocks referenced by motion address lists.

The extraction code can combine an `NJTL`/`GJTL` immediately followed by an object `NJCM`/`GJCM` into one extracted object block and marks that as including an NJTL prefix.

## Export Behavior

`MldFileExporter` starts from `MldFile::originalBytes`, then rewrites known fields in the target endian:

- Top-level 0x14-byte header.
- Every 0x68-byte index entry field listed above.
- Counted U32 lists.
- Selected internals of GRND and GOBJ raw data blocks when converting between Dreamcast and GameCube endian.

Unknown bytes remain preserved from the original file. This means the exporter is currently best described as a preserving endian/AKLZ conversion writer, not a full MLD reassembler from semantic models.

## Minimal Fixture Shape

`SpiceTests/test_mld_endian.cpp` builds a minimal valid MLD with:

- Header at `0x00`.
- One index entry.
- Counted U32 lists for ground links, params, objects, grounds, and motions.
- A `wall` function name.
- Position, rotation, and scale floats.
- A minimal `GRND` block with declared size `0x2C`.
- A zero-count texture table.

That fixture is used to prove that big-endian and little-endian inputs parse to equivalent semantic IR, that GameCube to Dreamcast export preserves semantic shape and FourCC bytes, that ambiguous endian detection chooses the smaller plausible entry count, and that GameCube export can be AKLZ-compressed while Dreamcast AKLZ export is rejected.

## Known Gaps

- The semantic meaning of `paramList2Pointer` is still unknown.
- The 0x0C-byte unknown/padding region in each index entry from `0x38` through `0x43` is preserved but not named.
- `realDataOffset` is preserved and validated only indirectly; current parsing follows per-entry address lists for actual payload discovery.
- GOBJ sibling pointer base should be verified against reference data; current code uses `nodeOffset + 0x2C` as the base for both child and sibling relative pointers.
- Texture archive record tail bytes in the 0x2C-byte name table stride are not yet named.
- NJ/Ninja model and motion payloads are preserved/extracted but not fully decoded by SPICE MLD proper.
- GRND and GOBJ support is intentionally partial and should be cross-checked against fresh Ghidra/reference evidence before treating all fields as final.
