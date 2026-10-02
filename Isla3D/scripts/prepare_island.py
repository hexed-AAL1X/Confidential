#!/usr/bin/env python3
"""Builds terrain + ground texture + building footprints of Little St. James from open data.

Data: OpenStreetMap (ODbL) and AWS Terrain Tiles (terrarium).
Output goes to the directory given as first argument (default: ./build).
"""
import io
import json
import math
import os
import sys
import urllib.request
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import gaussian_filter, map_coordinates

LAT0, LAT1, LON0, LON1 = 18.2945, 18.3045, -64.8325, -64.8195
LAT_C = (LAT0 + LAT1) / 2
LON_C = (LON0 + LON1) / 2
M_PER_DEG_LAT = 110574.0
M_PER_DEG_LON = 111320.0 * math.cos(math.radians(LAT_C))

GRID_STEP = 3.0
TEX_SIZE = 4096
ZOOM = 15
UA = {"User-Agent": "Confidential-game/1.0"}

COLORS = {
    "sea_shallow": (96, 206, 196),
    "sea_deep": (18, 74, 122),
    "land": (108, 126, 70),
    "scrub": (92, 116, 58),
    "grassland": (132, 150, 82),
    "grass": (112, 162, 72),
    "garden": (88, 142, 62),
    "beach": (228, 210, 166),
    "rock": (142, 128, 112),
    "water": (64, 150, 160),
    "wetland": (90, 112, 82),
    "landfill": (150, 140, 120),
    "pool": (58, 192, 224),
    "pitch": (58, 118, 150),
    "helipad": (120, 120, 124),
    "parking": (170, 166, 158),
    "compass": (232, 226, 212),
    "service": (206, 196, 176),
    "footway": (220, 210, 190),
    "path": (182, 162, 122),
    "wall": (196, 190, 180),
    "pier": (150, 112, 72),
}


def to_local(lat, lon):
    return ((lon - LON_C) * M_PER_DEG_LON, (lat - LAT_C) * M_PER_DEG_LAT)


def fetch(url, data=None):
    req = urllib.request.Request(url, data=data, headers=UA)
    with urllib.request.urlopen(req, timeout=90) as r:
        return r.read()


def load_osm(path):
    if not os.path.exists(path):
        url = f"https://api.openstreetmap.org/api/0.6/map?bbox={LON0},{LAT0},{LON1},{LAT1}"
        with open(path, "wb") as f:
            f.write(fetch(url))
    root = ET.parse(path).getroot()
    nodes = {n.get("id"): to_local(float(n.get("lat")), float(n.get("lon"))) for n in root.iter("node")}
    ways = {}
    for w in root.iter("way"):
        pts = [nodes[nd.get("ref")] for nd in w.iter("nd") if nd.get("ref") in nodes]
        tags = {t.get("k"): t.get("v") for t in w.iter("tag")}
        ways[w.get("id")] = (pts, tags)
    relations = []
    for r in root.iter("relation"):
        tags = {t.get("k"): t.get("v") for t in r.iter("tag")}
        outers = [m.get("ref") for m in r.iter("member") if m.get("type") == "way" and m.get("role") in ("outer", "")]
        relations.append((outers, tags))
    return ways, relations


def assemble_rings(segments):
    segs = [list(s) for s in segments if len(s) >= 2]
    rings = []
    while segs:
        ring = segs.pop(0)
        changed = True
        while changed and ring[0] != ring[-1]:
            changed = False
            for i, s in enumerate(segs):
                if s[0] == ring[-1]:
                    ring += s[1:]
                elif s[-1] == ring[-1]:
                    ring += s[::-1][1:]
                elif s[-1] == ring[0]:
                    ring = s + ring[1:]
                elif s[0] == ring[0]:
                    ring = s[::-1] + ring[1:]
                else:
                    continue
                segs.pop(i)
                changed = True
                break
        if len(ring) >= 4:
            rings.append(ring)
    return rings


def load_dem():
    n = 2 ** ZOOM

    def tile_xy(lat, lon):
        x = (np.asarray(lon) + 180) / 360 * n
        y = (1 - np.arcsinh(np.tan(np.radians(lat))) / np.pi) / 2 * n
        return x, y

    x0, y0 = tile_xy(LAT1, LON0)
    x1, y1 = tile_xy(LAT0, LON1)
    tx = range(int(x0), int(x1) + 1)
    ty = range(int(y0), int(y1) + 1)
    big = np.zeros((256 * len(ty), 256 * len(tx)))
    for j, y in enumerate(ty):
        for i, x in enumerate(tx):
            url = f"https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{ZOOM}/{x}/{y}.png"
            img = np.asarray(Image.open(io.BytesIO(fetch(url))).convert("RGB")).astype(float)
            big[j * 256:(j + 1) * 256, i * 256:(i + 1) * 256] = img[..., 0] * 256 + img[..., 1] + img[..., 2] / 256 - 32768

    def sample(lat, lon):
        x, y = tile_xy(lat, lon)
        px = (x - tx[0]) * 256 - 0.5
        py = (y - ty[0]) * 256 - 0.5
        return map_coordinates(big, [py, px], order=3, mode="nearest")

    return sample


class Painter:
    def __init__(self, xmin, ymin, size_m):
        self.xmin, self.ymin, self.size_m = xmin, ymin, size_m
        self.img = Image.new("RGB", (TEX_SIZE, TEX_SIZE), COLORS["land"])
        self.draw = ImageDraw.Draw(self.img)
        self.scale = TEX_SIZE / size_m

    def px(self, p):
        return ((p[0] - self.xmin) * self.scale, (self.ymin + self.size_m - p[1]) * self.scale)

    def poly(self, pts, color):
        if len(pts) >= 3:
            self.draw.polygon([self.px(p) for p in pts], fill=color)

    def line(self, pts, color, width_m):
        if len(pts) >= 2:
            self.draw.line([self.px(p) for p in pts], fill=color, width=max(1, int(width_m * self.scale)), joint="curve")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build"
    os.makedirs(out, exist_ok=True)
    ways, relations = load_osm(os.path.join(out, "osm.xml"))

    island = []
    for outers, tags in relations:
        if tags.get("place") == "island" and "Little Saint James" in tags.get("name", ""):
            island = assemble_rings([ways[w][0] for w in outers if w in ways])
    if not island:
        island = assemble_rings([p for p, t in ways.values() if t.get("natural") == "coastline"])
    island_ring = max(island, key=len)

    xs = [p[0] for p in island_ring]
    ys = [p[1] for p in island_ring]
    margin = 160.0
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    size = max(max(xs) - min(xs), max(ys) - min(ys)) + 2 * margin
    xmin, ymin = cx - size / 2, cy - size / 2

    painter = Painter(xmin, ymin, size)
    mask_img = Image.new("L", (TEX_SIZE, TEX_SIZE), 0)
    ImageDraw.Draw(mask_img).polygon([painter.px(p) for p in island_ring], fill=255)
    land_mask = np.asarray(mask_img) > 0

    sample = load_dem()
    cells = int(size / GRID_STEP) + 1
    gx = xmin + np.arange(cells) * GRID_STEP
    gy = ymin + np.arange(cells) * GRID_STEP
    GX, GY = np.meshgrid(gx, gy)
    lat = LAT_C + GY / M_PER_DEG_LAT
    lon = LON_C + GX / M_PER_DEG_LON
    h = sample(lat.ravel(), lon.ravel()).reshape(GX.shape)
    h = gaussian_filter(h, 1.2)

    mi = ((GX - xmin) * painter.scale).astype(int).clip(0, TEX_SIZE - 1)
    mj = ((ymin + size - GY) * painter.scale).astype(int).clip(0, TEX_SIZE - 1)
    on_land = land_mask[mj, mi]
    from scipy.ndimage import distance_transform_edt
    dist_sea = distance_transform_edt(~on_land) * GRID_STEP
    h = np.where(on_land, np.maximum(h, 0.8), np.maximum(-0.6 - dist_sea * 0.12, -30.0))
    h = gaussian_filter(h, 0.8)

    tex = painter.img
    sea = np.zeros((TEX_SIZE, TEX_SIZE, 3), dtype=np.uint8)
    hh = np.asarray(Image.fromarray(h.astype(np.float32)).resize((TEX_SIZE, TEX_SIZE), Image.BILINEAR))[::-1]
    t = np.clip(-hh / 22.0, 0, 1)[..., None]
    sea[:] = (np.array(COLORS["sea_shallow"]) * (1 - t) + np.array(COLORS["sea_deep"]) * t).astype(np.uint8)
    base = np.where(land_mask[..., None], np.array(COLORS["land"], dtype=np.uint8), sea)
    tex.paste(Image.fromarray(base.astype(np.uint8)))
    painter.draw = ImageDraw.Draw(tex)

    area_order = [
        ("natural", "grassland", "grassland"), ("natural", "scrub", "scrub"), ("landuse", "landfill", "landfill"),
        ("natural", "wetland", "wetland"), ("landuse", "grass", "grass"), ("leisure", "garden", "garden"),
        ("natural", "beach", "beach"), ("natural", "bare_rock", "rock"), ("natural", "scree", "rock"),
        ("natural", "water", "water"), ("natural", "tidepool", "water"), ("amenity", "parking", "parking"),
        ("leisure", "pitch", "pitch"), ("aeroway", "helipad", "helipad"), ("man_made", "compass_rose", "compass"),
        ("leisure", "swimming_pool", "pool"), ("amenity", "fountain", "pool"),
    ]
    rel_polys = []
    for outers, tags in relations:
        if tags.get("type") == "multipolygon":
            for ring in assemble_rings([ways[w][0] for w in outers if w in ways]):
                rel_polys.append((ring, tags))
    all_polys = [(p, t) for p, t in ways.values() if len(p) >= 3 and p[0] == p[-1]] + rel_polys

    for key, value, color in area_order:
        for pts, tags in all_polys:
            if tags.get(key) == value:
                painter.poly(pts, COLORS[color])

    line_styles = {
        "track": ("path", 3.0), "path": ("path", 1.4), "service": ("service", 4.2), "footway": ("footway", 1.6),
        "pedestrian": ("footway", 3.0), "steps": ("footway", 2.0),
    }
    for pts, tags in ways.values():
        if tags.get("highway") in line_styles:
            style, width = line_styles[tags["highway"]]
            painter.line(pts, COLORS[style], width)
        elif tags.get("barrier") in ("wall", "retaining_wall"):
            painter.line(pts, COLORS["wall"], 0.6)
        elif tags.get("barrier") == "hedge":
            painter.line(pts, COLORS["garden"], 1.2)
        elif tags.get("natural") == "cliff":
            painter.line(pts, COLORS["rock"], 2.5)

    def ground_at(x, y):
        fx = (x - xmin) / GRID_STEP
        fy = (y - ymin) / GRID_STEP
        return float(map_coordinates(h, [[fy], [fx]], order=1, mode="nearest")[0])

    buildings = []
    for pts, tags in list(ways.values()) + rel_polys:
        kind = tags.get("building")
        if not kind or len(pts) < 4:
            continue
        ring = pts[:-1] if pts[0] == pts[-1] else pts
        base = min(ground_at(x, y) for x, y in ring)
        height = {"shed": 3.2, "roof": 3.8, "ruins": 2.0, "temple": 7.5}.get(kind, 6.2)
        if "building:levels" in tags:
            try:
                height = 3.4 * float(tags["building:levels"])
            except ValueError:
                pass
        buildings.append({"kind": kind, "name": tags.get("name", ""), "ring": ring, "base": base, "height": height})

    piers = []
    for pts, tags in ways.values():
        if tags.get("man_made") == "pier" and len(pts) >= 2:
            piers.append(pts)
            painter.line(pts, COLORS["pier"], 3.0)

    tex.save(os.path.join(out, "ground.png"))
    np.save(os.path.join(out, "height.npy"), h.astype(np.float32))
    with open(os.path.join(out, "island.json"), "w") as f:
        json.dump({
            "xmin": xmin, "ymin": ymin, "size": size, "step": GRID_STEP, "cells": cells,
            "buildings": buildings, "piers": piers,
            "attribution": "Map data (c) OpenStreetMap contributors (ODbL); elevation: AWS Terrain Tiles",
        }, f)
    print(f"area {size:.0f} m, grid {cells}x{cells}, height {h.min():.1f}..{h.max():.1f} m, "
          f"{len(buildings)} buildings, {len(piers)} piers")


if __name__ == "__main__":
    main()
