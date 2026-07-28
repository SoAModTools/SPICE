# STD File Progress

This document tracks STD semantic-editing guidance, re-export policy, and workflow notes that are not strictly file layout.

## References And Context

Core binary layout and semantic-editing contract for Skies of Arcadia Legends `.std` files in `SpiceStd`.

Disc `.std` files are AKLZ-wrapped. Parse all layout fields from the decompressed byte stream. Numeric fields are big-endian.

## Semantic Editing Notes

- Keep `%s_STD` action rows separate from `%s0_STD` entry payloads. Combatant action rows can install callbacks that later use action-view payload lists, but the two tables are different editing surfaces.
- `0x0003002a` records can suppress or replace the synthetic action-view camera path for an action key. The runtime count gate is equivalent to `CountMatchingStd0Entries(root, key, -1, 0x2a, 3)`: a nonzero count uses serialized action-view payloads, while a zero count may allow a synthetic action-view record. Do not infer camera behavior from a `%s_STD` `callbackIndex` alone.
- Treat action-view timing, mode, and camera-RNG eligibility as `0x0003002a` payload semantics. The `%s_STD` action row selects or schedules the visual/action callback path; it does not itself encode the final action-view record mode.
- Do not treat queued instruction parameter writes as persistent combatant worksheet `field6_0x6` writes. They are separate runtime state and only become STD selectors through specific lookup paths.
- Current static evidence keeps regular battle MLD resources adjacent to STD visual-resource setup rather than part of the STD entry-handler table. Do not model MLD named-function dispatch as an STD payload owner unless a later trace proves that bridge.

## Re-Export Rules

- Do not synthesize runtime-only rows.
- Preserve source bytes for every field outside the active owning surface.
- Entry payload bodies remain opaque byte spans unless a known semantic field is explicitly edited.
- Runtime-mutated fields must re-export from original source bytes unless the editor explicitly owns the file field.
