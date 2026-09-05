# SpiceModeling public API

`SpiceModeling` is the SoA-bounded C++ modeling library used by SPICE filetype projects. It is not a promise of complete SA3D or general Ninja format coverage.

The supported public umbrella exposes focused `ModelDocument` and `MotionDocument` values with standalone decode and encode services. Decoder-produced documents are read-only in this release: consumers can inspect their model hierarchy, motion data, target layout, and format, but cannot replace the owned semantic state.

The lower-level headers under `Animation`, `File`, `Mesh`, `ObjectData`, and `Structs` remain implementation details used by SpiceMLD and the modeling tests. They are intentionally absent from `SpiceModeling.h` and are not a stable consumer contract.

Texture bitstreams remain the responsibility of SpiceGvm and SpicePvm. MLD container identity, ordering, resource association, GRND/GOBJ ownership, opaque preservation, validation, and target writing remain the responsibility of SpiceMLD.
