# MLD File Layout

MLD files combine an entry index with model, ground, motion, texture-list, and texture-archive data.

## Encoding

GameCube MLD data is big-endian and may be AKLZ-wrapped. Dreamcast MLD data is raw little-endian. Four-character block tags remain byte strings and are not byte-swapped.

## Top-Level Header

The header is `0x14` bytes.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `entryCount` | Number of `0x68`-byte index entries. |
| `0x04` | 4 | `indexTableOffset` | Absolute offset of the index table. |
| `0x08` | 4 | `functionParametersOffset` | Header-level function-parameter pointer. |
| `0x0C` | 4 | `realDataOffset` | Header-level payload-data pointer. |
| `0x10` | 4 | `textureTableOffset` | Absolute offset of the texture archive table. |

## Index Entry

Each index entry is `0x68` bytes.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `entryId` | Entry identifier. |
| `0x04` | 4 | `tblId` | Table or dispatch identifier. |
| `0x08` | 4 | `groundLinksPointer` | Absolute pointer to a counted U32 list. |
| `0x0C` | 4 | `paramList2Pointer` | Absolute pointer to a counted U32 list; meaning unknown. |
| `0x10` | 4 | `functionParametersPointer` | Absolute pointer to function parameters. |
| `0x14` | 4 | `objectAddressesPointer` | Absolute pointer to object block addresses. |
| `0x18` | 4 | `groundAddressesPointer` | Absolute pointer to ground block addresses. |
| `0x1C` | 4 | `motionAddressesPointer` | Absolute pointer to motion block addresses. |
| `0x20` | 4 | `texturesPointer` | Pointer to entry-local texture-list data. |
| `0x24` | `0x14` | `functionName` | Fixed-width or null-terminated ASCII name. |
| `0x38` | `0x0C` | `unknown` | Opaque bytes. |
| `0x44` | `0x0C` | `position` | Three 32-bit floats. |
| `0x50` | `0x0C` | `rotation` | Three 32-bit floats, interpreted as degrees. |
| `0x5C` | `0x0C` | `scale` | Three 32-bit floats. |

## Counted U32 Lists

Address and parameter lists begin with a U32 count followed by that many U32 values. Zero values are valid list slots, particularly in motion lists, and must not be removed when preserving animation-slot numbering.

## Entry Texture Lists

Entry texture lists may be direct `NJTL`/`GJTL` chunks, wrappers whose pointer at `+0x08` leads to such a chunk, or simple counted tables of `0x0C`-byte records. In `NJTL`/`GJTL`, the tag is at `+0x00`, payload size at `+0x04`, texture count at `+0x0C`, and each texture record contains a name pointer.

## Texture Archive Table

The archive begins with a U32 count followed by `0x2C`-byte records.

| Record offset | Size | Field |
| --- | ---: | --- |
| `0x00` | `0x20` | Fixed-width texture name |
| `0x20` | 4 | Control word |
| `0x24` | 4 | Alignment or descriptor control |
| `0x28` | 4 | Encoded texture block size |

GameCube archives associate records with GVR texture chunks. Dreamcast archives associate records in order with optional alignment followed by `GBIX`/`PVRT`; alignment control `0x80000000` places `GBIX` on an absolute 32-byte boundary.

## Payload Blocks

Object, ground, and motion blocks are reached through their owning address lists. Known tags include `GRND`, `GOBJ`, `NJCM`, `GJCM`, `NJTL`, and `GJTL`. `GRND` and `GOBJ` store a declared block size at `+0x04`; unknown blocks must remain bounded and preserved according to their owning list.

## GRND Block

The promoted GRND header is:

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | Tag | ASCII `GRND`. |
| `0x04` | 4 | `declaredSize` | Block size. |
| `0x10` | 4 | `triangleSetsRelative` | Signed relative pointer based at `0x10`. |
| `0x14` | 4 | `quadRegistryRelative` | Signed relative pointer based at `0x10`. |
| `0x18` | 4 | `gridOriginX` | Grid origin X float. |
| `0x1C` | 4 | `gridOriginZ` | Grid origin Z float. |
| `0x20` | 2 | `gridX` | Grid width. |
| `0x22` | 2 | `gridZ` | Grid depth. |
| `0x24` | 2 | `cellSizeX` | Cell size in X. |
| `0x26` | 2 | `cellSizeZ` | Cell size in Z. |
| `0x28` | 2 | `triangleSetCount` | Number of `0x18`-byte triangle-set headers. |
| `0x2A` | 2 | `quadCellCount` | Number of quad-grid cells. |

Each triangle-set header stores a translation vector, a relative vertex pointer based at field `+0x0C`, a relative stream pointer based at field `+0x10`, and a declared triangle count. Stream entries are U16 float indexes plus U16 flags. A negative flag on the third entry reverses winding. A referenced vertex contains position XYZ followed by normal XYZ as six floats.

The quad registry begins with four bytes followed by `quadCellCount` records. Each record contains a U32 reference count and a signed relative pointer, based at that pointer field, to U16 triangle-set/U16 triangle-index pairs.

## GOBJ Block

GOBJ begins with tag and declared size, then a root node at `+0x10`. Nodes are `0x34` bytes.

| Node offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 4 | Relative attach pointer; zero means none |
| `0x08` | `0x0C` | Position floats |
| `0x14` | `0x0C` | Rotation values |
| `0x20` | `0x0C` | Scale floats |
| `0x2C` | 4 | Relative child pointer |
| `0x30` | 4 | Relative sibling pointer |

An attach record points from `attach + 0x10` to a vertex chunk. The polygon stream occupies the preceding range after its attach metadata and uses U16 float-index/U16 flag entries, with `0xFFFF` separators. Vertex chunk headers identify the vertex record form and count; unsupported chunk data remains opaque.
