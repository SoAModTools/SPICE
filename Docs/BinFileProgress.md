# BIN File Progress

## Current Support

`SpiceBin` parses raw or AKLZ-wrapped data and can identify big- or little-endian indexed HRS/UI-layout tables used by several battle and field interfaces. The selected endian is recorded for confirmed indexed files, and callers may force it during corpus research. The same probe can be used for loose files and BIN members extracted from MLL containers.

The indexed family is deliberately not applied to every `.bin` file. Known UI resources can share a purpose without sharing a binary structure, and nonmatching payloads remain preserved rather than forced into the indexed model.

## Known Limitations

The format does not identify its companion texture or material bank by itself. Context selectors therefore remain raw values unless the caller supplies the surrounding asset context. Several element fields and the exact names of the top-level extent fields are still unknown. Editing should preserve those bytes and unresolved selectors.

`SpiceBin` does not parse `WMAPAREA.BIN`, encounter files, VMU executables, or executable images. `WMAPAREA.BIN` is handled by `SpiceWMap`. Other unresolved BIN families require an explicit owner before implementation.

## Encounter BIN Ownership

Encounter-related `.bin` files already handled by ALX remain ALX-owned. SPICE does not duplicate their native parsers or writers.

| Disc family | ALX native handler | ALX CSV tables |
| --- | --- | --- |
| Dreamcast `FIELD/A###X_EP.BIN` and GameCube `field/*_ep.enp` | `EnpFile` | `enemyencounter.csv`, `enemy.csv`, `enemytask.csv` |
| Dreamcast `FIELD/A099A_EP.BIN` and GameCube Area 99 multi-ENP data | `EnpFile` multi-segment form | The same three tables; each embedded member is an `enemyencounter.csv` owner group |
| Dreamcast `BATTLE/EPEVENT.BIN` and GameCube `battle/epevent.evp` | `EvpFile` | `enemyevent.csv`, with embedded enemy/task data represented in `enemy.csv` and `enemytask.csv` |
| Dreamcast `FIELD/R###X.BIN` and GameCube `field/r*.tec` | `TecFile` | `enemyshiptask.csv` |

`SpiceTrade` provides import-only typed views for its existing ALX CSV whitelist, including `enemy.csv`, `enemyencounter.csv`, `enemyevent.csv`, and `enemytask.csv`. That compatibility surface does not transfer native encounter-file ownership into SPICE, and `enemyshiptask.csv` remains outside the SpiceTrade whitelist.
