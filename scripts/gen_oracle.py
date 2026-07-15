#!/usr/bin/env python3
"""Generate scenes/12-molten-oracle.json — the cover scene
「熔岩圣殿的机械先知」(The Clockwork Oracle of the Molten Sanctum).

A ruined half-open temple: sunlight stabs through a collapsed roof onto a
mechanical cow frozen mid-explosion above a burning altar (PhysX freeze-
frame), gears and shell fragments hanging in the air; a sunken pool on the
right fades from clear to abyssal blue; rune strips glow on the walls.

Composition-critical constants live at the top; positions of the debris
cloud come from a fixed seed so the scene is reproducible. Both this script
and its JSON output are committed (gen_drop.py precedent).
"""
import json
import math
import os
import random

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "scenes", "12-molten-oracle.json")

random.seed(1207)

# ---- knobs ------------------------------------------------------------------
ENV_ROTATE = 255          # sun azimuth; beam comes from the north over the back wall
STOP_TIME = 0.20          # freeze-frame instant of the explosion
COW_POS = (0.0, 2.9, -3.0)
N_FRAG = 48
N_GEARS = 12
N_SPARKS = 20

scene = {
    "render": {"width": 3840, "height": 2160, "spp": 1024, "max_depth": 12,
               "seed": 7, "clamp": 10, "exposure": 0.7},
    "camera": {"lookfrom": [-2.2, 1.7, 9.4], "lookat": [0.5, 3.0, -3.0],
               "vfov": 55},
    "background": {
        "type": "envmap",
        "file": "../assets/kloofendal_48d_partly_cloudy_puresky_4k.hdr",
        "rotate": ENV_ROTATE, "intensity": 1.0, "importance": True,
    },
    "physics": {"timestep": 0.0041667, "max_time": 4.0,
                "stop_time": STOP_TIME, "friction": 0.6, "restitution": 0.3},
    "textures": {
        "floorTex": {"type": "grid", "a": [0.16, 0.155, 0.16],
                     "b": [0.08, 0.078, 0.085], "scale": [26, 26], "width": 0.05},
        "mossTex": {"type": "grid", "a": [0.30, 0.40, 0.24],
                    "b": [0.15, 0.22, 0.14], "scale": [10, 10], "width": 0.08},
        "gearTex": {"type": "image", "file": "textures/gear.png"},
        "runeTex": {"type": "image", "file": "textures/runes.png", "srgb": True},
    },
    "materials": {
        "floor": {"type": "lambert", "texture": "floorTex"},
        "stone": {"type": "lambert", "color": [0.075, 0.072, 0.08]},
        "pillar": {"type": "lambert", "color": [0.055, 0.052, 0.06]},
        "altar": {"type": "lambert", "color": [0.10, 0.09, 0.095]},
        "ice": {"type": "metal", "color": [0.78, 0.86, 0.95], "roughness": 0.3},
        "cowShell": {"type": "metal", "color": [0.30, 0.30, 0.32], "roughness": 0.35},
        "shard": {"type": "metal", "color": [0.34, 0.34, 0.36], "roughness": 0.3},
        "gold": {"type": "metal", "color": [1.0, 0.78, 0.34], "roughness": 0.18},
        "copper": {"type": "metal", "color": [0.92, 0.52, 0.32], "roughness": 0.22},
        "gearGold": {"type": "metal", "texture": "gearTex", "roughness": 0.2},
        "moss": {"type": "lambert", "texture": "mossTex"},
        "rune": {"type": "emissive", "texture": "runeTex", "intensity": 2.2},
        "spark": {"type": "emissive", "color": [1.0, 0.55, 0.18], "intensity": 14},
    },
    "meshes": {"spot": {"obj": "../assets/spot.obj", "normals": "smooth"}},
    "objects": [],
    "lights": [],
    "flames": [
        # twin altar fires (each auto-registers two warm soft point lights)
        {"base": [-0.45, 1.05, -3.0], "height": 2.3, "radius": 0.55,
         "intensity": 24, "sigma": 4.5, "noise_scale": 2.8, "seed": 3,
         "light_intensity": 36},
        {"base": [0.55, 1.05, -3.25], "height": 1.8, "radius": 0.42,
         "intensity": 20, "sigma": 4.5, "noise_scale": 3.2, "seed": 11,
         "light_intensity": 26},
        # smoke column: pure absorber (intensity 0) rising above the fires
        {"base": [0.0, 4.4, -3.6], "height": 3.4, "radius": 0.72,
         "intensity": 0, "sigma": 2.0, "noise_scale": 3.0, "seed": 7,
         "light_intensity": 0},
    ],
}
objects = scene["objects"]


def obj(shape, material, transform, **kw):
    o = {"shape": shape, "material": material, "transform": transform}
    o.update(kw)
    objects.append(o)
    return o


# ---- temple shell -----------------------------------------------------------
# Floor: slabs framing the sunken pool opening at x in [-1.6,3.6], z in [2.5,8].
# Screen-space check (camera -2.2,1.7,9.4 / vfov 55): the lower-right dead zone
# of the frame maps to world x -1.5..3.6, z 2.5..6.5 — the pool must sit THERE
# to read in the cover, and the mirror line camera->flames crosses the water at
# x ~ -1.4, z ~ 4.7..5.2, so the west shore at -1.6 catches the fire's
# reflection. (A rect cannot have a hole cut into it.)
FLOOR_PHYS = {"physics": {"thickness": 0.5}}
obj("rect", "floor", [{"scale": [5.2, 1, 12.5]}, {"translate": [-6.8, 0, 2.5]}], **FLOOR_PHYS)  # west slab (x -12..-1.6)
obj("rect", "floor", [{"scale": [6.8, 1, 6.25]}, {"translate": [5.2, 0, -3.75]}], **FLOOR_PHYS)  # north-east slab (z -10..2.5)
obj("rect", "floor", [{"scale": [6.8, 1, 3.5]}, {"translate": [5.2, 0, 11.5]}])   # south-east slab (z 8..15)
obj("rect", "floor", [{"scale": [4.2, 1, 2.75]}, {"translate": [7.8, 0, 5.25]}])  # east rim (x 3.6..12)

# Pool: tilted moss-brick bed (shallow clear west shore -> deep blue east),
# dark stone walls, water sheet just below floor level
obj("rect", "moss", [{"scale": [2.75, 1, 2.8]}, {"rotate_z": -18}, {"translate": [1.0, -1.25, 5.25]}])
obj("rect", "stone", [{"scale": [2.6, 1, 1.1]}, {"rotate_x": 90}, {"translate": [1.0, -1.1, 2.5]}])
obj("rect", "stone", [{"scale": [2.6, 1, 1.1]}, {"rotate_x": -90}, {"translate": [1.0, -1.1, 8]}])
obj("rect", "stone", [{"scale": [1.1, 1, 2.75]}, {"rotate_z": -90}, {"translate": [-1.6, -1.1, 5.25]}])
obj("rect", "stone", [{"scale": [1.1, 1, 2.75]}, {"rotate_z": 90}, {"translate": [3.6, -1.1, 5.25]}])
obj("rect", "water", [{"scale": [2.6, 1, 2.75]}, {"translate": [1.0, -0.12, 5.25]}])
# half-submerged stones on the shallow west shore
obj("sphere", "stone", [{"scale": [0.5, 0.33, 0.45]}, {"translate": [-1.45, -0.08, 3.7]}])
obj("sphere", "stone", [{"scale": [0.34, 0.24, 0.3]}, {"translate": [-1.1, -0.2, 5.9]}])
scene["materials"]["water"] = {"type": "water", "wave_amp": 0.035, "wave_freq": 2.6,
                               "absorb": [0.85, 0.18, 0.08]}

# Back wall (z=-9) and west wall (x=-11); east side stays open above the pool
obj("rect", "stone", [{"scale": [12, 1, 4.5]}, {"rotate_x": 90}, {"translate": [0, 4.5, -9]}])
obj("rect", "stone", [{"scale": [5.5, 1, 9]}, {"rotate_z": -90}, {"translate": [-11, 5.5, 0]}])
# low east wall behind the pool (half-open side)
obj("rect", "stone", [{"scale": [1.75, 1, 9]}, {"rotate_z": 90}, {"translate": [11, 1.75, 0]}])

# Roof at y=9. The breach sits in the NORTHERN band (x[-3,3], z[-9,-4]):
# with the 48-deg sun due north, its beam clears the back wall (top y=9),
# enters the breach, grazes the airborne debris and pools south of the altar.
obj("rect", "stone", [{"scale": [4.5, 1, 2.5]}, {"translate": [-7.5, 9, -6.5]}])  # west of breach
obj("rect", "stone", [{"scale": [4.5, 1, 2.5]}, {"translate": [7.5, 9, -6.5]}])   # east of breach
obj("rect", "stone", [{"scale": [12, 1, 4]}, {"translate": [0, 9, 0]}])           # southern slab
# (roof deliberately ends at z=4: the front half of the temple is open sky)

# Pillars: two rows of three, ice caps near the breach
for i, z in enumerate([-6.5, -2.5, 1.5]):
    for x in (-7.5, 7.5):
        obj("cylinder", "pillar", [{"scale": [0.7, 4.5, 0.7]}, {"translate": [x, 4.5, z]}])
        obj("sphere", "ice", [{"scale": [0.95, 0.32, 0.95]}, {"translate": [x, 9.05, z]}])

# Altar under the cow
obj("cylinder", "altar", [{"scale": [1.7, 0.55, 1.7]}, {"translate": [0, 0.55, -3.1]}])
obj("disk", "altar", [{"scale": 1.7}, {"translate": [0, 1.101, -3.1]}])

# Rune strips: back wall and west wall (emissive texture, kept out of NEE)
obj("rect", "rune", [{"scale": [5.5, 1, 0.32]}, {"rotate_x": 90}, {"translate": [-3.5, 3.2, -8.94]}], nee=False)
obj("rect", "rune", [{"scale": [4.0, 1, 0.28]}, {"rotate_x": 90}, {"translate": [4.5, 5.0, -8.94]}], nee=False)
obj("rect", "rune", [{"scale": [4.5, 1, 0.3]}, {"rotate_z": -90}, {"translate": [-10.94, 3.8, -2.0]}], nee=False)

# ---- the clockwork oracle, mid-detonation -----------------------------------
obj("mesh:spot", "cowShell",
    [{"scale": 1.7}, {"rotate_y": 205}, {"translate": list(COW_POS)}],
    physics={"dynamic": True, "density": 400,
             "velocity": [0.4, 2.8, 0.2], "angular_velocity": [0.8, 0.5, -1.0]})

# fallen roof rubble in the sun pool (debris from the collapsed dome)
obj("sphere", "stone", [{"scale": [0.55, 0.32, 0.5]}, {"translate": [-2.6, 0.3, 1.6]}])
obj("sphere", "stone", [{"scale": [0.35, 0.22, 0.4]}, {"translate": [-1.3, 0.2, 1.5]}])
obj("cylinder", "pillar", [{"scale": [0.45, 0.5, 0.45]}, {"rotate_z": 82}, {"rotate_y": 15}, {"translate": [-4.3, 0.42, 3.5]}])
obj("sphere", "stone", [{"scale": [0.28, 0.18, 0.3]}, {"translate": [0.2, 0.17, 1.7]}])

# shell fragments / machine parts: spheres bursting radially from the cow
frag_mats = ["shard", "shard", "shard", "gold", "copper"]
for i in range(N_FRAG):
    # biased-upward random direction
    while True:
        d = (random.uniform(-1, 1), random.uniform(-0.35, 1), random.uniform(-1, 1))
        n = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2)
        if 0.2 < n <= 1.0:
            break
    d = (d[0] / n, d[1] / n, d[2] / n)
    start = 0.95
    speed = random.uniform(3.5, 9.0)
    r = random.uniform(0.05, 0.22)
    pos = [COW_POS[0] + d[0] * start, COW_POS[1] + d[1] * start, COW_POS[2] + d[2] * start]
    spin = [random.uniform(-6, 6) for _ in range(3)]
    obj("sphere", random.choice(frag_mats),
        [{"scale": r}, {"translate": pos}],
        physics={"dynamic": True, "density": 500,
                 "velocity": [d[0] * speed, d[1] * speed, d[2] * speed],
                 "angular_velocity": spin})

# gears: static cutout disks scattered on a shell around the blast
for i in range(N_GEARS):
    while True:
        d = (random.uniform(-1, 1), random.uniform(-0.2, 1), random.uniform(-1, 1))
        n = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2)
        if 0.25 < n <= 1.0:
            break
    dist = random.uniform(0.9, 2.0)
    pos = [COW_POS[0] + d[0] / n * dist, COW_POS[1] + d[1] / n * dist,
           COW_POS[2] + d[2] / n * dist]
    r = random.uniform(0.16, 0.42)
    obj("disk", random.choice(["gearGold", "copper"]),
        [{"scale": r}, {"rotate_x": random.uniform(0, 360)},
         {"rotate_y": random.uniform(0, 360)}, {"rotate_z": random.uniform(0, 360)},
         {"translate": pos}],
        cutout="gearTex")

# sparks: tiny emissive embers rising above the fires (out of NEE)
for i in range(N_SPARKS):
    x = random.uniform(-1.9, 2.1)
    y = random.uniform(2.0, 7.0)
    z = -3.1 + random.uniform(-1.5, 1.3)
    r = random.uniform(0.018, 0.045)
    obj("sphere", "spark", [{"scale": r}, {"translate": [x, y, z]}], nee=False)

with open(OUT, "w") as f:
    json.dump(scene, f, separators=(",", ":"))
print(f"wrote {OUT} ({len(objects)} objects, "
      f"{sum(1 for o in objects if 'physics' in o and o['physics'].get('dynamic'))} dynamic)")
