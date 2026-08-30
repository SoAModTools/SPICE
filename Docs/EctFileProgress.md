# ECT File Progress

## Current Support

SPICE supports both known ECT layouts: ordinary flat encounter-table sequences and the indexed overworld form used by `A099A.ECT`. Each table exposes its stage, overall encounter rate, and 32 encounter ID/rate pairs. The overworld model also exposes entry titles and the eight tables referenced by each index row.

Dreamcast raw little-endian input and GameCube AKLZ-wrapped big-endian input are converted to the same editable encounter model. Files can be written back to either platform, with platform byte order, outer compression, index offsets, and fixed container values rebuilt as needed.

SpiceRack provides read-only inspection for both layouts, including platform and envelope metadata, indexed-title navigation, ordered table summaries, all 32 encounter slots, and parser diagnostics. ECT editing remains library-only for now.

## Known Limitations

The indexed layout is selected through the known `A099A.ECT` identity; no independent format marker has been confirmed for other filenames. Entry titles are preserved, but the game-level meaning of each of the eight tables in an overworld entry is not yet named. Encounter IDs and rates are represented faithfully without attempting to resolve them to external enemy, area, or script databases.
