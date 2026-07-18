"""Blender-runtime smoke test for generated Area 99 lookup images.

Run with:
    blender --background --python-exit-code 1 --python SpiceTests/test_spice_blender_lookup_image_runtime.py
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import bpy


def _load_importer():
    importer_path = (
        Path(__file__).resolve().parents[1]
        / "SpiceMLD"
        / "blender"
        / "spice_blender_ir_importer.py"
    )
    spec = importlib.util.spec_from_file_location(
        "spice_blender_ir_importer_runtime_test",
        importer_path,
    )
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load importer from {importer_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _pack_lsb_first(raw_bytes: list[int]) -> tuple[int, ...]:
    return tuple(
        raw_bytes[index]
        | (raw_bytes[index + 1] << 8)
        | (raw_bytes[index + 2] << 16)
        | (raw_bytes[index + 3] << 24)
        for index in range(0, len(raw_bytes), 4)
    )


def _unique_colors(image) -> set[tuple[float, float, float, float]]:
    pixels = list(image.pixels)
    return {
        tuple(round(component, 6) for component in pixels[index:index + 4])
        for index in range(0, len(pixels), 4)
    }


def _area99_metadata_materials() -> list:
    return [
        material
        for material in bpy.data.materials
        if bool(material.get("spice_triangle_metadata_material", False))
        and str(material.get("spice_encounter_context_kind", "")) == "AREA99"
    ]


def _assert_resolved_materials(materials: list, expected_opacity: float) -> None:
    for material in materials:
        nodes = material.node_tree.nodes
        assert nodes.get("SpiceResolvedEmission") is not None
        assert nodes.get("SpiceResolvedTransparent") is not None
        mix = nodes.get("SpiceResolvedOpacity")
        assert mix is not None
        actual = float(mix.inputs[0].default_value)
        assert abs(actual - expected_opacity) < 1e-6, (
            material.name,
            actual,
            expected_opacity,
        )
        assert not any(node.type == "BSDF_PRINCIPLED" for node in nodes)


def _assert_authored_materials(materials: list, expected_opacity: float) -> None:
    for material in materials:
        nodes = material.node_tree.nodes
        bsdf = next(node for node in nodes if node.type == "BSDF_PRINCIPLED")
        actual = float(bsdf.inputs["Alpha"].default_value)
        assert abs(actual - expected_opacity) < 1e-6, (
            material.name,
            actual,
            expected_opacity,
        )
        assert nodes.get("SpiceResolvedEmission") is None


def _validate_loaded_blend(importer) -> None:
    scene = bpy.context.scene
    assert hasattr(scene, "spice_triangle_metadata_display_mode")
    if not hasattr(bpy.types.Scene, "spice_triangle_metadata_resolved_opacity"):
        bpy.types.Scene.spice_triangle_metadata_resolved_opacity = importer.FloatProperty(
            name="Resolved Opacity",
            default=0.45,
            min=0.0,
            max=1.0,
        )
    materials = _area99_metadata_materials()
    assert materials
    assert abs(float(scene.spice_triangle_metadata_resolved_opacity) - 0.45) < 1e-6

    scene["spice_triangle_metadata_scenario"] = 0
    scene["spice_triangle_metadata_altitude_source"] = 2
    scene["spice_triangle_metadata_resolved_opacity"] = 1.0
    scene["spice_triangle_metadata_display_mode"] = 4
    importer._refresh_triangle_metadata_visualization(scene)
    images = {
        bpy.data.images.get(
            f"SpiceEncounterLookup_{material.get('spice_encounter_context_id', 'Area99')}"
        )
        for material in materials
    }
    assert all(image is not None for image in images)
    assert all(image.colorspace_settings.name == "Non-Color" for image in images)
    assert all(image.packed_file is not None for image in images)
    assert all(len(_unique_colors(image)) > 1 for image in images)
    assert all(
        material.node_tree.nodes.get("SpiceEncounterLookupImage") is not None
        for material in materials
    )
    _assert_resolved_materials(materials, 1.0)

    scene["spice_triangle_metadata_opacity"] = 0.25
    scene["spice_triangle_metadata_display_mode"] = 1
    importer._refresh_triangle_metadata_visualization(scene)
    assert all(
        material.node_tree.nodes.get("SpiceEncounterLookupImage") is None
        for material in materials
    )
    _assert_authored_materials(materials, 0.25)

    scene["spice_triangle_metadata_resolved_opacity"] = 0.45
    scene["spice_triangle_metadata_display_mode"] = 4
    importer._refresh_triangle_metadata_visualization(scene)
    assert all(len(_unique_colors(image)) > 1 for image in images)
    _assert_resolved_materials(materials, 0.45)
    print("Loaded SPICE blend lookup material validation passed.")


def main(*, validate_loaded_blend: bool = False) -> None:
    importer = _load_importer()
    raw_bytes = [0] * (importer.AREA99_LOOKUP_PAGE_SIZE * 2)
    raw_bytes[0] = 31
    raw_bytes[importer.AREA99_LOOKUP_PAGE_SIZE] = 72
    context = importer.EncounterLookupContext(
        kind="AREA99",
        function_parameters=_pack_lsb_first(raw_bytes),
    )
    context_id = "SpiceRuntimeRegression"
    image_name = f"SpiceEncounterLookup_{context_id}"

    existing = bpy.data.images.get(image_name)
    if existing is not None:
        bpy.data.images.remove(existing)

    try:
        image = importer._update_area99_lookup_image(
            context_id,
            context,
            page=0,
            mode="ZONE_TABLE",
        )
        assert tuple(image.size) == (
            importer.AREA99_LOOKUP_WIDTH,
            importer.AREA99_LOOKUP_HEIGHT,
        )
        assert image.colorspace_settings.name == "Non-Color"
        assert image.packed_file is not None
        assert len(_unique_colors(image)) > 1

        reused = importer._update_area99_lookup_image(
            context_id,
            context,
            page=1,
            mode="ZONE_TABLE",
        )
        assert reused == image
        assert image.packed_file is not None
        assert len(_unique_colors(image)) > 1
        expected = importer._resolved_zone_table_color(7, 2)
        actual = tuple(image.pixels[:4])
        assert all(
            abs(actual[index] - expected[index]) <= (2.0 / 255.0)
            for index in range(4)
        ), (actual, expected)
    finally:
        image = bpy.data.images.get(image_name)
        if image is not None:
            bpy.data.images.remove(image)

    print("Spice Area 99 lookup image runtime test passed.")
    if validate_loaded_blend:
        _validate_loaded_blend(importer)


if __name__ == "__main__":
    script_args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    main(validate_loaded_blend="--validate-loaded-blend" in script_args)
