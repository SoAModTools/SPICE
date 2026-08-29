# SCT File Layout

SCT files contain named script and string sections followed by optional footer data.

## Encoding

SCT may be raw or AKLZ-wrapped. Decoded numeric fields can be big-endian or little-endian. All offsets below refer to the decoded file.

## Container Header

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 8 | `headerPrefix` | Preserved bytes whose fields are not yet named. |
| `0x08` | 4 | `sectionCount` | Number of section index rows. |
| `0x0C` | `sectionCount * 0x14` | `sectionIndex` | Section start offsets and names. |

The shared payload arena begins at:

```text
dataStart = 0x0C + sectionCount * 0x14
```

## Section Index Row

Each row is `0x14` bytes.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `payloadStart` | Offset relative to `dataStart`. |
| `0x04` | `0x10` | `name` | Fixed-width, normally null-terminated ASCII name. |

Rows do not store a size. A section normally ends at the next row’s start; the final section ends at the footer boundary or end of file.

## Label and String Preamble

Label and string sections can begin with an opcode-9 word, followed by an SCPT expression and the stop word `0x0000001D`. String bytes begin after the stop word. Text and other noninstruction bytes remain part of the section’s raw span.

## Script Instructions

Instructions are sequences of 32-bit words:

```text
optional prefixes
opcode
operands
```

Operand count and which operands contain variable-length SCPT expressions depend on the opcode. Some patterns contain repeated operand groups whose count is supplied by an earlier operand.

Two serialized prefixes are known:

| Prefix | Structure |
| ---: | --- |
| `13` | Marks the following instruction as skip-refresh. |
| `129` | Followed by a delay expression, an instruction byte length, then the real opcode and operands. |

## SCPT Expressions

SCPT operands are stack expressions encoded as 32-bit words and terminated by `0x0000001D`. Known word families include integer, float, bit, and byte variable references; fixed-decimal and floating-point literals; arithmetic and comparison operators; and several one-word sentinel values. A float-literal prefix consumes the following word as its float bits.

## Control-Flow Offsets

Branch targets are relative to positions in the shared payload arena and may cross section rows.

For `If` and `Jump` instructions:

```text
target = instructionOffset + instructionSize + signedOperand - 4
```

For switch entries:

```text
target = jumpOperandOffset + signedOperand
```

## Footer

Bytes after the final indexed section form an optional footer. Some instruction operands address null-terminated footer strings through absolute or signed-relative offsets. The exact reference rule is opcode-specific. Footer bytes, referenced strings, and unclassified padding must remain preserved even when their semantic role is unknown.
