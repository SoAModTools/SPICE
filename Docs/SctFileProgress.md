# SCT File Progress

This document tracks SCT implementation references, export behavior, fixture coverage, and open gaps that are not strictly file layout.

## References And Context

This document captures the currently known SCT container and script layout as implemented in SPICE. It is conservative by design: fields below are either parsed/exported by current code or covered by current unit tests. Heuristics are called out as heuristics rather than final file-format truth.

Primary implementation references:

- `SpiceSCT/SctParser.cpp`
- `SpiceSCT/SctModel.h`
- `SpiceSCT/SctOpcodeMetadata.h`
- `SpiceSCT/SctScptDecodeHelpers.cpp`
- `SpiceSCT/SctBinaryExporter.cpp`
- `SpiceTests/test_sct_roundtrip.cpp`
- `SpiceTests/test_sct_real_fixtures.cpp`
- `SpiceSCT/SALSA_default_instructions_reference.md`
- `SpiceSCT/SALSA_bi_defaults_opcode_table.md`

## Canonical Export

`SctBinaryExporter` has two modes:

- `PreserveBytesForTest`: returns `originalBytes` unchanged when available.
- `Canonical`: rebuilds a canonical SCT payload from the parsed IR.

Canonical export:

1. Builds normalized IR through `SctIrBuilder`.
2. Recomputes `sectionCount`, index size, and `dataStart`.
3. Exports script sections from sorted instructions.
4. Exports non-script sections from raw spans.
5. Recomputes section payload offsets.
6. Patches branch, jump, switch, and footer-reference words through an old-to-new payload offset map.
7. Preserves the first eight header bytes when available.
8. Writes `sectionCount` at `0x08`.
9. Rewrites each 0x14-byte index row.
10. Appends section bytes and footer raw bytes.

Canonical export may intentionally differ from original bytes. Current tests assert that unreachable garbage can be dropped while semantic equivalence is preserved.

## Minimal Fixture Shape

`SpiceTests/test_sct_roundtrip.cpp` builds fixtures with:

- 12-byte header.
- Section count at `0x08`.
- 0x14-byte index rows containing payload-relative starts and 16-byte names.
- Concatenated section payloads.
- Word-based script instructions such as `Jump` (`10`), `Return` (`12`), `If` (`0`), `Switch` (`3`), label prefix (`9`), skip-refresh prefix (`13`), and scheduled prefix (`129`).
- Final footer strings after script or final string sections.
- AKLZ-compressed input coverage.

These tests prove current behavior around canonical export, preserve mode, unreached-code opt-in decoding, string group labels, prefix canonicalization, switch targets with multiword SCPT selectors, opcode-119 loop expansion, footer detection, cross-row control flow, overlap rejection, missing-label diagnostics, and compressed input.

## Known Gaps

- Header bytes `0x00..0x07` are preserved but not yet named.
- Endian detection is heuristic and based only on plausible section count.
- Section kind detection is partly heuristic and should be refined as more real SCT files are classified.
- Opcode semantics are still incomplete beyond the metadata table and SALSA-derived parameter patterns.
- Some instruction boundaries can be mixed-endian or swapped; current support is defensive rather than a fully explained format feature.
- Footer detection is reference/terminator-based and may need more ground truth for edge cases.
- String encoding is currently treated as printable ASCII plus control whitespace; any game-specific encoding remains to be documented.
- Canonical export is semantic and preserving for known fields, but it is not a byte-for-byte full reassembler unless preserve mode is used.
