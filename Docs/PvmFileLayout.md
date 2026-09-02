# Dreamcast PVM/PVR File Layout

PVR stores Dreamcast texture data, while PVM groups PVR textures in an archive. All scalar fields are little-endian.

## GBIX and PVRT Chunks

A texture is a `PVRT` chunk optionally preceded by `GBIX`.

| GBIX offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 4 | ASCII `GBIX` |
| `0x04` | 4 | Payload size |
| `0x08` | 4 | Global texture index when present |
| `0x0C` | variable | Additional GBIX payload bytes |

| PVRT offset | Size | Field |
| --- | ---: | --- |
| `0x00` | 4 | ASCII `PVRT` |
| `0x04` | 4 | Payload size, including the eight-byte texture header |
| `0x08` | 1 | Pixel-format identifier |
| `0x09` | 1 | Data-layout identifier |
| `0x0A` | 2 | Reserved header bytes |
| `0x0C` | 2 | Width |
| `0x0E` | 2 | Height |
| `0x10` | variable | Codebook and/or pixel data |

Some VQ-mipmap payloads include zero padding inside the declared PVRT size to reach a 32-byte boundary.

## Pixel Formats

| Value | Format | Word layout |
| ---: | --- | --- |
| `0x00` | ARGB1555 | `A RRRRR GGGGG BBBBB` |
| `0x01` | RGB565 | `RRRRR GGGGGG BBBBB` |
| `0x02` | ARGB4444 | `AAAA RRRR GGGG BBBB` |

## Data Layouts

| Value | Layout |
| ---: | --- |
| `0x01` | Square twiddled |
| `0x02` | Square twiddled with mipmaps |
| `0x03` | Vector quantized |
| `0x04` | Vector quantized with mipmaps |
| `0x09` | Linear rectangle |
| `0x0D` | Rectangle twiddled |
| `0x10` | Small VQ |
| `0x11` | Small VQ with mipmaps |
| `0x12` | Twiddled mipmaps with a six-byte DMA prefix |

Linear rectangle data is row-major. Rectangle-twiddled data divides the image into square Morton tiles whose side is `min(width, height)`; tiles advance row-major along the longer axis. Both dimensions must be nonzero powers of two, and the layout contains one non-mipmapped direct-color image. Square twiddled data uses Morton order across the entire image. VQ data begins with eight-byte codebook entries containing four 16-bit colors in top-left, bottom-left, top-right, bottom-right order, followed by Morton-ordered byte indexes for 2x2 vectors. Standard VQ uses 256 codebook entries; Small VQ uses dimension-dependent codebook sizes.

Mip levels are stored smallest to largest. Uncompressed levels use two bytes per pixel. VQ levels use one byte per 2x2 vector, with one byte retained for both the 1x1 and 2x2 levels. Layout `0x02` has a two-byte prefix before its mip data and layout `0x12` has a six-byte prefix.

## PVMH Archive

`PVMH` begins with an eight-byte chunk header followed by a U16 flags word and U16 texture count. Every entry begins with a U16 archive index and conditionally includes fields selected by flags:

| Flag | Entry field |
| ---: | --- |
| `0x0001` | U32 global index |
| `0x0002` | U16 packed dimensions |
| `0x0004` | Pixel-format and data-layout bytes |
| `0x0008` | `0x1C`-byte null-terminated filename |

The dimensions nibbles encode `log2(width) - 2` and `log2(height) - 2`; the upper byte is unresolved. Header padding and metadata between PVMH and the first texture are part of the archive and must be preserved. Archive records pair with following PVR chunks in order.
