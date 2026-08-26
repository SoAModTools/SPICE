# SpicePvm Progress

Implemented in the standalone C++20 `SpicePvm` static library:

- Owning PVR/PVM models with raw identifiers, exact source bytes, source ranges, parse status, and diagnostics.
- Strict parsing for `PVMH`, optional `GBIX`, `PVRT`, formal archives, and embedded scans.
- RGBA decoding for ARGB1555, RGB565, and ARGB4444.
- Rectangle, square twiddled, mipmapped twiddled, VQ, mipmapped VQ, Small VQ, mipmapped Small VQ, and DMA-prefixed mipmapped twiddled layouts.
- Largest-to-smallest logical mip results with physical source ranges.
- Synthetic malformed-input, channel, Morton, VQ orientation, Small VQ, mipmap, and archive tests.
- Read-only EU and US Dreamcast corpus validation.
- Deterministic PVR encoding for every promoted pixel format and data layout.
- Explicit or generated complete mip chains, normal VQ, and SDK-sized Small VQ codebooks.
- Formal PVMH archive encoding with promoted metadata flags and preserved padding/metadata.
- Dreamcast MLD PVR parsing and canonical texture archive replacement through SpiceMLD.

Intentionally outside this deliverable:

- PNG or other image-file I/O.
- Command-line tools.
- Direct PNG-to-PVR command-line replacement workflows.
- Decoding for YUV, bump-map, RGB555/PCX, or palettized formats not observed in the selected SoA corpus.
