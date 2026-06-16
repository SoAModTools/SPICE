# MLL File Type

This is the starting SPICE note for Skies of Arcadia Legends `.mll` files.
Keep it conservative: `.mll` should be treated as a separate indexed container
until regional corpus checks and game-side handler evidence agree on the table
shape.

Primary implementation references:

- `SpiceMll/MllParser.cpp`
- `SpiceMll/MllModel.h`
- `SpiceMll/MllCorpus.cpp`
- `SpiceMll/MllCorpusScanMain.cpp`
- `SpiceMll/StandaloneMldTextureScan.cpp`
- `SpiceTests/test_mll_parser.cpp`
- `SpiceTests/test_mll_corpus_export.cpp`

Primary research references:

- `planning/Analysis/2026-06-13_initial_mlk_investigation/summary.md`
- `planning/Analysis/2026-06-13_initial_mlk_investigation/handler_evidence.md`
- `SpiceMll/results/corpus/2026-06-16_eu_variant_selector_probe/summary.md`
- `SpiceMll/results/corpus/2026-06-16_eu_selector_code_probe/summary.md`
- `SpiceMll/results/corpus/2026-06-16_indexed_bin_word_fields/summary.md`
- `SpiceMll/results/corpus/2026-06-16_standalone_mld_word24_producer_rule/summary.md`
- `SpiceMll/results/corpus/2026-06-16_standalone_mld_word24_family_probe/summary.md`
- `SpiceMll/results/ghidra/2026-06-16_bchara_beff_word24_cache_path/summary.md`
- `SpiceMll/results/ghidra/2026-06-16_indexed_bin_record_consumers/summary.md`
- `SpiceMll/results/ghidra/2026-06-16_indexed_bin_field_refinement/summary.md`
- `SpiceMll/results/ghidra/2026-06-16_deeper_helper_exports/summary.md`
- `SpiceMll/results/ghidra/2026-06-16_mll_mlk_handler_separation/summary.md`
- `SpiceMll/results/ghidra/2026-06-16_status_save_shop_member_boundary/summary.md`
- `SpiceMll/results/ghidra/2026-06-16_standalone_mld_word24_boundary/summary.md`

## Current Role Hypothesis

The initial Ghidra pass found direct `.mll` references in field, menu, and
ending loaders rather than the MLK character/effect queue path.

Current working interpretation:

- `.mll` is likely an indexed multi-member container.
- Selected members can be copied out by index.
- Selected members can be passed directly to the existing MLD texture/model
  loader path.
- `.mll` should not be folded into `SpiceMlk` unless later evidence proves a
  shared container contract.

## Observed US Layout

The first US corpus scan corrected the initial table guess. All 11 US `.mll`
files currently match this layout after AKLZ decompression:

- header word `+0x00`: observed `0x0000ffff`
- count word `+0x04`: high `u16` is the member count, low `u16` observed
  `0xffff`
- member records begin at `+0x08`
- member record stride is `0x20`
- record `+0x00..+0x13`: null-terminated member name
- record `+0x14`: payload offset
- record `+0x18`: payload size
- record `+0x1c`: observed `0xffffffff`
- first payload offset equals `0x08 + member_count * 0x20`

## Current Parser Boundary

`SpiceMll` currently provides a read-only research probe and corpus scanner:

- optional AKLZ decompression
- `0x08`-byte header probe
- `0x20`-byte named member records containing payload offset and size
- member count from the high halfword at `+0x04`, with a fallback count inferred
  from the first member offset when the header hypothesis does not fit
- payload bounds checks
- payload signature and embedded MLD header probing
- indexed `.bin` table probing by member name, even when the payload also
  looks MLD-like
- diagnostics for malformed or inferred shapes
- JSON and CSV corpus output for files, members, anomalies, and payload-kind
  histograms
- standalone MLD texture-table validation mode:
  `SpiceMllCorpusScan --standalone-mld <input> <output>`

This is still not a final regional schema. It is the first bounded US-backed
probe so real files can be scanned consistently and compared against handler
evidence.

## Embedded MLD Texture Table

For MLD-like MLL member payloads, embedded MLD header `+0x10`
(`textureTableOffset`) points to a counted texture table immediately before the
appended GCIX/GVRT texture blobs:

- `+0x00`: `u32` texture count
- then `texture_count` records, stride `0x2c`
- then padding so the appended texture blob stream starts on a `0x20` boundary
- then appended GCIX/GVRT blobs in table-record order

Current record interpretation:

- `+0x00`: null-terminated texture name within a `0x10` byte field
- `+0x10`, `+0x14`, `+0x18`, `+0x1c`: observed zero in the US/EU/JP MLL
  and standalone MLD corpus scans
- `+0x20`: texture-load descriptor init pointer slot; observed zero in
  file-form records, overwritten during load
- `+0x24`: texture-load descriptor flags/type word; observed `0x80000000` in
  file-form MLL records, with standalone MLD also exposing a `0x00000000`
  file-form variant; overwritten to `0x40800000` by the MLL load helper
- `+0x28`: source byte size of the corresponding appended GCIX/GVRT blob in
  file-form records; used as the descriptor runtime-resource slot during load

Runtime loader evidence now links this table to `FUN_80227af0@80227af0` and
`FUN_80227d5c@80227d5c`. The loader indexes records by `cur_tex_id * 0x2c`,
sums prior `+0x28` source sizes to locate the current blob, and passes
`entry + 0x20` as a three-word texture-load descriptor. On successful MLL loads,
the helper writes loaded descriptor values into those three words, the cache copy
keeps the loaded descriptor, and the live table record restores `+0x28` to the
original source size. In this inspected path, `FUN_80227af0` does not read the
current record `+0x24` before helper normalization; descriptor word 1 is
overwritten to `0x40800000` by the helper path before resource dispatch.
The standalone `word24=0` split is therefore not currently explained as a
direct branch in this loader. Treat it as a preserved file-form descriptor word.
Current BCHARA/BEFF evidence points at shared-resource/cache-reference
semantics rather than a different texture-row decoder.

## Standalone MLD Texture Table Validation 2026-06-15

Command pattern:

```powershell
.\x64\Debug\SpiceMllCorpusScan.exe --standalone-mld <disc_dump_root> SpiceMll\results\corpus\2026-06-15_standalone_mld_texture_table_validation\<region>
```

Regional summary:

- US: 1791 standalone MLD files, 88329 table entries, 86298 inline texture
  entries
- EU: 1810 standalone MLD files, 88959 table entries, 86928 inline texture
  entries
- JP: 1791 standalone MLD files, 88321 table entries, 86292 inline texture
  entries

Standalone validation confirms the `0x2c` texture table is general MLD, not
MLL-specific. The regular inline GCIX/GVRT rows match the MLL-embedded records:
`word24=0x80000000`, `word28` equals paired source size, and `+0x10..+0x20`
remain zero.

Standalone MLD also reveals a file-form variant not present in the MLL corpus:
`word24=0`. These rows can be missing inline texture bytes at the expected order
position, can still be inline GCIX-backed rows, or can be GVRT-only rows. In the
GVRT-only class, `word28` is consistently the parsed GVRT source size plus 16,
which suggests the accounting field can include an implicit/missing GCIX prefix.

The no-inline `word24=0` rows are concentrated in BCHARA/BEFF style resource
families rather than general field/menu MLDs. Across US/EU/JP, nearly all
no-inline rows are under `bchara`, with a fixed small tail under `beff` and
`battle`. In US, 427 no-inline texture names also appear as inline-backed
texture names elsewhere in the standalone MLD corpus. This supports a
shared-resource or cache-reference interpretation for the no-inline class.

The runtime side that makes that interpretation plausible is the MLK
character/effect resource layer. BCHARA/BEFF MLK handlers queue and cache whole
MLD-like resources by name before the normal MLD texture loader resolves
individual texture rows by texture name. `FUN_8006e244@8006e244` stores loaded
MLD-like resources in a linked list keyed by resource name and skips duplicate
loads.

The producer-side rule is now narrowed by standalone MLD corpus evidence.
`word24=0` is file-level consistent: scanned files do not mix `word24=0` and
`word24=0x80000000` rows. The no-inline class is a complete name/size texture
table with no local texture stream, concentrated in BCHARA/BEFF plus
`battle/btlcursor.mld`. The same `word24=0` mode can also appear in BCHARA
files with local inline GCIX rows, so `word24=0` should be treated as a
character/effect-style descriptor mode, while inline/no-inline packaging is a
separate decision.

## US Corpus Scan 2026-06-15

Command:

```powershell
.\x64\Debug\SpiceMllCorpusScan.exe <local-corpus>\2002-12-19-gc-us-final_Skies_of_Arcadia_Legends research\mll\us_corpus_2026-06-15
```

Summary:

- files: 11
- supported files: 11
- normal table shapes: 11
- malformed table shapes: 0
- members: 96
- MLD-like members: 92
- out-of-bounds members: 0
- warnings: 0
- errors: 0

The four non-MLD-like payloads are named `.bin` members and are in bounds:

- `field/HrsBin_Hakken.mll`: `HrsBin_Hakken2.bin`
- `field/HrsBin_sbp.mll`: `HrsBin_sbp_hrs3.bin`
- `field/hrs_wmap.mll`: `hrswmap0.bin`
- `field/hrs_wmap.mll`: `hrswmap1.bin`

The ignored local scan artifacts are under
`research/mll/us_corpus_2026-06-15/`.

## Regional Corpus Scan 2026-06-15

Commands:

```powershell
.\x64\Debug\SpiceMllCorpusScan.exe <local-corpus>\2003-03-05-gc-eu-final_Skies_of_Arcadia_Legends research\mll\eu_corpus_2026-06-15
.\x64\Debug\SpiceMllCorpusScan.exe <local-corpus>\2002-11-12-gc-jp-final_Eternal_Arcadia_Legends research\mll\jp_corpus_2026-06-15
```

Regional summary:

- US: 11 files, 11 supported, 96 members, 92 MLD-like, 4 unknown, 0 warnings,
  0 errors
- EU: 27 files, 27 supported, 326 members, 319 MLD-like, 7 unknown,
  0 warnings, 0 errors
- JP: 10 files, 10 supported, 91 members, 87 MLD-like, 4 unknown, 0 warnings,
  0 errors

All scanned US/EU/JP files use the same `0x20` named member record layout.
Anomaly CSVs are header-only for all three regions.

The 10 files common to US, EU, and JP have identical member names at matching
indexes:

- `ending/sr.mll`
- `ending/srok.mll`
- `field/HrsBin_Hakken.mll`
- `field/HrsBin_Status.mll`
- `field/HrsBin_sbp.mll`
- `field/hrs_wmap.mll`
- `field/ln_test.mll`
- `field/save_hrs.mll`
- `field/shop_hrs.mll`
- `field/wanted.mll`

`field/shop_hrse.mll` is present in US and EU, but not JP.

EU includes language/variant duplicates:

- ending variants: `sr1.mll`, `sr2.mll`, `sr3.mll`, `srok1.mll`,
  `srok2.mll`, `srok3.mll`
- field variants: `HrsBin_sbpf.mll`, `HrsBin_sbpg.mll`, `HrsBin_sbps.mll`,
  `save_hrse.mll`, `save_hrsf.mll`, `save_hrsg.mll`, `save_hrss.mll`,
  `shop_hrsf.mll`, `shop_hrsg.mll`, `shop_hrss.mll`

EU `&&systemdata/Start.dol` explicitly contains these variant `.mll` paths.
US and JP `Start.dol` contain only the base `.mll` path set. This means EU
variant containers are game-side file targets, not only filesystem convention.
The EU selector code is resolved for field/save/shop variants. The helper at
`0x801dd188` returns the word stored at SDA offset `-32448(r13)`. For
field/save/shop variant selection, that helper word maps as `1` English/base,
`2` German, `3` French, `4` Spanish. `HrsBin_sbp`, `save_hrse`, and
`shop_hrse` use base-string offsets `0x00/0x18/0x30/0x48` for
English/German/French/Spanish. Values below `2` or at least `5` fall back to
the base English path.

Ending selection uses a second byte from the same language table. The setter at
`0x801ddf20` writes both the helper word and an ending selector byte from rows
at `0x802f4ff0`:

- English: helper word `1`, ending selector `0`
- German: helper word `2`, ending selector `3`
- French: helper word `3`, ending selector `1`
- Spanish: helper word `4`, ending selector `2`

The EU save dispatcher starts around `0x801a7dd8` and computes the save path
addresses from base `0x802b5a08` plus offsets `0x1f50`, `0x1f68`, `0x1f80`,
and `0x1f98`, producing `/field/save_hrse.mll`, `/field/save_hrsg.mll`,
`/field/save_hrsf.mll`, and `/field/save_hrss.mll`.

The ending loader reads that byte from `0x8031409c + 0x57`, clamps values above
`3` to `0`, and indexes pointer tables at `0x802f4e00` and `0x802f4e10`.
Therefore numbered ending variants map as German `sr3/srok3`, French
`sr1/srok1`, and Spanish `sr2/srok2`, not simply numeric order.

Common-file payload size deltas are limited to MLD-like members:

- `ending/sr.mll`: `sr33.mld` is smaller in JP
- `field/hrs_wmap.mll`: `hrswmap2.bin` differs in EU
- `field/HrsBin_sbp.mll`: members 0, 2, 3, 4, and 5 differ across regions
- `field/HrsBin_Status.mll`: `StaSprite00.bin` is larger in JP

The unknown payload pattern is stable. US and JP have the same four unknown
members. EU has those four plus one `HrsBin_sbp_hrs3.bin` member in each of
the three EU-only `HrsBin_sbp*` variants.

## Indexed `.bin` Members

Named `.bin` members are structured indexed UI/render layout record tables.
They are not generic blobs. Some classify as MLD-like in static probing, but
runtime handler evidence still feeds them to the indexed table initializer
instead of directly to the copied MLD texture/model path.

Validated file-form shape:

- payload `+0x00`: big-endian `u32 count`
- payload `+0x04`: `count` big-endian `u32` relative record offsets
- data base offset: `4 + count * 4`
- record address: `dataBaseOffset + offset[index]`

Runtime handler evidence links this shape to `FUN_801d6998@801d6998`, which
initializes a small table object, stores the original payload base, derives the
data-base pointer, and fixes up record words in place. The accessor
`FUN_801d68c4@801d68c4` returns records by integer index.

Regional named-`.bin` probe results:

- US: 28 indexed BIN table probes, 28 plausible
- EU: 68 indexed BIN table probes, 68 plausible
- JP: 24 indexed BIN table probes, 24 plausible

US support-table members include 24 members that classify as MLD-like and 4
members that classify as unknown. This means `payloadKind=mld` is not enough to
choose the runtime path for `.bin` members.

Deeper consumer disassembly gives this top-level record-field map:

- record `+0x00`: fixed-data table pointer after `FUN_801d6998` fixup
- record `+0x04`: element table pointer after `FUN_801d6998` fixup
- record `+0x08`: element count
- record `+0x0c`: big-endian float layout width / X extent
- record `+0x10`: big-endian float layout height / Y extent
- record `+0x14`: base X / mutable layout X offset
- record `+0x18`: base Y / mutable layout Y offset

The element table has `0x34` byte records. Element `+0x00` indexes a fixed-data
record, element `+0x04/+0x08/+0x0c/+0x10` are local quad coordinate fields, and
element `+0x14` is treated as packed color/alpha data.

The fixed-data table has `0x14` byte records. In the direct quad submit helper,
fixed-data `+0x04/+0x08` feed one source/UV corner and fixed-data
`+0x0c/+0x10` feed the opposite source/UV corner. The nested fixed-data fields
are no longer broad unknowns.

The table accessor is also used to build runtime arrays of display descriptor
pointers. Examples include `FUN_8012b054`, which stores record indexes
`8,0,1,2,3,4,5,6,7`, and `FUN_8012dfe0`, which stores a nonlinear set of
status/ship-page record indexes including `0x8c` or `0x8d` based on a UI state
byte.

Ignored regional comparison artifacts are under
`research/mll/regional_compare_2026-06-15/`.

## MLL vs MLK Boundary

The current evidence supports keeping `.mll` and `.mlk` as separate container
contracts. They share the downstream MLD texture/model loader path, but not the
same wrapper structure.

MLK evidence from the initial investigation points at a BCHARA/BEFF
character/effect resource table:

- count at container `+0x04`
- records at `+0x08`
- record stride `0x10`
- record `+0x04` is a base-relative payload pointer patched before use
- record `+0x08` is the payload byte size passed into an MLD-like load helper

MLL evidence points at a field/menu/ending indexed member container:

- header at `+0x00/+0x04`
- named member records at `+0x08`
- member stride `0x20`
- zero-based member index lookup through `FUN_801e4974` and `FUN_801e4984`
- selected member payloads can be copied or passed to `loadTextures_801db124`

Parser consequence: keep `SpiceMll` as the dedicated MLL parser and start any
future MLK parser from the MLK `0x10` record-table evidence, not from the MLL
member-table model. Shared handling begins at validated MLD payload bytes.

This boundary also explains the likely runtime side of standalone MLD
`word24=0` rows. BCHARA/BEFF code can cache whole MLD-like resources by name
through `FUN_8006e244` before individual texture rows are resolved through the
normal loaded-texture cache. That makes no-inline texture-name references
plausible in character/effect assets without requiring `FUN_80227af0` to branch
on texture-table `+0x24`.

## Status, Save, And Shop Member Boundary

The status/save/shop UI containers have descriptive member names. Runtime
helpers show member `4` is copied and loaded through the MLD texture/model path,
while members `0..3` are initialized as indexed render/layout tables through
`FUN_801d6998`.

- `field/HrsBin_Status.mll`: `StaCard.bin`, `StaDeco.bin`, `StaPaper.bin`,
  `StaSprite00.bin`, `ts0009.mld`
- `field/save_hrs.mll`: `save.bin`, `savecard.bin`, `savedeco.bin`,
  `sprite02.bin`, `sprite02.mld`
- `field/shop_hrs.mll`: `shop.bin`, `shopcard.bin`, `shopdeco.bin`,
  `sprite01.bin`, `sprite01.mld`
- `field/shop_hrse.mll`: `shope.bin`, `shopcarde.bin`, `shopdecoe.bin`,
  `sprite01.bin`, `sprite01.mld`

`menu_listener@801920ac` calls the status MLL wrapper path, then
`FUN_801926b8@801926b8` performs concrete setup for `HrsBin_Status`: member `4`
is copied and loaded through `loadTextures_801db124`; members `0..3` become
indexed table objects.

`FUN_801a69dc@801a69dc` is a save wrapper into `FUN_801c5228@801c5228`, and
`FUN_801be670@801be670` is a shop state dispatcher that also calls
`FUN_801c5228`. `FUN_801c5228` uses the incoming MLL path, loads member `4`
through the MLD path, then loops member indexes `0..3` through `FUN_801d6998`.

Resolved runtime role pattern:

- status/save/shop members `0..3`: indexed render/layout tables
- status/save/shop member `4`: copied MLD asset loaded through texture/model
  path

## How To Start The Investigation

1. Run the `SpiceMll` parser against all US `.mll` files and summarize table
   shapes, member counts, payload signatures, and embedded MLD plausibility.
2. Use the regional comparison artifacts to prioritize member extraction and
   named `.bin` support-table investigation.
3. For the known handler files, correlate member indexes with the Ghidra
   consumers:
   - `field/HrsBin_Hakken.mll`
   - `field/ln_test.mll`
   - `field/hrs_wmap.mll`
   - `field/HrsBin_sbp.mll`
   - `field/wanted.mll`
   - `field/HrsBin_Status.mll`
   - `field/save_hrs.mll`
   - `field/shop_hrse.mll`
   - `ending/sr.mll`
   - `ending/srok.mll`
4. Add member extraction or MLD handoff export now that US/EU/JP comparison
   confirms a stable member table shape.

## Open Questions

- Should indexed `.bin` record `+0x0c/+0x10` be named simply width/height or
  more specifically clipping/layout extents after deeper consumer naming?
- Which surrounding MLK/resource manifests supply the texture bytes for each
  no-inline standalone MLD `word24=0` reference table?
