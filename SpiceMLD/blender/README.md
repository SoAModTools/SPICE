# Spice Blender IR Importer: Install and Use

This folder contains a baseline Blender add-on script:

- `spice_blender_ir_importer.py`

## Install in Blender

1. Open Blender.
2. Go to **Edit → Preferences → Add-ons**.
3. Click **Install...**.
4. Select `SpiceMLD/blender/spice_blender_ir_importer.py`.
5. Enable the add-on named **Spice Blender IR Importer**.

Alternative manual install:

- Copy `spice_blender_ir_importer.py` to your Blender add-ons folder (for your Blender version), then enable it from **Preferences → Add-ons**.

## Access after install

Once enabled, open:

- **File → Import → Spice Blender IR (.json)**

You can also press **Ctrl+Alt+I** to open the Spice Blender IR JSON import file picker directly.

That opens the importer operator (`import_scene.spice_blender_ir`) with options:

- **Collection** (target collection name, default `Spice_Imported`)
- **Clear Target Collection** (clear previous imported objects before loading)
- **Emit Parity Debug Log** (writes per-entry/per-node local + world transform matrices into a Blender Text datablock named `<Collection>_ParityDebug`)
- **Object Tree Mode** (`Armature` by default; `Empty Debug` preserves the older empty-per-node layout for transform parity checks)
- **Preview Motion Slot** (optional motion slot to assign as the active Action in the Action Editor; all slots are still imported as separate Actions)
- **Create NLA Tracks** (off by default; creates muted NLA strips for users who want an NLA overview)

Then select your exported `blender_ir_scene.json` and import.

## What this baseline importer handles

- Mesh vertices (positions)
- Triangle indices from triangle-set corners
- Basic materials and texture hookups by `textureName`
- Decoded RGBA8 texture payloads (`pixelDataBase64`)
- Instance placement from `indexEntries` transforms
- SA3D object trees as one armature per tree by default, with bones named after source node indices.
- Node-transform animations as one armature Action per motion slot. Actions are kept with fake users and can be selected from the Action Editor. The active Action is the first slot by default, or the slot named in **Preview Motion Slot**.
- Weighted and rigid meshes through armature modifiers and vertex groups named after source node indices.
- Empty-per-node import through **Object Tree Mode: Empty Debug** for parity/debugging.
- Optional muted NLA strips through **Create NLA Tracks**. This is disabled by default so the Action Editor remains the primary animation slot selector.

It is intentionally baseline and aligned to current JSON export.

## Triangle metadata display

GRND and GOBJ triangle sets may carry three raw `u16` metadata words per face.
The importer preserves them as `spice_triangle_metadata_raw_u16_0` through
`spice_triangle_metadata_raw_u16_2` `FACE` attributes. It also creates derived
runtime-decoder attributes and a `SpiceTriangleMetadataColor` `CORNER` color
attribute. Raw attributes are authoritative for future export; decoded values
and colors are visualization aids.

Open **3D View -> Sidebar -> SPICE -> Triangle Metadata** to:

- show attributed sky-rift force regions by exact runtime force class;
- show encounter-selector regions from the authored tens digit;
- resolve Area 99 encounter zone and table ID per visible surface position;
- resolve dungeon encounter table IDs directly from selectors `1..7`;
- checker the sky-rift force and resolved encounter layers without merging them;
- switch between inferred Area 99 lookup pages and surface/forced altitude bands;
- show remaining payload groups and unclassified decoded classes in the
  unattributed-values view;
- inspect raw words or stream winding separately;
- adjust metadata material opacity;
- select an imported ground entry and show a mode-specific key for that entry or
  all unique metadata meshes in the scene;
- optionally show the advanced geometry key, which lists exact raw triplets and
  face counts;
- refresh colors after editing raw face attributes.

Material Preview and Rendered shading display the color-node material directly.
For Solid shading, set the viewport color source to **Attribute**.

Resolved encounter maps use an unlit OKLCH-derived palette so terrain lighting
does not change categorical brightness. Encounter zones share evenly spaced hue
pairs with alternating bright and dark lightness; table IDs add bounded variation
inside each zone's hue region. A resolved zone or table ID of zero is neutral gray.
Resolved maps use a separate opacity setting that defaults to `0.45`; authored
metadata views retain the `0.25` default.

The JSON field is `triangleMetadata`. The former `collisionTriangles` field and
`spice_collision_*` Blender attributes are intentionally unsupported after the
clean naming migration; regenerate JSON and reimport older scenes.

## Triangle metadata layers

The sky-rift-force view recognizes decoded high-bit classes `0` through `7`
and class `30`, which are the classes dispatched by the researched runtime
force path. Other decoded classes are not labeled as force behavior.

The encounter-selector view uses only the authored decimal tens digit. Payload
groups and unclassified high-byte classes do not influence encounter resolution
and appear only in the unattributed-values view.

The encounter selector is context-dependent: dungeon maps use it directly as a
table ID, while area 99 uses it as a local lane for a separate contextual lookup.
For Area 99, the importer finds the single `fldEfcontrol` entry with TBLID `5300`
and 504 unsigned function parameters, stores those raw parameters in a Blender
Text datablock, and builds a nearest-filtered `56x18` lookup image for the chosen
page. The material recovers source coordinates from Blender world position and
resolves every shading point independently, so overlapping surfaces remain
visible and large triangles change color exactly at lookup bucket boundaries.
The page labels are explicitly inferred and do not assert game-state semantics.

`indexEntries[].functionParameters` is the authoritative Blender IR source for
this lookup. Values are unsigned U32 decimal numbers; derived images, node trees,
colors, zones, and table IDs are disposable visualization data.
