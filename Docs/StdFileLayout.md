# STD File Layout

SpiceStd imports STD bytes into a platform-neutral StdDocument. Platform and compression are explicit I/O policy: Dreamcast output is little-endian, GameCube output is big-endian, and either may be raw or AKLZ-compressed. AKLZ is not evidence of platform or byte order.

## Action-Row Layout

The header is 0x10 bytes followed by one or more 0x18-byte source rows. The serialized row count and total span are derived when writing.

| Offset | Size | Document field |
| --- | ---: | --- |
| 0x00 | 2 | rawCommandLow |
| 0x02 | 2 | rawCommandHigh |
| 0x04 | 4 | rawLoaderContextWord |
| 0x08 | 4 | Derived row count |
| 0x0C | 4 | rawRowTablePointerWord |

Every row owns a stable document-local ID plus its action, selector-callback, and secondary key. Row type 0 exposes a reserved halfword at +0x06, raw flags at +0x08, the signed vertical-extent override code at +0x0E, the bit-exact default planar movement step at +0x10, and the bit-exact turning step or threshold at +0x14. A zero vertical-extent code requests geometry-derived height; a nonzero code represents world units equal to the code multiplied by 3.75. Row type 1 exposes the signed MLD motion-resource selector, complete motion flags, action parameter, timing or transition scalar, and motion-frame increment in its own variant. Other serialized row types use an explicitly unrecognized variant and preserve every known-width field.

Row type 3 is not a serialized variant. Both platform loaders synthesize it as an additional runtime terminator, so its appearance in source bytes is rejected as malformed.

Generated runtime 0x10-byte link nodes are not source action rows and are not represented here.

## Entry-Table Layout

The 0x10-byte header contains a record count including the terminator, kind 4, two preserved raw words, and the declared decoded span after the header. Ordinary 0x10-byte records contain signed location and opcode fields, a raw word at +0x04, a derived payload size, and a derived payload offset relative to decoded +0x10.

The last record is a distinct terminator whose location is negative. Its remaining words are preserved as raw data and do not describe a payload. A terminator-only table is valid.

Payloads are owned entities referenced by stable IDs. payloadLayout independently records payload order and explicit gaps inside the header-declared span. Writing rebuilds all record counts, payload sizes, offsets, and the declared span.

Bytes after the declared span are retained as a separate optional file trailer. They are not part of payloadLayout, are not consumed by the inspected loaders, and are appended only after the derived header span has been written. A trailer is preservation-only: its bytes must exactly match the hash in the import receipt and are never endian-converted.

## Typed Commands

The binary command table identifies eleven stable command names:

| Combined type | Binary name | Loader size | Complete semantic codec |
| --- | --- | ---: | ---: |
| 0x00030002 | SPARC | 0x164 | Yes |
| 0x00030003 | PUTMODEL | 0x194 | Yes |
| 0x00030004 | SET COMMAND | 0x1C | Yes |
| 0x0003000A | MOTION PAUSE | 0x18 | Yes |
| 0x0003000B | COLISION BOX | 0x38 | Yes |
| 0x0003000C | MOVE MODEL | 0x1A0 | Yes |
| 0x0003000D | HIT WEAPON | 0x174 | Yes |
| 0x0003001D | POINT LIGHT | 0x70 | Yes |
| 0x0003002A | SYSTEM CAMERA | 0x24 | Yes |
| 0x00030032 | EFFECT WAIT | 0x14 | Yes |
| 0x00030036 | SE REQUEST | 0x2C | Yes |

Loader extent and SPICE codec coverage are separate facts. Every recognized command has a fixed size used by the game loader. A missing or differently sized recognized payload is malformed. All eleven recognized commands now use dedicated typed variants; genuinely unrecognized commands retain bounded opaque payloads and remain receipt-bound and same-byte-order only.

SPARC exposes its established scalar and vector region through +0x63. Its +0x08 word is a decimal-packed MLD filename key, and a formatting helper returns the intrinsic E-family or M-family filename when representable. Asset existence is intentionally consumer-owned. The fixed region from +0x64 through +0x163 is 64 signed `{ value, weight }` choices. A zero weight terminates the active prefix at runtime, but every physical slot remains editable and serializable. SPARC is fully relocatable, endian-portable, and does not require a receipt.

PUTMODEL, MOVE MODEL, and HIT WEAPON each own a fixed 64-entry deterministic model timeline of signed `{ modelIndex, durationTicks }` entries plus signed repeat indices. A zero duration ends the active prefix. An enabled repeat range is inclusive, requires `first < last`, and must remain within the physical 64-entry table. Every entry after the active prefix remains part of the editable document and is always serialized.

PUTMODEL and MOVE MODEL expose their encoded MLD resource identities without resolving external files. Their model flags, frame windows, transforms, scale changes, texture selectors, motion-frame deltas, and model timelines are typed and endian-portable. MOVE MODEL additionally exposes its condition flags and alternate-child parameters. Runtime normalization of invalid source frame windows is not written back into the document.

MOVE MODEL condition bits `0x1000` and `0x8000` are structurally supported by the inspected reader but were absent from the surveyed corpus. They remain valid and representable; corpus absence does not produce a structural validation warning or an invented legality rule.

PUTMODEL bytes +0x192..+0x193 are an established raw byte pair. They are copied in physical order and are never byte-swapped. Unlike an opaque payload, this fixed-width field has a known cross-platform writing rule and does not require a receipt.

MOTION PAUSE exposes its pause flags and start/end frames. Bit `0x8000` selects the established latching state-request path. The game adds common command flag `0x2000` only to a runtime copy; SpiceStd preserves the serialized command flags and never synthesizes that bit.

HIT WEAPON exposes its weapon-path, model, timeline, nested-child, texture, motion, and target-state fields. Its unread +0x08 word and bit-preserving +0x15C scalar retain neutral raw names but follow ordinary numeric byte order. The known target-state modes 1 and 2 map to the observed worksheet flag masks; other values remain representable without invented legality rules.

POINT LIGHT exposes a positional light, signed light slot, RGB, two attenuation parameters, two wave blocks, a pulse interval, and two attenuation ramps. The game rejects light slots greater than 3, so the validator rejects those values without imposing an unproven lower bound. The unread +0x08 word remains a neutral endian-converted integer, and unknown light-flag bits are preserved.

The shared model-flag helpers expose only reader-established behavior: low-nibble anchor-coordinate mode, fallback draw-mode priority, final-motion hold, the qualified Ninja render option, and the fallback-render or mode-0-anchor path. Higher renderer-state bits remain preserved without speculative user-facing names.

System Camera stores a bit-exact f32 mode parameter at +0x14; +0x18 is a signed start frame and +0x1A is a separate reserved u16. No fixed-point conversion applies.

Reserved fixed-position byte arrays, PUTMODEL's raw tail, and unknown bits in established numeric flag words remain editable and endian-portable under their documented physical rules. Recognized fixed-size payload alignment is checked and warned about, but not rejected because the inspected loaders do not enforce it.

Names ending in `Raw` mark semantics that remain unresolved at the current evidence level. These fields retain their established numeric width, signedness, and byte-order behavior and are not opaque byte content. For example, `waitValueRaw` remains a signed endian-aware value without a speculative convenience interpretation.

## Opaque Preservation

Genuinely unrecognized nonempty input may be imported as top-level opaque content only when byte order is caller-specified. Opaque content requires the matching import receipt and may only be emitted with the same byte order. Top-level opaque bytes must remain identical; recognized documents may edit receipt-backed bounded opaque payloads and fragments.

File trailers are receipt-bound, byte-exact, and cannot be written across byte orders. Ordinary fixed reserved byte ranges and structurally known but semantically unresolved action-row fields are not treated as opaque content because their physical representation is established.
