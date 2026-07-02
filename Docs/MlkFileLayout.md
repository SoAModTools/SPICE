# MLK File Layout

This is the living SPICE note for the currently known `.mlk` file layout and
safe ways to use it. Keep this document conservative: promote field meanings
only when backed by runtime handler evidence, corpus scans, or Blender/MLD
inspection.

Primary implementation references:

- `SpiceMlk/MlkScanner.cpp`
- `SpiceMlk/MlkCorpus.cpp`
- `SpiceMlk/MlkBlenderIrExport.cpp`
- `SpiceMlk/MlkAnnotation.schema.json`
- `SpiceMlk/MlkBlenderIrMetadata.schema.json`
- `SpiceFileParsing/SpiceFileParsing.cpp`
- `SpiceTests/test_mlk_scanner.cpp`
- `SpiceTests/test_mlk_corpus_export.cpp`
- `SpiceTests/test_mlk_blender_ir_export.cpp`

Related investigation summaries:

- `planning/Analysis/2026-06-13_initial_mlk_investigation/summary.md`
- `planning/Analysis/2026-06-13_mlk_handler_drilldown/summary.md`
- `planning/Analysis/2026-06-13_mlk_corpus_triage/summary.md`
- `planning/Analysis/2026-06-13_mlk_word12_usage_audit/summary.md`
- `SpiceMlk/research/mlk_filename_pattern_groups_20260613.md`
- `SpiceMlk/research/mlk_blender_group_samples_20260613.md`

## Purpose

MLK is a battle resource container. It is not currently treated as a standalone
geometry format. The decoded file contains a small header, a 0x10-byte record
table, and embedded payloads. Most normal in-bounds payloads probe as MLD-like
files and can be routed through the existing MLD parser for read-only
inspection.

Use this format knowledge for:

- Scanning and classifying `.mlk` files.
- Correlating MLK records with embedded MLD entries, textures, and object trees.
- Generating corpus CSV/JSON summaries.
- Generating combined Blender IR contact sheets and MLK-specific metadata
  sidecars.
- Maintaining local living annotation documents for visual research.

Do not use this as a writer contract yet. SPICE does not currently repack MLK
files, rewrite record tables, or extract embedded payload binaries by default.

## Runtime Loader Evidence

The refreshed local Ghidra project names the MLK path in three layers:

- Battle resource queue construction and file loading:
  - `Battle::Resource::QueueBattleCharacterResourceSet_80073024`: queues
    `CR%03d.MLD`, companion STD files, `CR%03d.MLK`, and `CREN%2d.MLK`.
  - `MLK::QueueSharedBattleMlkResources_8006c710`: queues `JOUCHU.MLK`,
    `F05000%02X.MLK`, and `D05000%02X.MLK` helper resources.
  - `Battle::Resource::AppendBattleResourceQueueEntry_8006e75c`: appends a
    0x1c-byte queued filename/resource entry.
  - `Battle::Resource::StartBattleResourceQueueProcessing_8006dc6c`: starts
    processing after queue builders append filenames.
  - `Battle::Resource::ProcessQueuedBattleResourceList_8006c124`,
    `Battle::Resource::ProcessQueuedBattleResourceFile_8006ddbc`, and
    `Battle::Resource::ProcessQueuedBattleResourceFileAlt_8006e00c`: resolve
    queued BCHARA/BEFF names, reuse cached data, schedule loads, and dispatch
    loaded MLK-like buffers when present.
  - `Battle::Resource::FindLoadedBattleResourceByName_8006daf8`: retrieves
    queued/loaded buffers such as `CR%03d.MLK` and `CRENxx.MLK`.
  - `Battle::Resource::GetSharedBattleResourceBuffer_8022aca8`: returns the
    shared/cached battle resource buffer used by the direct JOUCHU path.
- MLK filename and container entrypoints:
  - `MLK::BuildBattleMlkFilename_8001c6cc`: builds D/F-family BEFF names,
    PCWIN-style special cases, and related battle MLK filenames from selector
    values.
  - `MLK::LoadBeffMlkAndProcessRecords_8006befc`: builds dynamic BEFF MLK
    names, loads `/BEFF/%s%02d.MLK` with fallback to `/BEFF/%s.MLK`, validates
    duplicate keys, and processes records in chunks.
  - `MLK::ProcessQueuedBattleCharacterMlk_80073164`: retrieves queued
    `CR%03d.MLK` / `CRENxx.MLK` buffers and processes them through the all-record
    walker.
- MLK record walking and embedded MLD setup:
  - `MLK::ValidateMlkUniqueRecordKeys_8006e5bc`: validates duplicate record keys
    using the signed record count at decoded offset `0x04`.
  - `MLK::ProcessAllMlkRecords_8006e6e4`: walks every record, patches payload
    offsets to absolute pointers, and calls the mode-0 record consumer.
  - `MLK::ProcessMlkRecordsReverseChunk_8006e4cc` and
    `MLK::ProcessMlkRecordsForwardChunk_8006e640`: process chunk windows of the
    same record table for BEFF paths.
  - `MLK::LoadMlkEmbeddedMldRecord_8006e244`: consumes one MLK record in the
    normal mode-0 path.
  - `MLK::FormatMlkRecordResourceName_8003dbac`: derives names such as
    `E%02d%03d%02d.MLD` or `M%c%03d.MLD` from the MLK record key.
  - `loadTextures_801db124`: existing generic MLD texture/model setup reached by
    copied embedded MLK payloads.
  - `MLK::InitializeMlkLoadedMldObjects_8006cf54`: post-load initialization that
    walks the loaded MLD index table and initializes object blocks.

Known static data:

- `MLK::bcharaJouchuMlkPath_802f9214`: static full-path string/data reference
  for `/bchara/jouchu.mlk`.

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

The observed loader paths walk records, patch each payload offset into an
absolute pointer by adding the MLK base address, and pass embedded payload
pointers/sizes into the existing model/effect loading path. The discovered
mode-0 loader path consumes key, payload pointer, and payload size; it does not
currently explain record `+0x0c`. The current Ghidra name set also clarifies
that `ProcessQueuedBattleResourceFile_8006ddbc` is a general queued battle
resource loader, not an STD-only path.

## Decoded File Layout

All corpus files observed so far are AKLZ-compressed on disc. `SpiceMlk`
decodes AKLZ first and interprets the layout below from decoded bytes.

All integer fields are big-endian.

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `headerWord0` | Preserved raw header word. Often category-looking. No confirmed semantic name yet. |
| `0x04` | 2 | `recordCount` | Signed halfword loop bound used by observed runtime walkers. |
| `0x06` | 2 | `headerWord1Low` | Preserved raw header halfword. Often `0xffff` in normal files. |
| `0x08` | variable | record table | Start of 0x10-byte MLK records. |

`MlkScanResult::headerWords` stores the first four decoded 32-bit words for
triage. Because the record table begins at `0x08`, `headerWords[2]` and
`headerWords[3]` are the first record's key and payload offset in normal files,
not separate header fields.

## Record Table

Each observed MLK record is 0x10 bytes.

| Offset in record | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 4 | `key` | Numeric resource key. Runtime duplicate-key validation compares this field. `MLK::LoadMlkEmbeddedMldRecord_8006e244` uses it to derive a resource label. |
| `0x04` | 4 | `payloadOffset` | Decoded-buffer-relative embedded payload offset. Runtime walkers patch this in place to an absolute pointer. |
| `0x08` | 4 | `payloadSize` | Embedded payload byte size used by the loader copy/parse path. |
| `0x0c` | 4 | `rawWord12` | Preserved unresolved metadata. Keep this name until a runtime consumer explains it. |

Scanner checks per record:

- payload span is in bounds
- payload overlaps the record table
- key duplicates an earlier record key
- payload signature
- payload kind
- embedded MLD header plausibility

## Record Count Selection

The primary count source is the signed halfword at decoded offset `0x04`, named
`header-u16-at-0x04` in corpus output.

The scanner also computes an inferred count from the first payload offset:

```text
(firstPayloadOffset - 0x08) / 0x10
```

This inferred count is used only when the header-count table would extend beyond
the decoded file and the first-payload-offset count is internally valid. When
neither count is usable, the scan is marked `unresolved`.

Current count-source values:

- `header-u16-at-0x04`: normal runtime-observed count source.
- `first-payload-offset`: fallback for a malformed header-count table with a
  plausible first-payload boundary.
- `unresolved`: table bounds cannot be made coherent.

## Payload Classification

`SpiceMlk` currently classifies payloads as:

- `empty`
- `unknown`
- `aklz`
- `mld`
- `ninja-chunk`
- `pof0`

Most useful records are bounded `mld` payloads. The scanner treats a payload as
plausible embedded MLD when the MLD-style header has a sane entry count, a
bounded index table, and offsets that are zero or within the payload.

Embedded MLD probe fields:

- entry count
- index table offset
- function-parameter offset
- real-data offset
- texture table offset

The corpus path can run a lightweight embedded MLD parse and report entry
counts, diagnostics, texture archive presence, object/ground/motion/texture
reference counts, and sampled function names.

## Table Shapes and Anomalies

`SpiceMlk` reports these table shapes:

- `normal`: no record-span errors and no count mismatch evidence.
- `first-payload-count-candidate`: first payload offset implies a plausible
  alternate count.
- `malformed-record-spans`: selected table has one or more invalid payload
  spans.

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

Do not silently repair these files yet. They need subtype-specific runtime or
corpus evidence.

## `rawWord12`

Keep the field named `rawWord12` until a runtime consumer explains it.

Current static audit result:

- The discovered MLK loader path does not read record `+0x0c`.
- Mode-0 calls into `MLK::LoadMlkEmbeddedMldRecord_8006e244` consume key,
  payload pointer, and payload size.
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

`cr001` through `cr022` form a clean sequential group. The file number maps
directly to the first-record/key family:

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

`pcp00` through `pcp09` are compact sequential files. Most report first-key
family `200600`; `pcp08` reports `200700`.

`pcwin.mlk` is a larger standalone file with 37 records and first-key family
`2000101`.

These are likely battle-state, party/window, or result-adjacent resources by
name and handler references, but their exact role needs visual or runtime
confirmation.

Current low-confidence visual hypothesis: `pcp##.mlk` may be a
party-composition slash/effect package rather than a generic UI/window package.
The runtime selector builds a bitmask from present non-Fina player characters,
then maps specific masks to `pcp##` filenames. Blender inspection of `pcp00`,
`pcp01`, and `pcp05` shows a shared set of sword-slash-like effects plus
variant effects that appear consistent with Enrique and Drachma-specific
additions. Fina may be absent from the selector because her weapon usually does
not require slash effects. Treat this as possible but not confirmed until more
runtime or visual evidence is collected.

## Current SPICE Surfaces

`SpiceFileParsing` supports two MLK investigation modes:

```powershell
bin\x64\Debug\SpiceFileParsing.exe <file-or-dir> <output-dir> --export-mlk-corpus
bin\x64\Debug\SpiceFileParsing.exe <file-or-dir> <output-dir> --export-mlk-blender-ir
```

`--export-mlk-corpus` writes:

- `mlk_corpus.json`
- `mlk_corpus_files.csv`
- `mlk_corpus_records.csv`
- `mlk_corpus_word12_histogram.csv`
- `mlk_corpus_anomalies.csv`
- `mlk_corpus_raw_word12_by_kind.csv`
- `mlk_corpus_embedded_mld_summary.csv`

`--export-mlk-blender-ir` writes one output folder per MLK file containing:

- `<stem>_mlk_combined_blender_ir_scene.json`
- `<stem>_mlk_blender_manifest.json`
- `<stem>_mlk_blender_metadata.json`
- `<stem>_mlk_blender_records.csv`

The combined Blender IR JSON stays generic. MLK-specific facts live in sidecars,
not in shared Blender IR schema extensions. The MLK export layer does apply
MLK-local name namespacing to combined scene object-tree labels, texture names,
and index entry `fxnName` values:

```text
<file-stem>_<record-index>_<original-name>
```

Examples:

- `cr001_0_cam_cam_nj`
- `cr001_1_e1901004_e190100jn_4`

The sidecar metadata preserves the original and adjusted `fxnName` values plus
record index, record offset, key, generated MLD name, `rawWord12`, payload span,
payload kind, parse status, combined IR index ranges, and entry metadata.

## Living Annotations

`--export-mlk-blender-ir` also seeds local living annotation documents under:

```text
SpiceMlk/annotations/<stem>/<stem>.mlk_annotation.json
SpiceMlk/annotations/<stem>/<stem>.mlk_annotation_media/
```

These files are ignored by git. They are for manual Blender/in-game research.

Default behavior:

- Missing annotation JSON is created.
- Existing annotation JSON is preserved byte-for-byte.
- The media directory is created if missing.
- The combined Blender IR scene is copied beside the annotation JSON for local
  inspection.

Use this flag only when intentionally regenerating annotation documents:

```powershell
bin\x64\Debug\SpiceFileParsing.exe <file-or-dir> <output-dir> --export-mlk-blender-ir --overwrite-mlk-annotations
```

Annotation JSON contains:

- human-owned `fileNotes`
- computed scanner/export snapshot
- one record entry per MLK record
- empty per-record `humanAnnotations`
- links to the export manifest, metadata, records CSV, and copied combined
  Blender IR

## Safe Usage Notes

- Decode AKLZ before reading the table.
- Treat offset and size fields as decoded-buffer-relative.
- Prefer `recordCount` at offset `0x04` unless the scanner reports the
  first-payload-offset fallback.
- Do not infer semantic meaning from `rawWord12` without runtime or visual
  evidence.
- Treat non-`mld` payload kinds as reportable records, not parser failures.
- Keep anomaly files visible in CSV/JSON output instead of silently repairing
  them.
- For visual inspection, use the MLK metadata/annotation sidecars to correlate
  Blender objects back to records.
- Keep generated corpus exports, Blender IR outputs, annotation docs, and media
  local/ignored unless explicitly requested otherwise.

## Open Questions

- What subtype explains the five cross-region anomaly files?
- Does any non-loader runtime path read record `+0x0c` after MLK processing?
- Are `d24` split suffixes temporal phases, actor/object splits, camera/effect
  splits, or size-driven packaging?
- How do `f29` split packages relate to `d24` split packages?
- Can `pcp` and `pcwin` be tied to a specific battle UI/result state from
  runtime handlers?
