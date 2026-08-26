# Dreamcast PVM/PVR File Layout

`SpicePvm` parses Dreamcast PVM archives and standalone or embedded PVR textures. All chunk sizes and scalar fields described here are little-endian.

## PVR texture chunks

A texture is either a `PVRT` chunk or an optional `GBIX` chunk immediately followed by `PVRT`.

`GBIX`:

| Offset | Size | Meaning |
| --- | ---: | --- |
| `0x00` | 4 | ASCII `GBIX` |
| `0x04` | 4 | Payload size |
| `0x08` | 4 | Global texture index when the payload is at least four bytes |
| remainder | variable | Retained GBIX payload data |

`PVRT`:

| Offset | Size | Meaning |
| --- | ---: | --- |
| `0x00` | 4 | ASCII `PVRT` |
| `0x04` | 4 | Payload size, including the eight-byte texture header |
| `0x08` | 1 | Raw pixel-format identifier |
| `0x09` | 1 | Raw data-layout identifier |
| `0x0A` | 2 | Unknown/reserved texture-header bytes, retained verbatim |
| `0x0C` | 2 | Width |
| `0x0E` | 2 | Height |
| `0x10` | variable | Codebook and/or texture data |

SoA VQ-mipmap payloads are zero-filled to a 32-byte boundary inside the declared `PVRT` size. The decoder accepts either an exact logical payload or the exact aligned size, and only when every alignment byte is zero. The padding has its own source range in `DecodeResult`.

## Pixel formats

| Raw value | Format | 16-bit word layout |
| ---: | --- | --- |
| `0x00` | ARGB1555 | `A RRRRR GGGGG BBBBB` |
| `0x01` | RGB565 | `RRRRR GGGGGG BBBBB`, alpha is 255 |
| `0x02` | ARGB4444 | `AAAA RRRR GGGG BBBB` |

Five-, six-, and four-bit channels are expanded across the complete 0-255 range. Other raw identifiers remain parseable but are intentionally not decoded.

## Data layouts

| Raw value | Layout |
| ---: | --- |
| `0x01` | Square twiddled |
| `0x02` | Square twiddled with mipmaps |
| `0x03` | Vector quantized (VQ) |
| `0x04` | VQ with mipmaps |
| `0x09` | Linear rectangle |
| `0x10` | Small VQ |
| `0x11` | Small VQ with mipmaps |
| `0x12` | Square twiddled with mipmaps and six-byte DMA prefix |

Rectangle pixels are stored left-to-right and top-to-bottom. Twiddled pixels use Morton order with `twiddle(x) << 1 | twiddle(y)`. Twiddled and VQ inputs must be square power-of-two textures.

VQ data begins with a codebook. Each codebook entry is eight bytes containing four 16-bit colors. The four colors map in x-major order: top-left, bottom-left, top-right, bottom-right. The following one-byte indices address 2x2 vectors; the index plane is Morton ordered. Normal VQ always contains 256 entries.

Small VQ codebook entry counts follow the Dreamcast SDK rules:

| Base dimension | Small VQ | Small VQ mipmaps |
| ---: | ---: | ---: |
| 16 | 16 | 16 |
| 32 | 32 | 64 |
| 64 | 128 | 256 |

The library rejects Small VQ dimensions outside the promoted 16/32/64 corpus contract.

## Mipmaps

Mip data is physically smallest-to-largest. Logical decoder results reverse that order: the base image is index zero and the 1x1 image is last. Every logical mip retains the range of its physical source bytes.

Uncompressed levels use two bytes per pixel. VQ levels use one byte per 2x2 vector, with one byte retained for both the 1x1 and 2x2 levels. Layout `0x02` has a two-byte prefix before the 1x1 level. Layout `0x12` has a six-byte prefix. VQ-mipmap layouts begin their index stream without that prefix.

## PVMH archive

`PVMH` begins with an eight-byte chunk header, then a 16-bit flags word and 16-bit texture count. Entry records always begin with a 16-bit archive index and conditionally contain:

| Flag | Entry field |
| ---: | --- |
| `0x0001` | 32-bit global index |
| `0x0002` | 16-bit dimensions field |
| `0x0004` | Pixel-format and data-layout bytes |
| `0x0008` | 28-byte, null-terminated filename field |

The dimensions low nibble encodes `log2(width) - 2`; the next nibble encodes `log2(height) - 2`. The upper byte is retained and remains unknown. Header padding and metadata between `PVMH` and the first texture are retained verbatim. Flags `0x0010` and `0x0100`, and metadata chunk semantics such as `MDLN`, `COMM`, `CONV`, `IMGC`, and `PVMI`, remain unpromoted; their bytes are preserved rather than interpreted.

Archive entries are paired with following PVR chunks in order. Count and pixel/layout/dimension/global-index identity mismatches are reported without discarding the partial archive model.

## Encoding contract

`spice::pvm::encoding` emits standalone `GBIX`/`PVRT` textures and formal `PVMH` archives from owning RGBA images. Mip images are accepted in logical largest-to-smallest order and emitted in the physical order described above. A single base image may request deterministic box-filter mip generation.

RGBA channels are quantized to the selected 16-bit format with nearest-integer scaling. ARGB1555 uses an alpha threshold of 128. Direct layouts are the exact inverse of rectangle and Morton decoding. VQ layouts use deterministic frequency-weighted median-cut clustering over packed 2x2 vectors; inputs with no more unique vectors than the available codebook encode without VQ clustering loss. Codebook entries and index streams retain the decoder's x-major vector and Morton-index conventions.

New VQ-mipmap textures are zero-padded to a 32-byte payload boundary by default. The API also preserves caller-supplied GBIX trailing bytes and the two unknown PVRT header bytes.

Formal PVM encoding supports the promoted `0x000F` entry flags. Unknown PVMH flags are rejected for new output because their record contribution is not known. Caller-supplied PVMH header padding and metadata between PVMH and the first texture are retained verbatim.
