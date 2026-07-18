#!/usr/bin/env python3
"""sundog scene features — Full-feature coverage test scene.

A checkerboard showroom that exercises every core loader feature in one
frame (this is the scene tests/host/test_scene_json.cpp asserts against, not
a gallery piece). All four material kinds appear: two lamberts (checker
ground, matte red), two metals (rough gold, perfect mirror), one dielectric
(glass), and one emissive lamp. The object lineup covers the analytic
primitives — rect, sphere, cylinder, parabola, disk — with the parabola
carrying "material_back": null for back-face pass-through. Lighting mixes
all three light kinds: an emissive ceiling rect auto-registered as an NEE
area light, an explicit soft-shadow point light, and a distant sun.

Run: python3 scenes/features.py
"""

from scenelib import Scene, rotate_x, rotate_z, scale, translate

s = Scene()
s.render(width=1920, height=1080, spp=64, max_depth=16, seed=7)
s.camera(lookfrom=[0, 3.2, 9], lookat=[0, 0.9, 0], vfov=38, aperture=0.02)
s.background_gradient(horizon=[0.9, 0.9, 0.95], zenith=[0.35, 0.55, 0.95])

# ---- textures ----
s.texture('floor', 'checker', a=[0.8, 0.8, 0.8], b=[0.25, 0.25, 0.3], scale=[12, 12])

# ---- materials (all four kinds: 2 lambert, 2 metal, 1 dielectric, 1 emissive) ----
s.lambert('ground', texture='floor')
s.lambert('red', color=[0.75, 0.25, 0.25])
s.metal('gold', color=[1.0, 0.78, 0.34], roughness=0.15)
s.metal('mirror', color=[0.9, 0.9, 0.9], roughness=0.0)
s.dielectric('glass', ior=1.5)
s.emissive('lamp', color=[1.0, 0.95, 0.85], intensity=20.0)

# ---- lights (the emissive rect below adds the third, an NEE area light) ----
s.point_light(position=[5, 5, 4], intensity=[30, 30, 30], radius=0.3)  # soft-shadow point
s.distant_light(direction=[-0.5, -1, -0.3], radiance=[0.6, 0.6, 0.55])  # sun

# ---- geometry (one of each analytic primitive) ----
s.add('rect', 'ground', scale(8))  # checker floor
s.add('sphere', 'glass', scale(0.8), translate(-3.0, 0.8, 1.0))
s.add('sphere', 'red', scale(0.8), translate(-1.0, 0.8, 1.0))
s.add('cylinder', 'gold', scale(0.6, 0.8, 0.6), translate(1.0, 0.8, 1.0))
s.add('parabola', 'mirror', scale(1.4, 1.4, 1.4), rotate_z(90), translate(3.4, 1.4, 0.0), material_back=None)  # back face pass-through
s.add('disk', 'mirror', scale(1.6), rotate_x(90), translate(0, 1.6, -3.2))  # upright mirror backdrop
s.add('rect', 'lamp', rotate_x(180), scale(1.2), translate(0, 6, 2))  # ceiling area light, faces down

if __name__ == "__main__":
    s.run(out="features.avif")
