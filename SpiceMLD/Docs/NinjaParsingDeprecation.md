# Legacy Ninja Parsing Removal (SA3D Port Preparation)

The legacy NJCM/NJTL ("Ninja") parsing stack in `SpiceMLD/Parsing` has been
removed from the active SpiceMLD parser implementation.

## Removed modules

- `NJCMParser` module and NJCM record/parity helper files
- `NJTLParser` module
- SA Tools parity decode path (`NJCMParityPath` and `SaToolsParity*`)
- Legacy NJCM/NJTL decode integration points in `MldParser`

## What is intentionally preserved

- Intermediate representation (IR) models under `SpiceMLD/Model` (including
  Blender-style IR structures)
- IR builders/exporters used for transition and validation workflows

## Runtime behavior

- `MldParser` now emits a migration diagnostic noting that legacy Ninja parsing
  has been removed and that object addresses are retained for SA3D IR work.
