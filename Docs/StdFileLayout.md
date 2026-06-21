# STD File Layout

This living document tracks the current understanding of Skies of Arcadia
Legends `.std` files and the SPICE `SpiceStd` project. It is intentionally
conservative: fields are named only when supported by current corpus scans,
SAVOR first-battle staging evidence, or direct runtime notes.

## Scope

`SpiceStd` owns `.std` usage scanning and layout research. Keep copied samples,
decoded payloads, local scripts, and other working evidence under
`SpiceStd/research/`; that folder is local-only and gitignored.

Use the US corpus first because the repo-local Ghidra project is based on the
US DOL:

- `<local-corpus>\2002-12-19-gc-us-final_Skies_of_Arcadia_Legends`

EU and JP files should be used later as compatibility checks. Record regional
deltas explicitly instead of folding them into the US schema.

## Current Implementation References

- `SpiceStd/StdUsage.cpp`
- `SpiceStd/StdUsage.h`
- `SpiceStd/StdModel.h`
- `SpiceStd/StdParser.cpp`
- `SpiceStd/StdParser.h`
- `SpiceStd/StdCorpusScanMain.cpp`
- `SpiceTests/test_std_parser.cpp`
- `SpiceTests/test_std_usage.cpp`
- local-only research under ignored `SpiceStd/research/`

`SpiceStd` currently provides usage classification, corpus reporting, and a
conservative `.std` parser/re-export API. The parser recognizes the two known
decoded layouts, keeps unknown record bodies opaque, preserves original source
bytes, and can export original source bytes, decoded bytes, or AKLZ-reencoded
decoded bytes.

The supported parser is intentionally conservative. It is suitable for
byte-preserving round trips and schema inspection, not semantic editing of
opaque `%s0_STD` payload bodies.

## US Corpus Baseline

Generated with:

```text
.\x64\Debug\SpiceStdCorpusScan.exe <local-corpus>\2002-12-19-gc-us-final_Skies_of_Arcadia_Legends SpiceStd\research\2026-06-21_us_std_usage
```

Summary:

- total `.std` files: 438
- AKLZ-compressed files: 438
- decode errors: 0
- `bchara` files: 432
- files outside `bchara`: 6
- ALX-known covered pattern files: 386

Usage buckets:

| Bucket | Files | AKLZ | Decode errors | ALX-known covered pattern |
| --- | ---: | ---: | ---: | ---: |
| `bchara_m_family` | 386 | 386 | 0 | 386 |
| `bchara_common` | 1 | 1 | 0 | 0 |
| `bchara_damage` | 1 | 1 | 0 | 0 |
| `bchara_character_resource` | 44 | 44 | 0 | 0 |
| `other_directory` | 6 | 6 | 0 | 0 |

These buckets are usage heuristics, not proof that every bucket has one binary
schema. All file-layout claims below should be read as decoded battle
combatant/action-row evidence until broader parsing proves otherwise.

## Compression

US disc `.std` files are AKLZ-wrapped. Parse layout fields from the decompressed
byte stream.

The copied SAVOR first-battle samples were decompressed from:

- `bchara/MB000.std`
- `bchara/ma000.std`
- `bchara/MA001.std`

## `%s_STD` Battle Action-Row Payload

The first-battle combatant `.std` samples such as `ma000.std`, `MA001.std`, and
`MB000.std` decode as a command payload with a 0x10-byte header followed by
fixed 0x18-byte action rows. Numeric fields are big-endian after decompression.

Ghidra evidence as of `2026-06-21` covers `FUN_80021934`, `FUN_80067e8c`,
`FUN_80067dfc`, `FUN_80067ca4`, `FUN_800670e4`, and `FUN_8006721c`.

Observed header:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `commandLow` | Low half of combined command kind. |
| `0x02` | 2 | `commandHigh` | High half of combined command kind. |
| `0x04` | 4 | `loaderContextWord` | Source word is copied, then overwritten by `FUN_80009860` during runtime setup. |
| `0x08` | 4 | `rowCount` | Number of 0x18-byte action rows. |
| `0x0c` | 4 | `rowTablePtr` | Source word is copied, then overwritten with the allocated runtime row-table pointer. |

Observed combined command kind:

```text
(commandHigh << 16) | commandLow == 0x00010000
```

`FUN_80067dfc` dispatches this command kind to `FUN_80067ca4`.

Rows start at decoded offset `0x10`.

| Row offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `actionId` | Signed action id; matches `InstructionWorksheet->field6_0x6` lookup key. |
| `0x02` | 2 | `rowType` | Signed row type. Runtime appends a terminator row with value 3. |
| `0x04` | 2 | `callbackIndex` | Signed index into `PTR_FUN_802df2b8`. |
| `0x06` | 2 | `motionSlotOrdinal` | Signed ordinal passed to MLD motion-list lookup in the normal row-activation path. Previously tracked as `aux`. |
| `0x08` | 4 | `flags` | Raw action flags. Known consumers below. |
| `0x0c` | 2 | `secondaryKey` | Signed secondary lookup key for selected special-action ids. |
| `0x0e` | 2 | `param` | Signed additional parameter. |
| `0x10` | 4 | `float0` | Floating value copied to worksheet `+0x12c`; `FUN_8001ecb4` treats zero as a gated/unavailable row unless a worksheet flag is set. Exact semantic name still open. |
| `0x14` | 4 | `motionBlendOrRate` | Floating value copied to worksheet `+0x128`, passed to `FUN_80075dac`, and stored at instruction worksheet `+0x6c`. Exact semantic name still open. |

Action-row lookup:

- `FUN_800670e4` scans rows from `InstructionWorksheet+0xdc`.
- Matching starts with row `+0x00 == requested actionId`.
- Action ids `0x18`, `0x19`, `0x1d`, and `0x1e` also require
  row `+0x0c == requested secondaryKey`.
- When not in lookup-only mode, `FUN_800670e4` installs
  `PTR_FUN_802df2b8[row.callbackIndex]` into `InstructionWorksheet+0xe0`.
- If the sentinel row is reached, action id `0x0e` falls back to callback index
  13 and action id `0x25` falls back to callback index 14.

Known `flags` consumers:

- `FUN_8006721c` treats `0x04000000` as a force-update bit for the selected
  action row.
- `FUN_80075dac` also uses `0x04000000` to bypass the same-slot early return
  when the requested motion slot matches the current slot.
- `FUN_8006721c` also treats `0x01000000` as force-update when the worksheet has
  flag `0x00020000`.
- `FUN_80075dac` treats `0x00080000` as the dynamic `E67%03d%02d.MLD` path
  instead of using the combatant's currently loaded MLD.
- `FUN_80075dac` treats `0x40000000` as a request to resolve a second motion
  pointer from `motionSlotOrdinal + 1` and stores that secondary pointer at
  instruction worksheet `+0x60`.
- Other bits remain open. They are not yet safe to name from the current Ghidra
  pass.

Known first-battle row counts:

| File | Decoded size | Rows |
| --- | ---: | ---: |
| `MB000.std.dec` | `0x220` | 22 |
| `ma000.std.dec` | `0x700` | 74 |
| `MA001.std.dec` | `0x6e8` | 73 |

The sizes exactly match `0x10 + rowCount * 0x18` for these three decoded
samples.

## First-Battle Resource Mapping

SAVOR's staging notes map the first battle's relevant separate STD payloads as:

| Battle slot | Actor | STD file |
| ---: | --- | --- |
| 0 | Vyse | `ma000.std` |
| 1 | Aika | `MA001.std` |
| 4 | Soldier | `MB000.std` |
| 5 | Soldier | `MB000.std` |

This mapping is grounded in the US corpus and first-battle SAVOR staging work.
Raw live party composition still needs independent runtime validation.

Key action rows from the first-battle samples:

| File | Row index | Action id | Row type | Callback index | Callback | Aux | Flags |
| --- | ---: | ---: | ---: | ---: | --- | ---: | --- |
| `MB000.std.dec` | 3 | 4 | 1 | 8 | `FUN_800662bc` | 3 | `0x88000000` |
| `MB000.std.dec` | 4 | 8 | 1 | 8 | `FUN_800662bc` | 4 | `0x88200000` |
| `ma000.std.dec` | 3 | 4 | 1 | 8 | `FUN_800662bc` | 3 | `0x8a200000` |
| `ma000.std.dec` | 4 | 8 | 1 | 8 | `FUN_800662bc` | 4 | `0x8a200000` |
| `MA001.std.dec` | 3 | 4 | 1 | 8 | `FUN_800662bc` | 3 | `0x88000000` |
| `MA001.std.dec` | 4 | 8 | 1 | 8 | `FUN_800662bc` | 4 | `0x88000000` |

`PTR_FUN_802df2b8` callback table:

| Callback index | Current function label |
| ---: | --- |
| 0 | `FUN_80073d10` |
| 1 | `FUN_8007e88c` |
| 2 | `FUN_8007e690` |
| 3 | `FUN_80021cbc` |
| 4 | `FUN_8007e4f8` |
| 5 | `FUN_8007e44c` |
| 6 | `FUN_80073d0c` |
| 7 | `FUN_8006782c` |
| 8 | `FUN_800662bc` |
| 9 | `FUN_80066028` |
| 10 | `FUN_800662bc` |
| 11 | `FUN_80019e44` |
| 12 | `FUN_80019d7c` |
| 13 | `FUN_80019f0c` |
| 14 | `FUN_8001977c` |
| 15 | `FUN_80065f1c` |
| 16 | `FUN_80073d14` |

Entry 17 is null. The words immediately after it point into `FUN_80062674`
ranges and should be treated as a neighboring table until a caller proves they
belong to this action callback table.

## Runtime Evidence

The current Ghidra pass records this separate-STD runtime path:

- `FUN_80021934` and `FUN_80067e8c` load a base battle resource and then build
  a `%s_STD` filename.
- `FUN_8006daf8` decompresses/loads the `%s_STD` file.
- It copies the 0x10-byte decoded header plus 0x18-byte rows.
- It appends an in-memory sentinel row with action id `0x26` and row type `3`.
- It stores the copied row-table pointer at copied root `+0x0c`.
- It initializes root `+0x10..+0x1f` to `-1`, `-1`, then zeroes.
- It calls `FUN_80009860`, which mutates root `+0x04`.
- It dispatches the copied root through `FUN_800091a4` or `FUN_80067dfc`.
- It stores the copied STD root at loaded resource `+0x30`.
- It then builds a `%s0_STD` filename and calls `FUN_80035d4c` to merge/load the
  companion entry-table STD into the same root pointer.

`FUN_80067dfc` dispatches combined ids:

| Combined id | Handler |
| ---: | --- |
| `0x00000000` | `FUN_8007e13c` |
| `0x00000001` | `FUN_8007e118` |
| `0x00000002` | `FUN_8007dfc0` |
| `0x00000003` | `FUN_8007dee0` |
| `0x00010000` | `FUN_80067ca4` |

Action-row setup:

- `FUN_80067ca4` creates an action child thread and stores the row-table pointer
  at `InstructionWorksheet+0xdc`.
- `FUN_80067ca4` copies row `+0x14` into worksheet field `+0x128` and row
  `+0x10` into worksheet field `+0x12c`.
- `FUN_80020060` scans 0x18 rows and returns the first row whose `rowType` is 0;
  row type 3 is treated as the terminator.
- `FUN_800670e4` does deterministic action-row lookup by comparing the requested
  worksheet action id to row `+0x00`; when not in lookup-only mode it writes the
  selected callback pointer into `InstructionWorksheet+0xe0`.

Important boundary: the 0x18 action-row table, the 0x10 `%s0_STD` entry table,
and selected action-view payload records are related at runtime, but they are not
the same struct.

## STD Action to MLD Motion-Slot Trace

A targeted Ghidra trace on `2026-06-21` walked the US callback table
`PTR_FUN_802df2b8` plus nearby action-view, resource-staging, STD entry-table,
and MLK/MLD resource roots. The local-only trace output is under:

```text
SpiceStd/research/2026-06-21_std_mld_targeted_trace/
```

The strongest direct action-row to MLD path is:

```text
STD row -> callback table -> FUN_80075f2c/FUN_80075ff0 -> FUN_80075dac -> FUN_8006ceec -> MLD index entry +0x1c counted list
```

Observed row activation:

- `FUN_80075f2c` uses the currently selected row index at instruction worksheet
  `+0xe4`, requires row `+0x02 == 1`, and passes row `+0x06`, row `+0x08`,
  row `+0x14`, current action id, and the worksheet secondary key to
  `FUN_80075dac`.
- `FUN_80075ff0` re-selects a row through `FUN_8001fdd8` by action id plus
  optional secondary key, then passes the same row fields to `FUN_80075dac`.
- For action ids `2` and `7`, both helpers can substitute row `+0x06` from a
  row index stored at instruction worksheet `+0x204`.

`FUN_80075dac` is the bridge from STD row fields to loaded MLD content:

- If row flags do not include `0x00080000`, it uses the combatant's current
  loaded MLD resource at `*(worksheet + 0x10) + 8`.
- If row flags include `0x00080000`, it builds/loads a dynamic
  `E67%03d%02d.MLD` resource via `FUN_80072e8c`, `FUN_8006e898`, and
  `FUN_80072dd4`, then uses the loaded resource at worksheet `+0x228`.
- It calls `FUN_8006ceec(loadedMld, out, motionSlotOrdinal,
  *(short *)(worksheet + 0x16))`.
- On success, it stores the selected pointer at instruction worksheet `+0x5c`,
  stores the source row ordinal at `+0x64`, stores row `+0x14` at `+0x6c`, and
  clears `+0x70`.
- If row flags include `0x40000000`, it also calls `FUN_8006ceec` with
  `motionSlotOrdinal + 1` and stores the second pointer at instruction worksheet
  `+0x60`.

`FUN_8006ceec` proves the MLD field being indexed:

```c
list = *(uint **)(*(int *)(loadedMld + 4) + entryIndex * 0x68 + 0x1c);
if (*list <= ordinal) return 0;
*out = list[ordinal + 1];
return 1;
```

`Docs/MldFileLayout.md` names MLD index-entry `+0x1c` as
`motionAddressesPointer`, a counted U32 list of motion block addresses. The
earlier `planning/Analysis/2026-06-15_mld_animation_binding_ghidra/summary.md`
also identifies this field as the runtime motion-slot list. Therefore, for the
normal path, STD action-row `+0x06` is best understood as a motion-slot ordinal
into the active MLD index entry selected by worksheet `+0x16`.

This trace did not find a direct STD action-row field that selects an MLK record.
MLK appears in resource staging/loading paths and can contribute MLD-like loaded
assets, but the action-row execution path above consumes a loaded MLD resource
and indexes its motion list.

Dynamic `E67%03d%02d.MLD` files are present in the US corpus, but only for the
action-17 shape:

| File | Size |
| --- | ---: |
| `beff/e6700017.mld` | 22058 |
| `beff/e6700117.mld` | 7699 |
| `beff/e6700217.mld` | 35975 |
| `beff/e6700317.mld` | 13562 |
| `beff/e6700417.mld` | 6823 |
| `beff/e6700517.mld` | 10959 |

EU and JP have the same six filenames with matching sizes. No other
`E67*.mld` files were found in the US dump.

`FUN_80072e8c` and `FUN_80072dd4` share the same filename rule:

```text
if actionId is 0x18, 0x19, 0x1d, or 0x1e:
    E67%03d%02d.MLD uses secondaryKey, actionId
else:
    E67%03d%02d.MLD uses worksheet/action-group field, actionId
```

`FUN_80075dac` calls the builder twice in the dynamic path:

- The cache probe calls `FUN_80072e8c` with current instruction worksheet
  shorts `+0x06`, `+0x08`, and `+0x02`.
- The load path calls `FUN_80072dd4` with the requested action id and secondary
  key supplied to `FUN_80075dac`, plus current instruction worksheet `+0x02`.

Immediate origins for the requested action id and secondary key:

- `FUN_80075f2c` supplies the current instruction worksheet action id `+0x06`
  and secondary key `+0x08`.
- `FUN_80075ff0` supplies handler-requested action id and secondary key
  parameters after finding the matching STD row through `FUN_8001fdd8`.
- `FUN_80076170`, reached from `FUN_8001eba4`, supplies action id from selected
  row `+0x00` and secondary key from selected row `+0x0c`.
- `FUN_8006721c` is a major upstream action transition helper. It derives a new
  action id and secondary key, finds the matching row through `FUN_800670e4`,
  then writes them into instruction worksheet `+0x06` and `+0x08`.

Current open point: `FUN_80067ca4` initializes instruction worksheet `+0x02` to
zero, and this pass did not identify a nonzero writer. Because loose corpus
files exist for `E6700017.MLD` through `E6700517.MLD`, worksheet `+0x02` is
likely a small action/effect group or actor/effect variant id, but its full
runtime origin remains unproven.

Corpus scan of US `bchara_m_family` action rows with flag `0x00080000` found 54
rows in eight STD stems: `ma000`, `MA001`, `ma002`, `MA003`, `MA004`, `MA005`,
`MG025`, and `mg029`. Those include action ids `17`, `24`, `26`, `27`, `28`, and
`29`; only action `17` currently has matching loose `E67*.mld` files in the
disc dump.

A follow-up US `.mlk` filename scan found no exact `E67*.mlk` files. However,
the dynamic special-action row keys do line up with existing MLK filename
families:

- Every action `24` secondary key from `36` through `59` has an exact
  `d24NNN00.mlk` counterpart, for example row key `36` maps by filename to
  `d2403600.mlk`.
- Every action `29` secondary key found in the flagged rows has an `f29NNN*.mlk`
  counterpart: `46 -> f2904699.mlk`, `48 -> f2904899.mlk`,
  `83 -> f290839d*.mlk`, `84 -> f290849d*.mlk`, `86 -> f290869d.mlk`,
  `90 -> f2909099.mlk`, and `94 -> f2909499.mlk`.
- The non-special flagged action ids `17`, `26`, `27`, and `28` do not have
  comparable exact `dNN`/`fNN` MLK-family coverage in the US corpus. The only
  `f27` hit from this narrow scan is `f2705733.mlk`, which does not explain the
  row set by itself.

Parsing the only US `f27*.mlk`, `beff/f2705733.mlk`, did not reveal a hidden
`.mld` filename match. Its five valid embedded MLD records parse as one-entry
MLD payloads with index-entry function names `e1500601.nj`, `e1500602.nj`,
`e1500832.nj`, `_e1500670.nj`, and `_e1500671.nj`. These names are `.nj`
function names, not `.mld` resource names; none of the matching stems exists as
a loose US corpus file. The only loose US `e15*.mld` file found in this check
was `beff/e1529601.mld`, which does not match the `f2705733.mlk` embedded-entry
names.

This is filename evidence only. It strengthens the idea that special-action
STD rows are coordinated with the supermove/effect asset families, but it still
does not prove a direct STD-to-MLK record selection field.

Handler-specific MLD interactions found in the same trace:

- Callback index 7, `FUN_8006782c`, compares the current loaded MLD filename
  against static names `MA010.MLD` through `MA025.MLD` and stores the matched
  index in shared battle state before continuing action setup.
- Callback index 13, `FUN_80019f0c`, uses STD0 entry-table scans and
  `mldStr2Slot_80020e10`; that helper maps `MA000.MLD`/`ma000.mld`,
  `MA001.MLD`/`ma001.mld`, and related party model names to small slots and
  changes behavior when no slot is found.

## `%s0_STD` Entry-Table Payload

Companion files such as `ma0000.std`, `common.std`, and `damage.std` decode as a
0x10-byte header followed by a 0x10-byte entry table and a payload area.

Representative US samples:

| File | Decoded size | Header word 0 | Entries before sentinel | Opcode mix |
| --- | ---: | ---: | ---: | --- |
| `ma0000.std.dec` | `0x4ea38` | `0x05e70004` | 1510 | all opcode 3 |
| `common.std.dec` | `0x45f38` | `0x05080004` | 1287 | all opcode 3 |
| `damage.std.dec` | `0x83e10` | `0x09880004` | 2439 | 2438 opcode 3, 1 opcode 5 |

Observed header:

| Offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `recordCountIncludingSentinel` | Observed as entry count plus one. |
| `0x02` | 2 | `kind` | Observed as 4 in the sampled entry-table files. |
| `0x04` | 4 | `reserved0` | Observed zero in sampled files. |
| `0x08` | 4 | `reserved1` | Observed zero in sampled files. |
| `0x0c` | 4 | `decodedSpanMinusHeader` | Observed as decoded file size minus 0x10. |

The entry table starts at decoded offset `0x10`.

| Entry offset | Size | Field | Current meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | `locationCode` | Signed location/type id. Negative value terminates the table. |
| `0x02` | 2 | `opcode` | Signed type group. Current samples use 3 and 5. |
| `0x04` | 4 | `field2` | Preserved by the loader. Observed zero in sampled first entries. |
| `0x08` | 4 | `payloadSize` | Source payload byte size; runtime helpers replace it with canonical size for recognized entries. |
| `0x0c` | 4 | `payloadOffsetOrPtr` | Source offset relative to decoded `+0x10`; runtime replaces it with an allocated payload pointer. |

Loader behavior:

- `FUN_80035c38` copies the 0x10 header, counts entries until a negative
  `locationCode`, allocates a runtime 0x10 entry table, and copies every entry
  plus the sentinel.
- `FUN_80035d4c` does the same allocation/copy path while merging a `%s0_STD`
  file into an existing STD root pointer.
- `FUN_80035fc0` loads a named STD file and uses the same entry allocation path.
- `GetSTDEntryBufferSize03Type` and `GetSTDEntryBufferSize05Type` recognize
  `(opcode << 16) | locationCode`, allocate the runtime payload buffer, replace
  `payloadSize`, and replace `payloadOffsetOrPtr` with the allocated pointer.
- Payload bytes are copied from `decoded + 0x10 + payloadOffset`.
- `FUN_80035918` frees entry payload pointers for entries where `opcode != 1`.
- `FUN_800364f4` counts entries until a negative `locationCode` and returns the
  count including the sentinel.

## Entry-Table Scan Helpers

Several consumers scan the 0x10 entry table after payload pointers have been
materialized:

- `FUN_80008f08` selects one entry and can match by payload key, by combined
  entry type `(opcode << 16) | locationCode`, or by both.
- `FUN_80009030` counts entries whose payload key matches, optionally filtered
  by combined entry type. It excludes combined type `0x00030041`.
- `FUN_8003de8c` compares payload short 0 and short 2 against a requested
  primary and secondary key. Primary keys `0x18`, `0x1d`, and `0x1e` require the
  secondary key; other primary keys match on the primary key alone.
- `FUN_80008800` and `FUN_80008938` build 0x10 runtime linked-list nodes that
  group entry ranges by payload key. These nodes are runtime helpers, not file
  entries.

Known filtered `FUN_80009030` callers include `FUN_80012f58`, which checks
combined entry types `0x0003002a` and `0x0003002e` while deciding action-view
suppression paths.

## Action-View Record Payload Fields

`Battle::Turn::UpdateActionViewRecord_80051264`, `FUN_80052058`, and
`FUN_8005259c` read fields from `trn_wksht->_x178_std_file_maybe`. This appears
to be a selected entry-table payload record, not a 0x18 action row.

Current payload-offset evidence:

| Payload offset | Size | Current meaning |
| --- | ---: | --- |
| `0x06` | 2 | Low 16-bit flags. Bit `0x2000` is checked by `FUN_80037fd8`. |
| `0x10` | 4 | Action-view flags. Known bits below. |
| `0x14` | 4 | Float yaw/angle used by `FUN_80052058` when generated yaw is not requested. |
| `0x18` | 2 | Start frame / lower timing bound. |
| `0x1c` | 2 | End frame / completion timing bound. |
| `0x1e` | 2 | Hold/setup duration copied to worksheet `+0x10c`. |
| `0x20` | 2 | Step/interpolation duration used to divide vector deltas. |
| `0x22` | 2 | Action-view mode switch value. |

Known action-view flag bits at payload `+0x10`:

| Bit | Evidence |
| ---: | --- |
| `0x80000000` | `UpdateActionViewRecord` calls `FUN_8006d954(1)` on entry and `FUN_8006d8dc(1)` on exit in the non-`DAT_80347394` path. |
| `0x20000000` | `FUN_80052058` changes yaw handling and adds `FLOAT_8034875c`. |
| `0x10000000` | Affects default/random mode transition and suppresses some target-camera handling. |
| `0x04000000` | `FUN_80052058` generates yaw via `FUN_800608c8` instead of using payload `+0x14`. |
| `0x00000800` | `FUN_80052058` chooses `FUN_8001359c` instead of `FUN_8001363c`. |

Known action-view modes at payload `+0x22`:

| Mode | Handler |
| ---: | --- |
| 0 | `FUN_80052ecc` |
| 1 | `FUN_8005259c` |
| 2 | `FUN_800534b0` |
| 7, 8, 9, 0x12 | `FUN_80053964` |
| 0xa, 0xb | `FUN_80052058` |
| 0xe | `FUN_80052b24` |
| 0xf, 0x10 | `FUN_80053c48` |
| 0x11 | `FUN_800521c4` |

## Local Research Artifacts

Current local-only research folders:

- `SpiceStd/research/2026-06-21_ghidra_std_handlers/`
- `SpiceStd/research/2026-06-21_savorpredict_first_battle_handoff/`
- `SpiceStd/research/2026-06-21_std_mld_targeted_trace/`
- `SpiceStd/research/2026-06-21_us_std_usage/`

The Ghidra handler folder includes:

- decompile-term search results for `STD`, `%s_STD`, `%s0_STD`,
  `FUN_80067e8c`, `FUN_800670e4`, and `PTR_FUN_802df2b8`
- focused Ghidra exports for the loader, action-row, entry-table, and
  action-view consumer functions
- callback-table export for `PTR_FUN_802df2b8`
- decoded US samples for `ma000.std`, `ma0000.std`, `common.std`, and
  `damage.std`

The SAVOR handoff folder includes the compact parser script, TSVs, and decoded
first-battle samples copied from:

```text
<local-user>/source\repos\jahorta\SAVOR\SavorPredict\planning\static_support
```

The SAVOR planning tree should remain read-only reference material.

The STD-to-MLD targeted trace folder includes:

- the callback-table and selected-root traversal script
- callback-root tables and call edges for a depth-2 Ghidra trace
- decompiled functions for the traced callback/action/resource paths
- focused exports for loaded-MLD lookup helpers such as `FUN_8006ceec`

## Parser/Re-Export Status

Implemented conservative support:

- AKLZ source detection and decompression.
- Action-row layout: 0x10-byte header plus `rowCount * 0x18` rows.
- Entry-table layout: 0x10-byte header plus 0x10-byte records through a negative
  `locationCode` sentinel.
- Original raw byte export for byte-identical no-edit round trips.
- Decoded byte export for inspection or external tooling.
- AKLZ re-encoding of decoded bytes when a rebuilt compressed file is needed.
- Diagnostics for unknown layouts, out-of-bounds entry payload spans, missing
  entry sentinels, and unexpected serialized action sentinel rows.

Writer boundary:

- The conservative exporter does not synthesize runtime-only rows.
- Entry payload bodies remain opaque byte spans addressed by the original table
  offsets and sizes.
- Unknown fields are parsed as raw words and preserved by decoded-byte export.

## Open Questions

- Validate the first-battle resource mapping against live runtime state.
- Identify runtime consumers for the six US `field/mb###.std` files outside
  `bchara`; their decoded file layout is already covered by the action-row
  parser.
- Name callback-index semantics from callback bodies, not just table addresses.
- Name `%s0_STD` payload bodies by combined type; the conservative parser keeps
  them opaque and preserves offsets/sizes.
- Explain remaining action-row field `param` and refine the semantic names for
  `float0` and row `+0x14`.
- Validate whether every row `+0x06` consumer should be named as a motion-slot
  ordinal, or whether some handlers reuse the field as a broader resource-list
  ordinal.
- Parse selected US MLD files beside their paired STD files to correlate actual
  row `+0x06` values with concrete motion-list entries.
- Split and name the action-view payload record by combined entry type once its
  source table entries are identified.
- Runtime semantics that might differ by region should be checked only if SPICE
  later supports semantic editing against non-US binaries.
