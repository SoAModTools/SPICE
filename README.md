# SPICE

SPICE is the Skies Package Interchange and Content Encoder, a Windows C++20 toolkit for inspecting, converting, and editing assets from *Skies of Arcadia* and *Skies of Arcadia Legends*.

## Features

- Dedicated libraries for supported Dreamcast and GameCube file formats.
- Platform-neutral editable models where safe rewriting is supported.
- `SpiceGrinder`, a command-line interface for conversion and export workflows.
- `SpiceRack`, a Qt desktop application for MLD and paired SST/SML inspection plus GVR/PVR texture editing.
- PNG, JSON, Blender IR, and selected ALX CSV interchange workflows.

## Building

### Requirements

- Windows with the Windows SDK and MSVC v145 toolchain.
- Qt 6.10.3 for MSVC 2022 x64, registered with Qt VS Tools as `6.10.3_msvc2022_64`.

From the repository root, initialize dependencies and run the solution build from an elevated Developer PowerShell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" SPICE.sln /p:Configuration=Debug /p:Platform=x64
```

## Testing

The public `SpiceTests` suite is self-contained: its inputs are constructed synthetically and it does not require game files, extracted assets, or environment-variable opt-ins.

```powershell
.\x64\Debug\SpiceTests.exe
```

Compatibility testing against real game material is maintained separately in a private test repository. Corpus files remain outside every Git working tree and are never required for the public build or test workflow.

## Usage

Use the command-line help for the current command list and command-specific options:

```powershell
.\bin\x64\Debug\SpiceGrinder.exe --help
```

The cross-platform research audit accepts explicit Dreamcast and GameCube corpus roots and writes only manifests and semantic comparison reports:

```powershell
.\bin\x64\Debug\SpiceGrinder.exe audit-dreamcast-parity --dreamcast-us <dir> --gamecube-us <dir> --output <dir>
```

Launch the desktop application with:

```powershell
.\bin\x64\Debug\SpiceRack.exe
```

SpiceRack currently opens MLD, paired SST/SML, GVR, and PVR documents and can create GVR or PVR textures from PNG images. Opening either member of an SST/SML pair requires its same-directory, same-stem companion and presents the pair as one read-only battle-stage document.

## Blender importing

SpiceRack is able to export the contents of MLD files for import into Blender using a Blender IR .json format. To use this, load the blender importer script at `SpiceMLD\blender\spice_blender_ir_importer.py` as an addon in blender (Using `Edit -> Preferences -> Add-ons`). You can then use `File -> Import -> Spice Blender IR (.json)` to import Blender IR files. This is still under active development, so it is not yet fully accurate.

## Supported formats

Support varies by format. Some formats have semantic editors and writers, while others currently provide conservative parsing or research exports only.

| Format           | Project       | Current support                                                                                                                    |
| ---------------- | ------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| AKLZ             | `Compression` | Compresses and decompresses the wrapper used by many GameCube assets.                                                              |
| `.bin`           | `SpiceBin`    | Provides endian-aware read-only probing for known indexed HRS/UI families; `.bin` is not treated as one universal format.          |
| `WMAPAREA.BIN`   | `SpiceWMap`   | **Import: yes. Edit: yes. Write: yes.** Supports the headerless `3 x 24 x 28` world-map grid as raw or AKLZ data.                    |
| `.ect`           | `SpiceEct`    | **Import: yes. Edit: yes. Write: yes.** Dreamcast and GameCube encounter tables share one platform-neutral model.                  |
| `.gvm` / `.gvr`  | `SpiceGvm`    | Parses GVM archives and decodes, creates, or edits GVR textures with PNG interchange.                                              |
| `.mld`           | `SpiceMLD`    | **Import: yes. Edit: MLD-owned content. Write: supported targets.** Embedded `SpiceModeling` documents remain read-only; Blender IR is a one-way projection. |
| `.mlk`           | `SpiceMlk`    | Provides endian-aware read-only battle-resource inspection, corpus reports, and embedded-MLD Blender IR exports.                   |
| `.mll`           | `SpiceMll`    | Parses big- and little-endian member archives and conservatively rebuilds them in their source endian.                             |
| `.pvm` / `.pvr`  | `SpicePvm`    | Parses, decodes, and encodes Dreamcast texture archives and textures.                                                              |
| `.sct`           | `SpiceSCT`    | **Import: yes. Edit: yes. Write: yes.** Canonical and preserving export paths are available while some opcode semantics remain incomplete. |
| `.sml` / `.sst`  | `SpiceSstSml` | **Import: yes. In-memory mutation: yes. Paired write: no.** Validation, analysis, and secondary materialization do not constitute container persistence. |
| `.std`           | `SpiceStd`    | **Import: yes. Edit: yes. Write: yes.** Either byte order and raw or AKLZ output are supported, subject to documented opaque-content restrictions. |
| ALX 5.0.0 `.csv` | `SpiceTrade`  | **Import: yes. Edit: yes. Write: no.** Fifteen whitelisted tables are published atomically as typed semantic models.                |

## Applications and supporting projects

| Project         | Purpose                                                                                                                   |
| --------------- | ------------------------------------------------------------------------------------------------------------------------- |
| `SpiceGrinder`  | Command-line interface for conversions, exports, and research operations.                                                 |
| `SpiceRack`     | Qt desktop interface for inspecting ECT, MLD, and paired SST/SML documents and editing standalone or embedded GVR/PVR textures. |
| `SpiceMix`      | Frontend-neutral operation and editable-document layer shared by SpiceGrinder and SpiceRack.                              |
| `SpiceRoot`     | Common endian, alignment, FourCC, and binary I/O primitives.                                                              |
| `SpiceModeling` | Read-only C++ model and motion documents plus the supported Skies of Arcadia block codecs; editing is intentionally disabled in this release. |
| `SpiceTests`    | Corpus-free GoogleTest suite using synthetic inputs only.                                                                 |

## Documentation

- [`Docs/`](Docs/) contains format layouts, implementation status, and known gaps.
- [`SpiceMLD/blender/`](SpiceMLD/blender/) contains the Blender IR importer and its usage notes.
- [`SpiceTrade/README.md`](SpiceTrade/README.md) documents the intentionally narrow ALX CSV compatibility surface.

## Acknowledgements

SPICE incorporates or builds on work from:

- [SA3D.Modeling](https://github.com/X-Hax/SA3D.Modeling) and the X-Hax contributors, which provide the upstream model and animation reference for `SpiceModeling`.
- SALSA, which provides reference metadata and terminology for SCT instructions.
- ALX 5.0.0, which provides the compatibility target for selected CSV tables.
- LodePNG, used for PNG encoding and decoding.

Third-party components remain subject to their respective licenses and notices.

SPICE is an independent fan and research project and is not affiliated with or endorsed by the rights holders of *Skies of Arcadia*.
