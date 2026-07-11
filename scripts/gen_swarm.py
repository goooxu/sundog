#!/usr/bin/env python3
"""sundog: generate scenes/05-spot-swarm.json.

RT-core throughput benchmark: a 32x32x32 lattice of 32,768 Spot (Keenan
Crane's cartoon cow, 5,856 tris) instances sharing one mesh (the renderer
shares the GAS across instances), with seeded jitter and per-instance
rotation, 8 cycling materials (including the native texture), two large rect
area lights and a dark gradient sky. ~192M effective triangles.

Stdlib only. Run: python3 scripts/gen_swarm.py
"""

import json
import os
import random

N = 32                 # lattice size per axis -> N^3 instances
SPACING = 1.9          # lattice pitch (spot is ~1.7 units tall at scale 1)
SCALE = 0.85           # per-instance uniform scale
JITTER = 0.35          # max +/- positional jitter per axis
FOOT_Y = -0.737        # spot.obj feet rest at y = -0.737

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "scenes", "05-spot-swarm.json")

random.seed(5090)

# --- 8 cycling materials: native texture + hue-varied lambert/metal + glass -
materials = {
    "spot_skin":  {"type": "lambert", "texture": "spotSkin"},
    "lam_red":    {"type": "lambert", "color": [0.75, 0.22, 0.18]},
    "lam_blue":   {"type": "lambert", "color": [0.22, 0.38, 0.80]},
    "met_gold":   {"type": "metal", "color": [1.00, 0.78, 0.34], "roughness": 0.15},
    "met_copper": {"type": "metal", "color": [0.95, 0.54, 0.38], "roughness": 0.30},
    "met_steel":  {"type": "metal", "color": [0.75, 0.78, 0.82], "roughness": 0.08},
    "glass":      {"type": "dielectric", "ior": 1.5},
    "lam_amber":  {"type": "lambert", "color": [0.85, 0.60, 0.20]},
}
mat_cycle = list(materials.keys())

# --- lattice ----------------------------------------------------------------
extent = (N - 1) * SPACING
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
                "shape": "mesh:spot",
                "material": mat_cycle[i % len(mat_cycle)],
                "transform": [
                    {"scale": SCALE},
                    {"rotate_y": round(rot, 2)},
                    {"translate": [round(x, 3), round(y, 3), round(z, 3)]},
                ],
            })

# --- two large rect area lights above the swarm -----------------------------
# Above camera height (N*SPACING*1.35): the camera pitches down enough that
# its upper frustum edge stays below horizontal, so plates this high never
# appear in frame.
top = N * SPACING * 1.45
lights_objects = [
    {"shape": "rect", "material": "keyLight", "nee": True,
     "transform": [{"rotate_x": 180}, {"scale": 20.0},
                   {"translate": [-half * 0.8, top, -half * 0.5]}]},
    {"shape": "rect", "material": "fillLight", "nee": True,
     "transform": [{"rotate_x": 180}, {"scale": 20.0},
                   {"translate": [half * 0.8, top, half * 0.7]}]},
]
# two_sided: the camera sits above the light plane; a one-sided plate would
# show its black back face against the sky
materials["keyLight"] = {"type": "emissive", "color": [1.0, 0.93, 0.82],
                         "intensity": 34.0, "two_sided": True}
materials["fillLight"] = {"type": "emissive", "color": [0.75, 0.85, 1.0],
                          "intensity": 20.0, "two_sided": True}

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
    "textures": {"spotSkin": {"type": "image",
                              "file": "textures/spot_texture.png",
                              "srgb": True}},
    "materials": materials,
    "meshes": {"spot": {"obj": "../assets/spot.obj", "normals": "smooth"}},
    "objects": lights_objects + objects,
    "lights": [],
}

with open(OUT, "w") as f:
    # compact: 32k instances at indent=1 would be ~7.6 MB
    json.dump(scene, f, separators=(",", ":"))
    f.write("\n")

print(f"wrote {os.path.normpath(OUT)}: {len(objects)} spot instances, "
       f"{len(mat_cycle)} materials, {len(lights_objects)} area lights")
