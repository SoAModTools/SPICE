# MLD File Progress

## Current Support

SPICE reads GameCube big-endian and Dreamcast little-endian MLD files, including AKLZ-wrapped GameCube input. It models the top-level index, counted link and address lists, entry transforms and names, texture lists, texture archives, and bounded raw payload blocks. Texture lists are independent owning resources rather than attempted object models, including direct NJTL/GJTL, wrapper, and counted-record layouts. GRND collision surfaces, GOBJ geometry, NJCM chunk models, Ninja motion resources, and non-rendering Ninja volume polygon chunks are structurally decoded. GameCube GVR and Dreamcast PVR archive entries are represented through their respective texture projects.

Container health and asset coverage are reported separately. `parseStatus` covers structural integrity, while `assetStatus` aggregates per-resource `Empty`, `Complete`, `Partial`, and `Failed` states. Resource limitations do not prevent byte-preserving no-edit output when the container itself is writable.

Known fields can be written in either platform byte order while unknown source bytes are preserved. The semantic writer supports texture replacement and archive relocation. GRND/GOBJ triangles retain decoded-payload provenance for selector patching in uncompressed Dreamcast/GameCube files and AKLZ-wrapped GameCube replacements.

## Known Limitations

MLD is not yet a complete semantic reassembler. Motion and NJCM model resources are read-only and byte-preserved; there is no motion encoder, runtime motion-selection resolver, ambiguous-animation export, or volume renderer. Same-entry unique bindings are explicitly scoped convenience projections rather than claims about handler or script selection. Decoded NCAM position/target channels project to Blender cameras, and type-56 volume triangles are available as optional non-rendering Blender visualization geometry. Handler-specific meanings are not inferred from entry function names or parameters. GRND and GOBJ support covers the promoted structures only, and several pointer, padding, and texture-control fields remain unnamed. Geometry edits outside the canonical GRND/GOBJ surfaces can therefore require raw preservation.

Triangle selector editing intentionally does not determine whether a selector is meaningful for a particular area or object.
