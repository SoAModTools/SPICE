# PVM/PVR File Progress

## Current Support

SPICE parses standalone and archived Dreamcast textures with optional GBIX metadata and formal PVMH tables. It decodes ARGB1555, RGB565, and ARGB4444 across linear rectangle, square twiddled, rectangle-twiddled `0x0D`, mipmapped, VQ, Small VQ, and DMA-prefixed twiddled layouts. Logical mip results are presented largest to smallest while retaining their physical source ranges.

The same promoted formats can be encoded from RGBA images, including complete mip chains and PVM archives. PVR data embedded in Dreamcast MLD texture archives is handled through this project, so MLD remains responsible only for archive placement and record association.

## Known Limitations

Unsupported pixel families such as YUV, bump-map, RGB555/PCX, and palettized data remain parseable as raw identifiers but are not decoded. Small VQ support is limited to the established 16, 32, and 64 pixel dimensions. Unknown PVMH flags and metadata chunks are preserved for existing files but are not interpreted or generated as new structured records.

VQ encoding is deterministic but can be lossy when the source has more unique 2x2 vectors than the codebook allows. Archive metadata inconsistencies are reported while preserving the partial model.
