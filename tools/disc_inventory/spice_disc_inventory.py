#!/usr/bin/env python3
"""Inventory Skies of Arcadia disc dumps for unsupported SPICE/ALX formats."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from itertools import combinations
from pathlib import Path
from typing import Iterable


AKLZ_MAGIC = b"AKLZ\x7e\x3f\x51\x64\x3d\xcc\xcc\xcd"
AKLZ_HEADER_SIZE = 16
AKLZ_WINDOW_SIZE = 0x1000
AKLZ_WINDOW_MASK = AKLZ_WINDOW_SIZE - 1
AKLZ_WINDOW_START = 0xFEE
AKLZ_MIN_MATCH_LENGTH = 3
RAW_PREVIEW_BYTES = 64
SIGNATURE_PREVIEW_BYTES = 512
SCT_STRING_MIN_LEN = 4

COVERED_EXTENSIONS = {
    ".mld",
    ".sct",
    ".dat",
    ".lmt",
    ".enp",
    ".evp",
    ".std",
    ".sot",
    ".tec",
    ".dol",
    ".bnr",
    ".hdr",
}

UNSUPPORTED_EXTENSIONS = {
    ".mlk",
    ".dsp",
    ".info",
    ".samp",
    ".sst",
    ".sml",
    ".ect",
    ".mll",
    ".gvr",
    ".bin",
    ".tpl",
    ".eu",
    ".toc",
    ".ldr",
    ".txt",
}

DISPLAY_EXTENSION = {
    ".eu": ".EU",
}

EXTERNAL_HANDLER_LEADS = {
    ".dsp": ["vgmstream/vgmstream"],
    ".gvr": ["nickworonekin/puyotools", "X-Hax/sa_tools"],
    ".tpl": ["soopercool101/BrawlCrate"],
    ".info": ["vgmstream/vgmstream (candidate companion-file/TXTH research)"],
    ".samp": ["vgmstream/vgmstream (candidate companion-file/TXTH research)"],
    ".mlk": ["No direct public handler found; compare X-Hax/sa_tools and local MLD/Ninja conventions"],
    ".mll": ["No direct public handler found; compare X-Hax/sa_tools and local MLD/Ninja conventions"],
    ".sst": ["No direct public handler found; local reverse engineering first"],
    ".sml": ["No direct public handler found; local reverse engineering first"],
    ".ect": ["No direct public handler found; local reverse engineering first"],
    ".eu": ["No direct public handler found; local AFNT font research first"],
    ".toc": ["Nintendo/GameCube system metadata references"],
    ".ldr": ["Nintendo/GameCube apploader references"],
}

RESOURCE_NAME_RE = re.compile(
    rb"(?i)[a-z0-9_./\\-]{3,64}\.(?:mlk|sst|sml|ect|mll|gvr|bin|tpl|dsp|info|samp|eu|toc|ldr|txt)"
)
PRINTABLE_STRING_RE = re.compile(rb"[ -~]{4,96}")


@dataclass
class FileRecord:
    path: Path
    relative_path: str
    extension: str
    size: int
    first_bytes: bytes
    decompressed_bytes: bytes | None
    decompressed_first_bytes: bytes
    aklz: dict[str, object]
    magic_guess: str
    decompressed_magic_guess: str | None


@dataclass
class ExtensionStats:
    extension: str
    count: int = 0
    total_size: int = 0
    min_size: int | None = None
    max_size: int | None = None
    aklz_count: int = 0
    raw_count: int = 0
    aklz_decompressed_sizes: list[int] = field(default_factory=list)
    directories: Counter[str] = field(default_factory=Counter)
    magic_guesses: Counter[str] = field(default_factory=Counter)
    decompressed_magic_guesses: Counter[str] = field(default_factory=Counter)
    samples: list[dict[str, object]] = field(default_factory=list)

    def add(self, record: FileRecord, max_samples: int) -> None:
        self.count += 1
        self.total_size += record.size
        self.min_size = record.size if self.min_size is None else min(self.min_size, record.size)
        self.max_size = record.size if self.max_size is None else max(self.max_size, record.size)
        self.directories[relative_parent(record.relative_path)] += 1
        self.magic_guesses[record.magic_guess] += 1
        if record.decompressed_magic_guess is not None:
            self.decompressed_magic_guesses[record.decompressed_magic_guess] += 1
        if record.aklz["is_aklz"]:
            self.aklz_count += 1
            size = record.aklz.get("decompressed_size")
            if isinstance(size, int):
                self.aklz_decompressed_sizes.append(size)
        else:
            self.raw_count += 1
        if len(self.samples) < max_samples:
            self.samples.append(sample_record(record))

    def to_json(self) -> dict[str, object]:
        decompressed = self.aklz_decompressed_sizes
        return {
            "extension": display_extension(self.extension),
            "normalized_extension": self.extension,
            "classification": classify_extension(self.extension),
            "count": self.count,
            "size_bytes": {
                "total": self.total_size,
                "min": self.min_size or 0,
                "max": self.max_size or 0,
            },
            "aklz": {
                "count": self.aklz_count,
                "raw_count": self.raw_count,
                "decompressed_size_bytes": {
                    "min": min(decompressed) if decompressed else None,
                    "max": max(decompressed) if decompressed else None,
                    "total": sum(decompressed) if decompressed else 0,
                },
            },
            "directories": counter_to_list(self.directories),
            "magic_guesses": counter_to_list(self.magic_guesses),
            "decompressed_magic_guesses": counter_to_list(self.decompressed_magic_guesses),
            "samples": self.samples,
        }


def display_extension(extension: str) -> str:
    return DISPLAY_EXTENSION.get(extension, extension)


def normalize_extension(path: Path) -> str:
    return path.suffix.lower() if path.suffix else "<none>"


def classify_extension(extension: str) -> str:
    if extension in COVERED_EXTENSIONS:
        return "covered_by_spice_or_alx"
    if extension in UNSUPPORTED_EXTENSIONS:
        return "unsupported_candidate"
    return "unknown_or_unclassified"


def counter_to_list(counter: Counter[str]) -> list[dict[str, object]]:
    return [{"name": key, "count": value} for key, value in sorted(counter.items(), key=lambda kv: (-kv[1], kv[0]))]


def relative_parent(relative_path: str) -> str:
    parent = Path(relative_path).parent
    return "(root)" if str(parent) == "." else str(parent).replace("/", "\\")


def read_prefix(path: Path, limit: int = SIGNATURE_PREVIEW_BYTES) -> bytes:
    with path.open("rb") as handle:
        return handle.read(limit)


def detect_aklz(prefix: bytes) -> dict[str, object]:
    if not prefix.startswith(AKLZ_MAGIC):
        return {"is_aklz": False}
    if len(prefix) < AKLZ_HEADER_SIZE:
        return {"is_aklz": True, "header_complete": False, "decompressed_size": None}
    return {
        "is_aklz": True,
        "header_complete": True,
        "decompressed_size": int.from_bytes(prefix[12:AKLZ_HEADER_SIZE], "big"),
    }


class AklzError(ValueError):
    """Raised when an AKLZ payload cannot be decompressed safely."""


def decompress_aklz(
    data: bytes,
    output_limit: int | None = None,
    max_output_size: int | None = None,
) -> bytes:
    if not data.startswith(AKLZ_MAGIC):
        raise AklzError("missing AKLZ magic")
    if len(data) < AKLZ_HEADER_SIZE:
        raise AklzError("truncated AKLZ header")

    expected_size = int.from_bytes(data[12:AKLZ_HEADER_SIZE], "big")
    if max_output_size is not None and expected_size > max_output_size:
        raise AklzError(f"decompressed size {expected_size} exceeds limit {max_output_size}")
    target_size = expected_size if output_limit is None else min(expected_size, output_limit)

    input_pos = AKLZ_HEADER_SIZE
    output = bytearray()
    window = bytearray(AKLZ_WINDOW_SIZE)
    window_pos = 0

    while len(output) < target_size:
        if input_pos >= len(data):
            raise AklzError("truncated AKLZ flag stream")
        flags = data[input_pos]
        input_pos += 1

        for bit in range(8):
            if len(output) >= target_size:
                break
            if flags & (1 << bit):
                if input_pos >= len(data):
                    raise AklzError("truncated AKLZ literal")
                value = data[input_pos]
                input_pos += 1
                output.append(value)
                window[window_pos] = value
                window_pos = (window_pos + 1) & AKLZ_WINDOW_MASK
                continue

            if input_pos + 1 >= len(data):
                raise AklzError("truncated AKLZ back-reference")
            b1 = data[input_pos]
            b2 = data[input_pos + 1]
            input_pos += 2

            offset = ((b2 >> 4) << 8) | b1
            length = (b2 & 0x0F) + AKLZ_MIN_MATCH_LENGTH
            offset = (AKLZ_WINDOW_SIZE + offset - AKLZ_WINDOW_START) & AKLZ_WINDOW_MASK

            for index in range(length):
                if len(output) >= target_size:
                    break
                value = window[(offset + index) & AKLZ_WINDOW_MASK]
                output.append(value)
                window[window_pos] = value
                window_pos = (window_pos + 1) & AKLZ_WINDOW_MASK

    return bytes(output)


def read_decompressed_payload(path: Path, extension: str, aklz: dict[str, object]) -> tuple[bytes | None, bytes, str | None]:
    if not aklz["is_aklz"]:
        return None, b"", None

    needs_full_payload = extension == ".sct"
    try:
        decompressed = decompress_aklz(
            path.read_bytes(),
            output_limit=None if needs_full_payload else SIGNATURE_PREVIEW_BYTES,
        )
    except AklzError as exc:
        aklz["decompression"] = {
            "attempted": True,
            "ok": False,
            "error": str(exc),
        }
        return None, b"", None

    preview = decompressed[:SIGNATURE_PREVIEW_BYTES]
    aklz["decompression"] = {
        "attempted": True,
        "ok": True,
        "preview_only": not needs_full_payload,
        "output_size": len(decompressed) if needs_full_payload else None,
    }
    return decompressed if needs_full_payload else None, preview, guess_magic(preview, extension)


def ascii_preview(data: bytes) -> str:
    return "".join(chr(byte) if 32 <= byte <= 126 else "." for byte in data)


def hex_preview(data: bytes) -> str:
    return " ".join(f"{byte:02X}" for byte in data)


def payload_preview(record: FileRecord) -> bytes:
    return record.decompressed_first_bytes if record.decompressed_first_bytes else record.first_bytes


def payload_preview_source(record: FileRecord) -> str:
    return "decompressed" if record.decompressed_first_bytes else "raw"


def header_fingerprint(data: bytes, size: int = 16) -> str:
    return hex_preview(data[:size])


def first_be_words(data: bytes) -> dict[str, list[str]]:
    u16 = [
        f"0x{int.from_bytes(data[offset:offset + 2], 'big'):04X}"
        for offset in range(0, min(len(data) - 1, 32), 2)
    ]
    u32 = [
        f"0x{int.from_bytes(data[offset:offset + 4], 'big'):08X}"
        for offset in range(0, min(len(data) - 3, 32), 4)
    ]
    return {"u16": u16, "u32": u32}


def extract_printable_strings(data: bytes, max_strings: int = 12) -> list[str]:
    strings: list[str] = []
    seen: set[str] = set()
    for match in PRINTABLE_STRING_RE.finditer(data):
        value = match.group(0).decode("ascii", errors="ignore").strip()
        if not value or value.lower() in seen:
            continue
        seen.add(value.lower())
        strings.append(value)
        if len(strings) >= max_strings:
            break
    return strings


def guess_magic(prefix: bytes, extension: str) -> str:
    if prefix.startswith(AKLZ_MAGIC):
        return "AKLZ"
    if prefix.startswith(b"AFNT"):
        return "AFNT"
    if prefix.startswith(bytes.fromhex("0020AF30")):
        return "TPL"
    if len(prefix) >= 10 and re.match(rb"\d{4}/\d{2}/\d{2}", prefix[:10]):
        return "date_string_header"
    if len(prefix) >= 4 and all(32 <= b <= 126 for b in prefix[:4]):
        return prefix[:4].decode("ascii", errors="replace")
    if extension == ".dsp" and len(prefix) >= 0x10:
        return "possible_gamecube_dsp_header"
    return "binary"


def sample_record(record: FileRecord) -> dict[str, object]:
    return {
        "path": record.relative_path,
        "size": record.size,
        "first_bytes_hex": hex_preview(record.first_bytes[:16]),
        "first_bytes_ascii": ascii_preview(record.first_bytes[:16]),
        "magic_guess": record.magic_guess,
        "decompressed_first_bytes_hex": hex_preview(record.decompressed_first_bytes[:16]),
        "decompressed_first_bytes_ascii": ascii_preview(record.decompressed_first_bytes[:16]),
        "decompressed_magic_guess": record.decompressed_magic_guess,
        "aklz": record.aklz,
    }


def iter_files(root: Path) -> Iterable[Path]:
    for path in sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: stable_rel(root, p).lower()):
        yield path


def stable_rel(root: Path, path: Path) -> str:
    return str(path.relative_to(root)).replace("/", "\\")


def scan_disc(root: Path, max_samples_per_extension: int, unsupported_only: bool = False) -> dict[str, object]:
    if not root.exists() or not root.is_dir():
        raise ValueError(f"disc root is not a directory: {root}")

    stats: dict[str, ExtensionStats] = {}
    records: list[FileRecord] = []
    unsupported_records: list[FileRecord] = []
    unsupported_names: dict[str, list[str]] = defaultdict(list)
    unsupported_stems: dict[str, list[str]] = defaultdict(list)

    for path in iter_files(root):
        relative = stable_rel(root, path)
        extension = normalize_extension(path)
        prefix = read_prefix(path)
        aklz = detect_aklz(prefix)
        magic = guess_magic(prefix, extension)
        decompressed_bytes, decompressed_prefix, decompressed_magic = read_decompressed_payload(path, extension, aklz)
        record = FileRecord(
            path=path,
            relative_path=relative,
            extension=extension,
            size=path.stat().st_size,
            first_bytes=prefix,
            decompressed_bytes=decompressed_bytes,
            decompressed_first_bytes=decompressed_prefix,
            aklz=aklz,
            magic_guess=magic,
            decompressed_magic_guess=decompressed_magic,
        )
        records.append(record)
        if extension not in stats:
            stats[extension] = ExtensionStats(extension=extension)
        stats[extension].add(record, max_samples_per_extension)

        if classify_extension(extension) == "unsupported_candidate":
            unsupported_records.append(record)
            unsupported_names[Path(relative).name.lower()].append(relative)
            unsupported_stems[Path(relative).stem.lower()].append(relative)

    extension_summaries = [stats[ext].to_json() for ext in sorted(stats)]
    if unsupported_only:
        extension_summaries = [
            item for item in extension_summaries if item["classification"] == "unsupported_candidate"
        ]

    signatures = build_unsupported_signatures(unsupported_records, max_samples_per_extension)
    references = find_sct_resource_references(records, unsupported_names, unsupported_stems)
    analysis = build_unsupported_format_analysis(records, stats, references)
    priority = rank_unsupported(stats, references, analysis)

    return {
        "schema_version": 1,
        "disc_root": str(root),
        "options": {
            "max_samples_per_extension": max_samples_per_extension,
            "unsupported_only": unsupported_only,
            "raw_preview_bytes": RAW_PREVIEW_BYTES,
            "signature_preview_bytes": SIGNATURE_PREVIEW_BYTES,
            "sct_reference_mode": "heuristic_in_memory_aklz_decompressed_bytes",
        },
        "totals": {
            "files": len(records),
            "extensions": len(stats),
            "unsupported_candidate_files": len(unsupported_records),
        },
        "extensions": extension_summaries,
        "unsupported_payload_signatures": signatures,
        "sct_resource_references": references,
        "unsupported_format_analysis": analysis,
        "unsupported_priority": priority,
    }


def build_unsupported_signatures(records: list[FileRecord], max_samples: int) -> list[dict[str, object]]:
    by_ext: dict[str, list[FileRecord]] = defaultdict(list)
    for record in records:
        by_ext[record.extension].append(record)

    out: list[dict[str, object]] = []
    for extension in sorted(by_ext):
        for record in sorted(by_ext[extension], key=lambda r: r.relative_path.lower())[:max_samples]:
            payload = payload_preview(record)
            out.append({
                "extension": display_extension(extension),
                "normalized_extension": extension,
                "path": record.relative_path,
                "size": record.size,
                "first_bytes_hex": hex_preview(record.first_bytes),
                "first_bytes_ascii": ascii_preview(record.first_bytes),
                "magic_guess": record.magic_guess,
                "decompressed_first_bytes_hex": hex_preview(record.decompressed_first_bytes),
                "decompressed_first_bytes_ascii": ascii_preview(record.decompressed_first_bytes),
                "decompressed_magic_guess": record.decompressed_magic_guess,
                "payload_preview_source": payload_preview_source(record),
                "payload_preview_bytes": len(payload),
                "payload_header16_hex": header_fingerprint(payload, 16),
                "payload_header32_hex": header_fingerprint(payload, 32),
                "payload_ascii": ascii_preview(payload),
                "payload_first_be_words": first_be_words(payload),
                "payload_printable_strings": extract_printable_strings(payload),
                "aklz": record.aklz,
                "external_handler_leads": EXTERNAL_HANDLER_LEADS.get(extension, []),
            })
    return out


def extract_resource_names(data: bytes) -> list[str]:
    found = []
    seen = set()
    for match in RESOURCE_NAME_RE.finditer(data):
        value = match.group(0).decode("ascii", errors="ignore").replace("/", "\\")
        if len(value) >= SCT_STRING_MIN_LEN and value.lower() not in seen:
            seen.add(value.lower())
            found.append(value)
    return found


def find_sct_resource_references(
    records: list[FileRecord],
    unsupported_names: dict[str, list[str]],
    unsupported_stems: dict[str, list[str]],
) -> list[dict[str, object]]:
    references: list[dict[str, object]] = []
    for record in records:
        if record.extension != ".sct":
            continue
        if record.aklz["is_aklz"] and record.decompressed_bytes is None:
            references.append({
                "source_sct": record.relative_path,
                "scan_status": "aklz_decompression_failed",
                "note": record.aklz.get("decompression", {}).get("error", "unknown AKLZ decompression error"),
                "matches": [],
            })
            continue

        data = record.decompressed_bytes if record.decompressed_bytes is not None else record.path.read_bytes()
        matches = []
        for resource in extract_resource_names(data):
            name = Path(resource).name.lower()
            stem = Path(resource).stem.lower()
            targets = unsupported_names.get(name) or unsupported_stems.get(stem) or []
            if not targets:
                continue
            matches.append({
                "resource": resource,
                "matched_paths": sorted(targets),
                "match_kind": "filename" if name in unsupported_names else "stem",
            })
        if matches:
            references.append({
                "source_sct": record.relative_path,
                "scan_status": "heuristic_aklz_decompressed_payload_scan"
                if record.decompressed_bytes is not None
                else "heuristic_raw_payload_scan",
                "matches": matches,
            })
    return references


def build_unsupported_format_analysis(
    records: list[FileRecord],
    stats: dict[str, ExtensionStats],
    references: list[dict[str, object]],
) -> dict[str, object]:
    unsupported_records = [record for record in records if classify_extension(record.extension) == "unsupported_candidate"]
    clusters = build_signature_clusters(unsupported_records)
    stem_pairs = build_stem_pair_analysis(unsupported_records)
    recommendations = build_parser_target_recommendations(stats, clusters, stem_pairs, references)
    return {
        "signature_preview_bytes": SIGNATURE_PREVIEW_BYTES,
        "signature_clusters": clusters,
        "stem_pair_analysis": stem_pairs,
        "parser_target_recommendations": recommendations,
    }


def build_signature_clusters(records: list[FileRecord]) -> list[dict[str, object]]:
    by_ext: dict[str, list[FileRecord]] = defaultdict(list)
    for record in records:
        by_ext[record.extension].append(record)

    out: list[dict[str, object]] = []
    for extension in sorted(by_ext):
        ext_records = sorted(by_ext[extension], key=lambda r: r.relative_path.lower())
        by_header: dict[str, list[FileRecord]] = defaultdict(list)
        string_records = 0
        string_samples: list[str] = []
        seen_strings: set[str] = set()

        for record in ext_records:
            payload = payload_preview(record)
            by_header[header_fingerprint(payload, 16)].append(record)
            strings = extract_printable_strings(payload, max_strings=4)
            if strings:
                string_records += 1
            for value in strings:
                if len(string_samples) >= 16:
                    break
                if value.lower() in seen_strings:
                    continue
                seen_strings.add(value.lower())
                string_samples.append(value)

        top_clusters = []
        for header, cluster_records in sorted(by_header.items(), key=lambda item: (-len(item[1]), item[0]))[:12]:
            sample = cluster_records[0]
            payload = payload_preview(sample)
            top_clusters.append({
                "header16_hex": header,
                "count": len(cluster_records),
                "ratio": round(len(cluster_records) / len(ext_records), 4) if ext_records else 0.0,
                "payload_preview_source": payload_preview_source(sample),
                "magic_guess": sample.decompressed_magic_guess or sample.magic_guess,
                "ascii": ascii_preview(payload[:32]),
                "first_be_words": first_be_words(payload),
                "sample_paths": [record.relative_path for record in cluster_records[:5]],
            })

        out.append({
            "extension": display_extension(extension),
            "normalized_extension": extension,
            "count": len(ext_records),
            "cluster_count": len(by_header),
            "top_cluster_ratio": top_clusters[0]["ratio"] if top_clusters else 0.0,
            "records_with_printable_strings": string_records,
            "printable_string_samples": string_samples,
            "top_clusters": top_clusters,
        })
    return out


def build_stem_pair_analysis(records: list[FileRecord]) -> dict[str, object]:
    by_stem: dict[tuple[str, str], list[FileRecord]] = defaultdict(list)
    for record in records:
        by_stem[(relative_parent(record.relative_path), Path(record.relative_path).stem.lower())].append(record)

    paired_groups: list[dict[str, object]] = []
    extension_pair_counts: Counter[str] = Counter()
    extension_group_counts: Counter[str] = Counter()

    for (directory, stem), group_records in sorted(by_stem.items(), key=lambda item: (item[0][0], item[0][1])):
        by_ext: dict[str, list[str]] = defaultdict(list)
        for record in sorted(group_records, key=lambda r: r.relative_path.lower()):
            by_ext[record.extension].append(record.relative_path)
        if len(by_ext) < 2:
            continue

        extensions = sorted(by_ext)
        for left, right in combinations(extensions, 2):
            extension_pair_counts[f"{display_extension(left)} + {display_extension(right)}"] += 1
        for extension in extensions:
            extension_group_counts[extension] += 1

        paired_groups.append({
            "directory": directory,
            "stem": stem,
            "extensions": [display_extension(extension) for extension in extensions],
            "normalized_extensions": extensions,
            "paths_by_extension": {
                display_extension(extension): paths
                for extension, paths in sorted(by_ext.items())
            },
        })

    return {
        "paired_group_count": len(paired_groups),
        "extension_pair_counts": counter_to_list(extension_pair_counts),
        "extension_group_counts": counter_to_list(extension_group_counts),
        "paired_groups": paired_groups[:200],
    }


def cluster_lookup(clusters: list[dict[str, object]]) -> dict[str, dict[str, object]]:
    return {str(item["normalized_extension"]): item for item in clusters}


def pair_count(stem_pairs: dict[str, object], pair_name: str) -> int:
    for item in stem_pairs.get("extension_pair_counts", []):
        if item["name"] == pair_name:
            return int(item["count"])
    return 0


def build_parser_target_recommendations(
    stats: dict[str, ExtensionStats],
    clusters: list[dict[str, object]],
    stem_pairs: dict[str, object],
    references: list[dict[str, object]],
) -> list[dict[str, object]]:
    cluster_by_ext = cluster_lookup(clusters)
    ref_counts = reference_counts_by_extension(references)
    recommendations: list[dict[str, object]] = []

    if ".sst" in stats and ".sml" in stats:
        sst = stats[".sst"]
        sml = stats[".sml"]
        pair_groups = pair_count(stem_pairs, ".sml + .sst")
        sst_cluster = cluster_by_ext.get(".sst", {})
        sml_cluster = cluster_by_ext.get(".sml", {})
        stable_bonus = int(float(sst_cluster.get("top_cluster_ratio", 0.0)) * 100)
        stable_bonus += int(float(sml_cluster.get("top_cluster_ratio", 0.0)) * 100)
        score = pair_groups * 30 + (sst.count + sml.count) * 5 + stable_bonus
        recommendations.append({
            "target": ".sst + .sml",
            "score": score,
            "kind": "paired_battle_data",
            "why": "High-value paired battle data. Pairing gives a natural parser boundary and cross-file validation oracle.",
            "evidence": {
                "sst_count": sst.count,
                "sml_count": sml.count,
                "paired_stem_groups": pair_groups,
                "sst_header_clusters": sst_cluster.get("cluster_count"),
                "sml_header_clusters": sml_cluster.get("cluster_count"),
                "sst_top_cluster_ratio": sst_cluster.get("top_cluster_ratio"),
                "sml_top_cluster_ratio": sml_cluster.get("top_cluster_ratio"),
                "sct_reference_count": ref_counts[".sst"] + ref_counts[".sml"],
            },
        })

    if ".mlk" in stats:
        mlk = stats[".mlk"]
        mlk_cluster = cluster_by_ext.get(".mlk", {})
        cluster_count = int(mlk_cluster.get("cluster_count", 0) or 0)
        top_ratio = float(mlk_cluster.get("top_cluster_ratio", 0.0) or 0.0)
        score = mlk.count * 6 + int(top_ratio * 500) - min(cluster_count, 100)
        recommendations.append({
            "target": ".mlk",
            "score": score,
            "kind": "high_volume_asset_family",
            "why": "Largest unsupported family. Best first target if header clustering shows a repeatable container or table shape.",
            "evidence": {
                "count": mlk.count,
                "aklz_count": mlk.aklz_count,
                "header_clusters": cluster_count,
                "top_cluster_ratio": top_ratio,
                "sct_reference_count": ref_counts[".mlk"],
            },
        })

    if ".gvr" in stats:
        gvr = stats[".gvr"]
        gvr_cluster = cluster_by_ext.get(".gvr", {})
        score = 450 + gvr.count * 10 + int(float(gvr_cluster.get("top_cluster_ratio", 0.0) or 0.0) * 100)
        recommendations.append({
            "target": ".gvr",
            "score": score,
            "kind": "quick_win_existing_parser",
            "why": "Standalone GVR has existing low-level parser support and decompressed previews show the expected GCIX/GVRT texture wrapper.",
            "evidence": {
                "count": gvr.count,
                "aklz_count": gvr.aklz_count,
                "header_clusters": gvr_cluster.get("cluster_count"),
                "top_cluster_ratio": gvr_cluster.get("top_cluster_ratio"),
                "sct_reference_count": ref_counts[".gvr"],
            },
        })

    for extension in [".ect", ".mll", ".bin"]:
        if extension not in stats:
            continue
        ext_stats = stats[extension]
        ext_cluster = cluster_by_ext.get(extension, {})
        score = ext_stats.count * 5 + int(float(ext_cluster.get("top_cluster_ratio", 0.0) or 0.0) * 150)
        recommendations.append({
            "target": display_extension(extension),
            "score": score,
            "kind": "secondary_unknown_family",
            "why": "Useful follow-up once the larger or easier targets establish shared parser conventions.",
            "evidence": {
                "count": ext_stats.count,
                "aklz_count": ext_stats.aklz_count,
                "header_clusters": ext_cluster.get("cluster_count"),
                "top_cluster_ratio": ext_cluster.get("top_cluster_ratio"),
                "sct_reference_count": ref_counts[extension],
            },
        })

    return sorted(recommendations, key=lambda item: (-int(item["score"]), str(item["target"])))


def reference_counts_by_extension(references: list[dict[str, object]]) -> Counter[str]:
    ref_counts: Counter[str] = Counter()
    for source in references:
        for match in source.get("matches", []):
            for target in match.get("matched_paths", []):
                ref_counts[normalize_extension(Path(target))] += 1
    return ref_counts


def rank_unsupported(
    stats: dict[str, ExtensionStats],
    references: list[dict[str, object]],
    analysis: dict[str, object],
) -> list[dict[str, object]]:
    ref_counts = reference_counts_by_extension(references)
    clusters = cluster_lookup(list(analysis.get("signature_clusters", [])))
    pair_counts_by_ext: Counter[str] = Counter()
    stem_pairs = analysis.get("stem_pair_analysis", {})
    if isinstance(stem_pairs, dict):
        for item in stem_pairs.get("extension_group_counts", []):
            pair_counts_by_ext[str(item["name"])] = int(item["count"])

    ranked = []
    for extension, ext_stats in stats.items():
        if classify_extension(extension) != "unsupported_candidate":
            continue
        sct_refs = ref_counts[extension]
        cluster = clusters.get(extension, {})
        cluster_count = int(cluster.get("cluster_count", 0) or 0)
        top_cluster_ratio = float(cluster.get("top_cluster_ratio", 0.0) or 0.0)
        pair_groups = pair_counts_by_ext[extension]
        score = (
            ext_stats.count * 10
            + sct_refs * 50
            + ext_stats.aklz_count * 2
            + pair_groups * 20
            + int(top_cluster_ratio * 25)
        )
        ranked.append({
            "extension": display_extension(extension),
            "normalized_extension": extension,
            "score": score,
            "count": ext_stats.count,
            "aklz_count": ext_stats.aklz_count,
            "sct_reference_count": sct_refs,
            "header_cluster_count": cluster_count,
            "top_header_cluster_ratio": top_cluster_ratio,
            "paired_stem_group_count": pair_groups,
            "top_directories": counter_to_list(ext_stats.directories)[:5],
            "external_handler_leads": EXTERNAL_HANDLER_LEADS.get(extension, []),
        })
    return sorted(ranked, key=lambda item: (-int(item["score"]), str(item["normalized_extension"])))


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def write_priority_report(path: Path, result: dict[str, object]) -> None:
    lines = [
        "# Unsupported Format Priority Report",
        "",
        f"Disc root: `{result['disc_root']}`",
        "",
        "This report is heuristic. SCT references are byte/string matches over raw payloads or in-memory AKLZ-decompressed payloads, not parsed SCT semantics.",
        "",
        "| Rank | Extension | Score | Count | AKLZ | SCT refs | Header clusters | Paired stems | Top directories | External leads |",
        "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
    ]
    for index, item in enumerate(result["unsupported_priority"], start=1):
        dirs = ", ".join(f"{d['name']} ({d['count']})" for d in item["top_directories"])
        leads = ", ".join(item["external_handler_leads"])
        lines.append(
            f"| {index} | `{item['extension']}` | {item['score']} | {item['count']} | "
            f"{item['aklz_count']} | {item['sct_reference_count']} | "
            f"{item['header_cluster_count']} | {item['paired_stem_group_count']} | {dirs} | {leads} |"
        )

    analysis = result.get("unsupported_format_analysis", {})
    recommendations = []
    if isinstance(analysis, dict):
        recommendations = list(analysis.get("parser_target_recommendations", []))

    if recommendations:
        lines.extend([
            "",
            "## Parser Target Recommendation",
            "",
            "| Rank | Target | Score | Kind | Evidence |",
            "| ---: | --- | ---: | --- | --- |",
        ])
        for index, item in enumerate(recommendations, start=1):
            evidence = item.get("evidence", {})
            evidence_text = ", ".join(f"{key}={value}" for key, value in evidence.items())
            lines.append(
                f"| {index} | `{item['target']}` | {item['score']} | {item['kind']} | {evidence_text} |"
            )

    clusters = []
    if isinstance(analysis, dict):
        clusters = list(analysis.get("signature_clusters", []))
    if clusters:
        lines.extend([
            "",
            "## Signature Evidence",
            "",
            "| Extension | Header clusters | Top cluster ratio | Printable preview records | Top header |",
            "| --- | ---: | ---: | ---: | --- |",
        ])
        for item in clusters:
            top_clusters = item.get("top_clusters", [])
            top_header = top_clusters[0]["header16_hex"] if top_clusters else ""
            lines.append(
                f"| `{item['extension']}` | {item['cluster_count']} | {item['top_cluster_ratio']} | "
                f"{item['records_with_printable_strings']} | `{top_header}` |"
            )

    lines.extend([
        "",
        "## Notes",
        "",
        "- AKLZ detection uses the header magic and records the big-endian decompressed-size field.",
        "- AKLZ payloads are decompressed in memory for previews and SCT reference scans when possible.",
        "- This tool does not write raw payloads or decompressed game data.",
        "- Formats without public handler leads should be treated as local reverse-engineering targets.",
        "",
    ])
    path.write_text("\n".join(lines), encoding="utf-8")


def write_outputs(out_dir: Path, result: dict[str, object]) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    write_json(out_dir / "disc_inventory.json", {
        key: result[key] for key in ("schema_version", "disc_root", "options", "totals", "extensions")
    })
    write_json(out_dir / "unsupported_payload_signatures.json", result["unsupported_payload_signatures"])
    write_json(out_dir / "sct_resource_references.json", result["sct_resource_references"])
    write_json(out_dir / "unsupported_format_analysis.json", result["unsupported_format_analysis"])
    write_priority_report(out_dir / "unsupported_priority_report.md", result)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Inventory a Skies of Arcadia disc dump for SPICE format triage.")
    parser.add_argument("--disc-root", required=True, type=Path, help="Extracted disc root to scan.")
    parser.add_argument("--out", required=True, type=Path, help="Output directory for generated reports.")
    parser.add_argument("--max-samples-per-extension", type=int, default=8, help="Representative sample cap per extension.")
    parser.add_argument("--unsupported-only", action="store_true", help="Only include unsupported candidates in disc_inventory.json.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.max_samples_per_extension < 1:
        raise SystemExit("--max-samples-per-extension must be at least 1")
    result = scan_disc(
        args.disc_root.resolve(),
        max_samples_per_extension=args.max_samples_per_extension,
        unsupported_only=args.unsupported_only,
    )
    write_outputs(args.out.resolve(), result)
    print(f"Wrote disc inventory reports to {args.out.resolve()}")
    print(f"Files scanned: {result['totals']['files']}")
    print(f"Unsupported candidate files: {result['totals']['unsupported_candidate_files']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
