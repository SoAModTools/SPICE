# SPICE Disc Inventory

`spice_disc_inventory.py` scans an extracted Skies of Arcadia disc dump and
produces heuristic reports for filetype coverage work. It is dependency-free and
uses only the Python standard library.

## Usage

Requires Python 3.10 or newer. Replace `python` with the desired interpreter
path if the launcher is not available on your machine.

```powershell
python tools\disc_inventory\spice_disc_inventory.py `
  --disc-root <local-corpus>\2003-03-05-gc-eu-final_Skies_of_Arcadia_Legends `
  --out $env:TEMP\spice_disc_inventory
```

Optional flags:

- `--max-samples-per-extension N`: representative sample cap, default `8`.
- `--unsupported-only`: only include unsupported candidates in
  `disc_inventory.json`.

## Outputs

- `disc_inventory.json`: extension counts, directory distribution, size stats,
  sample files, AKLZ header/decompression status, and simple magic guesses.
- `unsupported_payload_signatures.json`: bounded byte previews and external
  handler leads for unsupported candidate formats, including decompressed
  header fingerprints, big-endian word previews, and printable string samples
  when available.
- `sct_resource_references.json`: heuristic SCT resource references matched
  against unsupported file stems and filenames. AKLZ-wrapped SCT payloads are
  decompressed in memory before scanning when possible.
- `unsupported_format_analysis.json`: aggregate header clusters, stem-pair
  groups, and parser-target recommendations for unsupported candidates.
- `unsupported_priority_report.md`: ranked human-readable triage report.

The tool does not write raw game payloads or decompressed files. AKLZ support is
in-memory only: it detects the magic, records the big-endian decompressed-size
field, and uses decompressed bytes for bounded previews and SCT reference scans.
