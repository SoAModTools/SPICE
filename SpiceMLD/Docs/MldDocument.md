# MLD document API

`MldDocument` is the consumer-neutral, mutable representation of an MLD container. It does not contain a source platform, region, byte order, compression flag, source path, parse status, diagnostics, or whole source/decoded byte image.

The document owns separate entry, object, motion, ground, texture-list, texture-archive, and opaque-member collections. Every member has a stable document-local ID. Entry slots refer to resources through those IDs, and the top-level `layout` records cross-category ordering without storing source offsets in semantic objects.

Objects and motions use strict decoded-or-opaque payload variants. Decoded model and motion payloads are read-only `SpiceModeling` documents in this release. MLD-owned entry fields, lists, GRND/GOBJ data, texture lists, textures, and opaque bytes remain directly mutable.

`MldDocumentImporter` automatically identifies Dreamcast little-endian raw input and GameCube big-endian raw or AKLZ-wrapped input. Region is not an MLD parsing concept. Source observations and layout evidence are returned in the copyable `MldImportReceipt`, separately from the document.

Output is explicit through `MldDocumentWriter::write(document, target, receipt)`. Dreamcast output is raw; GameCube output may be raw or AKLZ-wrapped. A receipt is required whenever opaque content must be preserved. `MldDocumentValidator` applies the same target rules before writing and rejects dangling IDs or collection mutations that the current encoder cannot represent.

`MldBlenderIrProjector` is the supported one-way Blender bridge. It accepts only an `MldDocument`; Blender-specific state and Blender IR are not canonical MLD content.

The older `MldFile`, `MldParser`, and `MldFileWriter` types are retained only as internal migration machinery and are intentionally absent from the `SpiceMLD.h` public umbrella.
