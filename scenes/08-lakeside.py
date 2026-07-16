#!/usr/bin/env python3
"""sundog scene 08-lakeside — Dusk at the lakeside.

A sunset lake showing off the full water-material feature set: an
ior-1.33 dielectric interface, fbm ripple normals that shatter the
reflections and lay a glitter path from the sun straight to the camera,
and Beer-Lambert absorption that shades the deeper water blue-green.
Spot cows graze along the far bank (one cast in gold); their mirror
images are crumpled by the gentle swell, while a huge emissive sun
sphere hangs low over the horizon under a warm gradient sky.

Run: python3 scenes/08-lakeside.py
"""

from scenelib import Scene, rotate_y, scale, translate

s = Scene()
s.render(width=1920, height=1080, spp=256, max_depth=10, clamp=10, seed=7, exposure=0.25)
s.camera(lookfrom=[0.0, 0.55, -7.5], lookat=[0.3, 0.8, 4.0], vfov=36)
s.background_gradient(horizon=[0.9, 0.45, 0.18], zenith=[0.06, 0.11, 0.3])  # sunset sky

# ---- assets ----
s.texture('spotSkin', 'image', file='textures/spot_texture.png', srgb=True)
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.water('water', absorb=[0.7, 0.14, 0.06], wave_amp=0.09, wave_freq=1.2)  # rippled lake surface
s.lambert('bank', color=[0.3, 0.27, 0.2])       # muddy shoreline
s.lambert('stone', color=[0.3, 0.29, 0.28])     # grey pebbles
s.lambert('spot_skin', texture='spotSkin')      # textured cow hide
s.metal('gold', color=[1.0, 0.78, 0.34], roughness=0.12)
s.emissive('sun', color=[1.0, 0.55, 0.25], intensity=60)

# ---- lights ----
s.distant_light(direction=[-0.2, -1.0, 0.5], radiance=[0.1, 0.09, 0.11])  # faint cool sky fill

# ---- geometry ----
s.add('rect', 'water', scale(45))                                # the lake
s.add('rect', 'bank', scale(40, 1, 70), translate(0, 0.06, 73.5))  # far shore, just above water level
s.add('sphere', 'stone', scale(0.24), translate(-2.1, 0.02, 3.62))  # pebbles along the waterline
s.add('sphere', 'stone', scale(0.17), translate(-0.4, 0.0, 3.55))
s.add('sphere', 'stone', scale(0.28), translate(1.1, 0.04, 3.7))
s.add('sphere', 'stone', scale(0.15), translate(3.0, -0.01, 3.52))
s.add('sphere', 'stone', scale(0.21), translate(5.1, 0.02, 3.66))
s.add('mesh:spot', 'spot_skin', scale(0.85), rotate_y(15), translate(-3.2, 0.687, 4.6))   # cows on the bank
s.add('mesh:spot', 'spot_skin', scale(0.85), rotate_y(-12), translate(-0.8, 0.687, 4.0))
s.add('mesh:spot', 'gold', scale(0.85), rotate_y(22), translate(1.8, 0.687, 4.4))         # the gilded cow
s.add('mesh:spot', 'spot_skin', scale(0.85), rotate_y(-65), translate(4.1, 0.687, 5.1))
s.add('sphere', 'sun', scale(9), translate(10, 7, 230))          # emissive sun disc on the horizon

if __name__ == "__main__":
    s.run(out="08-lakeside.png")
