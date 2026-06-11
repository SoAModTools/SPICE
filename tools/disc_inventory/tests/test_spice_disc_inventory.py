import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "spice_disc_inventory.py"
SPEC = importlib.util.spec_from_file_location("spice_disc_inventory", MODULE_PATH)
disc_inventory = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = disc_inventory
SPEC.loader.exec_module(disc_inventory)


AKLZ_MAGIC = b"AKLZ\x7e\x3f\x51\x64\x3d\xcc\xcc\xcd"


def make_aklz_literal(data: bytes) -> bytes:
    compressed = bytearray(AKLZ_MAGIC + len(data).to_bytes(4, "big"))
    offset = 0
    while offset < len(data):
        chunk = data[offset:offset + 8]
        compressed.append((1 << len(chunk)) - 1)
        compressed.extend(chunk)
        offset += len(chunk)
    return bytes(compressed)


def write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


class DiscInventoryTests(unittest.TestCase):
    def test_decompress_aklz_literal_payload(self):
        self.assertEqual(
            disc_inventory.decompress_aklz(make_aklz_literal(b"s001.sst\x00payload")),
            b"s001.sst\x00payload",
        )

    def test_inventory_groups_extensions_and_detects_aklz(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_bytes(root / "battle" / "s001.sst", make_aklz_literal(b"payload"))
            write_bytes(root / "battle" / "s002.sst", b"raw")
            write_bytes(root / "field" / "a017a00.mld", b"mld")

            result = disc_inventory.scan_disc(root, max_samples_per_extension=1)
            by_ext = {item["normalized_extension"]: item for item in result["extensions"]}

            self.assertEqual(by_ext[".sst"]["count"], 2)
            self.assertEqual(by_ext[".sst"]["aklz"]["count"], 1)
            self.assertEqual(by_ext[".sst"]["aklz"]["decompressed_size_bytes"]["max"], len(b"payload"))
            self.assertEqual(len(by_ext[".sst"]["samples"]), 1)
            self.assertEqual(by_ext[".sst"]["samples"][0]["decompressed_first_bytes_ascii"], "payload")
            self.assertEqual(by_ext[".mld"]["classification"], "covered_by_spice_or_alx")

    def test_unsupported_only_filters_inventory_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_bytes(root / "field" / "me001.sct", b"script")
            write_bytes(root / "field" / "a017a.ect", b"event")

            result = disc_inventory.scan_disc(root, max_samples_per_extension=8, unsupported_only=True)
            extensions = {item["normalized_extension"] for item in result["extensions"]}

            self.assertEqual(extensions, {".ect"})

    def test_sct_like_resource_references_match_unsupported_targets(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_bytes(root / "field" / "me001.sct", b"load\x00s001.sst\x00ending\\sr.mll\x00ignored.mld\x00")
            write_bytes(root / "battle" / "s001.sst", b"battle")
            write_bytes(root / "ending" / "sr.mll", b"model-list")
            write_bytes(root / "field" / "ignored.mld", b"covered")

            result = disc_inventory.scan_disc(root, max_samples_per_extension=8)
            references = result["sct_resource_references"]

            self.assertEqual(len(references), 1)
            matched = {
                target
                for match in references[0]["matches"]
                for target in match["matched_paths"]
            }
            self.assertIn("battle\\s001.sst", matched)
            self.assertIn("ending\\sr.mll", matched)
            self.assertNotIn("field\\ignored.mld", matched)

    def test_aklz_wrapped_sct_resource_references_match_unsupported_targets(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_bytes(root / "field" / "me001.sct", make_aklz_literal(b"load\x00s001.sst\x00"))
            write_bytes(root / "battle" / "s001.sst", b"battle")

            result = disc_inventory.scan_disc(root, max_samples_per_extension=8)
            references = result["sct_resource_references"]

            self.assertEqual(len(references), 1)
            self.assertEqual(references[0]["scan_status"], "heuristic_aklz_decompressed_payload_scan")
            self.assertEqual(references[0]["matches"][0]["matched_paths"], ["battle\\s001.sst"])

    def test_unsupported_signatures_include_aklz_decompressed_preview(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_bytes(root / "texture.gvr", make_aklz_literal(b"GVRTpayload"))

            result = disc_inventory.scan_disc(root, max_samples_per_extension=8)
            signatures = result["unsupported_payload_signatures"]

            self.assertEqual(len(signatures), 1)
            self.assertEqual(signatures[0]["payload_preview_source"], "decompressed")
            self.assertEqual(signatures[0]["decompressed_first_bytes_ascii"], "GVRTpayload")
            self.assertEqual(signatures[0]["payload_header16_hex"], "47 56 52 54 70 61 79 6C 6F 61 64")
            self.assertIn("u16", signatures[0]["payload_first_be_words"])

    def test_format_analysis_reports_stem_pairs_and_recommendations(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write_bytes(root / "battle" / "s001.sst", make_aklz_literal(b"\x00\x01\x00\x02table"))
            write_bytes(root / "battle" / "s001.sml", make_aklz_literal(b"\x00\x01\x00\x02model"))
            write_bytes(root / "beff" / "e001.mlk", make_aklz_literal(b"\x00\x00\x00\x10link"))

            result = disc_inventory.scan_disc(root, max_samples_per_extension=8)
            analysis = result["unsupported_format_analysis"]

            pair_counts = {
                item["name"]: item["count"]
                for item in analysis["stem_pair_analysis"]["extension_pair_counts"]
            }
            self.assertEqual(pair_counts[".sml + .sst"], 1)

            targets = [item["target"] for item in analysis["parser_target_recommendations"]]
            self.assertIn(".sst + .sml", targets)
            self.assertIn(".mlk", targets)

    def test_write_outputs_creates_expected_report_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "disc"
            out = Path(tmp) / "out"
            write_bytes(root / "FontData.EU", b"AFNT" + b"\x00" * 16)

            result = disc_inventory.scan_disc(root, max_samples_per_extension=8)
            disc_inventory.write_outputs(out, result)

            self.assertTrue((out / "disc_inventory.json").exists())
            self.assertTrue((out / "unsupported_payload_signatures.json").exists())
            self.assertTrue((out / "sct_resource_references.json").exists())
            self.assertTrue((out / "unsupported_format_analysis.json").exists())
            self.assertTrue((out / "unsupported_priority_report.md").exists())

            inventory = json.loads((out / "disc_inventory.json").read_text(encoding="utf-8"))
            self.assertEqual(inventory["totals"]["files"], 1)


if __name__ == "__main__":
    unittest.main()
