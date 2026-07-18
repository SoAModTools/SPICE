from __future__ import annotations

import importlib.util
import sys
import types
import unittest
from pathlib import Path


def _install_blender_stubs() -> None:
    bpy = types.ModuleType("bpy")
    bpy_props = types.ModuleType("bpy.props")
    bpy_types = types.ModuleType("bpy.types")
    bpy_extras = types.ModuleType("bpy_extras")
    bpy_extras_io = types.ModuleType("bpy_extras.io_utils")
    mathutils = types.ModuleType("mathutils")

    class _RnaType:
        pass

    for name in (
        "Collection",
        "Context",
        "Image",
        "LayerObjects",
        "Material",
        "Mesh",
        "Object",
        "Operator",
        "Panel",
        "PropertyGroup",
        "Scene",
        "TOPBAR_MT_file_import",
        "UIList",
    ):
        setattr(bpy_types, name, type(name, (_RnaType,), {}))

    def _property_stub(**_kwargs: object) -> None:
        return None

    for name in (
        "BoolProperty",
        "CollectionProperty",
        "EnumProperty",
        "FloatProperty",
        "FloatVectorProperty",
        "IntProperty",
        "StringProperty",
    ):
        setattr(bpy_props, name, _property_stub)

    class ImportHelper:
        pass

    class Quaternion:
        def __init__(self, *_args: object) -> None:
            pass

    bpy_extras_io.ImportHelper = ImportHelper
    mathutils.Quaternion = Quaternion
    bpy.types = bpy_types
    bpy.props = bpy_props
    sys.modules.update({
        "bpy": bpy,
        "bpy.props": bpy_props,
        "bpy.types": bpy_types,
        "bpy_extras": bpy_extras,
        "bpy_extras.io_utils": bpy_extras_io,
        "mathutils": mathutils,
    })


def _load_importer() -> types.ModuleType:
    _install_blender_stubs()
    importer_path = (
        Path(__file__).resolve().parents[1]
        / "SpiceMLD"
        / "blender"
        / "spice_blender_ir_importer.py"
    )
    spec = importlib.util.spec_from_file_location("spice_blender_ir_importer_under_test", importer_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load importer from {importer_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


IMPORTER = _load_importer()


class TriangleMetadataDecoderTests(unittest.TestCase):
    def _decode(self, raw_word_2: int):
        return IMPORTER._decode_triangle_metadata((0, 0, raw_word_2))

    def test_known_runtime_decoder_examples(self) -> None:
        expected = {
            1000: (0x8000, 0, 0),
            1005: (0x8100, 1, 0),
            1065: (0x8106, 1, 6),
            2000: (0x8200, 2, 0),
            2065: (0x8306, 3, 6),
            10: (0x0001, 0, 1),
            20: (0x0002, 0, 2),
            70: (0x0007, 0, 7),
        }
        for raw_value, (decoded_u16, behavior_class, encounter_selector) in expected.items():
            with self.subTest(raw_value=raw_value):
                decoded = self._decode(raw_value)
                self.assertEqual(decoded.decoded_u16, decoded_u16)
                self.assertEqual(decoded.behavior_class, behavior_class)
                self.assertEqual(decoded.encounter_selector, encounter_selector)

    def test_stream_winding_does_not_change_attributed_layer_colors(self) -> None:
        normal = self._decode(1065)
        reversed_winding = self._decode(0x8000 | 1065)
        self.assertEqual(normal.selector_low15, reversed_winding.selector_low15)
        for mode in ("SKY_RIFT_FORCE", "ENCOUNTER_SELECTOR", "FORCE_RESOLVED_ENCOUNTER"):
            with self.subTest(mode=mode):
                normal_color, _ = IMPORTER._triangle_metadata_color(normal, mode, [])
                reversed_color, _ = IMPORTER._triangle_metadata_color(reversed_winding, mode, [])
                self.assertEqual(normal_color, reversed_color)

    def test_force_color_ignores_encounter_and_payload_layers(self) -> None:
        colors = {
            IMPORTER._triangle_metadata_color(
                self._decode(raw_value),
                "SKY_RIFT_FORCE",
                [],
            )[0]
            for raw_value in (1000, 1060, 1100, 1160)
        }
        self.assertEqual(len(colors), 1)

    def test_force_map_recognizes_only_runtime_force_dispatch_classes(self) -> None:
        expected = {
            1000: "Progression-gated rift force",
            1005: "Progression-gated rift force",
            2000: "Hard-boundary rift force",
            2005: "Hard-boundary rift force",
        }
        for raw_value, expected_label in expected.items():
            with self.subTest(raw_value=raw_value):
                _color, family = IMPORTER._triangle_metadata_color(
                    self._decode(raw_value),
                    "SKY_RIFT_FORCE",
                    [],
                )
                self.assertEqual(family.label, expected_label)
        _color, family = IMPORTER._triangle_metadata_color(
            self._decode(9020),
            "SKY_RIFT_FORCE",
            [],
        )
        self.assertEqual(family.label, "No attributed sky-rift force")

    def test_encounter_map_depends_only_on_tens_digit_selector(self) -> None:
        values = (1060, 1065, 2060, 2065)
        colors = {
            IMPORTER._triangle_metadata_color(
                self._decode(raw_value),
                "ENCOUNTER_SELECTOR",
                [],
            )[0]
            for raw_value in values
        }
        self.assertEqual(len(colors), 1)

    def test_combined_mode_keeps_force_as_an_independent_face_color_layer(self) -> None:
        decoded = self._decode(1065)
        force_color, _ = IMPORTER._triangle_metadata_color(decoded, "SKY_RIFT_FORCE", [])
        combined_force_color, _ = IMPORTER._triangle_metadata_color(
            decoded,
            "FORCE_RESOLVED_ENCOUNTER",
            [],
        )
        self.assertEqual(combined_force_color, force_color)

    def test_unattributed_map_excludes_known_force_and_encounter_layers(self) -> None:
        neutral_1000, _ = IMPORTER._triangle_metadata_color(
            self._decode(1000), "UNATTRIBUTED", []
        )
        neutral_1060, _ = IMPORTER._triangle_metadata_color(
            self._decode(1060), "UNATTRIBUTED", []
        )
        payload_1160, _ = IMPORTER._triangle_metadata_color(
            self._decode(1160), "UNATTRIBUTED", []
        )
        unknown_9020, _ = IMPORTER._triangle_metadata_color(
            self._decode(9020), "UNATTRIBUTED", []
        )
        self.assertEqual(neutral_1000, neutral_1060)
        self.assertNotEqual(neutral_1000, payload_1160)
        self.assertNotEqual(neutral_1000, unknown_9020)


class PositionAwareEncounterTests(unittest.TestCase):
    class _FakePixels:
        def __init__(self, image: "PositionAwareEncounterTests._FakeImage") -> None:
            self._image = image
            self.values = [0.0] * (image.size[0] * image.size[1] * 4)

        def foreach_set(self, values: list[float]) -> None:
            self._image.events.append("pixels")
            self.values = list(values)

    class _FakeColorSpace:
        def __init__(self, image: "PositionAwareEncounterTests._FakeImage") -> None:
            self._image = image
            self._name = "sRGB"

        @property
        def name(self) -> str:
            return self._name

        @name.setter
        def name(self, value: str) -> None:
            self._image.events.append("colorspace")
            self._name = value
            self._image.pixels.values = [0.0] * len(self._image.pixels.values)

    class _FakeImage(dict):
        def __init__(self, name: str, width: int, height: int) -> None:
            super().__init__()
            self.name = name
            self.size = [width, height]
            self.events: list[str] = []
            self.pixels = PositionAwareEncounterTests._FakePixels(self)
            self.colorspace_settings = PositionAwareEncounterTests._FakeColorSpace(self)

        def scale(self, width: int, height: int) -> None:
            self.events.append("scale")
            self.size = [width, height]
            self.pixels.values = [0.0] * (width * height * 4)

        def update(self) -> None:
            self.events.append("update")

        def pack(self) -> None:
            self.events.append("pack")

    class _FakeImages:
        def __init__(self) -> None:
            self.items: dict[str, PositionAwareEncounterTests._FakeImage] = {}

        def get(self, name: str) -> "PositionAwareEncounterTests._FakeImage | None":
            return self.items.get(name)

        def new(
            self,
            name: str,
            *,
            width: int,
            height: int,
            alpha: bool,
            float_buffer: bool,
        ) -> "PositionAwareEncounterTests._FakeImage":
            del alpha, float_buffer
            image = PositionAwareEncounterTests._FakeImage(name, width, height)
            self.items[name] = image
            return image

        def remove(self, image: "PositionAwareEncounterTests._FakeImage") -> None:
            self.items.pop(image.name, None)

    @staticmethod
    def _pack_lsb_first(raw_bytes: list[int]) -> list[int]:
        if len(raw_bytes) % 4 != 0:
            raise ValueError("test byte table must be divisible by four")
        return [
            raw_bytes[index]
            | (raw_bytes[index + 1] << 8)
            | (raw_bytes[index + 2] << 16)
            | (raw_bytes[index + 3] << 24)
            for index in range(0, len(raw_bytes), 4)
        ]

    def test_u32_parameters_unpack_least_significant_byte_first(self) -> None:
        self.assertEqual(
            IMPORTER._unpack_function_parameter_bytes((0x11223344, 0xFFFFFFFF)),
            [0x44, 0x33, 0x22, 0x11, 0xFF, 0xFF, 0xFF, 0xFF],
        )

    def test_function_parameter_text_preserves_unsigned_u32(self) -> None:
        payload = IMPORTER._function_parameter_text_payload([{
            "sourceEntryId": 7,
            "tableIndex": 2,
            "tblId": 5300,
            "fxnName": "fldEfcontrol",
            "functionParameters": [0, 4294967295],
        }])
        self.assertEqual(
            payload["indexEntries"][0]["functionParameters"],
            [0, 4294967295],
        )

    def test_area99_context_requires_exact_identity_and_parameter_count(self) -> None:
        parameters = [0] * IMPORTER.AREA99_LOOKUP_PARAMETER_COUNT
        valid = {
            "sourceEntryId": 4,
            "tableIndex": 3,
            "tblId": 5300,
            "fxnName": "fldEfcontrol",
            "functionParameters": parameters,
        }
        context = IMPORTER._find_area99_lookup_context([valid])
        self.assertIsNotNone(context)
        self.assertEqual(context.table_index, 3)
        self.assertIsNone(IMPORTER._find_area99_lookup_context([
            {**valid, "functionParameters": parameters[:-1]},
        ]))
        self.assertIsNone(IMPORTER._find_area99_lookup_context([valid, valid]))

    def test_altitude_thresholds_and_truncation_toward_zero(self) -> None:
        self.assertEqual(IMPORTER._area99_altitude_band(-150.01), 0)
        self.assertEqual(IMPORTER._area99_altitude_band(-150.0), 1)
        self.assertEqual(IMPORTER._area99_altitude_band(150.0), 1)
        self.assertEqual(IMPORTER._area99_altitude_band(150.01), 2)
        self.assertEqual(IMPORTER._trunc_toward_zero_division(-2399.0, 2400.0), 0)
        self.assertEqual(IMPORTER._trunc_toward_zero_division(-2400.0, 2400.0), -1)

    def test_resolves_both_pages_and_exact_zone_table(self) -> None:
        raw_bytes = [0] * (IMPORTER.AREA99_LOOKUP_PAGE_SIZE * 2)
        page0_index = 1 * 0x150 + 0 * 0x38 + 0 * 8 + 3 - 1
        page1_index = IMPORTER.AREA99_LOOKUP_PAGE_SIZE + page0_index
        raw_bytes[page0_index] = 47
        raw_bytes[page1_index] = 82
        parameters = self._pack_lsb_first(raw_bytes)
        unpacked = IMPORTER._unpack_function_parameter_bytes(parameters)
        first = IMPORTER._resolve_area99_encounter(
            unpacked,
            page=0,
            source_x=8400.0,
            source_y=0.0,
            source_z=7200.0,
            lane=3,
        )
        second = IMPORTER._resolve_area99_encounter(
            unpacked,
            page=1,
            source_x=8400.0,
            source_y=0.0,
            source_z=7200.0,
            lane=3,
        )
        self.assertTrue(first.valid)
        self.assertEqual((first.zone, first.table_id), (4, 7))
        self.assertTrue(second.valid)
        self.assertEqual((second.zone, second.table_id), (8, 2))

    def test_rejects_invalid_lane_bucket_and_zero_lookup(self) -> None:
        unpacked = [0] * (IMPORTER.AREA99_LOOKUP_PAGE_SIZE * 2)
        cases = (
            {"lane": 0, "source_x": 8400.0, "source_z": 7200.0},
            {"lane": 9, "source_x": 8400.0, "source_z": 7200.0},
            {"lane": 1, "source_x": 10800.0, "source_z": 7200.0},
            {"lane": 1, "source_x": 8400.0, "source_z": 9600.0},
            {"lane": 1, "source_x": 8400.0, "source_z": 7200.0},
        )
        for case in cases:
            with self.subTest(case=case):
                resolved = IMPORTER._resolve_area99_encounter(
                    unpacked,
                    page=0,
                    source_y=0.0,
                    **case,
                )
                self.assertFalse(resolved.valid)

    def test_lookup_image_is_56_by_18_and_nearest_cell_color_is_stable(self) -> None:
        raw_bytes = [0] * (IMPORTER.AREA99_LOOKUP_PAGE_SIZE * 2)
        raw_bytes[0] = 31
        parameters = self._pack_lsb_first(raw_bytes)
        pixels = IMPORTER._area99_lookup_image_pixels(
            parameters,
            page=0,
            mode="ZONE_TABLE",
        )
        self.assertEqual(
            len(pixels),
            IMPORTER.AREA99_LOOKUP_WIDTH * IMPORTER.AREA99_LOOKUP_HEIGHT * 4,
        )
        self.assertEqual(
            tuple(pixels[:4]),
            IMPORTER._resolved_zone_table_color(3, 1),
        )
        self.assertEqual(tuple(pixels[4:8]), (0.25, 0.25, 0.25, 1.0))

    def test_oklch_conversion_is_deterministic_bounded_and_gamut_mapped(self) -> None:
        neutral = IMPORTER._oklch_to_linear_rgb(0.5, 0.0, 55.0)
        self.assertAlmostEqual(neutral[0], neutral[1], places=7)
        self.assertAlmostEqual(neutral[1], neutral[2], places=7)
        for hue in range(0, 360, 15):
            first = IMPORTER._oklch_to_linear_rgb(0.82, 0.40, float(hue))
            second = IMPORTER._oklch_to_linear_rgb(0.82, 0.40, float(hue))
            self.assertEqual(first, second)
            self.assertTrue(all(0.0 <= component <= 1.0 for component in first))

    def test_zone_palette_uses_even_hue_pairs_and_alternating_lightness(self) -> None:
        pair_hues: list[float] = []
        for odd_zone in range(1, 13, 2):
            bright = IMPORTER._resolved_zone_oklch(odd_zone)
            dark = IMPORTER._resolved_zone_oklch(odd_zone + 1)
            self.assertEqual(bright[2], dark[2])
            self.assertEqual(bright[0], IMPORTER.RESOLVED_ZONE_LIGHTNESS_BRIGHT)
            self.assertEqual(dark[0], IMPORTER.RESOLVED_ZONE_LIGHTNESS_DARK)
            pair_hues.append(bright[2])
        pair_hues.append(IMPORTER._resolved_zone_oklch(13)[2])
        for first, second in zip(pair_hues, pair_hues[1:]):
            self.assertAlmostEqual(
                (second - first) % 360.0,
                IMPORTER.RESOLVED_ZONE_HUE_STEP,
                places=7,
            )
        self.assertEqual(
            IMPORTER._resolved_zone_oklch(13)[0],
            IMPORTER.RESOLVED_ZONE_LIGHTNESS_BRIGHT,
        )

    def test_zone_table_palette_stays_inside_zone_tiers_and_quantizes_distinctly(self) -> None:
        for zone in range(1, 14):
            colors: set[tuple[int, int, int]] = set()
            base_lightness, _base_chroma, base_hue = IMPORTER._resolved_zone_oklch(zone)
            for table_id in range(1, 10):
                lightness, chroma, hue = IMPORTER._resolved_zone_table_oklch(zone, table_id)
                self.assertLessEqual(abs(lightness - base_lightness), 0.04 + 1e-9)
                self.assertIn(chroma, IMPORTER.RESOLVED_TABLE_CHROMA)
                signed_offset = ((hue - base_hue + 180.0) % 360.0) - 180.0
                self.assertGreaterEqual(signed_offset, -18.0)
                self.assertLessEqual(signed_offset, 18.0)
                color = IMPORTER._resolved_zone_table_color(zone, table_id)
                colors.add(tuple(round(component * 255.0) for component in color[:3]))
            self.assertEqual(len(colors), 9)
            if zone % 2 == 1:
                self.assertTrue(all(
                    0.78 - 1e-9
                    <= IMPORTER._resolved_zone_table_oklch(zone, table_id)[0]
                    <= 0.86 + 1e-9
                    for table_id in range(1, 10)
                ))
            else:
                self.assertTrue(all(
                    0.46 - 1e-9
                    <= IMPORTER._resolved_zone_table_oklch(zone, table_id)[0]
                    <= 0.54 + 1e-9
                    for table_id in range(1, 10)
                ))

    def test_lookup_image_sets_color_space_before_upload_for_new_and_reused_images(self) -> None:
        raw_bytes = [0] * (IMPORTER.AREA99_LOOKUP_PAGE_SIZE * 2)
        raw_bytes[0] = 31
        raw_bytes[IMPORTER.AREA99_LOOKUP_PAGE_SIZE] = 72
        context = IMPORTER.EncounterLookupContext(
            kind="AREA99",
            function_parameters=tuple(self._pack_lsb_first(raw_bytes)),
        )
        images = self._FakeImages()
        original_data = getattr(IMPORTER.bpy, "data", None)
        IMPORTER.bpy.data = types.SimpleNamespace(images=images)
        try:
            image = IMPORTER._update_area99_lookup_image(
                "Regression",
                context,
                page=0,
                mode="ZONE_TABLE",
            )
            self.assertLess(image.events.index("colorspace"), image.events.index("pixels"))
            self.assertEqual(image.colorspace_settings.name, "Non-Color")
            self.assertEqual(tuple(image.pixels.values[:4]), IMPORTER._resolved_zone_table_color(3, 1))
            self.assertGreater(len(set(zip(*[iter(image.pixels.values)] * 4))), 1)

            image.events.clear()
            reused = IMPORTER._update_area99_lookup_image(
                "Regression",
                context,
                page=1,
                mode="ZONE_TABLE",
            )
            self.assertIs(reused, image)
            self.assertLess(image.events.index("colorspace"), image.events.index("pixels"))
            self.assertEqual(tuple(image.pixels.values[:4]), IMPORTER._resolved_zone_table_color(7, 2))
            self.assertEqual(image.events[-2:], ["update", "pack"])
        finally:
            if original_data is None:
                del IMPORTER.bpy.data
            else:
                IMPORTER.bpy.data = original_data

    def test_zero_zone_or_table_remains_structurally_valid_but_renders_gray(self) -> None:
        neutral = IMPORTER.RESOLVED_ENCOUNTER_NEUTRAL_COLOR
        for mode in IMPORTER.POSITION_AWARE_ENCOUNTER_MODES:
            with self.subTest(mode=mode, zero="zone"):
                self.assertEqual(IMPORTER._resolved_encounter_color(mode, 0, 7), neutral)
            with self.subTest(mode=mode, zero="table"):
                self.assertEqual(IMPORTER._resolved_encounter_color(mode, 1, 0), neutral)

    def test_dungeon_table_ids_are_direct_and_bounded(self) -> None:
        self.assertEqual(IMPORTER._dungeon_encounter_table_id(0), 0)
        self.assertEqual(IMPORTER._dungeon_encounter_table_id(10), 1)
        self.assertEqual(IMPORTER._dungeon_encounter_table_id(70), 7)
        self.assertIsNone(IMPORTER._dungeon_encounter_table_id(80))
        self.assertIsNone(IMPORTER._dungeon_encounter_table_id(90))

if __name__ == "__main__":
    unittest.main()
