# SoAL_ect_file_reader Summary

Source reviewed: [jahorta/SoAL_ect_file_reader](https://github.com/jahorta/SoAL_ect_file_reader), default branch `master`, accessed 2026-06-13.

## Repository Shape

The repository is a small Python extraction utility, not a reusable package. It contains five source files:

- [`ByteArrayUtils.py`](https://github.com/jahorta/SoAL_ect_file_reader/blob/master/ByteArrayUtils.py): big-endian byte, halfword, word, and string helpers over a byte array.
- [`ectFileUtils.py`](https://github.com/jahorta/SoAL_ect_file_reader/blob/master/ectFileUtils.py): the core encounter-table parsing logic.
- [`main.py`](https://github.com/jahorta/SoAL_ect_file_reader/blob/master/main.py): batch driver that reads `./ect_files/*.ect`, optionally decompresses AKLZ payloads, parses tables, and writes `encounter_tables.xls`.
- [`aklz.py`](https://github.com/jahorta/SoAL_ect_file_reader/blob/master/aklz.py): AKLZ compression/decompression helper.
- [`scriptCSVFileFormatter.py`](https://github.com/jahorta/SoAL_ect_file_reader/blob/master/scriptCSVFileFormatter.py): a separate Shift-JIS CSV reshaper for script-like CSV output. It is not used by `main.py`.

## ECT Structure Implied By The Parser

The parser treats normal ECT files as a flat sequence of fixed-size encounter tables. Each table is `0x84` bytes:

- `u16 stage` at table offset `0x00`, read big-endian and displayed as hex.
- `u16 overallEncounterRate` at table offset `0x02`.
- 32 encounter entries starting at table offset `0x04`.
- Each encounter entry is 4 bytes: `u16 encounterId`, then `u16 encounterRate`.
- `0x04 + (32 * 0x04) = 0x84`, matching the table size used by the code.

For ordinary files, `main.py` starts at offset 0 and repeatedly parses `EncTable` records until it reaches the decompressed byte length. The exported columns are encounter index, stage, overall encounter rate, encounter ID, and encounter chance.

The driver has a special path for filenames whose base name contains `099`. For these, it treats the file as an indexed container:

- Reads an index length from big-endian `u16` at file offset `0x04`.
- Assumes index records begin at offset `0x08`.
- Assumes each index record is 32 bytes.
- Reads a null-terminated title at the start of each record.
- Skips titles beginning with `dam`.
- Reads a big-endian `u32` data offset from record offset `0x14`.
- At each referenced offset, parses 8 consecutive `0x84`-byte encounter tables.

The `099` path then groups index names by splitting on `_`: the prefix becomes the worksheet key and the suffix becomes a region/index key. This naming logic is presentation-oriented and may not be part of the binary format itself.

## Parser Behavior Notes

All numeric reads are big-endian. The helper functions do manual byte arithmetic rather than using `struct.unpack`.

`main.py` checks `Aklz.is_compressed` before parsing. AKLZ-compressed files are decompressed first, so a native SpiceEct parser should probably expose parse-from-decompressed-bytes and leave compression detection/decompression as a wrapper step or reuse SPICE's existing `Compression` project.

The current parser does minimal validation. It assumes table boundaries are valid, the `099` index length is trustworthy, referenced offsets exist, and each referenced indexed payload has exactly 8 tables. It also prints offsets while parsing `099` files.

## Open Questions For SpiceEct

- Confirm whether all non-`099` ECT files are pure concatenated `0x84`-byte tables, or whether some regions contain headers/padding.
- Confirm whether `099` is a unique file type or a filename heuristic for a more general indexed ECT container layout.
- Identify the semantic meaning of the 8 tables referenced by each `099` index entry.
- Validate encounter IDs and rates against US files first, then EU/JP, following the repo's regional-schema guidance.
- Decide whether AKLZ handling should be an optional parse wrapper in `SpiceEct` or live entirely at the caller level.
