"""Scene setup shared by the arena render and sweep scripts.

Both run inside Blender on a dungeon scene imported with wow.export's
io_scene_wowobj addon. Keeping the lighting and material fixes in one place
means a sweep shot looks exactly like the final render it is chosen for.
"""

import bpy
from mathutils import Vector

# Surfaces closer together than this are the same structure (a floor and its
# trim) rather than a separate storey.
STOREY_GAP = 12.0


def load_images():
    """Background Blender leaves image pixels unloaded, which renders black."""
    for image in bpy.data.images:
        if image.source == "FILE":
            try:
                len(image.pixels)
            except Exception:
                pass


def flatten_liquids():
    """Make imported liquid an opaque dark surface.

    The liquid comes in set up as glass - transmission, low roughness, strong
    specular. Under a shadowless lamp that refracts the bright environment and
    burns out to a solid white sheet over the arena.
    """
    for material in bpy.data.materials:
        if "liquid" not in material.name.lower() or not material.use_nodes:
            continue

        tree = material.node_tree
        principled = next((n for n in tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if not principled:
            continue

        for link in list(tree.links):
            if link.to_socket == principled.inputs["Alpha"]:
                tree.links.remove(link)

        principled.inputs["Alpha"].default_value = 1.0
        principled.inputs["Base Color"].default_value = (0.05, 0.16, 0.13, 1.0)
        principled.inputs["Roughness"].default_value = 0.5

        for socket_name, value in (("Transmission Weight", 0.0),
                                   ("Specular IOR Level", 0.2),
                                   ("Metallic", 0.0),
                                   ("Coat Weight", 0.0),
                                   ("Emission Strength", 0.0)):
            socket = principled.inputs.get(socket_name)
            if socket and not socket.is_linked:
                socket.default_value = value

        material.blend_method = "OPAQUE"


def light_scene(scene):
    """Dim background, one shadowless sun pointing straight down.

    A bright sky shows through open sightlines and blows out to white next to
    the stone. The world is rebuilt rather than edited, because a file can
    arrive with nothing but an output node and then renders white. These arenas
    are roofed, so a shadow-casting sun never reaches the floor; angled fills
    rake across wall tops and blow them out.
    """
    if not scene.world:
        scene.world = bpy.data.worlds.new("World")
    scene.world.use_nodes = True

    world_tree = scene.world.node_tree
    world_tree.nodes.clear()
    world_output = world_tree.nodes.new("ShaderNodeOutputWorld")
    background = world_tree.nodes.new("ShaderNodeBackground")
    background.inputs["Color"].default_value = (0.16, 0.17, 0.19, 1.0)
    background.inputs["Strength"].default_value = 1.0
    world_tree.links.new(background.outputs["Background"], world_output.inputs["Surface"])

    light_data = bpy.data.lights.new("ArenaKey", type="SUN")
    light_data.energy = 1.6
    light_data.use_shadow = False
    light = bpy.data.objects.new("ArenaKey", light_data)
    light.rotation_euler = (0.0, 0.0, 0.0)
    scene.collection.objects.link(light)


def show_wmo_liquid(visible):
    """Show or hide the liquid import_wmo_liquid.py built.

    Its surfaces sit under one WMO_Liquids empty. Some arenas render better
    without them: over Sszorak's ring they come out as white sheets.
    """
    parent = bpy.data.objects.get("WMO_Liquids")
    if parent is None:
        return
    for child in parent.children:
        child.hide_render = not visible


def make_camera(scene):
    camera_data = bpy.data.cameras.new("ArenaCam")
    camera_data.type = "ORTHO"
    camera_data.clip_start = 0.5
    camera_data.clip_end = 6000.0
    camera = bpy.data.objects.new("ArenaCam", camera_data)
    camera.rotation_euler = (0.0, 0.0, 0.0)
    scene.collection.objects.link(camera)
    scene.camera = camera
    return camera


def prepare(resolution):
    """Apply every fix and return (scene, camera)."""
    scene = bpy.context.scene
    load_images()
    flatten_liquids()
    light_scene(scene)
    camera = make_camera(scene)

    scene.view_settings.view_transform = "Standard"
    scene.view_settings.exposure = 0.0
    scene.render.resolution_x = resolution
    scene.render.resolution_y = resolution
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    return scene, camera


def floors_at(scene, x, y):
    """Heights of every upward-facing building surface under a point.

    Only upward-facing hits count: the undersides of balconies overhead also
    register, and terrain (adt_ objects) is below the building.
    """
    depsgraph = bpy.context.evaluated_depsgraph_get()
    floors = []
    origin = Vector((x, y, 5000.0))
    down = Vector((0.0, 0.0, -1.0))
    for _ in range(64):
        ok, loc, normal, _index, obj, _matrix = scene.ray_cast(depsgraph, origin, down)
        if not ok:
            break
        if not obj.name.startswith("adt_") and normal.z > 0.5:
            floors.append(loc.z)
        origin = loc + down * 0.05
    return floors


def camera_height(scene, x, y, expected_floor=None):
    """(floor, camera z) looking down on the arena from inside its room.

    These dungeons stack rooms, so the floor is the one nearest the expected
    height when there is one, otherwise the highest. The camera sits three
    quarters of the way up to the next storey so the ceiling is not in shot.
    """
    floors = floors_at(scene, x, y)
    if not floors:
        return None, None

    if expected_floor is not None:
        floor = min(floors, key=lambda z: abs(z - expected_floor))
    else:
        floor = max(floors)

    ceiling = next((z for z in sorted(floors) if z > floor + STOREY_GAP), None)
    if ceiling is None:
        return floor, floor + 80.0
    return floor, floor + (ceiling - floor) * 0.75


def map_file_name(encounter_id, x, y, scale):
    """encounter_{id}_x{X}_y{Y}_s{scale}.png - AwowLab reads the framing back
    from the name with stof, so keep the numbers plain integers."""
    return (f"encounter_{encounter_id}"
            f"_x{int(round(x))}_y{int(round(y))}_s{int(round(scale))}.png")
