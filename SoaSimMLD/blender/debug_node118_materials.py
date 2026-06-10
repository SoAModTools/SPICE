"""Ad hoc diagnostics for a101b entry 99 / node 118.

Run from Blender's Text editor after importing a101b.json. These helpers do not
modify the importer or the JSON; they only recolor/isolate the current scene so
the node 118 texture issue can be narrowed down.
"""

import bpy


NODE_118_ATTACH_OFFSET = 80828
NODE_118_LEGACY_SUFFIX = "node_118_attach_80828"
NODE_118_HEX_SUFFIX = "Node_118_attach_0x13BBC"


def _node118_objects():
    return [
        obj
        for obj in bpy.data.objects
        if obj.type == "MESH"
        and (
            obj.get("soasim_source_attach_offset") == NODE_118_ATTACH_OFFSET
            or NODE_118_LEGACY_SUFFIX in obj.name
            or NODE_118_HEX_SUFFIX in obj.name
        )
    ]


def find_node118():
    objs = _node118_objects()
    if not objs:
        print(f"No mesh object for node 118 attach 0x{NODE_118_ATTACH_OFFSET:X} was found.")
        return None
    if len(objs) > 1:
        print("Multiple node 118 objects found:")
        for obj in objs:
            print(f"  {obj.name}")
    return objs[0]


def print_node118_summary():
    obj = find_node118()
    if obj is None:
        return

    mesh = obj.data
    print(f"Object: {obj.name}")
    print(f"Mesh: {mesh.name}")
    print(f"Polygons: {len(mesh.polygons)}")
    print(f"UV layers: {[layer.name for layer in mesh.uv_layers]}")
    for index, slot in enumerate(obj.material_slots):
        mat = slot.material
        texture_name = mat.get("soasim_texture_name", "") if mat else ""
        poly_count = sum(1 for poly in mesh.polygons if poly.material_index == index)
        print(f"{index:02d}: faces={poly_count:3d} mat={mat.name if mat else '<none>'} tex={texture_name}")


def isolate_node118_material(material_index):
    obj = find_node118()
    if obj is None:
        return

    bpy.ops.object.mode_set(mode="OBJECT")
    for other in bpy.context.scene.objects:
        other.hide_viewport = other != obj
    obj.hide_viewport = False
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    mesh = obj.data
    for poly in mesh.polygons:
        poly.select = poly.material_index == material_index
    print(f"Selected node 118 faces using material slot {material_index}.")


def colorize_node118_material_slots():
    obj = find_node118()
    if obj is None:
        return

    colors = [
        (1.0, 0.0, 0.0, 1.0),
        (0.0, 0.7, 0.0, 1.0),
        (0.0, 0.2, 1.0, 1.0),
        (1.0, 0.8, 0.0, 1.0),
        (1.0, 0.0, 0.8, 1.0),
        (0.0, 0.9, 0.9, 1.0),
        (1.0, 0.45, 0.0, 1.0),
        (0.6, 0.2, 1.0, 1.0),
    ]
    for index, slot in enumerate(obj.material_slots):
        source = slot.material
        if source is None:
            continue
        mat = bpy.data.materials.new(f"DEBUG_slot_{index:02d}_{source.name}")
        mat.diffuse_color = colors[index % len(colors)]
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = mat.diffuse_color
            bsdf.inputs["Roughness"].default_value = 1.0
        slot.material = mat
    print("Node 118 material slots replaced with flat debug colors.")


def print_selected_face_debug():
    obj = bpy.context.object
    if obj is None or obj.type != "MESH":
        print("Select a mesh object first.")
        return

    bpy.ops.object.mode_set(mode="OBJECT")
    mesh = obj.data
    uv_layer = mesh.uv_layers.active
    selected = [poly for poly in mesh.polygons if poly.select]
    if not selected:
        print("No selected faces.")
        return

    for poly in selected[:20]:
        mat = obj.material_slots[poly.material_index].material
        texture_name = mat.get("soasim_texture_name", "") if mat else ""
        print(f"face={poly.index} matSlot={poly.material_index} mat={mat.name if mat else '<none>'} tex={texture_name}")
        if uv_layer is not None:
            for loop_index in poly.loop_indices:
                uv = uv_layer.data[loop_index].uv
                print(f"  loop={loop_index} uv=({uv.x:.6f}, {uv.y:.6f})")


def bypass_node118_sampler_math():
    obj = find_node118()
    if obj is None:
        return

    for slot in obj.material_slots:
        mat = slot.material
        if mat is None or mat.node_tree is None:
            continue
        nodes = mat.node_tree.nodes
        links = mat.node_tree.links
        tex = nodes.get("SoaTexture")
        coord = nodes.get("SoaTexCoord")
        if tex is None or coord is None:
            continue
        for link in list(tex.inputs["Vector"].links):
            links.remove(link)
        links.new(coord.outputs["UV"], tex.inputs["Vector"])
    print("Connected node 118 texture nodes directly to UV, bypassing clamp/mirror math.")


def set_node118_texture_extension(extension="REPEAT"):
    obj = find_node118()
    if obj is None:
        return

    for slot in obj.material_slots:
        mat = slot.material
        if mat is None or mat.node_tree is None:
            continue
        tex = mat.node_tree.nodes.get("SoaTexture")
        if tex is not None:
            tex.extension = extension
    print(f"Set node 118 Image Texture extension to {extension!r}.")


def flip_node118_images_vertically_once():
    obj = find_node118()
    if obj is None:
        return

    seen = set()
    for slot in obj.material_slots:
        mat = slot.material
        if mat is None or mat.node_tree is None:
            continue
        tex = mat.node_tree.nodes.get("SoaTexture")
        if tex is None or tex.image is None or tex.image.name in seen:
            continue
        seen.add(tex.image.name)
        image = tex.image
        width, height = image.size
        row_stride = width * 4
        pixels = list(image.pixels)
        flipped = [0.0] * len(pixels)
        for y in range(height):
            src = y * row_stride
            dst = (height - 1 - y) * row_stride
            flipped[dst : dst + row_stride] = pixels[src : src + row_stride]
        image.pixels = flipped
        image.update()
        print(f"Flipped image {image.name}.")


print_node118_summary()
