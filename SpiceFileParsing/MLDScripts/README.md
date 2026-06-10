# SpiceFileParsing MLD A/B script (Windows)

This script runs A/B mode in `SpiceFileParsing`:

- `sa3d_port` (C++ path)
- `.NET sa3d` bridge reference path

A/B mode is enabled via `--ab-sa3d-port-vs-sa3d-bridge`.
When A/B mode is active, `SpiceFileParsing` automatically:

- discovers the bridge executable relative to `SpiceFileParsing.exe`,
- runs all slices `0..9`,
- emits per-fixture NJ block manifests,
- invokes the bridge once per fixture per slice using the block manifest protocol.

## Scripts

- `run_ab.bat`
  - Runs A/B mode for provided input/output directories.
  - If parser exe is omitted, defaults to `..\..\bin\x64\Debug\SpiceFileParsing.exe`.

## Usage

From repo root (example):

```bat
SpiceFileParsing\MLDScripts\run_ab.bat ^
  "build\bin\Release\SpiceFileParsing.exe" ^
  "SpiceFileParsing\inputs" ^
  "SpiceFileParsing\parsed\ab"
```

With defaults:

```bat
SpiceFileParsing\MLDScripts\run_ab.bat
```
