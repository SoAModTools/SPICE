"""Spice Blender IR JSON importer (baseline).

This importer targets the current JSON payload exported by SpiceMLD's
BlenderIrJsonExporter. It intentionally focuses on currently exported fields:
- vertex positions
- triangle corner vertex indices
- material metadata + textureName
- texture pixelDataBase64 (rgba8)
- indexEntries with transform + meshIndices, including decoded GRND meshes
"""

from __future__ import annotations

import base64
import json
import math
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, NamedTuple

import bpy
import mathutils
from bpy.props import BoolProperty, EnumProperty, StringProperty
from bpy.types import Collection, Image, Material, Mesh, Object
from bpy_extras.io_utils import ImportHelper

bl_info = {
    "name": "Spice Blender IR Importer",
    "author": "Spice",
    "version": (0, 1, 0),
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
GRND_COLLISION_VISUAL_SOURCE_Y_OFFSET = 0.05


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


def _ensure_collection(name: str, parent: Collection | None = None) -> Collection:
    collection = bpy.data.collections.get(name)
    if collection is None:
        collection = bpy.data.collections.new(name)
        if parent is None:
            bpy.context.scene.collection.children.link(collection)
        else:
            parent.children.link(collection)
    return collection


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


def _material_cache_name(material_data: dict[str, Any]) -> str:
    material_hash = int(material_data.get("materialHash", 0))
    texture_id = _read_optional_texture_id(material_data.get("textureId"))
    texture_name = str(material_data.get("textureName", "")).strip()
    if texture_name:
        safe_texture_name = "".join(
            ch if ch.isalnum() or ch in ("_", "-") else "_"
            for ch in texture_name
        )
        return f"SoaMat_{material_hash:016x}_{safe_texture_name}"
    if texture_id is not None:
        return f"SoaMat_{material_hash:016x}_tex_{texture_id}"
    return f"SoaMat_{material_hash:016x}"


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


def _build_material(
    material_data: dict[str, Any],
    texture_lookup: TextureLookup,
    stats: ImportStats,
    *,
    mesh_field_name: str,
    material_index: int,
) -> Material:
    name = _material_cache_name(material_data)
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


def _build_mesh(mesh_data: dict[str, Any], texture_lookup: TextureLookup, stats: ImportStats) -> Object:
    mesh_name = _mesh_object_name(mesh_data)
    mesh_field_name = f"meshes[{mesh_name}]"
    label = str(mesh_data.get("label", ""))
    is_grnd_mesh = label.startswith("GRND_")

    vertices_data = mesh_data.get("vertices", [])
    vertices = []
    for vertex in vertices_data:
        pos = vertex.get("position", [0.0, 0.0, 0.0])
        source_y = float(pos[1])
        if is_grnd_mesh:
            source_y += GRND_COLLISION_VISUAL_SOURCE_Y_OFFSET
        source_position = mathutils.Vector((float(pos[0]), source_y, float(pos[2])))
        blender_position = NJCM_TO_BLENDER_AXIS @ source_position
        vertices.append((blender_position.x, blender_position.y, blender_position.z))

    triangles: list[tuple[int, int, int]] = []
    poly_material_indices: list[int] = []
    poly_flat_flags: list[bool] = []
    uv_values: list[tuple[float, float] | None] = []
    color_values: list[tuple[float, float, float, float] | None] = []
    seen_triangle_geometry: set[
        tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]
    ] = set()
    for tri_set in mesh_data.get("triangleSets", []):
        corners = tri_set.get("corners", [])
        material_index = int(tri_set.get("materialIndex", 0))
        material_data = {}
        if 0 <= material_index < len(mesh_data.get("materials", [])):
            material_data = mesh_data["materials"][material_index]
        flat_shading = bool(material_data.get("flatShading", False))
        for corner_index in range(0, len(corners) - 2, 3):
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
            if geometry_key in seen_triangle_geometry:
                continue
            seen_triangle_geometry.add(geometry_key)

            triangles.append(tri)
            poly_material_indices.append(material_index)
            poly_flat_flags.append(flat_shading)
            _append_triangle_corner_attributes(triangle_corners, uv_values, color_values)

    mesh = bpy.data.meshes.new(mesh_name)
    mesh.from_pydata(vertices, [], triangles)
    mesh.validate(verbose=False)
    mesh.update()

    material_slots: list[Material] = []
    for material_index, material_data in enumerate(mesh_data.get("materials", [])):
        material_slots.append(
            _build_material(
                material_data,
                texture_lookup,
                stats,
                mesh_field_name=mesh_field_name,
                material_index=material_index,
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
        mesh["spice_visual_source_y_offset"] = GRND_COLLISION_VISUAL_SOURCE_Y_OFFSET
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

        for channel in animation.get("unsupportedChannels", []):
            stats.add_warning(
                f"Animation motionSlot={animation.get('motionSlot')} parsed unsupported "
                f"channel={channel.get('channel')} nodeIndex={channel.get('nodeIndex')} "
                f"keyframes={channel.get('keyframeCount')}."
            )

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

        for channel in animation.get("unsupportedChannels", []):
            stats.add_warning(
                f"Animation motionSlot={animation.get('motionSlot')} parsed unsupported "
                f"channel={channel.get('channel')} nodeIndex={channel.get('nodeIndex')} "
                f"keyframes={channel.get('keyframeCount')}."
            )

    if preview_motion_slot is not None and not selected_preview_actions:
        stats.add_warning(f"Preview Motion Slot {preview_motion_slot} did not match any imported animation actions.")

    for obj, fallback_action in fallback_preview_actions.items():
        action = selected_preview_actions.get(obj, fallback_action) if preview_motion_slot is not None else fallback_action
        animation_data = obj.animation_data_create()
        animation_data.action = action
        if action is not None:
            obj["spice_preview_action"] = action.name
            obj["spice_preview_motion_slot"] = int(action.get("spice_motion_slot", 0))


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

    mesh_payloads: list[dict[str, Any]] = payload.get("meshes", [])
    mesh_objects: list[Object] = []
    for mesh_data in mesh_payloads:
        mesh_obj = _build_mesh(mesh_data, texture_lookup, stats)
        source_collection.objects.link(mesh_obj)
        mesh_obj.hide_viewport = True
        mesh_obj.hide_render = True
        mesh_objects.append(mesh_obj)

    object_trees: list[dict[str, Any]] = payload.get("objectTrees", [])
    debug_lines: list[str] = []
    tree_node_bindings: dict[tuple[int, int], list[Object | None]] = {}
    tree_armature_bindings: dict[tuple[int, int], Object] = {}
    tree_nodes_by_binding: dict[tuple[int, int], list[dict[str, Any]]] = {}

    for entry_index, entry in enumerate(payload.get("indexEntries", [])):
        transform = entry.get("transform", {})
        entry_id = int(entry.get("sourceEntryId", 0))
        table_index = int(entry.get("tableIndex", entry_index))
        fxn_name = str(entry.get("fxnName", ""))
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
        entry_root["spice_object_addresses"] = ",".join(
            str(int(v)) for v in entry.get("objectAddresses", [])
        )
        entry_root["spice_ground_addresses"] = ",".join(
            str(int(v)) for v in entry.get("groundAddresses", [])
        )
        root_collection.objects.link(entry_root)
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
                root_collection.objects.link(instance_obj)
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
                root_collection.objects.link(node_obj)
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
                root_collection.objects.link(attach_obj)
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
            root_collection.objects.link(instance_obj)
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

    if emit_parity_debug:
        _write_debug_log(debug_lines, target_collection_name, stats)

    return stats


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
    IMPORT_SCENE_OT_spice_blender_ir,
)


def register() -> None:
    for klass in CLASSES:
        bpy.utils.register_class(klass)
    bpy.types.TOPBAR_MT_file_import.append(menu_func_import)


def unregister() -> None:
    bpy.types.TOPBAR_MT_file_import.remove(menu_func_import)
    for klass in reversed(CLASSES):
        bpy.utils.unregister_class(klass)


if __name__ == "__main__":
    register()
