#!/usr/bin/env python3
"""sundog: generate scenes/05-bunny-swarm.json.

RT-core throughput benchmark: a 16x16x16 lattice of 4096 Stanford bunny
instances sharing one mesh (the renderer shares the GAS across instances),
with seeded jitter and per-instance rotation, 8 cycling materials, two large
rect area lights and a dark gradient sky.

Stdlib only. Run: python3 scripts/gen_swarm.py
"""

import json
import os
import random

N = 16                 # lattice size per axis -> N^3 instances
SPACING = 2.2          # lattice pitch (bunny is ~1.6 units tall at scale 1)
SCALE = 0.85           # per-instance uniform scale
JITTER = 0.45          # max +/- positional jitter per axis
FOOT_Y = 0.021         # bunny.obj rests at y = 0.021 (feet offset)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "scenes", "05-bunny-swarm.json")

random.seed(5090)

# --- 8 cycling materials: hue-varied lambert/metal mix + glass -------------
materials = {
    "lam_red":    {"type": "lambert", "color": [0.75, 0.22, 0.18]},
    "lam_green":  {"type": "lambert", "color": [0.25, 0.65, 0.30]},
    "lam_blue":   {"type": "lambert", "color": [0.22, 0.38, 0.80]},
    "met_gold":   {"type": "metal", "color": [1.00, 0.78, 0.34], "roughness": 0.15},
    "met_copper": {"type": "metal", "color": [0.95, 0.54, 0.38], "roughness": 0.30},
    "met_steel":  {"type": "metal", "color": [0.75, 0.78, 0.82], "roughness": 0.08},
    "glass":      {"type": "dielectric", "ior": 1.5},
    "lam_amber":  {"type": "lambert", "color": [0.85, 0.60, 0.20]},
}
mat_cycle = list(materials.keys())

# --- lattice ----------------------------------------------------------------
extent = (N - 1) * SPACING          # 33.0 for N=16
half = extent / 2.0                 # lattice centered on origin in x/z

objects = []
for iy in range(N):
    for iz in range(N):
        for ix in range(N):
            i = (iy * N + iz) * N + ix
            jx = random.uniform(-JITTER, JITTER)
            jy = random.uniform(-JITTER, JITTER)
            jz = random.uniform(-JITTER, JITTER)
            rot = random.uniform(0.0, 360.0)
            x = ix * SPACING - half + jx
            y = iy * SPACING + jy - FOOT_Y * SCALE
            z = iz * SPACING - half + jz
            objects.append({
                "shape": "mesh:bunny",
                "material": mat_cycle[i % len(mat_cycle)],
                "transform": [
                    {"scale": SCALE},
                    {"rotate_y": round(rot, 2)},
                    {"translate": [round(x, 3), round(y, 3), round(z, 3)]},
                ],
            })

# --- two large rect area lights above the swarm -----------------------------
top = N * SPACING + 8.0
lights_objects = [
    {"shape": "rect", "material": "keyLight", "nee": True,
     "transform": [{"rotate_x": 180}, {"scale": 10.0},
                   {"translate": [-half * 0.6, top, -half * 0.4]}]},
    {"shape": "rect", "material": "fillLight", "nee": True,
     "transform": [{"rotate_x": 180}, {"scale": 10.0},
                   {"translate": [half * 0.7, top, half * 0.6]}]},
]
materials["keyLight"] = {"type": "emissive", "color": [1.0, 0.93, 0.82],
                         "intensity": 22.0}
materials["fillLight"] = {"type": "emissive", "color": [0.75, 0.85, 1.0],
                          "intensity": 14.0}

# --- camera: from diagonally above, looking into the swarm ------------------
cam_dist = extent * 1.55
scene = {
    "render": {"width": 1920, "height": 1080, "spp": 64, "max_depth": 8,
               "seed": 5090, "clamp": 10},
    "camera": {
        "lookfrom": [cam_dist * 0.75, N * SPACING * 1.35, cam_dist * 0.85],
        "lookat": [0.0, N * SPACING * 0.40, 0.0],
        "vfov": 42,
        "aperture": 0.0,
    },
    "background": {"type": "gradient",
                   "horizon": [0.05, 0.06, 0.09],
                   "zenith": [0.01, 0.01, 0.03]},
    "textures": {},
    "materials": materials,
    "meshes": {"bunny": {"obj": "../assets/bunny.obj", "normals": "smooth"}},
    "objects": lights_objects + objects,
    "lights": [],
}

with open(OUT, "w") as f:
    json.dump(scene, f, indent=1)
    f.write("\n")

print(f"wrote {os.path.normpath(OUT)}: {len(objects)} bunny instances, "
       f"{len(mat_cycle)} materials, {len(lights_objects)} area lights")
