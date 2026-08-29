# MLD File Progress

This document tracks MLD research status, implementation references, fixture/export notes, and open questions that are not strictly file layout.

## References And Context

This document captures the currently known MLD container layout as implemented in SPICE. It is intentionally conservative: fields below are either parsed/exported by current code or represented by current tests. Unknown padding and game-specific payload semantics should stay marked as unknown until confirmed from source data or reference code.

Primary implementation references:

- `SpiceMLD/Parsing/MldParser.cpp`
- `SpiceMLD/Model/IndexEntry.h`
- `SpiceMLD/Model/U32List.h`
- `SpiceMLD/Parsing/GrndParser.cpp`
- `SpiceMLD/Parsing/GobjParser.cpp`
- `SpiceMLD/Parsing/MldTextureArchiveParser.cpp`
- `SpiceMLD/Patching/DreamcastTrianglePatcher.cpp`
- `SpiceMLD/Export/MldFileExporter.cpp`
- `SpiceTests/test_mld_endian.cpp`

## Export Behavior

`MldFileExporter` starts from `MldFile::originalBytes`, then rewrites known fields in the target endian:

- Top-level 0x14-byte header.
- Every 0x68-byte index entry field listed above.
- Counted U32 lists.
- Selected internals of GRND and GOBJ raw data blocks when converting between Dreamcast and GameCube endian.

Unknown bytes remain preserved from the original file. This means the exporter is currently best described as a preserving endian/AKLZ conversion writer, not a full MLD reassembler from semantic models.

GRND triangle-set `+0x00/+0x04/+0x08` floats are decoded as local-to-resource translation. Stored per-set vertices remain local while the canonical mesh bakes the translation once; normals remain untranslated. Inner-header grid-origin X/Z floats are modeled separately. Semantic GRND rebuilds write the baked canonical geometry in one zero-translation set.

`MldFileWriter` is the canonical semantic writer. Its texture archive path is platform-neutral: GameCube entries retain GVR data and Dreamcast entries retain PVR data. Dreamcast record sizes and 32-byte alignment are regenerated when textures are added, removed, or replaced, and the archive is relocated when it no longer fits its source range. The compatibility exporter routes Dreamcast PVR replacements through this canonical writer.

Dreamcast GRND/GOBJ selector editing uses a separate patching path. The parsers retain absolute source offsets for every canonical triangle's three metadata words. `planDreamcastTriangleSelectorPatches` changes only the decimal tens digit of the third word, and `applyMldPatchPlan` validates the complete patch set before modifying an uncompressed Dreamcast MLD buffer. This mechanism does not impose area, encounter, TBLID, collision-class, or resource-role policy.

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
- Dreamcast texture record control word `+0x20` is still semantically unknown; it is zero in the validated corpus.
- The broader meaning of Dreamcast alignment control `0x80000000`, beyond its observed 32-byte `GBIX` alignment effect, remains unpromoted.
- NJ/Ninja model and motion payloads are preserved/extracted but not fully decoded by SPICE MLD proper.
- GRND and GOBJ support remains partial outside the promoted fields and should be cross-checked against fresh Ghidra/reference evidence before treating remaining fields as final.
- Triangle selector patching is currently limited to uncompressed little-endian Dreamcast MLD files. GameCube/AKLZ patching remains separate work.
