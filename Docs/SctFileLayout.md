# SCT File Layout

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

## Compression and Endian

SCT input may be wrapped in AKLZ compression. `SctParser::parse` detects AKLZ, decompresses it, records `originalCompressedAklz`, and parses the decompressed payload. `SctBinaryExporter` writes canonical uncompressed SCT bytes by default and can optionally AKLZ-compress the exported bytes.

SCT numeric fields are parsed as either big-endian or little-endian. Current endian detection is based on the section count at file offset `0x08`: it reads the count both ways and chooses big-endian when the big-endian value is less than or equal to the little-endian value. This is a practical heuristic, not a magic-based header check.

## Top-Level Container

The SCT container begins with a 12-byte header, followed by a fixed-size section index table, followed by concatenated section payload bytes.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 8 | header prefix | Preserved by parser/exporter as `headerBytes[0..8)`. Current SPICE code does not name these bytes. |
| `0x08` | 4 | `sectionCount` | Number of section index rows. Determines the index table size. |

The section index table starts at `0x0C` and has `sectionCount` rows of 0x14 bytes each. Payload data starts at:

```text
dataStart = 0x0C + sectionCount * 0x14
```

All section start offsets stored in the index are relative to `dataStart`, not absolute file offsets.

## Section Index Row

Each section index row is 0x14 bytes.

| Offset in row | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `payloadStart` | Start offset relative to `dataStart`. |
| `0x04` | 0x10 | `name` | Fixed-width ASCII name, zero-terminated if shorter than 16 bytes. |

There is no explicit section size in the row. Current parser computes each row's end from the next row's `payloadStart`; the final row initially ends at the end of the payload, then may be shortened if a footer is inferred.

If a row name is empty, SPICE synthesizes `section_<index>` in the parsed model.

## Section Payloads

The bytes after the index table are a single payload arena. A section's raw byte range is:

```text
start = dataStart + row[i].payloadStart
end   = dataStart + row[i + 1].payloadStart
```

for non-final rows. Final-row end is initially `fileSize`, but footer handling may move it earlier.

Current SPICE section kinds:

| Kind | Current detection |
| --- | --- |
| `Script` | Row is treated as code and walked from its payload start. |
| `String` | Row is treated as text/string payload. |
| `Label` | Label-only row used as a string group label. |
| `Unknown` | Invalid bounds or not enough evidence. |

Section classification is partly heuristic. The parser uses row names, label preambles, opcode boundary probes, and neighboring string sections. Label-only rows before string rows can become `Label` sections and open a string group.

## Label and String Preamble

Several string and label-like sections begin with an opcode-9 preamble. Current parser detects a preamble by reading 32-bit words from the start of a section until it sees SCPT stop code `0x0000001D`.

Known preamble shape:

| Word | Field | Current meaning |
| ---: | --- | --- |
| `0` | opcode `9` | `LabelOrStringPrefix`. |
| `1..N` | SCPT expression words | Label/string metadata expression. |
| final | `0x0000001D` | SCPT stop code. Text starts after this word for string sections. |

The parser can detect this preamble in the base file endian or the opposite endian. Text bytes after the preamble are decoded as printable ASCII, newline, carriage return, or tab. Zero bytes are skipped during display decoding.

## Script Instructions

Script instructions are word-based. Current parser treats one instruction as:

```text
optional metadata prefixes
opcode word
operand words
```

Each word is 32 bits in the chosen instruction endian. The instruction boundary probe accepts opcodes up to `265`. If the base-endian word is implausible but the opposite-endian word is plausible, the instruction is marked as swapped. A swapped word equal to opcode `4` is rejected as suspicious because it aliases an SCPT float-literal preamble.

`SctInstruction` records both local `offset` within its section and global `payloadOffset` within the payload arena.

## Instruction Prefixes

Two instruction-level prefixes are currently recognized.

### Skip Refresh Prefix

| Word | Value | Current meaning |
| ---: | --- | --- |
| `0` | `13` | Skip-refresh prefix. Parsed as `SctInstruction::skipRefresh = true`. |

When present, opcode decoding continues at the next word.

### Scheduled Prefix

| Word | Value | Current meaning |
| ---: | --- | --- |
| `0` | `129` | Scheduled-instruction prefix. |
| `1..N` | SCPT expression | Frame-delay expression. Parsed with the SCPT analyzer until stop code. |
| next | byte length | Byte length of the actual instruction payload after the scheduled prefix. |
| next | opcode | Real instruction opcode. |

The exporter can canonicalize prefixes so `13` precedes `129`, followed by the scheduled delay expression, length word, opcode, and operands.

## Opcode and Operand Metadata

Current opcode parameter sizes come from `kSalsaOpcodeParamPatterns`, an array with entries for opcodes `0..265`. Each opcode pattern describes:

- Nominal parameter count.
- Which parameters are SCPT-analyzed expressions.
- Loop parameter ranges.
- Iteration-count parameter index.
- Jump/switch target parameter indexes.
- Special loop-break values for some patterns.

The parser expands looped parameter groups when an iteration count requires additional parameter slots, except for known external-loop-break cases such as opcodes `118` and `119` with iteration count `0x00010000`.

Known high-confidence control/resource opcodes in current metadata:

| Opcode | Mnemonic | Role |
| ---: | --- | --- |
| `0` | `If` | Branch. |
| `3` | `Switch` | Switch/case branch. |
| `9` | `LabelOrStringPrefix` | Label/string prefix payload. |
| `10` | `Jump` | Unconditional jump. |
| `11` | `CallSubscript` | Subscript call. |
| `12` | `Return` | Return/terminator. |
| `23` | `LoadMld` | Resource reference to MLD. |
| `43` | `LoadScript` | Resource reference to SCT/script. |
| `210` | `LoadScriptGameState12` | Resource reference to SCT/script. |
| `238` | `ReturnToOverworld` | Resource reference to SCT/script. |
| `257` | `LoadScriptGameState7` | Resource reference to SCT/script. |

Unknown opcodes are still decoded as raw words when possible and receive fallback mnemonic `op_<number>`.

## SCPT Parameter Expressions

Some instruction operands are not simple one-word integers. For parameters flagged in `kSalsaOpcodeParamPatterns.scptAnalyzeMask`, the parser consumes a variable-length SCPT expression stream.

Known SCPT expression rules in current code:

| Word/prefix | Current meaning |
| --- | --- |
| `0x0000001D` | Stop/return-values code. Ends SCPT expression. |
| `0x7F7FFFFF`, `0x00800000`, `0x7FFFFFFF` | No-loop/sentinel values accepted as one-word expressions. |
| `0x00000000..0x00000011` selected values | Compare/assignment operators. |
| `0x0000000B..0x00000016` selected values | Arithmetic operators. |
| `0x50000000` prefix | Int variable reference. |
| `0x40000000` prefix | Float variable reference. |
| `0x20000000` prefix | Bit variable reference. |
| `0x10000000` prefix | Byte variable reference. |
| `0x08000000` prefix | Fixed decimal literal. |
| `0x04000000` prefix | Float literal; consumes the next word as float bits. |

The SCPT analyzer is stack-driven and stops at a stack overflow threshold or stop code. It stores a display value, raw-word trace, and optional AST for each decoded expression.

## Control Flow Offsets

Current branch math is payload-relative.

For opcodes `0` and `10`, the branch target is computed as:

```text
target = instructionOffset + instructionSizeBytes + signedOperand - 4
```

For switch opcode `3`, each switch-case jump parameter is interpreted relative to the payload offset of that jump operand word:

```text
target = jumpWordOffset + signedOperand
```

Control-flow targets can cross section index rows. The parser stores global payload targets and also maps them back to local section offsets when possible. Targets that land inside an already decoded instruction are skipped and reported as diagnostics rather than decoded as overlapping instructions.

## Footer and Footer Strings

SCT files can have trailing footer bytes after the final indexed section. Current parser represents this as `SctFooter`.

The footer starts at one of:

- The first null terminator after the final string section's label/string preamble.
- A boundary inferred from valid footer string references and an aligned final-section terminator.
- The end of payload if no footer bytes are detected.

Footer bytes are preserved as raw bytes. Footer entries are discovered from instruction parameters whose opcode/parameter pair is marked as a footer string reference.

Known footer-reference metadata:

| Opcode | Parameter | Kind | Relative signed? |
| ---: | ---: | --- | --- |
| `23` | `0` | raw string | yes |
| `24` | `0` | SCT string | yes |
| `25` | `1` | SCT string | no |
| `43` | `0` | raw string | no |
| `54` | `1` | raw string | yes |
| `69` | `0` | raw string | yes |
| `110` | `0` | raw string | no |
| `113` | `0` | raw string | no |
| `144` | `0` | SCT string | yes |
| `155` | `1` | SCT string | yes |
| `210` | `0` | raw string | no |
| `214` | `0` | raw string | no |
| `215` | `1` | raw string | no |
| `248` | `0` | raw string | no |
| `250` | `0` | raw string | no |
| `257` | `0` | raw string | no |

Footer entries are null-terminated byte strings. Current decoded display skips zero bytes and substitutes `?` for non-printable bytes. Footer SCT strings are grouped under a synthetic `_Footer_` string group.

## Raw Spans, Unknown Regions, and Unreached Code

Current parser preserves bytes that are not part of reached instructions through raw spans and unknown regions.

- `rawSpans` preserve string payloads, string preambles, label/group-label bytes, and other raw section bytes.
- `unknownRegions` mark bytes inside script sections not covered by reached instructions.
- `unreachedCode` is decoded only when `SctParseOptions::decodeUnreachedCode` is enabled. This keeps speculative unreachable instruction decoding separate from the main reached instruction list.

The default parse path walks global script instructions from script-row entry points and follows control flow. Unreachable garbage in a script section is not exported as canonical instructions.

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
