"""Brick Breaker models: brick, paddle, ball, wall_side, wall_top, life.

Build: blender -b --factory-startup --python brick_breaker.py -- [model ...]
Preview: blender -b --factory-startup --python brick_breaker.py -- --preview <dir>

Sizes are in engine axes (X width, Y height, Z depth). In Blender Z is up and engine depth
runs along Blender Y; export_yup turns Blender (x, y, z) into engine (x, z, -y).
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

ART_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

BRICK_WHITE = (1.0, 1.0, 1.0, 1.0)
PADDLE_BLUE = (0.15, 0.35, 0.85, 1.0)
PADDLE_CAP = (0.85, 0.9, 0.95, 1.0)
BALL_WHITE = (1.0, 1.0, 1.0, 1.0)
WALL_SLATE = (0.35, 0.42, 0.52, 1.0)
LIFE_RED = (0.85, 0.12, 0.12, 1.0)


# ---------------------------------------------------------------- common

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, color):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = color
    mat.diffuse_color = color
    return mat


def mesh_object(name, bm, mat, smooth=False):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    for poly in mesh.polygons:
        poly.use_smooth = smooth
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(mat)
    return obj


def box(width, depth, height, center=(0.0, 0.0, 0.0), bevel=0.0):
    """Box in Blender axes: width X, depth Y, height Z, optional bevel on every edge."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(width, depth, height), verts=bm.verts)
    bmesh.ops.translate(bm, vec=center, verts=bm.verts)
    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=bm.edges[:] + bm.verts[:], offset=bevel, segments=1,
                        affect='EDGES', profile=0.5)
    return bm


def join(objects, name):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    result = bpy.context.view_layer.objects.active
    result.name = name
    result.data.name = name
    return result


def export(name, obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    path = os.path.join(ART_DIR, name + ".gltf")
    bpy.ops.export_scene.gltf(
        filepath=path, export_format='GLTF_SEPARATE', use_selection=True,
        export_yup=True, export_apply=True, export_texcoords=False, export_normals=True,
        export_materials='EXPORT', export_draco_mesh_compression_enable=False,
        export_animations=False, export_skins=False, export_lights=False, export_cameras=False)
    print_bounds(name, obj)


def print_bounds(name, obj):
    xs = [v.co.x for v in obj.data.vertices]
    ys = [v.co.y for v in obj.data.vertices]
    zs = [v.co.z for v in obj.data.vertices]
    obj.data.calc_loop_triangles()
    tris = len(obj.data.loop_triangles)
    # Engine axes: X = x, Y = z, Z = -y
    print(f"[{name}] engine AABB X {min(xs):.3f}..{max(xs):.3f}  Y {min(zs):.3f}..{max(zs):.3f}  "
          f"Z {-max(ys):.3f}..{-min(ys):.3f}  size {max(xs)-min(xs):.3f} x {max(zs)-min(zs):.3f} x "
          f"{max(ys)-min(ys):.3f}  triangles {tris}")


# ---------------------------------------------------------------- models

def make_brick():
    """1.0 x 0.4 x 0.5, origin at the volume center. Neutral color: the node color tints it."""
    obj = mesh_object("brick", box(1.0, 0.5, 0.4, bevel=0.04), material("brick", BRICK_WHITE))
    export("brick", obj)


def half_disc_cap(side, radius, height, segments):
    """Half cylinder with a vertical axis at x = side * 0.8, bulging outwards along X."""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments * 2,
                          radius1=radius, radius2=radius, depth=height)
    result = bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:],
                                    plane_co=(0.0, 0.0, 0.0), plane_no=(side, 0.0, 0.0),
                                    clear_inner=True)
    bmesh.ops.contextual_create(bm, geom=[e for e in result["geom_cut"] if isinstance(e, bmesh.types.BMEdge)])
    bmesh.ops.translate(bm, vec=(side * 0.8, 0.0, 0.0), verts=bm.verts)
    return bm


def make_paddle():
    """2.0 x 0.4 x 0.4, origin at the volume center: blue body, light rounded ends."""
    body = mesh_object("paddle_body", box(1.6, 0.4, 0.4), material("paddle_body", PADDLE_BLUE))
    cap_mat = material("paddle_cap", PADDLE_CAP)
    caps = [mesh_object(f"paddle_cap_{side}", half_disc_cap(side, 0.2, 0.4, 6), cap_mat) for side in (-1, 1)]
    export("paddle", join([body] + caps, "paddle"))


def make_ball():
    """Sphere 0.4 across, origin at the center, smooth normals."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=10, radius=0.2)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    export("ball", mesh_object("ball", bm, material("ball", BALL_WHITE), smooth=True))


def make_wall_side():
    """0.5 x 0.6 x 16.5, origin at the bottom center."""
    obj = mesh_object("wall_side", box(0.5, 16.5, 0.6, center=(0.0, 0.0, 0.3)), material("wall", WALL_SLATE))
    export("wall_side", obj)


def make_wall_top():
    """11.0 x 0.6 x 0.5, origin at the bottom center."""
    obj = mesh_object("wall_top", box(11.0, 0.5, 0.6, center=(0.0, 0.0, 0.3)), material("wall", WALL_SLATE))
    export("wall_top", obj)


def make_life():
    """Cube 0.4 with beveled edges, origin at the bottom center."""
    obj = mesh_object("life", box(0.4, 0.4, 0.4, center=(0.0, 0.0, 0.2), bevel=0.06), material("life", LIFE_RED))
    export("life", obj)


MODELS = {
    "brick": make_brick,
    "paddle": make_paddle,
    "ball": make_ball,
    "wall_side": make_wall_side,
    "wall_top": make_wall_top,
    "life": make_life,
}


# ---------------------------------------------------------------- preview

PREVIEW_LAYOUT = {
    # Blender location of each model in the preview, a small arena around the small models
    "wall_side": [(-5.25, -2.0, 0.0), (5.25, -2.0, 0.0)],
    "wall_top": [(0.0, 6.5, 0.0)],
    "brick": [(-1.2, 3.0, 0.2), (0.0, 3.0, 0.2), (1.2, 3.0, 0.2)],
    "paddle": [(0.0, -8.0, 0.2)],
    "ball": [(0.0, -6.0, 0.2)],
    "life": [(-4.0, -9.5, 0.0), (-3.4, -9.5, 0.0), (-2.8, -9.5, 0.0)],
}


def look_at(cam, target):
    direction = Vector(target) - cam.location
    cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()


def render_preview(out_dir):
    reset_scene()
    os.makedirs(out_dir, exist_ok=True)
    for name, locations in PREVIEW_LAYOUT.items():
        for location in locations:
            bpy.ops.import_scene.gltf(filepath=os.path.join(ART_DIR, name + ".gltf"))
            for obj in bpy.context.selected_objects:
                obj.location = location
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'MATERIAL'
    scene.display.shading.show_cavity = False
    scene.display.shading.show_object_outline = True
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("preview")
    world.color = (1.0, 1.0, 1.0)
    scene.world = world
    scene.view_settings.view_transform = 'Standard'
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 1200
    cam = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    views = {
        "three_quarter": ((9.0, -18.0, 12.0), (0.0, -1.5, 0.0), 'PERSP'),
        "top": ((0.0, -1.5, 30.0), (0.0, -1.5, 0.0), 'ORTHO'),
        "close": ((2.5, -11.5, 2.2), (-0.8, -7.5, 0.2), 'PERSP'),
    }
    for view, (location, target, kind) in views.items():
        cam.location = location
        look_at(cam, target)
        cam.data.type = kind
        cam.data.ortho_scale = 18.0
        scene.render.filepath = os.path.join(out_dir, f"brick_breaker_{view}.png")
        bpy.ops.render.render(write_still=True)


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if args[:1] == ["--preview"]:
        render_preview(args[1] if len(args) > 1 else os.path.join(ART_DIR, "blender", "preview"))
        return
    for name in args or MODELS:
        reset_scene()
        MODELS[name]()


main()
