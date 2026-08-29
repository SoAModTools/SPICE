# MLK File Progress

## Current Support

SPICE decompresses MLK files, reads the big-endian header and `0x10`-byte record table, validates keys and payload spans, and classifies bounded payloads. Most ordinary records contain MLD-like data that can be passed to the MLD parser for models, textures, entry names, and transforms. Unknown or non-MLD payloads remain visible and preserve their original bounds.

The scanner can recognize a small set of malformed or variant table shapes without silently rewriting them. Record keys, offsets, sizes, and `rawWord12` are retained for correlation with the embedded resources.

## Known Limitations

MLK writing and repacking are not yet supported. A few files have incoherent header counts or record spans and require subtype-specific understanding before they can be treated as editable containers. `rawWord12` is still unresolved, and payload classification does not replace parsing by the payload’s owning project.

Filename families provide useful hints about battle-resource grouping, but several roles and split-package suffixes remain provisional and should not be used as hard parsing rules.
