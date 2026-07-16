#!/usr/bin/env python3
"""sundog scene 03-spot-atrium — Sparky the robot meets the Spot herd.

Sparky (an AI-generated cartoon robot, 7,284 triangles across ten usemtl
material groups) stands in a grid-floor atrium surrounded by three Spot
cows: one in its native UV texture, one in polished gold, one in clear
glass. The robot demonstrates multi-material meshes — one OBJ split into
per-group sub-meshes sharing a transform: a dielectric glass head dome,
emissive albedo-textured face/chest/palm screens, a glowing yellow core,
grey metal joints, and lambert plastic shells. The cows cover hardware
triangles, UV image textures, and smooth normals as before.

Run: python3 scenes/03-spot-atrium.py
"""

from scenelib import (Scene, rigid_body, rotate_x, rotate_y, rotate_z,
                      scale, static_body, translate)

s = Scene()
s.render(width=1920, height=1080, spp=128, max_depth=12, clamp=8, seed=3)
s.camera(lookfrom=[0.0, 2.1, 6.8], lookat=[0.0, 1.0, 0.0], vfov=37, aperture=0.02)
s.background_gradient(horizon=[0.92, 0.94, 1.0], zenith=[0.3, 0.5, 0.9])

# ---- textures & meshes ----
s.texture('floorGrid', 'grid', a=[0.82, 0.82, 0.85], b=[0.22, 0.26, 0.32], scale=[50, 50], width=0.045)
s.texture('spotSkin', 'image', file='textures/spot_texture.png', srgb=True)  # Spot's native UV texture
s.texture('sparkyScreen', 'image', file='textures/sparky_albedo.png', srgb=True)  # Sparky's screen atlas
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.lambert('ground', texture='floorGrid')
s.lambert('spot', texture='spotSkin')
s.metal('gold', color=[1.0, 0.78, 0.34], roughness=0.15)
s.dielectric('glass', ior=1.5)
# Sparky's material groups, interpreted from the .mtl names (Kd colors kept):
s.dielectric('sparkyGlass', ior=1.5)                                   # head dome
s.emissive('sparkyScreen', texture='sparkyScreen', intensity=3.0)      # face/chest/palm panels
s.emissive('sparkyGlow', color=[1.0, 0.85, 0.2], intensity=6.0)        # core light
s.metal('sparkyMetal', color=[0.45, 0.47, 0.5], roughness=0.35)        # joints
s.lambert('sparkyBlue', color=[0.35, 0.65, 0.9])
s.lambert('sparkyWhite', color=[0.92, 0.93, 0.95])
s.lambert('sparkyAccent', color=[0.95, 0.45, 0.12])
s.lambert('sparkyTread', color=[0.95, 0.4, 0.08])

# ---- lights ----
s.distant_light(direction=[-0.45, -1.0, -0.3], radiance=[1.5, 1.4, 1.2])  # sun
s.point_light(position=[4.5, 5.0, 3.5], intensity=[55, 50, 42], radius=0.4)  # warm fill

# ---- geometry ----
s.add('rect', 'ground', scale(30))  # atrium floor

# Sparky: one OBJ, ten usemtl groups -> ten sub-meshes sharing one pose
SPARKY_POSE = [scale(1.0), rotate_y(18), translate(0.0, 0.0, -0.6)]
SPARKY_GROUPS = [
    ('GlassHead', 'sparkyGlass'),
    ('ScreenFace', 'sparkyScreen'),
    ('ScreenChest', 'sparkyScreen'),
    ('ScreenPalm', 'sparkyScreen'),
    ('EmitYellow', 'sparkyGlow'),
    ('MetalGrey', 'sparkyMetal'),
    ('PlasticBlue', 'sparkyBlue'),
    ('PlasticWhite', 'sparkyWhite'),
    ('AccentOrange', 'sparkyAccent'),
    ('TreadOrange', 'sparkyTread'),
]
for grp, mat in SPARKY_GROUPS:
    s.add(s.mesh('sparky_' + grp, '../assets/sparky.obj', normals='smooth', usemtl=grp),
          mat, *SPARKY_POSE)

# The herd, gathered around the robot
s.add('mesh:spot', 'spot', scale(1.0), rotate_y(150), translate(-2.15, 0.737, 0.55))  # textured cow (left front)
s.add('mesh:spot', 'gold', scale(1.12), rotate_y(190), translate(-1.5, 0.826, -1.75))  # gold cow (left back)
s.add('mesh:spot', 'glass', scale(1.0), rotate_y(155), translate(1.85, 0.737, 0.5))  # glass cow (right)

if __name__ == "__main__":
    s.run(out="03-spot-atrium.png")
