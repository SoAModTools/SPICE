# STD File Progress

## Current Support

SPICE recognizes the two STD forms in little-endian raw Dreamcast files and big-endian raw or AKLZ-wrapped GameCube files: fixed action-row tables and sentinel-terminated entry tables with separately addressed payloads. It records source endian, supports a forced-endian research path, and preserves that endian when writing. AKLZ output remains GameCube-only.

Action rows and entry payloads are intentionally separate models. An action row selects a callback family; an entry’s combined opcode/location code selects a payload family. Shared action keys can relate the two at runtime, but they do not make them one serialized table.

## Known Limitations

Many action-row fields are callback-local, and many payload fields are mode- or flag-dependent. They cannot be edited safely from their offset alone. Unknown callback indices, flags, modes, and entry types remain preserve-only. Runtime-created rows and runtime pointer fixups are not source records and are never synthesized as file content.

Current semantic support is strongest for selected camera, icon, stream, sound/effect, character, and model-placement payload families, but some field labels remain provisional.
