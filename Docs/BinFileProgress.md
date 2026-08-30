# BIN File Progress

## Current Support

SPICE parses raw or AKLZ-wrapped BIN data and can identify big- or little-endian indexed HRS/UI-layout tables used by several battle and field interfaces. The selected endian is recorded for confirmed indexed files, and callers may force it during corpus research. The same probe can be used for loose files and BIN members extracted from MLL containers.

The indexed family is deliberately not applied to every `.bin` file. Known UI resources can share a purpose without sharing a binary structure, and nonmatching payloads remain preserved rather than forced into the indexed model.

## Known Limitations

The format does not identify its companion texture or material bank by itself. Context selectors therefore remain raw values unless the caller supplies the surrounding asset context. Several element fields and the exact names of the top-level extent fields are still unknown. Editing should preserve those bytes and unresolved selectors.

The fixed world-map table represented by `wmaparea.BIN` is recognized as a separate family but is not yet decoded into entries. Other BIN families may exist.
