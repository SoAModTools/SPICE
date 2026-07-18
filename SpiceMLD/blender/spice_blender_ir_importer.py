"""Spice Blender IR JSON importer (baseline).

This importer targets the current JSON payload exported by SpiceMLD's
BlenderIrJsonExporter. It intentionally focuses on currently exported fields:
- vertex positions
- triangle corner vertex indices
- material metadata + textureName
- texture pixelDataBase64 (rgba8)
- raw GOBJ vertex user attributes and GRND/GOBJ triangle metadata
- indexEntries with transform + meshIndices, including decoded GRND meshes
"""

from __future__ import annotations

import base64
import colorsys
import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, NamedTuple

import bpy
import mathutils
from bpy.props import (
    BoolProperty,
    CollectionProperty,
    EnumProperty,
    FloatProperty,
    FloatVectorProperty,
    IntProperty,
    StringProperty,
)
from bpy.types import Collection, Image, Material, Mesh, Object
from bpy_extras.io_utils import ImportHelper

bl_info = {
    "name": "Spice Blender IR Importer",
    "author": "Spice",
    "version": (0, 2, 0),
    "blender": (3, 6, 0),
    "location": "File > Import > Spice Blender IR (.json)",
    "description": "Imports blender_ir_scene.json exported from SpiceMLD",
    "category": "Import-Export",
}


@dataclass
class ImportStats:
    mesh_count: int = 0
    object_count: int = 0
    armature_count: int = 0
    texture_count: int = 0
    material_count: int = 0
    animation_action_count: int = 0
    warnings: int = 0
    warning_messages: list[str] = field(default_factory=list)
    debug_lines: int = 0

    def add_warning(self, message: str) -> None:
        self.warnings += 1
        self.warning_messages.append(message)
        print(f"[Spice Import Warning] {message}")


class TextureLookup(NamedTuple):
    by_name: dict[str, Image]
    by_id: dict[int, Image]


BLENDER_CUSTOM_INT_MIN = -(2**31)
BLENDER_CUSTOM_INT_MAX = (2**31) - 1
NJCM_TO_BLENDER_AXIS = mathutils.Quaternion((1.0, 0.0, 0.0), 1.5707963267948966)
NJD_EVAL_UNIT_POS = 1 << 0
NJD_EVAL_UNIT_ANG = 1 << 1
NJD_EVAL_UNIT_SCL = 1 << 2
GRND_VISUAL_SOURCE_Y_OFFSET = 0.05
TRIANGLE_METADATA_COLOR_ATTRIBUTE = "SpiceTriangleMetadataColor"
TRIANGLE_METADATA_RESOLVED_ENCOUNTER_COLOR_ATTRIBUTE = "SpiceResolvedEncounterColor"
TRIANGLE_METADATA_RAW_ATTRIBUTE_NAMES = (
    "spice_triangle_metadata_raw_u16_0",
    "spice_triangle_metadata_raw_u16_1",
    "spice_triangle_metadata_raw_u16_2",
)
TRIANGLE_METADATA_SELECTOR_ATTRIBUTE = "spice_triangle_metadata_selector_low15"
TRIANGLE_METADATA_WINDING_ATTRIBUTE = "spice_triangle_metadata_stream_winding_high_bit"
TRIANGLE_METADATA_DECODED_ATTRIBUTE = "spice_triangle_metadata_decoded_u16"
TRIANGLE_METADATA_DECODED_HIGH_BIT_ATTRIBUTE = "spice_triangle_metadata_decoded_high_bit"
TRIANGLE_METADATA_BEHAVIOR_CLASS_ATTRIBUTE = "spice_triangle_metadata_behavior_class"
TRIANGLE_METADATA_ENCOUNTER_SELECTOR_ATTRIBUTE = "spice_triangle_metadata_encounter_selector"
TRIANGLE_METADATA_PAYLOAD_GROUP_ATTRIBUTE = "spice_triangle_metadata_payload_group"
TRIANGLE_METADATA_DIGIT_ATTRIBUTE_NAMES = (
    "spice_triangle_metadata_digit_ones",
    "spice_triangle_metadata_digit_tens",
    "spice_triangle_metadata_digit_hundreds",
    "spice_triangle_metadata_digit_thousands",
)
TRIANGLE_METADATA_HUE_CENTERS = (232.0, 52.0, 124.0, 16.0, 268.0, 160.0, 340.0, 88.0, 304.0, 196.0)
TRIANGLE_METADATA_ONES_TABLE = (0x0000, 0x6800, 0x7800, 0x1800, 0x1400, 0x0100, 0x1200, 0x1300, 0x0000, 0x0000)
TRIANGLE_METADATA_TENS_TABLE = (0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009)
TRIANGLE_METADATA_HUNDREDS_TABLE = (0x0000, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050, 0x0060, 0x0000, 0x0000, 0x8000)
TRIANGLE_METADATA_THOUSANDS_TABLE = (0x0000, 0x8000, 0x8200, 0x8400, 0x8600, 0x8800, 0x8A00, 0x8C00, 0x8E00, 0x9000)
TRIANGLE_METADATA_DISPLAY_MODES = (
    ("SKY_RIFT_FORCE", "Sky-Rift Force", "Show triangles dispatched through known sky-rift force classes."),
    ("ENCOUNTER_SELECTOR", "Encounter Selector", "Show the encounter selector encoded by the authored tens digit."),
    ("ENCOUNTER_ZONE", "Encounter Zone", "Resolve each authored lane to its position-dependent Area 99 encounter zone."),
    ("ENCOUNTER_TABLE_ID", "Encounter Table ID", "Resolve each authored lane to its encounter table ID."),
    ("ZONE_TABLE", "Zone + Table", "Show resolved zone hue with table-ID saturation and brightness."),
    ("FORCE_RESOLVED_ENCOUNTER", "Sky-Rift Force + Resolved Encounter", "Checker the force and resolved encounter layers without merging their meanings."),
    ("UNATTRIBUTED", "Unattributed Values", "Show payload and decoded classes that are not currently attributed."),
    ("RAW0", "Raw Word 0", "Color by raw triangle metadata word 0."),
    ("RAW1", "Raw Word 1", "Color by raw triangle metadata word 1."),
    ("RAW2", "Raw Word 2", "Color by raw triangle metadata word 2."),
    ("WINDING", "Stream Winding", "Color by the stream winding high bit in word 2."),
)

TRIANGLE_METADATA_FORCE_CLASSES = {
    0: ("Progression-gated rift force", 52.0, 0.68, 0.96),
    1: ("Progression-gated rift force", 44.0, 0.92, 0.80),
    2: ("Hard-boundary rift force", 124.0, 0.68, 0.90),
    3: ("Hard-boundary rift force", 136.0, 0.92, 0.72),
    4: ("Y-gated rift force", 18.0, 0.72, 0.95),
    5: ("Y-gated rift force", 8.0, 0.94, 0.78),
    6: ("Y-gated rift force", 268.0, 0.70, 0.93),
    7: ("Y-gated rift force", 282.0, 0.94, 0.75),
    30: ("Rift-force dispatch class 30", 350.0, 0.90, 0.85),
}
TRIANGLE_METADATA_ENCOUNTER_HUES = (0.0, 210.0, 185.0, 160.0, 110.0, 60.0, 32.0, 0.0, 320.0, 275.0)
POSITION_AWARE_ENCOUNTER_MODES = {
    "ENCOUNTER_ZONE",
    "ENCOUNTER_TABLE_ID",
    "ZONE_TABLE",
    "FORCE_RESOLVED_ENCOUNTER",
}
AREA99_LOOKUP_PARAMETER_COUNT = 504
AREA99_LOOKUP_PAGE_SIZE = 0x3F0
AREA99_LOOKUP_WIDTH = 56
AREA99_LOOKUP_HEIGHT = 18
AREA99_FXN_NAME = "fldEfcontrol"
AREA99_TBL_ID = 5300
RESOLVED_ENCOUNTER_NEUTRAL_COLOR = (0.25, 0.25, 0.25, 1.0)
RESOLVED_ENCOUNTER_INVALID_COLOR = (1.0, 0.0, 1.0, 1.0)
RESOLVED_ZONE_HUE_START = 55.0
RESOLVED_ZONE_HUE_STEP = 360.0 / 7.0
RESOLVED_ZONE_LIGHTNESS_BRIGHT = 0.82
RESOLVED_ZONE_LIGHTNESS_DARK = 0.50
RESOLVED_ZONE_CHROMA = 0.17
RESOLVED_TABLE_HUE_STEP = 360.0 / 10.0
RESOLVED_TABLE_HUE_OFFSET_START = -18.0
RESOLVED_TABLE_HUE_OFFSET_STEP = 4.0
RESOLVED_TABLE_CHROMA = (0.14, 0.20)
RESOLVED_TABLE_LIGHTNESS_OFFSETS = (-0.04, 0.0, 0.04)


@dataclass(frozen=True)
class TriangleMetadataDecoded:
    raw_u16: tuple[int, int, int]
    selector_low15: int
    stream_winding_high_bit: int
    decoded_u16: int
    decoded_high_bit: int
    behavior_class: int
    encounter_selector: int
    payload_group: int
    digits: tuple[int, int, int, int]


@dataclass(frozen=True)
class TriangleMetadataFamily:
    family_id: str
    label: str
    hue_start: float
    hue_end: float
    saturation_range: tuple[float, float] | None = None
    value_range: tuple[float, float] | None = None
    match: dict[str, int | bool] = field(default_factory=dict)


@dataclass(frozen=True)
class EncounterLookupContext:
    kind: str
    function_parameters: tuple[int, ...] = ()
    source_entry_id: int = 0
    table_index: int = 0


@dataclass(frozen=True)
class ResolvedEncounter:
    valid: bool
    lane: int
    page: int
    x_bucket: int
    z_bucket: int
    altitude_band: int
    lookup_index: int
    packed_byte: int
    zone: int
    table_id: int


def _unpack_function_parameter_bytes(function_parameters: list[int] | tuple[int, ...]) -> list[int]:
    unpacked: list[int] = []
    for index, raw_value in enumerate(function_parameters):
        value = int(raw_value)
        if value < 0 or value > 0xFFFFFFFF:
            raise ValueError(f"functionParameters[{index}]={value} is outside the u32 range")
        unpacked.extend((
            value & 0xFF,
            (value >> 8) & 0xFF,
            (value >> 16) & 0xFF,
            (value >> 24) & 0xFF,
        ))
    return unpacked


def _trunc_toward_zero_division(numerator: float, denominator: float) -> int:
    if denominator == 0.0:
        raise ZeroDivisionError("encounter bucket denominator cannot be zero")
    return math.trunc(numerator / denominator)


def _area99_altitude_band(source_y: float) -> int:
    if source_y < -150.0:
        return 0
    if source_y <= 150.0:
        return 1
    return 2


def _area99_encounter_lane(raw_word_2: int) -> int:
    return ((int(raw_word_2) & 0x7FFF) // 10) % 10


def _resolve_area99_encounter(
    packed_pages: list[int] | tuple[int, ...],
    *,
    page: int,
    source_x: float,
    source_y: float,
    source_z: float,
    lane: int,
    forced_altitude_band: int | None = None,
) -> ResolvedEncounter:
    x_bucket = _trunc_toward_zero_division(8400.0 - source_x, 2400.0)
    z_bucket = _trunc_toward_zero_division(7200.0 - source_z, 2400.0)
    altitude_band = (
        _area99_altitude_band(source_y)
        if forced_altitude_band is None
        else int(forced_altitude_band)
    )
    valid = (
        page in (0, 1)
        and 1 <= lane <= 8
        and 0 <= x_bucket < 7
        and 0 <= z_bucket < 6
        and 0 <= altitude_band < 3
    )
    lookup_index = -1
    packed_byte = 0
    if valid:
        lookup_index = (
            page * AREA99_LOOKUP_PAGE_SIZE
            + altitude_band * 0x150
            + z_bucket * 0x38
            + x_bucket * 8
            + lane
            - 1
        )
        if lookup_index < 0 or lookup_index >= len(packed_pages):
            valid = False
        else:
            packed_byte = int(packed_pages[lookup_index])
            valid = packed_byte != 0
    return ResolvedEncounter(
        valid=valid,
        lane=lane,
        page=page,
        x_bucket=x_bucket,
        z_bucket=z_bucket,
        altitude_band=altitude_band,
        lookup_index=lookup_index,
        packed_byte=packed_byte,
        zone=(packed_byte // 10) if valid else 0,
        table_id=(packed_byte % 10) if valid else 0,
    )


def _dungeon_encounter_table_id(raw_word_2: int) -> int | None:
    lane = _area99_encounter_lane(raw_word_2)
    if lane == 0:
        return 0
    return lane if lane <= 7 else None


def _oklab_to_linear_rgb(
    lightness: float,
    a_component: float,
    b_component: float,
) -> tuple[float, float, float]:
    l_root = lightness + 0.3963377774 * a_component + 0.2158037573 * b_component
    m_root = lightness - 0.1055613458 * a_component - 0.0638541728 * b_component
    s_root = lightness - 0.0894841775 * a_component - 1.2914855480 * b_component
    l_value = l_root * l_root * l_root
    m_value = m_root * m_root * m_root
    s_value = s_root * s_root * s_root
    return (
        4.0767416621 * l_value - 3.3077115913 * m_value + 0.2309699292 * s_value,
        -1.2684380046 * l_value + 2.6097574011 * m_value - 0.3413193965 * s_value,
        -0.0041960863 * l_value - 0.7034186147 * m_value + 1.7076147010 * s_value,
    )


def _oklch_candidate_linear_rgb(
    lightness: float,
    chroma: float,
    hue_degrees: float,
) -> tuple[float, float, float]:
    hue_radians = math.radians(hue_degrees % 360.0)
    return _oklab_to_linear_rgb(
        lightness,
        chroma * math.cos(hue_radians),
        chroma * math.sin(hue_radians),
    )


def _linear_rgb_in_gamut(color: tuple[float, float, float]) -> bool:
    return all(0.0 <= component <= 1.0 for component in color)


def _oklch_to_linear_rgb(
    lightness: float,
    chroma: float,
    hue_degrees: float,
) -> tuple[float, float, float]:
    bounded_lightness = min(max(float(lightness), 0.0), 1.0)
    requested_chroma = max(float(chroma), 0.0)
    color = _oklch_candidate_linear_rgb(
        bounded_lightness,
        requested_chroma,
        hue_degrees,
    )
    if not _linear_rgb_in_gamut(color):
        low = 0.0
        high = requested_chroma
        color = _oklch_candidate_linear_rgb(bounded_lightness, low, hue_degrees)
        for _iteration in range(16):
            candidate_chroma = (low + high) * 0.5
            candidate = _oklch_candidate_linear_rgb(
                bounded_lightness,
                candidate_chroma,
                hue_degrees,
            )
            if _linear_rgb_in_gamut(candidate):
                low = candidate_chroma
                color = candidate
            else:
                high = candidate_chroma
    return tuple(min(max(component, 0.0), 1.0) for component in color)


def _resolved_zone_oklch(zone: int) -> tuple[float, float, float]:
    if zone < 1 or zone > 13:
        raise ValueError(f"resolved encounter zone {zone} is outside 1..13")
    pair_index = (zone - 1) // 2
    lightness = (
        RESOLVED_ZONE_LIGHTNESS_BRIGHT
        if zone % 2 == 1
        else RESOLVED_ZONE_LIGHTNESS_DARK
    )
    hue = (RESOLVED_ZONE_HUE_START + pair_index * RESOLVED_ZONE_HUE_STEP) % 360.0
    return lightness, RESOLVED_ZONE_CHROMA, hue


def _resolved_table_oklch(table_id: int) -> tuple[float, float, float]:
    if table_id < 0 or table_id > 9:
        raise ValueError(f"resolved encounter table ID {table_id} is outside 0..9")
    lightness = (
        RESOLVED_ZONE_LIGHTNESS_BRIGHT
        if table_id % 2 == 1
        else RESOLVED_ZONE_LIGHTNESS_DARK
    )
    hue = (RESOLVED_ZONE_HUE_START + table_id * RESOLVED_TABLE_HUE_STEP) % 360.0
    return lightness, RESOLVED_ZONE_CHROMA, hue


def _resolved_zone_table_oklch(zone: int, table_id: int) -> tuple[float, float, float]:
    base_lightness, _base_chroma, base_hue = _resolved_zone_oklch(zone)
    lightness = base_lightness + RESOLVED_TABLE_LIGHTNESS_OFFSETS[
        table_id % len(RESOLVED_TABLE_LIGHTNESS_OFFSETS)
    ]
    chroma = RESOLVED_TABLE_CHROMA[table_id % len(RESOLVED_TABLE_CHROMA)]
    hue = (
        base_hue
        + RESOLVED_TABLE_HUE_OFFSET_START
        + table_id * RESOLVED_TABLE_HUE_OFFSET_STEP
    ) % 360.0
    return lightness, chroma, hue


def _oklch_rgba(lightness: float, chroma: float, hue: float) -> tuple[float, float, float, float]:
    red, green, blue = _oklch_to_linear_rgb(lightness, chroma, hue)
    return red, green, blue, 1.0


def _resolved_zone_color(zone: int) -> tuple[float, float, float, float]:
    if zone == 0:
        return RESOLVED_ENCOUNTER_NEUTRAL_COLOR
    if zone < 0 or zone > 13:
        return RESOLVED_ENCOUNTER_INVALID_COLOR
    return _oklch_rgba(*_resolved_zone_oklch(zone))


def _resolved_table_color(table_id: int) -> tuple[float, float, float, float]:
    if table_id == 0:
        return RESOLVED_ENCOUNTER_NEUTRAL_COLOR
    if table_id < 0 or table_id > 9:
        return RESOLVED_ENCOUNTER_INVALID_COLOR
    return _oklch_rgba(*_resolved_table_oklch(table_id))


def _resolved_zone_table_color(zone: int, table_id: int) -> tuple[float, float, float, float]:
    if zone == 0 or table_id == 0:
        return RESOLVED_ENCOUNTER_NEUTRAL_COLOR
    if zone < 0 or zone > 13 or table_id < 0 or table_id > 9:
        return RESOLVED_ENCOUNTER_INVALID_COLOR
    return _oklch_rgba(*_resolved_zone_table_oklch(zone, table_id))


def _resolved_encounter_color(
    mode: str,
    zone: int,
    table_id: int,
) -> tuple[float, float, float, float]:
    if zone == 0 or table_id == 0:
        return RESOLVED_ENCOUNTER_NEUTRAL_COLOR
    if mode == "ENCOUNTER_ZONE":
        return _resolved_zone_color(zone)
    if mode == "ENCOUNTER_TABLE_ID":
        return _resolved_table_color(table_id)
    return _resolved_zone_table_color(zone, table_id)


def _area99_lookup_image_pixels(
    function_parameters: list[int] | tuple[int, ...],
    *,
    page: int,
    mode: str,
) -> list[float]:
    if len(function_parameters) != AREA99_LOOKUP_PARAMETER_COUNT:
        raise ValueError(
            f"Area 99 encounter lookup requires {AREA99_LOOKUP_PARAMETER_COUNT} u32 parameters"
        )
    packed_pages = _unpack_function_parameter_bytes(function_parameters)
    pixels: list[float] = []
    lookup_mode = "ZONE_TABLE" if mode == "FORCE_RESOLVED_ENCOUNTER" else mode
    for altitude_band in range(3):
        for z_bucket in range(6):
            for x_bucket in range(7):
                for lane in range(1, 9):
                    lookup_index = (
                        page * AREA99_LOOKUP_PAGE_SIZE
                        + altitude_band * 0x150
                        + z_bucket * 0x38
                        + x_bucket * 8
                        + lane
                        - 1
                    )
                    packed_byte = packed_pages[lookup_index]
                    if packed_byte == 0:
                        color = RESOLVED_ENCOUNTER_NEUTRAL_COLOR
                    else:
                        color = _resolved_encounter_color(
                            lookup_mode,
                            packed_byte // 10,
                            packed_byte % 10,
                        )
                    pixels.extend(color)
    return pixels


def _read_u32_function_parameters(entry: dict[str, Any]) -> tuple[int, ...]:
    raw_parameters = entry.get("functionParameters", [])
    if not isinstance(raw_parameters, list):
        raise ValueError("functionParameters must be an array")
    parameters: list[int] = []
    for index, raw_value in enumerate(raw_parameters):
        value = int(raw_value)
        if value < 0 or value > 0xFFFFFFFF:
            raise ValueError(f"functionParameters[{index}]={value} is outside the u32 range")
        parameters.append(value)
    return tuple(parameters)


def _find_area99_lookup_context(
    index_entries: list[dict[str, Any]],
    stats: ImportStats | None = None,
) -> EncounterLookupContext | None:
    candidates: list[EncounterLookupContext] = []
    for entry_index, entry in enumerate(index_entries):
        if str(entry.get("fxnName", "")) != AREA99_FXN_NAME:
            continue
        if int(entry.get("tblId", 0)) != AREA99_TBL_ID:
            continue
        try:
            parameters = _read_u32_function_parameters(entry)
        except (TypeError, ValueError) as exc:
            if stats is not None:
                stats.add_warning(f"indexEntries[{entry_index}] Area 99 parameters are invalid: {exc}")
            continue
        if len(parameters) != AREA99_LOOKUP_PARAMETER_COUNT:
            if stats is not None:
                stats.add_warning(
                    f"indexEntries[{entry_index}] {AREA99_FXN_NAME} TBLID {AREA99_TBL_ID} has "
                    f"{len(parameters)} parameters; expected {AREA99_LOOKUP_PARAMETER_COUNT}."
                )
            continue
        candidates.append(EncounterLookupContext(
            kind="AREA99",
            function_parameters=parameters,
            source_entry_id=int(entry.get("sourceEntryId", 0)),
            table_index=int(entry.get("tableIndex", entry_index)),
        ))
    if len(candidates) == 1:
        return candidates[0]
    if len(candidates) > 1 and stats is not None:
        stats.add_warning(
            f"Found {len(candidates)} valid {AREA99_FXN_NAME} TBLID {AREA99_TBL_ID} entries; "
            "position-aware encounter resolution requires exactly one."
        )
    return None


def _function_parameter_text_payload(index_entries: list[dict[str, Any]]) -> dict[str, Any]:
    records: list[dict[str, Any]] = []
    for entry_index, entry in enumerate(index_entries):
        records.append({
            "sourceEntryId": int(entry.get("sourceEntryId", 0)),
            "tableIndex": int(entry.get("tableIndex", entry_index)),
            "tblId": int(entry.get("tblId", 0)),
            "fxnName": str(entry.get("fxnName", "")),
            "functionParameters": list(_read_u32_function_parameters(entry)),
        })
    return {"schemaVersion": 1, "indexEntries": records}


def _decode_triangle_metadata(raw_u16: tuple[int, int, int]) -> TriangleMetadataDecoded:
    selector = raw_u16[2] & 0x7FFF
    ones = selector % 10
    tens = (selector // 10) % 10
    hundreds = (selector // 100) % 10
    thousands = (selector // 1000) % 10

    decoded = TRIANGLE_METADATA_ONES_TABLE[ones]
    if selector // 10 != 0:
        decoded |= TRIANGLE_METADATA_TENS_TABLE[tens]
    if selector // 100 != 0:
        decoded |= TRIANGLE_METADATA_HUNDREDS_TABLE[hundreds]
    if selector // 1000 != 0:
        decoded = (decoded + TRIANGLE_METADATA_THOUSANDS_TABLE[thousands]) & 0xFFFF

    return TriangleMetadataDecoded(
        raw_u16=raw_u16,
        selector_low15=selector,
        stream_winding_high_bit=(raw_u16[2] >> 15) & 1,
        decoded_u16=decoded,
        decoded_high_bit=(decoded >> 15) & 1,
        behavior_class=(decoded >> 8) & 0x7F,
        encounter_selector=decoded & 0x0F,
        payload_group=(decoded >> 4) & 0x0F,
        digits=(ones, tens, hundreds, thousands),
    )


def _read_profile_number(value: Any, *, field_name: str) -> float:
    if isinstance(value, bool):
        raise ValueError(f"{field_name} must be numeric.")
    try:
        return float(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{field_name} must be numeric.") from exc


def _read_profile_range(value: Any, *, field_name: str) -> tuple[float, float] | None:
    if value is None:
        return None
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{field_name} must contain two numeric values.")
    low = _read_profile_number(value[0], field_name=f"{field_name}[0]")
    high = _read_profile_number(value[1], field_name=f"{field_name}[1]")
    if low < 0.0 or high > 1.0 or low > high:
        raise ValueError(f"{field_name} must be ordered within 0..1.")
    return low, high


def _parse_triangle_metadata_profile(payload: Any) -> tuple[str, list[TriangleMetadataFamily]]:
    if not isinstance(payload, dict):
        raise ValueError("Triangle metadata profile root must be an object.")
    if payload.get("schemaVersion") != 1:
        raise ValueError("Triangle metadata profile schemaVersion must be 1.")

    profile_name = str(payload.get("name", "Custom Profile")).strip() or "Custom Profile"
    raw_families = payload.get("families")
    if not isinstance(raw_families, list):
        raise ValueError("Triangle metadata profile families must be an array.")

    allowed_match_fields = {
        "selectorMin",
        "selectorMax",
        "thousandsDigit",
        "decodedHighBit",
        "behaviorClassMin",
        "behaviorClassMax",
        "encounterSelectorMin",
        "encounterSelectorMax",
        "payloadGroupMin",
        "payloadGroupMax",
    }
    seen_ids: set[str] = set()
    families: list[TriangleMetadataFamily] = []
    for index, raw_family in enumerate(raw_families):
        prefix = f"families[{index}]"
        if not isinstance(raw_family, dict):
            raise ValueError(f"{prefix} must be an object.")
        family_id = str(raw_family.get("id", "")).strip()
        if not family_id:
            raise ValueError(f"{prefix}.id is required.")
        if family_id in seen_ids:
            raise ValueError(f"Duplicate triangle metadata family id: {family_id}")
        seen_ids.add(family_id)

        label = str(raw_family.get("label", family_id)).strip() or family_id
        hue_start = _read_profile_number(raw_family.get("hueStart"), field_name=f"{prefix}.hueStart")
        hue_end = _read_profile_number(raw_family.get("hueEnd"), field_name=f"{prefix}.hueEnd")
        if hue_start < 0.0 or hue_end > 360.0 or hue_start > hue_end:
            raise ValueError(f"{prefix} hue range must be ordered within 0..360.")

        raw_match = raw_family.get("match")
        if not isinstance(raw_match, dict) or not raw_match:
            raise ValueError(f"{prefix}.match must contain at least one criterion.")
        unknown_fields = set(raw_match) - allowed_match_fields
        if unknown_fields:
            raise ValueError(f"{prefix}.match contains unsupported fields: {sorted(unknown_fields)}")

        match: dict[str, int | bool] = {}
        for key, raw_value in raw_match.items():
            if key == "decodedHighBit":
                if not isinstance(raw_value, bool):
                    raise ValueError(f"{prefix}.match.{key} must be boolean.")
                match[key] = raw_value
            else:
                try:
                    match[key] = int(raw_value)
                except (TypeError, ValueError) as exc:
                    raise ValueError(f"{prefix}.match.{key} must be an integer.") from exc

        range_pairs = (
            ("selectorMin", "selectorMax", 0, 0x7FFF),
            ("behaviorClassMin", "behaviorClassMax", 0, 0x7F),
            ("encounterSelectorMin", "encounterSelectorMax", 0, 0x0F),
            ("payloadGroupMin", "payloadGroupMax", 0, 0x0F),
        )
        for low_key, high_key, minimum, maximum in range_pairs:
            low = int(match.get(low_key, minimum))
            high = int(match.get(high_key, maximum))
            if low < minimum or high > maximum or low > high:
                raise ValueError(f"{prefix}.match {low_key}/{high_key} is invalid.")
        if "thousandsDigit" in match and not 0 <= int(match["thousandsDigit"]) <= 9:
            raise ValueError(f"{prefix}.match.thousandsDigit must be within 0..9.")

        families.append(TriangleMetadataFamily(
            family_id=family_id,
            label=label,
            hue_start=hue_start,
            hue_end=hue_end,
            saturation_range=_read_profile_range(
                raw_family.get("saturationRange"),
                field_name=f"{prefix}.saturationRange",
            ),
            value_range=_read_profile_range(
                raw_family.get("valueRange"),
                field_name=f"{prefix}.valueRange",
            ),
            match=match,
        ))
    return profile_name, families


def _triangle_metadata_family_matches(
    family: TriangleMetadataFamily,
    decoded: TriangleMetadataDecoded,
) -> bool:
    values = {
        "selectorMin": decoded.selector_low15,
        "selectorMax": decoded.selector_low15,
        "thousandsDigit": decoded.digits[3],
        "decodedHighBit": bool(decoded.decoded_high_bit),
        "behaviorClassMin": decoded.behavior_class,
        "behaviorClassMax": decoded.behavior_class,
        "encounterSelectorMin": decoded.encounter_selector,
        "encounterSelectorMax": decoded.encounter_selector,
        "payloadGroupMin": decoded.payload_group,
        "payloadGroupMax": decoded.payload_group,
    }
    for key, expected in family.match.items():
        actual = values[key]
        if key.endswith("Min") and int(actual) < int(expected):
            return False
        if key.endswith("Max") and int(actual) > int(expected):
            return False
        if not key.endswith("Min") and not key.endswith("Max") and actual != expected:
            return False
    return True


def _resolve_triangle_metadata_family(
    decoded: TriangleMetadataDecoded,
    families: list[TriangleMetadataFamily],
) -> TriangleMetadataFamily:
    for family in families:
        if _triangle_metadata_family_matches(family, decoded):
            return family
    digit = decoded.digits[3]
    center = TRIANGLE_METADATA_HUE_CENTERS[digit]
    return TriangleMetadataFamily(
        family_id=f"authored_{digit}",
        label=f"Authored family {digit}",
        hue_start=center - 12.0,
        hue_end=center + 12.0,
    )


def _triangle_metadata_display_family(
    family_id: str,
    label: str,
    hue: float,
) -> TriangleMetadataFamily:
    return TriangleMetadataFamily(
        family_id=family_id,
        label=label,
        hue_start=hue,
        hue_end=hue,
    )


def _triangle_metadata_force_info(
    decoded: TriangleMetadataDecoded,
) -> tuple[str, float, float, float] | None:
    if decoded.decoded_high_bit == 0:
        return None
    return TRIANGLE_METADATA_FORCE_CLASSES.get(decoded.behavior_class)


def _triangle_metadata_force_color(
    decoded: TriangleMetadataDecoded,
) -> tuple[tuple[float, float, float, float], TriangleMetadataFamily] | None:
    info = _triangle_metadata_force_info(decoded)
    if info is None:
        return None
    label, hue, saturation, value = info
    red, green, blue = colorsys.hsv_to_rgb(hue / 360.0, saturation, value)
    return (
        (red, green, blue, 1.0),
        _triangle_metadata_display_family(
            f"force_class_{decoded.behavior_class}",
            label,
            hue,
        ),
    )


def _triangle_metadata_encounter_color(
    selector: int,
) -> tuple[tuple[float, float, float, float], TriangleMetadataFamily]:
    if selector <= 0:
        return (
            (0.35, 0.35, 0.35, 1.0),
            _triangle_metadata_display_family("encounter_0", "No encounter selector", 0.0),
        )
    hue = TRIANGLE_METADATA_ENCOUNTER_HUES[selector % len(TRIANGLE_METADATA_ENCOUNTER_HUES)]
    red, green, blue = colorsys.hsv_to_rgb(hue / 360.0, 0.86, 0.94)
    return (
        (red, green, blue, 1.0),
        _triangle_metadata_display_family(
            f"encounter_{selector}",
            f"Encounter selector {selector}",
            hue,
        ),
    )


def _triangle_metadata_unattributed_identity(
    decoded: TriangleMetadataDecoded,
) -> tuple[int, int, int]:
    force_is_attributed = _triangle_metadata_force_info(decoded) is not None
    unclassified_high_bit = 0
    unclassified_class = 0
    if not force_is_attributed and (decoded.decoded_high_bit != 0 or decoded.behavior_class != 0):
        unclassified_high_bit = decoded.decoded_high_bit
        unclassified_class = decoded.behavior_class + 1
    return decoded.payload_group, unclassified_high_bit, unclassified_class


def _triangle_metadata_unattributed_color(
    decoded: TriangleMetadataDecoded,
) -> tuple[tuple[float, float, float, float], TriangleMetadataFamily]:
    payload, high_bit, encoded_class = _triangle_metadata_unattributed_identity(decoded)
    if payload == 0 and encoded_class == 0:
        return (
            (0.35, 0.35, 0.35, 1.0),
            _triangle_metadata_display_family("unattributed_none", "No unattributed word-2 value", 0.0),
        )
    key = payload | (high_bit << 4) | (encoded_class << 5)
    color = _metadata_categorical_color(key, zero_is_gray=False)
    description_parts: list[str] = []
    if payload != 0:
        description_parts.append(f"payload group {payload}")
    if encoded_class != 0:
        bit_label = "set" if high_bit else "clear"
        description_parts.append(
            f"unclassified decoded class {encoded_class - 1}, high bit {bit_label}"
        )
    return (
        color,
        _triangle_metadata_display_family(
            f"unattributed_{key}",
            "; ".join(description_parts),
            colorsys.rgb_to_hsv(color[0], color[1], color[2])[0] * 360.0,
        ),
    )


def _metadata_categorical_color(value: int, *, zero_is_gray: bool = True) -> tuple[float, float, float, float]:
    if value < 0:
        return 1.0, 0.0, 1.0, 1.0
    if zero_is_gray and value == 0:
        return 0.35, 0.35, 0.35, 1.0
    mixed = (value * 0x9E3779B1) & 0xFFFFFFFF
    hue = float(mixed % 360) / 360.0
    red, green, blue = colorsys.hsv_to_rgb(hue, 0.78, 0.92)
    return red, green, blue, 1.0


def _triangle_metadata_color(
    decoded: TriangleMetadataDecoded,
    mode: str,
    families: list[TriangleMetadataFamily],
) -> tuple[tuple[float, float, float, float], TriangleMetadataFamily]:
    authored_family = _resolve_triangle_metadata_family(decoded, families)
    force_color = _triangle_metadata_force_color(decoded)
    if mode == "SKY_RIFT_FORCE":
        if force_color is not None:
            return force_color
        return (
            (0.35, 0.35, 0.35, 1.0),
            _triangle_metadata_display_family("force_none", "No attributed sky-rift force", 0.0),
        )
    if mode in {"ENCOUNTER_SELECTOR", "ENCOUNTER"}:
        return _triangle_metadata_encounter_color(decoded.encounter_selector)
    if mode == "ENCOUNTER_ZONE":
        return (
            (0.35, 0.35, 0.35, 1.0),
            _triangle_metadata_display_family(
                "dungeon_zone_unavailable",
                "Encounter zone unavailable",
                0.0,
            ),
        )
    if mode in {"ENCOUNTER_TABLE_ID", "ZONE_TABLE"}:
        table_id = _dungeon_encounter_table_id(decoded.raw_u16[2])
        if table_id is None:
            return (
                (1.0, 0.0, 1.0, 1.0),
                _triangle_metadata_display_family(
                    "dungeon_table_unsupported",
                    "Unsupported dungeon encounter selector",
                    300.0,
                ),
            )
        color = _resolved_table_color(table_id)
        return (
            color,
            _triangle_metadata_display_family(
                f"dungeon_table_{table_id}",
                "No encounter" if table_id == 0 else f"Encounter table ID {table_id}",
                colorsys.rgb_to_hsv(color[0], color[1], color[2])[0] * 360.0,
            ),
        )
    if mode == "FORCE_RESOLVED_ENCOUNTER":
        if force_color is not None:
            return force_color
        return (
            (0.35, 0.35, 0.35, 1.0),
            _triangle_metadata_display_family("force_none", "No attributed sky-rift force", 0.0),
        )
    if mode in {"UNATTRIBUTED", "PAYLOAD"}:
        return _triangle_metadata_unattributed_color(decoded)
    if mode == "RAW0":
        return _metadata_categorical_color(decoded.raw_u16[0]), authored_family
    if mode == "RAW1":
        return _metadata_categorical_color(decoded.raw_u16[1]), authored_family
    if mode == "RAW2":
        return _metadata_categorical_color(decoded.raw_u16[2]), authored_family
    if mode == "WINDING":
        return ((0.12, 0.48, 0.9, 1.0) if decoded.stream_winding_high_bit == 0 else (1.0, 0.28, 0.05, 1.0)), authored_family
    return _triangle_metadata_unattributed_color(decoded)


def _read_int(value: Any, *, field_name: str) -> int:
    try:
        return int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"Invalid integer for {field_name}: {value!r}") from exc


def _set_custom_int_property(
    owner: Any,
    key: str,
    raw_value: Any,
    stats: ImportStats,
    *,
    field_name: str,
) -> None:
    value = _read_int(raw_value, field_name=field_name)
    if BLENDER_CUSTOM_INT_MIN <= value <= BLENDER_CUSTOM_INT_MAX:
        owner[key] = value
        return

    owner[key] = str(value)
    stats.add_warning(
        (
            f"{field_name}={value} does not fit Blender custom int range "
            f"[{BLENDER_CUSTOM_INT_MIN}, {BLENDER_CUSTOM_INT_MAX}] "
            f"and was stored as string property '{key}'."
        )
    )


def _write_mesh_int_attribute(
    mesh: Mesh,
    name: str,
    domain: str,
    values: list[int],
    stats: ImportStats,
) -> None:
    expected_count = len(mesh.polygons) if domain == "FACE" else len(mesh.vertices)
    if len(values) != expected_count:
        stats.add_warning(
            f"Mesh attribute '{name}' has {len(values)} values for {expected_count} {domain.lower()} elements."
        )
    existing = mesh.attributes.get(name)
    if existing is not None:
        mesh.attributes.remove(existing)
    attribute = mesh.attributes.new(name=name, type="INT", domain=domain)
    for index in range(expected_count):
        attribute.data[index].value = values[index] if index < len(values) else -1


def _read_triangle_metadata_raw_u16(
    metadata_data: Any,
    stats: ImportStats,
    *,
    field_name: str,
) -> tuple[int, int, int] | None:
    if not isinstance(metadata_data, dict):
        stats.add_warning(f"{field_name} is not an object.")
        return None
    raw_values = metadata_data.get("rawU16")
    if not isinstance(raw_values, list) or len(raw_values) != 3:
        stats.add_warning(f"{field_name}.rawU16 must contain exactly three values.")
        return None

    parsed: list[int] = []
    for value_index, raw_value in enumerate(raw_values):
        try:
            value = _read_int(raw_value, field_name=f"{field_name}.rawU16[{value_index}]")
        except ValueError as exc:
            stats.add_warning(str(exc))
            return None
        if value < 0 or value > 0xFFFF:
            stats.add_warning(f"{field_name}.rawU16[{value_index}]={value} is outside the u16 range.")
            return None
        parsed.append(value)
    return parsed[0], parsed[1], parsed[2]


def _triangle_metadata_profile_from_scene(scene: Any) -> tuple[str, list[TriangleMetadataFamily]]:
    raw_profile = str(getattr(scene, "spice_triangle_metadata_profile_json", "")).strip()
    if not raw_profile:
        return "Built-In", []
    return _parse_triangle_metadata_profile(json.loads(raw_profile))


def _read_mesh_face_int_attribute(mesh: Mesh, name: str) -> list[int] | None:
    attribute = mesh.attributes.get(name)
    if attribute is None or attribute.domain != "FACE" or len(attribute.data) != len(mesh.polygons):
        return None
    return [int(item.value) for item in attribute.data]


def _read_mesh_triangle_metadata(mesh: Mesh) -> list[tuple[int, int, int] | None] | None:
    raw_columns = [_read_mesh_face_int_attribute(mesh, name) for name in TRIANGLE_METADATA_RAW_ATTRIBUTE_NAMES]
    if any(column is None for column in raw_columns):
        return None
    columns = [column for column in raw_columns if column is not None]
    result: list[tuple[int, int, int] | None] = []
    for index in range(len(mesh.polygons)):
        values = (columns[0][index], columns[1][index], columns[2][index])
        if any(value < 0 or value > 0xFFFF for value in values):
            result.append(None)
        else:
            result.append(values)
    return result


def _write_triangle_metadata_attributes(
    mesh: Mesh,
    raw_values: list[tuple[int, int, int] | None],
    stats: ImportStats,
    *,
    mode: str,
    families: list[TriangleMetadataFamily],
) -> None:
    raw_columns = [
        [value[column] if value is not None else -1 for value in raw_values]
        for column in range(3)
    ]
    for name, values in zip(TRIANGLE_METADATA_RAW_ATTRIBUTE_NAMES, raw_columns):
        _write_mesh_int_attribute(mesh, name, "FACE", values, stats)

    decoded_values: list[TriangleMetadataDecoded | None] = [
        _decode_triangle_metadata(value) if value is not None else None
        for value in raw_values
    ]
    derived_attributes = (
        (TRIANGLE_METADATA_SELECTOR_ATTRIBUTE, [value.selector_low15 if value is not None else -1 for value in decoded_values]),
        (TRIANGLE_METADATA_WINDING_ATTRIBUTE, [value.stream_winding_high_bit if value is not None else -1 for value in decoded_values]),
        (TRIANGLE_METADATA_DECODED_ATTRIBUTE, [value.decoded_u16 if value is not None else -1 for value in decoded_values]),
        (TRIANGLE_METADATA_DECODED_HIGH_BIT_ATTRIBUTE, [value.decoded_high_bit if value is not None else -1 for value in decoded_values]),
        (TRIANGLE_METADATA_BEHAVIOR_CLASS_ATTRIBUTE, [value.behavior_class if value is not None else -1 for value in decoded_values]),
        (TRIANGLE_METADATA_ENCOUNTER_SELECTOR_ATTRIBUTE, [value.encounter_selector if value is not None else -1 for value in decoded_values]),
        (TRIANGLE_METADATA_PAYLOAD_GROUP_ATTRIBUTE, [value.payload_group if value is not None else -1 for value in decoded_values]),
    )
    for name, values in derived_attributes:
        _write_mesh_int_attribute(mesh, name, "FACE", values, stats)
    for digit_index, name in enumerate(TRIANGLE_METADATA_DIGIT_ATTRIBUTE_NAMES):
        _write_mesh_int_attribute(
            mesh,
            name,
            "FACE",
            [value.digits[digit_index] if value is not None else -1 for value in decoded_values],
            stats,
        )

    existing_color = mesh.color_attributes.get(TRIANGLE_METADATA_COLOR_ATTRIBUTE)
    if existing_color is not None:
        mesh.color_attributes.remove(existing_color)
    color_layer = mesh.color_attributes.new(
        name=TRIANGLE_METADATA_COLOR_ATTRIBUTE,
        type="BYTE_COLOR",
        domain="CORNER",
    )
    for polygon_index, polygon in enumerate(mesh.polygons):
        decoded = decoded_values[polygon_index] if polygon_index < len(decoded_values) else None
        color = (1.0, 0.0, 1.0, 1.0)
        if decoded is not None:
            color, _family = _triangle_metadata_color(decoded, mode, families)
        for loop_index in polygon.loop_indices:
            color_layer.data[loop_index].color = color
    try:
        mesh.color_attributes.active_color = color_layer
    except (AttributeError, TypeError):
        pass
    try:
        mesh.color_attributes.active = color_layer
    except (AttributeError, TypeError):
        pass
    existing_resolved_color = mesh.color_attributes.get(
        TRIANGLE_METADATA_RESOLVED_ENCOUNTER_COLOR_ATTRIBUTE
    )
    if existing_resolved_color is not None:
        mesh.color_attributes.remove(existing_resolved_color)
    resolved_layer = mesh.color_attributes.new(
        name=TRIANGLE_METADATA_RESOLVED_ENCOUNTER_COLOR_ATTRIBUTE,
        type="BYTE_COLOR",
        domain="CORNER",
    )
    for polygon_index, polygon in enumerate(mesh.polygons):
        decoded = decoded_values[polygon_index] if polygon_index < len(decoded_values) else None
        resolved_color = (1.0, 0.0, 1.0, 1.0)
        if decoded is not None:
            table_id = _dungeon_encounter_table_id(decoded.raw_u16[2])
            resolved_color = (
                (1.0, 0.0, 1.0, 1.0)
                if table_id is None
                else _resolved_table_color(table_id)
            )
        for loop_index in polygon.loop_indices:
            resolved_layer.data[loop_index].color = resolved_color
    mesh["spice_triangle_metadata_face_count"] = len(raw_values)


def _triangle_metadata_meshes_for_scope(scene: Any) -> list[Mesh]:
    scope = str(getattr(scene, "spice_triangle_metadata_legend_scope", "ENTRY"))
    if scope == "ENTRY":
        active = getattr(getattr(bpy.context, "view_layer", None), "objects", None)
        active_object = getattr(active, "active", None)
        selected_entry = str(
            getattr(scene, "spice_triangle_metadata_entry_selector", "ACTIVE")
        )
        table_index: int | None = None
        if selected_entry != "ACTIVE":
            try:
                table_index = int(selected_entry)
            except ValueError:
                table_index = None
        else:
            cursor = active_object
            while cursor is not None:
                if "spice_ground_table_index" in cursor:
                    table_index = int(cursor["spice_ground_table_index"])
                    break
                cursor = getattr(cursor, "parent", None)
        if table_index is None:
            if active_object is None or not isinstance(getattr(active_object, "data", None), Mesh):
                return []
            mesh = active_object.data
            return [mesh] if _read_mesh_triangle_metadata(mesh) is not None else []

        meshes: list[Mesh] = []
        seen: set[int] = set()
        for obj in scene.objects:
            if int(obj.get("spice_ground_table_index", -1)) != table_index:
                continue
            mesh = getattr(obj, "data", None)
            if not isinstance(mesh, Mesh) or _read_mesh_triangle_metadata(mesh) is None:
                continue
            identity = mesh.as_pointer()
            if identity in seen:
                continue
            seen.add(identity)
            meshes.append(mesh)
        return meshes

    meshes: list[Mesh] = []
    seen: set[int] = set()
    for obj in scene.objects:
        mesh = getattr(obj, "data", None)
        if not isinstance(mesh, Mesh) or _read_mesh_triangle_metadata(mesh) is None:
            continue
        identity = mesh.as_pointer()
        if identity in seen:
            continue
        seen.add(identity)
        meshes.append(mesh)
    return meshes


def _refresh_triangle_metadata_legend(
    scene: Any,
    families: list[TriangleMetadataFamily],
    mode: str,
) -> None:
    legend = scene.spice_triangle_metadata_legend
    legend.clear()
    advanced_legend = getattr(scene, "spice_triangle_metadata_advanced_legend", None)
    if advanced_legend is not None:
        advanced_legend.clear()
    counts: dict[tuple[int, int, int], int] = {}
    malformed_count = 0
    scoped_meshes = _triangle_metadata_meshes_for_scope(scene)
    for mesh in scoped_meshes:
        metadata = _read_mesh_triangle_metadata(mesh)
        if metadata is None:
            continue
        for raw_u16 in metadata:
            if raw_u16 is None:
                malformed_count += 1
                continue
            counts[raw_u16] = counts.get(raw_u16, 0) + 1

    if malformed_count > 0:
        item = legend.add()
        item.name = "Malformed"
        item.show_face_count = False
        item.color = (1.0, 0.0, 1.0, 1.0)
        item.details = "one or more raw words are missing or outside the u16 range"
        if advanced_legend is not None:
            advanced_item = advanced_legend.add()
            advanced_item.name = "Malformed"
            advanced_item.face_count = malformed_count
            advanced_item.show_face_count = True
            advanced_item.color = (1.0, 0.0, 1.0, 1.0)
            advanced_item.details = "invalid raw triangle metadata"

    decoded_rows = [(_decode_triangle_metadata(raw_u16), raw_u16) for raw_u16 in counts]

    area99_context: EncounterLookupContext | None = None
    for mesh in scoped_meshes:
        for material in mesh.materials:
            if str(material.get("spice_encounter_context_kind", "")) != "AREA99":
                continue
            area99_context = _area99_context_from_text(
                str(material.get("spice_function_parameters_text", ""))
            )
            if area99_context is not None:
                break
        if area99_context is not None:
            break

    if mode == "SKY_RIFT_FORCE":
        force_classes = {
            decoded.behavior_class
            for decoded, _raw_u16 in decoded_rows
            if _triangle_metadata_force_info(decoded) is not None
        }
        if any(_triangle_metadata_force_info(decoded) is None for decoded, _raw_u16 in decoded_rows):
            item = legend.add()
            item.name = "No attributed sky-rift force"
            item.details = "neutral"
            item.show_face_count = False
            item.color = (0.35, 0.35, 0.35, 1.0)
        for behavior_class in sorted(force_classes):
            label, hue, saturation, value = TRIANGLE_METADATA_FORCE_CLASSES[behavior_class]
            red, green, blue = colorsys.hsv_to_rgb(hue / 360.0, saturation, value)
            item = legend.add()
            item.name = label
            item.details = f"decoded force class {behavior_class}"
            item.show_face_count = False
            item.color = (red, green, blue, 1.0)
    elif mode in {"ENCOUNTER_SELECTOR", "ENCOUNTER"}:
        for value in sorted({decoded.encounter_selector for decoded, _raw_u16 in decoded_rows}):
            color, family = _triangle_metadata_encounter_color(value)
            item = legend.add()
            item.name = family.label
            item.details = "authored tens digit; context-dependent lookup selector"
            item.show_face_count = False
            item.color = color
    elif mode in POSITION_AWARE_ENCOUNTER_MODES:
        if mode == "FORCE_RESOLVED_ENCOUNTER":
            force_classes = {
                decoded.behavior_class
                for decoded, _raw_u16 in decoded_rows
                if _triangle_metadata_force_info(decoded) is not None
            }
            for behavior_class in sorted(force_classes):
                label, hue, saturation, value = TRIANGLE_METADATA_FORCE_CLASSES[behavior_class]
                red, green, blue = colorsys.hsv_to_rgb(hue / 360.0, saturation, value)
                item = legend.add()
                item.name = label
                item.details = f"checker force layer; decoded class {behavior_class}"
                item.show_face_count = False
                item.color = (red, green, blue, 1.0)
        if area99_context is not None:
            page = 1 if str(getattr(scene, "spice_triangle_metadata_scenario", "PAGE0")) == "PAGE1" else 0
            packed_pages = _unpack_function_parameter_bytes(area99_context.function_parameters)
            page_slice = packed_pages[
                page * AREA99_LOOKUP_PAGE_SIZE:(page + 1) * AREA99_LOOKUP_PAGE_SIZE
            ]
            page_values = sorted({
                value
                for value in page_slice
                if value != 0 and value // 10 != 0 and value % 10 != 0
            })
            if any(value == 0 or value // 10 == 0 or value % 10 == 0 for value in page_slice):
                item = legend.add()
                item.name = "No resolved encounter"
                item.details = "lookup byte, zone, or table ID is 0"
                item.show_face_count = False
                item.color = RESOLVED_ENCOUNTER_NEUTRAL_COLOR
            legend_mode = "ZONE_TABLE" if mode == "FORCE_RESOLVED_ENCOUNTER" else mode
            identities: set[tuple[int, int]] = set()
            for packed_byte in page_values:
                zone = packed_byte // 10
                table_id = packed_byte % 10
                identity = (
                    zone if legend_mode != "ENCOUNTER_TABLE_ID" else 0,
                    table_id if legend_mode != "ENCOUNTER_ZONE" else 0,
                )
                if identity in identities:
                    continue
                identities.add(identity)
                item = legend.add()
                if legend_mode == "ENCOUNTER_ZONE":
                    item.name = f"Encounter zone {zone}"
                elif legend_mode == "ENCOUNTER_TABLE_ID":
                    item.name = f"Encounter table ID {table_id}"
                else:
                    item.name = f"Zone {zone}, table {table_id}"
                item.details = "position-resolved Area 99 lookup"
                item.show_face_count = False
                item.color = _resolved_encounter_color(legend_mode, zone, table_id)
        else:
            table_ids = sorted(
                {_dungeon_encounter_table_id(raw_u16[2]) for raw_u16 in counts},
                key=lambda value: 99 if value is None else value,
            )
            for table_id in table_ids:
                item = legend.add()
                if table_id is None:
                    item.name = "Unsupported dungeon selector"
                    item.details = "authored tens digit above 7"
                    item.color = (1.0, 0.0, 1.0, 1.0)
                elif mode == "ENCOUNTER_ZONE":
                    item.name = "Encounter zone unavailable"
                    item.details = "dungeon metadata resolves only a table ID"
                    item.color = (0.35, 0.35, 0.35, 1.0)
                else:
                    item.name = "No encounter" if table_id == 0 else f"Encounter table ID {table_id}"
                    item.details = "direct dungeon authored tens digit"
                    item.color = _resolved_table_color(table_id)
                item.show_face_count = False
    elif mode in {"UNATTRIBUTED", "PAYLOAD"}:
        unattributed_rows: dict[tuple[int, int, int], TriangleMetadataDecoded] = {}
        for decoded, _raw_u16 in decoded_rows:
            unattributed_rows.setdefault(_triangle_metadata_unattributed_identity(decoded), decoded)
        for identity, decoded in sorted(unattributed_rows.items()):
            color, family = _triangle_metadata_unattributed_color(decoded)
            item = legend.add()
            item.name = family.label
            item.details = (
                "neutral" if identity == (0, 0, 0)
                else f"unattributed key payload/high-bit/class={identity}"
            )
            item.show_face_count = False
            item.color = color
    elif mode == "WINDING":
        for value, label in ((0, "Original stream order"), (1, "Reversed stream winding")):
            item = legend.add()
            item.name = label
            item.details = f"word 2 high bit {value}"
            item.show_face_count = False
            item.color = (0.12, 0.48, 0.9, 1.0) if value == 0 else (1.0, 0.28, 0.05, 1.0)
    elif mode in {"RAW0", "RAW1", "RAW2"}:
        word_index = int(mode[-1])
        for value in sorted({raw_u16[word_index] for _decoded, raw_u16 in decoded_rows}):
            item = legend.add()
            item.name = f"Raw word {word_index}"
            item.details = f"{value} (0x{value:04X})"
            item.show_face_count = False
            item.color = _metadata_categorical_color(value)
    if advanced_legend is not None:
        for raw_u16, face_count in sorted(counts.items()):
            decoded = _decode_triangle_metadata(raw_u16)
            color, _family = _triangle_metadata_color(decoded, "RAW2", families)
            item = advanced_legend.add()
            item.name = f"Raw triplet {raw_u16}"
            item.face_count = face_count
            item.show_face_count = True
            item.color = color
            item.details = (
                f"raw2={decoded.selector_low15} (0x{decoded.selector_low15:04X}) "
                f"decoded=0x{decoded.decoded_u16:04X} class={decoded.behavior_class} "
                f"encounter={decoded.encounter_selector} payload={decoded.payload_group} "
                f"triplet=({raw_u16[0]},{raw_u16[1]},{raw_u16[2]})"
            )


def _refresh_triangle_metadata_visualization(scene: Any, stats: ImportStats | None = None) -> None:
    local_stats = stats if stats is not None else ImportStats()
    try:
        profile_name, families = _triangle_metadata_profile_from_scene(scene)
    except (ValueError, json.JSONDecodeError) as exc:
        local_stats.add_warning(f"Triangle metadata profile is invalid: {exc}")
        profile_name, families = "Built-In", []
    mode = str(getattr(scene, "spice_triangle_metadata_display_mode", "SKY_RIFT_FORCE"))
    resolved_mode = mode in POSITION_AWARE_ENCOUNTER_MODES
    opacity = float(getattr(
        scene,
        "spice_triangle_metadata_resolved_opacity" if resolved_mode else "spice_triangle_metadata_opacity",
        0.45 if resolved_mode else 0.25,
    ))
    scenario_page = 1 if str(getattr(scene, "spice_triangle_metadata_scenario", "PAGE0")) == "PAGE1" else 0
    altitude_source = str(
        getattr(scene, "spice_triangle_metadata_altitude_source", "SURFACE")
    )

    for mesh in bpy.data.meshes:
        raw_values = _read_mesh_triangle_metadata(mesh)
        if raw_values is None:
            continue
        _write_triangle_metadata_attributes(
            mesh,
            list(raw_values),
            local_stats,
            mode=mode,
            families=families,
        )

    for material in bpy.data.materials:
        if not bool(material.get("spice_triangle_metadata_material", False)):
            continue
        material.use_nodes = True
        bsdf, _output = _reset_material_nodes(material)
        context_kind = str(material.get("spice_encounter_context_kind", "DUNGEON"))
        if mode in POSITION_AWARE_ENCOUNTER_MODES and context_kind == "AREA99":
            text_name = str(material.get("spice_function_parameters_text", ""))
            context = _area99_context_from_text(text_name)
            if context is None:
                local_stats.add_warning(
                    f"Material {material.name} has no valid persisted Area 99 lookup; "
                    "using face-attribute colors."
                )
                _configure_triangle_metadata_material(material, bsdf, opacity, unlit=True)
            else:
                image = _update_area99_lookup_image(
                    str(material.get("spice_encounter_context_id", "Area99")),
                    context,
                    page=scenario_page,
                    mode=mode,
                )
                _configure_area99_encounter_material(
                    material,
                    bsdf,
                    opacity,
                    image,
                    altitude_source=altitude_source,
                    combined_force=mode == "FORCE_RESOLVED_ENCOUNTER",
                )
        elif mode == "FORCE_RESOLVED_ENCOUNTER" and context_kind == "DUNGEON":
            _configure_dungeon_force_encounter_material(material, bsdf, opacity)
        else:
            _configure_triangle_metadata_material(
                material,
                bsdf,
                opacity,
                unlit=resolved_mode,
            )

    scene.spice_triangle_metadata_profile_name = profile_name
    _refresh_triangle_metadata_legend(scene, families, mode)


def _ensure_collection(name: str, parent: Collection | None = None) -> Collection:
    collection = bpy.data.collections.get(name)
    if collection is None:
        collection = bpy.data.collections.new(name)
        if parent is None:
            bpy.context.scene.collection.children.link(collection)
        else:
            parent.children.link(collection)
    return collection


def _link_object_to_collection(collection: Collection, obj: Object) -> None:
    if collection.objects.get(obj.name) is None:
        collection.objects.link(obj)


def _clear_collection(collection: Collection) -> None:
    for obj in list(collection.objects):
        bpy.data.objects.remove(obj, do_unlink=True)


def _parse_json(path: str) -> dict[str, Any]:
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)

    for required_key in ("meshes", "indexEntries", "textures"):
        if required_key not in payload:
            raise ValueError(f"Missing required top-level key: {required_key}")
    if "objectTrees" not in payload:
        payload["objectTrees"] = []
    if "animations" not in payload:
        payload["animations"] = []

    return payload


def _flip_rgba8_vertically(raw: bytes, width: int, height: int) -> bytes:
    flipped = bytearray(len(raw))
    row_stride = width * 4
    for y in range(height):
        src = y * row_stride
        dst = (height - 1 - y) * row_stride
        flipped[dst : dst + row_stride] = raw[src : src + row_stride]
    return bytes(flipped)


def _decode_rgba8_image(texture: dict[str, Any], image_name: str) -> Image | None:
    width = int(texture.get("width", 0))
    height = int(texture.get("height", 0))
    pixel_format = str(texture.get("pixelFormat", "")).lower()
    encoded_pixels = texture.get("pixelDataBase64", "")

    if width <= 0 or height <= 0:
        return None
    if pixel_format != "rgba8":
        return None
    if not encoded_pixels:
        return None

    raw = base64.b64decode(encoded_pixels)
    expected_size = width * height * 4
    if len(raw) != expected_size:
        return None
    has_transparency = any(raw[i] != 255 for i in range(3, len(raw), 4))

    image = bpy.data.images.get(image_name)
    if image is None:
        image = bpy.data.images.new(name=image_name, width=width, height=height, alpha=True)
    else:
        image.scale(width, height)

    raw = _flip_rgba8_vertically(raw, width, height)
    float_pixels = [channel / 255.0 for channel in raw]
    image.pixels = float_pixels
    image.alpha_mode = "STRAIGHT"
    image.update()
    image.pack()
    image["spice_has_transparency"] = has_transparency

    return image


def _read_optional_texture_id(value: Any) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def _build_texture_lookup(textures: list[dict[str, Any]], stats: ImportStats) -> TextureLookup:
    images_by_name: dict[str, Image] = {}
    images_by_id: dict[int, Image] = {}

    for index, texture in enumerate(textures):
        texture_id = _read_optional_texture_id(texture.get("textureId"))
        texture_name = str(texture.get("textureName", "")).strip()
        if not texture_name:
            texture_name = f"texture_{texture_id}" if texture_id is not None else f"texture_{index:04d}"
        image = _decode_rgba8_image(texture, image_name=texture_name)
        if image is None:
            stats.add_warning(
                f"Texture {texture_name} was not decoded as RGBA8 and will not be displayable."
            )
            continue

        image["spice_texture_name"] = texture_name
        if texture_id is not None:
            _set_custom_int_property(
                image,
                "spice_texture_id",
                texture_id,
                stats,
                field_name=f"textures[{index}].textureId",
            )
        _set_custom_int_property(
            image,
            "spice_source_offset",
            texture.get("sourceOffset", 0),
            stats,
            field_name=f"textures[{index}].sourceOffset",
        )
        _set_custom_int_property(
            image,
            "spice_source_size",
            texture.get("sourceSize", 0),
            stats,
            field_name=f"textures[{index}].sourceSize",
        )
        image["spice_encoded_format"] = str(texture.get("encodedFormat", ""))
        images_by_name[texture_name] = image
        if texture_id is not None:
            images_by_id[texture_id] = image
        stats.texture_count += 1

    return TextureLookup(by_name=images_by_name, by_id=images_by_id)


def _resolve_material_texture(
    material_data: dict[str, Any],
    texture_lookup: TextureLookup,
) -> Image | None:
    texture_name = str(material_data.get("textureName", "")).strip()
    if texture_name and texture_name in texture_lookup.by_name:
        return texture_lookup.by_name[texture_name]

    texture_id = _read_optional_texture_id(material_data.get("textureId"))
    if texture_id is not None and texture_id in texture_lookup.by_id:
        return texture_lookup.by_id[texture_id]

    return None


def _material_cache_name(material_data: dict[str, Any], *, triangle_metadata: bool = False) -> str:
    material_hash = int(material_data.get("materialHash", 0))
    texture_id = _read_optional_texture_id(material_data.get("textureId"))
    texture_name = str(material_data.get("textureName", "")).strip()
    if texture_name:
        safe_texture_name = "".join(
            ch if ch.isalnum() or ch in ("_", "-") else "_"
            for ch in texture_name
        )
        name = f"SoaMat_{material_hash:016x}_{safe_texture_name}"
        return f"{name}_TriangleMetadata" if triangle_metadata else name
    if texture_id is not None:
        name = f"SoaMat_{material_hash:016x}_tex_{texture_id}"
        return f"{name}_TriangleMetadata" if triangle_metadata else name
    name = f"SoaMat_{material_hash:016x}"
    return f"{name}_TriangleMetadata" if triangle_metadata else name


def _safe_name_part(value: str, fallback: str) -> str:
    safe_value = "".join(
        ch if ch.isalnum() or ch in ("_", "-") else "_"
        for ch in value.strip()
    ).strip("_")
    return safe_value or fallback


def _hex_name_part(value: Any) -> str:
    try:
        numeric = int(value)
    except (TypeError, ValueError):
        numeric = 0
    return f"0x{numeric:X}"


def _entry_root_name(entry_index: int, entry_id: int, fxn_name: str) -> str:
    entry_number = entry_id if entry_id >= 0 else entry_index
    safe_fxn_name = _safe_name_part(fxn_name, "entry")
    return f"{entry_number:03d}_{safe_fxn_name}"


def _mesh_object_name(mesh_data: dict[str, Any]) -> str:
    return (
        f"obj_{_hex_name_part(mesh_data.get('sourceObjectAddress', 0))}"
        f"_attach_{_hex_name_part(mesh_data.get('sourceAttachOffset', 0))}"
    )


def _node_object_name(entry_name: str, node_idx: int, tree: dict[str, Any]) -> str:
    base_name = f"{entry_name}_Node_{node_idx}"
    if node_idx != 0:
        return base_name
    return f"{base_name}_obj_{_hex_name_part(tree.get('sourceObjectAddress', 0))}"


def _attach_object_name(entry_name: str, node_idx: int, attach_offset: Any) -> str:
    return f"{entry_name}_Node_{node_idx}_attach_{_hex_name_part(attach_offset)}"


def _mesh_weighted_binding(mesh_data: dict[str, Any]) -> dict[str, Any] | None:
    binding = mesh_data.get("weightedBinding")
    return binding if isinstance(binding, dict) else None


def _weighted_root_node_index(mesh_data: dict[str, Any], fallback_node_index: int) -> int:
    binding = _mesh_weighted_binding(mesh_data)
    if binding is None:
        return fallback_node_index
    try:
        return int(binding.get("rootNodeIndex", fallback_node_index))
    except (TypeError, ValueError):
        return fallback_node_index


def _configure_material_alpha(
    material: Material,
    uses_alpha: bool,
    *,
    prefer_blended: bool = False,
) -> None:
    if hasattr(material, "surface_render_method"):
        if uses_alpha:
            preferred_method = "BLENDED" if prefer_blended else "DITHERED"
            fallback_method = "DITHERED" if prefer_blended else "BLENDED"
            try:
                enum_items = material.bl_rna.properties["surface_render_method"].enum_items
                enum_ids = {item.identifier for item in enum_items}
                if preferred_method in enum_ids:
                    material.surface_render_method = preferred_method
                elif fallback_method in enum_ids:
                    material.surface_render_method = fallback_method
            except (AttributeError, KeyError, TypeError):
                material.surface_render_method = preferred_method
        else:
            try:
                enum_items = material.bl_rna.properties["surface_render_method"].enum_items
                if any(item.identifier == "OPAQUE" for item in enum_items):
                    material.surface_render_method = "OPAQUE"
            except (AttributeError, KeyError, TypeError):
                pass

    if hasattr(material, "blend_method"):
        material.blend_method = ("BLEND" if prefer_blended else "HASHED") if uses_alpha else "OPAQUE"

    if hasattr(material, "shadow_method"):
        material.shadow_method = ("BLEND" if prefer_blended else "HASHED") if uses_alpha else "OPAQUE"


def _set_principled_alpha(bsdf: Any, alpha: float) -> None:
    alpha_input = bsdf.inputs.get("Alpha") if hasattr(bsdf.inputs, "get") else None
    if alpha_input is not None:
        alpha_input.default_value = alpha


def _set_first_existing_input_default(bsdf: Any, names: tuple[str, ...], value: float) -> None:
    if not hasattr(bsdf.inputs, "get"):
        return
    for name in names:
        socket = bsdf.inputs.get(name)
        if socket is not None:
            socket.default_value = value
            return


def _configure_principled_surface(bsdf: Any, material_data: dict[str, Any]) -> None:
    no_specular = _read_bool(material_data.get("noSpecular"), False)

    _set_first_existing_input_default(bsdf, ("Metallic",), 0.0)
    _set_first_existing_input_default(bsdf, ("Roughness",), 1.0)
    _set_first_existing_input_default(
        bsdf,
        ("Specular IOR Level", "Specular", "Specular Tint"),
        0.0 if no_specular else 0.2,
    )


def _set_material_alpha_value(material: Material, bsdf: Any, alpha: float) -> None:
    diffuse = list(material.diffuse_color)
    if len(diffuse) >= 4:
        diffuse[3] = alpha
        material.diffuse_color = diffuse
    _set_principled_alpha(bsdf, alpha)


def _configure_texture_extension(image_node: Any) -> None:
    image_node.extension = "REPEAT"


def _read_bool(value: Any, default: bool = False) -> bool:
    if isinstance(value, bool):
        return value
    if value is None:
        return default
    if isinstance(value, (int, float)):
        return bool(value)
    if isinstance(value, str):
        return value.strip().lower() in ("1", "true", "yes", "on")
    return default


def _ensure_link(links: Any, from_socket: Any, to_socket: Any) -> None:
    for link in links:
        if link.from_socket == from_socket and link.to_socket == to_socket:
            return
    links.new(from_socket, to_socket)


def _clear_input_links(links: Any, input_socket: Any) -> None:
    for link in list(input_socket.links):
        links.remove(link)


def _named_math_node(nodes: Any, name: str, operation: str) -> Any:
    node = nodes.get(name)
    if node is None:
        node = nodes.new(type="ShaderNodeMath")
        node.name = name
        node.label = name
    node.operation = operation
    node.use_clamp = False
    return node


def _build_sampler_axis(
    nodes: Any,
    links: Any,
    source_socket: Any,
    *,
    axis_name: str,
    clamp_axis: bool,
    mirror_axis: bool,
) -> Any:
    if clamp_axis:
        max_node = _named_math_node(nodes, f"Soa{axis_name}ClampMin", "MAXIMUM")
        max_node.inputs[1].default_value = -1.0
        min_node = _named_math_node(nodes, f"Soa{axis_name}ClampMax", "MINIMUM")
        min_node.inputs[1].default_value = 1.0
        _clear_input_links(links, max_node.inputs[0])
        _clear_input_links(links, min_node.inputs[0])
        _ensure_link(links, source_socket, max_node.inputs[0])
        _ensure_link(links, max_node.outputs[0], min_node.inputs[0])
        return min_node.outputs[0]

    if mirror_axis:
        mod_node = _named_math_node(nodes, f"Soa{axis_name}MirrorModulo", "MODULO")
        mod_node.inputs[1].default_value = 2.0
        sub_node = _named_math_node(nodes, f"Soa{axis_name}MirrorCenter", "SUBTRACT")
        sub_node.inputs[1].default_value = 1.0
        abs_node = _named_math_node(nodes, f"Soa{axis_name}MirrorAbs", "ABSOLUTE")
        inv_node = _named_math_node(nodes, f"Soa{axis_name}MirrorInvert", "SUBTRACT")
        inv_node.inputs[0].default_value = 1.0

        _clear_input_links(links, mod_node.inputs[0])
        _clear_input_links(links, sub_node.inputs[0])
        _clear_input_links(links, abs_node.inputs[0])
        _clear_input_links(links, inv_node.inputs[1])

        _ensure_link(links, source_socket, mod_node.inputs[0])
        _ensure_link(links, mod_node.outputs[0], sub_node.inputs[0])
        _ensure_link(links, sub_node.outputs[0], abs_node.inputs[0])
        _ensure_link(links, abs_node.outputs[0], inv_node.inputs[1])
        return inv_node.outputs[0]

    return source_socket


def _configure_texture_sampler(
    nodes: Any,
    links: Any,
    image_node: Any,
    *,
    clamp_u: bool,
    clamp_v: bool,
    mirror_u: bool,
    mirror_v: bool,
) -> None:
    tex_coord = nodes.get("SoaTexCoord")
    if tex_coord is None:
        tex_coord = nodes.new(type="ShaderNodeTexCoord")
        tex_coord.name = "SoaTexCoord"

    separate = nodes.get("SoaUVSeparate")
    if separate is None:
        separate = nodes.new(type="ShaderNodeSeparateXYZ")
        separate.name = "SoaUVSeparate"

    combine = nodes.get("SoaUVCombine")
    if combine is None:
        combine = nodes.new(type="ShaderNodeCombineXYZ")
        combine.name = "SoaUVCombine"
    combine.inputs["Z"].default_value = 0.0

    _clear_input_links(links, separate.inputs["Vector"])
    _ensure_link(links, tex_coord.outputs["UV"], separate.inputs["Vector"])

    u_socket = _build_sampler_axis(
        nodes,
        links,
        separate.outputs["X"],
        axis_name="U",
        clamp_axis=clamp_u,
        mirror_axis=mirror_u,
    )
    v_socket = _build_sampler_axis(
        nodes,
        links,
        separate.outputs["Y"],
        axis_name="V",
        clamp_axis=clamp_v,
        mirror_axis=mirror_v,
    )

    _clear_input_links(links, combine.inputs["X"])
    _clear_input_links(links, combine.inputs["Y"])
    _clear_input_links(links, image_node.inputs["Vector"])
    _ensure_link(links, u_socket, combine.inputs["X"])
    _ensure_link(links, v_socket, combine.inputs["Y"])
    _ensure_link(links, combine.outputs["Vector"], image_node.inputs["Vector"])


def _enable_custom_normals_shading(mesh: Mesh) -> None:
    if hasattr(mesh, "use_auto_smooth"):
        mesh.use_auto_smooth = True
        return

    if len(mesh.polygons) > 0:
        mesh.polygons.foreach_set("use_smooth", [True] * len(mesh.polygons))


def _reset_material_nodes(material: Material) -> tuple[Any, Any]:
    node_tree = material.node_tree
    assert node_tree is not None
    node_tree.links.clear()
    node_tree.nodes.clear()

    bsdf = node_tree.nodes.new(type="ShaderNodeBsdfPrincipled")
    bsdf.name = "Principled BSDF"
    output = node_tree.nodes.new(type="ShaderNodeOutputMaterial")
    output.name = "Material Output"
    node_tree.links.new(bsdf.outputs["BSDF"], output.inputs["Surface"])
    return bsdf, output


def _configure_unlit_color_output(
    material: Material,
    bsdf: Any,
    color_socket: Any,
    opacity: float,
) -> None:
    node_tree = material.node_tree
    assert node_tree is not None
    nodes = node_tree.nodes
    links = node_tree.links
    output = nodes.get("Material Output")
    if output is None:
        output = nodes.new(type="ShaderNodeOutputMaterial")
        output.name = "Material Output"
    _clear_input_links(links, output.inputs["Surface"])
    nodes.remove(bsdf)

    transparent = nodes.new(type="ShaderNodeBsdfTransparent")
    transparent.name = "SpiceResolvedTransparent"
    emission = nodes.new(type="ShaderNodeEmission")
    emission.name = "SpiceResolvedEmission"
    emission.inputs["Strength"].default_value = 1.0
    links.new(color_socket, emission.inputs["Color"])
    mix = nodes.new(type="ShaderNodeMixShader")
    mix.name = "SpiceResolvedOpacity"
    mix.inputs[0].default_value = opacity
    links.new(transparent.outputs[0], mix.inputs[1])
    links.new(emission.outputs[0], mix.inputs[2])
    links.new(mix.outputs[0], output.inputs["Surface"])

    diffuse = list(material.diffuse_color)
    if len(diffuse) >= 4:
        diffuse[3] = opacity
        material.diffuse_color = diffuse
    _configure_material_alpha(material, True, prefer_blended=True)


def _configure_triangle_metadata_material(
    material: Material,
    bsdf: Any,
    opacity: float,
    *,
    unlit: bool = False,
) -> None:
    node_tree = material.node_tree
    assert node_tree is not None
    try:
        color_node = node_tree.nodes.new(type="ShaderNodeVertexColor")
        color_node.layer_name = TRIANGLE_METADATA_COLOR_ATTRIBUTE
    except RuntimeError:
        color_node = node_tree.nodes.new(type="ShaderNodeAttribute")
        color_node.attribute_name = TRIANGLE_METADATA_COLOR_ATTRIBUTE
    color_node.name = "SpiceTriangleMetadataColor"
    color_node.label = "Triangle Metadata Color"
    if unlit:
        _configure_unlit_color_output(material, bsdf, color_node.outputs["Color"], opacity)
    else:
        _clear_input_links(node_tree.links, bsdf.inputs["Base Color"])
        _ensure_link(node_tree.links, color_node.outputs["Color"], bsdf.inputs["Base Color"])
        _clear_input_links(node_tree.links, bsdf.inputs["Alpha"])
        _set_material_alpha_value(material, bsdf, opacity)
        _configure_material_alpha(material, True, prefer_blended=True)
    material["spice_triangle_metadata_material"] = True


def _persist_function_parameters_text(
    target_collection_name: str,
    index_entries: list[dict[str, Any]],
) -> str:
    text_name = f"{target_collection_name}_MldFunctionParameters"
    text_block = bpy.data.texts.get(text_name)
    if text_block is None:
        text_block = bpy.data.texts.new(text_name)
    else:
        text_block.clear()
    text_block.write(json.dumps(_function_parameter_text_payload(index_entries), separators=(",", ":")))
    return text_name


def _area99_context_from_text(text_name: str) -> EncounterLookupContext | None:
    text_block = bpy.data.texts.get(text_name)
    if text_block is None:
        return None
    try:
        payload = json.loads(text_block.as_string())
    except (AttributeError, json.JSONDecodeError):
        return None
    entries = payload.get("indexEntries", []) if isinstance(payload, dict) else []
    if not isinstance(entries, list):
        return None
    return _find_area99_lookup_context(entries)


def _update_area99_lookup_image(
    context_id: str,
    context: EncounterLookupContext,
    *,
    page: int,
    mode: str,
) -> Image:
    image_name = f"SpiceEncounterLookup_{context_id}"

    def create_image() -> Image:
        return bpy.data.images.new(
            image_name,
            width=AREA99_LOOKUP_WIDTH,
            height=AREA99_LOOKUP_HEIGHT,
            alpha=True,
            float_buffer=False,
        )

    image = bpy.data.images.get(image_name)
    if image is None:
        image = create_image()
    elif tuple(image.size) != (AREA99_LOOKUP_WIDTH, AREA99_LOOKUP_HEIGHT):
        try:
            image.scale(AREA99_LOOKUP_WIDTH, AREA99_LOOKUP_HEIGHT)
        except RuntimeError:
            bpy.data.images.remove(image)
            image = create_image()
    pixels = _area99_lookup_image_pixels(
        context.function_parameters,
        page=page,
        mode=mode,
    )

    def upload_pixels(target: Image) -> None:
        try:
            target.colorspace_settings.name = "Non-Color"
        except (AttributeError, TypeError):
            pass
        target.pixels.foreach_set(pixels)
        target.update()

    try:
        upload_pixels(image)
    except RuntimeError:
        bpy.data.images.remove(image)
        image = create_image()
        upload_pixels(image)
    try:
        image.pack()
    except RuntimeError:
        pass
    image["spice_encounter_page"] = page
    image["spice_encounter_mode"] = mode
    return image


def _new_math_node(nodes: Any, name: str, operation: str) -> Any:
    node = nodes.new(type="ShaderNodeMath")
    node.name = name
    node.operation = operation
    return node


def _configure_area99_encounter_material(
    material: Material,
    bsdf: Any,
    opacity: float,
    image: Image,
    *,
    altitude_source: str,
    combined_force: bool,
) -> None:
    node_tree = material.node_tree
    assert node_tree is not None
    nodes = node_tree.nodes
    links = node_tree.links

    geometry = nodes.new(type="ShaderNodeNewGeometry")
    geometry.name = "SpiceEncounterWorldPosition"
    separate = nodes.new(type="ShaderNodeSeparateXYZ")
    separate.name = "SpiceEncounterPositionAxes"
    links.new(geometry.outputs["Position"], separate.inputs["Vector"])

    x_numerator = _new_math_node(nodes, "SpiceEncounterXNumerator", "SUBTRACT")
    x_numerator.inputs[0].default_value = 8400.0
    links.new(separate.outputs["X"], x_numerator.inputs[1])
    x_divide = _new_math_node(nodes, "SpiceEncounterXDivide", "DIVIDE")
    x_divide.inputs[1].default_value = 2400.0
    links.new(x_numerator.outputs[0], x_divide.inputs[0])
    x_bucket = _new_math_node(nodes, "SpiceEncounterXBucket", "TRUNC")
    links.new(x_divide.outputs[0], x_bucket.inputs[0])

    z_numerator = _new_math_node(nodes, "SpiceEncounterZNumerator", "ADD")
    z_numerator.inputs[0].default_value = 7200.0
    links.new(separate.outputs["Y"], z_numerator.inputs[1])
    z_divide = _new_math_node(nodes, "SpiceEncounterZDivide", "DIVIDE")
    z_divide.inputs[1].default_value = 2400.0
    links.new(z_numerator.outputs[0], z_divide.inputs[0])
    z_bucket = _new_math_node(nodes, "SpiceEncounterZBucket", "TRUNC")
    links.new(z_divide.outputs[0], z_bucket.inputs[0])

    lane_attribute = nodes.new(type="ShaderNodeAttribute")
    lane_attribute.name = "SpiceEncounterLane"
    lane_attribute.attribute_name = TRIANGLE_METADATA_DIGIT_ATTRIBUTE_NAMES[1]
    lane_minus_one = _new_math_node(nodes, "SpiceEncounterLaneMinusOne", "SUBTRACT")
    lane_minus_one.inputs[1].default_value = 1.0
    links.new(lane_attribute.outputs["Fac"], lane_minus_one.inputs[0])
    x_times_lanes = _new_math_node(nodes, "SpiceEncounterXTimesLanes", "MULTIPLY")
    x_times_lanes.inputs[1].default_value = 8.0
    links.new(x_bucket.outputs[0], x_times_lanes.inputs[0])
    column = _new_math_node(nodes, "SpiceEncounterColumn", "ADD")
    links.new(x_times_lanes.outputs[0], column.inputs[0])
    links.new(lane_minus_one.outputs[0], column.inputs[1])

    if altitude_source == "SURFACE":
        low = _new_math_node(nodes, "SpiceEncounterLowAltitude", "LESS_THAN")
        low.inputs[1].default_value = -150.0
        links.new(separate.outputs["Z"], low.inputs[0])
        high = _new_math_node(nodes, "SpiceEncounterHighAltitude", "GREATER_THAN")
        high.inputs[1].default_value = 150.0
        links.new(separate.outputs["Z"], high.inputs[0])
        middle_or_high = _new_math_node(nodes, "SpiceEncounterMiddleOrHigh", "SUBTRACT")
        middle_or_high.inputs[0].default_value = 1.0
        links.new(low.outputs[0], middle_or_high.inputs[1])
        altitude_band = _new_math_node(nodes, "SpiceEncounterAltitudeBand", "ADD")
        links.new(middle_or_high.outputs[0], altitude_band.inputs[0])
        links.new(high.outputs[0], altitude_band.inputs[1])
        altitude_socket = altitude_band.outputs[0]
    else:
        forced_band = {"LOW": 0.0, "MIDDLE": 1.0, "HIGH": 2.0}.get(altitude_source, 1.0)
        altitude_value = nodes.new(type="ShaderNodeValue")
        altitude_value.name = "SpiceEncounterForcedAltitudeBand"
        altitude_value.outputs[0].default_value = forced_band
        altitude_socket = altitude_value.outputs[0]

    band_times_rows = _new_math_node(nodes, "SpiceEncounterBandTimesRows", "MULTIPLY")
    band_times_rows.inputs[1].default_value = 6.0
    links.new(altitude_socket, band_times_rows.inputs[0])
    row = _new_math_node(nodes, "SpiceEncounterRow", "ADD")
    links.new(band_times_rows.outputs[0], row.inputs[0])
    links.new(z_bucket.outputs[0], row.inputs[1])

    column_center = _new_math_node(nodes, "SpiceEncounterColumnCenter", "ADD")
    column_center.inputs[1].default_value = 0.5
    links.new(column.outputs[0], column_center.inputs[0])
    u = _new_math_node(nodes, "SpiceEncounterU", "DIVIDE")
    u.inputs[1].default_value = float(AREA99_LOOKUP_WIDTH)
    links.new(column_center.outputs[0], u.inputs[0])
    row_center = _new_math_node(nodes, "SpiceEncounterRowCenter", "ADD")
    row_center.inputs[1].default_value = 0.5
    links.new(row.outputs[0], row_center.inputs[0])
    v = _new_math_node(nodes, "SpiceEncounterV", "DIVIDE")
    v.inputs[1].default_value = float(AREA99_LOOKUP_HEIGHT)
    links.new(row_center.outputs[0], v.inputs[0])
    uv = nodes.new(type="ShaderNodeCombineXYZ")
    uv.name = "SpiceEncounterLookupUV"
    links.new(u.outputs[0], uv.inputs["X"])
    links.new(v.outputs[0], uv.inputs["Y"])

    image_node = nodes.new(type="ShaderNodeTexImage")
    image_node.name = "SpiceEncounterLookupImage"
    image_node.image = image
    image_node.interpolation = "Closest"
    image_node.extension = "CLIP"
    links.new(uv.outputs["Vector"], image_node.inputs["Vector"])

    validity_sockets: list[Any] = []
    for name, socket, minimum, maximum in (
        ("Lane", lane_attribute.outputs["Fac"], 0.5, 8.5),
        ("X", x_bucket.outputs[0], -0.5, 6.5),
        ("Z", z_bucket.outputs[0], -0.5, 5.5),
    ):
        above = _new_math_node(nodes, f"SpiceEncounter{name}AboveMin", "GREATER_THAN")
        above.inputs[1].default_value = minimum
        links.new(socket, above.inputs[0])
        below = _new_math_node(nodes, f"SpiceEncounter{name}BelowMax", "LESS_THAN")
        below.inputs[1].default_value = maximum
        links.new(socket, below.inputs[0])
        within = _new_math_node(nodes, f"SpiceEncounter{name}Valid", "MULTIPLY")
        links.new(above.outputs[0], within.inputs[0])
        links.new(below.outputs[0], within.inputs[1])
        validity_sockets.append(within.outputs[0])
    valid_xy = _new_math_node(nodes, "SpiceEncounterValidLaneX", "MULTIPLY")
    links.new(validity_sockets[0], valid_xy.inputs[0])
    links.new(validity_sockets[1], valid_xy.inputs[1])
    valid = _new_math_node(nodes, "SpiceEncounterValid", "MULTIPLY")
    links.new(valid_xy.outputs[0], valid.inputs[0])
    links.new(validity_sockets[2], valid.inputs[1])

    valid_mix = nodes.new(type="ShaderNodeMixRGB")
    valid_mix.name = "SpiceEncounterValidColor"
    valid_mix.blend_type = "MIX"
    valid_mix.inputs[1].default_value = (0.25, 0.25, 0.25, 1.0)
    links.new(valid.outputs[0], valid_mix.inputs[0])
    links.new(image_node.outputs["Color"], valid_mix.inputs[2])
    color_socket = valid_mix.outputs["Color"]

    if combined_force:
        force_attribute = nodes.new(type="ShaderNodeAttribute")
        force_attribute.name = "SpiceTriangleMetadataForceColor"
        force_attribute.attribute_name = TRIANGLE_METADATA_COLOR_ATTRIBUTE
        source_x_scale = _new_math_node(nodes, "SpiceEncounterCheckerX", "DIVIDE")
        source_x_scale.inputs[1].default_value = 300.0
        links.new(separate.outputs["X"], source_x_scale.inputs[0])
        source_z_scale = _new_math_node(nodes, "SpiceEncounterCheckerZ", "DIVIDE")
        source_z_scale.inputs[1].default_value = 300.0
        links.new(z_numerator.outputs[0], source_z_scale.inputs[0])
        x_floor = _new_math_node(nodes, "SpiceEncounterCheckerXFloor", "FLOOR")
        z_floor = _new_math_node(nodes, "SpiceEncounterCheckerZFloor", "FLOOR")
        links.new(source_x_scale.outputs[0], x_floor.inputs[0])
        links.new(source_z_scale.outputs[0], z_floor.inputs[0])
        checker_sum = _new_math_node(nodes, "SpiceEncounterCheckerSum", "ADD")
        links.new(x_floor.outputs[0], checker_sum.inputs[0])
        links.new(z_floor.outputs[0], checker_sum.inputs[1])
        checker = _new_math_node(nodes, "SpiceEncounterChecker", "MODULO")
        checker.inputs[1].default_value = 2.0
        links.new(checker_sum.outputs[0], checker.inputs[0])
        checker_mix = nodes.new(type="ShaderNodeMixRGB")
        checker_mix.name = "SpiceForceEncounterChecker"
        links.new(checker.outputs[0], checker_mix.inputs[0])
        links.new(color_socket, checker_mix.inputs[1])
        links.new(force_attribute.outputs["Color"], checker_mix.inputs[2])
        color_socket = checker_mix.outputs["Color"]

    _configure_unlit_color_output(material, bsdf, color_socket, opacity)
    material["spice_triangle_metadata_material"] = True


def _configure_dungeon_force_encounter_material(
    material: Material,
    bsdf: Any,
    opacity: float,
) -> None:
    node_tree = material.node_tree
    assert node_tree is not None
    nodes = node_tree.nodes
    links = node_tree.links
    geometry = nodes.new(type="ShaderNodeNewGeometry")
    separate = nodes.new(type="ShaderNodeSeparateXYZ")
    links.new(geometry.outputs["Position"], separate.inputs["Vector"])
    x_scale = _new_math_node(nodes, "SpiceDungeonCheckerX", "DIVIDE")
    x_scale.inputs[1].default_value = 300.0
    links.new(separate.outputs["X"], x_scale.inputs[0])
    y_scale = _new_math_node(nodes, "SpiceDungeonCheckerY", "DIVIDE")
    y_scale.inputs[1].default_value = 300.0
    links.new(separate.outputs["Y"], y_scale.inputs[0])
    x_floor = _new_math_node(nodes, "SpiceDungeonCheckerXFloor", "FLOOR")
    y_floor = _new_math_node(nodes, "SpiceDungeonCheckerYFloor", "FLOOR")
    links.new(x_scale.outputs[0], x_floor.inputs[0])
    links.new(y_scale.outputs[0], y_floor.inputs[0])
    checker_sum = _new_math_node(nodes, "SpiceDungeonCheckerSum", "ADD")
    links.new(x_floor.outputs[0], checker_sum.inputs[0])
    links.new(y_floor.outputs[0], checker_sum.inputs[1])
    checker = _new_math_node(nodes, "SpiceDungeonChecker", "MODULO")
    checker.inputs[1].default_value = 2.0
    links.new(checker_sum.outputs[0], checker.inputs[0])
    force_attribute = nodes.new(type="ShaderNodeAttribute")
    force_attribute.attribute_name = TRIANGLE_METADATA_COLOR_ATTRIBUTE
    encounter_attribute = nodes.new(type="ShaderNodeAttribute")
    encounter_attribute.attribute_name = TRIANGLE_METADATA_RESOLVED_ENCOUNTER_COLOR_ATTRIBUTE
    mix = nodes.new(type="ShaderNodeMixRGB")
    mix.name = "SpiceDungeonForceEncounterChecker"
    links.new(checker.outputs[0], mix.inputs[0])
    links.new(encounter_attribute.outputs["Color"], mix.inputs[1])
    links.new(force_attribute.outputs["Color"], mix.inputs[2])
    _configure_unlit_color_output(material, bsdf, mix.outputs["Color"], opacity)
    material["spice_triangle_metadata_material"] = True


def _build_material(
    material_data: dict[str, Any],
    texture_lookup: TextureLookup,
    stats: ImportStats,
    *,
    mesh_field_name: str,
    material_index: int,
    has_triangle_metadata: bool = False,
    triangle_metadata_opacity: float = 0.25,
    encounter_context_id: str = "",
    encounter_context_kind: str = "DUNGEON",
    function_parameters_text: str = "",
) -> Material:
    name = _material_cache_name(material_data, triangle_metadata=has_triangle_metadata)
    if has_triangle_metadata and encounter_context_id:
        name = f"{name}_{encounter_context_id}"
    material = bpy.data.materials.get(name)
    if material is None:
        material = bpy.data.materials.new(name=name)

    material.use_nodes = True
    bsdf, output = _reset_material_nodes(material)
    _set_custom_int_property(
        material,
        "spice_poly_type",
        material_data.get("polyType", 0),
        stats,
        field_name=f"{mesh_field_name}.materials[{material_index}].polyType",
    )
    _set_custom_int_property(
        material,
        "spice_chunk_flags",
        material_data.get("chunkFlags", 0),
        stats,
        field_name=f"{mesh_field_name}.materials[{material_index}].chunkFlags",
    )
    _set_custom_int_property(
        material,
        "spice_material_state_key",
        material_data.get("materialStateKey", 0),
        stats,
        field_name=f"{mesh_field_name}.materials[{material_index}].materialStateKey",
    )
    _set_custom_int_property(
        material,
        "spice_texture_id",
        material_data.get("textureId", 0),
        stats,
        field_name=f"{mesh_field_name}.materials[{material_index}].textureId",
    )
    material["spice_texture_name"] = str(material_data.get("textureName", ""))
    chunk_flags = int(material_data.get("chunkFlags", 0))
    use_texture = _read_bool(material_data.get("useTexture"), True)
    use_alpha = _read_bool(material_data.get("useAlpha"), False)
    no_alpha_test = _read_bool(material_data.get("noAlphaTest"), False)
    double_sided = _read_bool(material_data.get("doubleSided"), False)
    clamp_u = _read_bool(material_data.get("clampU"), (chunk_flags & 0x4) != 0)
    clamp_v = _read_bool(material_data.get("clampV"), (chunk_flags & 0x8) != 0)
    mirror_u = _read_bool(material_data.get("mirrorU"), (chunk_flags & 0x1) != 0)
    mirror_v = _read_bool(material_data.get("mirrorV"), (chunk_flags & 0x2) != 0)

    node_tree = material.node_tree
    assert node_tree is not None
    nodes = node_tree.nodes
    links = node_tree.links

    _configure_principled_surface(bsdf, material_data)

    material.use_backface_culling = not double_sided
    if hasattr(material, "use_backface_culling_shadow"):
        material.use_backface_culling_shadow = material.use_backface_culling

    texture_image = _resolve_material_texture(material_data, texture_lookup)
    if texture_image is not None:
        image_node = None
        for node in nodes:
            if node.type == "TEX_IMAGE" and node.name == "SoaTexture":
                image_node = node
                break
        if image_node is None:
            image_node = nodes.new(type="ShaderNodeTexImage")
            image_node.name = "SoaTexture"

        image_node.image = texture_image
        _configure_texture_extension(image_node)
        _configure_texture_sampler(
            nodes,
            links,
            image_node,
            clamp_u=clamp_u,
            clamp_v=clamp_v,
            mirror_u=mirror_u,
            mirror_v=mirror_v,
        )
        if use_texture:
            _ensure_link(links, image_node.outputs["Color"], bsdf.inputs["Base Color"])
            if use_alpha:
                _ensure_link(links, image_node.outputs["Alpha"], bsdf.inputs["Alpha"])
                diffuse = list(material.diffuse_color)
                if len(diffuse) >= 4:
                    diffuse[3] = 1.0
                    material.diffuse_color = diffuse
                _configure_material_alpha(material, True)
            else:
                _clear_input_links(links, bsdf.inputs["Alpha"])
                _set_material_alpha_value(material, bsdf, 1.0)
                _configure_material_alpha(material, False)
        else:
            _clear_input_links(links, bsdf.inputs["Base Color"])
            _clear_input_links(links, bsdf.inputs["Alpha"])
            _set_material_alpha_value(material, bsdf, 0.25)
            _configure_material_alpha(material, True, prefer_blended=True)
        material["spice_texture_bound"] = True
    else:
        _clear_input_links(links, bsdf.inputs["Base Color"])
        _clear_input_links(links, bsdf.inputs["Alpha"])
        _set_material_alpha_value(material, bsdf, 0.25)
        _configure_material_alpha(material, True, prefer_blended=True)
        texture_id = material_data.get("textureId", None)
        texture_name = str(material_data.get("textureName", "")).strip()
        if texture_id not in (None, 0xFFFF, "65535") or texture_name:
            stats.add_warning(
                (
                    f"Material {material.name} could not resolve texture "
                    f"id={texture_id!r} name={texture_name!r}."
                )
            )
        material["spice_texture_bound"] = False

    if has_triangle_metadata:
        _configure_triangle_metadata_material(material, bsdf, triangle_metadata_opacity)
        material["spice_encounter_context_id"] = encounter_context_id
        material["spice_encounter_context_kind"] = encounter_context_kind
        material["spice_function_parameters_text"] = function_parameters_text

    return material


def _read_corner_vertex_index(corner: Any) -> int:
    if isinstance(corner, dict):
        return int(corner.get("vertexIndex", 0))
    return int(corner)


def _triangles_from_corners(corners: list[Any]) -> list[tuple[int, int, int]]:
    triangles: list[tuple[int, int, int]] = []
    for i in range(0, len(corners) - 2, 3):
        a = _read_corner_vertex_index(corners[i])
        b = _read_corner_vertex_index(corners[i + 1])
        c = _read_corner_vertex_index(corners[i + 2])
        triangles.append((a, b, c))
    return triangles


def _triangle_has_area(
    vertices: list[tuple[float, float, float]],
    tri: tuple[int, int, int],
) -> bool:
    if tri[0] == tri[1] or tri[1] == tri[2] or tri[2] == tri[0]:
        return False
    if tri[0] < 0 or tri[1] < 0 or tri[2] < 0:
        return False
    if tri[0] >= len(vertices) or tri[1] >= len(vertices) or tri[2] >= len(vertices):
        return False

    a = mathutils.Vector(vertices[tri[0]])
    b = mathutils.Vector(vertices[tri[1]])
    c = mathutils.Vector(vertices[tri[2]])
    return (b - a).cross(c - a).length_squared > 1.0e-10


def _triangle_geometry_key(
    vertices: list[tuple[float, float, float]],
    tri: tuple[int, int, int],
) -> tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]:
    return tuple(
        sorted(
            (
                round(vertices[index][0], 6),
                round(vertices[index][1], 6),
                round(vertices[index][2], 6),
            )
            for index in tri
        )
    )


def _append_triangle_corner_attributes(
    triangle_corners: tuple[Any, Any, Any],
    uv_values: list[tuple[float, float] | None],
    color_values: list[tuple[float, float, float, float] | None],
) -> None:
    for corner in triangle_corners:
        if not isinstance(corner, dict):
            uv_values.append(None)
            color_values.append(None)
            continue

        if bool(corner.get("hasUv", False)):
            u = float(corner.get("u", 0.0))
            v = float(corner.get("v", 0.0))
            uv_values.append((u, 1.0 - v))
        else:
            uv_values.append(None)

        color = corner.get("color", [1.0, 1.0, 1.0, 1.0])
        if bool(corner.get("hasColor", False)) and len(color) >= 4:
            color_values.append(
                (float(color[0]), float(color[1]), float(color[2]), float(color[3]))
            )
        else:
            color_values.append(None)


def _build_mesh(
    mesh_data: dict[str, Any],
    texture_lookup: TextureLookup,
    stats: ImportStats,
    *,
    encounter_context_id: str = "",
    encounter_context_kind: str = "DUNGEON",
    function_parameters_text: str = "",
) -> Object:
    mesh_name = _mesh_object_name(mesh_data)
    mesh_field_name = f"meshes[{mesh_name}]"
    label = str(mesh_data.get("label", ""))
    is_grnd_mesh = label.startswith("GRND_")

    vertices_data = mesh_data.get("vertices", [])
    vertices = []
    vertex_user_attributes: list[int | None] = []
    for vertex in vertices_data:
        pos = vertex.get("position", [0.0, 0.0, 0.0])
        source_y = float(pos[1])
        if is_grnd_mesh:
            source_y += GRND_VISUAL_SOURCE_Y_OFFSET
        source_position = mathutils.Vector((float(pos[0]), source_y, float(pos[2])))
        blender_position = NJCM_TO_BLENDER_AXIS @ source_position
        vertices.append((blender_position.x, blender_position.y, blender_position.z))
        raw_user_attributes = vertex.get("rawUserAttributesU32")
        if raw_user_attributes is None:
            vertex_user_attributes.append(None)
        else:
            try:
                value = _read_int(raw_user_attributes, field_name="vertices[].rawUserAttributesU32")
            except ValueError as exc:
                stats.add_warning(str(exc))
                vertex_user_attributes.append(None)
                continue
            if value < 0 or value > 0xFFFFFFFF:
                stats.add_warning(
                    f"vertices[].rawUserAttributesU32={value} is outside the u32 range."
                )
                vertex_user_attributes.append(None)
            else:
                vertex_user_attributes.append(value)

    triangles: list[tuple[int, int, int]] = []
    poly_material_indices: list[int] = []
    poly_flat_flags: list[bool] = []
    uv_values: list[tuple[float, float] | None] = []
    color_values: list[tuple[float, float, float, float] | None] = []
    triangle_metadata_values: list[tuple[int, int, int] | None] = []
    has_triangle_metadata = False
    seen_triangle_geometry: set[
        tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]
    ] = set()
    for tri_set_index, tri_set in enumerate(mesh_data.get("triangleSets", [])):
        corners = tri_set.get("corners", [])
        triangle_metadata = tri_set.get("triangleMetadata", [])
        tri_set_has_triangle_metadata = isinstance(triangle_metadata, list) and len(triangle_metadata) > 0
        has_triangle_metadata = has_triangle_metadata or tri_set_has_triangle_metadata
        expected_metadata_count = len(corners) // 3
        if tri_set_has_triangle_metadata and len(triangle_metadata) != expected_metadata_count:
            stats.add_warning(
                f"{mesh_field_name}.triangleSets[{tri_set_index}].triangleMetadata has "
                f"{len(triangle_metadata)} entries for {expected_metadata_count} triangles."
            )
        material_index = int(tri_set.get("materialIndex", 0))
        material_data = {}
        if 0 <= material_index < len(mesh_data.get("materials", [])):
            material_data = mesh_data["materials"][material_index]
        flat_shading = bool(material_data.get("flatShading", False))
        for corner_index in range(0, len(corners) - 2, 3):
            triangle_index = corner_index // 3
            triangle_corners = (
                corners[corner_index],
                corners[corner_index + 1],
                corners[corner_index + 2],
            )
            tri = (
                _read_corner_vertex_index(triangle_corners[0]),
                _read_corner_vertex_index(triangle_corners[1]),
                _read_corner_vertex_index(triangle_corners[2]),
            )
            if not _triangle_has_area(vertices, tri):
                continue
            geometry_key = _triangle_geometry_key(vertices, tri)
            if not tri_set_has_triangle_metadata and geometry_key in seen_triangle_geometry:
                continue
            if not tri_set_has_triangle_metadata:
                seen_triangle_geometry.add(geometry_key)

            raw_metadata = None
            if tri_set_has_triangle_metadata and triangle_index < len(triangle_metadata):
                raw_metadata = _read_triangle_metadata_raw_u16(
                    triangle_metadata[triangle_index],
                    stats,
                    field_name=(
                        f"{mesh_field_name}.triangleSets[{tri_set_index}]"
                        f".triangleMetadata[{triangle_index}]"
                    ),
                )

            triangles.append(tri)
            poly_material_indices.append(material_index)
            poly_flat_flags.append(flat_shading)
            triangle_metadata_values.append(raw_metadata)
            _append_triangle_corner_attributes(triangle_corners, uv_values, color_values)

    mesh = bpy.data.meshes.new(mesh_name)
    mesh.from_pydata(vertices, [], triangles)
    if not has_triangle_metadata:
        mesh.validate(verbose=False)
    mesh.update()
    if has_triangle_metadata:
        mesh["spice_encounter_context_id"] = encounter_context_id
        mesh["spice_encounter_context_kind"] = encounter_context_kind
        mesh["spice_function_parameters_text"] = function_parameters_text

    material_slots: list[Material] = []
    for material_index, material_data in enumerate(mesh_data.get("materials", [])):
        material_slots.append(
            _build_material(
                material_data,
                texture_lookup,
                stats,
                mesh_field_name=mesh_field_name,
                material_index=material_index,
                has_triangle_metadata=has_triangle_metadata,
                triangle_metadata_opacity=float(
                    getattr(bpy.context.scene, "spice_triangle_metadata_opacity", 0.25)
                ),
                encounter_context_id=encounter_context_id,
                encounter_context_kind=encounter_context_kind,
                function_parameters_text=function_parameters_text,
            )
        )

    for material in material_slots:
        mesh.materials.append(material)

    for poly_index, poly in enumerate(mesh.polygons):
        if poly_index >= len(poly_material_indices):
            continue
        mat_index = poly_material_indices[poly_index]
        if 0 <= mat_index < len(mesh.materials):
            poly.material_index = mat_index
        else:
            stats.warnings += 1
        if poly_index < len(poly_flat_flags):
            poly.use_smooth = not poly_flat_flags[poly_index]

    if any(value is not None for value in uv_values) and len(uv_values) == len(mesh.loops):
        uv_layer = mesh.uv_layers.new(name="SoaUV")
        for loop_index, uv in enumerate(uv_values):
            if uv is not None:
                uv_layer.data[loop_index].uv = uv
        mesh.uv_layers.active = uv_layer

    if any(value is not None for value in color_values) and len(color_values) == len(mesh.loops):
        color_layer = mesh.color_attributes.new(
            name="SoaColor",
            type="BYTE_COLOR",
            domain="CORNER",
        )
        for loop_index, color in enumerate(color_values):
            if color is not None:
                color_layer.data[loop_index].color = color

    if has_triangle_metadata and triangle_metadata_values:
        try:
            _profile_name, families = _triangle_metadata_profile_from_scene(bpy.context.scene)
        except (ValueError, json.JSONDecodeError) as exc:
            stats.add_warning(f"Triangle metadata profile is invalid: {exc}")
            families = []
        _write_triangle_metadata_attributes(
            mesh,
            triangle_metadata_values,
            stats,
            mode=str(getattr(bpy.context.scene, "spice_triangle_metadata_display_mode", "SKY_RIFT_FORCE")),
            families=families,
        )

    if any(value is not None for value in vertex_user_attributes):
        _write_mesh_int_attribute(
            mesh,
            "spice_gobj_user_attr_low_u16",
            "POINT",
            [(value & 0xFFFF) if value is not None else -1 for value in vertex_user_attributes],
            stats,
        )
        _write_mesh_int_attribute(
            mesh,
            "spice_gobj_user_attr_high_u16",
            "POINT",
            [((value >> 16) & 0xFFFF) if value is not None else -1 for value in vertex_user_attributes],
            stats,
        )

    diagnostics = mesh_data.get("diagnostics", {})
    _set_custom_int_property(
        mesh,
        "spice_source_object_address",
        mesh_data.get("sourceObjectAddress", 0),
        stats,
        field_name=f"{mesh_field_name}.sourceObjectAddress",
    )
    mesh["spice_label"] = label
    if is_grnd_mesh:
        mesh["spice_visual_source_y_offset"] = GRND_VISUAL_SOURCE_Y_OFFSET
    _set_custom_int_property(
        mesh,
        "spice_source_chunk_offset",
        mesh_data.get("sourceChunkOffset", 0),
        stats,
        field_name=f"{mesh_field_name}.sourceChunkOffset",
    )
    _set_custom_int_property(
        mesh,
        "spice_source_attach_offset",
        mesh_data.get("sourceAttachOffset", 0),
        stats,
        field_name=f"{mesh_field_name}.sourceAttachOffset",
    )
    binding = _mesh_weighted_binding(mesh_data)
    if binding is not None:
        _set_custom_int_property(
            mesh,
            "spice_weighted_root_node_index",
            binding.get("rootNodeIndex", 0),
            stats,
            field_name=f"{mesh_field_name}.weightedBinding.rootNodeIndex",
        )
        _set_custom_int_property(
            mesh,
            "spice_weighted_source_node_index",
            binding.get("sourceNodeIndex", 0),
            stats,
            field_name=f"{mesh_field_name}.weightedBinding.sourceNodeIndex",
        )
        mesh["spice_weighted_node_indices"] = ",".join(
            str(int(node_index)) for node_index in binding.get("nodeIndices", [])
        )
    _set_custom_int_property(
        mesh,
        "spice_diag_degenerate",
        diagnostics.get("degenerateTriangleCount", 0),
        stats,
        field_name=f"{mesh_field_name}.diagnostics.degenerateTriangleCount",
    )
    _set_custom_int_property(
        mesh,
        "spice_diag_out_of_range",
        diagnostics.get("outOfRangeIndexCount", 0),
        stats,
        field_name=f"{mesh_field_name}.diagnostics.outOfRangeIndexCount",
    )
    _set_custom_int_property(
        mesh,
        "spice_diag_cache_replay",
        diagnostics.get("cacheReplayTriangleCount", 0),
        stats,
        field_name=f"{mesh_field_name}.diagnostics.cacheReplayTriangleCount",
    )

    obj = bpy.data.objects.new(mesh_name, mesh)
    stats.mesh_count += 1
    stats.material_count += len(material_slots)
    return obj


def _set_parent_with_identity_inverse(obj: Object, parent: Object) -> None:
    obj.parent = parent
    obj.matrix_parent_inverse = mathutils.Matrix.Identity(4)


def _bone_name(node_index: int) -> str:
    return f"SoaNode_{node_index}"


def _node_local_matrix(node: dict[str, Any]) -> mathutils.Matrix:
    return _transform_to_matrix(_resolve_node_transform(node))


def _link_object_next_to(source_obj: Object, new_obj: Object) -> None:
    if source_obj.users_collection:
        source_obj.users_collection[0].objects.link(new_obj)
    else:
        bpy.context.scene.collection.objects.link(new_obj)


def _restore_object_mode(active_obj: Object | None, mode: str | None) -> None:
    if active_obj is None:
        return
    try:
        bpy.context.view_layer.objects.active = active_obj
        if mode is not None and active_obj.mode != mode:
            bpy.ops.object.mode_set(mode=mode)
    except (AttributeError, RuntimeError, TypeError):
        pass


def _create_weighted_armature(
    attach_obj: Object,
    node_indices: list[int],
    node_objects: list[Object | None],
    stats: ImportStats,
) -> Object | None:
    parent_obj = attach_obj.parent
    armature_data = bpy.data.armatures.new(f"{attach_obj.name}_ArmatureData")
    armature_obj = bpy.data.objects.new(f"{attach_obj.name}_Armature", armature_data)
    armature_obj.display_type = "WIRE"
    armature_obj.show_in_front = False
    armature_obj["spice_weighted_armature"] = True
    _link_object_next_to(attach_obj, armature_obj)

    if parent_obj is not None:
        _set_parent_with_identity_inverse(armature_obj, parent_obj)
    _apply_identity_local_transform(armature_obj)

    previous_active = bpy.context.view_layer.objects.active
    previous_mode = getattr(previous_active, "mode", None) if previous_active is not None else None
    try:
        bpy.context.view_layer.objects.active = armature_obj
        armature_obj.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        edit_bones = armature_data.edit_bones
        for bone in list(edit_bones):
            edit_bones.remove(bone)

        armature_inverse = armature_obj.matrix_world.inverted()
        for node_index in node_indices:
            if node_index < 0 or node_index >= len(node_objects) or node_objects[node_index] is None:
                continue
            target = node_objects[node_index]
            assert target is not None
            rest_matrix = armature_inverse @ target.matrix_world
            bone = edit_bones.new(f"SoaNode_{node_index}")
            head = rest_matrix.to_translation()
            rest_basis = rest_matrix.to_3x3()
            y_axis = rest_basis @ mathutils.Vector((0.0, 1.0, 0.0))
            z_axis = rest_basis @ mathutils.Vector((0.0, 0.0, 1.0))
            if y_axis.length_squared <= 1.0e-10:
                y_axis = mathutils.Vector((0.0, 1.0, 0.0))
            y_axis.normalize()
            bone.head = head
            bone.tail = head + y_axis * 0.05
            if z_axis.length_squared > 1.0e-10:
                z_axis.normalize()
                bone.align_roll(z_axis)

        bpy.ops.object.mode_set(mode="POSE")
        for node_index in node_indices:
            pose_bone = armature_obj.pose.bones.get(f"SoaNode_{node_index}")
            if pose_bone is None:
                continue
            target = node_objects[node_index]
            if target is None:
                continue
            constraint = pose_bone.constraints.new(type="COPY_TRANSFORMS")
            constraint.name = f"SoaCopy_Node_{node_index}"
            constraint.target = target
            constraint.target_space = "WORLD"
            constraint.owner_space = "WORLD"
        bpy.ops.object.mode_set(mode="OBJECT")
    except (AttributeError, RuntimeError, TypeError, ValueError) as exc:
        stats.add_warning(f"Unable to create weighted armature for {attach_obj.name}: {exc}")
        bpy.data.objects.remove(armature_obj, do_unlink=True)
        bpy.data.armatures.remove(armature_data, do_unlink=True)
        armature_obj = None
    finally:
        if armature_obj is not None:
            armature_obj.select_set(False)
        _restore_object_mode(previous_active, previous_mode)

    return armature_obj


def _create_object_tree_armature(
    entry_root: Object,
    entry_name: str,
    tree_index: int,
    tree: dict[str, Any],
    nodes: list[dict[str, Any]],
    node_objects: list[Object | None],
    stats: ImportStats,
) -> Object | None:
    armature_data = bpy.data.armatures.new(f"{entry_name}_Tree_{tree_index}_ArmatureData")
    armature_obj = bpy.data.objects.new(f"{entry_name}_Tree_{tree_index}_Armature", armature_data)
    armature_obj.display_type = "WIRE"
    armature_obj.show_in_front = True
    armature_obj["spice_object_tree_armature"] = True
    _set_custom_int_property(
        armature_obj,
        "spice_tree_index",
        tree_index,
        stats,
        field_name=f"objectTrees[{tree_index}]",
    )
    _set_custom_int_property(
        armature_obj,
        "spice_source_object_address",
        tree.get("sourceObjectAddress", 0),
        stats,
        field_name=f"objectTrees[{tree_index}].sourceObjectAddress",
    )
    _link_object_next_to(entry_root, armature_obj)
    _set_parent_with_identity_inverse(armature_obj, entry_root)
    _apply_identity_local_transform(armature_obj)

    previous_active = bpy.context.view_layer.objects.active
    previous_mode = getattr(previous_active, "mode", None) if previous_active is not None else None
    try:
        bpy.context.view_layer.objects.active = armature_obj
        armature_obj.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        edit_bones = armature_data.edit_bones
        for bone in list(edit_bones):
            edit_bones.remove(bone)

        armature_inverse = armature_obj.matrix_world.inverted()
        for node_index, target in enumerate(node_objects):
            if target is None:
                continue
            rest_matrix = armature_inverse @ target.matrix_world
            bone = edit_bones.new(_bone_name(node_index))
            head = rest_matrix.to_translation()
            rest_basis = rest_matrix.to_3x3()
            y_axis = rest_basis @ mathutils.Vector((0.0, 1.0, 0.0))
            z_axis = rest_basis @ mathutils.Vector((0.0, 0.0, 1.0))
            if y_axis.length_squared <= 1.0e-10:
                y_axis = mathutils.Vector((0.0, 1.0, 0.0))
            y_axis.normalize()
            bone.head = head
            bone.tail = head + y_axis * 0.05
            if z_axis.length_squared > 1.0e-10:
                z_axis.normalize()
                bone.align_roll(z_axis)

        for node_index, node in enumerate(nodes):
            parent_index = node.get("parentNodeIndex")
            if parent_index is None:
                continue
            bone = edit_bones.get(_bone_name(node_index))
            parent_bone = edit_bones.get(_bone_name(int(parent_index)))
            if bone is not None and parent_bone is not None:
                bone.parent = parent_bone
                bone.use_connect = False

        bpy.ops.object.mode_set(mode="OBJECT")
        for node_index, node in enumerate(nodes):
            bone = armature_obj.data.bones.get(_bone_name(node_index))
            if bone is None:
                continue
            bone["spice_node_index"] = node_index
            bone["spice_source_node_offset"] = int(node.get("sourceNodeOffset", 0))
            bone["spice_source_eval_flags"] = int(node.get("sourceEvalFlags", 0))
            bone["spice_source_attach_offset"] = int(node.get("sourceAttachOffset", 0))
        stats.armature_count += 1
    except (AttributeError, RuntimeError, TypeError, ValueError) as exc:
        stats.add_warning(f"Unable to create object tree armature for {entry_name}: {exc}")
        bpy.data.objects.remove(armature_obj, do_unlink=True)
        bpy.data.armatures.remove(armature_data, do_unlink=True)
        armature_obj = None
    finally:
        if armature_obj is not None:
            armature_obj.select_set(False)
        _restore_object_mode(previous_active, previous_mode)

    return armature_obj


def _create_armature_bound_attach(
    source_obj: Object,
    mesh_data: dict[str, Any],
    attach_name: str,
    source_node_index: int,
    armature_obj: Object,
    node_objects: list[Object | None],
    stats: ImportStats,
) -> Object:
    binding = _mesh_weighted_binding(mesh_data)
    local_node_index = source_node_index
    if binding is not None:
        local_node_index = _weighted_root_node_index(mesh_data, source_node_index)

    mesh = source_obj.data.copy()
    mesh.name = f"{attach_name}_Mesh"
    if 0 <= local_node_index < len(node_objects) and node_objects[local_node_index] is not None:
        local_to_armature = armature_obj.matrix_world.inverted() @ node_objects[local_node_index].matrix_world
        for vertex in mesh.vertices:
            vertex.co = local_to_armature @ vertex.co
        mesh.update()
    else:
        stats.add_warning(
            f"Armature-bound mesh {attach_name} has invalid local node index {local_node_index}."
        )

    attach_obj = bpy.data.objects.new(attach_name, mesh)
    _set_parent_with_identity_inverse(attach_obj, armature_obj)
    _apply_identity_local_transform(attach_obj)
    attach_obj["spice_armature_bound"] = True

    if binding is None:
        group = attach_obj.vertex_groups.new(name=_bone_name(source_node_index))
        if len(mesh.vertices) > 0:
            group.add(list(range(len(mesh.vertices))), 1.0, "REPLACE")
        attach_obj["spice_rigid_node_index"] = source_node_index
    else:
        groups: dict[int, Any] = {}
        for raw_node_index in binding.get("nodeIndices", []):
            try:
                node_index = int(raw_node_index)
            except (TypeError, ValueError):
                continue
            if 0 <= node_index < len(node_objects) and node_objects[node_index] is not None:
                groups[node_index] = attach_obj.vertex_groups.new(name=_bone_name(node_index))
            else:
                stats.add_warning(f"Weighted mesh {attach_name} references invalid nodeIndex={node_index}.")
        for vertex_index, vertex in enumerate(mesh_data.get("vertices", [])):
            assigned = False
            for weight_data in vertex.get("weights", []):
                try:
                    node_index = int(weight_data.get("boneOrNodeIndex", -1))
                    weight = float(weight_data.get("weight", 0.0))
                except (TypeError, ValueError):
                    continue
                if weight <= 0.0 or node_index not in groups:
                    continue
                groups[node_index].add([vertex_index], weight, "ADD")
                assigned = True
            if not assigned and source_node_index in groups:
                groups[source_node_index].add([vertex_index], 1.0, "ADD")
        attach_obj["spice_weighted_binding"] = True
        attach_obj["spice_weighted_node_indices"] = ",".join(str(i) for i in sorted(groups.keys()))

    modifier = attach_obj.modifiers.new(name="SoaArmature", type="ARMATURE")
    modifier.object = armature_obj
    modifier.use_vertex_groups = True
    modifier.use_bone_envelopes = False
    try:
        modifier.use_deform_preserve_volume = False
    except AttributeError:
        pass

    return attach_obj


def _configure_weighted_deformation(
    attach_obj: Object,
    mesh_data: dict[str, Any],
    node_objects: list[Object | None],
    stats: ImportStats,
) -> None:
    binding = _mesh_weighted_binding(mesh_data)
    if binding is None:
        return

    node_indices: list[int] = []
    for raw_node_index in binding.get("nodeIndices", []):
        try:
            node_index = int(raw_node_index)
        except (TypeError, ValueError):
            continue
        if node_index not in node_indices:
            node_indices.append(node_index)

    if not node_indices:
        stats.add_warning(f"Weighted mesh {attach_obj.name} has no node influences.")
        return

    groups: dict[int, Any] = {}
    for node_index in node_indices:
        if node_index < 0 or node_index >= len(node_objects) or node_objects[node_index] is None:
            stats.add_warning(
                f"Weighted mesh {attach_obj.name} references invalid nodeIndex={node_index}."
            )
            continue
        groups[node_index] = attach_obj.vertex_groups.new(name=f"SoaNode_{node_index}")

    for vertex_index, vertex in enumerate(mesh_data.get("vertices", [])):
        for weight_data in vertex.get("weights", []):
            try:
                node_index = int(weight_data.get("boneOrNodeIndex", -1))
                weight = float(weight_data.get("weight", 0.0))
            except (TypeError, ValueError):
                continue
            if weight <= 0.0 or node_index not in groups:
                continue
            groups[node_index].add([vertex_index], weight, "ADD")

    valid_node_indices = [node_index for node_index in node_indices if node_index in groups]
    armature_obj = _create_weighted_armature(attach_obj, valid_node_indices, node_objects, stats)
    if armature_obj is not None:
        modifier = attach_obj.modifiers.new(name="SoaWeightedArmature", type="ARMATURE")
        modifier.object = armature_obj
        modifier.use_vertex_groups = True
        modifier.use_bone_envelopes = False
        try:
            modifier.use_deform_preserve_volume = False
        except AttributeError:
            pass

    attach_obj["spice_weighted_binding"] = True
    _set_custom_int_property(
        attach_obj,
        "spice_weighted_root_node_index",
        binding.get("rootNodeIndex", 0),
        stats,
        field_name=f"{attach_obj.name}.weightedBinding.rootNodeIndex",
    )


def _transform_to_matrix(transform: dict[str, Any]) -> mathutils.Matrix:
    position = transform.get("position", [0.0, 0.0, 0.0])
    quat = transform.get("rotation", [0.0, 0.0, 0.0, 1.0])
    scale = transform.get("scale", [1.0, 1.0, 1.0])

    source_position = mathutils.Vector((float(position[0]), float(position[1]), float(position[2])))
    blender_position = NJCM_TO_BLENDER_AXIS @ source_position

    source_rotation = mathutils.Quaternion(
        (float(quat[3]), float(quat[0]), float(quat[1]), float(quat[2]))
    )
    blender_rotation = NJCM_TO_BLENDER_AXIS @ source_rotation @ NJCM_TO_BLENDER_AXIS.conjugated()
    blender_scale = mathutils.Vector((float(scale[0]), float(scale[1]), float(scale[2])))
    return mathutils.Matrix.LocRotScale(blender_position, blender_rotation, blender_scale)

def _source_vec3_to_blender(value: list[Any]) -> mathutils.Vector:
    source = mathutils.Vector((float(value[0]), float(value[1]), float(value[2])))
    return NJCM_TO_BLENDER_AXIS @ source


def _source_quat_to_blender(value: list[Any]) -> mathutils.Quaternion:
    source = mathutils.Quaternion(
        (float(value[3]), float(value[0]), float(value[1]), float(value[2]))
    )
    return NJCM_TO_BLENDER_AXIS @ source @ NJCM_TO_BLENDER_AXIS.conjugated()


def _source_euler_to_blender_quat(value: list[Any]) -> mathutils.Quaternion:
    source = mathutils.Euler(
        (float(value[0]), float(value[1]), float(value[2])),
        "XYZ",
    ).to_quaternion()
    return NJCM_TO_BLENDER_AXIS @ source @ NJCM_TO_BLENDER_AXIS.conjugated()


def _animation_action_name(animation: dict[str, Any], node_index: int) -> str:
    source_entry_id = int(animation.get("sourceEntryId", animation.get("tableIndex", 0)))
    motion_slot = int(animation.get("motionSlot", 0))
    source_motion_address = int(animation.get("sourceMotionAddress", 0))
    return (
        f"SoaAnim_{source_entry_id:03d}_slot_{motion_slot:02d}"
        f"_motion_0x{source_motion_address:X}_node_{node_index:03d}"
    )


def _armature_animation_action_name(animation: dict[str, Any]) -> str:
    source_entry_id = int(animation.get("sourceEntryId", animation.get("tableIndex", 0)))
    motion_slot = int(animation.get("motionSlot", 0))
    source_motion_address = int(animation.get("sourceMotionAddress", 0))
    return f"SoaAnim_{source_entry_id:03d}_slot_{motion_slot:02d}_motion_0x{source_motion_address:X}"


def _animation_track_name(animation: dict[str, Any]) -> str:
    motion_slot = int(animation.get("motionSlot", 0))
    source_motion_address = int(animation.get("sourceMotionAddress", 0))
    return f"SoaSlot_{motion_slot:02d}_0x{source_motion_address:X}"


def _set_action_interpolation(action: Any, interpolation_mode: str) -> None:
    interpolation = "LINEAR" if interpolation_mode.strip().lower() == "linear" else None
    if interpolation is None:
        return
    for fcurve in action.fcurves:
        for keyframe in fcurve.keyframe_points:
            keyframe.interpolation = interpolation


def _add_muted_nla_strip(obj: Object, action: Any, animation: dict[str, Any], stats: ImportStats) -> None:
    try:
        animation_data = obj.animation_data_create()
        track_name = _animation_track_name(animation)
        track = animation_data.nla_tracks.new()
        track.name = track_name
        strip = track.strips.new(track_name, 0, action)
        strip.name = track_name
        track.mute = True
    except (AttributeError, RuntimeError, TypeError, ValueError) as exc:
        stats.add_warning(f"Could not create muted NLA strip for action {action.name}: {exc}")


def _parse_optional_motion_slot(value: str) -> int | None:
    text = value.strip()
    if not text:
        return None
    try:
        return int(text, 0)
    except ValueError as exc:
        raise ValueError(f"Invalid Preview Motion Slot value: {value!r}") from exc


def _apply_vec3_keyframes(
    obj: Object,
    keyframes: list[dict[str, Any]],
    data_path: str,
    convert: Callable[[list[Any]], Any],
) -> None:
    for keyframe in keyframes:
        frame = int(keyframe.get("frame", 0))
        setattr(obj, data_path, convert(keyframe.get("value", [0.0, 0.0, 0.0])))
        obj.keyframe_insert(data_path=data_path, frame=frame)


def _apply_quat_keyframes(
    obj: Object,
    keyframes: list[dict[str, Any]],
    convert: Callable[[list[Any]], mathutils.Quaternion],
) -> None:
    if not keyframes:
        return
    obj.rotation_mode = "QUATERNION"
    for keyframe in keyframes:
        frame = int(keyframe.get("frame", 0))
        obj.rotation_quaternion = convert(keyframe.get("value", [0.0, 0.0, 0.0, 1.0]))
        obj.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def _keyframe_pose_bone_channels(pose_bone: Any, frame: int) -> None:
    pose_bone.keyframe_insert(data_path="location", frame=frame)
    pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=frame)
    pose_bone.keyframe_insert(data_path="scale", frame=frame)


def _apply_pose_position_keyframes(
    pose_bone: Any,
    keyframes: list[dict[str, Any]],
    rest_location: mathutils.Vector,
) -> None:
    for keyframe in keyframes:
        frame = int(keyframe.get("frame", 0))
        pose_bone.location = _source_vec3_to_blender(keyframe.get("value", [0.0, 0.0, 0.0])) - rest_location
        pose_bone.keyframe_insert(data_path="location", frame=frame)


def _apply_pose_scale_keyframes(
    pose_bone: Any,
    keyframes: list[dict[str, Any]],
    rest_scale: mathutils.Vector,
) -> None:
    for keyframe in keyframes:
        frame = int(keyframe.get("frame", 0))
        desired = mathutils.Vector(tuple(float(v) for v in keyframe.get("value", [1.0, 1.0, 1.0])))
        pose_bone.scale = mathutils.Vector((
            desired.x / rest_scale.x if abs(rest_scale.x) > 1.0e-10 else desired.x,
            desired.y / rest_scale.y if abs(rest_scale.y) > 1.0e-10 else desired.y,
            desired.z / rest_scale.z if abs(rest_scale.z) > 1.0e-10 else desired.z,
        ))
        pose_bone.keyframe_insert(data_path="scale", frame=frame)


def _apply_pose_rotation_keyframes(
    pose_bone: Any,
    keyframes: list[dict[str, Any]],
    rest_rotation: mathutils.Quaternion,
    convert: Callable[[list[Any]], mathutils.Quaternion],
) -> None:
    if not keyframes:
        return
    pose_bone.rotation_mode = "QUATERNION"
    rest_inverse = rest_rotation.conjugated()
    for keyframe in keyframes:
        frame = int(keyframe.get("frame", 0))
        rotation = rest_inverse @ convert(keyframe.get("value", [0.0, 0.0, 0.0, 1.0]))
        rotation.normalize()
        pose_bone.rotation_quaternion = rotation
        pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def _apply_ir_armature_animations(
    animations: list[dict[str, Any]],
    tree_armature_bindings: dict[tuple[int, int], Object],
    tree_nodes_by_binding: dict[tuple[int, int], list[dict[str, Any]]],
    stats: ImportStats,
    preview_motion_slot: int | None,
    create_nla_tracks: bool,
) -> None:
    fallback_preview_actions: dict[Object, Any] = {}
    selected_preview_actions: dict[Object, Any] = {}
    for animation in animations:
        table_index = int(animation.get("tableIndex", animation.get("sourceEntryId", 0)))
        tree_index = int(animation.get("objectTreeIndex", -1))
        motion_slot = int(animation.get("motionSlot", 0))
        binding_key = (table_index, tree_index)
        armature_obj = tree_armature_bindings.get(binding_key)
        nodes = tree_nodes_by_binding.get(binding_key)
        if armature_obj is None or nodes is None:
            stats.add_warning(
                f"Animation motionSlot={animation.get('motionSlot')} for tableIndex={table_index} "
                f"could not bind to armature objectTreeIndex={tree_index}."
            )
            continue

        animation_data = armature_obj.animation_data_create()
        previous_action = animation_data.action
        action = bpy.data.actions.new(_armature_animation_action_name(animation))
        action.use_fake_user = True
        action["spice_source_entry_id"] = int(animation.get("sourceEntryId", table_index))
        action["spice_table_index"] = table_index
        action["spice_motion_slot"] = motion_slot
        action["spice_source_motion_address"] = int(animation.get("sourceMotionAddress", 0))
        action["spice_object_tree_index"] = tree_index
        animation_data.action = action

        for node_animation in animation.get("nodes", []):
            node_index = int(node_animation.get("nodeIndex", -1))
            if node_index < 0 or node_index >= len(nodes):
                stats.add_warning(
                    f"Animation motionSlot={animation.get('motionSlot')} references invalid nodeIndex={node_index}."
                )
                continue
            pose_bone = armature_obj.pose.bones.get(_bone_name(node_index))
            if pose_bone is None:
                stats.add_warning(
                    f"Animation motionSlot={animation.get('motionSlot')} references missing bone nodeIndex={node_index}."
                )
                continue

            rest_location, rest_rotation, rest_scale = _node_local_matrix(nodes[node_index]).decompose()
            _apply_pose_position_keyframes(pose_bone, node_animation.get("position", []), rest_location)
            _apply_pose_scale_keyframes(pose_bone, node_animation.get("scale", []), rest_scale)
            _apply_pose_rotation_keyframes(
                pose_bone,
                node_animation.get("eulerRotation", []),
                rest_rotation,
                _source_euler_to_blender_quat,
            )
            _apply_pose_rotation_keyframes(
                pose_bone,
                node_animation.get("quaternionRotation", []),
                rest_rotation,
                _source_quat_to_blender,
            )

        _set_action_interpolation(action, str(animation.get("interpolationMode", "")))
        if create_nla_tracks:
            _add_muted_nla_strip(armature_obj, action, animation, stats)
        stats.animation_action_count += 1
        if armature_obj not in fallback_preview_actions:
            fallback_preview_actions[armature_obj] = action
        if preview_motion_slot is not None and motion_slot == preview_motion_slot and armature_obj not in selected_preview_actions:
            selected_preview_actions[armature_obj] = action
        animation_data.action = previous_action

        _warn_unapplied_animation_channels(animation, stats)

    if preview_motion_slot is not None and not selected_preview_actions:
        stats.add_warning(f"Preview Motion Slot {preview_motion_slot} did not match any imported armature actions.")

    for obj, fallback_action in fallback_preview_actions.items():
        action = selected_preview_actions.get(obj, fallback_action) if preview_motion_slot is not None else fallback_action
        animation_data = obj.animation_data_create()
        animation_data.action = action
        if action is not None:
            obj["spice_preview_action"] = action.name
            obj["spice_preview_motion_slot"] = int(action.get("spice_motion_slot", 0))


def _apply_ir_animations(
    animations: list[dict[str, Any]],
    tree_node_bindings: dict[tuple[int, int], list[Object | None]],
    stats: ImportStats,
    preview_motion_slot: int | None,
    create_nla_tracks: bool,
) -> None:
    fallback_preview_actions: dict[Object, Any] = {}
    selected_preview_actions: dict[Object, Any] = {}
    for animation in animations:
        table_index = int(animation.get("tableIndex", animation.get("sourceEntryId", 0)))
        tree_index = int(animation.get("objectTreeIndex", -1))
        motion_slot = int(animation.get("motionSlot", 0))
        node_objects = tree_node_bindings.get((table_index, tree_index))
        if node_objects is None:
            stats.add_warning(
                f"Animation motionSlot={animation.get('motionSlot')} for tableIndex={table_index} "
                f"could not bind to objectTreeIndex={tree_index}."
            )
            continue

        for node_animation in animation.get("nodes", []):
            node_index = int(node_animation.get("nodeIndex", -1))
            if node_index < 0 or node_index >= len(node_objects) or node_objects[node_index] is None:
                stats.add_warning(
                    f"Animation motionSlot={animation.get('motionSlot')} references invalid nodeIndex={node_index}."
                )
                continue
            node_obj = node_objects[node_index]
            assert node_obj is not None

            animation_data = node_obj.animation_data_create()
            previous_action = animation_data.action
            action = bpy.data.actions.new(_animation_action_name(animation, node_index))
            action.use_fake_user = True
            action["spice_source_entry_id"] = int(animation.get("sourceEntryId", table_index))
            action["spice_table_index"] = table_index
            action["spice_motion_slot"] = motion_slot
            action["spice_source_motion_address"] = int(animation.get("sourceMotionAddress", 0))
            action["spice_object_tree_index"] = tree_index
            action["spice_node_index"] = node_index
            animation_data.action = action

            _apply_vec3_keyframes(
                node_obj,
                node_animation.get("position", []),
                "location",
                _source_vec3_to_blender,
            )
            _apply_vec3_keyframes(
                node_obj,
                node_animation.get("scale", []),
                "scale",
                lambda value: mathutils.Vector((float(value[0]), float(value[1]), float(value[2]))),
            )
            _apply_quat_keyframes(
                node_obj,
                node_animation.get("eulerRotation", []),
                _source_euler_to_blender_quat,
            )
            _apply_quat_keyframes(
                node_obj,
                node_animation.get("quaternionRotation", []),
                _source_quat_to_blender,
            )
            _set_action_interpolation(action, str(animation.get("interpolationMode", "")))
            if create_nla_tracks:
                _add_muted_nla_strip(node_obj, action, animation, stats)
            stats.animation_action_count += 1
            if node_obj not in fallback_preview_actions:
                fallback_preview_actions[node_obj] = action
            if preview_motion_slot is not None and motion_slot == preview_motion_slot and node_obj not in selected_preview_actions:
                selected_preview_actions[node_obj] = action
            animation_data.action = previous_action

        _warn_unapplied_animation_channels(animation, stats)

    if preview_motion_slot is not None and not selected_preview_actions:
        stats.add_warning(f"Preview Motion Slot {preview_motion_slot} did not match any imported animation actions.")

    for obj, fallback_action in fallback_preview_actions.items():
        action = selected_preview_actions.get(obj, fallback_action) if preview_motion_slot is not None else fallback_action
        animation_data = obj.animation_data_create()
        animation_data.action = action
        if action is not None:
            obj["spice_preview_action"] = action.name
            obj["spice_preview_motion_slot"] = int(action.get("spice_motion_slot", 0))


def _warn_unapplied_animation_channels(animation: dict[str, Any], stats: ImportStats) -> None:
    for channel in animation.get("channels", []):
        stats.add_warning(
            f"Animation motionSlot={animation.get('motionSlot')} parsed non-transform "
            f"channel={channel.get('channel')} nodeIndex={channel.get('nodeIndex')} "
            f"keyframes={len(channel.get('keyframes', []))}; it was not visually applied."
        )
    for channel in animation.get("unsupportedChannels", []):
        stats.add_warning(
            f"Animation motionSlot={animation.get('motionSlot')} parsed unsupported "
            f"channel={channel.get('channel')} nodeIndex={channel.get('nodeIndex')} "
            f"keyframes={channel.get('keyframeCount')}."
        )


def _entry_transform_to_matrix(transform: dict[str, Any]) -> mathutils.Matrix:
    position = transform.get("position", [0.0, 0.0, 0.0])
    rotation_raw = transform.get("rotationRaw", [0.0, 0.0, 0.0])
    scale = transform.get("scale", [1.0, 1.0, 1.0])

    blender_position = mathutils.Vector((
        float(position[0]),
        -float(position[2]),
        float(position[1]),
    ))
    blender_rotation = mathutils.Euler((
        math.radians(float(rotation_raw[0])),
        math.radians(float(rotation_raw[2])),
        math.radians(float(rotation_raw[1])),
    ), "XYZ").to_quaternion()
    blender_scale = mathutils.Vector((
        float(scale[0]),
        float(scale[2]),
        float(scale[1]),
    ))
    return mathutils.Matrix.LocRotScale(blender_position, blender_rotation, blender_scale)


def _apply_transform(obj: Object, transform: dict[str, Any]) -> None:
    obj.rotation_mode = "QUATERNION"
    obj.matrix_basis = _transform_to_matrix(transform)


def _apply_entry_transform(obj: Object, transform: dict[str, Any]) -> None:
    obj.rotation_mode = "QUATERNION"
    obj.matrix_basis = _entry_transform_to_matrix(transform)


def _apply_identity_local_transform(obj: Object) -> None:
    obj.rotation_mode = "QUATERNION"
    obj.matrix_basis = mathutils.Matrix.Identity(4)


def _matrix_to_compact_string(matrix: mathutils.Matrix) -> str:
    rows: list[str] = []
    for row in matrix:
        rows.append(f"[{row[0]:.6f},{row[1]:.6f},{row[2]:.6f},{row[3]:.6f}]")
    return "[" + ",".join(rows) + "]"


def _write_debug_log(debug_lines: list[str], target_collection_name: str, stats: ImportStats) -> None:
    if not debug_lines:
        return

    text_name = f"{target_collection_name}_ParityDebug"
    text_block = bpy.data.texts.get(text_name)
    if text_block is None:
        text_block = bpy.data.texts.new(text_name)
    else:
        text_block.clear()
    text_block.write("\n".join(debug_lines))

    stats.debug_lines = len(debug_lines)
    print(f"[Spice Parity Debug] wrote {len(debug_lines)} lines to Blender text '{text_name}'")


def _tag_entry_object(obj: Object, entry_id: int, table_index: int, fxn_name: str) -> None:
    obj["spice_ground_entry_id"] = entry_id
    obj["spice_ground_table_index"] = table_index
    obj["spice_ground_entry_function"] = fxn_name


def _resolve_node_transform(node: dict[str, Any]) -> dict[str, Any]:
    transform = dict(node.get("localTransform", {}))
    eval_flags = int(node.get("sourceEvalFlags", 0))

    if (eval_flags & NJD_EVAL_UNIT_POS) != 0:
        transform["position"] = [0.0, 0.0, 0.0]
    if (eval_flags & NJD_EVAL_UNIT_ANG) != 0:
        transform["rotation"] = [0.0, 0.0, 0.0, 1.0]
    if (eval_flags & NJD_EVAL_UNIT_SCL) != 0:
        transform["scale"] = [1.0, 1.0, 1.0]

    return transform


def _create_empty(name: str) -> Object:
    return bpy.data.objects.new(name, None)


def import_blender_ir_json(
    json_path: str,
    clear_target_collection: bool,
    target_collection_name: str,
    emit_parity_debug: bool,
    animation_preview_slot: str = "",
    object_tree_import_mode: str = "ARMATURE",
    create_nla_tracks: bool = False,
) -> ImportStats:
    payload = _parse_json(json_path)
    stats = ImportStats()
    preview_motion_slot = _parse_optional_motion_slot(animation_preview_slot)
    use_armatures = object_tree_import_mode == "ARMATURE"

    root_collection = _ensure_collection(target_collection_name)
    source_collection = _ensure_collection(f"{target_collection_name}_SourceMeshes", parent=root_collection)

    if clear_target_collection:
        _clear_collection(root_collection)
        _clear_collection(source_collection)

    texture_lookup = _build_texture_lookup(payload.get("textures", []), stats)

    index_entries: list[dict[str, Any]] = payload.get("indexEntries", [])
    area99_context = _find_area99_lookup_context(index_entries, stats)
    encounter_context_kind = "AREA99" if area99_context is not None else "DUNGEON"
    context_token = "".join(
        character if character.isalnum() else "_" for character in target_collection_name
    )
    encounter_context_id = f"{context_token}_{encounter_context_kind}"
    function_parameters_text = ""
    try:
        function_parameters_text = _persist_function_parameters_text(
            target_collection_name,
            index_entries,
        )
        root_collection["spice_mld_function_parameters_text"] = function_parameters_text
    except (TypeError, ValueError) as exc:
        stats.add_warning(f"Could not persist MLD function parameters: {exc}")

    mesh_payloads: list[dict[str, Any]] = payload.get("meshes", [])
    mesh_objects: list[Object] = []
    for mesh_data in mesh_payloads:
        mesh_obj = _build_mesh(
            mesh_data,
            texture_lookup,
            stats,
            encounter_context_id=encounter_context_id,
            encounter_context_kind=encounter_context_kind,
            function_parameters_text=function_parameters_text,
        )
        _link_object_to_collection(source_collection, mesh_obj)
        mesh_obj.hide_viewport = True
        mesh_obj.hide_render = True
        mesh_objects.append(mesh_obj)

    object_trees: list[dict[str, Any]] = payload.get("objectTrees", [])
    debug_lines: list[str] = []
    tree_node_bindings: dict[tuple[int, int], list[Object | None]] = {}
    tree_armature_bindings: dict[tuple[int, int], Object] = {}
    tree_nodes_by_binding: dict[tuple[int, int], list[dict[str, Any]]] = {}

    for entry_index, entry in enumerate(index_entries):
        transform = entry.get("transform", {})
        entry_id = int(entry.get("sourceEntryId", 0))
        table_index = int(entry.get("tableIndex", entry_index))
        fxn_name = str(entry.get("fxnName", ""))
        entry_collection = root_collection
        entry_root = _create_empty(_entry_root_name(entry_index, entry_id, fxn_name))
        _apply_entry_transform(entry_root, transform)
        if emit_parity_debug:
            debug_lines.append(
                (
                    f"ENTRY sourceEntryId={entry_id} tblId={int(entry.get('tblId', 0))} "
                    f"fxnName={fxn_name} "
                    f"basis={_matrix_to_compact_string(entry_root.matrix_basis)} "
                    f"world={_matrix_to_compact_string(entry_root.matrix_world)}"
                )
            )
        _set_custom_int_property(
            entry_root,
            "spice_source_entry_id",
            entry_id,
            stats,
            field_name=f"indexEntries[{entry_id}].sourceEntryId",
        )
        _set_custom_int_property(
            entry_root,
            "spice_tbl_id",
            entry.get("tblId", 0),
            stats,
            field_name=f"indexEntries[{entry_id}].tblId",
        )
        _set_custom_int_property(
            entry_root,
            "spice_table_index",
            table_index,
            stats,
            field_name=f"indexEntries[{entry_id}].tableIndex",
        )
        entry_root["spice_fxn_name"] = fxn_name
        _tag_entry_object(entry_root, entry_id, table_index, fxn_name)
        entry_root["spice_object_addresses"] = ",".join(
            str(int(v)) for v in entry.get("objectAddresses", [])
        )
        entry_root["spice_ground_addresses"] = ",".join(
            str(int(v)) for v in entry.get("groundAddresses", [])
        )
        _link_object_to_collection(entry_collection, entry_root)
        stats.object_count += 1

        tree_indices = entry.get("objectTreeIndices", [])
        if not tree_indices:
            mesh_indices = entry.get("meshIndices", [])
            for slot, mesh_index in enumerate(mesh_indices):
                mi = int(mesh_index)
                if mi < 0 or mi >= len(mesh_objects):
                    stats.warnings += 1
                    continue
                source_obj = mesh_objects[mi]
                instance_name = f"SoaInst_{entry_id}_{slot}_{source_obj.name}"
                instance_obj = bpy.data.objects.new(instance_name, source_obj.data)
                _set_parent_with_identity_inverse(instance_obj, entry_root)
                _apply_identity_local_transform(instance_obj)
                instance_obj["spice_mesh_index"] = mi
                _tag_entry_object(instance_obj, entry_id, table_index, fxn_name)
                _link_object_to_collection(entry_collection, instance_obj)
                stats.object_count += 1
            continue

        for slot, tree_index in enumerate(tree_indices):
            ti = int(tree_index)
            if ti < 0 or ti >= len(object_trees):
                stats.warnings += 1
                continue

            tree = object_trees[ti]
            nodes = tree.get("nodes", [])
            node_objects: list[Object | None] = [None] * len(nodes)
            binding_key = (table_index, ti)
            tree_node_bindings[binding_key] = node_objects
            for node_idx, node in enumerate(nodes):
                node_name = _node_object_name(entry_root.name, node_idx, tree)
                node_obj = _create_empty(node_name)
                _set_custom_int_property(
                    node_obj,
                    "spice_node_index",
                    node_idx,
                    stats,
                    field_name=f"objectTrees[{ti}].nodes[{node_idx}].nodeIndex",
                )
                _set_custom_int_property(
                    node_obj,
                    "spice_source_node_offset",
                    node.get("sourceNodeOffset", 0),
                    stats,
                    field_name=f"objectTrees[{ti}].nodes[{node_idx}].sourceNodeOffset",
                )
                _set_custom_int_property(
                    node_obj,
                    "spice_source_eval_flags",
                    node.get("sourceEvalFlags", 0),
                    stats,
                    field_name=f"objectTrees[{ti}].nodes[{node_idx}].sourceEvalFlags",
                )
                _set_custom_int_property(
                    node_obj,
                    "spice_source_attach_offset",
                    node.get("sourceAttachOffset", 0),
                    stats,
                    field_name=f"objectTrees[{ti}].nodes[{node_idx}].sourceAttachOffset",
                )
                if node_idx == 0:
                    _set_custom_int_property(
                        node_obj,
                        "spice_tree_index",
                        ti,
                        stats,
                        field_name=f"indexEntries[{entry_id}].objectTreeIndices[{slot}]",
                    )
                    _set_custom_int_property(
                        node_obj,
                        "spice_source_object_address",
                        tree.get("sourceObjectAddress", 0),
                        stats,
                        field_name=f"objectTrees[{ti}].sourceObjectAddress",
                    )
                    _set_custom_int_property(
                        node_obj,
                        "spice_source_chunk_offset",
                        tree.get("sourceChunkOffset", 0),
                        stats,
                        field_name=f"objectTrees[{ti}].sourceChunkOffset",
                    )
                _link_object_to_collection(entry_collection, node_obj)
                node_objects[node_idx] = node_obj
                stats.object_count += 1

            for node_idx, node in enumerate(nodes):
                node_obj = node_objects[node_idx]
                if node_obj is None:
                    continue

                parent_idx = node.get("parentNodeIndex")
                if parent_idx is None:
                    _set_parent_with_identity_inverse(node_obj, entry_root)
                else:
                    pi = int(parent_idx)
                    if 0 <= pi < len(node_objects) and node_objects[pi] is not None:
                        _set_parent_with_identity_inverse(node_obj, node_objects[pi])
                    else:
                        _set_parent_with_identity_inverse(node_obj, entry_root)
                        stats.warnings += 1
                _apply_transform(node_obj, _resolve_node_transform(node))
                if emit_parity_debug:
                    debug_lines.append(
                        (
                            f"NODE treeIndex={ti} nodeIndex={node_idx} "
                            f"parentNodeIndex={parent_idx} "
                            f"sourceNodeOffset={int(node.get('sourceNodeOffset', 0))} "
                            f"sourceEvalFlags=0x{int(node.get('sourceEvalFlags', 0)):X} "
                            f"basis={_matrix_to_compact_string(node_obj.matrix_basis)} "
                            f"world={_matrix_to_compact_string(node_obj.matrix_world)}"
                        )
                    )

            armature_obj: Object | None = None
            if use_armatures:
                armature_obj = _create_object_tree_armature(
                    entry_root,
                    entry_root.name,
                    ti,
                    tree,
                    nodes,
                    node_objects,
                    stats,
                )
                if armature_obj is not None:
                    _tag_entry_object(armature_obj, entry_id, table_index, fxn_name)
                    tree_armature_bindings[binding_key] = armature_obj
                    tree_nodes_by_binding[binding_key] = nodes
                    for node_obj in node_objects:
                        if node_obj is not None:
                            node_obj.hide_viewport = True
                            node_obj.hide_render = True

            for node_idx, node in enumerate(nodes):
                mesh_index = node.get("meshIndex")
                if mesh_index is None:
                    continue
                mi = int(mesh_index)
                if mi < 0 or mi >= len(mesh_objects):
                    stats.add_warning(
                        f"Invalid mesh index {mi} at objectTrees[{ti}].nodes[{node_idx}].meshIndex."
                    )
                    continue

                source_obj = mesh_objects[mi]
                mesh_data = mesh_payloads[mi]
                attach_name = _attach_object_name(entry_root.name, node_idx, node.get("sourceAttachOffset", 0))
                if use_armatures and armature_obj is not None:
                    attach_obj = _create_armature_bound_attach(
                        source_obj,
                        mesh_data,
                        attach_name,
                        node_idx,
                        armature_obj,
                        node_objects,
                        stats,
                    )
                else:
                    node_obj = node_objects[node_idx]
                    if node_obj is None:
                        continue
                    attach_obj = bpy.data.objects.new(attach_name, source_obj.data)
                    parent_obj = node_obj
                    weighted_root_index = _weighted_root_node_index(mesh_data, node_idx)
                    if weighted_root_index != node_idx:
                        if 0 <= weighted_root_index < len(node_objects) and node_objects[weighted_root_index] is not None:
                            parent_obj = node_objects[weighted_root_index]
                        else:
                            stats.add_warning(
                                f"Weighted mesh index {mi} has invalid rootNodeIndex={weighted_root_index}."
                            )
                    _set_parent_with_identity_inverse(attach_obj, parent_obj)
                    _apply_identity_local_transform(attach_obj)
                attach_obj["spice_mesh_index"] = mi
                _tag_entry_object(attach_obj, entry_id, table_index, fxn_name)
                _set_custom_int_property(
                    attach_obj,
                    "spice_node_index",
                    node_idx,
                    stats,
                    field_name=f"objectTrees[{ti}].nodes[{node_idx}].nodeIndex",
                )
                _set_custom_int_property(
                    attach_obj,
                    "spice_source_attach_offset",
                    node.get("sourceAttachOffset", 0),
                    stats,
                    field_name=f"objectTrees[{ti}].nodes[{node_idx}].sourceAttachOffset",
                )
                _link_object_to_collection(entry_collection, attach_obj)
                if not use_armatures:
                    _configure_weighted_deformation(attach_obj, mesh_data, node_objects, stats)
                stats.object_count += 1

        for slot, mesh_index in enumerate(entry.get("meshIndices", [])):
            mi = int(mesh_index)
            if mi < 0 or mi >= len(mesh_objects):
                stats.warnings += 1
                continue
            source_obj = mesh_objects[mi]
            if not str(source_obj.data.get("spice_label", "")).startswith("GRND_"):
                continue
            instance_name = f"SoaInst_{entry_id}_grnd_{slot}_{source_obj.name}"
            instance_obj = bpy.data.objects.new(instance_name, source_obj.data)
            _set_parent_with_identity_inverse(instance_obj, entry_root)
            _apply_identity_local_transform(instance_obj)
            instance_obj["spice_mesh_index"] = mi
            _tag_entry_object(instance_obj, entry_id, table_index, fxn_name)
            _link_object_to_collection(entry_collection, instance_obj)
            stats.object_count += 1

    if use_armatures:
        _apply_ir_armature_animations(
            payload.get("animations", []),
            tree_armature_bindings,
            tree_nodes_by_binding,
            stats,
            preview_motion_slot,
            create_nla_tracks,
        )
    else:
        _apply_ir_animations(
            payload.get("animations", []),
            tree_node_bindings,
            stats,
            preview_motion_slot,
            create_nla_tracks,
        )

    _refresh_triangle_metadata_visualization(bpy.context.scene, stats)

    if emit_parity_debug:
        _write_debug_log(debug_lines, target_collection_name, stats)

    return stats


_TRIANGLE_METADATA_REFRESH_GUARD = False
_TRIANGLE_METADATA_MSGBUS_OWNER = object()


def _refresh_triangle_metadata_from_ui(scene: Any) -> None:
    global _TRIANGLE_METADATA_REFRESH_GUARD
    if _TRIANGLE_METADATA_REFRESH_GUARD:
        return
    _TRIANGLE_METADATA_REFRESH_GUARD = True
    try:
        _refresh_triangle_metadata_visualization(scene)
    finally:
        _TRIANGLE_METADATA_REFRESH_GUARD = False


def _on_triangle_metadata_display_update(scene: Any, _context: Any) -> None:
    _refresh_triangle_metadata_from_ui(scene)


def _on_triangle_metadata_scope_update(scene: Any, _context: Any) -> None:
    try:
        _profile_name, families = _triangle_metadata_profile_from_scene(scene)
    except (ValueError, json.JSONDecodeError):
        families = []
    _refresh_triangle_metadata_legend(
        scene,
        families,
        str(getattr(scene, "spice_triangle_metadata_display_mode", "SKY_RIFT_FORCE")),
    )


def _on_triangle_metadata_active_object_changed() -> None:
    scene = getattr(bpy.context, "scene", None)
    if scene is None or not hasattr(scene, "spice_triangle_metadata_legend"):
        return
    if (
        str(getattr(scene, "spice_triangle_metadata_legend_scope", "ENTRY")) == "ENTRY"
        and str(getattr(scene, "spice_triangle_metadata_entry_selector", "ACTIVE")) == "ACTIVE"
    ):
        _on_triangle_metadata_scope_update(scene, bpy.context)


def _triangle_metadata_entry_items(_self: Any, context: Any) -> list[tuple[str, str, str]]:
    items: list[tuple[str, str, str]] = [
        ("ACTIVE", "Active Ground Entry", "Use the entry that owns the active object."),
    ]
    scene = getattr(context, "scene", None)
    if scene is None:
        return items
    entries: dict[int, tuple[int, str]] = {}
    for obj in scene.objects:
        if "spice_ground_table_index" not in obj:
            continue
        table_index = int(obj["spice_ground_table_index"])
        entry_id = int(obj.get("spice_ground_entry_id", 0))
        function_name = str(obj.get("spice_ground_entry_function", ""))
        entries.setdefault(table_index, (entry_id, function_name))
    for table_index, (entry_id, function_name) in sorted(entries.items()):
        label = f"Entry {entry_id} / table {table_index}"
        if function_name:
            label += f" / {function_name}"
        items.append((str(table_index), label, "Show metadata for this MLD index entry."))
    return items


class SPICE_PG_triangle_metadata_legend_item(bpy.types.PropertyGroup):
    name: StringProperty(name="Family")
    details: StringProperty(name="Details")
    face_count: IntProperty(name="Faces", default=0, min=0)
    show_face_count: BoolProperty(name="Show Face Count", default=False)
    color: FloatVectorProperty(
        name="Color",
        subtype="COLOR",
        size=4,
        min=0.0,
        max=1.0,
        default=(0.35, 0.35, 0.35, 1.0),
    )


class SPICE_UL_triangle_metadata_legend(bpy.types.UIList):
    def draw_item(
        self,
        _context: Any,
        layout: Any,
        _data: Any,
        item: Any,
        _icon: int,
        _active_data: Any,
        _active_property: str,
        _index: int,
    ) -> None:
        if self.layout_type in {"DEFAULT", "COMPACT"}:
            split = layout.split(factor=0.18)
            split.prop(item, "color", text="")
            text = split.row()
            text.label(text=f"{item.name}: {item.details}")
            if item.show_face_count:
                text.label(text=f"{item.face_count} faces")
        else:
            layout.prop(item, "color", text="")


class SPICE_OT_refresh_triangle_metadata(bpy.types.Operator):
    bl_idname = "spice.refresh_triangle_metadata"
    bl_label = "Refresh Triangle Metadata"
    bl_description = "Regenerate derived triangle metadata colors and the visible key"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context: bpy.types.Context) -> set[str]:
        _refresh_triangle_metadata_from_ui(context.scene)
        self.report({"INFO"}, "Triangle metadata visualization refreshed.")
        return {"FINISHED"}


class SPICE_OT_load_triangle_metadata_profile(bpy.types.Operator, ImportHelper):
    bl_idname = "spice.load_triangle_metadata_profile"
    bl_label = "Load Triangle Metadata Profile"
    bl_description = "Load a versioned JSON profile for metadata family labels and hue regions"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".json"
    filter_glob: StringProperty(default="*.json", options={"HIDDEN"})

    def execute(self, context: bpy.types.Context) -> set[str]:
        try:
            with open(self.filepath, "r", encoding="utf-8") as handle:
                payload = json.load(handle)
            profile_name, _families = _parse_triangle_metadata_profile(payload)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            self.report({"ERROR"}, f"Triangle metadata profile could not be loaded: {exc}")
            return {"CANCELLED"}

        context.scene.spice_triangle_metadata_profile_path = str(Path(self.filepath))
        context.scene.spice_triangle_metadata_profile_json = json.dumps(
            payload,
            ensure_ascii=True,
            separators=(",", ":"),
        )
        context.scene.spice_triangle_metadata_profile_name = profile_name
        _refresh_triangle_metadata_from_ui(context.scene)
        self.report({"INFO"}, f"Loaded triangle metadata profile: {profile_name}")
        return {"FINISHED"}


class SPICE_OT_reset_triangle_metadata_profile(bpy.types.Operator):
    bl_idname = "spice.reset_triangle_metadata_profile"
    bl_label = "Use Built-In Profile"
    bl_description = "Clear the custom metadata profile and restore the built-in family labels and colors"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context: bpy.types.Context) -> set[str]:
        context.scene.spice_triangle_metadata_profile_path = ""
        context.scene.spice_triangle_metadata_profile_json = ""
        context.scene.spice_triangle_metadata_profile_name = "Built-In"
        _refresh_triangle_metadata_from_ui(context.scene)
        return {"FINISHED"}


class SPICE_PT_triangle_metadata(bpy.types.Panel):
    bl_label = "Triangle Metadata"
    bl_idname = "SPICE_PT_triangle_metadata"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "SPICE"

    def draw(self, context: bpy.types.Context) -> None:
        layout = self.layout
        scene = context.scene

        layout.prop(scene, "spice_triangle_metadata_display_mode")
        if scene.spice_triangle_metadata_display_mode in POSITION_AWARE_ENCOUNTER_MODES:
            layout.prop(scene, "spice_triangle_metadata_resolved_opacity", text="Opacity")
        else:
            layout.prop(scene, "spice_triangle_metadata_opacity")
        layout.prop(scene, "spice_triangle_metadata_legend_scope")
        if scene.spice_triangle_metadata_legend_scope == "ENTRY":
            layout.prop(scene, "spice_triangle_metadata_entry_selector")
        if scene.spice_triangle_metadata_display_mode in POSITION_AWARE_ENCOUNTER_MODES:
            layout.prop(scene, "spice_triangle_metadata_scenario")
            layout.prop(scene, "spice_triangle_metadata_altitude_source")
            layout.label(text="Use Material Preview or Rendered view.")

        layout.operator(
            SPICE_OT_refresh_triangle_metadata.bl_idname,
            text="Refresh Colors",
            icon="FILE_REFRESH",
        )
        layout.separator()
        layout.label(text="Layer Color Key")
        if len(scene.spice_triangle_metadata_legend) == 0:
            layout.label(text="No triangle metadata in the current scope.")
            return
        layout.template_list(
            SPICE_UL_triangle_metadata_legend.__name__,
            "",
            scene,
            "spice_triangle_metadata_legend",
            scene,
            "spice_triangle_metadata_legend_index",
            rows=8,
        )
        layout.prop(scene, "spice_triangle_metadata_show_advanced_key")
        if scene.spice_triangle_metadata_show_advanced_key:
            layout.label(text="Advanced Raw Geometry Key")
            layout.template_list(
                SPICE_UL_triangle_metadata_legend.__name__,
                "advanced",
                scene,
                "spice_triangle_metadata_advanced_legend",
                scene,
                "spice_triangle_metadata_advanced_legend_index",
                rows=8,
            )


class IMPORT_SCENE_OT_spice_blender_ir(bpy.types.Operator, ImportHelper):
    bl_idname = "import_scene.spice_blender_ir"
    bl_label = "Import Spice Blender IR"
    bl_options = {"REGISTER", "UNDO"}

    filename_ext = ".json"
    filter_glob: StringProperty(default="*.json", options={"HIDDEN"})

    target_collection_name: StringProperty(
        name="Collection",
        default="Spice_Imported",
        description="Collection to place imported Spice objects",
    )

    clear_target_collection: BoolProperty(
        name="Clear Target Collection",
        default=False,
        description="Delete existing objects in target collections before import",
    )

    emit_parity_debug: BoolProperty(
        name="Emit Parity Debug Log",
        default=False,
        description=(
            "Write per-entry and per-node transform matrices into a Blender Text datablock "
            "named '<Collection>_ParityDebug' for SAIO parity comparison."
        ),
    )

    object_tree_import_mode: EnumProperty(
        name="Object Tree Mode",
        default="ARMATURE",
        items=(
            (
                "ARMATURE",
                "Armature",
                "Import each SA3D object tree as one armature with one Action per motion slot.",
            ),
            (
                "EMPTY",
                "Empty Debug",
                "Import SA3D nodes as separate empties for transform parity debugging.",
            ),
        ),
        description="Controls how SA3D object tree nodes are represented in Blender.",
    )

    animation_preview_slot: StringProperty(
        name="Preview Motion Slot",
        default="",
        description=(
            "Optional motion slot to assign as the active preview action. "
            "Leave blank to use the first imported animation per object tree; all slots are still imported as Actions."
        ),
    )

    create_nla_tracks: BoolProperty(
        name="Create NLA Tracks",
        default=False,
        description=(
            "Also create muted NLA strips for imported animation actions. "
            "Leave disabled to use the Action Editor as the primary slot selector."
        ),
    )

    def execute(self, context: bpy.types.Context) -> set[str]:
        del context
        json_path = str(Path(self.filepath))

        try:
            stats = import_blender_ir_json(
                json_path=json_path,
                clear_target_collection=self.clear_target_collection,
                target_collection_name=self.target_collection_name,
                emit_parity_debug=self.emit_parity_debug,
                animation_preview_slot=self.animation_preview_slot,
                object_tree_import_mode=self.object_tree_import_mode,
                create_nla_tracks=self.create_nla_tracks,
            )
        except Exception as exc:  # Blender operator-level error boundary
            self.report({"ERROR"}, f"Spice import failed: {exc}")
            return {"CANCELLED"}

        for warning in stats.warning_messages[:5]:
            self.report({"WARNING"}, warning)
        if len(stats.warning_messages) > 5:
            self.report(
                {"WARNING"},
                f"{len(stats.warning_messages) - 5} additional warnings written to the Blender console.",
            )

        self.report(
            {"INFO"},
            (
                "Spice import complete: "
                f"meshes={stats.mesh_count}, objects={stats.object_count}, "
                f"armatures={stats.armature_count}, textures={stats.texture_count}, "
                f"materials={stats.material_count}, "
                f"actions={stats.animation_action_count}, "
                f"warnings={stats.warnings}, debugLines={stats.debug_lines}"
            ),
        )
        return {"FINISHED"}


def menu_func_import(self: bpy.types.TOPBAR_MT_file_import, _context: bpy.types.Context) -> None:
    self.layout.operator(
        IMPORT_SCENE_OT_spice_blender_ir.bl_idname,
        text="Spice Blender IR (.json)",
    )


CLASSES = (
    SPICE_PG_triangle_metadata_legend_item,
    SPICE_UL_triangle_metadata_legend,
    SPICE_OT_refresh_triangle_metadata,
    SPICE_OT_load_triangle_metadata_profile,
    SPICE_OT_reset_triangle_metadata_profile,
    SPICE_PT_triangle_metadata,
    IMPORT_SCENE_OT_spice_blender_ir,
)

ADDON_KEYMAPS: list[tuple[Any, Any]] = []


def _register_keymaps() -> None:
    addon_keyconfig = bpy.context.window_manager.keyconfigs.addon
    if addon_keyconfig is None:
        return

    keymap = addon_keyconfig.keymaps.new(name="Window", space_type="EMPTY")
    keymap_item = keymap.keymap_items.new(
        IMPORT_SCENE_OT_spice_blender_ir.bl_idname,
        type="I",
        value="PRESS",
        ctrl=True,
        alt=True,
    )
    ADDON_KEYMAPS.append((keymap, keymap_item))


def _unregister_keymaps() -> None:
    for keymap, keymap_item in ADDON_KEYMAPS:
        try:
            keymap.keymap_items.remove(keymap_item)
        except (ReferenceError, RuntimeError):
            pass
    ADDON_KEYMAPS.clear()


def register() -> None:
    for klass in CLASSES:
        bpy.utils.register_class(klass)
    bpy.types.Scene.spice_triangle_metadata_display_mode = EnumProperty(
        name="Display",
        description="Select which triangle metadata layer drives face colors",
        items=TRIANGLE_METADATA_DISPLAY_MODES,
        default="SKY_RIFT_FORCE",
        update=_on_triangle_metadata_display_update,
    )
    bpy.types.Scene.spice_triangle_metadata_opacity = FloatProperty(
        name="Opacity",
        description="Opacity used by triangle metadata materials",
        default=0.25,
        min=0.0,
        max=1.0,
        update=_on_triangle_metadata_display_update,
    )
    bpy.types.Scene.spice_triangle_metadata_resolved_opacity = FloatProperty(
        name="Resolved Opacity",
        description="Opacity used by resolved encounter map materials",
        default=0.45,
        min=0.0,
        max=1.0,
        update=_on_triangle_metadata_display_update,
    )
    bpy.types.Scene.spice_triangle_metadata_legend_scope = EnumProperty(
        name="Entry Scope",
        description="Choose which ground entry contributes to the visible color key",
        items=(
            ("ENTRY", "Selected Entry", "Show metadata present in the selected or active ground entry."),
            ("SCENE", "Whole Scene", "Show metadata present across unique mesh data in the scene."),
        ),
        default="ENTRY",
        update=_on_triangle_metadata_scope_update,
    )
    bpy.types.Scene.spice_triangle_metadata_entry_selector = EnumProperty(
        name="Ground Entry",
        description="Select an imported MLD entry, or follow the active object",
        items=_triangle_metadata_entry_items,
        update=_on_triangle_metadata_scope_update,
    )
    bpy.types.Scene.spice_triangle_metadata_scenario = EnumProperty(
        name="Area 99 Scenario",
        description="Select which inferred 1008-byte fldEfcontrol lookup page is visualized",
        items=(
            ("PAGE0", "Page 0 (inferred pre-Soltis)", "Use the first lookup page."),
            ("PAGE1", "Page 1 (inferred post-Soltis)", "Use the second lookup page."),
        ),
        default="PAGE0",
        update=_on_triangle_metadata_display_update,
    )
    bpy.types.Scene.spice_triangle_metadata_altitude_source = EnumProperty(
        name="Altitude",
        description="Choose surface-derived or forced encounter altitude band",
        items=(
            ("SURFACE", "Surface Height", "Resolve each visible layer from its own source Y position."),
            ("LOW", "Forced Low", "Force the lookup's low altitude band."),
            ("MIDDLE", "Forced Middle", "Force the lookup's middle altitude band."),
            ("HIGH", "Forced High", "Force the lookup's high altitude band."),
        ),
        default="SURFACE",
        update=_on_triangle_metadata_display_update,
    )
    bpy.types.Scene.spice_triangle_metadata_profile_path = StringProperty(
        name="Profile Path",
        subtype="FILE_PATH",
        default="",
    )
    bpy.types.Scene.spice_triangle_metadata_profile_json = StringProperty(
        name="Profile JSON",
        default="",
        options={"HIDDEN"},
    )
    bpy.types.Scene.spice_triangle_metadata_profile_name = StringProperty(
        name="Profile",
        default="Built-In",
    )
    bpy.types.Scene.spice_triangle_metadata_legend = CollectionProperty(
        type=SPICE_PG_triangle_metadata_legend_item,
    )
    bpy.types.Scene.spice_triangle_metadata_legend_index = IntProperty(default=0, min=0)
    bpy.types.Scene.spice_triangle_metadata_show_advanced_key = BoolProperty(
        name="Show Advanced Geometry Key",
        description="Show the exact raw-triplet census and face counts",
        default=False,
    )
    bpy.types.Scene.spice_triangle_metadata_advanced_legend = CollectionProperty(
        type=SPICE_PG_triangle_metadata_legend_item,
    )
    bpy.types.Scene.spice_triangle_metadata_advanced_legend_index = IntProperty(default=0, min=0)
    bpy.types.TOPBAR_MT_file_import.append(menu_func_import)
    _register_keymaps()
    layer_objects_type = getattr(bpy.types, "LayerObjects", None)
    if layer_objects_type is not None:
        bpy.msgbus.subscribe_rna(
            key=(layer_objects_type, "active"),
            owner=_TRIANGLE_METADATA_MSGBUS_OWNER,
            args=(),
            notify=_on_triangle_metadata_active_object_changed,
        )


def unregister() -> None:
    bpy.msgbus.clear_by_owner(_TRIANGLE_METADATA_MSGBUS_OWNER)
    _unregister_keymaps()
    bpy.types.TOPBAR_MT_file_import.remove(menu_func_import)
    for property_name in (
        "spice_triangle_metadata_advanced_legend_index",
        "spice_triangle_metadata_advanced_legend",
        "spice_triangle_metadata_show_advanced_key",
        "spice_triangle_metadata_legend_index",
        "spice_triangle_metadata_legend",
        "spice_triangle_metadata_profile_name",
        "spice_triangle_metadata_profile_json",
        "spice_triangle_metadata_profile_path",
        "spice_triangle_metadata_altitude_source",
        "spice_triangle_metadata_scenario",
        "spice_triangle_metadata_entry_selector",
        "spice_triangle_metadata_legend_scope",
        "spice_triangle_metadata_resolved_opacity",
        "spice_triangle_metadata_opacity",
        "spice_triangle_metadata_display_mode",
    ):
        if hasattr(bpy.types.Scene, property_name):
            delattr(bpy.types.Scene, property_name)
    for klass in reversed(CLASSES):
        bpy.utils.unregister_class(klass)


if __name__ == "__main__":
    register()
