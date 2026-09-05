# SST/SML File Progress

## Current Support

SPICE reads little-endian raw Dreamcast and big-endian raw or AKLZ-wrapped GameCube SML/SST stage pairs. Byte order is selected from structural candidates or can be forced for corpus research; paired members must agree. Embedded SML payloads are imported as canonical `MldDocument` values when structurally valid and otherwise retained as explicit opaque resources. SST command payloads use canonical typed generic fields, specialized type-0 placement and type-1 lighting entities, and offset-addressed opaque fragments rather than duplicate full payload buffers. Sentinels, raw record words, alignment, and post-command tail bytes are retained.

The current model also identifies the first block’s 9x9 battle-grid source when enough tail data is present. Known command types can be summarized semantically without requiring the parser to reproduce their runtime objects.

SpiceRack provides a read-only paired SST/SML workbench. Opening either member resolves the same-directory, same-stem companion and exposes:

- pair metadata, record-count agreement, compression state, and command histograms;
- paired SML resource and SST command records;
- a nested inspector for each successfully parsed embedded MLD, including entries, textures, decoded previews, and diagnostics;
- command payload fields, lighting rows, consumer windows, reconstructed raw payload views, and local-slot links to stable nested MLD entry IDs where resolvable;
- the currently identified 9x9 battle-grid source and the research-backed runtime row layout.

Field evidence is displayed explicitly as Gekko-derived, Gekko-and-corpus, corpus-stable, code-supported but unobserved, or provisional. These labels remain GameCube-derived until Dreamcast corpus comparison corroborates the runtime meaning. The workbench consumes the binary parser IR directly and does not require an annotation repository.

## Known Limitations

Command payload field names remain incomplete and several meanings depend on the selected model or stage context. Types 6 and 7 are structurally supported but have not been observed in the known stage data. Type 11 has a fixed walker span plus additional nearby fields, so those bytes are preserved separately rather than folded into an enlarged universal record.

Editing and repacking require care because SML payload offsets, SST block offsets, payload spans, alignment, and tail boundaries are interdependent. Unknown command fields and tail bytes must remain unchanged unless their ownership is established.

The SpiceRack workbench intentionally does not edit or save the paired stage container in this slice. Secondary extraction, research JSON, stage annotation, and combined Blender IR remain supported over the canonical document. A malformed embedded MLD becomes an opaque record-local resource without rejecting an otherwise structurally valid pair; structural SST/SML parse failures reject the document.
