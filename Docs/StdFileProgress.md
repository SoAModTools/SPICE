# STD File Progress

## Current Support

**Capability:** Import, in-memory editing, validation, and whole-file writing are supported, subject to receipt and same-byte-order requirements for preservation-only opaque content.

`SpiceStd` provides the canonical pipeline `STD bytes -> StdDocument + receipt -> validation -> target bytes`. It recognizes action-row documents and kind-4 entry tables, including terminator-only entry tables. Import accepts raw or AKLZ input in either byte order, auto-detects byte order from decoded structure, and supports an authoritative caller override.

The editable document contains semantic and layout state only. Source path, hashes, sizes, compression, detected byte order, and opaque-preservation evidence live in the separate import receipt. Writers accept independent platform and compression choices, supporting raw and AKLZ output for both Dreamcast and GameCube byte orders.

Entry-table output derives record counts, spans, payload sizes, and offsets after insertion, deletion, reordering, or payload resizing. Stable typed local IDs keep document references independent of vector position. Diagnostics expose stable codes, severity, text, and optional decoded offsets.

## Deliberate Limits

All eleven commands classified by the inspected binary command table have dedicated typed, relocatable payload variants. Genuinely unrecognized command payloads, payload-area gaps, and file trailers remain explicit preservation content and cannot cross byte order. Top-level opaque documents are preservation surfaces rather than semantic editing surfaces.

`StdUsage` remains a source-oriented research utility, and the versioned JSON exporter remains a secondary interchange adapter rather than a version of `StdDocument`. Its schema name is `spice_std_json_export` and its independent numeric `schemaVersion` is 5. Cross-file links to MLD, combatants, actions, or other systems are consumer-owned. Additional payload semantics await evidence and consumer feedback before the mutable contract is considered for freezing.
