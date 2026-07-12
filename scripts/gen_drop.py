#!/usr/bin/env python3
"""sundog: generate scenes/06-spot-cascade.json.

PhysX GPU showcase: 512 Spot instances pour from a spawn lattice into a
Cornell-colored open-top room. The scene JSON stores only the INITIAL poses
and velocities plus a top-level "physics" block; at load time the renderer
runs PhysX GPU rigid-body simulation (eENABLE_GPU_DYNAMICS) to rest and bakes
the settled poses before building acceleration structures. The floor and
walls are static colliders; the pile shape exists nowhere in this file.

Stdlib only. Run: python3 scripts/gen_drop.py
"""

import json
import os
import random

N_XZ = 8               # spawn lattice: N_XZ x N_XZ per layer
N_Y = 8                # layers -> N_XZ^2 * N_Y cows
PITCH_XZ = 0.85        # lattice pitch (cow bounding radius ~0.5 at SCALE)
PITCH_Y = 1.1
BASE_Y = 2.6           # lowest spawn layer
SCALE = 0.5            # per-cow uniform scale (spot is ~1.7 units tall at 1)
JITTER = 0.15          # max +/- positional jitter per axis
FOOT_Y = -0.737        # spot.obj feet rest at y = -0.737 (scale 1)

ROOM_HALF = 4.5        # interior half-extent of the room in x/z
WALL_H = 2.6           # left/right/back wall height
PARAPET_H = 1.0        # low front wall so the camera sees the pile
FLOOR_HALF = 12.0      # floor extends past the walls to catch escapees

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "scenes", "06-spot-cascade.json")

random.seed(2026)

materials = {
    "spot_skin": {"type": "lambert", "texture": "spotSkin"},
    "met_gold":  {"type": "metal", "color": [1.00, 0.78, 0.34], "roughness": 0.15},
    "glass":     {"type": "dielectric", "ior": 1.5},
    "floor":     {"type": "lambert", "color": [0.62, 0.60, 0.57]},
    "wall_red":  {"type": "lambert", "color": [0.63, 0.065, 0.05]},
    "wall_green": {"type": "lambert", "color": [0.14, 0.45, 0.091]},
    "wall_white": {"type": "lambert", "color": [0.73, 0.73, 0.73]},
    "keyLight":  {"type": "emissive", "color": [1.0, 0.95, 0.85], "intensity": 12.0},
}

# --- static colliders: floor + three tall walls + a low front parapet -------
# Canonical rect is XZ [-1,1]^2 with front +Y; rotate so the front faces the
# room interior (the physics slab extrudes behind the front face).
statics = [
    {"shape": "rect", "material": "floor",
     "physics": {"thickness": 0.5, "friction": 0.8, "restitution": 0.05},
     "transform": [{"scale": [FLOOR_HALF, 1, FLOOR_HALF]}]},
    # back wall (z = -ROOM_HALF, faces +z)
    {"shape": "rect", "material": "wall_white",
     "physics": {"thickness": 0.3, "restitution": 0.05},
     "transform": [{"scale": [ROOM_HALF, 1, WALL_H / 2]}, {"rotate_x": 90},
                   {"translate": [0, WALL_H / 2, -ROOM_HALF]}]},
    # left wall (x = -ROOM_HALF, faces +x)
    {"shape": "rect", "material": "wall_red",
     "physics": {"thickness": 0.3, "restitution": 0.05},
     "transform": [{"scale": [WALL_H / 2, 1, ROOM_HALF]}, {"rotate_z": -90},
                   {"translate": [-ROOM_HALF, WALL_H / 2, 0]}]},
    # right wall (x = +ROOM_HALF, faces -x)
    {"shape": "rect", "material": "wall_green",
     "physics": {"thickness": 0.3, "restitution": 0.05},
     "transform": [{"scale": [WALL_H / 2, 1, ROOM_HALF]}, {"rotate_z": 90},
                   {"translate": [ROOM_HALF, WALL_H / 2, 0]}]},
    # front parapet (z = +ROOM_HALF, faces -z): low, so the camera looks over
    # it and the occasional cow tumbles out toward the viewer
    {"shape": "rect", "material": "wall_white",
     "physics": {"thickness": 0.3, "restitution": 0.05},
     "transform": [{"scale": [ROOM_HALF, 1, PARAPET_H / 2]}, {"rotate_x": -90},
                   {"translate": [0, PARAPET_H / 2, ROOM_HALF]}]},
]

# --- 512 falling cows --------------------------------------------------------
n_cows = N_XZ * N_XZ * N_Y
special = random.sample(range(n_cows), 40)
mat_of = {i: "met_gold" for i in special[:32]}
mat_of.update({i: "glass" for i in special[32:]})

half = (N_XZ - 1) * PITCH_XZ / 2.0
cows = []
for iy in range(N_Y):
    for iz in range(N_XZ):
        for ix in range(N_XZ):
            i = (iy * N_XZ + iz) * N_XZ + ix
            x = ix * PITCH_XZ - half + random.uniform(-JITTER, JITTER)
            y = BASE_Y + iy * PITCH_Y + random.uniform(-JITTER, JITTER)
            z = iz * PITCH_XZ - half + random.uniform(-JITTER, JITTER)
            cows.append({
                "shape": "mesh:spot",
                "material": mat_of.get(i, "spot_skin"),
                "physics": {
                    "dynamic": True,
                    "density": 250,
                    "velocity": [round(random.uniform(-0.25, 0.25), 3),
                                 round(random.uniform(-3.0, -1.0), 3),
                                 round(random.uniform(-0.25, 0.25), 3)],
                    "angular_velocity": [round(random.uniform(-3, 3), 2),
                                         round(random.uniform(-3, 3), 2),
                                         round(random.uniform(-3, 3), 2)],
                },
                "transform": [
                    {"scale": SCALE},
                    {"rotate_x": round(random.uniform(0, 360), 2)},
                    {"rotate_y": round(random.uniform(0, 360), 2)},
                    {"rotate_z": round(random.uniform(0, 360), 2)},
                    {"translate": [round(x, 3), round(y, 3), round(z, 3)]},
                ],
            })

# --- one warm key light high above the room (one-sided, faces down; the
# camera pitches down enough that the plate stays out of frame) --------------
lights_objects = [
    {"shape": "rect", "material": "keyLight", "nee": True,
     "transform": [{"rotate_x": 180}, {"scale": 2.8},
                   {"translate": [0.0, 8.5, 0.0]}]},
]

scene = {
    "render": {"width": 1920, "height": 1080, "spp": 256, "max_depth": 12,
               "seed": 2026, "clamp": 10},
    "physics": {"gravity": [0, -9.81, 0], "timestep": 0.0041666667,
                "max_time": 20.0, "friction": 0.6, "restitution": 0.1,
                "sleep_threshold": 0.05},
    "camera": {
        "lookfrom": [0.0, 8.5, 11.0],
        "lookat": [0.0, 0.6, -0.6],
        "vfov": 38,
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
    "objects": lights_objects + statics + cows,
    "lights": [],
}

with open(OUT, "w") as f:
    # compact like 05-spot-swarm: hundreds of generated instances
    json.dump(scene, f, separators=(",", ":"))
    f.write("\n")

print(f"wrote {os.path.normpath(OUT)}: {len(cows)} falling cows "
      f"({len(special[:32])} gold, {len(special[32:])} glass), "
      f"{len(statics)} static colliders, {len(lights_objects)} area light")
