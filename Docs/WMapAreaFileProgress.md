# WMAPAREA File Progress

## Current Support

`SpiceWMap` parses `WMAPAREA.BIN` into an editable three-layer, 24-row, 28-column byte grid. It accepts raw and AKLZ-wrapped input, validates the exact decoded size and the `0..15` cell-ID range, and writes either raw or AKLZ output.

Synthetic tests cover grid ordering, validation, and both storage policies. A compatibility test compares available Dreamcast US, GameCube US, and AKLZ-wrapped GameCube JP files after decoding.

## Ownership Boundary

`SpiceWMap` owns only the serialized world-map grid. Runtime coordinate conversion, geographic naming, area-ID mapping, save-state visibility masks, and encounter selection remain consumer-side concerns until they have their own supported semantic contracts.
