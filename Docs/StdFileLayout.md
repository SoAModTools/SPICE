# STD File Layout

Core binary layout and semantic-editing contract for Skies of Arcadia Legends
`.std` files in `SpiceStd`.

Disc `.std` files are AKLZ-wrapped. Parse all layout fields from the
decompressed byte stream. Numeric fields are big-endian.

## Layout Forms

- `%s_STD`: 0x10-byte header followed by fixed 0x18-byte action rows.
- `%s0_STD`: 0x10-byte header followed by 0x10-byte entry records and payload
  bodies.

## `%s_STD` Action Rows

Use `%s_STD` rows as combatant action-row tables. Runtime lookup compares
`actionId` with the current action/source key or combatant worksheet
`field6_0x6`, then uses `callbackIndex` to install the action callback.

Header:

| Offset | Size | Field | Editor rule |
| --- | ---: | --- | --- |
| `0x00` | 2 | `commandLow` | Low half of combined command kind. |
| `0x02` | 2 | `commandHigh` | High half of combined command kind. |
| `0x04` | 4 | `loaderContextWord` | Preserve source word. |
| `0x08` | 4 | `rowCount` | Number of serialized 0x18-byte action rows. |
| `0x0c` | 4 | `rowTablePtr` | Preserve source word. |

Supported combined command kind:

```text
(commandHigh << 16) | commandLow == 0x00010000
```

Rows start at decoded offset `0x10`.

| Row offset | Size | Field | Editor rule |
| --- | ---: | --- | --- |
| `0x00` | 2 | `actionId` | Signed primary row key. |
| `0x02` | 2 | `rowType` | Signed row kind. Do not synthesize runtime terminator rows on export. |
| `0x04` | 2 | `callbackIndex` | Signed callback-table index; primary semantic router. |
| `0x06` | 2 | `callbackOrdinal` | Callback-local ordinal. Normal-motion callbacks may own it as a motion slot. |
| `0x08` | 4 | `flags` | Raw action flags. Named bits must remain callback-family scoped. |
| `0x0c` | 2 | `secondaryKey` | Secondary row key for selected actions and helper lookups. |
| `0x0e` | 2 | `callbackAuxParam` | Callback-local parameter. Callback 7 may own it as setup height/extent. |
| `0x10` | 4 | `transitionGateDivisor` | Normal-motion timing/gate field. Preserve outside normal-motion ownership. |
| `0x14` | 4 | `motionProgressStep` | Normal-motion progress step. Menu-child callbacks may own it as guarded menu progress. |

Semantic ownership:

- Resolve `callbackIndex` before editing `+0x06`, `+0x0e`, `+0x10`, or
  `+0x14`.
- Treat `actionId` as the primary action/source key. First-battle resources
  confirm action ids `4` and `8` route through callback index `8`
  (`FUN_800662bc`) for `MA000`, `MA001`, and `MB000`.
- For action ids `0x18`, `0x1d`, and `0x1e`, row lookup also checks
  `secondaryKey`. Queued instruction state can choose `0x18` versus `0x1d`
  behavior and pass its queued parameter as this secondary selector.
- Normal-motion callbacks `8`, `9`, `10`, `13`, and `15` may own `+0x06`,
  `+0x10`, and `+0x14` after resource validation.
- Menu-child callbacks `11` and `12` may own `+0x14` only.
- Setup callback `7` may own `+0x0e` only.
- Callback indices `16`, `17`, `18`, `19`, and unknown callback indices are
  preserve/expert until promoted by type-specific research.

## `%s0_STD` Entry Table

Entry-table records start at decoded offset `0x10`. The table ends at the
first entry whose `locationCode` is negative.

Header:

| Offset | Size | Field | Editor rule |
| --- | ---: | --- | --- |
| `0x00` | 2 | `recordCountIncludingSentinel` | Entry count plus sentinel. |
| `0x02` | 2 | `kind` | Preserve unless a type-specific editor owns it. |
| `0x04` | 4 | `reserved0` | Preserve. |
| `0x08` | 4 | `reserved1` | Preserve. |
| `0x0c` | 4 | `decodedSpanMinusHeader` | Decoded file size minus `0x10`. |

Entry record:

| Entry offset | Size | Field | Editor rule |
| --- | ---: | --- | --- |
| `0x00` | 2 | `locationCode` | Signed location/type id. Negative value terminates the table. |
| `0x02` | 2 | `opcode` | Signed type group. |
| `0x04` | 4 | `field2` | Preserve unless a type-specific editor owns it. |
| `0x08` | 4 | `payloadSize` | Source payload byte size. |
| `0x0c` | 4 | `payloadOffsetOrPtr` | Source offset relative to decoded `+0x10`; never re-export a runtime pointer. |

Payload storage rules:

- Payload bytes are stored at `decoded + 0x10 + payloadOffset`.
- Unknown entry payload bodies are raw byte spans.
- Preserve payload sizes, offsets, and opaque bytes unless a promoted
  type-specific editor explicitly owns the field.

## Entry Payload Dispatch

The combined entry type is:

```text
(opcode << 16) | locationCode
```

Common selected-payload gate fields:

| Payload offset | Size | Semantic role |
| --- | ---: | --- |
| `0x00` | 2 | Primary action key. |
| `0x02` | 2 | Generic scan-helper secondary key. |
| `0x04` | 2 | Direct-gate secondary key for special primary keys `0x18`, `0x1d`, and `0x1e`. |

Do not collapse payload `+0x02` and `+0x04` into one semantic field.

Promoted payload records:

| Combined type | Debug label | Size | Editor stance |
| ---: | --- | ---: | --- |
| `0x0003002a` | `SYSTEM CAMER` | `0x24` | Guarded action-view payload. |
| `0x0003002e` | `ICON CONTROL` | `0x20` | Guarded icon-control payload. |
| `0x00030036` | `SE REQUEST` | `0x2c` | Guarded sound/effect request payload; content labels incomplete. |
| `0x0003003b` | `STREAM SET` | `0x24` | Guarded stream-control payload. |
| `0x00030049` | `CALL CHARA` | `0x1c` | Guarded character/model request payload; model labels incomplete. |
| `0x00030053` | `PUTMODEL-` | `0x90` | Guarded model-placement payload; owner-node labels incomplete. |
| `0x00030057` | `EXT CHARA` | `0x2c` | Provisional extended-character payload. |
| `0x00030058` | `SYS CAMERA2` | `0x7c` | Guarded camera payload; submode and flag gated. |

### `0x0003002a` Action-View Payload

`SYSTEM CAMER` action-view payloads are 0x24-byte records selected through the
common gate fields. The selected payload pointer is copied into the action-view
child worksheet, then dispatched by requested mode.

| Offset | Size | Field | Editor rule |
| --- | ---: | --- | --- |
| `0x00` | 2 | `primaryActionKey` | Expert selector; compared against the combatant action/source key. |
| `0x02` | 2 | `routeSecondaryKey` | Expert selector; special route value `2` is required by the alternate `0x0b`/`0x20` worksheet path. |
| `0x04` | 2 | `directSecondaryKey` | Expert selector; direct secondary key for primary actions `0x18`, `0x1d`, and `0x1e`. |
| `0x06` | 2 | `lowFlags` | Expert bitfield. Bit `0x2000` can force the action-view task state when global action-view state has `0x40`; bit `0x8000` is observed but unnamed. |
| `0x08` | 4 | `reserved08` | Preserve source bytes; zero in the profiled US corpus. |
| `0x0c` | 4 | `reserved0c` | Preserve source bytes; zero in the profiled US corpus. |
| `0x10` | 4 | `actionViewFlags` | Mode-family scoped bitfield; preserve unknown and inactive bits. |
| `0x14` | 4 | `modeLocalAngleOrOffset` | Mode-local float/word; yaw, Y/component offset, or yaw step depending on `requestedMode` and flags. |
| `0x18` | 2 | `startFrame` | Safe timing field; handlers wait until the child local timer reaches this frame. |
| `0x1a` | 2 | `reserved1a` | Preserve source bytes; zero in the profiled US corpus. |
| `0x1c` | 2 | `endFrame` | Safe timing field; the dispatcher completes when `startFrame < endFrame <= localTimer`. |
| `0x1e` | 2 | `holdFrameCount` | Mode-local countdown/setup field; directly owned by modes `0`, `1`, and runtime mode `2`. |
| `0x20` | 2 | `stepFrameCount` | Mode-local interpolation divisor/countdown; runtime mode `0x11` can write `0x000f` here. |
| `0x22` | 2 | `requestedMode` | Guarded mode selector; runtime can temporarily rewrite mode `0` to effective mode `0xe` and restores the saved source mode on cleanup. |

Known `actionViewFlags` bits:

| Bit | Current role |
| ---: | --- |
| `0x80000000` | Lifecycle stream/resource toggle around activation and cleanup when the global override is inactive. |
| `0x40000000` | Anchor combatant selector in modes `7`, `8`, `9`, `0x12`, `0xf`, and `0x10`. |
| `0x20000000` | Continuous refresh / yaw-advance path in anchor and yaw handlers. |
| `0x10000000` | Suppresses or changes combatant control toggles in several mode handlers. |
| `0x08000000` | Actor/target-anchor direct-position path; `modeLocalAngleOrOffset` becomes a direct Y/component offset. |
| `0x04000000` | Generated-yaw path in modes `0xa` and `0xb`. |
| `0x02000000` | Uses `modeLocalAngleOrOffset` as the yaw increment in modes `0xf` and `0x10` when continuous refresh is active. |
| `0x01000000` | Completion/stop gate tied to a combatant state flag in modes `0xf` and `0x10`. |
| `0x00001000` | Observed in serialized data, but no direct selected-payload consumer is known. |
| `0x00000800` | Vector-helper selector in modes `0xa` and `0xb`; implemented but not observed in the profiled US corpus. |

Mode ownership:

- Modes `0` and `1` own `holdFrameCount` and `stepFrameCount` as interpolation
  controls; runtime mode `2` follows the same handler shape but has no profiled
  serialized US rows.
- Modes `7`, `8`, `9`, and `0x12` own `modeLocalAngleOrOffset` as a camera or
  target Y/component offset, with flag `0x08000000` selecting the direct
  combatant-position path.
- Modes `0xa` and `0xb` own `modeLocalAngleOrOffset` only when flags
  `0x20000000` and `0x04000000` do not select fixed or generated yaw.
- Modes `0xf` and `0x10` own `modeLocalAngleOrOffset` only when flags
  `0x20000000` and `0x02000000` select payload-driven yaw advance.
- Mode `0x11` is runtime-only in current evidence and can mutate
  `stepFrameCount`; do not treat that write as a source-file edit.

Camera RNG eligibility:

- Serialized mode `0` records are RNG-eligible during dispatcher activation:
  one random draw can temporarily rewrite effective mode to `0xe` when the
  global camera override is inactive. Cleanup restores `requestedMode`.
- Synthetic action-view records are zero-filled 0x24-byte payload-like records
  created by runtime code. They always write `requestedMode`, may write
  `holdFrameCount`/`stepFrameCount` on the unobserved synthetic mode-`6` path,
  and are freed on child cleanup.

Semantic editing notes:

- Keep `%s_STD` action rows separate from `%s0_STD` entry payloads. Combatant
  action rows can install callbacks that later use action-view payload lists,
  but the two tables are different editing surfaces.
- `0x0003002a` records can suppress or replace the synthetic action-view camera
  path for an action key. The runtime count gate is equivalent to
  `CountMatchingStd0Entries(root, key, -1, 0x2a, 3)`: a nonzero count uses
  serialized action-view payloads, while a zero count may allow a synthetic
  action-view record. Do not infer camera behavior from a `%s_STD`
  `callbackIndex` alone.
- Treat action-view timing, mode, and camera-RNG eligibility as
  `0x0003002a` payload semantics. The `%s_STD` action row selects or schedules
  the visual/action callback path; it does not itself encode the final
  action-view record mode.
- Do not treat queued instruction parameter writes as persistent combatant
  worksheet `field6_0x6` writes. They are separate runtime state and only become
  STD selectors through specific lookup paths.
- Current static evidence keeps regular battle MLD resources adjacent to STD
  visual-resource setup rather than part of the STD entry-handler table. Do not
  model MLD named-function dispatch as an STD payload owner unless a later trace
  proves that bridge.

## Re-Export Rules

- Do not synthesize runtime-only rows.
- Preserve source bytes for every field outside the active owning surface.
- Entry payload bodies remain opaque byte spans unless a known semantic field
  is explicitly edited.
- Runtime-mutated fields must re-export from original source bytes unless the
  editor explicitly owns the file field.
