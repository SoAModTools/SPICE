# WMAPAREA File Layout

`WMAPAREA.BIN` is a headerless world-map area grid. It is not part of the indexed HRS/UI family handled by `SpiceBin`.

## Storage

The decoded payload is exactly `0x7E0` bytes. Known files may be stored raw or wrapped in AKLZ. The decoded grid contains bytes only, so it has no byte-order distinction.

## Grid

The complete payload is:

```cpp
uint8_t cells[3][24][28];
```

The serialized offset of a cell is:

```text
offset = layer * 0x2A0 + row * 0x1C + column
```

| Dimension | Count | Stride |
| --- | ---: | ---: |
| Layer | 3 | `0x2A0` |
| Row per layer | 24 | `0x1C` |
| Column per row | 28 | `0x01` |

## Cell IDs

Serialized cell IDs are in the range `0..15`. The game rejects values above 15. Raw value 15 follows a special executable-side fallback path, but it remains a valid serialized value.

Numeric world-map modes `0`, `1`, and `2` select layers `0`, `1`, and `2`, respectively. Human-readable layer names, geographic area names, coordinate transforms, encounter-table associations, conditional ID remapping, and runtime visibility masking are not encoded in this file and remain outside the `SpiceWMap` document model.
