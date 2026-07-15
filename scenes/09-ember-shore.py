#!/usr/bin/env python3
"""sundog scene 09-ember-shore — Ember Shore.

A campfire on a night-time lakeshore: light from a volumetric flame is
reflected by the rippling water, the fire's mirror image crumbling into
the waves. Flame, water surface and soft shadows share one frame, which
makes this the noisiest scene at low sample counts — and therefore the
reference subject for the AI-denoiser comparison shots in the gallery.
Three spot cows have gathered around the fire ring on the shore.

Run: python3 scenes/09-ember-shore.py
"""

from scenelib import (Scene, rigid_body, rotate_x, rotate_y, rotate_z,
                      scale, static_body, translate)

s = Scene()
s.render(width=1920, height=1080, spp=256, max_depth=10, clamp=8, seed=7, exposure=0.5)
s.camera(lookfrom=[-4.5, 0.9, -2.5], lookat=[0.5, 0.8, 3.2], vfov=42)
s.background_gradient(horizon=[0.045, 0.055, 0.085], zenith=[0.003, 0.005, 0.012])  # moonless night sky

# ---- textures & meshes ----
s.texture('spotSkin', 'image', file='textures/spot_texture.png', srgb=True)
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.water('water', absorb=[0.7, 0.14, 0.06], wave_amp=0.09, wave_freq=1.2)  # lake: absorbs red -> deep teal
s.lambert('shore', color=[0.28, 0.24, 0.19])   # dark sandy bank
s.lambert('stone', color=[0.32, 0.31, 0.3])    # fire-ring stones
s.lambert('log', color=[0.23, 0.15, 0.09])     # firewood bark
s.emissive('ember', color=[1.0, 0.22, 0.04], intensity=4.0)  # glowing coals
s.lambert('spot_skin', texture='spotSkin')

# ---- lights ----
s.distant_light(direction=[0.4, -1.0, -0.3], radiance=[0.008, 0.011, 0.02])  # faint blue moonlight fill
s.flame(base=[0.5, 0.18, 3.4], height=1.5, radius=0.45, intensity=22, sigma=4.5, noise_scale=3.0, seed=5, light_intensity=36)  # volumetric campfire + its light

# ---- geometry ----
s.add('rect', 'water', scale(45))                                # lake surface (wavy water plane)
s.add('rect', 'shore', scale(40, 1, 60), translate(0, 0.06, 62))  # shore slab just above the water
# ring of stones around the fire pit
s.add('sphere', 'stone', scale(0.2), translate(-0.35, 0.14, 2.85))
s.add('sphere', 'stone', scale(0.16), translate(0.25, 0.12, 2.7))
s.add('sphere', 'stone', scale(0.19), translate(1.05, 0.13, 2.9))
s.add('sphere', 'stone', scale(0.17), translate(1.4, 0.12, 3.55))
s.add('sphere', 'stone', scale(0.21), translate(0.95, 0.14, 4.15))
s.add('sphere', 'stone', scale(0.18), translate(-0.25, 0.13, 4.1))
# four logs leaned into a teepee under the flame
s.add('cylinder', 'log', scale(0.055, 0.4, 0.055), rotate_x(35), rotate_y(20), translate(0.43, 0.4, 3.19))
s.add('cylinder', 'log', scale(0.055, 0.4, 0.055), rotate_x(35), rotate_y(110), translate(0.3, 0.4, 3.47))
s.add('cylinder', 'log', scale(0.055, 0.4, 0.055), rotate_x(35), rotate_y(200), translate(0.57, 0.4, 3.61))
s.add('cylinder', 'log', scale(0.055, 0.4, 0.055), rotate_x(35), rotate_y(290), translate(0.7, 0.4, 3.33))
# embers in the pit (too small/dim to be worth NEE)
s.add('sphere', 'ember', scale(0.08), translate(0.56, 0.13, 3.36), nee=False)
s.add('sphere', 'ember', scale(0.06), translate(0.4, 0.12, 3.5), nee=False)
# three cows gathered around the fire
s.add('mesh:spot', 'spot_skin', scale(0.85), rotate_y(-35), translate(-1.35, 0.687, 4.25))
s.add('mesh:spot', 'spot_skin', scale(0.85), rotate_y(45), translate(2.3, 0.687, 4.05))
s.add('mesh:spot', 'spot_skin', scale(0.85), rotate_y(5), translate(0.45, 0.687, 5.55))

if __name__ == "__main__":
    s.run(out="09-ember-shore.png")
