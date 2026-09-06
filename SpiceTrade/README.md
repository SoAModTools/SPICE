# SpiceTrade

`SpiceTrade` is an import-only static library for typed interchange with CSVs produced by ALX 5.0.0. It fills selected editing gaps without taking ownership of game file formats already modeled by another SPICE project.

**Capability:** Atomic import and in-memory editing of typed whitelisted tables are supported. CSV writing and publication are not supported.

The hard-cut public surface imports exactly these tables:

- `enemy.csv`
- `enemyencounter.csv`
- `enemyevent.csv`
- `enemytask.csv`
- `enemymagic.csv`
- `accessory.csv`
- `armor.csv`
- `usableitem.csv`
- `weapon.csv`
- `weaponeffect.csv`
- `expcurve.csv`
- `character.csv`
- `charactermagic.csv`
- `charactersupermove.csv`
- `magicexpcurve.csv`

Each table has a typed importer and semantic record model. `AlxDatasetImporter` imports a requested set atomically: a missing or invalid table prevents publication of the entire dataset. It can also request the complete whitelist. There is intentionally no writer, change tracker, or generic public CSV document/workspace API in this release.

Every successful table import also returns source metadata containing the logical path, raw byte size, and raw SHA-256. This provenance remains outside the editable table and does not imply relationships between independently imported CSVs.

Ordinary records use stable identities of the form `<table>.<entryId>`. Enemy tasks use `enemytask.<ecId>.<entryId>` because their entry IDs are local to an enemy. Enemy encounters use their ENP owner and entry ID. Membership, identity, and order are fixed by import; consumers may edit record fields through each table's `edit` function.

For `enemy.csv` and `enemytask.csv`, only rows whose `[Filter]` cell is exactly `*` are canonical gameplay records. Enemy encounters retain every owner group but discard entry 0 as the ALX placeholder slot; entries 1 and later are published. The encounter `[Filter]` value is the ENP owner key, not wildcard selection metadata.

Bracketed columns are normally read-only derived annotations. European `[Entry GB Name]`, `[GB Descr Str]`, and `[Ship GB Descr Str]` fields are exceptions because ALX imports them as actual localized message text; those values are semantic and editable. Imported derived annotations live in `AlxDerivedContext`, and `AlxDerivedViewBuilder` creates fresh read-only views from the current dataset. Derived mismatches produce warnings without blocking publication.

The exact Japanese, US, and European ALX 5.0.0 header profiles are validated. Platform is not represented because the CSV schema does not distinguish Dreamcast and GameCube. Locale-neutral tables require a locale hint when imported alone; a dataset import can infer their locale from another requested locale-specific table.

SpiceTrade does not own display models, domain-level joins, SALSA script authoring, SIMMER AI execution, binary game formats, or source-range provenance. CSV output remains intentionally deferred; any future publication operation must be atomic.

ALX's GPLv3 Ruby source is a behavioral reference and compatibility oracle, not copied implementation. Public tests construct synthetic CSV inputs; compatibility against private reference exports is maintained in a separate private test repository and never requires corpus material in this working tree.
