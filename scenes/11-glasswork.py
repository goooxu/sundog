#!/usr/bin/env python3
"""sundog scene 11-glasswork — Glasswork still life with nested dielectrics.

A glass still life on a wooden grid tabletop under an HDR sky: a water
sphere sits nested inside a glass sphere, with an air bubble suspended
inside the water — three layers of nested media resolved interface by
interface via the medium stack and relative IORs. The cow hidden behind
the orb is refracted through the double interface, flipped upside down
and back upright, ending up standing inside the bubble. Three tinted
glass beads (Beer-Lambert absorption) throw rose, amber and teal
transparent bright shadows across the table — shadow rays no longer
treat glass as opaque, but accumulate Fresnel and medium attenuation
along the straight line to the light.

Run: python3 scenes/11-glasswork.py
"""

from scenelib import Scene, rotate_y, scale, translate

s = Scene()
s.render(width=1920, height=1080, spp=256, max_depth=16, clamp=10, seed=7)
s.camera(lookfrom=[-3.8, 1.6, 5.0], lookat=[0.3, 0.9, 0], vfov=35)
s.background_envmap('../assets/kloofendal_48d_partly_cloudy_puresky_4k.hdr', rotate=180, intensity=1.0, importance=True)  # sole light source

# ---- textures & meshes ----
s.texture('table', 'grid', a=[0.55, 0.42, 0.28], b=[0.34, 0.25, 0.17], scale=[36, 36], width=0.04)  # wooden grid tabletop
s.texture('spotSkin', 'image', file='textures/spot_texture.png')
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.lambert('ground', texture='table')
s.dielectric('glass', ior=1.5)                               # clear outer shell
s.water('water', wave_amp=0.0, absorb=[0.5, 0.12, 0.05])     # inner water sphere (flat surface)
s.dielectric('bubble', ior=1.0)                              # air bubble: ior 1.0 pocket inside the water
s.dielectric('ruby', ior=1.5, absorb=[0.12, 1.6, 1.9])       # rose-tinted bead (Beer-Lambert)
s.dielectric('amberg', ior=1.5, absorb=[0.15, 0.8, 2.4])     # amber-tinted bead
s.dielectric('teal', ior=1.5, absorb=[2.2, 0.4, 0.25])       # teal-tinted bead
s.lambert('spot', texture='spotSkin')

# ---- geometry ----
s.add('rect', 'ground', scale(30))                                          # tabletop
s.add('sphere', 'glass', scale(1.0), translate(0.1, 1.0, -0.2))             # outer glass orb
s.add('sphere', 'water', scale(0.8), translate(0.1, 1.0, -0.2))             # water sphere nested inside the orb
s.add('sphere', 'bubble', scale(0.28), translate(0.25, 1.18, -0.2))         # air bubble suspended in the water
s.add('sphere', 'ruby', scale(0.32), translate(-1.7, 0.32, 1.5))            # rose bead
s.add('sphere', 'amberg', scale(0.28), translate(-0.5, 0.28, 2.15))         # amber bead
s.add('sphere', 'teal', scale(0.36), translate(1.6, 0.36, 1.6))             # teal bead
s.add('mesh:spot', 'spot', scale(1.3), rotate_y(210), translate(2.3, 0.96, -2.9))  # cow hidden behind the orb

if __name__ == "__main__":
    s.run(out="11-glasswork.png")
