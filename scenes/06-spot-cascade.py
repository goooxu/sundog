#!/usr/bin/env python3
"""sundog scene 06 — Spot Cascade.

PhysX GPU showcase: 512 Spot instances pour from a spawn lattice into a
Cornell-colored open-top room. The scene declares only the INITIAL poses
and velocities plus a top-level physics block; at load time the renderer
runs PhysX GPU rigid-body simulation (eENABLE_GPU_DYNAMICS) to rest and bakes
the settled poses before building acceleration structures. The floor and
walls are static colliders; the pile shape exists nowhere in this file.

Run: python3 scenes/06-spot-cascade.py          (renders 06-spot-cascade.avif)
"""

import random

from scenelib import (Scene, rigid_body, rotate_x, rotate_y, rotate_z, scale,
                      static_body, translate)

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

random.seed(2026)      # layout seed (render.seed below is the sampling seed)

s = Scene()
s.render(width=1920, height=1080, spp=256, max_depth=12, seed=2026, clamp=10,
         exposure=0.25)   # slight EV lift for the night framing
s.physics(gravity=[0, -9.81, 0], timestep=0.0041666667, max_time=20.0,
          friction=0.6, restitution=0.1, sleep_threshold=0.05)
s.camera(lookfrom=[0.0, 8.5, 11.0], lookat=[0.0, 0.6, -0.6], vfov=38,
         aperture=0.0)
s.background_gradient(horizon=[0.05, 0.06, 0.09], zenith=[0.01, 0.01, 0.03])

s.texture("spotSkin", "image", file="textures/spot_texture.avif", srgb=True)
s.mesh("spot", "../assets/spot.obj", normals="smooth")

s.lambert("spot_skin", texture="spotSkin")
s.metal("met_gold", color=[1.00, 0.78, 0.34], roughness=0.15)
s.dielectric("glass", ior=1.5)
s.lambert("floor", color=[0.62, 0.60, 0.57])
s.lambert("wall_red", color=[0.63, 0.065, 0.05])
s.lambert("wall_green", color=[0.14, 0.45, 0.091])
s.lambert("wall_white", color=[0.73, 0.73, 0.73])
s.emissive("keyLight", color=[1.0, 0.95, 0.85], intensity=12.0)

# --- one warm key light high above the room (one-sided, faces down; the
# camera pitches down enough that the plate stays out of frame) --------------
s.add("rect", "keyLight", rotate_x(180), scale(2.8),
      translate(0.0, 8.5, 0.0), nee=True)

# --- static colliders: floor + three tall walls + a low front parapet -------
# Canonical rect is XZ [-1,1]^2 with front +Y; rotate so the front faces the
# room interior (the physics slab extrudes behind the front face).
s.add("rect", "floor", scale(FLOOR_HALF, 1, FLOOR_HALF),
      physics=static_body(thickness=0.5, friction=0.8, restitution=0.05))
# back wall (z = -ROOM_HALF, faces +z)
s.add("rect", "wall_white", scale(ROOM_HALF, 1, WALL_H / 2), rotate_x(90),
      translate(0, WALL_H / 2, -ROOM_HALF),
      physics=static_body(thickness=0.3, restitution=0.05))
# left wall (x = -ROOM_HALF, faces +x)
s.add("rect", "wall_red", scale(WALL_H / 2, 1, ROOM_HALF), rotate_z(-90),
      translate(-ROOM_HALF, WALL_H / 2, 0),
      physics=static_body(thickness=0.3, restitution=0.05))
# right wall (x = +ROOM_HALF, faces -x)
s.add("rect", "wall_green", scale(WALL_H / 2, 1, ROOM_HALF), rotate_z(90),
      translate(ROOM_HALF, WALL_H / 2, 0),
      physics=static_body(thickness=0.3, restitution=0.05))
# front parapet (z = +ROOM_HALF, faces -z): low, so the camera looks over
# it and the occasional cow tumbles out toward the viewer
s.add("rect", "wall_white", scale(ROOM_HALF, 1, PARAPET_H / 2), rotate_x(-90),
      translate(0, PARAPET_H / 2, ROOM_HALF),
      physics=static_body(thickness=0.3, restitution=0.05))

# --- 512 falling cows --------------------------------------------------------
# NOTE: the random draws below happen in the exact order of the original
# generator (position jitter, velocity, angular velocity, then rotations) —
# do not fold them into the add() call, argument evaluation would reorder.
n_cows = N_XZ * N_XZ * N_Y
special = random.sample(range(n_cows), 40)
mat_of = {i: "met_gold" for i in special[:32]}
mat_of.update({i: "glass" for i in special[32:]})

half = (N_XZ - 1) * PITCH_XZ / 2.0
for iy in range(N_Y):
    for iz in range(N_XZ):
        for ix in range(N_XZ):
            i = (iy * N_XZ + iz) * N_XZ + ix
            x = ix * PITCH_XZ - half + random.uniform(-JITTER, JITTER)
            y = BASE_Y + iy * PITCH_Y + random.uniform(-JITTER, JITTER)
            z = iz * PITCH_XZ - half + random.uniform(-JITTER, JITTER)
            vel = [round(random.uniform(-0.25, 0.25), 3),
                   round(random.uniform(-3.0, -1.0), 3),
                   round(random.uniform(-0.25, 0.25), 3)]
            ang = [round(random.uniform(-3, 3), 2),
                   round(random.uniform(-3, 3), 2),
                   round(random.uniform(-3, 3), 2)]
            rx = round(random.uniform(0, 360), 2)
            ry = round(random.uniform(0, 360), 2)
            rz = round(random.uniform(0, 360), 2)
            s.add("mesh:spot", mat_of.get(i, "spot_skin"),
                  scale(SCALE), rotate_x(rx), rotate_y(ry), rotate_z(rz),
                  translate(round(x, 3), round(y, 3), round(z, 3)),
                  physics=rigid_body(density=250, velocity=vel,
                                     angular_velocity=ang))

if __name__ == "__main__":
    s.run(out="06-spot-cascade.avif")
