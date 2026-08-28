# SpiceGrinder MLD A/B script (Windows)

This script runs A/B mode in `SpiceGrinder`:

- `sa3d_port` (C++ path)
- `.NET sa3d` bridge reference path

A/B mode is selected with the `compare-mld-sa3d` command.
When A/B mode is active, `SpiceGrinder` automatically:

- discovers the bridge executable relative to `SpiceGrinder.exe`,
- runs slices `1..9`,
- emits per-fixture NJ block manifests,
- invokes the bridge once per fixture per slice using the block manifest protocol.

## Scripts

- `run_ab.bat`
  - Runs A/B mode for provided input/output directories.
  - Requires the parser executable, input directory, and output directory.

## Usage

From repo root (example):

```bat
SpiceGrinder\MLDScripts\run_ab.bat ^
  ".\bin\x64\Release\SpiceGrinder.exe" ^
  "<input-dir>" ^
  "<output-dir>"
```

The wrapper supplies `compare-mld-sa3d --input ... --output ...`; it does not
provide implicit filesystem defaults.
