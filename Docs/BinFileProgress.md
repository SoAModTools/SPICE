# BIN File Progress

This living document tracks current `.bin` research status, runtime evidence, companion asset hypotheses, investigation workflow, and open questions. Durable binary field layouts live in `Docs/BinFileLayout.md`, and one-to-one record annotations live in `Docs/BinFileRecords.md`.

## Current Status

The `.bin` extension is not one proven single format yet. Current evidence supports at least one important family: HRSBin-style indexed UI/render layout tables. These payloads appear both as named members inside MLL archives and as standalone loose files.

The parser surface preserves the original bytes, records diagnostics, and runs the indexed UI/render layout probe described in `Docs/BinFileLayout.md`. It is still a scaffold for research and downstream schema work, not a claim that every `.bin` file has been decoded.

Do not classify every `.bin` file as the indexed layout family. The current negative example is `field/wmaparea.BIN`, which does not match the indexed-table probe and is traced as a fixed-size world-map area/menu table.

Working usage hypothesis: the known `.bin` files may all be UI/menu-adjacent resources, even when they do not share the same binary family. The HRSBin-style indexed files describe UI/render layout elements; `battle/HrsBinCW.bin` is battle command/menu/window layout data, `battle/HrsBinPCWin.bin` is battle player-character status window layout data, `field/hrs_bend.bin` is battle-end screen layout data, and `field/HrsBin_Status.mll` embeds the field X-menu/status-menu layout banks.

The loose `field/HRSBin.bin` remains a valid 34-record HRSBin-style layout, but its runtime consumer is not proven by the current Ghidra trace. Current DOL searches find `/field/HrsBin_Hakken.mll`, `/field/HrsBin_sbp.mll`, and `/field/HrsBin_Status.mll` loader matches, but no match for `HRSBin.bin` or `HRSBin.mld`.

For the confirmed field X-menu banks in `HrsBin_Status.mll`, every record reached by the current Ghidra accessor scans has a promoted one-to-one row in `Docs/BinFileRecords.md`. Remaining serialized embedded records are tracked there as explicit no-discovered-consumer rows.

## Companion Texture And Material Context Status

Current loose HRSBin-style `.bin` samples do not contain embedded GVR texture chunks. Instead, their fixed-data tables look like texture/context selector plus normalized UV/source-rectangle metadata, while nearby MLD/MLL companion assets contain the actual GCIX/GVRT texture chunks.

Companion-bank resolution is context-sensitive. Fixed-data word `0x00` should not be treated as a direct ordinal into extracted GVR files by archive offset. The checked direct draw path receives texture/material state through the active render context and does not read that word. The checked descriptor/queue path preserves this word as a short and later passes it to `FUN_80298fe4`, which indexes the active context entry list.

The current texture-resolution model is: fixed-data offsets `0x04..0x10` are normalized UV/source coordinates, while the visible source texture is selected through the active render/material context. Direct submitters bind texture state through `FUN_80299040` and `FUN_80297dc8`. Descriptor/queue submitters set the active context through `FUN_80299040`, then call `FUN_80298fe4(fixedDataWord0)` to bind a context entry.

The common queued submitter wrappers `FUN_801d52a8` and `FUN_801d5210` prove that many queued status draws use a different active context from the direct `DAT_80347568` path. Both load the context argument from `DAT_80347614->+4->+0x20`; `FUN_801d66d4` initializes `DAT_80347614` by loading `/field/Sprite00.mld` and registering its `TKStringList`, and `FUN_801d668c` later unregisters/frees the same context.

Current proven/visually supported Sprite00 context selector mappings for `field/HrsBin_Status.mll` status records:

| Fixed-data word0 selector | `/field/Sprite00.mld` texture | Preview evidence |
| ---: | --- | --- |
| `0` | `ts000111` | Stat labels and companion stat fragments such as `Attack`, `Defense`, `Will`, `Quick`, `Power`, `Vigor`, `Spirit`, and `Agile`. |
| `1` | `ts000112` | Compact digit records `0x05..0x0e` render as `0..9`; also moon labels, selector strips, detail markers, and some option/value labels. |
| `4` | `ts000116` | Options choice panel dynamic bar fragments. |
| `5` | `ts000124` | `MAXHP`, `MAXMP`, `Rank`, `Next`, and `MAXSpirit` labels. |
| `6` | `ts000201` | Options choice panel active selector icon. |
| `4,5` | `ts000116` and `ts000124` | Multi-element `Limit`/ship weapon stat extra label record uses both selectors. |

The same selector rule resolves `StaDeco.bin` wide digit records `0x04..0x0d`: all use selector `4` and render as digits `0..9` on `Sprite00.mld` texture `ts000116`. The queued `StaDeco.bin` common frame/deco/accent records also resolve through the Sprite00 context: records `0x00`, `0x01`, `0x02`, `0x03`, and `0x0e..0x11` render coherently as frame/rule/portrait-deco/selector-accent art against Sprite00 selectors `1..4`.

For the direct submitter path, the current audit proves non-Sprite rows using the default `/field/ts000110.gvr` context through immediate direct submitters, render-queue copies that drain as direct mode `0`/`1`, branch-selected direct draws, object slot groups `0` and `1`, and object primary `param[0x11]` record pointers. These wrappers pass `&DAT_80347568` to their lower renderers; material initializer functions set `DAT_80347568 = &DAT_80311894` and register material id `0x1869e` from `/field/ts000110.gvr`.

Supporting local artifacts include `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_context_mapping_audit.md`, `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_material_diagnostics/status_preview_context_mapping_audit.tsv`, `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_material_diagnostics/HrsBin_Status_material_aware_contact_sheet.png`, `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_material_diagnostics/stasprite00_selector_context_groups.png`, and `SpiceBin/research/2026-06-20_field_hrsbin_menu_controller/status_preview_material_diagnostics/non_sprite00_ts000110_direct_context_candidates.png`.

## High-Confidence Companion Examples

| `.bin` payload | Companion bank | Evidence |
| --- | --- | --- |
| `battle/HrsBinPCWin.bin` | `battle/PCWindow.mld` | Battle preload setup initializes the PCWin indexed object from cache index 2 and loads `PCWindow.mld` from nearby cache index 3. JP `HrsBinPCWin.bin` records `1..6` align with playable-character name atlas rectangles when projected onto the JP PCWindow/HRSBin-style katakana texture. |
| `battle/HrsBinCW.bin` | `battle/command.mld`, possibly `battle/btlcursor.mld` | Battle preload setup loads `command.mld` immediately before initializing the CW indexed object, and the texture contents match battle command/menu UI art. |
| `field/hrs_bend.bin` | `field/ts000110.gvr` through the default field/UI material bank | Runtime draw helpers pass the default field/UI bank initialized from `ts000110.gvr`; visual inspection identifies the sheet as the battle-end sprite sheet. |
| `field/HrsBin_Status.mll` members `0..3` | Embedded member 4 `ts0009.mld` plus active field/menu texture state, with `/field/HrsBin_sbp.mll` preloaded and `/field/Sprite00.mld` loaded at menu open | `menu_listener@801920ac` loads `/field/HrsBin_Status.mll`; `FUN_801926b8` loads member 4 and initializes members `0..3`; queued wrapper paths through `FUN_801d52a8` and `FUN_801d5210` supply the active context from `DAT_80347614->+4->+0x20`. |
| `field/wanted.mll` members `0..1` | Member 2 texture bank plus runtime poster MLD `field/wanted_%02d{a,b}.mld` | Wanted viewer state `19` calls `FUN_801866b4`, which loads `/field/wanted.mll`, registers member 2, and initializes members `0` and `1` into `DAT_803470dc` and `DAT_803470d8`; `FUN_80185c00` loads and frees the current poster MLD. |
| Embedded HRSBin-style MLL members | Member/container-local MLL material bank | MLL payloads carry nearby or embedded texture payloads and are passed into the same indexed object initializer from member-local buffers. |

## Regional Loose Corpus Validation

The US, EU, and JP loose-file scans all found the same family split: five loose `.bin` files, four AKLZ-decoded indexed HRSBin-style positives, and one non-indexed `field/wmaparea.BIN`. No decode errors or indexed-probe warnings were reported.

| Region | File | Raw size | Decoded size | Indexed record count | Notes |
| --- | --- | ---: | ---: | ---: | --- |
| US | `battle/HrsBinCW.bin` | 5233 | 21788 | 104 | Battle command/window layout. |
| EU | `battle/HrsBinCW.bin` | 5281 | 21972 | 104 | Same record count, larger decoded data. |
| JP | `battle/HrsBinCW.bin` | 5157 | 21696 | 104 | Same record count, smaller decoded data. |
| US/EU | `battle/HrsBinPCWin.bin` | 1606 | 5704 | 41 | US and EU match in scan metadata. |
| JP | `battle/HrsBinPCWin.bin` | 1637 | 6032 | 41 | Same record count, larger decoded data and different offsets. |
| US/EU/JP | `field/HRSBin.bin` | 1066 | 3876 | 34 | Scan metadata matches across all three regions. |
| US/EU/JP | `field/hrs_bend.bin` | 4489 | 17508 | 63 | Scan metadata matches across all three regions. |
| US/EU | `field/wmaparea.BIN` | 2016 | 2016 | n/a | Non-indexed, uncompressed in US/EU. |
| JP | `field/wmaparea.BIN` | 454 | 2016 | n/a | Non-indexed after AKLZ decode. |

Exporter implication: record counts and the family split are stable across regions, but offsets, decoded sizes, and data content can differ. Preserve region-specific data rather than normalizing to US.

## Runtime Path References

- MLL embedded payloads use `FUN_801d6998` through paths such as `/field/HrsBin_Hakken.mll`, `/field/hrs_wmap.mll`, `/field/HrsBin_sbp.mll`, `/field/HrsBin_Status.mll`, and `/field/wanted.mll`.
- The field X-menu controller is `menu_listener@801920ac`. It opens on `RawControllers[DAT_80347154]->newPresses & 0x400`, loads `/field/HrsBin_Status.mll`, calls `FUN_801926b8`, and creates child callback `FUN_80191ec8`.
- `FUN_801926b8` loads member 4 (`ts0009.mld`) as the texture/model bank and initializes members 0 (`StaCard.bin`), 1 (`StaDeco.bin`), 2 (`StaPaper.bin`), and 3 (`StaSprite00.bin`) as indexed layout objects.
- The field X-menu root state callback `FUN_8018394c` routes selected values `0..3` to party/status state `3`, selected value `4` to ship-equipment state `0xb`, and selected value `5` to More-menu state `2`.
- More-menu dispatcher `FUN_801851c8` routes value `2` to wanted viewer state `0x13`, which loads `/field/wanted.mll`, initializes its member 0 and 1 indexed BIN objects, registers member 2 as a texture bank, and dynamically loads `field/wanted_%02d{a,b}.mld` for the current poster texture.
- A whole-program xref audit of `FUN_801d68c4` found 569 status-bank accessor calls. Local PowerPC argument backtracking resolved every status-bank record index and found the same 204 unique reached records as the focused pass: 12 `StaCard`, 18 `StaDeco`, 101 `StaPaper`, and 73 `StaSprite00` records.
- Direct non-accessor references to the four field status globals are limited to lifecycle functions: `FUN_801926b8` initializes the MLL member banks and `FUN_801925d8` frees/clears them.
- A structural scan of the known indexed-object entry points did not find a loose `field/HRSBin.bin` consumer. `FUN_801d6998` has 19 current xrefs and covers the known MLL member callers; `FUN_801d6b58` has one current xref, `FUN_800e3594` for `/field/HRS_BEND.BIN`.
- A lower-level direct caller audit of `loadFileFromPath@801cc3c4` exported 17 callsites in 13 unique caller functions. It found direct field loads for `/field/ts000110.gvr`, `/field/wmaparea.bin`, and `/field/wmaparea.BIN`, plus one constructed field path for `sr_%03d%c.tec`; it did not find a direct loose `field/HRSBin.bin` or `field/HRSBin.mld` load path.
- `field/wmaparea.BIN` appears through `/field/wmaparea.bin` and `/field/wmaparea.BIN`, but those callers use `loadFileFromPath` and direct copies/state setup, not the indexed-object initializers. Current notes identify a `0x7e0`-byte copy into `DAT_80346e14 + 0x47c`.
- The battle strings `/battle/hrsbincw.bin` and `/battle/hrsbinpcwin.bin` are entries 1 and 2 in the 16-entry battle preload/cache table rooted at `PTR_s__battle_command_mld_802f91f8`. `FUN_801d67dc` initializes `DAT_80347628` from cache index 1 for `battle/HrsBinCW.bin` and `DAT_8034762c` from cache index 2 for `battle/HrsBinPCWin.bin`; the same function loads nearby companions `command.mld` and `pcwindow.mld`.

## Usage Hypotheses

`Docs/BinFileRecords.md` is the record-identity authority for assessed files. Rows without promoted one-to-one keys should remain explicitly unmapped or provisional until supported by consumer evidence, runtime screenshots, or proof that the serialized record is unused.

| File or family | Hypothesized purpose |
| --- | --- |
| HRSBin-style indexed layout files | UI/render layout elements. |
| `field/HrsBin_Status.mll` embedded members | Confirmed field X-menu/status-menu indexed layout banks. Members 0..3 are `StaCard.bin`, `StaDeco.bin`, `StaPaper.bin`, and `StaSprite00.bin`; member 4 `ts0009.mld` is loaded with the menu. |
| `field/HRSBin.bin` | Loose 34-record HRSBin-style layout with identified companion `field/HRSBin.mld`; not currently proven to be the field X-menu record bank. |
| `battle/HrsBinCW.bin` | Confirmed battle command/menu/window UI layout: command windows, list rows, category/weapon/item/spell icons, cursor/arrow pieces, and party selection/status panels. Exact record names remain provisional. |
| `battle/HrsBinPCWin.bin` | Confirmed battle player-character status window layout: base panel, character-specific name/portrait pieces, Lv/HP/MP stat rows, dynamic HP gauge, runtime-colored element marker, and status-condition icons. |
| `field/hrs_bend.bin` | Confirmed battle-end result screen indexed UI layout. Draw-side evidence routes its quads through the default field/UI material bank initialized from `field/ts000110.gvr`, and visual inspection identifies that texture as the battle-end sprite sheet. |
| `field/wmaparea.BIN` | World-map area/menu table tied to world-map flow. Separate fixed-size table family from HRSBin-style indexed files. |

## Field X-Menu Controller Progress

The initial field X-menu selector/stat carousel is controlled by `FUN_80183d90`. The callback checks controller code `7` for rightward carousel movement and controller code `6` for leftward movement. The current carousel slot is stored in root object field `+0x44`; the previous slot is copied to `+0x48` before animation. Code `7` increments `+0x44` and wraps `5 -> 0`, while code `6` decrements it and wraps `-1 -> 4`. The same input updates bottom page-toggle field `+0x14` with a two-state wrap.

| Slot | Root stat-window role |
| ---: | --- |
| `0` | Attack, Defense, Will, Magic Defense |
| `1` | Hit %, Dodge %, Quick |
| `2` | Weapon and armor equipment |
| `3` | Accessory equipment |
| `4` | Total EXP and next/needed EXP |

`FUN_80199024` allocates these five child objects for each active party member, and `FUN_8018c00c` installs the per-slot record payloads. Promoted record-level keys and object ranges live in `Docs/BinFileRecords.md`.

The root selector's accept path is separate from the left/right carousel logic. In `FUN_8018394c`, selected values `0..3` enter the party/status page hub (`DAT_80347110 = 3`), selected value `4` enters the ship-equipment chooser (`DAT_80347110 = 0xb`), and selected value `5` enters the second-page More menu (`DAT_80347110 = 2`). More-menu dispatcher `FUN_801851c8` maps values `0..4` to options/settings state `15`, crew assignment state `13`, wanted viewer state `19`, options description/apply state `18`, and back-to-root state `1`.

## Battle Command/Menu Progress

The dynamic rotating battle command wheel is traced to `FUN_80025624`, the top battle command-menu display/animation child callback. This callback consumes `DAT_80347628`, the indexed object initialized from `battle/HrsBinCW.bin`, and clones records through `FUN_801d68c4` and `FUN_801d497c`.

The command wheel's visual pieces are serialized in `HrsBinCW.bin`, but the short rotation animation is runtime code, not a separate serialized animation table.

| Runtime field/global | Meaning |
| --- | --- |
| command worksheet `field_0x5` | Signed selected command index. Normal values are `0..6`; `FUN_80025624` writes `-1` during the transition so input is ignored. |
| command worksheet `field_0x6` | Movement request written by the input controller and consumed by `FUN_80025624`. |
| `80346b40` | Transition direction delta, `-1`, `0`, or `+1`. |
| `80346b3c` | Transition countdown, initialized to `3` and decremented each callback pass. |
| `80346b4c` | Old selected command index captured when transition starts. |
| `80302a00` | Temporary float position table for the seven orbiting command slots. |

During a transition, `FUN_80025624` draws both the primary selected command record and an adjacent old/next selected command record with separate alpha factors. When the countdown reaches zero, it commits `field_0x5 = oldIndex + direction`, wraps into `0..6`, and clears `80346b40`.

| Command id | UI command | Normal icon record | Selected icon record | Text label record | Key material slots |
| ---: | --- | ---: | ---: | ---: | --- |
| 0 | Focus | `0x0d` | `0x17` | `0x34` | icon slot `22`, selected background slot `6`, text slot `15` |
| 1 | Magic | `0x0f` | `0x19` | `0x36` | icon slot `24`, selected background slot `6`, text slot `15` |
| 2 | S-Move | `0x11` | `0x1b` | `0x38` | icon slot `26`, selected background slot `6`, text slot `15` |
| 3 | Attack | `0x0b` | `0x15` | `0x32` | icon slot `20`, selected background slot `6`, text slot `15` |
| 4 | Block/Guard | `0x0c` | `0x16` | `0x33` | icon slot `21`, selected background slot `6`, text slot `15` |
| 5 | Item | `0x0e` | `0x18` | `0x35` | icon slot `23`, selected background slot `6`, text slot `15` |
| 6 | Run | `0x10` | `0x1a` | `0x37` | icon slot `25`, selected background slot `6`, text slot `15` |
| 6 alt | Crew/alternate Run state | n/a | `0x1c` | `0x39` | icon slot `19`, selected background slot `6`, text slot `15` |

`FUN_80026b20` selects the highlighted records. The alternate command-id `6` path is used when `DAT_80346b98 == 1` and worksheet `field_0x8 == 0`; the text source rectangles line up with the `Crew` label on the `command.mld` text sheet.

Exporter/editor implication: expose these records as command-wheel parts when the active context is `battle/HrsBinCW.bin`, but keep the transition countdown, direction, and alpha layering as runtime behavior rather than serialized `.bin` fields.

## Battle Command/Menu Record Status

Do not advertise `HrsBinCW.bin` as only a command wheel layout. It should be presented as the battle command/menu layout bank, with exact command-wheel records named, other high-confidence groups named by group role, and remaining category/list markers kept provisional until the worksheet/list producer is traced and labeled.

A full one-to-one draft mapping exists under the ignored research folder: `SpiceBin/research/2026-06-20_hrsbin_cw_record_mapping/HrsBinCW_record_mapping_draft.md`. Promoted record groups and one-to-one record annotations live in `Docs/BinFileRecords.md`.

## Research Workflow

1. Copy raw `.bin` samples into `SpiceBin/research/` or another ignored evidence folder.
2. Keep source provenance beside the sample: region, disc path, archive/member name, extraction command, and timestamp.
3. Record tentative offsets and field interpretations in local research notes until they have been validated against multiple files.
4. Promote stable field-layout conclusions into `Docs/BinFileLayout.md`, stable record identities into `Docs/BinFileRecords.md`, and current progress/status into this document.
5. Validate US files first when correlating with Ghidra, because the repo-local Ghidra project is based on the US `main.dol`; compare EU and JP files afterward and document deltas.

## Open Questions

- What should this family be named in `SpiceBin`: `HrsBin`, `IndexedBin`, or a more domain-specific UI/layout name?
- Which standalone and embedded `.bin` payloads should be first-class fixtures?
- What is the exact per-entry schema of `field/wmaparea.BIN`, which is not the indexed layout family and is consumed as a raw fixed-size world-map area/menu table?
- Should the high-confidence record keys currently promoted in `Docs/BinFileRecords.md` become stable public exporter field names, or should `SpiceBin` add a separate versioned key/alias layer before editor exposure?
- Is `field/HRSBin.bin` unused/leftover, loaded by a constructed path, loaded from script/archive data, or referenced indirectly by a system that does not retain the literal path in the DOL?
- Can runtime file-open instrumentation or a script/archive resource-name source audit identify any consumer for loose `field/HRSBin.bin`, now that static Ghidra, direct `loadFileFromPath`, indexed-wrapper, and US corpus searches do not show one?
- How are fixed-data texture indices mapped to companion MLD/MLL texture slots in each runtime context?
- For regional exporter validation, which differences are pure content changes and which, if any, require region-specific semantic labels?
- Which indexed `.bin` member subformats deserve exporters rather than raw preservation?
