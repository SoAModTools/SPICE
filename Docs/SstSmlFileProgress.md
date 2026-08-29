# SST/SML File Progress

## Current Support

SPICE reads raw or AKLZ-wrapped SML/SST stage pairs, validates their shared record count, and exposes the SML embedded-resource table and SST command-block table. Embedded SML payloads are bounded and passed to the MLD project, while SST command payloads are split using the known type-to-size mapping. Sentinels, raw record words, alignment, and post-command tail bytes are retained.

The current model also identifies the first block’s 9x9 battle-grid source when enough tail data is present. Known command types can be summarized semantically without requiring the parser to reproduce their runtime objects.

SpiceRack provides a read-only paired SST/SML workbench. Opening either member resolves the same-directory, same-stem companion and exposes:

- pair metadata, record-count agreement, compression state, and command histograms;
- paired SML resource and SST command records;
- a nested inspector for each successfully parsed embedded MLD, including entries, textures, decoded previews, and diagnostics;
- command payload fields, lighting rows, consumer windows, raw payload bytes, and local-slot links;
- the currently identified 9x9 battle-grid source and the research-backed runtime row layout.

Field evidence is displayed explicitly as Gekko-derived, Gekko-and-corpus, corpus-stable, code-supported but unobserved, or provisional. The workbench consumes the binary parser IR directly and does not require an annotation repository.

## Known Limitations

Command payload field names remain incomplete and several meanings depend on the selected model or stage context. Types 6 and 7 are structurally supported but have not been observed in the known stage data. Type 11 has a fixed walker span plus additional nearby fields, so those bytes are preserved separately rather than folded into an enlarged universal record.

Editing and repacking require care because SML payload offsets, SST block offsets, payload spans, alignment, and tail boundaries are interdependent. Unknown command fields and tail bytes must remain unchanged unless their ownership is established.

The SpiceRack workbench intentionally does not edit, save, export, or build a combined 3D scene in this slice. A malformed embedded MLD is reported on its record without rejecting an otherwise structurally valid pair; structural SST/SML parse failures reject the document.
