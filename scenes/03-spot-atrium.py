#!/usr/bin/env python3
"""sundog scene 03-spot-atrium — Three Spot cows in a grid-floor atrium.

Three instances of the Spot cartoon cow (5,856 triangles each) stand on a
grid-textured atrium floor under a bright sky gradient: one wearing its
native image texture, one cast in polished gold, and one rendered as clear
glass. The scene showcases hardware triangle intersection, OBJ loading with
UV-mapped image textures, and smooth vertex normals.

Run: python3 scenes/03-spot-atrium.py
"""

from scenelib import (Scene, rigid_body, rotate_x, rotate_y, rotate_z,
                      scale, static_body, translate)

s = Scene()
s.render(width=1920, height=1080, spp=128, max_depth=12, clamp=8, seed=3)
s.camera(lookfrom=[0.0, 2.1, 6.8], lookat=[0.0, 0.9, 0.0], vfov=37, aperture=0.02)
s.background_gradient(horizon=[0.92, 0.94, 1.0], zenith=[0.3, 0.5, 0.9])

# ---- textures & meshes ----
s.texture('floorGrid', 'grid', a=[0.82, 0.82, 0.85], b=[0.22, 0.26, 0.32], scale=[50, 50], width=0.045)
s.texture('spotSkin', 'image', file='textures/spot_texture.png', srgb=True)  # Spot's native UV texture
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.lambert('ground', texture='floorGrid')
s.lambert('spot', texture='spotSkin')
s.metal('gold', color=[1.0, 0.78, 0.34], roughness=0.15)
s.dielectric('glass', ior=1.5)

# ---- lights ----
s.distant_light(direction=[-0.45, -1.0, -0.3], radiance=[1.5, 1.4, 1.2])  # sun
s.point_light(position=[4.5, 5.0, 3.5], intensity=[55, 50, 42], radius=0.4)  # warm fill

# ---- geometry ----
s.add('rect', 'ground', scale(30))  # atrium floor
s.add('mesh:spot', 'spot', scale(1.0), rotate_y(205), translate(-2.1, 0.737, 0.5))  # textured cow (left)
s.add('mesh:spot', 'gold', scale(1.12), rotate_y(172), translate(0.15, 0.826, -0.9))  # gold cow (center, back)
s.add('mesh:spot', 'glass', scale(1.0), rotate_y(145), translate(2.15, 0.737, 0.6))  # glass cow (right)

if __name__ == "__main__":
    s.run(out="03-spot-atrium.png")
