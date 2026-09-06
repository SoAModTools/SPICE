# SpiceWMap

`SpiceWMap` owns the `WMAPAREA.BIN` world-map area grid. It does not own other files merely because they use the `.bin` extension.

The public model is an editable three-layer, 24-row, 28-column byte grid. The parser accepts either the raw `0x7e0`-byte payload or an AKLZ-wrapped payload, and the writer can emit either storage form. Cell IDs are restricted to the executable-supported range `0..15`.

Layer names, geographic labels, encounter-table associations, coordinate transforms, and runtime visibility state are consumer concerns and are not encoded in the document model.
