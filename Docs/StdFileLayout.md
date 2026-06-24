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

Semantic editing notes:

- Keep `%s_STD` action rows separate from `%s0_STD` entry payloads. Combatant
  action rows can install callbacks that later use action-view payload lists,
  but the two tables are different editing surfaces.
- `0x0003002a` records can suppress or replace the synthetic action-view camera
  path for an action key. Do not infer camera behavior from a `%s_STD`
  `callbackIndex` alone.
- Do not treat queued instruction parameter writes as persistent combatant
  worksheet `field6_0x6` writes. They are separate runtime state and only become
  STD selectors through specific lookup paths.

## Re-Export Rules

- Do not synthesize runtime-only rows.
- Preserve source bytes for every field outside the active owning surface.
- Entry payload bodies remain opaque byte spans unless a known semantic field
  is explicitly edited.
- Runtime-mutated fields must re-export from original source bytes unless the
  editor explicitly owns the file field.
