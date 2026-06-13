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
