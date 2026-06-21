# Bin File Records

Living document for one-to-one record annotations for assessed `.bin` files. `BinFileLayout.md` describes the file family and field layout; this document tracks per-file record identity for editor/exporter naming.

Detailed evidence, generated crops, contact sheets, Ghidra exports, and scratch notes stay under ignored `SpiceBin/research/`. Promote rows here when a record has a stable one-to-one identity or a useful provisional editor label.

## Promotion Rules

- Keep one row per serialized top-level record.
- Use `Human Annotation` for observed in-game meaning or user-verified visual interpretation.
- Keep provisional keys and descriptions when the consumer evidence is useful but not final.
- Leave unknowns explicit instead of inventing names.
- Record companion texture banks in `BinFileLayout.md`; use this document for record identity.

## Assessed File Coverage

| File | Family | Records | One-to-one status | Notes |
| --- | --- | ---: | --- | --- |
| `battle/HrsBinCW.bin` | HRSBin-style indexed UI layout | 104 | Promoted below | Complete draft from Ghidra consumers plus rendered `command.mld` composites. 92 high-confidence rows, 12 medium-confidence rows, 50 human annotations. |
| `battle/HrsBinPCWin.bin` | HRSBin-style indexed UI layout | 41 | Promoted below | Battle player-character status window shown between the SP gauge and command wheel. The battle action-select thread toggles byte 0 of its worksheet for current-character vs all-character display. |
| `field/HRSBin.bin` | HRSBin-style indexed UI layout | 34 | Pending consumer identification | Loose field layout with identified companion textures. Current Ghidra term search found zero decompile matches for `HRSBin.bin` / `HRSBin.mld`; the X-menu uses embedded `field/HrsBin_Status.mll` members instead. |
| `field/HrsBin_Status.mll:0 StaCard.bin` | Embedded HRSBin-style indexed UI layout | 13 | Direct constant-access rows promoted | Confirmed field X-menu embedded status-card/page layout bank initialized into `DAT_803470e8`; one record remains unmapped because no constant consumer has been found. |
| `field/HrsBin_Status.mll:1 StaDeco.bin` | Embedded HRSBin-style indexed UI layout | 18 | Complete for current evidence | Confirmed field X-menu embedded decoration/selector layout bank initialized into `DAT_803470ec`; all records have promoted one-to-one rows. |
| `field/HrsBin_Status.mll:2 StaPaper.bin` | Embedded HRSBin-style indexed UI layout | 113 | Direct constant-access rows promoted | Confirmed field X-menu embedded paper/page layout bank initialized into `DAT_803470f0`; 12 records remain unmapped because no constant consumers have been found. |
| `field/HrsBin_Status.mll:3 StaSprite00.bin` | Embedded HRSBin-style indexed UI layout | 89 | Direct constant-access rows promoted | Confirmed field X-menu embedded sprite/icon layout bank initialized into `DAT_803470f4`; 16 records remain unmapped because no constant consumers have been found. |
| `field/wanted.mll:0..1` | Embedded HRSBin-style indexed UI layout | pending | Runtime-proven, record mapping pending | Wanted viewer state `19` loads `/field/wanted.mll`; `FUN_801866b4` initializes members `0` and `1` into `DAT_803470dc` and `DAT_803470d8` and registers member `2` as the texture bank. These are separate from the `HrsBin_Status.mll` records promoted below. |
| `field/hrs_bend.bin` | HRSBin-style indexed UI layout | 63 | Promoted below | Confirmed battle-end result screen layout paired with `field/ts000110.gvr`. Includes main result backdrop, EXP/stat/magic result rows, level/rank-up badges, and dropped-item popup pieces. |
| `field/wmaparea.BIN` | Fixed world-map area table | n/a | Not a record atlas | Assessed separately as world-map area/state table, not the same indexed UI record family. |

## field/HRSBin.bin Loose Candidate

Source research: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/README.md`

`field/HRSBin.bin` is still a valid loose 34-record HRSBin-style indexed layout with likely companion `field/HRSBin.mld`, but it should not be assigned to the field X-menu/status-menu. A current read-only Ghidra decompile-term search under `ghidra_export/loose_hrsbin_current_decompile_search/` found:

| Search term | Matching functions |
| --- | ---: |
| `HRSBin`, `HRSBIN`, `hrsbin` | 3 |
| `HrsBin_Hakken` | 1 |
| `HrsBin_sbp` | 1 |
| `HrsBin_Status` | 1 |
| `HRSBin.bin` | 0 |
| `HRSBin.mld` | 0 |

The three matches are `FUN_800c2f4c` (`/field/HrsBin_Hakken.mll`), `FUN_8012ab84` (`/field/HrsBin_sbp.mll`), and `menu_listener@801920ac` (`/field/HrsBin_Status.mll`). No current DOL decompile match names the loose `/field/HRSBin.bin` or `/field/HRSBin.mld` path. Record-level annotations for the loose 34-record file remain blocked on finding a consumer or proving an archive/script-side constructed load path.

A later structural Ghidra scan reached the same boundary from the loader side.
The embedded/member initializer `FUN_801d6998` has 19 current xrefs covering the
known MLL-style callers, while the standalone loose-file wrapper `FUN_801d6b58`
has only one current xref: `FUN_800e3594` loading `/field/HRS_BEND.BIN`. A US
extracted-corpus search found `HRSBin.bin` and `HRSBin.mld` in directory/TOC
metadata but did not find a script-side load reference. Keep the loose
`field/HRSBin.bin` records unmapped until runtime instrumentation or a
non-string resource-construction trace identifies a consumer.

A lower-level direct caller audit of `loadFileFromPath@801cc3c4` found 17
callsites in 13 unique caller functions. The audited field literal loads were
`/field/ts000110.gvr`, `/field/wmaparea.bin`, and `/field/wmaparea.BIN`; the
visible constructed field path was `sr_%03d%c.tec`. It did not find a direct
loose `field/HRSBin.bin` or `field/HRSBin.mld` load.

## field/HrsBin_Status.mll Embedded Members

Source research: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/README.md`

Runtime evidence identifies these embedded members as the field X-menu/status-menu layout banks. `menu_listener@801920ac` opens the menu from `RawControllers[DAT_80347154]->newPresses & 0x400`, loads `/field/HrsBin_Status.mll`, calls `FUN_801926b8`, and creates child callback `FUN_80191ec8`. `FUN_801926b8` initializes members `0..3` with `FUN_801d6998` and loads member `4` (`ts0009.mld`) through `loadTextures_801db124`. The normal field-entry path loads `/field/HrsBin_sbp.mll` first through `FUN_8012a39c -> FUN_8012aa44 -> FUN_8012ab84`, then sets `DAT_803473e8 = 1`; the listener requires that flag before accepting X-menu input. This supports treating common UI previews as dependent on already-active field/menu texture state, not just the `HrsBin_Status.mll` member-4 GVR list.

Ghidra annotation handoff: `planning/Analysis/2026-06-20_field_x_menu_bin_annotation/20260620_field_x_menu_bin_annotation_guide.txt`

Rendered contact sheets and per-element metadata are in `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/`. Treat the original texture-backed rendered previews as diagnostic only for most non-portrait rows: the first-pass renderer used extracted-GVR ordinal order for fixed-data word `0x00`, but the game binds textures through an active render context. The checked direct draw path (`FUN_801d4f0c` -> `FUN_801d4f54`) does not read that word at all; it receives texture/render state through `DAT_80347568` and only reads fixed-data offsets `0x04..0x10` as UV/source coordinates. The checked descriptor/queue path copies fixed-data word `0x00` into a transient descriptor, then passes it to `FUN_80298fe4`, which indexes the active context entry table and binds that entry's texture descriptor. The common queued wrappers `FUN_801d52a8` and `FUN_801d5210` supply that active context from `DAT_80347614->+4->+0x20`, and `DAT_80347614` is the `/field/Sprite00.mld` object loaded by `FUN_801d66d4`. This explains why small rows such as `StaPaper 0x2c`, `StaPaper 0x40`, and `StaSprite00 0x0c` can render as blank or nonsensical crops in the old sheet: the previewer selected the wrong engine context entry/material bank, not just the wrong crop rounding. A follow-up alpha/candidate scan of the high-confidence sheet found 300 fully blank elements and 540 elements under 5% alpha with the first-pass renderer, confirming a systematic material-binding mismatch. A focused screenshot follow-up showed that `StaPaper 0x2c` and `StaPaper 0x40` produce non-empty, correctly scaled source crops when the same UVs are applied to `/field/ts000110.gvr` (`1024x512`, material id `0x1869e`). A later focused Sprite00 pass showed compact digit records `StaSprite00 0x05..0x0e`, all with fixed-data word `1`, form digits `0..9` when rendered against decoded `/field/Sprite00.mld` texture entry `1` (`ts000112`), proving that at least this queued selector maps to the Sprite00 context rather than to `HrsBin_Status.mll` member-4 texture slot `1`. A queued Sprite00-context pass also resolved the `StaDeco` common frame/deco/accent rows, including `StaDeco 0x02`, against Sprite00 selectors. The latest direct-context pass then added `FUN_800ff348` render-queue mode `0`/`1`, branch-selected direct records, object slot groups `0` and `1`, primary object `param[0x11]` records, and the `FUN_8019ae84` active selector-frame stack pair to the proven `/field/ts000110.gvr` context. Use `status_preview_material_diagnostics/HrsBin_Status_material_aware_contact_sheet.png` for visual review: all 204 keyed rows now use material contexts, with no geometry-only fallbacks. Rows below are promoted from consumer evidence, not from the old texture-backed previews. Expanded direct Ghidra access evidence is summarized in `status_mll_record_contact_sheets/direct_record_access_summary.tsv`, with raw decompile-line evidence in `ghidra_export/hrs_status_global_refs/direct_record_accesses.tsv`; preview failure evidence is in `status_preview_material_diagnostics/high_confidence_preview_material_candidates.tsv`, `status_preview_material_diagnostics/sample_preview_material_candidates.png`, `status_preview_material_diagnostics/screenshot_sample_ts000110_comparison.png`, the focused four-example sheet `status_preview_material_diagnostics/sent_sample_failure_diagnostic.png`, the Sprite00 digit proof sheet `status_preview_material_diagnostics/stasprite00_compact_digits_sprite00_grid.png`, the queued `StaDeco` context sheet `status_preview_material_diagnostics/stadeco_queued_sprite00_context_candidates.png`, and the material-aware sheet `status_preview_material_diagnostics/HrsBin_Status_material_aware_contact_sheet.png`.

### Member Layout Groups

| Member | Runtime global | Member name | Records | Current role |
| ---: | --- | --- | ---: | --- |
| 0 | `DAT_803470e8` | `StaCard.bin` | 13 | Status-card/page backing records. |
| 1 | `DAT_803470ec` | `StaDeco.bin` | 18 | Decoration, selector, numeric glyph, and animated accent records. |
| 2 | `DAT_803470f0` | `StaPaper.bin` | 113 | Paper/page records, including state-specific page pieces. |
| 3 | `DAT_803470f4` | `StaSprite00.bin` | 89 | Sprite/icon and compact numeric glyph records. |

### Preview Material Context Status

Current preview evidence now resolves the high-confidence `StaSprite00.bin`
selector groups against `/field/Sprite00.mld`: fixed-data word0 maps to the
same-numbered decoded Sprite00 texture entry for the checked rows. This gives
usable previews for compact digits, stat labels, moon labels, selector strips,
option bars, and MAX/Rank/Next labels. The same rule resolves `StaDeco.bin`
wide digit records `0x04..0x0d`: selector `4` renders as digits `0..9` on
`ts000116`. The queued Sprite00-context path also resolves `StaDeco.bin`
records `0x00`, `0x01`, `0x02`, `0x03`, and `0x0e..0x11` as common
frame/rule/portrait-deco/selector-accent art. The generated selector sheet is
`SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_material_diagnostics/stasprite00_selector_context_groups.png`;
the row audit is
`SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_material_diagnostics/status_preview_context_mapping_audit.tsv`.

| Selector | Sprite00 texture | Rows covered |
| ---: | --- | --- |
| `0` | `ts000111` | Stat labels and companion stat fragments. |
| `1` | `ts000112` | Compact digits `0..9`, moon labels, selector strips, detail markers, and option/value labels. |
| `4` | `ts000116` | Options dynamic bar fragments. |
| `5` | `ts000124` | `MAXHP`, `MAXMP`, `Rank`, `Next`, `MAXSpirit`. |
| `6` | `ts000201` | Options active selector icon. |

The remaining non-Sprite00 groups are not solved by this selector rule. The
current row audit marks 17 non-Sprite rows as `proven_direct_ts000110_context`,
9 as `proven_render_queue_direct_ts000110_context`, 10 as
`proven_selected_direct_ts000110_context`, 53 as
`proven_object_direct_ts000110_context`, and 24 as
`proven_object_primary_ts000110_context`. These paths all use `DAT_80347568`
and the default `/field/ts000110.gvr` material context. The final selected
direct row is `StaPaper.bin 0x69`: the disassembly for `FUN_8019ae84` shows it
is stack entry 2 for selector slot `0` and stack entry 3 for selector slot `1`,
and both entries are the direct `FUN_801d4f0c` pass. The paired
`StaSprite00.bin 0x4f` icon remains on the Sprite00 selector context. A broader
visual candidate sheet exists at
`SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_material_diagnostics/non_sprite00_ts000110_direct_context_candidates.png`.

### Direct Access Coverage

The focused Ghidra pass exports every function directly referencing one of the four runtime globals. Coverage below counts unique records reached by constant `FUN_801d68c4(global, record)` calls.

| Member | Records | Directly referenced records | Missing direct records |
| --- | ---: | ---: | --- |
| `StaCard.bin` | 13 | 12 | `0x09` |
| `StaDeco.bin` | 18 | 18 | none |
| `StaPaper.bin` | 113 | 101 | `0x02`, `0x03`, `0x29`, `0x47`, `0x56`, `0x57`, `0x58`, `0x59`, `0x5a`, `0x61`, `0x6c`, `0x6d` |
| `StaSprite00.bin` | 89 | 73 | `0x03`, `0x34`, `0x39`, `0x3d`, `0x3e`, `0x40`, `0x41`, `0x42`, `0x43`, `0x44`, `0x45`, `0x48`, `0x4b`, `0x53`, `0x54`, `0x57` |

### Current One-To-One Coverage

Generated audits:

- `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/status_record_coverage_audit.tsv`
- `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/ghidra_export/whole_program_record_accessor_scan/status_accessor_call_arg_scan.tsv`
- `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/ghidra_export/whole_program_record_accessor_scan/status_global_non_accessor_refs.tsv`
- `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/unmapped_record_focus/unmapped_records_contact_sheet.png`
- `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/unmapped_record_focus/unmapped_record_visual_summary.tsv`

All records reached by constant `FUN_801d68c4(status_global, record)` calls in the focused status-global export now have promoted rows in this document. A follow-up whole-program disassembly-level scan walked every xref to `FUN_801d68c4`, locally backtracked PowerPC argument setup for `r3` and `r4`, and found the same 204 unique status-bank records: 12 `StaCard`, 18 `StaDeco`, 101 `StaPaper`, and 73 `StaSprite00`. Every status-bank call in that scan also has a resolved record index; there are no unresolved table-driven status-bank calls in the current evidence. Comparing all direct xrefs to the four status-bank globals against the accessor scan leaves only lifecycle references: `FUN_801926b8` initializes the banks from MLL members `0..3`, and `FUN_801925d8` frees and clears them. No additional consumers were found for the remaining unmapped records, so those rows need runtime evidence, screenshot correlation, or proof that they are unused before they should receive machine-facing keys.

| Member | Records | Promoted one-to-one rows | Unmapped records without constant consumer |
| --- | ---: | ---: | --- |
| `StaCard.bin` | 13 | 12 | `0x09` |
| `StaDeco.bin` | 18 | 18 | none |
| `StaPaper.bin` | 113 | 101 | `0x02`, `0x03`, `0x29`, `0x47`, `0x56`, `0x57`, `0x58`, `0x59`, `0x5a`, `0x61`, `0x6c`, `0x6d` |
| `StaSprite00.bin` | 89 | 73 | `0x03`, `0x34`, `0x39`, `0x3d`, `0x3e`, `0x40`, `0x41`, `0x42`, `0x43`, `0x44`, `0x45`, `0x48`, `0x4b`, `0x53`, `0x54`, `0x57` |

### Field X-Menu Mapping Status

The field X-menu controller and record banks are now identified well enough for
a descriptive parser/exporter to expose the confirmed record families without
assigning the wrong source file. The menu opened by pressing X on the field is
`/field/HrsBin_Status.mll`, not the loose `field/HRSBin.bin` file. Current
Ghidra evidence proves the X-triggered load path, the party-selector root page,
the More/next-page route, and the five left/right root stat-carousel windows.

For the embedded status banks, all constant `FUN_801d68c4(status_global,
record)` consumers found by the focused and whole-program Ghidra scans now have
one-to-one rows in this document. The remaining mapping work is split into two
different classes:

| Class | Records | Exporter treatment |
| --- | --- | --- |
| Consumed but visually provisional | none currently marked medium | The common backing/overlay records, selector-helper variants, and detail-row `0xff` sentinel marker are now high-confidence structural records after Ghidra caller and rendered-composite review. Some final visible UI labels can still be improved with screenshot/runtime correlation, but the machine-facing structural keys are stable enough for exporter exposure. |
| Serialized with no discovered consumer | `StaCard 0x09`; `StaPaper 0x02`, `0x03`, `0x29`, `0x47`, `0x56`, `0x57`, `0x58`, `0x59`, `0x5a`, `0x61`, `0x6c`, `0x6d`; `StaSprite00 0x03`, `0x34`, `0x39`, `0x3d`, `0x3e`, `0x40`, `0x41`, `0x42`, `0x43`, `0x44`, `0x45`, `0x48`, `0x4b`, `0x53`, `0x54`, `0x57` | Preserve as serialized records with raw element data and rendered previews, but do not expose semantic editor keys unless runtime evidence, screenshot correlation, or new consumer evidence proves a role. |

The loose `field/HRSBin.bin` remains a valid 34-record HRSBin-style layout with
identified companion textures, but its consumer is still unresolved. Static DOL
term searches, indexed-object initializer/wrapper scans, direct
`loadFileFromPath` caller auditing, and US extracted-corpus searches currently
do not find a consumer for that loose file.

### Original Field X-Menu Request Audit

This table records the current answer to the original field-menu investigation
request. The starting assumption was that the X-triggered field menu used loose
`field/HRSBin.bin`; current evidence shows that assumption is false.

| Requirement from investigation | Current evidence | Result |
| --- | --- | --- |
| Find the controller for the menu opened by pressing X on the field. | `menu_listener@801920ac` checks `RawControllers[DAT_80347154]->newPresses & 0x400` when closed and begins the open transition. | Proven. |
| Identify the file loaded by that controller. | `menu_listener@801920ac` loads `/field/HrsBin_Status.mll`, then `FUN_801926b8` initializes embedded members `0..3` and loads member `4` (`ts0009.mld`). | Proven: the X-menu uses `HrsBin_Status.mll`, not loose `field/HRSBin.bin`. |
| Identify the party-member selector and next/more page. | Root state `1` creates party selector entries and routes selected party members to state `3`; selector value `5` routes to More menu state `2`, whose entries include options, crew assignment, wanted list, options-description/apply, and back. | Proven. |
| Identify the left/right five-window stat carousel. | `FUN_80183d90` handles controller codes `7` and `6`, wraps root object field `+0x44` through `0..4`, and animates the matching carousel object. The five windows are Attack/Defense/Will/Magic Defense; Hit/Dodge/Quick; weapon/armor equipment; accessory equipment; and EXP/next-EXP. | Proven. |
| Determine per-record mapping for the actual X-menu banks. | Every status-bank record reached by constant `FUN_801d68c4(status_global, record)` calls has a promoted high-confidence structural key. Current audit reports no medium-confidence consumed field-status rows. | Complete for discovered consumers. |
| Avoid misleading editor/exporter keys for records without a discovered consumer. | 29 serialized embedded status records have no discovered accessor in the focused and whole-program `FUN_801d68c4` scans and are listed as preserve-only. | Complete as preserve-only handling. |
| Decide what to do with loose `field/HRSBin.bin`. | It parses as a valid 34-record HRSBin-style layout with likely companion `field/HRSBin.mld`, but static DOL string/decompile scans, indexed-object initializer/wrapper scans, direct `loadFileFromPath` auditing, and US extracted-corpus searches do not find it in the X-menu path or any other consumer. | Not assigned to the X-menu; keep unmapped until runtime evidence proves a consumer. |

### Explicitly Unmapped Serialized Records

These records exist in the serialized `HrsBin_Status.mll` embedded `.bin`
members, but current Ghidra evidence does not find a consumer for them. The
whole-program `FUN_801d68c4` xref scan found no status-bank accessor calls for
these member/record pairs, and the remaining non-accessor status-global xrefs
are limited to the MLL member init/free lifecycle. Keep them out of
machine-facing editor keys until runtime evidence, screenshot correlation, or
new consumer evidence proves a role.

| Member | Record | Elements | Texture slots | Dimensions | Visual summary | Current status |
| --- | --- | ---: | --- | --- | --- | --- |
| `StaCard.bin` | 0x09 | 70 | `0` | 24x48;24x8;48x48;48x8 | Large card/window backing variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x02 | 1 | `0` | 340x72 | Wide panel/header strip variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x03 | 1 | `0` | 340x72 | Wide panel/header strip variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x29 | 1 | `0` | -340x96 | Mirrored wide panel/header strip variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x47 | 1 | `0` | 36x36 | Small square marker/icon. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x56 | 2 | `0` | -32x4;400x4 | Wide rule/frame fragment variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x57 | 4 | `0` | 312x4;52x4;60x32;-8x4 | Wide rule/frame fragment variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x58 | 4 | `0` | 136x4;228x4;60x32;-8x4 | Wide rule/frame fragment variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x59 | 4 | `0` | 144x4;220x4;60x32;-8x4 | Wide rule/frame fragment variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x5a | 4 | `0` | 304x4;60x32;60x4;-8x4 | Wide rule/frame fragment variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x61 | 1 | `0` | 140x22 | Medium horizontal label/strip. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x6c | 4 | `0` | 23x20;24x16;35x11;60x60 | Composite icon/panel variant. | Serialized, no discovered consumer. |
| `StaPaper.bin` | 0x6d | 4 | `0` | 23x20;24x16;35x11;60x60 | Composite icon/panel variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x03 | 8 | `1` | 72x2;8x16;8x2 | Multi-piece horizontal selector/rule variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x34 | 4 | `0,1` | 16x303;4x4 | Tall vertical frame/rule variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x39 | 8 | `1` | 76x2;8x16;8x2 | Multi-piece horizontal selector/rule variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x3d | 1 | `0` | 32x52 | Medium icon/panel fragment. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x3e | 13 | `1` | 16x2;20x2;2x9;39x2;64x2;66x2;8x16 | Large grid/frame line assembly. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x40 | 91 | `1` | 2x71;2x72;95x2;98x2 | Large grid/frame line assembly. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x41 | 30 | `1` | 16x2;18x2;24x2;2x9;8x16 | Large grid/frame line assembly. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x42 | 1 | `1` | 24x24 | Small 24x24 icon variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x43 | 1 | `1` | 24x24 | Small 24x24 icon variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x44 | 1 | `1` | 24x24 | Small 24x24 icon variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x45 | 1 | `1` | 24x24 | Small 24x24 icon variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x48 | 8 | `1` | 51x2;53x2;8x16;8x2 | Multi-piece horizontal selector/rule variant. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x4b | 1 | `1` | 192x24 | Wide horizontal strip. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x53 | 1 | `5` | 108x12 | Texture-slot-5 label/strip. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x54 | 1 | `5` | 107x12 | Texture-slot-5 label/strip. | Serialized, no discovered consumer. |
| `StaSprite00.bin` | 0x57 | 12 | `1` | 16x2;2x9;59x2;65x2;8x16 | Multi-piece horizontal selector/rule variant. | Serialized, no discovered consumer. |

### Direct Record Uses

These rows are selected direct Ghidra record accesses through `FUN_801d68c4`. Some rows now have stable role names from page/controller evidence; rows that still need visual correlation keep provisional wording.

| Member | Record | Machine-facing key | Description | Confidence | Direct consumer functions |
| --- | --- | --- | --- | --- | --- |
| `StaCard.bin` | 0x00 | `status_card_tiled_backing` | Common field X-menu card/page backing tile. `FUN_80195f9c` mutates its destination fields and draws it in a 10x8 grid at 64-pixel steps; the rendered composite is a 64x64 card/backing tile. | high | 80195f9c_FUN_80195f9c |
| `StaPaper.bin` | 0x00 | `status_paper_common_backing` | Common full-screen paper/page backing drawn once by the per-frame pre-dispatch renderer after the tiled card layer; the rendered composite is 640x480. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x00 | `status_common_outer_deco_frame` | Common decorative outer frame drawn by the per-frame pre-dispatch renderer `FUN_80195f9c` after the tiled card/paper backing. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x01 | `status_common_horizontal_rule_set` | Common horizontal red rule/line set drawn by the per-frame pre-dispatch renderer `FUN_80195f9c`. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x02 | `status_common_character_portrait_deco` | Common character portrait decoration record drawn by the per-frame pre-dispatch renderer `FUN_80195f9c`. Exact visible role still needs screenshot correlation, but the record identity is stable. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x03 | `status_common_large_panel_deco_frame` | Common large panel decoration/frame record drawn by the per-frame pre-dispatch renderer `FUN_80195f9c`. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x0e | `status_common_selector_accent_left` | Animated or highlighted selector/accent fragment. `FUN_80195f9c` temporarily changes its color and draws it twice with a one-pixel offset. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x0f | `status_common_selector_accent_long` | Animated or highlighted selector/accent fragment. `FUN_80195f9c` temporarily changes both color fields and draws it twice with a one-pixel offset. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x10 | `status_common_selector_accent_scanline` | Animated selector/accent scanline fragment. `FUN_80195f9c` moves it from `FLOAT_80347118`, reverses direction at bounds, then draws it twice with a one-pixel offset. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x11 | `status_common_selector_accent_right` | Animated or highlighted selector/accent fragment. `FUN_80195f9c` temporarily changes its color and draws it twice with a one-pixel offset. | high | 80195f9c_FUN_80195f9c |
| `StaSprite00.bin` | 0x0f | `status_sprite_common_overlay` | Common sprite/frame overlay drawn once by the per-frame pre-dispatch renderer after the common decoration records; rendered dimensions show full-height side strips plus a small center mark. | high | 80195f9c_FUN_80195f9c |
| `StaDeco.bin` | 0x04 | `status_numeric_digit_wide_0` | Wide digit glyph `0` for the shared status numeric renderer. `FUN_8019a374` decomposes signed integers into decimal digits; `FUN_8019aa18` selects this bank when its style parameter is `1` and steps digits by 14 px. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x05 | `status_numeric_digit_wide_1` | Wide digit glyph `1` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x06 | `status_numeric_digit_wide_2` | Wide digit glyph `2` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x07 | `status_numeric_digit_wide_3` | Wide digit glyph `3` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x08 | `status_numeric_digit_wide_4` | Wide digit glyph `4` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x09 | `status_numeric_digit_wide_5` | Wide digit glyph `5` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x0a | `status_numeric_digit_wide_6` | Wide digit glyph `6` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x0b | `status_numeric_digit_wide_7` | Wide digit glyph `7` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x0c | `status_numeric_digit_wide_8` | Wide digit glyph `8` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaDeco.bin` | 0x0d | `status_numeric_digit_wide_9` | Wide digit glyph `9` for the shared status numeric renderer. | high | 8019ab94_FUN_8019ab94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x05 | `status_numeric_digit_compact_0` | Compact digit glyph `0` for the shared status numeric renderer. `FUN_8019a374` decomposes signed integers into decimal digits; `FUN_8019aa18` selects this bank when its style parameter is `0` or default and steps digits by 12 px. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x06 | `status_numeric_digit_compact_1` | Compact digit glyph `1` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x07 | `status_numeric_digit_compact_2` | Compact digit glyph `2` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x08 | `status_numeric_digit_compact_3` | Compact digit glyph `3` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x09 | `status_numeric_digit_compact_4` | Compact digit glyph `4` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x0a | `status_numeric_digit_compact_5` | Compact digit glyph `5` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x0b | `status_numeric_digit_compact_6` | Compact digit glyph `6` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x0c | `status_numeric_digit_compact_7` | Compact digit glyph `7` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x0d | `status_numeric_digit_compact_8` | Compact digit glyph `8` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaSprite00.bin` | 0x0e | `status_numeric_digit_compact_9` | Compact digit glyph `9` for the shared status numeric renderer. | high | 8019ac94_FUN_8019ac94<br>8019aa18_FUN_8019aa18<br>8019a374_FUN_8019a374 |
| `StaPaper.bin` | 0x2b | `status_character_moon_marker_vyse` | Character-indexed 24x24 marker for Vyse. `FUN_80171b64` selects this for character index `0`; `FUN_8019520c` tints it from Vyse's current `Weapon_Element`. | high | 80171b64_FUN_80171b64<br>80186f58_FUN_80186f58 |
| `StaPaper.bin` | 0x2c | `status_character_moon_marker_aika` | Character-indexed 24x24 marker for Aika. `FUN_80171b64` selects this for character index `1`; `FUN_8019520c` tints it from Aika's current `Weapon_Element`. | high | 80171b64_FUN_80171b64<br>80186f58_FUN_80186f58 |
| `StaPaper.bin` | 0x2d | `status_character_moon_marker_fina` | Character-indexed 24x24 marker for Fina. `FUN_80171b64` selects this for character index `2`; `FUN_8019520c` tints it from Fina's current `Weapon_Element`. | high | 80171b64_FUN_80171b64<br>80186f58_FUN_80186f58 |
| `StaPaper.bin` | 0x2e | `status_character_moon_marker_drachma` | Character-indexed 24x24 marker for Drachma. `FUN_80171b64` selects this for character index `3`; `FUN_8019520c` tints it from Drachma's current `Weapon_Element`. | high | 80171b64_FUN_80171b64<br>80186f58_FUN_80186f58 |
| `StaPaper.bin` | 0x2f | `status_character_moon_marker_enrique` | Character-indexed 24x24 marker for Enrique. `FUN_80171b64` selects this for character index `4`; `FUN_8019520c` tints it from Enrique's current `Weapon_Element`. | high | 80171b64_FUN_80171b64<br>80186f58_FUN_80186f58 |
| `StaPaper.bin` | 0x30 | `status_character_moon_marker_gilder` | Character-indexed 24x24 marker for Gilder. `FUN_80171b64` selects this for character index `5`; `FUN_8019520c` tints it from Gilder's current `Weapon_Element`. | high | 80171b64_FUN_80171b64<br>80186f58_FUN_80186f58 |
| `StaPaper.bin` | 0x43 | `status_list_navigation_marker_right` | Small red vertical right-side list-navigation marker drawn when the active list has multiple entries. Used by magic page state `5`, item page state `7`, S-Move/Moonberries page state `10`, and options/settings state `16`. | high | 801739b8_FUN_801739b8<br>80177134_FUN_80177134<br>801784d8_FUN_801784d8<br>8017f840_FUN_8017f840<br>80188098_FUN_80188098 |
| `StaPaper.bin` | 0x44 | `status_list_navigation_marker_left` | Small red vertical left-side list-navigation marker drawn when the active list has multiple entries. Used by magic page state `5`, item page state `7`, S-Move/Moonberries page state `10`, and options/settings state `16`. | high | 801739b8_FUN_801739b8<br>80177134_FUN_80177134<br>801784d8_FUN_801784d8<br>8017f840_FUN_8017f840<br>80188098_FUN_80188098 |
| `StaPaper.bin` | 0x45 | `status_pair_switch_marker_left` | Small red square left-side marker drawn when multiple party members, crew-pair entries, or option choices are available. Used by equipment page state `8`, magic/S-Move page helpers, crew assignment state `14`, and options/settings state `17`. | high | 8016fb2c_FUN_8016fb2c<br>80171c80_FUN_80171c80<br>80174518_FUN_80174518<br>801784d8_FUN_801784d8<br>80179ba4_FUN_80179ba4<br>8017f840_FUN_8017f840 |
| `StaPaper.bin` | 0x46 | `status_pair_switch_marker_right` | Small red square right-side marker drawn when multiple party members, crew-pair entries, or option choices are available. Used by equipment page state `8`, magic/S-Move page helpers, crew assignment state `14`, and options/settings state `17`. | high | 8016fb2c_FUN_8016fb2c<br>80171c80_FUN_80171c80<br>80174518_FUN_80174518<br>801784d8_FUN_801784d8<br>80179ba4_FUN_80179ba4<br>8017f840_FUN_8017f840 |
| `StaPaper.bin` | 0x48 | `status_shared_vyse_roster_icon_moonberries_choice_panel` | Shared record: selected for Vyse by `FUN_8018796c` in the party-roster/character selector, and also installed by `FUN_8016f21c` for the two Moonberries choice-panel layout ids `0x12/0x13`. | high | 8018796c_FUN_8018796c<br>80198a0c_FUN_80198a0c<br>8017f9d8_FUN_8017f9d8 |
| `StaPaper.bin` | 0x49 | `status_party_roster_icon_aika` | Party-roster/character selector icon selected for Aika by `FUN_8018796c` when the corresponding character availability bit is set. | high | 8018796c_FUN_8018796c |
| `StaPaper.bin` | 0x4a | `status_party_roster_icon_fina` | Party-roster/character selector icon selected for Fina by `FUN_8018796c` when the corresponding character availability bit is set. | high | 8018796c_FUN_8018796c |
| `StaPaper.bin` | 0x4b | `status_party_roster_icon_drachma` | Party-roster/character selector icon selected for Drachma by `FUN_8018796c` when the corresponding character availability bit is set. | high | 8018796c_FUN_8018796c |
| `StaPaper.bin` | 0x4c | `status_party_roster_icon_enrique` | Party-roster/character selector icon selected for Enrique by `FUN_8018796c` when the corresponding character availability bit is set. | high | 8018796c_FUN_8018796c |
| `StaPaper.bin` | 0x4d | `status_party_roster_icon_gilder` | Party-roster/character selector icon selected for Gilder by `FUN_8018796c` when the corresponding character availability bit is set. | high | 8018796c_FUN_8018796c |
| `StaPaper.bin` | 0x4e | `status_party_roster_moon_overlay` | Overlay drawn with each party-roster icon; `FUN_8018796c` tints it from that character's current `Weapon_Element`. | high | 8018796c_FUN_8018796c |
| `StaPaper.bin` | 0x5e | `status_secondary_detail_selection_highlight_base` | First layer of the animated selection/highlight overlay drawn over secondary status/detail panels. `FUN_80194f88` positions it from the active child object's `x/y`, and callers include the secondary-detail render branch in `FUN_8018a16c`. | high | 80194f88_FUN_80194f88 |
| `StaPaper.bin` | 0x5f | `status_secondary_detail_selection_highlight_scanline` | Animated 4 px strip for the secondary status/detail selection overlay. `FUN_80194f88` advances `DAT_8034711e`, derives a 0..12 scanline offset, and draws this record only while that offset is below `0x0d`. | high | 80194f88_FUN_80194f88 |
| `StaPaper.bin` | 0x60 | `status_secondary_detail_selection_highlight_overlay` | Final layer of the animated selection/highlight overlay drawn over secondary status/detail panels after the base and optional scanline layer. | high | 80194f88_FUN_80194f88 |
| `StaPaper.bin` | 0x62 | `status_subpage_navigation_marker_left` | 24x24 left-side navigation marker used by the page hub, item/magic/S-Move list helpers, and ship equipment chooser states when another page/list choice is available. | high | 8016fb2c_FUN_8016fb2c<br>80175dc4_FUN_80175dc4<br>80177134_FUN_80177134<br>8017bbc8_FUN_8017bbc8<br>8017cc70_FUN_8017cc70<br>8017e4e4_FUN_8017e4e4<br>80183c54_FUN_80183c54 |
| `StaPaper.bin` | 0x63 | `status_subpage_navigation_marker_right` | 24x24 right-side navigation marker paired with `0x62`, used by the page hub, item/magic/S-Move list helpers, and ship equipment chooser states when another page/list choice is available. | high | 8016fb2c_FUN_8016fb2c<br>80175dc4_FUN_80175dc4<br>80177134_FUN_80177134<br>8017bbc8_FUN_8017bbc8<br>8017cc70_FUN_8017cc70<br>8017e4e4_FUN_8017e4e4<br>80183c54_FUN_80183c54 |
| `StaSprite00.bin` | 0x49 | `status_options_description_bar` | Long red horizontal description/selection bar drawn by state `18`. The option table around `0x802e990c` / `0x802e9918` identifies rows Sound (`Stereo` / `Mono`), Camera (`Normal` / `Reverse`), Rumble (`On` / `Off`), and VMUsound (`On` / `Off`); state `18` shows row descriptions and dispatches actions through `PTR_FUN_802e9428`. | high | 8017a040_FUN_8017a040 |
| `StaCard.bin` | 0x04 | `status_shared_description_text_panel_card_backing` | Shared description/help text panel card backing. Constructor `FUN_80189cf0` installs this with `StaPaper 0x27`; callback `FUN_80189b70` renders the string pointer stored through `FUN_801954d0`. | high | 80189cf0_FUN_80189cf0<br>80189b70_FUN_80189b70<br>801954d0_FUN_801954d0 |
| `StaPaper.bin` | 0x27 | `status_shared_description_text_panel_frame` | Shared description/help text panel frame paired with `StaCard 0x04`. Used by the `DAT_80307118` child object when state helpers update the current description text. | high | 80189cf0_FUN_80189cf0<br>80189b70_FUN_80189b70<br>801954d0_FUN_801954d0 |
| `StaCard.bin` | 0x05 | `status_equipment_item_detail_row_card_backing` | Equipment item/detail row card backing. Constructor `FUN_80187d78` is used for equipped weapon/armor/accessory rows, the initial weapon detail row, and the first ship-equipment chooser row. | high | 80187d78_FUN_80187d78<br>80196c10_FUN_80196c10<br>801974b4_FUN_801974b4<br>80197654_FUN_80197654 |
| `StaPaper.bin` | 0x28 | `status_equipment_item_detail_row_frame` | Equipment item/detail row frame retained by `FUN_80187d78` for the equipment row object family. | high | 80187d78_FUN_80187d78 |
| `StaPaper.bin` | 0x2a | `status_equipment_item_detail_row_divider_lines` | Divider-line record installed into the equipment item/detail row payload. | high | 80187d78_FUN_80187d78 |
| `StaSprite00.bin` | 0x37 | `status_equipment_item_detail_row_slot_marker` | Slot marker/connector sprite installed into the equipment item/detail row payload. | high | 80187d78_FUN_80187d78 |
| `StaCard.bin` | 0x08 | `status_crew_assignment_grid_card_backing` | Crew-assignment grid card backing. Built by `FUN_80186d2c` for object `DAT_803071a0`, which the crew assignment states use for the 22-crew / 11-row assignment grid. | high | 80186d2c_FUN_80186d2c<br>80186a60_FUN_80186a60<br>801988f8_FUN_801988f8 |
| `StaPaper.bin` | 0x52 | `status_crew_assignment_grid_left_edge_marker` | Left-edge/side marker retained by the crew-assignment grid payload. | high | 80186d2c_FUN_80186d2c |
| `StaPaper.bin` | 0x55 | `status_crew_assignment_grid_frame` | Crew-assignment grid frame record installed with `StaCard 0x08` and `StaSprite00 0x3f`. | high | 80186d2c_FUN_80186d2c |
| `StaSprite00.bin` | 0x3f | `status_crew_assignment_grid_row_lines` | Multi-row grid-line sprite for the crew-assignment panel. Callback `FUN_80186a60` draws the grid and supporting line primitives for the crew assignment page. | high | 80186d2c_FUN_80186d2c<br>80186a60_FUN_80186a60 |
| `StaCard.bin` | 0x0a | `status_options_setting_row_card_backing` | Options/settings row card backing. `FUN_80197fcc` creates four row objects at `DAT_803071dc..803071e8`; state `18` uses them for the options description/apply list. | high | 8018a07c_FUN_8018a07c<br>80189dbc_FUN_80189dbc<br>80189ea0_FUN_80189ea0<br>80197fcc_FUN_80197fcc |
| `StaPaper.bin` | 0x5b | `status_options_setting_row_frame` | Options/settings row frame retained by `FUN_8018a07c`; renderer `FUN_80189ea0` draws option labels and value choices from the options table. | high | 8018a07c_FUN_8018a07c<br>80189ea0_FUN_80189ea0 |
| `StaPaper.bin` | 0x5c | `status_options_setting_row_divider_lines` | Options/settings row divider-line record installed into each options setting row payload. | high | 8018a07c_FUN_8018a07c<br>80189ea0_FUN_80189ea0 |
| `StaCard.bin` | 0x01 | `status_selector_entry_card_backing` | Shared tiled card backing for root party-selector entries, primary/secondary page menu entries, and the ship-equipment root entry. Constructors `FUN_8016e96c`, `FUN_8016ea60`, `FUN_8016ec3c`, `FUN_8016f21c`, and `FUN_80191d78` install this as the selector-entry card layer. | high | 8016e96c_FUN_8016e96c<br>8016ea60_FUN_8016ea60<br>8016ec3c_FUN_8016ec3c<br>8016f21c_FUN_8016f21c<br>80191d78_FUN_80191d78 |
| `StaPaper.bin` | 0x07 | `status_root_party_selector_entry_frame` | Shared long paper/frame strip for root party-selector member entries. `FUN_801999b4` creates one object per available party member and `FUN_8016ec3c` installs this frame before the character-specific selector marker. | high | 8016e96c_FUN_8016e96c<br>8016ea60_FUN_8016ea60<br>8016ec3c_FUN_8016ec3c |
| `StaSprite00.bin` | 0x00 | `status_root_party_selector_entry_accent_strip` | Shared thin sprite/accent strip for root party-selector member entries. Installed by `FUN_8016e96c`, `FUN_8016ea60`, and `FUN_8016ec3c` alongside `StaPaper 0x07` and `StaCard 0x01`. | high | 8016e96c_FUN_8016e96c<br>8016ea60_FUN_8016ea60<br>8016ec3c_FUN_8016ec3c |
| `StaPaper.bin` | 0x0b | `status_root_party_selector_character_vyse_marker` | Character-specific selector marker for Vyse. `FUN_8016ec3c` selects this when the character id argument is `0`; `FUN_801999b4` creates root selector entries from party composition and routes selected members to the primary page hub. | high | 8016ec3c_FUN_8016ec3c |
| `StaPaper.bin` | 0x0c | `status_root_party_selector_character_aika_marker` | Character-specific selector marker for Aika. `FUN_8016ec3c` selects this when the character id argument is `1`; `FUN_801999b4` creates root selector entries from party composition and routes selected members to the primary page hub. | high | 8016ec3c_FUN_8016ec3c |
| `StaPaper.bin` | 0x0d | `status_root_party_selector_character_fina_marker` | Character-specific selector marker for Fina. `FUN_8016ec3c` selects this when the character id argument is `2`; `FUN_801999b4` creates root selector entries from party composition and routes selected members to the primary page hub. | high | 8016ec3c_FUN_8016ec3c |
| `StaPaper.bin` | 0x0e | `status_root_party_selector_character_drachma_marker` | Character-specific selector marker for Drachma. `FUN_8016ec3c` selects this when the character id argument is `3`; `FUN_801999b4` creates root selector entries from party composition and routes selected members to the primary page hub. | high | 8016ec3c_FUN_8016ec3c |
| `StaPaper.bin` | 0x0f | `status_root_party_selector_character_enrique_marker` | Character-specific selector marker for Enrique. `FUN_8016ec3c` selects this when the character id argument is `4`; `FUN_801999b4` creates root selector entries from party composition and routes selected members to the primary page hub. | high | 8016ec3c_FUN_8016ec3c |
| `StaPaper.bin` | 0x10 | `status_root_party_selector_character_gilder_marker` | Character-specific selector marker for Gilder. `FUN_8016ec3c` selects this when the character id argument is `5`; `FUN_801999b4` creates root selector entries from party composition and routes selected members to the primary page hub. | high | 8016ec3c_FUN_8016ec3c |
| `StaPaper.bin` | 0x08 | `status_root_ship_equipment_entry_frame` | Root-page ship-equipment selector entry frame. `FUN_801998b8` creates object `0x04`; `FUN_80191d78` installs this frame and `StaPaper 0x1e`; state `1` routes selected value `4` to ship equipment chooser state `11` when the ship-equipment gate is enabled. | high | 80191d78_FUN_80191d78 |
| `StaPaper.bin` | 0x1e | `status_root_ship_equipment_entry_marker` | Root-page ship-equipment selector entry marker paired with `StaPaper 0x08` by `FUN_80191d78`. State `1` routes selected value `4` to ship equipment chooser state `11` when the ship-equipment gate is enabled. | high | 80191d78_FUN_80191d78 |
| `StaSprite00.bin` | 0x02 | `status_root_ship_equipment_entry_accent_strip` | Thin sprite/accent strip for the root-page ship-equipment selector entry. Installed by `FUN_80191d78` through `FUN_80199ffc` before the `StaCard 0x01`, `StaPaper 0x08`, and `StaPaper 0x1e` layers. | high | 80191d78_FUN_80191d78 |
| `StaSprite00.bin` | 0x01 | `status_layout_entry_accent_strip` | Thin sprite/accent strip for `FUN_8016f21c` layout-id entries. This is shared across primary page entries, More/back entries, options choice entries, ship category entries, and Moonberries confirmation entries. | high | 8016f21c_FUN_8016f21c |
| `StaSprite00.bin` | 0x2b | `status_shared_green_moon_sprite_marker` | Shared green moon sprite marker. `FUN_8018b278` installs it for secondary magic-rank detail slot `0`, while `FUN_80188274` pairs it with the green paper marker for options/toggle helper row `0`. | high | 80188274_FUN_80188274<br>8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x2c | `status_shared_red_moon_sprite_marker` | Shared red moon sprite marker. `FUN_8018b278` installs it for secondary magic-rank detail slot `1`, while `FUN_80188274` pairs it with the red paper marker for options/toggle helper row `1`. | high | 80188274_FUN_80188274<br>8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x2d | `status_shared_blue_moon_sprite_marker` | Shared blue moon sprite marker. `FUN_8018b278` installs it for secondary magic-rank detail slot `3`, while `FUN_80188274` pairs it with the blue paper marker for options/toggle helper row `3`. | high | 80188274_FUN_80188274<br>8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x2e | `status_shared_purple_moon_sprite_marker` | Shared purple moon sprite marker. `FUN_8018b278` installs it for secondary magic-rank detail slot `2`, while `FUN_80188274` pairs it with the purple paper marker for options/toggle helper row `2`. | high | 80188274_FUN_80188274<br>8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x2f | `status_shared_yellow_moon_sprite_marker` | Shared yellow moon sprite marker. `FUN_8018b278` installs it for secondary magic-rank detail slot `4`, while `FUN_80188274` pairs it with the yellow paper marker for options/toggle helper row `4`. | high | 80188274_FUN_80188274<br>8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x30 | `status_shared_silver_moon_sprite_marker` | Shared silver moon sprite marker. `FUN_8018b278` installs it for secondary magic-rank detail slot `5`, while `FUN_80188274` pairs it with the silver paper marker for options/toggle helper row `5`. | high | 80188274_FUN_80188274<br>8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x31 | `status_secondary_magic_rank_shared_panel_strip_a` | Shared sprite fragment installed into every secondary magic-rank detail panel by `FUN_8018b278`. Kept structural because the crop is a shared panel strip, not a confirmed standalone text label. | high | 8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x32 | `status_secondary_magic_rank_shared_panel_strip_b` | Shared sprite fragment installed into every secondary magic-rank detail panel by `FUN_8018b278`. Kept structural because the crop is a shared panel strip, not a confirmed standalone text label. | high | 8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x33 | `status_secondary_magic_rank_shared_panel_strip_c` | Shared sprite fragment installed into every secondary magic-rank detail panel by `FUN_8018b278`. Kept structural because the crop is a shared panel strip, not a confirmed standalone text label. | high | 8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x2a | `status_secondary_magic_rank_shared_horizontal_marker` | Shared horizontal marker/guide sprite installed into every secondary magic-rank detail panel by `FUN_8018b278` after the per-element marker and three shared panel strips. | high | 8018b278_FUN_8018b278 |
| `StaSprite00.bin` | 0x35 | `status_secondary_detail_stat_grid_corner_accent` | Small accent installed by `FUN_8018c6a8` with the secondary-detail stat-grid payload. The same constructor installs stat label/icon records `0x10..0x16` and is created by `FUN_80196f58` for secondary detail group `0` slots `2..3`. | high | 8018c6a8_FUN_8018c6a8<br>80196f58_FUN_80196f58 |
| `StaSprite00.bin` | 0x56 | `status_stat_max_spirit_companion_marker` | Companion marker for the Max Spirit value block. `FUN_8018eb3c` draws Spirit, Max Spirit, and the related numeric values, then draws this marker beside the Max Spirit-side value area. | high | 8018eb3c_FUN_8018eb3c |
| `StaSprite00.bin` | 0x3a | `status_selector_variant_3_accent_strip` | Selector/control accent strip installed by `FUN_8016e878`, which is reached only through `FUN_8016e818(..., 3)`. `FUN_80196b08` creates object `0x32`, stores selector variant `3`, and installs this strip with `StaCard 0x02`, `StaPaper 0x0a`, and `StaPaper 0x04`. | high | 8016e878_FUN_8016e878<br>8016e818_FUN_8016e818<br>80196b08_FUN_80196b08 |
| `StaSprite00.bin` | 0x3c | `status_detail_ship_weapon_stat_extra_label_icon` | Fourth stat label/icon in the ship-weapon detail renderer. `FUN_8019082c` renders an item/ship-weapon name, then draws `StaSprite00 0x10`, `0x14`, `0x19`, and this record at fixed x offsets; the same dispatcher cases use it for ship weapon/detail views selected through `FUN_8018e82c`. Exact visible label still needs screenshot correlation. | high | 8019082c_FUN_8019082c<br>8018e82c_FUN_8018e82c |
| `StaSprite00.bin` | 0x4d | `status_selector_group_default_accent_strip` | Accent strip for `FUN_80197838` selector entries `a`, `b`, `c`, `d`, and `e`. The shared constructor installs this record through `FUN_8018873c` when the entry id is not `f` or `g`; callback `FUN_8018858c` positions these default-group entries from the corresponding selector globals. | high | 80197838_FUN_80197838<br>8018873c_FUN_8018873c<br>8018858c_FUN_8018858c |
| `StaSprite00.bin` | 0x4c | `status_selector_group_f_accent_strip` | Accent strip for `FUN_80197838` selector entry `f`. The constructor selects callback `FUN_801867b8` and installs this record through `FUN_80186908`; `FUN_801968e8` creates the entry after `a`, `b`, `d`, `c`, and `e`. | high | 80197838_FUN_80197838<br>80186908_FUN_80186908<br>801867b8_FUN_801867b8 |
| `StaSprite00.bin` | 0x4a | `status_selector_group_g_accent_strip` | Accent strip for `FUN_80197838` selector entry `g`. The constructor selects callback `FUN_8018dd24` and installs this record through `FUN_8018defc`; `FUN_801968e8` creates it after selector entry `f`. | high | 80197838_FUN_80197838<br>8018defc_FUN_8018defc<br>8018dd24_FUN_8018dd24 |
| `StaCard.bin` | 0x06 | `status_wanted_poster_viewer_panel_card_backing_a` | Wanted-list/poster viewer panel backing variant. State `19` loads `field/wanted_%02d{a,b}.mld`, calls `FUN_801863e8`, and the call graph reaches `FUN_800c2a88`, which draws this backing with `StaPaper 0x50`, `0x53`, `0x70`, and `StaSprite00 0x46`. | high | 800c2a88_FUN_800c2a88<br>80185c00_FUN_80185c00 |
| `StaCard.bin` | 0x07 | `status_wanted_poster_viewer_panel_card_backing_b` | Wanted-list/poster viewer panel backing variant shared by `FUN_800c2650` and `FUN_800c287c`, both reached from `FUN_8018e73c` in the state-19 wanted viewer route. | high | 800c2650_FUN_800c2650<br>800c287c_FUN_800c287c<br>80185c00_FUN_80185c00 |
| `StaPaper.bin` | 0x50 | `status_wanted_poster_viewer_panel_frame_a` | Wanted viewer panel/frame record drawn by `FUN_800c2a88` before card backing variant `StaCard 0x06`. | high | 800c2a88_FUN_800c2a88<br>80185c00_FUN_80185c00 |
| `StaPaper.bin` | 0x51 | `status_wanted_poster_viewer_panel_frame_b` | Wanted viewer panel/frame record drawn by `FUN_800c2650` and `FUN_800c287c` before card backing variant `StaCard 0x07`. | high | 800c2650_FUN_800c2650<br>800c287c_FUN_800c287c<br>80185c00_FUN_80185c00 |
| `StaPaper.bin` | 0x53 | `status_wanted_poster_viewer_panel_rule_a` | Wanted viewer panel/rule record drawn by `FUN_800c2a88` after card backing variant `StaCard 0x06`. | high | 800c2a88_FUN_800c2a88<br>80185c00_FUN_80185c00 |
| `StaPaper.bin` | 0x54 | `status_wanted_poster_viewer_panel_rule_b` | Wanted viewer panel/rule record drawn by `FUN_800c2650` and `FUN_800c287c` after card backing variant `StaCard 0x07`. | high | 800c2650_FUN_800c2650<br>800c287c_FUN_800c287c<br>80185c00_FUN_80185c00 |
| `StaPaper.bin` | 0x70 | `status_wanted_poster_viewer_small_marker` | Small wanted viewer marker drawn by `FUN_800c2a88` with a fixed offset from the panel origin. | high | 800c2a88_FUN_800c2a88<br>80185c00_FUN_80185c00 |
| `StaSprite00.bin` | 0x46 | `status_wanted_poster_viewer_rule_sheet_a` | Wanted viewer rule/list-line sprite drawn by `FUN_800c2a88` with the `StaCard 0x06` / `StaPaper 0x50` / `0x53` variant. | high | 800c2a88_FUN_800c2a88<br>80185c00_FUN_80185c00 |
| `StaSprite00.bin` | 0x47 | `status_wanted_poster_viewer_rule_sheet_b` | Wanted viewer poster/list frame and rule-line sprite drawn by `FUN_800c2650` and `FUN_800c287c` with the `StaCard 0x07` / `StaPaper 0x51` / `0x54` variant. | high | 800c2650_FUN_800c2650<br>800c287c_FUN_800c287c<br>80185c00_FUN_80185c00 |

### Page State Identity Notes

Expanded helper dumps in `ghidra_export/hrs_status_page_identity_helpers/`, `ghidra_export/hrs_status_shared_helpers/`, `ghidra_export/hrs_status_list_producers/`, and `ghidra_export/hrs_status_character_helpers/` identify the major page states and character-indexed marker behavior behind the left/right field X-menu loop.

| State | Current role | Evidence summary | Confidence |
| ---: | --- | --- | --- |
| `3` | Party selector / page hub | Dispatches selected slot values to states `7`, `5`, `10`, and `8`; helper `FUN_8016fd10` moves across party composition and visible slot controls. | high |
| `5` | Magic page | Uses shared list producer mode `1`, which calls `FUN_801f224c`; that producer scans `PLAYER_ABILITIES`, `tblMagicDescriptions`, MP, ability flags, and ability elements. | high |
| `6` | Item/magic use target/confirmation hub | Uses `USABLE_ITEM_DEFINITIONS` and `tblShipMagicDescriptions`, then returns into item, magic, S-Move, or equipment page states by `DAT_803077a4`. | high |
| `7` | Item/inventory page | Uses shared list producer mode `0`, which calls `FUN_801f1774`; that producer enumerates inventory sections `0..7` and filters by equipped/stat/usable inventory data. | high |
| `8` | Equipment page | Directly reads `Equipped_Weapon`, `Equipped_Armor`, and `Equipped_Accessory`; resolves item names through `FUN_801f48ac` and displays them through `FUN_801954d0`. | high |
| `10` | S-Move / Moonberries page | Uses shared list producer mode `2`, which calls `FUN_801f2518`; that producer scans ability ids `0x24..0x3d`, checks ability flags, uses Moonberries and Moonberries Left strings, and updates `Moonberries_used`. | high |
| `11` / `12` | Ship equipment slot chooser | Uses shared producer mode `4`, which aliases to `FUN_801f1280`. The item-list producer is called with category `DAT_80307b84 + 6`, resolving category `0` to ship battle weapons and category `1` to ship battle accessories. The selected ids are resolved through `FUN_801f48ac`, and state helpers draw `StaPaper 0x62/0x63` as chooser navigation markers. Exact user-visible page label still pending. | high |
| `13` / `14` | Crew assignment page | Draws `StaPaper 0x45/0x46`, reads crew-assignment globals `DAT_803071a0`, `DAT_803071ac`, `DAT_803071a4`, `DAT_803071b0`, and `DAT_803071a8`, builds a 22-crew / 11-row grid with `getCrewBit`, and applies changes through `FUN_80194f3c` -> `FUN_8021b2b4`. | high |
| `15` / `16` / `17` | Options/settings toggle flow | State `16` draws `StaPaper 0x43/0x44`; state `17` uses `DAT_80307d44` through helper selectors `FUN_8019438c`, `FUN_80194428`, `FUN_8019445c`, and `FUN_80194490`. The action table identifies Sound as `DAT_80347144` plus `OSSetSoundMode`, Camera as `FLAGS_80310bc4` bit `0x08`, Rumble as `FLAGS_80310bc4` bit `0x10`, and VMUsound as `FLAGS_80310bc4` bit `0x20`. | high |
| `18` | Options/settings description and apply list | Draws `StaSprite00 0x49`, selects text from the four-row options-description table, and invokes selected option actions through `PTR_FUN_802e9428` for Sound, Camera, Rumble, and VMUsound. | high |

### Page-State Record Cluster Evidence

Generated cluster table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/page_state_record_cluster_summary.tsv`

The rows below connect the known page-state callbacks to directly reached record-consuming helpers. These are stronger than visual grouping, but still stop short of assigning final editor labels to every child-control artwork record.

For the ship equipment slot chooser, `DAT_8030783c` switches between two category/list modes. Category `0` drives a five-entry ship weapon slot list whose selected ids are read from `DAT_80306f8c + selected_row * 2 + 0x18`; category `1` drives a three-entry ship accessory slot list whose selected ids are read from `DAT_80306f8c + selected_row * 2 + 0x22`. The state `12` item list uses `DAT_80307b84 + 6`, so these categories resolve to ship battle weapons and ship battle accessories. This is high-confidence route structure, but the visible category tab labels remain pending.

| State/context | Role | Record-consuming helper | Record cluster | Confidence |
| --- | --- | --- | --- | --- |
| `3` | Party selector / page hub | `FUN_80171b64` | `StaPaper 0x2b..0x30` character moon markers. | high |
| `3` | Party selector / page hub | `FUN_8016fb2c` | `StaPaper 0x45/0x46` left/right pair-switch markers and `0x62/0x63` subpage navigation markers. | high |
| `5` | Magic page | `FUN_801784d8` | `StaPaper 0x43/0x44` left/right list navigation markers and `0x45/0x46` left/right pair-switch markers. | high |
| `7` | Item/inventory page | `FUN_80177134` | `StaPaper 0x43/0x44` left/right list navigation markers and `0x62/0x63` subpage navigation markers. | high |
| `8` | Equipment page | `FUN_80174518` | `StaPaper 0x45/0x46` left/right pair-switch markers. | high |
| `10` | S-Move / Moonberries page | `FUN_8017f840` | `StaPaper 0x43/0x44` left/right list navigation markers and `0x45/0x46` left/right pair-switch markers. | high |
| `11` / `12` | Ship equipment slot chooser | `FUN_8017cc70` / `FUN_8017bbc8` | `StaPaper 0x62/0x63` subpage navigation markers. | high |
| `14` | Crew assignment apply/confirmation flow | `FUN_80171c80` | `StaPaper 0x45/0x46` left/right crew-pair switch markers. | high |
| `16` | Options/settings toggle flow | `FUN_801739b8` | `StaPaper 0x43/0x44` left/right list navigation markers. | high |
| `17` | Options/settings apply/confirmation flow | `FUN_80179ba4` | `StaPaper 0x45/0x46` left/right option switch markers. | high |
| `18` | Options/settings description and apply list | `FUN_8017a040` | `StaSprite00 0x49` options description bar. | high |

### Root Selector and Second-Page Keys

Generated route table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/root_and_second_page_route_map.tsv`

`FUN_801999b4`, `FUN_80199024`, `FUN_801998b8`, `FUN_80199678`, `FUN_80183d90`, `FUN_801841dc`, `FUN_801851c8`, and `FUN_801852f8` now support high-confidence machine-facing keys for the initial root selector and its More menu page. These keys describe runtime object routes and controller states. The five root carousel columns are now identified as Attack/Defense/Will/Magic Defense, Hit/Dodge/Quick, weapon/armor equipment, accessory equipment, and EXP/next-EXP.

| Selector context | Selector value / field | Machine key range | Object ids | `DAT_80306f88` indexes | Target state / role | Confidence |
| --- | --- | --- | --- | --- | --- | --- |
| Initial root state `1` | `0..3` | `status_root_party_selector_member_00..03` | `0x00..0x03` | `0x04..0x07` | Party member entries into page hub state `3`. | high |
| Initial root state `1` | `4` | `status_root_ship_equipment_entry` | `0x04` | `0x08` | Ship equipment slot chooser state `11`, gated by `FUN_801953b8()`. | high |
| Initial root state `1` | `5` | `status_root_more_menu_entry` | `0x05` | `0x09` | More menu state `2`, gated by `FUN_80194cb8()==1`. | high |
| Initial root state `1` | carousel field `+0x44`, member `0`, windows `0..4` | `status_root_party_carousel_member_00_window_00_attack_defense_will_magic_defense` through `status_root_party_carousel_member_00_window_04_experience` | `0x06..0x0a` | `0x0a..0x0e` | Five-window carousel for party member `0`; left/right controller codes `6`/`7` wrap the field across `0..4`. | high |
| Initial root state `1` | carousel field `+0x44`, member `1`, windows `0..4` | `status_root_party_carousel_member_01_window_00_attack_defense_will_magic_defense` through `status_root_party_carousel_member_01_window_04_experience` | `0x0b..0x0f` | `0x0f..0x13` | Five-window carousel for party member `1`; left/right controller codes `6`/`7` wrap the field across `0..4`. | high |
| Initial root state `1` | carousel field `+0x44`, member `2`, windows `0..4` | `status_root_party_carousel_member_02_window_00_attack_defense_will_magic_defense` through `status_root_party_carousel_member_02_window_04_experience` | `0x10..0x14` | `0x14..0x18` | Five-window carousel for party member `2`; left/right controller codes `6`/`7` wrap the field across `0..4`. | high |
| Initial root state `1` | carousel field `+0x44`, member `3`, windows `0..4` | `status_root_party_carousel_member_03_window_00_attack_defense_will_magic_defense` through `status_root_party_carousel_member_03_window_04_experience` | `0x15..0x19` | `0x19..0x1d` | Five-window carousel for party member `3`; left/right controller codes `6`/`7` wrap the field across `0..4`. | high |
| Initial root state `1` | bottom toggle field `+0x14`, slots `0..1` | `status_root_bottom_page_toggle_slot_00..01` | `0x6c..0x6d` | `0x70..0x71` | Bottom page-toggle/current-page controls used when `FUN_801953b8()` is true. | high |
| More menu state `2` | `0` | `status_options_menu_entry` | `0x1a` | `0x1e` | Options/settings entry state `15`. | high |
| More menu state `2` | `1` | `status_crew_assignment_menu_entry` | `0x1b` | `0x1f` | Crew assignment state `13`. | high |
| More menu state `2` | `2` | `status_wanted_list_menu_entry` | `0x1c` | `0x20` | Wanted-list/poster viewer state `19`; loads `/field/wanted.mll` plus `field/wanted_%02d{a,b}.mld`. | high |
| More menu state `2` | `3` | `status_options_description_apply_entry` | `0x1d` | `0x21` | Options/settings description/apply list state `18`. | high |
| More menu state `2` | `4` | `status_more_menu_back_entry` | `0x1e` | `0x22` | Back/return to initial root state `1`; controller codes `0x10`/`0x11` force this selector value. | high |

The root carousel slot payloads are installed by `FUN_8018c00c`, which is called from `FUN_80199024` once for each party member and window slot. Generated payload table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/root_carousel_payload_record_map.tsv`

Controller evidence: `FUN_80183d90` is the initial root selector callback. The
disassembly at `80183e2c..80183e90` handles controller code `7` by saving the
old carousel slot from object field `+0x44` to `+0x48`, incrementing `+0x44`,
and wrapping `5 -> 0`; it also advances the bottom page-toggle field `+0x14`
and wraps `2 -> 0`. The block at `80183e94..80183ef8` handles controller code
`6` by saving `+0x44` to `+0x48`, decrementing `+0x44`, and wrapping `-1 -> 4`;
it also decrements `+0x14` and wraps `-1 -> 1`. The animation block then indexes
`DAT_80306f98[0x06 + member * 5 + oldSlot]` and
`DAT_80306f98[0x06 + member * 5 + newSlot]`, so the five-slot loop below is
directly tied to the visible left/right stat-window carousel.

| Carousel window slot | Machine-facing suffix | Common records | Slot-specific records | Current interpretation | Confidence |
| ---: | --- | --- | --- | --- | --- |
| `0` | `window_00_attack_defense_will_magic_defense` | `StaPaper 0x0a`; `StaCard 0x02`; `StaPaper 0x04` | `StaSprite00 0x10`, `0x1d`, `0x11`, `0x1e`, `0x12`, `0x1f`, `0x13`, `0x20`, `0x04` | Attack, Defense, Will, and Magic Defense status window. Renderer `FUN_8018d250` draws payload offsets `0x08`, `0x10`, `0x18`, and `0x20`; `FUN_8018dabc` fills them from statblock offsets `0x52`, `0x54`, `0x4a`, and `0x56`. | high |
| `1` | `window_01_hit_dodge_quick` | `StaPaper 0x0a`; `StaCard 0x02`; `StaPaper 0x04` | `StaSprite00 0x14`, `0x21`, `0x15`, `0x22`, `0x16`, `0x23`, `0x04` | Hit %, Dodge %, and Quick status window. Renderer `FUN_8018d250` draws payload offsets `0x28`, `0x30`, and `0x38`; `FUN_8018dabc` fills them from statblock offsets `0x58`, `0x5a`, and `0x50`. | high |
| `2` | `window_02_weapon_armor_equipment` | `StaPaper 0x0a`; `StaCard 0x02`; `StaPaper 0x04` | One of `StaPaper 0x2b..0x30` selected by character id and tinted by `FUN_8019520c`; `StaPaper 0x31`; `StaSprite00 0x38` | Equipped weapon and armor window. Renderer `FUN_8018d250` draws payload name pointers `0x4c` and `0x50`. | high |
| `3` | `window_03_accessory_equipment` | `StaPaper 0x0a`; `StaCard 0x02`; `StaPaper 0x04` | `StaPaper 0x33`; `StaSprite00 0x38` | Equipped accessory window. Renderer `FUN_8018d250` draws payload name pointer `0x54`. | high |
| `4` | `window_04_experience` | `StaPaper 0x0a`; `StaCard 0x02`; `StaPaper 0x04` | `StaSprite00 0x36` | Total EXP and next/needed EXP window. Renderer `FUN_8018d250` draws payload offsets `0x58` and `0x5c`; level 99 uses max-level fallback text. | high |

Generated renderer-value table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/root_carousel_rendered_value_map.tsv`

Generated stat-field table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/root_carousel_stat_field_map.tsv`

Child-control constructors expose larger clusters that likely contain the visible stat-window/page artwork, but they still need per-control correlation before one-to-one naming:

| Context | Record-consuming helper | Record cluster | Current interpretation |
| --- | --- | --- | --- |
| Large status-window/page child constructor family | `FUN_8016f21c` | `StaCard 0x01`; `StaPaper 0x01`, `0x06`, `0x09`, `0x11..0x1d`, `0x48`, `0x65..0x67`, `0x6e..0x6f`; `StaSprite00 0x01` | Large page/card cluster used by multiple child constructors. |
| Party roster / character detail child constructor | `FUN_8018796c` | `StaPaper 0x48..0x4e` | Party roster icons and tinted moon overlay. |
| Party roster / character detail child constructor | `FUN_80186f58` | `StaPaper 0x2b..0x31`, `0x33`; `StaSprite00 0x10..0x16` | Character moon markers plus related status sprites. |
| Status/stat detail child constructor family | `FUN_80188a18` | `StaPaper 0x2b..0x42`, `0x4f`, `0x64` | Large status/stat detail record cluster. |
| Status/stat detail child constructor family | `FUN_80189214` | `StaPaper 0x21..0x26`, `0x31..0x39`, `0x3c`, `0x41`, `0x5d` | Stat/detail panel record cluster. |
| Options/settings helper | `FUN_80188274` | `StaPaper 0x34..0x39`; `StaSprite00 0x2b..0x30` | Six-row options/toggle helper cluster. |

`FUN_8016f21c` is now mapped well enough to expose stable machine-facing keys for its `param_3` ids. The generated TSVs are `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/fun_8016f21c_layout_id_map.tsv` and `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/fun_8016f128_selector_routing.tsv`. The inventory, magic, S-Move/Moonberries, equipment, More menu, ship equipment slot chooser, options choice-panel, and Moonberries choice-panel rows have semantic route names. Some of these are still route-level keys rather than final visible labels.

`FUN_8016f128` writes the child-object selector bytes as `{selector_byte0, selector_byte1}` followed by a float scale. For the rows below, `selector_byte1` matches the `FUN_8016f21c` layout id. The child object's stored object id is four less than the equivalent `DAT_80306f88` global-array index because many decompiler expressions are based at `DAT_80306f98`, not the true start of the array.

| Layout id | Machine key stem | Constructor | Object id | `DAT_80306f88` index | Selector bytes | Backing record | Main record | Confidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `0x00` | `status_inventory_menu_entry` | `80198e90` | `0x1f` | `0x23` | `{0x01, 0x00}` | `StaPaper 0x06` | `StaPaper 0x15` | high |
| `0x01` | `status_magic_menu_entry` | `80198e90` | `0x20` | `0x24` | `{0x02, 0x01}` | `StaPaper 0x06` | `StaPaper 0x18` | high |
| `0x02` | `status_smove_moonberries_menu_entry` | `80198e90` | `0x21` | `0x25` | `{0x03, 0x02}` | `StaPaper 0x06` | `StaPaper 0x17` | high |
| `0x03` | `status_equipment_menu_entry` | `80198e90` | `0x22` | `0x26` | `{0x04, 0x03}` | `StaPaper 0x06` | `StaPaper 0x14` | high |
| `0x04` | `status_ship_weapon_slot_menu_entry` | `8019936c` | `0x78` | `0x7c` | `{0x01, 0x04}` | `StaPaper 0x06` | `StaPaper 0x14` | high |
| `0x05` | `status_ship_accessory_slot_menu_entry` | `8019936c` | `0x79` | `0x7d` | `{0x02, 0x05}` | `StaPaper 0x06` | `StaPaper 0x1d` | high |
| `0x06` | `status_options_menu_entry` | `80198cf4` | `0x1a` | `0x1e` | `{0x01, 0x06}` | `StaPaper 0x06` | `StaPaper 0x16` | high |
| `0x07` | `status_crew_assignment_menu_entry` | `80198cf4` | `0x1b` | `0x1f` | `{0x02, 0x07}` | `StaPaper 0x06` | `StaPaper 0x1a` | high |
| `0x08` | `status_wanted_list_menu_entry` | `80198cf4` | `0x1c` | `0x20` | `{0x03, 0x08}` | `StaPaper 0x06` | `StaPaper 0x19` | high |
| `0x09` | `status_options_description_apply_entry` | `80198cf4` | `0x1d` | `0x21` | `{0x04, 0x09}` | `StaPaper 0x06` | `StaPaper 0x13` | high |
| `0x0a` | `status_root_more_menu_entry` | `80198b80` | `0x05` | `0x09` | `{0x01, 0x0a}` | `StaPaper 0x09` | `StaPaper 0x11` | high |
| `0x0b` | `status_more_menu_back_entry` | `80198b80` | `0x1e` | `0x22` | `{0x01, 0x0b}` | `StaPaper 0x09` | `StaPaper 0x12` | high |
| `0x0c` | `status_options_choice_value_0_entry` | `8019844c` | `0x87` | `0x8b` | `{0x01, 0x0c}` | `StaPaper 0x65` | `StaPaper 0x1b` | high |
| `0x0d` | `status_options_choice_value_1_entry` | `8019844c` | `0x88` | `0x8c` | `{0x01, 0x0d}` | `StaPaper 0x65` | `StaPaper 0x1c` | high |
| `0x0e` | `status_options_choice_value_0_selector_panel_entry` | `80198118` | `0x89` | `0x8d` | `{0x01, 0x0e}` | `StaPaper 0x65` | `StaPaper 0x6e` | high |
| `0x0f` | `status_options_choice_value_0_value_panel_entry` | `80198118` | `0x8a` | `0x8e` | `{0x01, 0x0f}` | `StaPaper 0x65` | `StaPaper 0x6f` | high |
| `0x10` | `status_options_choice_value_1_selector_panel_entry` | `80198118` | `0x8b` | `0x8f` | `{0x01, 0x10}` | `StaPaper 0x65` | `StaPaper 0x66` | high |
| `0x11` | `status_options_choice_value_1_value_panel_entry` | `80198118` | `0x8c` | `0x90` | `{0x01, 0x11}` | `StaPaper 0x65` | `StaPaper 0x67` | high |
| `0x12` | `status_moonberries_confirm_apply_entry` | `80198a0c` | `0x9b` | `0x9f` | `{0x01, 0x12}` | `StaPaper 0x09` | `StaPaper 0x48` | high |
| `0x13` | `status_moonberries_confirm_cancel_entry` | `80198a0c` | `0x9c` | `0xa0` | `{0x01, 0x13}` | `StaPaper 0x09` | `StaPaper 0x48` | high |

The records below now have stable machine-facing route keys from `FUN_8016f21c` and its constructors. These are record-level layout identities; preserve the visible-label caveat for rows whose final on-screen semantic role is still pending. Generated table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/fun_8016f21c_constructor_record_key_map.tsv`

| Member | Record | Machine-facing key | Constructor/layout route | Confidence |
| --- | --- | --- | --- | --- |
| `StaPaper.bin` | 0x06 | `status_menu_entry_primary_backing` | Common backing record for primary menu-entry layout ids `0x00..0x09`. | high |
| `StaPaper.bin` | 0x09 | `status_page_toggle_moonberries_choice_backing` | Backing record for root next/back entries and Moonberries choice layout ids `0x12..0x13`. | high |
| `StaPaper.bin` | 0x11 | `status_root_more_menu_entry_main` | Main record for layout id `0x0a`, object `0x05`, route `status_root_more_menu_entry`. | high |
| `StaPaper.bin` | 0x12 | `status_more_menu_back_entry_main` | Main record for layout id `0x0b`, object `0x1e`, route `status_more_menu_back_entry`. | high |
| `StaPaper.bin` | 0x13 | `status_options_description_apply_entry_main` | Main record for layout id `0x09`, object `0x1d`, route `status_options_description_apply_entry`. | high |
| `StaPaper.bin` | 0x14 | `status_equipment_or_ship_weapon_menu_entry_main` | Shared main record for layout id `0x03` (`status_equipment_menu_entry`) and layout id `0x04` (`status_ship_weapon_slot_menu_entry`). | high |
| `StaPaper.bin` | 0x15 | `status_inventory_menu_entry_main` | Main record for layout id `0x00`, route `status_inventory_menu_entry`. | high |
| `StaPaper.bin` | 0x16 | `status_options_menu_entry_main` | Main record for layout id `0x06`, object `0x1a`, route `status_options_menu_entry`. | high |
| `StaPaper.bin` | 0x17 | `status_smove_moonberries_menu_entry_main` | Main record for layout id `0x02`, route `status_smove_moonberries_menu_entry`. | high |
| `StaPaper.bin` | 0x18 | `status_magic_menu_entry_main` | Main record for layout id `0x01`, route `status_magic_menu_entry`. | high |
| `StaPaper.bin` | 0x19 | `status_wanted_list_menu_entry_main` | Main record for layout id `0x08`, object `0x1c`, route `status_wanted_list_menu_entry`. | high |
| `StaPaper.bin` | 0x1a | `status_crew_assignment_menu_entry_main` | Main record for layout id `0x07`, object `0x1b`, route `status_crew_assignment_menu_entry`. | high |
| `StaPaper.bin` | 0x1b | `status_options_choice_value_0_entry_main` | Main record for layout id `0x0c`, object `0x87`, selected when `DAT_80307d44 == 0` in the options/settings flow. | high |
| `StaPaper.bin` | 0x1c | `status_options_choice_value_1_entry_main` | Main record for layout id `0x0d`, object `0x88`, selected when `DAT_80307d44 == 1` in the options/settings flow. | high |
| `StaPaper.bin` | 0x1d | `status_ship_accessory_slot_menu_entry_main` | Main record for layout id `0x05`, object `0x79`, selected as the ship accessory slot entry in the ship equipment chooser. | high |
| `StaPaper.bin` | 0x65 | `status_options_choice_entry_backing` | Common backing record for options/settings choice layout ids `0x0c..0x11`. | high |
| `StaPaper.bin` | 0x66 | `status_options_choice_value_1_selector_panel_main` | Main record for layout id `0x10`; selected with `0x67` when `DAT_80307d44 == 1` in the options/settings choice flow. | high |
| `StaPaper.bin` | 0x67 | `status_options_choice_value_1_value_panel_main` | Main record for layout id `0x11`; selected with `0x66` when `DAT_80307d44 == 1` in the options/settings choice flow. | high |
| `StaPaper.bin` | 0x6e | `status_options_choice_value_0_selector_panel_main` | Main record for layout id `0x0e`; selected with `0x6f` when `DAT_80307d44 == 0` in the options/settings choice flow. | high |
| `StaPaper.bin` | 0x6f | `status_options_choice_value_0_value_panel_main` | Main record for layout id `0x0f`; selected with `0x6e` when `DAT_80307d44 == 0` in the options/settings choice flow. | high |

`FUN_80197270` creates the secondary selector/detail-window object groups that `FUN_8016fd10` slides when the field X-menu page hub receives left/right input on controller codes `6` and `7`. The generated TSV is `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/fun_8016fd10_secondary_selector_groups.tsv`. These rows are high-confidence as runtime object routing, but their final visible editor labels still need screenshot/consumer correlation.

| Selector value | Machine key range | Object ids | `DAT_80306f88` indexes | Slots | Record helper | Record cluster | Confidence |
| ---: | --- | --- | --- | ---: | --- | --- | --- |
| `0` | `status_secondary_detail_max_hp_max_mp_spirit`, `status_secondary_detail_power_vigor_will`, `status_secondary_detail_agile_quick`, `status_secondary_base_stats_experience` | `0x23..0x26` | `0x27..0x2a` | 4 | `FUN_8018b278` | Secondary base-stat/detail panels rendered by `FUN_8018a2d8` from Max HP/MP, Spirit, Power/Vigor/Will, Agile/Quick, and EXP payload fields. | high |
| `1` | `status_secondary_detail_weapon_armor_equipment`, `status_secondary_detail_accessory_equipment`, `status_secondary_detail_attack_defense_will_magic_defense`, `status_secondary_detail_hit_dodge_quick`, `status_secondary_detail_experience` | `0x27..0x2b` | `0x2b..0x2f` | 5 | `FUN_8018b278` | Large `StaPaper`/`StaSprite00` status-detail cluster that reuses the root-carousel equipment/stat/experience art families. | high |
| `2` | `status_secondary_magic_rank_detail_green`, `status_secondary_magic_rank_detail_red`, `status_secondary_magic_rank_detail_purple`, `status_secondary_magic_rank_detail_blue`, `status_secondary_magic_rank_detail_yellow`, `status_secondary_magic_rank_detail_silver` | `0x2c..0x31` | `0x30..0x35` | 6 | `FUN_8018b278` | Six moon-element magic-rank/detail panels with payload rank fields and matching `FUN_801f2894(character, element)` values. | high |

`FUN_8018b278` is now mapped as the secondary status/detail payload builder. It installs the common `StaCard 0x02`, `StaPaper 0x04`, and `StaPaper 0x0a` frame/control records, then chooses label/value artwork by selector group and slot. Generated table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/secondary_status_detail_payload_record_map.tsv`

| Selector group | Slots | Machine key range | Installed slot-specific records | Current interpretation | Confidence |
| ---: | --- | --- | --- | --- | --- |
| `0` | `0..3` | `status_secondary_detail_max_hp_max_mp_spirit`, `status_secondary_detail_power_vigor_will`, `status_secondary_detail_agile_quick`, `status_secondary_base_stats_experience` | `StaSprite00 0x17..0x1c`, `0x24..0x29`, `0x12`, `0x16`, `0x1f`, `0x23`, `0x36`, `0x55`, plus shared `0x04` | Secondary base-stat/detail panels. `FUN_8018a2d8` draws Max HP/MP, Spirit, Power/Vigor/Will, Agile/Quick, and EXP values from the status payload. | high |
| `1` | `0..4` | `status_secondary_detail_weapon_armor_equipment`, `status_secondary_detail_accessory_equipment`, `status_secondary_detail_attack_defense_will_magic_defense`, `status_secondary_detail_hit_dodge_quick`, `status_secondary_detail_experience` | Character moon marker `StaPaper 0x2b..0x30`, `StaPaper 0x31`, `StaPaper 0x33`, `StaSprite00 0x10..0x16`, `0x1d..0x23`, `0x36`, `0x38`, plus shared `0x04` | Secondary detail panels that reuse the root carousel equipment, attack/defense/will/magic-defense, hit/dodge/quick, and experience record sets. | high |
| `2` | `0..5` | `status_secondary_magic_rank_detail_green`, `red`, `purple`, `blue`, `yellow`, `silver` | `StaPaper 0x34`, `0x35`, `0x37`, `0x36`, `0x38`, `0x39`; `StaSprite00 0x2b..0x33`; shared `StaSprite00 0x2a` | Six moon-element magic-rank/detail panels. `FUN_8018dabc` stores Green/Red/Purple/Blue/Yellow/Silver rank fields at payload `+0x80..+0x94` and matching `FUN_801f2894(character, element)` values at `+0x98..+0xac`. | high |

`FUN_80194f88` draws the secondary-detail selection/highlight overlay from `StaPaper 0x5e`, `0x5f`, and `0x60`. The base and overlay layers are drawn at the active child object's current `x/y` with fixed offsets, while `0x5f` is an animated scanline whose Y offset advances through `DAT_8034711e` / `DAT_8034711c`. `FUN_8018a16c` calls this helper in the secondary-detail branch after `FUN_8018c8b4`, and the wider call graph also routes child menu/control rendering through `FUN_8018e82c`. Generated table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/secondary_detail_selection_highlight_record_map.tsv`.

`FUN_8016f6a4` provides the first semantic page-dispatch mapping from the page hub. When the primary selector's stored slot value reaches state `0x12`, it maps slot `0` to item/inventory state `7`, slot `1` to magic state `5`, slot `2` to S-Move/Moonberries state `10`, and slot `3` to equipment state `8`. It then refreshes seven prebuilt detail objects with `FUN_80189900(character, row-1, mode)`. The generated per-slot table is `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/fun_8016f6a4_primary_page_dispatch.tsv`.

| Primary selector slot | Target state | Page role | Mode arg | Constructor | Object ids | `DAT_80306f88` indexes | Machine key range | Confidence |
| ---: | ---: | --- | ---: | --- | --- | --- | --- | --- |
| `0` | `7` | Item/inventory page | `0` | `80197e48` | `0x33..0x39` | `0x37..0x3d` | `status_primary_page_item_inventory_header`, `status_primary_page_item_inventory_usable_items_row`, `status_primary_page_item_inventory_weapons_row`, `status_primary_page_item_inventory_armor_row`, `status_primary_page_item_inventory_accessories_row`, `status_primary_page_item_inventory_ship_battle_items_row`, `status_primary_page_item_inventory_key_items_row` | high |
| `1` | `5` | Magic page | `1` | `80197cc4` | `0x3a..0x40` | `0x3e..0x44` | `status_primary_page_magic_header`, `status_primary_page_magic_green_row`, `status_primary_page_magic_red_row`, `status_primary_page_magic_purple_row`, `status_primary_page_magic_blue_row`, `status_primary_page_magic_yellow_row`, `status_primary_page_magic_silver_row` | high |
| `2` | `10` | S-Move / Moonberries page | `2` | `80197b1c` | `0x48..0x4e` | `0x4c..0x52` | `status_primary_page_smove_moonberries_header`, `status_primary_page_smove_moonberries_first_row`, `status_primary_page_smove_moonberries_repeated_row_01`, `status_primary_page_smove_moonberries_repeated_row_02`, `status_primary_page_smove_moonberries_repeated_row_03`, `status_primary_page_smove_moonberries_repeated_row_04`, `status_primary_page_smove_moonberries_repeated_row_05` | high |
| `3` | `8` | Equipment page | `3` | `80197998` | `0x41..0x47` | `0x45..0x4b` | `status_primary_page_equipment_header`, `status_primary_page_equipment_detail_row_00..05` | high |

`FUN_801f1774` maps item/inventory categories to concrete inventory section globals, and `FUN_801f4ae4` maps those section ids to the underlying definition arrays. This provides high-confidence item-type labels for the category rows.

| Category | Row object key | Marker key | Inventory section | Confidence |
| ---: | --- | --- | --- | --- |
| `0` | `status_primary_page_item_inventory_usable_items_row` | `status_primary_item_inventory_marker_usable_items` | `Inventory::Section3_8030bf08` / `USABLE_ITEM_DEFINITIONS`, ids `0xf0..0x13f` | high |
| `1` | `status_primary_page_item_inventory_weapons_row` | `status_primary_item_inventory_marker_weapons` | `Inventory::Section0_8030bb48` / `WeaponData_ARRAY`, ids `0x00..0x4f` | high |
| `2` | `status_primary_page_item_inventory_armor_row` | `status_primary_item_inventory_marker_armor` | `Inventory::Section1_8030bc88` / `armor_ARRAY`, ids `0x50..0x9f` | high |
| `3` | `status_primary_page_item_inventory_accessories_row` | `status_primary_item_inventory_marker_accessories` | `Inventory::Section2_8030bdc8` / `accessory_ARRAY`, ids `0xa0..0xef` | high |
| `4` | `status_primary_page_item_inventory_ship_battle_items_row` | `status_shared_ship_battle_item_or_fallback_marker` | `Inventory::Section7_8030c188` / `ShipBattle_Item_ARRAY`, ids `0x1e0+` | high |
| `5` | `status_primary_page_item_inventory_key_items_row` | `status_primary_item_inventory_marker_key_items` | `Inventory::Section4_8030c048` / `KEY_ITEM_DEFINITIONS`, ids `0x140..0x18f` | high |

The primary page slots use a small set of shared detail-payload builders. The generated tables are `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/primary_page_detail_payload_record_map.tsv` and `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/primary_page_detail_record_key_map.tsv`. These rows identify helper-installed artwork records; exact visible labels beyond page/row role still require screenshot correlation.

| Helper | Applies to slots | Installed records | Current role | Confidence |
| --- | --- | --- | --- | --- |
| `FUN_80189834` | Slot `00` for all four primary pages | `StaPaper 0x1f`, `StaCard 0x03`, `StaPaper 0x05` | Shared primary detail header/backing payload. | high |
| `FUN_80189214` | Item/inventory slots `01..06`; magic slots `01..06` | Row frame records `StaPaper 0x21..0x26`; item/inventory row markers `StaPaper 0x31`, `0x32`, `0x33`, `0x3c`, `0x41`, `0x5d`; magic element markers `StaPaper 0x34`, `0x35`, `0x37`, `0x36`, `0x38`, `0x39`. | Six-row item/magic detail payload family. | high |
| `FUN_8018968c` | S-Move/Moonberries slot `01` | `StaPaper 0x21`, `StaPaper 0x3a` | First S-Move/Moonberries detail row payload. | high |
| `FUN_801895c0` | S-Move/Moonberries slots `02..06` | `StaPaper 0x22`, `StaPaper 0x3b` | Repeated S-Move/Moonberries detail row payload. | high |
| `FUN_80189788` | Equipment slots `01..06` | `StaPaper 0x20` | Equipment detail row payload. | high |

Promoted helper-level record keys:

| Member | Record | Machine-facing key | Helper/page role | Confidence |
| --- | --- | --- | --- | --- |
| `StaCard.bin` | 0x03 | `status_primary_detail_header_card_backing` | Shared slot-00 backing for item/inventory, magic, S-Move/Moonberries, and equipment primary pages. | high |
| `StaPaper.bin` | 0x05 | `status_primary_detail_header_paper_payload` | Shared slot-00 paper payload for item/inventory, magic, S-Move/Moonberries, and equipment primary pages. | high |
| `StaPaper.bin` | 0x1f | `status_shared_detail_header_frame` | Shared slot-00 frame/header for item/inventory, magic, S-Move/Moonberries, and equipment primary pages; also reused by options/settings choice-panel slot `2`. | high |
| `StaPaper.bin` | 0x20 | `status_shared_equipment_or_options_detail_row_backing` | Equipment detail row backing for primary equipment slots `01..06`; also reused as the first panel record for options/settings choice-panel slot `2`. | high |
| `StaPaper.bin` | 0x21 | `status_primary_detail_row_slot_00_frame` | Item/magic row `00` frame; also first S-Move/Moonberries row frame. | high |
| `StaPaper.bin` | 0x22 | `status_primary_detail_row_slot_01_frame` | Item/magic row `01` frame; also repeated S-Move/Moonberries row frame. | high |
| `StaPaper.bin` | 0x23 | `status_primary_detail_row_slot_02_frame` | Item/magic row `02` frame. | high |
| `StaPaper.bin` | 0x24 | `status_primary_detail_row_slot_03_frame` | Item/magic row `03` frame. | high |
| `StaPaper.bin` | 0x25 | `status_primary_detail_row_slot_04_frame` | Item/magic row `04` frame. | high |
| `StaPaper.bin` | 0x26 | `status_primary_detail_row_slot_05_frame` | Item/magic row `05` frame. | high |
| `StaPaper.bin` | 0x31 | `status_primary_item_inventory_marker_armor` | Item/inventory marker for category `2`; `FUN_801f1774` routes it through `Inventory::Section1_8030bc88`, and `FUN_801f4ae4` maps that section to `armor_ARRAY`. | high |
| `StaPaper.bin` | 0x32 | `status_primary_item_inventory_marker_usable_items` | Item/inventory marker for category `0`; `FUN_801f1774` routes it through `Inventory::Section3_8030bf08`, and `FUN_801f4ae4` maps that section to `USABLE_ITEM_DEFINITIONS`. | high |
| `StaPaper.bin` | 0x33 | `status_primary_item_inventory_marker_accessories` | Item/inventory marker for category `3`; `FUN_801f1774` routes it through `Inventory::Section2_8030bdc8`, and `FUN_801f4ae4` maps that section to `accessory_ARRAY`. | high |
| `StaPaper.bin` | 0x34 | `status_shared_green_moon_paper_marker` | Shared green moon marker. Used by the primary magic row for element `0`, secondary magic-rank detail slot `0`, and options/toggle helper row `0` / Sound. | high |
| `StaPaper.bin` | 0x35 | `status_shared_red_moon_paper_marker` | Shared red moon marker. Used by the primary magic row for element `1`, secondary magic-rank detail slot `1`, and options/toggle helper row `1` / Camera. | high |
| `StaPaper.bin` | 0x36 | `status_shared_blue_moon_paper_marker` | Shared blue moon marker. Used by the primary magic row for element `3`, secondary magic-rank detail slot `3`, and options/toggle helper row `3`. | high |
| `StaPaper.bin` | 0x37 | `status_shared_purple_moon_paper_marker` | Shared purple moon marker. Used by the primary magic row for element `2`, secondary magic-rank detail slot `2`, and options/toggle helper row `2` / Rumble. | high |
| `StaPaper.bin` | 0x38 | `status_shared_yellow_moon_paper_marker` | Shared yellow moon marker. Used by the primary magic row for element `4`, secondary magic-rank detail slot `4`, and options/toggle helper row `4`. | high |
| `StaPaper.bin` | 0x39 | `status_shared_silver_moon_paper_marker` | Shared silver moon marker. Used by the primary magic row for element `5`, secondary magic-rank detail slot `5`, and options/toggle helper row `5`. | high |
| `StaPaper.bin` | 0x3a | `status_primary_smove_moonberries_first_row_marker` | First S-Move/Moonberries detail row marker. | high |
| `StaPaper.bin` | 0x3b | `status_primary_smove_moonberries_repeated_row_marker` | Repeated S-Move/Moonberries detail row marker. | high |
| `StaPaper.bin` | 0x3c | `status_primary_item_inventory_marker_key_items` | Item/inventory marker for category `5`; `FUN_801f1774` routes it through `Inventory::Section4_8030c048`, and `FUN_801f4ae4` maps that section to `KEY_ITEM_DEFINITIONS`. | high |
| `StaPaper.bin` | 0x41 | `status_shared_ship_battle_item_or_fallback_marker` | Shared marker used as item/inventory category `4` / ship battle items; `FUN_801f1774` routes that category through `Inventory::Section7_8030c188` and `FUN_801f4ae4` maps it to `ShipBattle_Item_ARRAY`. `FUN_80188a18` also selects this record for one fallback/unhandled item-type path. | high |
| `StaPaper.bin` | 0x5d | `status_primary_item_inventory_marker_weapons` | Item/inventory marker for category `1`; `FUN_801f1774` routes it through `Inventory::Section0_8030bb48`, and `FUN_801f4ae4` maps that section to `WeaponData_ARRAY`. | high |

The root carousel and secondary-detail installers split many `StaSprite00` records into one stat label/icon plus a nearby companion marker. `FUN_8018c00c` installs the five root carousel windows; `FUN_8018b278` installs secondary detail groups; renderers `FUN_8018d250` and `FUN_8018a2d8` identify the displayed payload fields. Generated table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/stat_detail_sprite_record_key_map.tsv`

| Member | Record | Machine-facing key | Helper/detail role | Confidence |
| --- | --- | --- | --- | --- |
| `StaSprite00.bin` | 0x10 | `status_stat_attack_label_icon` | Attack label/icon for root carousel window `0` and secondary detail group `1` slot `2`; paired with payload Attack value. | high |
| `StaSprite00.bin` | 0x1d | `status_stat_attack_companion_marker` | Companion marker installed beside the Attack label/icon before the Attack numeric value. | high |
| `StaSprite00.bin` | 0x11 | `status_stat_defense_label_icon` | Defense label/icon for root carousel window `0` and secondary detail group `1` slot `2`; paired with payload Defense value. | high |
| `StaSprite00.bin` | 0x1e | `status_stat_defense_companion_marker` | Companion marker installed beside the Defense label/icon before the Defense numeric value. | high |
| `StaSprite00.bin` | 0x12 | `status_stat_will_label_icon` | Will label/icon reused by root carousel window `0`, secondary detail group `1` slot `2`, and secondary detail group `0` slot `1`; paired with payload Will value. | high |
| `StaSprite00.bin` | 0x1f | `status_stat_will_companion_marker` | Companion marker installed beside the Will label/icon before the Will numeric value. | high |
| `StaSprite00.bin` | 0x13 | `status_stat_magic_defense_label_icon` | Magic Defense label/icon for root carousel window `0` and secondary detail group `1` slot `2`; paired with payload Magic Defense value. | high |
| `StaSprite00.bin` | 0x20 | `status_stat_magic_defense_companion_marker` | Companion marker installed beside the Magic Defense label/icon before the Magic Defense numeric value. | high |
| `StaSprite00.bin` | 0x14 | `status_stat_hit_percent_label_icon` | Hit % label/icon for root carousel window `1` and secondary detail group `1` slot `3`; paired with payload Hit value. | high |
| `StaSprite00.bin` | 0x21 | `status_stat_hit_percent_companion_marker` | Companion marker installed beside the Hit % label/icon before the Hit numeric value. | high |
| `StaSprite00.bin` | 0x15 | `status_stat_dodge_percent_label_icon` | Dodge % label/icon for root carousel window `1` and secondary detail group `1` slot `3`; paired with payload Dodge value. | high |
| `StaSprite00.bin` | 0x22 | `status_stat_dodge_percent_companion_marker` | Companion marker installed beside the Dodge % label/icon before the Dodge numeric value. | high |
| `StaSprite00.bin` | 0x16 | `status_stat_quick_label_icon` | Quick label/icon reused by root carousel window `1`, secondary detail group `1` slot `3`, and secondary detail group `0` slot `2`; paired with payload Quick value. | high |
| `StaSprite00.bin` | 0x23 | `status_stat_quick_companion_marker` | Companion marker installed beside the Quick label/icon before the Quick numeric value. | high |
| `StaSprite00.bin` | 0x1b | `status_stat_max_hp_label_icon` | Max HP label/icon for secondary detail group `0` slot `0`; paired with payload Max HP value. | high |
| `StaSprite00.bin` | 0x28 | `status_stat_max_hp_companion_marker` | Companion marker installed beside the Max HP label/icon before the Max HP numeric value. | high |
| `StaSprite00.bin` | 0x1c | `status_stat_max_mp_label_icon` | Max MP label/icon for secondary detail group `0` slot `0`; paired with payload Max MP value. | high |
| `StaSprite00.bin` | 0x29 | `status_stat_max_mp_companion_marker` | Companion marker installed beside the Max MP label/icon before the Max MP numeric value. | high |
| `StaSprite00.bin` | 0x19 | `status_stat_spirit_label_icon` | Spirit label/icon for secondary detail group `0` slot `0`; paired with payload Spirit value. | high |
| `StaSprite00.bin` | 0x26 | `status_stat_spirit_companion_marker` | Companion marker installed beside the Spirit label/icon before the Spirit numeric value. | high |
| `StaSprite00.bin` | 0x55 | `status_stat_max_spirit_label_icon` | Max Spirit label/icon for secondary detail group `0` slot `0`; paired with payload Max Spirit value. | high |
| `StaSprite00.bin` | 0x17 | `status_stat_power_label_icon` | Power label/icon for secondary detail group `0` slot `1`; paired with payload Power value. | high |
| `StaSprite00.bin` | 0x24 | `status_stat_power_companion_marker` | Companion marker installed beside the Power label/icon before the Power numeric value. | high |
| `StaSprite00.bin` | 0x18 | `status_stat_vigor_label_icon` | Vigor label/icon for secondary detail group `0` slot `1`; paired with payload Vigor value. | high |
| `StaSprite00.bin` | 0x25 | `status_stat_vigor_companion_marker` | Companion marker installed beside the Vigor label/icon before the Vigor numeric value. | high |
| `StaSprite00.bin` | 0x1a | `status_stat_agile_label_icon` | Agile label/icon for secondary detail group `0` slot `2`; paired with payload Agile value. | high |
| `StaSprite00.bin` | 0x27 | `status_stat_agile_companion_marker` | Companion marker installed beside the Agile label/icon before the Agile numeric value. | high |
| `StaSprite00.bin` | 0x36 | `status_stat_experience_panel_label` | Experience / next-EXP label art for root carousel window `4` and secondary detail experience panels. | high |
| `StaSprite00.bin` | 0x38 | `status_equipment_name_row_marker` | Shared sprite marker installed for weapon/armor and accessory name panels in root carousel and secondary detail equipment views. | high |

`FUN_80188098` and `FUN_80188274` identify a six-row options/toggle helper cluster. The helper loops six bit positions, passes the row id through stack byte `+0x0c`, and `FUN_80188274` selects one `StaPaper` marker plus one `StaSprite00` marker per row. Rows `0..2` now have high-confidence setting labels from `FUN_8017a040` and the option action helpers. Action/table row `3` is VMU sound, but the active predicate/list builders `FUN_8017a6a4` and `FUN_8017a8a8` enumerate only rows `0..2`; the adjacent predicate slot at `802e9424` is `FUN_8019406c`, which prints a VM debug message and returns `0`. Keep helper rows `3..5` row-keyed unless screenshot or runtime evidence proves they render. Generated table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/options_helper_record_key_map.tsv`

| Helper row | Paper marker key | Sprite marker key | Helper role | Confidence |
| ---: | --- | --- | --- | --- |
| `0` | `status_shared_green_moon_paper_marker` | `status_shared_green_moon_sprite_marker` | Sound option row. `FUN_8017a040` row `0` dispatches through `FUN_80194254`, which updates the pending sound mode. | high |
| `1` | `status_shared_red_moon_paper_marker` | `status_shared_red_moon_sprite_marker` | Camera option row. `FUN_8017a040` row `1` dispatches through `FUN_80194210`, which updates the camera-control direction flag. | high |
| `2` | `status_shared_purple_moon_paper_marker` | `status_shared_purple_moon_sprite_marker` | Rumble option row. `FUN_8017a040` row `2` dispatches through `FUN_80194150`, which updates the rumble-enabled flag. | high |
| `3` | `status_shared_blue_moon_paper_marker` | `status_shared_blue_moon_sprite_marker` | Helper row `3`, selected by `FUN_80188274`; current visible options list evidence does not prove this row renders. | high |
| `4` | `status_shared_yellow_moon_paper_marker` | `status_shared_yellow_moon_sprite_marker` | Helper row `4`, selected by `FUN_80188274`; current visible options list evidence does not prove this row renders. | high |
| `5` | `status_shared_silver_moon_paper_marker` | `status_shared_silver_moon_sprite_marker` | Helper row `5`, selected by `FUN_80188274`; current visible options list evidence does not prove this row renders. | high |

The options action table has four setting rows. `FUN_8017a040` dispatches selected rows through `PTR_FUN_802e9428`, and helper exports under `ghidra_export/hrs_status_option_helpers/` identify the saved-setting side effects. The VMU-sound action exists in that table, but the visible predicate/list loops currently prove only rows `0..2`. Generated table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/options_setting_action_map.tsv`

| Action row | Setting key | Values | Action/effect | Availability/status helper | Confidence |
| ---: | --- | --- | --- | --- | --- |
| `0` | `status_option_sound_mode` | `Stereo` / `Mono` | `80194254_FUN_80194254` writes `DAT_80347144 = DAT_80307eec`; `FUN_80194268` applies it through `FUN_8026deb8` and `OSSetSoundMode`. | `80194104_FUN_80194104` always returns `1`. | high |
| `1` | `status_option_camera_control_direction` | `Normal` / `Reverse` | `80194210_FUN_80194210` clears `FLAGS_80310bc4` bit `0x08` when `DAT_80307eec == 0`, and sets it when nonzero. | `801940fc_FUN_801940fc` always returns `1`. | high |
| `2` | `status_option_rumble_enabled` | `On` / `Off` | `80194150_FUN_80194150` sets `FLAGS_80310bc4` bit `0x10` and initializes rumble state when `DAT_80307eec == 0`; it clears bit `0x10` when nonzero. | `801940ac_FUN_801940ac` probes `FUN_801d6e38(0)` and returns true only when rumble support is available. | high |
| `3` | `status_option_vmu_sound_enabled` | `On` / `Off` | `8019410c_FUN_8019410c` sets `FLAGS_80310bc4` bit `0x20` when `DAT_80307eec == 0`, and clears it when nonzero. | The three-row predicate loops do not consume row `3`; adjacent raw predicate `8019406c_FUN_8019406c` prints a VM debug message and returns `0`. | medium |

`FUN_80198118` builds the options/settings choice-panel object family. It creates the selector object with callback `FUN_8019ada0`, then three sibling objects with callback `FUN_8019b44c`. `FUN_8019b42c` stores sibling slot ids `0..2`; `FUN_8019bd2c` installs the slot-specific records below. Generated table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/options_choice_panel_record_key_map.tsv`

| Member | Record | Machine-facing key | Slot scope | Helper role | Confidence |
| --- | --- | --- | --- | --- | --- |
| `StaCard.bin` | 0x0b | `status_options_choice_panel_slot_00_01_card_backing` | `0`, `1` | Card/backing record installed for the first two option choice-panel slots. | high |
| `StaCard.bin` | 0x0c | `status_options_choice_panel_slot_02_card_backing` | `2` | Card/backing record installed for the third option choice-panel slot. | high |
| `StaPaper.bin` | 0x68 | `status_options_choice_panel_slot_00_01_main_panel` | `0`, `1` | Main paper/panel record installed for the first two option choice-panel slots. | high |
| `StaPaper.bin` | 0x6a | `status_options_choice_panel_slot_00_01_aux_panel` | `0`, `1` | Auxiliary paper/panel record installed for the first two option choice-panel slots. | high |
| `StaPaper.bin` | 0x6b | `status_options_choice_panel_slot_02_aux_panel` | `2` | Auxiliary paper/panel record installed for the third option choice-panel slot. | high |
| `StaSprite00.bin` | 0x4e | `status_options_choice_panel_slot_00_01_sprite_marker` | `0`, `1` | Sprite marker installed through `FUN_80199ffc` for the first two option choice-panel slots. | high |
| `StaPaper.bin` | 0x69 | `status_options_choice_panel_active_selector_frame` | selector slots `0`, `1` | Red bordered selector-frame record drawn by `FUN_8019ae84` for the active options/settings choice-panel selector. The helper swaps draw order with `StaSprite00 0x4f` between slot ids `0` and `1`. | high |
| `StaSprite00.bin` | 0x4f | `status_options_choice_panel_active_selector_icon` | selector slots `0`, `1` | Icon/art record drawn by `FUN_8019ae84` for the active options/settings choice-panel selector, paired with `StaPaper 0x69`. | high |
| `StaSprite00.bin` | 0x50 | `status_options_choice_panel_dynamic_bar` | selector slots `0`, `1` | Long red bar record always drawn by `FUN_8019ae84` for the active options/settings choice-panel selector. | high |
| `StaSprite00.bin` | 0x51 | `status_options_choice_panel_dynamic_bar_cap` | selector slots `0`, `1` | Small red cap/marker record always drawn by `FUN_8019ae84`; the helper mutates its width and color from runtime option-panel state before drawing. | high |
| `StaSprite00.bin` | 0x52 | `status_options_choice_panel_value_row_marker` | sibling slot `2` | Small marker record drawn by `FUN_8019b59c` at option/value row endpoints in the third options/settings choice-panel sibling. | high |

`FUN_80188a18`, `FUN_8019082c`, `FUN_8018fdac`, and exported classifier `FUN_801951c0` also identify a status/detail item-type marker family. These are additional consumer roles for several records already used by primary-page helpers. Generated table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/status_detail_item_type_record_key_map.tsv`

| Member | Record | Machine-facing key | Helper/detail role | Confidence |
| --- | --- | --- | --- | --- |
| `StaPaper.bin` | 0x3d | `status_detail_weapon_type_marker_00` | Marker selected for weapon ids `0x190..0x1b7` whose item table byte is `0`. | high |
| `StaPaper.bin` | 0x3e | `status_detail_weapon_type_marker_01` | Marker selected for weapon ids `0x190..0x1b7` whose item table byte is `1`. | high |
| `StaPaper.bin` | 0x3f | `status_detail_weapon_type_marker_02` | Marker selected for weapon ids `0x190..0x1b7` whose item table byte is `2`. | high |
| `StaPaper.bin` | 0x40 | `status_detail_weapon_type_marker_03` | Marker selected for weapon ids `0x190..0x1b7` whose item table byte is `3`, except item id `0x1af`. | high |
| `StaPaper.bin` | 0x64 | `status_detail_weapon_type_marker_03_special_0x1af` | Special marker selected instead of `0x40` for item id `0x1af` when the item table byte is `3`. | high |
| `StaPaper.bin` | 0x42 | `status_detail_accessory_or_nonweapon_equipment_marker` | Marker selected for ids `0x1b8..0x1df`; `FUN_8018fdac` uses it while rendering `ShipBattle_accessory_ARRAY` entries. | high |
| `StaPaper.bin` | 0x4f | `status_detail_row_ff_sentinel_marker` | Detail-row sentinel/selected-state marker selected by `FUN_80188a18` when copied row metadata byte `+0x05` is `0xff`. `FUN_801f1590` writes this sentinel for currently equipped weapon, armor, accessory, ship weapon, and ship accessory rows. `FUN_801f1f80` can also copy `0xff` from inventory section metadata, so final visible wording outside equipment lists can still be improved with screenshot correlation. | high |

`FUN_801732c8` / `FUN_80171c80` are now identified as the crew assignment page reached from More menu selector value `1`. Supporting helpers build a 22-crew table (`FUN_80194b84`), arrange it as 11 two-person rows (`FUN_8017379c` / `FUN_80172b54`), display role text through `tblShipCrewDescriptions` (`FUN_80194c38`), and apply the rebuilt active crew list through `FUN_80194f3c` -> `FUN_8021b2b4`. Generated evidence table: `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/crew_assignment_state_map.tsv`

### Selector Helper Record Cluster

`FUN_80191a04` is a compact selector/control builder reached by adjacent helper
thunks near the wider callback-table slice after the normal X-menu state range,
and by normal callers `FUN_80199508` and `FUN_801986bc`. A later
`DAT_80347110` write audit supports normal dispatched X-menu states `0..19`
with entry `20` null, so do not treat the wider-slice helper thunks as ordinary
states `21..32` without new runtime evidence. The rows below are
high-confidence as helper-cluster membership, but their exact on-screen roles
remain pending page/control correlation. Generated case map:
`SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_mll_record_contact_sheets/selector_helper_case_context_map.tsv`

| Member | Record | Machine-facing key | Description | Confidence | Direct consumer functions |
| --- | --- | --- | --- | --- | --- |
| `StaCard.bin` | 0x02 | `status_selector_helper_card_backing` | Common card/backing record installed into every `FUN_80191a04` helper object. | high | 80191a04_FUN_80191a04 |
| `StaPaper.bin` | 0x0a | `status_selector_helper_paper_divider` | Common paper/divider record installed into every `FUN_80191a04` helper object. | high | 80191a04_FUN_80191a04 |
| `StaPaper.bin` | 0x01 | `status_selector_helper_backing_variant_a` | Selector/control backing variant used by observed helper cases `3`, `4`, `7`, `8`, `0x0d`, and `0x0e`. Implemented case `6` also selects this record, but no current caller reaches case `6`. | high | 80191a04_FUN_80191a04 |
| `StaPaper.bin` | 0x04 | `status_selector_helper_backing_variant_b` | Selector/control backing variant used by observed helper cases `5`, `9`, `0x0a`, `0x0f`, and `0x10`. Implemented cases `1` and `2` also select this record, but no current caller reaches those cases. | high | 80191a04_FUN_80191a04 |
| `StaSprite00.bin` | 0x04 | `status_selector_helper_icon_variant_a` | Selector/control icon variant used by observed helper cases `4`, `8`, `0x0d`, `0x0e`, `0x0f`, and `0x10`. Implemented case `2` also selects this record, but no current caller reaches case `2`. | high | 80191a04_FUN_80191a04 |
| `StaSprite00.bin` | 0x3b | `status_selector_helper_icon_variant_b` | Selector/control icon variant used by observed helper cases `5`, `7`, `9`, and `0x0a`. Implemented case `6` also selects this record, but no current caller reaches case `6`. | high | 80191a04_FUN_80191a04 |
| `StaSprite00.bin` | 0x58 | `status_selector_helper_icon_variant_c` | Selector/control icon variant used by observed helper case `3`. Implemented case `1` also selects this record, but no current caller reaches case `1`. | high | 80191a04_FUN_80191a04 |

Raw state-table case mapping:

| State indexes | Callback entries | Helper cases | Record implication |
| --- | --- | --- | --- |
| `21`, `23` | `80199724` | `3` | `StaSprite00 0x58`, `StaPaper 0x01`, common `StaCard 0x02`, `StaPaper 0x0a`. |
| `22`, `24` | `80199744` | `4` | `StaSprite00 0x04`, `StaPaper 0x01`, common `StaCard 0x02`, `StaPaper 0x0a`. |
| `25`, `26` | `80199764` | `7` | `StaSprite00 0x3b`, `StaPaper 0x01`, common `StaCard 0x02`, `StaPaper 0x0a`. |
| `27`, `28` | `80199784` | `8` | `StaSprite00 0x04`, `StaPaper 0x01`, common `StaCard 0x02`, `StaPaper 0x0a`. |
| `29` | `801997a4` | `9` | `StaSprite00 0x3b`, `StaPaper 0x04`, common `StaCard 0x02`, `StaPaper 0x0a`. |
| `30` | `801997c0` | `0x0a` | `StaSprite00 0x3b`, `StaPaper 0x04`, common `StaCard 0x02`, `StaPaper 0x0a`. |
| `31` | `801997dc` | `0x0d` | `StaSprite00 0x04`, `StaPaper 0x01`, common `StaCard 0x02`, `StaPaper 0x0a`. |
| `32` | `801997f8` | `0x0e` | `StaSprite00 0x04`, `StaPaper 0x01`, common `StaCard 0x02`, `StaPaper 0x0a`. |

Normal-function selector-helper cases:

| Caller | Helper cases | Record implication |
| --- | --- | --- |
| `FUN_80199508` | `5` | Creates eight repeated `FUN_8018e82c` controls with `StaSprite00 0x3b`, `StaPaper 0x04`, common `StaCard 0x02`, and `StaPaper 0x0a`. |
| `FUN_801986bc` | `0x0f`, `0x10` | Creates two paired `FUN_8018e82c` controls with `StaSprite00 0x04`, `StaPaper 0x04`, common `StaCard 0x02`, and `StaPaper 0x0a`; the same function then creates two sibling `FUN_8016e220` controls. |

Implemented but not observed in the current caller audit: helper cases `1`,
`2`, and `6`. They are present in the `FUN_80191a04` switch body but no current
Ghidra caller passes those case ids.

Next promotion target: correlate the primary page/detail rows with screenshots so their high-confidence machine-facing route keys can be upgraded into visible editor labels. Current Ghidra evidence already identifies the root selector, root carousel roles and stat fields, More menu routes, primary page dispatch groups, secondary selector groups and payloads, options/settings rows, option choice-panel records, and the wanted-list entry.

## field/hrs_bend.bin

Source research: `SpiceBin/research/2026-06-20_hrs_bend_record_mapping/README.md`

Companion texture evidence: `field/ts000110.gvr`. Runtime evidence identifies this as the battle-end result screen layout. `FUN_800e3594` loads `/field/HRS_BEND.BIN` into the field result-screen state object at `DAT_80346dcc + 0x15f8`. The large result-screen state machine in `FUN_800e4644` advances on A presses (`RawControllers[0]->newPresses & 0x100`) and delegates drawing to the HRS_BEND record consumers listed below.

### Record Groups

`field/hrs_bend.bin` contains 63 indexed records, `0x00..0x3e`.

| Record range | Current role | Confidence |
| --- | --- | --- |
| `0x00..0x05`, `0x0b` | Main battle-end result screen backdrop and frame pieces. | High for screen role; medium for exact art-variant semantics. |
| `0x06..0x07` | Total EXP / Magic EXP / Gold header and backing. | High. |
| `0x08`, `0x0a` | Dropped-items popup window pieces. | High. |
| `0x09`, `0x0c..0x11`, `0x16..0x17` | Per-character result-row backings and stat/magic row labels. | High group confidence; medium for exact subrow labels. |
| `0x12..0x15`, `0x37..0x3e` | Level-up and magic-rank-up panel variants. | Medium. |
| `0x18..0x1d` | Playable-character portraits: Vyse, Aika, Fina, Drachma, Enrique, Gilder. | High. |
| `0x20..0x25` | Moon/element icons used for learned magic and rank display: green, red, purple, blue, yellow, silver. | High. |
| `0x1e..0x1f`, `0x27..0x2d`, `0x30..0x36` | Dropped-item category icons selected through the local lookup table at `802b19e4`, now matched to visible item/equipment/ship categories. | High for lookup-selected icons; records `0x26`, `0x2e`, and `0x2f` remain unresolved atlas icons. |

### Main Consumers

| Function | Record use |
| --- | --- |
| `FUN_800e4644` | Parent battle-end result state machine. Handles A-press progression and calls the main screen, per-character, and drop-popup renderers. |
| `FUN_800ea244` | Draws records `0x00..0x05` and `0x0b` for the main result backdrop. |
| `FUN_800e9f28` | Draws records `0x06` and `0x07`, then text-renders total EXP, Magic EXP, and Gold values. |
| `FUN_800e96c0` | Draws records `0x08` and `0x0a`, then draws dropped-item category icons through the `802b19e4` lookup table and text-renders item names/counts. |
| `FUN_800e6554` | Dispatches per-character result row renderers by row state. |
| `FUN_800e9b04` | Draws records `0x0f..0x11` and character portrait records `0x18..0x1d`, then text-renders character names and values. |
| `FUN_800e9044` | Draws record `0x09` plus stat-label record `0x0c`, `0x0d`, or `0x0e`, then text-renders stat values. |
| `FUN_800e8c6c` | Draws record `0x09` plus moon/magic EXP label record `0x17`, then text-renders six moon EXP values. |
| `FUN_800e88fc` | Draws record `0x09`, record `0x16`, and moon icon records selected by learned ability element, then text-renders learned magic messages. |
| `FUN_800e7d0c` | Draws level-up and magic-rank-up panel variants from records `0x12..0x15` and `0x37..0x3e`. |

### One-to-One Records

| Record | Key | Description | Confidence | Human Annotation | Direct consumer functions |
| --- | --- | --- | --- | --- | --- |
| 0x00 | `bend_result_screen_base_backing` | Main battle-end result screen base/backing. | high |  | 800ea244_FUN_800ea244 |
| 0x01 | `bend_result_top_art_variant_a` | Top result-screen art variant selected when `FUN_800ea244` receives variant `0`. | medium | This is probably before Fina joins the party | 800ea244_FUN_800ea244 |
| 0x02 | `bend_result_top_art_variant_b` | Top result-screen art variant selected when `FUN_800ea244` receives a nonzero variant. | medium | This is probably after Fina joins the party | 800ea244_FUN_800ea244 |
| 0x03 | `bend_result_blur_backdrop` | Large blurred/clouded backdrop piece drawn after the top art. | medium |  | 800ea244_FUN_800ea244 |
| 0x04 | `bend_result_bottom_ornament_strip` | Bottom ornament strip for the result screen. | medium |  | 800ea244_FUN_800ea244 |
| 0x05 | `bend_result_main_window_frame` | Main result-screen window/frame. | high |  | 800ea244_FUN_800ea244 |
| 0x06 | `bend_totals_header_backing` | Backing/header strip for total EXP, Magic EXP, and Gold values. | high |  | 800e9f28_FUN_800e9f28 |
| 0x07 | `bend_totals_header_labels` | Static total labels: Exp., Magic Exp., and Gold. | high |  | 800e9f28_FUN_800e9f28 |
| 0x08 | `bend_drop_popup_header_backing` | Dropped-items popup header/backing. | high |  | 800e96c0_FUN_800e96c0 |
| 0x09 | `bend_result_detail_row_separator` | Shared detail-row strip drawn before stat, magic EXP, and learned-magic rows. | high |  | 800e9044_FUN_800e9044<br>800e8c6c_FUN_800e8c6c<br>800e88fc_FUN_800e88fc |
| 0x0a | `bend_drop_items_window_frame` | Dropped-items list window with Items label. | high |  | 800e96c0_FUN_800e96c0 |
| 0x0b | `bend_result_vertical_separator` | Vertical separator/frame piece in the main result backdrop. | medium |  | 800ea244_FUN_800ea244 |
| 0x0c | `bend_stat_gain_labels_agile_quick` | Stat-gain label row variant used by `FUN_800e9044`; visually includes Agile and Quick labels. | medium | Only shows during level up | 800e9044_FUN_800e9044 |
| 0x0d | `bend_stat_gain_labels_hp_mp_spirit` | Stat-gain label row variant used by `FUN_800e9044`; visually includes MaxHP/MaxMP/Spirit-style labels. | medium | Only shows during level up | 800e9044_FUN_800e9044 |
| 0x0e | `bend_stat_gain_labels_power_will_vigor` | Stat-gain label row variant used by `FUN_800e9044`; visually includes Power, Will, and Vigor labels. | medium | Only shows during level up | 800e9044_FUN_800e9044 |
| 0x0f | `bend_character_result_row_window` | Per-character result row window/backing. | high |  | 800e9b04_FUN_800e9b04 |
| 0x10 | `bend_character_result_stat_slots` | Segmented per-character stat/value slots. | high |  | 800e9b04_FUN_800e9b04 |
| 0x11 | `bend_character_result_row_accent` | Per-character row underline/accent. | medium |  | 800e9b04_FUN_800e9b04 |
| 0x12 | `bend_levelup_panel_small_backing` | Small level-up panel backing variant drawn by `FUN_800e7d0c`. | medium |  | 800e7d0c_FUN_800e7d0c |
| 0x13 | `bend_levelup_panel_small_inner` | Small level-up panel inner/label backing variant drawn by `FUN_800e7d0c`. | medium |  | 800e7d0c_FUN_800e7d0c |
| 0x14 | `bend_levelup_panel_small_underline` | Small level-up panel underline/accent. | medium |  | 800e7d0c_FUN_800e7d0c<br>800e7934_FUN_800e7934 |
| 0x15 | `bend_levelup_label_small` | Static LEVEL UP!! label for the small level-up panel variant. | high |  | 800e7d0c_FUN_800e7d0c |
| 0x16 | `bend_magic_learned_row_window` | Learned-magic/result row backing used with ability element icons and learned spell text. | high |  | 800e88fc_FUN_800e88fc<br>800e9424_FUN_800e9424 |
| 0x17 | `bend_magic_exp_moon_labels` | Six moon/magic EXP label row. | high |  | 800e8c6c_FUN_800e8c6c |
| 0x18 | `bend_portrait_vyse` | Vyse portrait used in per-character result rows. | high |  | 800e9b04_FUN_800e9b04 |
| 0x19 | `bend_portrait_aika` | Aika portrait used in per-character result rows. | high |  | 800e9b04_FUN_800e9b04 |
| 0x1a | `bend_portrait_fina` | Fina portrait used in per-character result rows. | high |  | 800e9b04_FUN_800e9b04 |
| 0x1b | `bend_portrait_drachma` | Drachma portrait used in per-character result rows. | high |  | 800e9b04_FUN_800e9b04 |
| 0x1c | `bend_portrait_enrique` | Enrique portrait used in per-character result rows. | high |  | 800e9b04_FUN_800e9b04 |
| 0x1d | `bend_portrait_gilder` | Gilder portrait used in per-character result rows. | high |  | 800e9b04_FUN_800e9b04 |
| 0x1e | `bend_drop_category_icon_consumable` | Dropped-item consumable icon selected by lookup codes `1` and `4` in the local item-category table. | high | Used for consumables | 800e96c0_FUN_800e96c0 |
| 0x1f | `bend_drop_category_icon_accessory` | Dropped-item accessory icon selected by lookup code `3`. | high | Used for accessories | 800e96c0_FUN_800e96c0 |
| 0x20 | `bend_magic_moon_icon_green` | Green moon/element icon. | high |  | 800e88fc_FUN_800e88fc<br>800e7d0c_FUN_800e7d0c |
| 0x21 | `bend_magic_moon_icon_red` | Red moon/element icon. | high |  | 800e88fc_FUN_800e88fc<br>800e7d0c_FUN_800e7d0c |
| 0x22 | `bend_magic_moon_icon_blue` | Blue moon/element icon. Runtime element table order selects this for element index `3`. | high |  | 800e88fc_FUN_800e88fc<br>800e7d0c_FUN_800e7d0c |
| 0x23 | `bend_magic_moon_icon_purple` | Purple moon/element icon. Runtime element table order selects this for element index `2`. | high |  | 800e88fc_FUN_800e88fc<br>800e7d0c_FUN_800e7d0c |
| 0x24 | `bend_magic_moon_icon_yellow` | Yellow moon/element icon. | high |  | 800e88fc_FUN_800e88fc<br>800e7d0c_FUN_800e7d0c |
| 0x25 | `bend_magic_moon_icon_silver` | Silver moon/element icon. | high |  | 800e88fc_FUN_800e88fc<br>800e7d0c_FUN_800e7d0c |
| 0x26 | `bend_drop_icon_unresolved_26` | Small dropped-item/category icon visible in the atlas; not reached by the currently resolved lookup table. | low | A generic weapon icon |  |
| 0x27 | `bend_drop_category_icon_weapon_vyse` | Dropped-item Vyse weapon icon selected by lookup code `9`. | high | Weapon icon (Vyse) | 800e96c0_FUN_800e96c0 |
| 0x28 | `bend_drop_category_icon_weapon_aika` | Dropped-item Aika weapon icon selected by lookup code `10`. | high | Weapon icon (Aika) | 800e96c0_FUN_800e96c0 |
| 0x29 | `bend_drop_category_icon_weapon_fina` | Dropped-item Fina weapon icon selected by lookup code `11`. | high | Weapon icon (Fina) | 800e96c0_FUN_800e96c0 |
| 0x2a | `bend_drop_category_icon_weapon_drachma` | Dropped-item Drachma weapon icon selected by lookup code `12`. | high | Weapon icon (Drachma) | 800e96c0_FUN_800e96c0 |
| 0x2b | `bend_drop_category_icon_weapon_enrique` | Dropped-item Enrique weapon icon selected by lookup code `13`. | high | Weapon icon (Enrique) | 800e96c0_FUN_800e96c0 |
| 0x2c | `bend_drop_category_icon_weapon_gilder` | Dropped-item Gilder weapon icon selected by lookup code `14`. | high | Weapon icon (Gilder) | 800e96c0_FUN_800e96c0 |
| 0x2d | `bend_drop_category_icon_armor` | Dropped-item armor icon selected by lookup code `2`. | high | Used for armor | 800e96c0_FUN_800e96c0 |
| 0x2e | `bend_drop_icon_unresolved_2e` | Small dropped-item/category icon visible in the atlas; not reached by the currently resolved lookup table. | low | Reminiscent of an smove icon |  |
| 0x2f | `bend_drop_icon_unresolved_2f` | Small dropped-item/category icon visible in the atlas; not reached by the currently resolved lookup table. | low | Reminiscent of a crew attack icon |  |
| 0x30 | `bend_drop_category_icon_key_item` | Dropped-item key-item icon selected by lookup code `5`. | high | Used for key items | 800e96c0_FUN_800e96c0 |
| 0x31 | `bend_drop_category_icon_ship_main_cannon` | Dropped-item ship main-cannon icon selected by lookup codes `6` and `15`. | high | Used for main ship cannons | 800e96c0_FUN_800e96c0 |
| 0x32 | `bend_drop_category_icon_ship_sub_cannon` | Dropped-item ship sub-cannon icon selected by lookup code `16`. | high | Used for ship subcannons | 800e96c0_FUN_800e96c0 |
| 0x33 | `bend_drop_category_icon_ship_torpedo` | Dropped-item ship torpedo icon selected by lookup code `17`. | high | Used for ship torpedos | 800e96c0_FUN_800e96c0 |
| 0x34 | `bend_drop_category_icon_ship_super_cannon` | Dropped-item ship super-cannon icon selected by lookup code `18`. | high | Used for ship super cannons | 800e96c0_FUN_800e96c0 |
| 0x35 | `bend_drop_category_icon_ship_consumable` | Dropped-item ship consumable icon selected by lookup code `8`. | high | Used for ship consumables | 800e96c0_FUN_800e96c0 |
| 0x36 | `bend_drop_category_icon_ship_accessory` | Dropped-item ship accessory icon selected by lookup code `7`. | high | Used for ship accessories | 800e96c0_FUN_800e96c0 |
| 0x37 | `bend_levelup_count_panel_backing` | Level-up count panel backing variant drawn by `FUN_800e7d0c`. | medium |  | 800e7d0c_FUN_800e7d0c |
| 0x38 | `bend_levelup_count_panel_inner` | Level-up count panel inner/label backing variant drawn by `FUN_800e7d0c`. | medium |  | 800e7d0c_FUN_800e7d0c |
| 0x39 | `bend_levelup_count_panel_underline` | Level-up count panel underline/accent. | medium |  | 800e7d0c_FUN_800e7d0c |
| 0x3a | `bend_levelup_label_large` | Static LEVEL UP!! label for the level-up count panel variant. | high |  | 800e7d0c_FUN_800e7d0c |
| 0x3b | `bend_rankup_panel_backing` | Magic-rank-up panel backing variant drawn by `FUN_800e7d0c`. | medium |  | 800e7d0c_FUN_800e7d0c |
| 0x3c | `bend_rankup_panel_inner` | Magic-rank-up panel inner/label backing variant drawn by `FUN_800e7d0c`. | medium |  | 800e7d0c_FUN_800e7d0c |
| 0x3d | `bend_rankup_panel_underline` | Magic-rank-up panel underline/accent. | medium |  | 800e7d0c_FUN_800e7d0c |
| 0x3e | `bend_rankup_label` | Static RANK UP!! label for magic rank-up display. | high |  | 800e7d0c_FUN_800e7d0c |

## battle/HrsBinPCWin.bin

Source research: `SpiceBin/research/2026-06-20_pcwin_status_window_mapping/README.md`

Companion texture evidence: `battle/PCWindow.mld`. Runtime evidence identifies this as the battle player-character status window used by the action-select battle menu thread. It appears between the SP gauge and command wheel. Pressing X sets worksheet byte 0 to `1`, causing the PCWin child thread to render all active player-character rows; the next button press clears byte 0 back to `0`, returning to current-character mode.

### Record Groups

`battle/HrsBinPCWin.bin` contains 41 indexed records, `0x00..0x28`.

| Record range | Current role | Confidence |
| --- | --- | --- |
| `0x00` | Status window base panel/frame. | High. |
| `0x01..0x06` | Playable-character name labels: Vyse, Aika, Fina, Drachma, Enrique, Gilder. | High. |
| `0x07..0x11` | Numeric glyphs `0..9` plus the slash separator used between current/max stat values. | High. |
| `0x12..0x14` | Static stat labels: HP, MP, and Lv. | High. |
| `0x15..0x1e`, `0x20` | Status-condition icons selected from the worksheet status bitfield. Serialized order is not draw-bit order. | High after Ghidra bit mapping plus visual status-name annotations. |
| `0x1f` | Static HP gauge backing/frame. | High. |
| `0x21` | Dynamic HP gauge fill. `FUN_8002356c` mutates its width and tint from current/max HP before drawing. | High. |
| `0x22..0x27` | Playable-character portraits: Vyse, Aika, Fina, Drachma, Enrique, Gilder. | High. |
| `0x28` | Runtime-colored portrait moon/element marker. `FUN_8002356c` colors it from the worksheet element byte. | High. |

### Worksheet Layout

`FUN_8007c7f8` initializes a 0x3a-byte worksheet and passes it to `FUN_80022f1c`, which creates the `FUN_80022f74` PCWin child thread.

| Worksheet offset | Size | Meaning |
| --- | ---: | --- |
| `0x00` | 1 | Display mode. `0` renders only the current selected player character; `1` renders all active player characters. |
| `0x02 + row * 0x0e` | 2 | Level value. |
| `0x04 + row * 0x0e` | 2 | Current HP value. |
| `0x06 + row * 0x0e` | 2 | Max HP value. |
| `0x08 + row * 0x0e` | 2 | Current MP/spell-use value displayed on the MP row. |
| `0x0a + row * 0x0e` | 2 | Max MP/spell-use value displayed on the MP row. |
| `0x0c + row * 0x0e` | 2 | Status icon bitfield built from `BattleInstance.status_flags` and `battleStateFlags1`. |
| `0x0e + row * 0x0e` | 1 | Element/moon color byte for record `0x28`. |

### One-to-One Records

| Record | Key | Description | Confidence | Human Annotation | Direct consumer functions |
| --- | --- | --- | --- | --- | --- |
| 0x00 | `pcwin_battle_status_window_panel` | Battle player-character status window base panel/frame. | high | Battle status window panel between SP gauge and command wheel. | 80022f74_FUN_80022f74 |
| 0x01 | `pcwin_character_name_vyse` | Vyse name label. | high |  | 80022f74_FUN_80022f74 |
| 0x02 | `pcwin_character_name_aika` | Aika name label. JP texture projection composes `Aika`/katakana Aika from this record. | high |  | 80022f74_FUN_80022f74 |
| 0x03 | `pcwin_character_name_fina` | Fina name label. JP texture projection composes `Fina`/katakana Fina from this record. | high |  | 80022f74_FUN_80022f74 |
| 0x04 | `pcwin_character_name_drachma` | Drachma name label. | high |  | 80022f74_FUN_80022f74 |
| 0x05 | `pcwin_character_name_enrique` | Enrique name label. | high |  | 80022f74_FUN_80022f74 |
| 0x06 | `pcwin_character_name_gilder` | Gilder name label. | high |  | 80022f74_FUN_80022f74 |
| 0x07 | `pcwin_numeric_digit_0` | Numeric glyph 0. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x08 | `pcwin_numeric_digit_1` | Numeric glyph 1. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x09 | `pcwin_numeric_digit_2` | Numeric glyph 2. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x0a | `pcwin_numeric_digit_3` | Numeric glyph 3. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x0b | `pcwin_numeric_digit_4` | Numeric glyph 4. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x0c | `pcwin_numeric_digit_5` | Numeric glyph 5. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x0d | `pcwin_numeric_digit_6` | Numeric glyph 6. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x0e | `pcwin_numeric_digit_7` | Numeric glyph 7. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x0f | `pcwin_numeric_digit_8` | Numeric glyph 8. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x10 | `pcwin_numeric_digit_9` | Numeric glyph 9. | high |  | 80022f74_FUN_80022f74<br>80023fec_FUN_80023fec |
| 0x11 | `pcwin_stat_slash_separator` | Slash separator drawn for `-1` by the numeric glyph helper. | high |  | 80023fec_FUN_80023fec |
| 0x12 | `pcwin_stat_label_hp` | HP label for the bottom stat row. | high |  | 80022f74_FUN_80022f74 |
| 0x13 | `pcwin_stat_label_mp` | MP label for the upper stat row. | high |  | 80022f74_FUN_80022f74 |
| 0x14 | `pcwin_stat_label_lv` | Lv label for the upper stat row. | high |  | 80022f74_FUN_80022f74 |
| 0x15 | `pcwin_status_icon_poison` | Poison status icon selected when worksheet status bit 0 is set from `status_flags & 0x80`. | high | Used for the Poison status | 80022f74_FUN_80022f74 |
| 0x16 | `pcwin_status_icon_unconscious` | Unconscious status icon selected when worksheet status bit 1 is set from `status_flags & 0x100`. | high | Used for the Unconscious status | 80022f74_FUN_80022f74 |
| 0x17 | `pcwin_status_icon_stone` | Stone status icon selected when worksheet status bit 6 is set from `status_flags & 0x4000`. | high | Used for the Stone status | 80022f74_FUN_80022f74 |
| 0x18 | `pcwin_status_icon_sleep` | Sleep status icon selected when worksheet status bit 3 is set from `status_flags & 0x400`. | high | Used for the Sleep status | 80022f74_FUN_80022f74 |
| 0x19 | `pcwin_status_icon_confuse` | Confuse status icon selected when worksheet status bit 4 is set from `status_flags & 0x800`. | high | Used for the Confuse status | 80022f74_FUN_80022f74 |
| 0x1a | `pcwin_status_icon_silence` | Silence status icon selected when worksheet status bit 2 is set from `status_flags & 0x200`. | high | Used for the Silence status | 80022f74_FUN_80022f74 |
| 0x1b | `pcwin_status_icon_fatigue` | Fatigue status icon selected when worksheet status bit 5 is set from `status_flags & 0x1000`. | high | Used for the Fatigue status | 80022f74_FUN_80022f74 |
| 0x1c | `pcwin_status_icon_weakened` | Weakened status icon selected when worksheet status bit 9 is set from `battleStateFlags1` flags `0x20`, `0x40`, `0x80`, or `0x100`. | high | Used for the Weakened status | 80022f74_FUN_80022f74 |
| 0x1d | `pcwin_status_icon_regeneration` | Regeneration status icon selected when worksheet status bit 10 is set from `battleStateFlags1 & 0x200`. | high | Used for the Regeneration status | 80022f74_FUN_80022f74 |
| 0x1e | `pcwin_status_icon_strengthened` | Strengthened status icon selected when worksheet status bit 7 is set from `battleStateFlags1` flags `0x1`, `0x2`, `0x8`, or `0x10`. | high | Used for the Strengthened status | 80022f74_FUN_80022f74 |
| 0x1f | `pcwin_hp_gauge_backing` | Static HP gauge backing/frame. | high |  | 80022f74_FUN_80022f74 |
| 0x20 | `pcwin_status_icon_hastened` | Hastened status icon selected when worksheet status bit 8 is set from `battleStateFlags1 & 0x4`. | high | Used for the Hastened status | 80022f74_FUN_80022f74 |
| 0x21 | `pcwin_hp_gauge_fill` | Dynamic HP gauge fill; width and tint are derived from current HP divided by max HP. | high |  | 8002356c_FUN_8002356c |
| 0x22 | `pcwin_portrait_vyse` | Vyse portrait. | high |  | 80022f74_FUN_80022f74 |
| 0x23 | `pcwin_portrait_aika` | Aika portrait. | high |  | 80022f74_FUN_80022f74 |
| 0x24 | `pcwin_portrait_fina` | Fina portrait. | high |  | 80022f74_FUN_80022f74 |
| 0x25 | `pcwin_portrait_drachma` | Drachma portrait. | high |  | 80022f74_FUN_80022f74 |
| 0x26 | `pcwin_portrait_enrique` | Enrique portrait. | high |  | 80022f74_FUN_80022f74 |
| 0x27 | `pcwin_portrait_gilder` | Gilder portrait. | high |  | 80022f74_FUN_80022f74 |
| 0x28 | `pcwin_portrait_moon_color_marker` | Runtime-colored portrait moon/element marker. Element values map to green, red, purple, blue, yellow, or silver/neutral. | high |  | 8002356c_FUN_8002356c |

## battle/HrsBinCW.bin

Source research: `SpiceBin/research/2026-06-20_hrsbin_cw_record_mapping/HrsBinCW_record_mapping_draft.md`

Companion texture evidence: `battle/command.mld`, with possible nearby cursor material from `battle/btlcursor.mld`. Runtime evidence identifies this file as the battle command/menu layout bank, not only the command wheel.

### Record Groups

`battle/HrsBinCW.bin` contains 104 indexed records, `0x00..0x67`. The command
wheel records are only one subset. Current consumer traces support exposing the
file as a broader battle command/menu UI layout bank.

| Record range | Current role | Confidence |
| --- | --- | --- |
| `0x00..0x08` | Battle party/status panel frame pieces and SP/command/status panel components. `0x03` is the animated SP gauge fill strip; `0x04` is a fixed status/SP panel strip. | High after direct `FUN_80027c04` use plus SP-gauge visual annotation. |
| `0x09..0x0a` | Command wheel background/base pieces. | High. |
| `0x0b..0x12` | Normal-size command icons, including Crew normal icon at `0x12`. | High. |
| `0x13..0x14` | Command-wheel character-transition frames used during active-character command wheel transitions. | High after Ghidra transition-state consumers plus screenshot sequence in local research. |
| `0x15..0x1c` | Selected/highlighted command icons, including Crew selected icon at `0x1c`. | High. |
| `0x1d..0x20` | Command wheel overlay/highlight pieces. | Medium. |
| `0x21..0x22` | Command/list window backing records. | Medium. |
| `0x23..0x2c` | Digit glyph records `0..9`. | High. |
| `0x2d..0x31` | Command row/list window variants by visible line count. | High for list-window height variants from direct list renderer use. |
| `0x32..0x39` | Command text labels. | High. |
| `0x3a..0x3d` | List/window accents or active-row backing pieces. | Medium. |
| `0x3e..0x43` | Green, red, purple, blue, yellow, and silver magic category tabs. | High. |
| `0x44..0x4c` | Item/equipment category tabs: consumables, per-character weapons, armor, and accessories. Fixed records and party-composition-selected records are both now mapped. | High. |
| `0x4d..0x52` | Green, red, purple, blue, yellow, and silver magic list icons selected by list/item type cases `3..8`. | High. |
| `0x53..0x58` | Per-character weapon/S-Move list icons selected from `PARTY_COMPOSITION[selectedInstance_2]`: Vyse, Aika, Fina, Drachma, Enrique, and Gilder. | High. |
| `0x59..0x5b` | Consumable, armor, and accessory item list icons selected by cases `9`, `0x0b`, `0x0c`, or default. | High. |
| `0x5c..0x5e` | List navigation arrows and selected-row highlight/cursor pieces. | High. |
| `0x5f..0x66` | Weapon color selection strip: backing/window, green/red/purple/blue/yellow/silver labels, and separator/cursor bar. | High for window and moon labels; medium for separator. |
| `0x67` | Special party/status panel record. | Medium. |

### One-to-One Records

| Record | Key | Description | Confidence | Human Annotation | Direct consumer functions |
| --- | --- | --- | --- | --- | --- |
| 0x00 | `command_sp_gauge_top_left_cap` | SP gauge/status panel top-left cap. | high | This is for the SP gauge | 80027c04_FUN_80027c04 |
| 0x01 | `command_sp_gauge_top_bar` | SP gauge/status panel long top bar. | high | This is for the SP gauge | 80027c04_FUN_80027c04 |
| 0x02 | `command_sp_gauge_top_right_cap` | SP gauge/status panel top-right cap. | high | This is for the SP gauge | 80027c04_FUN_80027c04 |
| 0x03 | `command_sp_gauge_fill_strip` | Animated SP gauge fill strip. | high | This is for the SP gauge | 80027c04_FUN_80027c04 |
| 0x04 | `command_sp_gauge_fixed_panel_strip` | Fixed SP gauge/status panel strip. | high | This is for the SP gauge | 80027c04_FUN_80027c04 |
| 0x05 | `command_sp_gauge_character_panel_backing` | SP gauge/status character panel backing. | high | This is for the SP gauge | 80027c04_FUN_80027c04 |
| 0x06 | `command_sp_gauge_right_bracket` | SP gauge/status panel right bracket. | high | This is for the SP gauge | 80027c04_FUN_80027c04 |
| 0x07 | `command_sp_gauge_long_underlay` | SP gauge/status panel long underlay bar. | high | This is for the SP gauge | 80027c04_FUN_80027c04 |
| 0x08 | `command_sp_gauge_blue_ring` | SP gauge/status blue ring icon. | high | This is for the SP gauge | 80026fec_FUN_80026fec<br>80027c04_FUN_80027c04 |
| 0x09 | `command_wheel_background_disc` | Command wheel background/base disc. | high | This is in the background | 80025624_FUN_80025624 |
| 0x0a | `command_wheel_current_command_foreground_overlay` | Command wheel foreground overlay drawn over the current command area. | high | This appears to be in the foreground overlaying the current command | 80025624_FUN_80025624 |
| 0x0b | `command_icon_attack_normal` | Attack normal icon | high |  | 80025624_FUN_80025624<br>80026fec_FUN_80026fec<br>800277f4_FUN_800277f4<br>80027c04_FUN_80027c04 |
| 0x0c | `command_icon_guard_normal` | Guard normal icon | high |  | 80025624_FUN_80025624<br>80026fec_FUN_80026fec<br>80027c04_FUN_80027c04 |
| 0x0d | `command_icon_focus_normal` | Focus normal icon | high |  | 80025624_FUN_80025624<br>80026fec_FUN_80026fec<br>80027c04_FUN_80027c04 |
| 0x0e | `command_icon_item_normal` | Item normal icon | high |  | 80025624_FUN_80025624<br>80026fec_FUN_80026fec<br>80027c04_FUN_80027c04 |
| 0x0f | `command_icon_magic_normal` | Magic normal icon | high |  | 80025624_FUN_80025624<br>80026fec_FUN_80026fec<br>80027c04_FUN_80027c04 |
| 0x10 | `command_icon_run_normal` | Run normal icon | high |  | 80025624_FUN_80025624<br>80026fec_FUN_80026fec<br>80027c04_FUN_80027c04 |
| 0x11 | `command_icon_s_move_normal` | S-Move normal icon | high |  | 80025624_FUN_80025624<br>80026fec_FUN_80026fec<br>80027c04_FUN_80027c04 |
| 0x12 | `command_icon_crew_normal` | Crew normal icon. | high |  | 80025624_FUN_80025624<br>80026fec_FUN_80026fec<br>80027c04_FUN_80027c04 |
| 0x13 | `command_wheel_character_transition_frame_1` | Command wheel character transition frame 1. | high | For transition between characters | 80026fec_FUN_80026fec<br>80027484_FUN_80027484 |
| 0x14 | `command_wheel_character_transition_frame_2` | Command wheel character transition frame 2. | high | For transition between characters | 80026fec_FUN_80026fec<br>80027484_FUN_80027484 |
| 0x15 | `command_icon_attack_selected` | Attack selected icon | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20 |
| 0x16 | `command_icon_guard_selected` | Guard selected icon | high |  | 80026b20_FUN_80026b20 |
| 0x17 | `command_icon_focus_selected` | Focus selected icon | high |  | 80026b20_FUN_80026b20 |
| 0x18 | `command_icon_item_selected` | Item selected icon | high |  | 80026b20_FUN_80026b20 |
| 0x19 | `command_icon_magic_selected` | Magic selected icon | high |  | 80026b20_FUN_80026b20 |
| 0x1a | `command_icon_run_selected` | Run selected icon | high |  | 80026b20_FUN_80026b20 |
| 0x1b | `command_icon_s_move_selected` | S-Move selected icon | high |  | 80026b20_FUN_80026b20 |
| 0x1c | `command_icon_crew_selected` | Crew selected icon. | high |  | 80026b20_FUN_80026b20 |
| 0x1d | `command_wheel_overlay_a` | Command wheel overlay/highlight A | medium |  | 80025624_FUN_80025624 |
| 0x1e | `command_wheel_overlay_b` | Command wheel overlay/highlight B | medium |  | 80025624_FUN_80025624 |
| 0x1f | `command_wheel_overlay_c` | Command wheel overlay/highlight C | medium |  | 80025624_FUN_80025624 |
| 0x20 | `command_wheel_overlay_d` | Command wheel overlay/highlight D | medium |  | 80025624_FUN_80025624 |
| 0x21 | `command_list_vertical_gradient_backing` | Command/list vertical gradient backing | medium |  | 8002468c_FUN_8002468c |
| 0x22 | `command_list_large_window_frame` | Command/list large window frame | medium |  | 8002468c_FUN_8002468c |
| 0x23 | `command_numeric_digit_0` | Command UI digit glyph 0. | high |  | 80027c04_FUN_80027c04 |
| 0x24 | `command_numeric_digit_1` | Command UI digit glyph 1. | high |  | 80027c04_FUN_80027c04 |
| 0x25 | `command_numeric_digit_2` | Command UI digit glyph 2. | high |  | 80027c04_FUN_80027c04 |
| 0x26 | `command_numeric_digit_3` | Command UI digit glyph 3. | high |  | 80027c04_FUN_80027c04 |
| 0x27 | `command_numeric_digit_4` | Command UI digit glyph 4. | high |  | 80027c04_FUN_80027c04 |
| 0x28 | `command_numeric_digit_5` | Command UI digit glyph 5. | high |  | 80027c04_FUN_80027c04 |
| 0x29 | `command_numeric_digit_6` | Command UI digit glyph 6. | high |  | 80027c04_FUN_80027c04 |
| 0x2a | `command_numeric_digit_7` | Command UI digit glyph 7. | high |  | 80027c04_FUN_80027c04 |
| 0x2b | `command_numeric_digit_8` | Command UI digit glyph 8. | high |  | 80027c04_FUN_80027c04 |
| 0x2c | `command_numeric_digit_9` | Command UI digit glyph 9. | high |  | 80027c04_FUN_80027c04 |
| 0x2d | `command_list_window_row_count_1` | Command row/list window variant, one-line height. | high |  | 8002468c_FUN_8002468c |
| 0x2e | `command_list_window_row_count_2` | Command row/list window variant, two-line height. | high |  | 800240ec_FUN_800240ec<br>8002468c_FUN_8002468c |
| 0x2f | `command_list_window_row_count_3` | Command row/list window variant, three-line height. | high |  | 800240ec_FUN_800240ec<br>8002468c_FUN_8002468c |
| 0x30 | `command_list_window_row_count_4` | Command row/list window variant, four-line height. | high |  | 8002468c_FUN_8002468c |
| 0x31 | `command_list_window_row_count_5` | Command row/list window variant, five-line height. | high |  | 8002468c_FUN_8002468c |
| 0x32 | `command_label_attack` | Attack text label | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20 |
| 0x33 | `command_label_guard` | Guard text label | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20 |
| 0x34 | `command_label_focus` | Focus text label | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20 |
| 0x35 | `command_label_item` | Item text label | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20 |
| 0x36 | `command_label_magic` | Magic text label | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20 |
| 0x37 | `command_label_run` | Run text label | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20 |
| 0x38 | `command_label_s_move` | S-Move text label | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20 |
| 0x39 | `command_label_crew` | Crew text label | high |  | 80025624_FUN_80025624<br>80026b20_FUN_80026b20<br>80026fec_FUN_80026fec |
| 0x3a | `wide_list_frame_332x40` | Wide list frame, 332x40 | medium |  | 8002468c_FUN_8002468c |
| 0x3b | `wide_list_frame_608x70` | Wide list frame, 608x70 | medium |  | 800240ec_FUN_800240ec<br>8002468c_FUN_8002468c |
| 0x3c | `thin_horizontal_list_highlight` | Thin horizontal list highlight/cursor | medium |  | 8002468c_FUN_8002468c |
| 0x3d | `thick_horizontal_list_highlight` | Thick horizontal list highlight/cursor | medium |  | 8002468c_FUN_8002468c |
| 0x3e | `command_magic_category_tab_green` | Green magic category tab for the magic command menu. | high | A tab used when selecting a category of magic for the magic command submenu - green magic | 800240ec_FUN_800240ec |
| 0x3f | `command_magic_category_tab_red` | Red magic category tab for the magic command menu. | high | A tab used when selecting a category of magic for the magic command submenu - red magic | 800240ec_FUN_800240ec |
| 0x40 | `command_magic_category_tab_purple` | Purple magic category tab for the magic command menu. | high | A tab used when selecting a category of magic for the magic command submenu - purple magic | 800240ec_FUN_800240ec |
| 0x41 | `command_magic_category_tab_blue` | Blue magic category tab for the magic command menu. | high | A tab used when selecting a category of magic for the magic command submenu - blue magic | 800240ec_FUN_800240ec |
| 0x42 | `command_magic_category_tab_yellow` | Yellow magic category tab for the magic command menu. | high | A tab used when selecting a category of magic for the magic command submenu - yellow magic | 800240ec_FUN_800240ec |
| 0x43 | `command_magic_category_tab_silver` | Silver magic category tab for the magic command menu. | high | A tab used when selecting a category of magic for the magic command submenu - silver magic | 800240ec_FUN_800240ec |
| 0x44 | `command_item_category_tab_consumable` | Fixed consumable tab for the item command menu. | high | A tab used for a category of items for the item command submenu - Consumables | 800240ec_FUN_800240ec |
| 0x45 | `command_item_category_tab_weapon_vyse` | Vyse weapon category tab selected from party-composition case 0 in the item command menu. | high | A tab used for a category of items for the item command submenu - Weapons (Vyse only) | 800240ec_FUN_800240ec<br>8002468c_FUN_8002468c |
| 0x46 | `command_item_category_tab_weapon_aika` | Aika weapon category tab selected from party-composition case 1 in the item command menu. | high | A tab used for a category of items for the item command submenu - Weapons (Aika only) | 8002468c_FUN_8002468c |
| 0x47 | `command_item_category_tab_weapon_fina` | Fina weapon category tab selected from party-composition case 2 in the item command menu. | high | A tab used for a category of items for the item command submenu - Weapons (Fina only) | 8002468c_FUN_8002468c |
| 0x48 | `command_item_category_tab_weapon_drachma` | Drachma weapon category tab selected from party-composition case 3 in the item command menu. | high | A tab used for a category of items for the item command submenu - Weapons (Drachma only) | 8002468c_FUN_8002468c |
| 0x49 | `command_item_category_tab_weapon_enrique` | Enrique weapon category tab selected from party-composition case 4 in the item command menu. | high | A tab used for a category of items for the item command submenu - Weapons (Enrique only) | 8002468c_FUN_8002468c |
| 0x4a | `command_item_category_tab_weapon_gilder` | Gilder weapon category tab selected from party-composition case 5 in the item command menu. | high | A tab used for a category of items for the item command submenu - Weapons (Gilder only) | 8002468c_FUN_8002468c |
| 0x4b | `command_item_category_tab_armor` | Fixed armor tab for the item command menu. | high | A tab used for a category of items for the item command submenu - Armor | 800240ec_FUN_800240ec |
| 0x4c | `command_item_category_tab_accessory` | Fixed accessory tab for the item command menu. | high | A tab used for a category of items for the item command submenu - Accessory | 800240ec_FUN_800240ec |
| 0x4d | `command_magic_list_icon_green` | Green magic list-entry icon selected by command-menu case `3`. | high | Icon for green magic | 800250f4_FUN_800250f4 |
| 0x4e | `command_magic_list_icon_red` | Red magic list-entry icon selected by command-menu case `4`. | high | Icon for red magic | 800250f4_FUN_800250f4 |
| 0x4f | `command_magic_list_icon_purple` | Purple magic list-entry icon selected by command-menu case `5`. | high | Icon for purple magic | 800250f4_FUN_800250f4 |
| 0x50 | `command_magic_list_icon_blue` | Blue magic list-entry icon selected by command-menu case `6`. | high | Icon for blue magic | 800250f4_FUN_800250f4 |
| 0x51 | `command_magic_list_icon_yellow` | Yellow magic list-entry icon selected by command-menu case `7`. | high | Icon for yellow magic | 800250f4_FUN_800250f4 |
| 0x52 | `command_magic_list_icon_silver` | Silver magic list-entry icon selected by command-menu case `8`. | high | Icon for silver magic | 800250f4_FUN_800250f4 |
| 0x53 | `command_character_weapon_smove_icon_vyse` | Vyse weapon/S-Move list-entry icon. | high | Icon for weapon (Vyse) used for both items and smove menu | 800250f4_FUN_800250f4 |
| 0x54 | `command_character_weapon_smove_icon_aika` | Aika weapon/S-Move list-entry icon. | high | Icon for weapon (Aika) used for both items and smove menu | 800250f4_FUN_800250f4 |
| 0x55 | `command_character_weapon_smove_icon_fina` | Fina weapon/S-Move list-entry icon. | high | Icon for weapon (Fina) used for both items and smove menu | 800250f4_FUN_800250f4 |
| 0x56 | `command_character_weapon_smove_icon_drachma` | Drachma weapon/S-Move list-entry icon. | high | Icon for weapon (Drachma) used for both items and smove menu | 800250f4_FUN_800250f4 |
| 0x57 | `command_character_weapon_smove_icon_enrique` | Enrique weapon/S-Move list-entry icon. | high | Icon for weapon (Enrique) used for both items and smove menu | 800250f4_FUN_800250f4 |
| 0x58 | `command_character_weapon_smove_icon_gilder` | Gilder weapon/S-Move list-entry icon. | high | Icon for weapon (Gilder) used for both items and smove menu | 800250f4_FUN_800250f4 |
| 0x59 | `command_item_list_icon_consumable` | Consumable list-entry icon selected by command-menu case `9` or default. | high | Icon for consumable | 800250f4_FUN_800250f4 |
| 0x5a | `command_item_list_icon_armor` | Armor list-entry icon selected by command-menu case `0x0b`. | high | Icon for armor | 800250f4_FUN_800250f4 |
| 0x5b | `command_item_list_icon_accessory` | Accessory list-entry icon selected by command-menu case `0x0c`. | high | Icon for accessory | 800250f4_FUN_800250f4 |
| 0x5c | `command_list_navigation_left_arrow` | Command list navigation left arrow. | high |  | 8002468c_FUN_8002468c |
| 0x5d | `command_list_navigation_right_arrow` | Command list navigation right arrow. | high |  | 8002468c_FUN_8002468c |
| 0x5e | `command_list_selected_row_highlight` | Command list selected-row highlight/cursor. | high |  | 8002468c_FUN_8002468c |
| 0x5f | `command_weapon_color_picker_window` | Weapon color selection strip window/backing. | high | Used when selecting weapon color after choosing a weapon | 80022b04_FUN_80022b04 |
| 0x60 | `command_weapon_color_picker_green_moon_label` | Green moon label for weapon color selection. | high | Used when selecting weapon color after choosing a weapon | 80022b04_FUN_80022b04 |
| 0x61 | `command_weapon_color_picker_red_moon_label` | Red moon label for weapon color selection. | high | Used when selecting weapon color after choosing a weapon | 80022b04_FUN_80022b04 |
| 0x62 | `command_weapon_color_picker_purple_moon_label` | Purple moon label for weapon color selection. | high | Used when selecting weapon color after choosing a weapon | 80022b04_FUN_80022b04 |
| 0x63 | `command_weapon_color_picker_blue_moon_label` | Blue moon label for weapon color selection. | high | Used when selecting weapon color after choosing a weapon | 80022b04_FUN_80022b04 |
| 0x64 | `command_weapon_color_picker_yellow_moon_label` | Yellow moon label for weapon color selection. | high | Used when selecting weapon color after choosing a weapon | 80022b04_FUN_80022b04 |
| 0x65 | `command_weapon_color_picker_silver_moon_label` | Silver moon label for weapon color selection. | high | Used when selecting weapon color after choosing a weapon | 80022b04_FUN_80022b04 |
| 0x66 | `command_weapon_color_menu_separator_bar` | Weapon color selection separator/cursor bar. | medium |  | 80022b04_FUN_80022b04 |
| 0x67 | `battle_status_special_panel_record` | Special battle status panel record | medium |  | 80027c04_FUN_80027c04 |
