#!/usr/bin/env python3
"""sundog scene 07-campfire — Campfire Night.

A clearing at night, lit almost entirely by its own fire. The flame is a
procedural emissive participating medium (emission + absorption, ray-marched
inside an analytic cylinder bound resolved in raygen) and doubles as the
scene's only key light: all illumination comes from the warm soft-shadow
point light embedded in the flame, sampled via NEE. Five Spot cows sit
around the stone fire ring while a faint blue moonlight rims their
silhouettes.

Run: python3 scenes/07-campfire.py
"""

from scenelib import Scene, rotate_x, rotate_y, scale, translate

s = Scene()

# ---- render / camera / night sky ----
s.render(width=1920, height=1080, spp=256, max_depth=8, clamp=8, seed=7, exposure=0.5)
s.camera(lookfrom=[3.4, 1.35, 4.6], lookat=[0.0, 0.75, 0.0], vfov=44)
s.background_gradient(horizon=[0.018, 0.028, 0.055], zenith=[0.003, 0.005, 0.012])

# ---- assets ----
s.texture('spotSkin', 'image', file='textures/spot_texture.avif', srgb=True)
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.lambert('dirt', color=[0.3, 0.24, 0.18])
s.lambert('stone', color=[0.42, 0.41, 0.4])
s.lambert('log', color=[0.23, 0.15, 0.09])
s.emissive('ember', color=[1.0, 0.22, 0.04], intensity=4.0)
s.lambert('spot_skin', texture='spotSkin')

# ---- lights ----
s.distant_light(direction=[0.5, -1.0, 0.3], radiance=[0.01, 0.014, 0.024])  # faint moonlight
s.flame(base=[0.0, 0.12, 0.0], height=1.7, radius=0.5, intensity=22, sigma=4.5,
        noise_scale=3.0, seed=3, light_intensity=40)  # volumetric fire, the sole key light

# ---- geometry: ground ----
s.add('disk', 'dirt', scale(15))  # bare dirt clearing

# ---- geometry: fire ring stones (radius, x, y, z) ----
for r, x, y, z in (
        (0.2, 1.0, 0.1, 0.1),
        (0.17, 0.72, 0.09, 0.72),
        (0.21, -0.05, 0.1, 1.02),
        (0.16, -0.75, 0.08, 0.68),
        (0.19, -1.02, 0.1, -0.08),
        (0.18, -0.68, 0.09, -0.74),
        (0.22, 0.06, 0.11, -1.0),
        (0.17, 0.74, 0.09, -0.7)):
    s.add('sphere', 'stone', scale(r), translate(x, y, z))

# ---- geometry: log teepee over the fire (yaw, x, z) ----
for ry, x, z in (
        (10, -0.038, -0.217),
        (85, -0.219, -0.019),
        (150, -0.11, 0.191),
        (225, 0.156, 0.156),
        (300, 0.191, -0.11)):
    s.add('cylinder', 'log', scale(0.06, 0.42, 0.06), rotate_x(35), rotate_y(ry),
          translate(x, 0.35, z))

# ---- geometry: glowing coals (emissive but not NEE-sampled) ----
s.add('sphere', 'ember', scale(0.09), translate(0.1, 0.08, 0.05), nee=False)
s.add('sphere', 'ember', scale(0.07), translate(-0.12, 0.07, -0.06), nee=False)
s.add('sphere', 'ember', scale(0.08), translate(0.02, 0.07, -0.14), nee=False)

# ---- geometry: Spot cows seated around the fire (yaw, x, z) ----
for ry, x, z in (
        (150, 1.4, -2.42),
        (185, -0.26, -2.99),
        (238, -2.75, -1.75),
        (255, -3.19, -0.85),
        (95, -2.89, 0.25)):
    s.add('mesh:spot', 'spot_skin', scale(0.85), rotate_y(ry), translate(x, 0.627, z))

if __name__ == "__main__":
    s.run(out="07-campfire.avif")
