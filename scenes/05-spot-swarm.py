#!/usr/bin/env python3
"""sundog scene 05 — Spot Swarm.

RT-core throughput benchmark: a 32x32x32 lattice of 32,768 Spot (Keenan
Crane's cartoon cow, 5,856 tris) instances sharing one mesh (the renderer
shares the GAS across instances), with seeded jitter and per-instance
rotation, 8 cycling materials (including the native texture), two large rect
area lights and a dark gradient sky. ~192M effective triangles.

Run: python3 scenes/05-spot-swarm.py            (renders 05-spot-swarm.png)
"""

import random

from scenelib import Scene, rotate_x, rotate_y, scale, translate

N = 32                 # lattice size per axis -> N^3 instances
SPACING = 1.9          # lattice pitch (spot is ~1.7 units tall at scale 1)
SCALE = 0.85           # per-instance uniform scale
JITTER = 0.35          # max +/- positional jitter per axis
FOOT_Y = -0.737        # spot.obj feet rest at y = -0.737

random.seed(5090)      # layout seed (render.seed below is the sampling seed)

extent = (N - 1) * SPACING
half = extent / 2.0                 # lattice centered on origin in x/z
top = N * SPACING * 1.45
cam_dist = extent * 1.55

s = Scene()
s.render(width=1920, height=1080, spp=64, max_depth=8, seed=5090, clamp=10)
s.camera(lookfrom=[cam_dist * 0.75, N * SPACING * 1.35, cam_dist * 0.85],
         lookat=[0.0, N * SPACING * 0.40, 0.0], vfov=42, aperture=0.0)
s.background_gradient(horizon=[0.05, 0.06, 0.09], zenith=[0.01, 0.01, 0.03])

s.texture("spotSkin", "image", file="textures/spot_texture.png", srgb=True)
s.mesh("spot", "../assets/spot.obj", normals="smooth")

# --- 8 cycling materials: native texture + hue-varied lambert/metal + glass -
mat_cycle = [
    s.lambert("spot_skin", texture="spotSkin"),
    s.lambert("lam_red", color=[0.75, 0.22, 0.18]),
    s.lambert("lam_blue", color=[0.22, 0.38, 0.80]),
    s.metal("met_gold", color=[1.00, 0.78, 0.34], roughness=0.15),
    s.metal("met_copper", color=[0.95, 0.54, 0.38], roughness=0.30),
    s.metal("met_steel", color=[0.75, 0.78, 0.82], roughness=0.08),
    s.dielectric("glass", ior=1.5),
    s.lambert("lam_amber", color=[0.85, 0.60, 0.20]),
]

# --- two large rect area lights above the swarm (before the lattice: object
# order is scene order). Above camera height (N*SPACING*1.35): the camera
# pitches down enough that its upper frustum edge stays below horizontal, so
# plates this high never appear in frame.
# two_sided: the camera sits above the light plane; a one-sided plate would
# show its black back face against the sky.
s.emissive("keyLight", color=[1.0, 0.93, 0.82], intensity=34.0, two_sided=True)
s.emissive("fillLight", color=[0.75, 0.85, 1.0], intensity=20.0, two_sided=True)
s.add("rect", "keyLight", rotate_x(180), scale(20.0),
      translate(-half * 0.8, top, -half * 0.5), nee=True)
s.add("rect", "fillLight", rotate_x(180), scale(20.0),
      translate(half * 0.8, top, half * 0.7), nee=True)

# --- lattice ----------------------------------------------------------------
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
            s.add("mesh:spot", mat_cycle[i % len(mat_cycle)],
                  scale(SCALE), rotate_y(round(rot, 2)),
                  translate(round(x, 3), round(y, 3), round(z, 3)))

if __name__ == "__main__":
    s.run(out="05-spot-swarm.png")
