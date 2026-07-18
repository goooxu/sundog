#!/usr/bin/env python3
"""sundog scene 12 — the cover scene
「熔岩圣殿的机械先知」(The Clockwork Oracle of the Molten Sanctum).

A ruined half-open temple: sunlight stabs through a collapsed roof onto a
mechanical cow frozen mid-explosion above a burning altar (PhysX freeze-
frame), gears and shell fragments hanging in the air; a sunken pool on the
right fades from clear to abyssal blue; rune strips glow on the walls.

Composition-critical constants live at the top; positions of the debris
cloud come from a fixed seed so the scene is reproducible.

Run: python3 scenes/12-molten-oracle.py        (renders 12-molten-oracle.avif)
"""
import math
import random

from scenelib import (Scene, rigid_body, rotate_x, rotate_y, rotate_z, scale,
                      static_body, translate)

random.seed(1207)         # layout seed (render.seed below is the sampling seed)

# ---- knobs ------------------------------------------------------------------
ENV_ROTATE = 255          # sun azimuth; beam comes from the north over the back wall
STOP_TIME = 0.20          # freeze-frame instant of the explosion
COW_POS = (0.0, 2.9, -3.0)
N_FRAG = 48
N_GEARS = 12
N_SPARKS = 20

s = Scene()
s.render(width=3840, height=2160, spp=1024, max_depth=12, seed=7, clamp=10,
         exposure=0.7)
s.camera(lookfrom=[-2.2, 1.7, 9.4], lookat=[0.5, 3.0, -3.0], vfov=55)
s.background_envmap("../assets/kloofendal_48d_partly_cloudy_puresky_4k.hdr",
                    rotate=ENV_ROTATE, intensity=1.0, importance=True)
s.physics(timestep=0.0041667, max_time=4.0, stop_time=STOP_TIME,
          friction=0.6, restitution=0.3)

s.texture("floorTex", "grid", a=[0.16, 0.155, 0.16], b=[0.08, 0.078, 0.085],
          scale=[26, 26], width=0.05)
s.texture("mossTex", "grid", a=[0.30, 0.40, 0.24], b=[0.15, 0.22, 0.14],
          scale=[10, 10], width=0.08)
s.texture("gearTex", "image", file="textures/gear.avif")
s.texture("runeTex", "image", file="textures/runes.avif", srgb=True)

s.lambert("floor", texture="floorTex")
s.lambert("stone", color=[0.075, 0.072, 0.08])
s.lambert("pillar", color=[0.055, 0.052, 0.06])
s.lambert("altar", color=[0.10, 0.09, 0.095])
s.metal("ice", color=[0.78, 0.86, 0.95], roughness=0.3)
s.metal("cowShell", color=[0.30, 0.30, 0.32], roughness=0.35)
s.metal("shard", color=[0.34, 0.34, 0.36], roughness=0.3)
s.metal("gold", color=[1.0, 0.78, 0.34], roughness=0.18)
s.metal("copper", color=[0.92, 0.52, 0.32], roughness=0.22)
s.metal("gearGold", texture="gearTex", roughness=0.2)
s.lambert("moss", texture="mossTex")
s.emissive("rune", texture="runeTex", intensity=2.2)
s.emissive("spark", color=[1.0, 0.55, 0.18], intensity=14)

s.mesh("spot", "../assets/spot.obj", normals="smooth")

# twin altar fires (each auto-registers two warm soft point lights)
s.flame(base=[-0.45, 1.05, -3.0], height=2.3, radius=0.55, intensity=24,
        sigma=4.5, noise_scale=2.8, seed=3, light_intensity=36)
s.flame(base=[0.55, 1.05, -3.25], height=1.8, radius=0.42, intensity=20,
        sigma=4.5, noise_scale=3.2, seed=11, light_intensity=26)
# smoke column: pure absorber (intensity 0) rising above the fires
s.flame(base=[0.0, 4.4, -3.6], height=3.4, radius=0.72, intensity=0,
        sigma=2.0, noise_scale=3.0, seed=7, light_intensity=0)

# ---- temple shell -----------------------------------------------------------
# Floor: slabs framing the sunken pool opening at x in [-1.6,3.6], z in [2.5,8].
# Screen-space check (camera -2.2,1.7,9.4 / vfov 55): the lower-right dead zone
# of the frame maps to world x -1.5..3.6, z 2.5..6.5 — the pool must sit THERE
# to read in the cover, and the mirror line camera->flames crosses the water at
# x ~ -1.4, z ~ 4.7..5.2, so the west shore at -1.6 catches the fire's
# reflection. (A rect cannot have a hole cut into it.)
s.add("rect", "floor", scale(5.2, 1, 12.5), translate(-6.8, 0, 2.5),
      physics=static_body(thickness=0.5))   # west slab (x -12..-1.6)
s.add("rect", "floor", scale(6.8, 1, 6.25), translate(5.2, 0, -3.75),
      physics=static_body(thickness=0.5))   # north-east slab (z -10..2.5)
s.add("rect", "floor", scale(6.8, 1, 3.5), translate(5.2, 0, 11.5))   # south-east slab (z 8..15)
s.add("rect", "floor", scale(4.2, 1, 2.75), translate(7.8, 0, 5.25))  # east rim (x 3.6..12)

# Pool: tilted moss-brick bed (shallow clear west shore -> deep blue east),
# dark stone walls, water sheet just below floor level
s.add("rect", "moss", scale(2.75, 1, 2.8), rotate_z(-18), translate(1.0, -1.25, 5.25))
s.add("rect", "stone", scale(2.6, 1, 1.1), rotate_x(90), translate(1.0, -1.1, 2.5))
s.add("rect", "stone", scale(2.6, 1, 1.1), rotate_x(-90), translate(1.0, -1.1, 8))
s.add("rect", "stone", scale(1.1, 1, 2.75), rotate_z(-90), translate(-1.6, -1.1, 5.25))
s.add("rect", "stone", scale(1.1, 1, 2.75), rotate_z(90), translate(3.6, -1.1, 5.25))
s.add("rect", "water", scale(2.6, 1, 2.75), translate(1.0, -0.12, 5.25))
# half-submerged stones on the shallow west shore
s.add("sphere", "stone", scale(0.5, 0.33, 0.45), translate(-1.45, -0.08, 3.7))
s.add("sphere", "stone", scale(0.34, 0.24, 0.3), translate(-1.1, -0.2, 5.9))
s.water("water", wave_amp=0.035, wave_freq=2.6, absorb=[0.85, 0.18, 0.08])

# Back wall (z=-9) and west wall (x=-11); east side stays open above the pool
s.add("rect", "stone", scale(12, 1, 4.5), rotate_x(90), translate(0, 4.5, -9))
s.add("rect", "stone", scale(5.5, 1, 9), rotate_z(-90), translate(-11, 5.5, 0))
# low east wall behind the pool (half-open side)
s.add("rect", "stone", scale(1.75, 1, 9), rotate_z(90), translate(11, 1.75, 0))

# Roof at y=9. The breach sits in the NORTHERN band (x[-3,3], z[-9,-4]):
# with the 48-deg sun due north, its beam clears the back wall (top y=9),
# enters the breach, grazes the airborne debris and pools south of the altar.
s.add("rect", "stone", scale(4.5, 1, 2.5), translate(-7.5, 9, -6.5))  # west of breach
s.add("rect", "stone", scale(4.5, 1, 2.5), translate(7.5, 9, -6.5))   # east of breach
s.add("rect", "stone", scale(12, 1, 4), translate(0, 9, 0))           # southern slab
# (roof deliberately ends at z=4: the front half of the temple is open sky)

# Pillars: two rows of three, ice caps near the breach
for i, z in enumerate([-6.5, -2.5, 1.5]):
    for x in (-7.5, 7.5):
        s.add("cylinder", "pillar", scale(0.7, 4.5, 0.7), translate(x, 4.5, z))
        s.add("sphere", "ice", scale(0.95, 0.32, 0.95), translate(x, 9.05, z))

# Altar under the cow
s.add("cylinder", "altar", scale(1.7, 0.55, 1.7), translate(0, 0.55, -3.1))
s.add("disk", "altar", scale(1.7), translate(0, 1.101, -3.1))

# Rune strips: back wall and west wall (emissive texture, kept out of NEE)
s.add("rect", "rune", scale(5.5, 1, 0.32), rotate_x(90), translate(-3.5, 3.2, -8.94), nee=False)
s.add("rect", "rune", scale(4.0, 1, 0.28), rotate_x(90), translate(4.5, 5.0, -8.94), nee=False)
s.add("rect", "rune", scale(4.5, 1, 0.3), rotate_z(-90), translate(-10.94, 3.8, -2.0), nee=False)

# ---- the clockwork oracle, mid-detonation -----------------------------------
s.add("mesh:spot", "cowShell", scale(1.7), rotate_y(205), translate(list(COW_POS)),
      physics=rigid_body(density=400, velocity=[0.4, 2.8, 0.2],
                         angular_velocity=[0.8, 0.5, -1.0]))

# fallen roof rubble in the sun pool (debris from the collapsed dome)
s.add("sphere", "stone", scale(0.55, 0.32, 0.5), translate(-2.6, 0.3, 1.6))
s.add("sphere", "stone", scale(0.35, 0.22, 0.4), translate(-1.3, 0.2, 1.5))
s.add("cylinder", "pillar", scale(0.45, 0.5, 0.45), rotate_z(82), rotate_y(15),
      translate(-4.3, 0.42, 3.5))
s.add("sphere", "stone", scale(0.28, 0.18, 0.3), translate(0.2, 0.17, 1.7))

# shell fragments / machine parts: spheres bursting radially from the cow.
# The random draws keep the original generator's exact order (rejection loop,
# speed, radius, spin, then the material choice inside the add call).
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
    s.add("sphere", random.choice(frag_mats), scale(r), translate(pos),
          physics=rigid_body(density=500,
                             velocity=[d[0] * speed, d[1] * speed, d[2] * speed],
                             angular_velocity=spin))

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
    s.add("disk", random.choice(["gearGold", "copper"]),
          scale(r), rotate_x(random.uniform(0, 360)),
          rotate_y(random.uniform(0, 360)), rotate_z(random.uniform(0, 360)),
          translate(pos), cutout="gearTex")

# sparks: tiny emissive embers rising above the fires (out of NEE)
for i in range(N_SPARKS):
    x = random.uniform(-1.9, 2.1)
    y = random.uniform(2.0, 7.0)
    z = -3.1 + random.uniform(-1.5, 1.3)
    r = random.uniform(0.018, 0.045)
    s.add("sphere", "spark", scale(r), translate(x, y, z), nee=False)

if __name__ == "__main__":
    s.run(out="12-molten-oracle.avif")
