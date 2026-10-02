"""Run with: blender -b -P build_island_blender.py -- <build_dir>

Builds the Little St. James 3D model from prepare_island.py output, renders previews and exports a GLB.
"""
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector

build = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "build"
data = json.load(open(os.path.join(build, "island.json")))
heights = np.load(os.path.join(build, "height.npy"))

XMIN, YMIN, SIZE, STEP, CELLS = data["xmin"], data["ymin"], data["size"], data["step"], data["cells"]
CX, CY = XMIN + SIZE / 2, YMIN + SIZE / 2
Z_SCALE = 1.6

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


def material(name, rgb, rough=0.8, metal=0.0, alpha=1.0, image=None):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if alpha < 1.0:
        bsdf.inputs["Alpha"].default_value = alpha
        mat.blend_method = "BLEND"
    if image:
        tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(image)
        mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def srgb(r, g, b):
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return (lin(r), lin(g), lin(b))


M_GROUND = material("Ground", (1, 1, 1), 0.95, image=os.path.abspath(os.path.join(build, "ground.png")))
M_WATER = material("Water", srgb(40, 150, 175), 0.05, alpha=0.45)
M_WALL = material("Wall", srgb(236, 230, 214), 0.85)
M_ROOF = material("Roof", srgb(64, 168, 176), 0.5, 0.2)
M_SHED_ROOF = material("ShedRoof", srgb(120, 128, 132), 0.6, 0.3)
M_STRIPE_BLUE = material("TempleBlue", srgb(40, 96, 190), 0.6)
M_STRIPE_WHITE = material("TempleWhite", srgb(240, 240, 236), 0.6)
M_GOLD = material("Gold", srgb(222, 176, 70), 0.3, 1.0)
M_WOOD = material("Wood", srgb(140, 100, 64), 0.8)
M_TRIM = material("Trim", srgb(250, 248, 240), 0.7)
M_PLINTH = material("Plinth", srgb(168, 156, 140), 0.9)
M_GLASS = material("Glass", srgb(28, 44, 62), 0.15, 0.3)
M_DOOR = material("Door", srgb(112, 74, 46), 0.7)
M_SHUTTER = material("Shutter", srgb(46, 132, 142), 0.6)
M_GARAGE = material("Garage", srgb(150, 156, 162), 0.5, 0.4)

W_WALL, W_ROOF, W_SHED_ROOF, W_TRIM, W_PLINTH, W_GLASS, W_DOOR, W_SHUTTER, W_GARAGE = range(9)
BUILDING_MATS = [M_WALL, M_ROOF, M_SHED_ROOF, M_TRIM, M_PLINTH, M_GLASS, M_DOOR, M_SHUTTER, M_GARAGE]


def add_object(name, bm, mats):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    for m in mats:
        mesh.materials.append(m)
    scene.collection.objects.link(obj)
    return obj


def build_terrain():
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new()
    verts = []
    for j in range(CELLS):
        row = []
        for i in range(CELLS):
            x = XMIN + i * STEP - CX
            y = YMIN + j * STEP - CY
            row.append(bm.verts.new((x, y, float(heights[j, i]) * Z_SCALE)))
        verts.append(row)
    for j in range(CELLS - 1):
        for i in range(CELLS - 1):
            f = bm.faces.new((verts[j][i], verts[j][i + 1], verts[j + 1][i + 1], verts[j + 1][i]))
            f.smooth = True
            for loop in f.loops:
                co = loop.vert.co
                loop[uv].uv = ((co.x + CX - XMIN) / SIZE, (co.y + CY - YMIN) / SIZE)
    add_object("Terrain", bm, [M_GROUND])

    wbm = bmesh.new()
    s = SIZE * 0.5
    wv = [wbm.verts.new((x, y, 0.0)) for x, y in ((-s, -s), (s, -s), (s, s), (-s, s))]
    wbm.faces.new(wv)
    add_object("Water", wbm, [M_WATER])


def ring_local(ring):
    pts = [(x - CX, y - CY) for x, y in ring]
    area = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
    return pts if area > 0 else pts[::-1]


def prism(bm, pts, z0, z1, mat_index, cap_top=True, cap_bottom=True):
    bottom = [bm.verts.new((x, y, z0)) for x, y in pts]
    top = [bm.verts.new((x, y, z1)) for x, y in pts]
    n = len(pts)
    for i in range(n):
        f = bm.faces.new((bottom[i], bottom[(i + 1) % n], top[(i + 1) % n], top[i]))
        f.material_index = mat_index
    faces = []
    if cap_top:
        try:
            ft = bm.faces.new(top)
            ft.material_index = mat_index
            faces.append(ft)
        except ValueError:
            pass
    if cap_bottom:
        try:
            fb = bm.faces.new(bottom[::-1])
            fb.material_index = mat_index
        except ValueError:
            pass
    return faces


def ccw(pts):
    area = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
    return pts if area > 0 else pts[::-1]


def offset_poly(pts, d):
    """Miter offset of a CCW polygon; positive d grows it outward."""
    n = len(pts)
    out = []
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        n1 = edge_normal(p0, p1)
        n2 = edge_normal(p1, p2)
        bx, by = n1[0] + n2[0], n1[1] + n2[1]
        bl = math.hypot(bx, by)
        if bl < 1e-6:
            out.append((p1[0] + n1[0] * d, p1[1] + n1[1] * d))
            continue
        bx, by = bx / bl, by / bl
        cos = max(0.4, bx * n1[0] + by * n1[1])
        out.append((p1[0] + bx * d / cos, p1[1] + by * d / cos))
    return out


def edge_normal(a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    ln = math.hypot(dx, dy) or 1.0
    return (dy / ln, -dx / ln)


def wall_box(bm, c, u, nrm, width, d_out, d_in, z0, z1, mat):
    hw = width / 2
    pts = [
        (c[0] - u[0] * hw - nrm[0] * d_in, c[1] - u[1] * hw - nrm[1] * d_in),
        (c[0] + u[0] * hw - nrm[0] * d_in, c[1] + u[1] * hw - nrm[1] * d_in),
        (c[0] + u[0] * hw + nrm[0] * d_out, c[1] + u[1] * hw + nrm[1] * d_out),
        (c[0] - u[0] * hw + nrm[0] * d_out, c[1] - u[1] * hw + nrm[1] * d_out),
    ]
    prism(bm, ccw(pts), z0, z1, mat)


def facade(bm, pts, base, floors, fh, style):
    """Windows, sills, shutters and a front door on every wall long enough to hold them."""
    n = len(pts)
    lengths = [math.dist(pts[i], pts[(i + 1) % n]) for i in range(n)]
    front = max(range(n), key=lambda i: lengths[i])
    for i in range(n):
        a, b, ln = pts[i], pts[(i + 1) % n], lengths[i]
        if ln < 2.4:
            continue
        u = ((b[0] - a[0]) / ln, (b[1] - a[1]) / ln)
        nrm = (u[1], -u[0])
        count = max(1, int(ln // (4.5 if style == "shed" else 3.2)))
        for f in range(floors):
            zf = base + f * fh
            for k in range(count):
                t = (k + 0.5) / count
                c = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                if f == 0 and i == front and k == count // 2:
                    if style == "shed":
                        wall_box(bm, c, u, nrm, min(3.2, ln * 0.6), 0.06, 0.1, zf, zf + fh * 0.75, W_GARAGE)
                    else:
                        wall_box(bm, c, u, nrm, 1.5, 0.06, 0.1, zf, zf + min(2.4, fh * 0.8), W_DOOR)
                        wall_box(bm, c, u, nrm, 2.0, 0.12, 0.0, zf + min(2.4, fh * 0.8), zf + min(2.6, fh * 0.88), W_TRIM)
                        wall_box(bm, c, u, nrm, 2.4, 0.9, 0.0, zf - 0.35, zf + 0.05, W_PLINTH)
                    continue
                w0, w1 = zf + fh * 0.32, zf + fh * 0.78
                wall_box(bm, c, u, nrm, 1.1, 0.05, 0.1, w0, w1, W_GLASS)
                wall_box(bm, c, u, nrm, 1.4, 0.16, 0.0, w0 - 0.14, w0, W_TRIM)
                if style == "house":
                    for s in (-1, 1):
                        sc = (c[0] + u[0] * s * 0.78, c[1] + u[1] * s * 0.78)
                        wall_box(bm, sc, u, nrm, 0.42, 0.09, 0.0, w0, w1, W_SHUTTER)


def simplify(pts, min_turn_deg=10):
    out = list(pts)
    changed = True
    while changed and len(out) > 3:
        changed = False
        for i in range(len(out)):
            a, b, c = out[i - 1], out[i], out[(i + 1) % len(out)]
            ab = (b[0] - a[0], b[1] - a[1])
            bc = (c[0] - b[0], c[1] - b[1])
            la, lb = math.hypot(*ab), math.hypot(*bc)
            if la < 0.3 or lb < 0.3:
                del out[i]
                changed = True
                break
            cross = (ab[0] * bc[1] - ab[1] * bc[0]) / (la * lb)
            dot = (ab[0] * bc[0] + ab[1] * bc[1]) / (la * lb)
            if abs(math.degrees(math.atan2(cross, dot))) < min_turn_deg:
                del out[i]
                changed = True
                break
    return out


def hip_roof_poly(bm, eave, z, rise, mat):
    """Real hip roof with a ridge for rectangles; truncated pyramid for any other footprint."""
    pts = simplify(eave)
    n = len(pts)
    V = lambda p, h=0.0: bm.verts.new((p[0], p[1], z + h))
    faces = []
    rect = n == 4 and all(
        abs((pts[(i + 1) % 4][0] - pts[i][0]) * (pts[i - 1][0] - pts[i][0]) + (pts[(i + 1) % 4][1] - pts[i][1]) * (pts[i - 1][1] - pts[i][1]))
        < 0.25 * math.dist(pts[i], pts[(i + 1) % 4]) * math.dist(pts[i], pts[i - 1])
        for i in range(4)
    )
    if rect:
        if math.dist(pts[0], pts[1]) < math.dist(pts[1], pts[2]):
            pts = pts[1:] + pts[:1]
        p = [V(q) for q in pts]
        length, width = math.dist(pts[0], pts[1]), math.dist(pts[1], pts[2])
        cx = sum(q[0] for q in pts) / 4
        cy = sum(q[1] for q in pts) / 4
        ux, uy = (pts[1][0] - pts[0][0]) / length, (pts[1][1] - pts[0][1]) / length
        half = (length - width) / 2
        h = min(rise, width * 0.32)
        r0 = V((cx - ux * half, cy - uy * half), h)
        r1 = V((cx + ux * half, cy + uy * half), h)
        if half > 0.05:
            faces = [(p[0], p[1], r1, r0), (p[1], p[2], r1), (p[2], p[3], r0, r1), (p[3], p[0], r0)]
        else:
            faces = [(p[0], p[1], r1), (p[1], p[2], r1), (p[2], p[3], r1), (p[3], p[0], r1)]
            bm.verts.remove(r0)
    else:
        cx = sum(q[0] for q in pts) / n
        cy = sum(q[1] for q in pts) / n
        p = [V(q) for q in pts]
        t = [V((cx + (q[0] - cx) * 0.2, cy + (q[1] - cy) * 0.2), rise) for q in pts]
        faces = [(p[i], p[(i + 1) % n], t[(i + 1) % n], t[i]) for i in range(n)] + [tuple(t)]
    for f in faces:
        try:
            bm.faces.new(f).material_index = mat
        except ValueError:
            pass


def build_house(bm, pts, b, style):
    base, h = b["base"], b["height"]
    z0, z1 = base - 0.6, base + h
    floors = max(1, round(h / 3.1))
    fh = h / floors
    prism(bm, offset_poly(pts, 0.25), z0, base + 0.35, W_PLINTH)
    prism(bm, pts, z0, z1, W_WALL, cap_top=False, cap_bottom=False)
    for f in range(1, floors):
        zf = base + f * fh
        prism(bm, offset_poly(pts, 0.14), zf - 0.12, zf + 0.12, W_TRIM)
    facade(bm, pts, base, floors, fh, style)

    eave = offset_poly(pts, 0.7)
    xs = [p[0] for p in eave]
    ys = [p[1] for p in eave]
    span = min(max(xs) - min(xs), max(ys) - min(ys))
    prism(bm, eave, z1, z1 + 0.25, W_TRIM)
    hip_roof_poly(bm, eave, z1 + 0.25, min(6.0, span * 0.3), W_SHED_ROOF if style == "shed" else W_ROOF)


def build_canopy(bm, pts, b):
    z1 = b["base"] + b["height"]
    for p in pts:
        wall_box(bm, p, (1, 0), (0, 1), 0.3, 0.15, 0.15, b["base"] - 0.5, z1 - 0.3, W_TRIM)
    eave = offset_poly(pts, 0.4)
    xs = [p[0] for p in eave]
    ys = [p[1] for p in eave]
    span = min(max(xs) - min(xs), max(ys) - min(ys))
    prism(bm, eave, z1 - 0.3, z1, W_TRIM)
    hip_roof_poly(bm, eave, z1, min(3.0, span * 0.25), W_ROOF)


def build_ruins(bm, pts, b):
    base, h = b["base"], b["height"]
    inner = offset_poly(pts, -0.35)
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        top = base + h * (0.45 + 0.55 * ((i * 7919) % 5) / 4)
        quad = [pts[i], pts[j], inner[j], inner[i]]
        prism(bm, ccw(quad), base - 0.5, top, W_PLINTH)


def add_primitive(name, mat, smooth=False):
    obj = bpy.context.active_object
    obj.name = name
    obj.data.materials.append(mat)
    if smooth:
        bpy.ops.object.shade_smooth()
    return obj


def build_temple(pts, b):
    base, h = b["base"], b["height"]
    tbm = bmesh.new()
    T_WHITE, T_BLUE, T_TRIM, T_PLINTH, T_DOOR, T_GLASS = range(6)
    prism(tbm, offset_poly(pts, 1.6), base - 0.8, base - 0.1, T_PLINTH)
    prism(tbm, offset_poly(pts, 0.9), base - 0.1, base + 0.4, T_TRIM)
    bands = 10
    band_h = h / bands
    for k in range(bands):
        prism(tbm, pts, base + k * band_h, base + (k + 1) * band_h, k % 2, cap_top=False, cap_bottom=False)
    prism(tbm, offset_poly(pts, 0.35), base + h, base + h + 0.6, T_TRIM)
    prism(tbm, offset_poly(pts, 0.15), base + h + 0.6, base + h + 1.0, T_BLUE)

    n = len(pts)
    lengths = [math.dist(pts[i], pts[(i + 1) % n]) for i in range(n)]
    front = max(range(n), key=lambda i: lengths[i])
    for i in range(n):
        a, c2, ln = pts[i], pts[(i + 1) % n], lengths[i]
        if ln < 3:
            continue
        u = ((c2[0] - a[0]) / ln, (c2[1] - a[1]) / ln)
        nrm = (u[1], -u[0])
        mid = ((a[0] + c2[0]) / 2, (a[1] + c2[1]) / 2)
        if i == front:
            wall_box(tbm, mid, u, nrm, 2.2, 0.08, 0.1, base + 0.4, base + h * 0.62, T_DOOR)
            wall_box(tbm, mid, u, nrm, 3.0, 0.2, 0.0, base + h * 0.62, base + h * 0.7, T_TRIM)
            for s in (-1, 1):
                pc = (mid[0] + u[0] * s * 1.9, mid[1] + u[1] * s * 1.9)
                wall_box(tbm, pc, u, nrm, 0.5, 0.6, 0.0, base + 0.4, base + h * 0.7, T_TRIM)
        else:
            count = max(1, int(ln // 3.5))
            for k in range(count):
                t = (k + 0.5) / count
                c = (a[0] + (c2[0] - a[0]) * t, a[1] + (c2[1] - a[1]) * t)
                wall_box(tbm, c, u, nrm, 0.9, 0.06, 0.1, base + h * 0.35, base + h * 0.75, T_GLASS)
                wall_box(tbm, c, u, nrm, 1.3, 0.18, 0.0, base + h * 0.75, base + h * 0.8, T_TRIM)
    add_object("Temple", tbm, [M_STRIPE_WHITE, M_STRIPE_BLUE, M_TRIM, M_PLINTH, M_DOOR, M_GLASS])

    cx = sum(p[0] for p in pts) / n
    cy = sum(p[1] for p in pts) / n
    r = 0.3 * min(max(p[0] for p in pts) - min(p[0] for p in pts), max(p[1] for p in pts) - min(p[1] for p in pts))
    top = base + h + 1.0
    bpy.ops.mesh.primitive_cylinder_add(radius=r * 1.05, depth=r * 0.7, location=(cx, cy, top + r * 0.35), vertices=24)
    add_primitive("TempleDrum", M_STRIPE_WHITE)
    bpy.ops.mesh.primitive_cylinder_add(radius=r * 1.15, depth=r * 0.12, location=(cx, cy, top + r * 0.72), vertices=24)
    add_primitive("TempleDrumRing", M_GOLD)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r, location=(cx, cy, top + r * 0.75), segments=32, ring_count=16)
    add_primitive("TempleDome", M_GOLD, smooth=True)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=r * 0.16, location=(cx, cy, top + r * 1.85), segments=16, ring_count=8)
    add_primitive("TempleKnob", M_GOLD, smooth=True)
    bpy.ops.mesh.primitive_cone_add(radius1=r * 0.1, depth=r * 0.9, location=(cx, cy, top + r * 2.4))
    add_primitive("TempleSpire", M_GOLD)
    corners = pts if n <= 8 else pts[:: max(1, n // 4)]
    for k, p in enumerate(corners):
        bpy.ops.mesh.primitive_cone_add(radius1=0.45, depth=1.6, location=(p[0], p[1], top + 0.8), vertices=8)
        add_primitive(f"TemplePinnacle{k}", M_GOLD)


def build_buildings():
    bm = bmesh.new()
    for b in data["buildings"]:
        pts = ring_local(b["ring"])
        if len(pts) < 3:
            continue
        b["base"] *= Z_SCALE
        kind = b["kind"]
        if kind == "temple":
            build_temple(pts, b)
        elif kind == "roof":
            build_canopy(bm, pts, b)
        elif kind == "ruins":
            build_ruins(bm, pts, b)
        else:
            build_house(bm, pts, b, "shed" if kind == "shed" else "house")
    obj = add_object("Buildings", bm, BUILDING_MATS)
    obj.data.polygons.foreach_set("use_smooth", [False] * len(obj.data.polygons))


def build_piers():
    bm = bmesh.new()
    for line in data["piers"]:
        for a, b in zip(line, line[1:]):
            ax, ay = a[0] - CX, a[1] - CY
            bx, by = b[0] - CX, b[1] - CY
            dx, dy = bx - ax, by - ay
            ln = math.hypot(dx, dy)
            if ln < 0.1:
                continue
            nx, ny = -dy / ln * 1.5, dx / ln * 1.5
            pts = [(ax + nx, ay + ny), (ax - nx, ay - ny), (bx - nx, by - ny), (bx + nx, by + ny)]
            prism(bm, ring_local([(p[0] + CX, p[1] + CY) for p in pts]), 0.4, 0.9, 0)
    add_object("Piers", bm, [M_WOOD])


def setup_render():
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 40
    scene.cycles.use_denoising = False
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 900
    scene.view_settings.view_transform = "Standard"

    world = bpy.data.worlds.new("Sky")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.42, 0.62, 0.9, 1.0)
    bg.inputs["Strength"].default_value = 0.6
    scene.world = world

    sun_data = bpy.data.lights.new("Sun", "SUN")
    sun_data.energy = 3.2
    sun_data.angle = math.radians(1.5)
    sun = bpy.data.objects.new("Sun", sun_data)
    sun.rotation_euler = (math.radians(52), 0, math.radians(210))
    scene.collection.objects.link(sun)


def look_at(obj, target):
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def render(name, loc, target, lens):
    cam_data = bpy.data.cameras.new(name)
    cam_data.lens = lens
    cam_data.clip_end = 5000
    cam = bpy.data.objects.new(name, cam_data)
    cam.location = loc
    scene.collection.objects.link(cam)
    look_at(cam, target)
    scene.camera = cam
    scene.render.filepath = os.path.abspath(os.path.join(build, f"{name}.png"))
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam)


build_terrain()
build_buildings()
build_piers()
setup_render()

temple = bpy.data.objects.get("Temple")
tc = temple.data.vertices[0].co if temple else Vector((0, 0, 0))

render("render_overview", (-520, -620, 480), (0, 20, 0), 38)
render("render_temple", (tc.x - 70, tc.y - 90, tc.z + 55), (tc.x, tc.y, tc.z + 4), 50)
render("render_top", (0, 0, 1400), (0, 0.01, 0), 50)

houses = [b for b in data["buildings"] if b["kind"] == "yes"]
main = max(houses, key=lambda b: abs(sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(b["ring"], b["ring"][1:] + b["ring"][:1]))))
hx = sum(p[0] for p in main["ring"]) / len(main["ring"]) - CX
hy = sum(p[1] for p in main["ring"]) / len(main["ring"]) - CY
render("render_houses", (hx - 45, hy - 60, main["base"] + 40), (hx, hy, main["base"] + 3), 40)

bpy.ops.export_scene.gltf(
    filepath=os.path.abspath(os.path.join(build, "little_st_james.glb")),
    export_format="GLB",
    export_image_format="JPEG",
    export_cameras=False,
    export_lights=False,
)
bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(os.path.join(build, "little_st_james.blend")))
print("DONE")
