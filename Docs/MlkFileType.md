# MLK File Type

This is the living SPICE note for the currently known `.mlk` file structure.
It should stay conservative: fields are promoted only when backed by game-side
handler evidence, corpus scans, or Blender/MLD inspection.

Primary implementation references:

- `SpiceMlk/MlkScanner.cpp`
- `SpiceMlk/MlkCorpus.cpp`
- `SpiceMlk/MlkBlenderIrExport.cpp`
- `SpiceFileParsing/SpiceFileParsing.cpp`
- `SpiceTests/test_mlk_scanner.cpp`
- `SpiceTests/test_mlk_corpus_export.cpp`
- `SpiceTests/test_mlk_blender_ir_export.cpp`

Related investigation summaries:

- `planning/Analysis/2026-06-13_initial_mlk_investigation/summary.md`
- `planning/Analysis/2026-06-13_mlk_handler_drilldown/summary.md`
- `planning/Analysis/2026-06-13_mlk_corpus_triage/summary.md`
- `planning/Analysis/2026-06-13_mlk_word12_usage_audit/summary.md`

## Container Role

MLK files are battle character/effect resource tables. The observed game paths
load an MLK buffer, walk a 0x10-byte record table, patch each record's payload
offset into an absolute pointer, and pass embedded payloads into the existing
MLD texture/model loader path.

Current evidence says MLK is not a standalone geometry format. It is a wrapper
around embedded MLD-like payloads, mostly under `/BCHARA/` and `/BEFF/`.

All US corpus `.mlk` files observed so far are AKLZ-compressed on disc. The
scanner decodes AKLZ before interpreting the MLK table.

## Runtime Loader Evidence

High-confidence handlers:

- `FUN_80073024`: queues `CR%03d.MLK` and `CREN%2d.MLK` character resources.
- `FUN_80073164`: retrieves queued `CR%03d.MLK` / `CRENxx.MLK` buffers and
  processes records through `FUN_8006e6e4`.
- `FUN_8006befc`: builds dynamic BEFF MLK names, loads `/BEFF/%s%02d.MLK` with
  fallback to `/BEFF/%s.MLK`, validates duplicate keys, and processes records in
  chunks.
- `FUN_8006c710`: queues `JOUCHU.MLK`, `F05000%02X.MLK`, and
  `D05000%02X.MLK` helper resources.
- `FUN_8006e244`: consumes one MLK record in the normal mode-0 path.

Observed format strings include:

- `CR%03d.MLK`
- `CREN%2d.MLK`
- `D%02d%03d00.MLK`
- `D05000%02X.MLK`
- `F05000%02X.MLK`
- `PCP%02d.MLK`
- `PCWIN.MLK`
- `/BEFF/%s%02d.MLK`
- `/BEFF/%s.MLK`

## Header

The decoded MLK buffer begins with an 8-byte header followed by the record
table.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `headerWord0` | Preserved. Often category-looking. Not consumed by the observed record walkers. |
| `0x04` | 2 | `recordCount` | Signed halfword loop bound used by observed runtime walkers. |
| `0x06` | 2 | `headerWord1Low` | Preserved as part of raw header word. Often `0xffff` in normal files. |
| `0x08` | variable | record table | Start of 0x10-byte MLK records. |

The scanner also reports the first payload offset and an inferred count from
that offset. This is evidence for anomaly triage, not a replacement for the
runtime-observed halfword count.

## Record Table

Each observed MLK record is 0x10 bytes.

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `key` | Numeric resource key. Duplicate-key validation compares this field. `FUN_8006e244` uses it to derive a resource label. |
| `0x04` | 4 | `payloadOffset` | Base-relative embedded payload offset. Runtime walkers patch it in place by adding the MLK base pointer. |
| `0x08` | 4 | `payloadSize` | Embedded payload byte size used by the loader copy path. |
| `0x0C` | 4 | `rawWord12` | Preserved unresolved metadata. The observed mode-0 loader path does not consume it. |

For bounded records whose payload starts with a plausible MLD header, SPICE
classifies the payload as `MldFile` and can optionally parse it through the MLD
pipeline for reporting or Blender IR export.

## Embedded MLD Payloads

Most normal records contain a plausible embedded MLD payload. The scanner probes
the embedded MLD header fields:

- entry count
- index table offset
- function-parameter offset
- real-data offset
- texture table offset

The corpus exporter can run a lightweight embedded MLD parse to report entry
counts, diagnostics, texture archive presence, object/ground/motion/texture
reference counts, and sampled function names. The Blender IR export mode parses
in-bounds `MldFile` payloads and combines them into one record-aware scene JSON
per MLK file.

This phase remains read-only. SPICE does not write MLK files and does not
extract embedded payload binaries by default.

## Table Shapes and Anomalies

`SpiceMlk` currently reports these table shapes:

- `normal`: no record-span errors and no count mismatch evidence.
- `first-payload-count-candidate`: first payload offset implies a plausible
  alternate count, but the runtime halfword count is still preserved.
- `malformed-record-spans`: the table count matches first-payload offset, but
  one or more records produce invalid payload spans.

US corpus snapshot:

- 574 `.mlk` files
- 569 `normal`
- 1 `first-payload-count-candidate`
- 4 `malformed-record-spans`

The same five anomaly files were seen in US, EU, and JP corpus scans:

| Path | Shape | Notes |
| --- | --- | --- |
| `beff/d2403900.mlk` | `first-payload-count-candidate` | Header halfword gives `20992`; first payload offset implies `82`. Split siblings are normal. |
| `beff/d2900200.mlk` | `malformed-record-spans` | Valid leading table region followed by unresolved auxiliary-looking rows. |
| `beff/f2705733.mlk` | `malformed-record-spans` | Same broad anomaly class as `f290986b`. |
| `beff/f2900200.mlk` | `malformed-record-spans` | Parallel to `d2900200`. |
| `beff/f290986b.mlk` | `malformed-record-spans` | Valid leading records plus unresolved malformed spans. |

Do not silently repair these files yet. They need subtype-specific evidence.

## `rawWord12`

Keep the field named `rawWord12` until a runtime consumer explains it.

Current static audit result:

- The discovered MLK loader path does not read record `+0x0C`.
- The mode-0 calls into `FUN_8006e244` consume key, payload pointer, and payload
  size.
- The loaded-resource cache stores the loaded resource/name, not a retained
  pointer to the source MLK record.

Filtered parsed-MLD corpus values collapse to this compact set:

`0, 2, 3, 4, 5, 6, 7, 8, 9, 12, 16, 17, 20, 24, 28`

Common values such as `16`, `4`, `8`, and `28` appear broadly. Rare non-multiple
or camera-heavy values cluster in BEFF records:

- `5`, `7`, and `9`: frequently camera/effect-camera-like records.
- `3`: seen in an effect-camera row in `d2404600`.
- `17`: seen in one `d2406100` component row.

Working interpretation: `rawWord12` is probably MLK-side record metadata such as
component class, layer/order, subtype, or presentation hint. It does not look
like a command dispatch value.

## Filename Families

The filename often carries command/resource identity.

### `bchara/cr###.mlk`

`cr001` through `cr022` form a clean sequential group. The number maps directly
to the header/key family:

- `cr001.mlk` -> `1901000`
- `cr002.mlk` -> `1902000`
- ...
- `cr022.mlk` -> `1922000`

Initial Blender inspection identifies these as crew attack battle animations.
Some files reuse records from neighboring `19xx` ranges, so a command package
can include shared components rather than only local records.

### `bchara/cren##.mlk`

`cren30` through `cren34` map to the `2303xxx` key range. They are related to
the `cr` family by naming and structure, but distinct from the main `190x` crew
attack range.

### `bchara/jouchu.mlk`

`jouchu.mlk` is a large shared/resident battle animation/effect bundle. It has
92 records and spans many key ranges. Visual inspection links it to an animation
after a counter attack related to `d2405100`.

### `beff/d24MMM00.mlk`

This is the strongest current battle command / S-move family.

Working interpretation:

- `d24`: BEFF command/effect family.
- `MMM`: decimal-looking move or command index.
- trailing `00`: normal variant slot.
- extra trailing digits: split package suffix.

Examples from visual inspection:

- `d2404600`: Enrique S-move candidate.
- `d2404700`: another Enrique S-move candidate.
- `d2405100`: Vyse S-move candidate.
- `d2406100`: crew move Prophecy candidate.

Split clusters worth inspecting:

- `d2403400`, `d240340000`, `d240340001`
- `d2404700`, `d240470000`, `d240470001`
- `d2405300`, `d240530000`, `d240530001`, `d240530002`, `d2405302`

### Other `d*` families

- `d05`: small `D05000%02X` helper group; mostly 2-record files.
- `d29`: small separate BEFF command/effect group in the `1500xxx` key range.

### `beff/f*`

`f*` files dominate the corpus and appear to be lower-level effect packages or
per-command/per-actor variants.

Observed families:

- `f04`
- `f05`
- `f08`
- `f27`
- `f29`

`f04` and `f08` share several suffixes, suggesting the suffix may identify a
reusable effect/action slot while the family number identifies a context.

`f29` has split-package patterns similar to `d24`, for example:

- `f290839d`, `f290839d00`, `f290839d01`, `f290839d02`, `f290839d03`
- `f290849d`, `f290849d00`, `f290849d01`, `f290849d02`, `f290849d03`,
  `f290849d04`

### `pcp##.mlk` and `pcwin.mlk`

`pcp00` through `pcp09` are compact sequential files. Most report
`headerWord2=200600`; `pcp08` reports `200700`.

`pcwin.mlk` is a larger standalone file with 37 records and
`headerWord2=2000101`.

These are likely battle-state, party/window, or result-adjacent resources by
name and handler references, but their exact role needs visual or runtime
confirmation.

## Current SPICE Surfaces

`SpiceFileParsing` supports two MLK investigation modes:

- `--export-mlk-corpus`: recursively scans MLK files and writes JSON/CSV corpus
  summaries.
- `--export-mlk-blender-ir`: recursively scans MLK files and writes one
  combined Blender IR scene plus manifest/record CSV per input MLK.

The Blender IR path carries optional `sourceRecord` metadata into JSON and the
Blender importer. Imported objects can be grouped by record and color-keyed by
`rawWord12` for manual inspection.

## Open Questions

- What subtype explains the five cross-region anomaly files?
- Does any non-loader runtime path read record `+0x0C` after MLK processing?
- Are `d24` split suffixes temporal phases, actor/object splits, camera/effect
  splits, or size-driven packaging?
- How do `f29` split packages relate to `d24` split packages?
- Can `pcp` and `pcwin` be tied to a specific battle UI/result state from
  runtime handlers?
