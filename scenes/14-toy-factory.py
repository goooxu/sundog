#!/usr/bin/env python3
"""sundog scene 14-toy-factory — Five plastic Sparkys on the line.

A quality-control shelf in a toy factory: five candy-colored Sparky
robots fresh out of the same mold, lined up under one big overhead light
panel. Their shells are the new plastic material — a diffuse base under
a glossy dielectric coat — and the coat roughness steps 0.03 -> 0.6
left to right: the cherry-red toy wears a wet clear-coat highlight, the
grape-purple one has gone fully matte. Everything else is held constant
so only the coat tells them apart. The center toy (factory-default
roughness 0.15) is the one QC just powered on: its face, chest and palm
screens glow as textured mesh NEE lights while its four siblings sleep
with dark glossy screens — unlit plastic too. Metal joints, glass head
domes and the rubber-look treads round out the material mix.

Run: python3 scenes/14-toy-factory.py
"""

from scenelib import Scene, rotate_x, scale, translate

s = Scene()
s.render(width=1920, height=1080, spp=384, max_depth=12, clamp=8, seed=14)
s.camera(lookfrom=[0.0, 1.15, 7.6], lookat=[0.0, 1.05, 0.0], vfov=35)
s.background_gradient(horizon=[0.55, 0.56, 0.60], zenith=[0.16, 0.19, 0.26])

# ---- textures & meshes ----
s.texture('sparkyScreen', 'image', file='textures/sparky_albedo.png', srgb=True)
SPARKY_GROUPS = ['GlassHead', 'ScreenFace', 'ScreenChest', 'ScreenPalm',
                 'EmitYellow', 'MetalGrey', 'PlasticBlue', 'PlasticWhite',
                 'AccentOrange', 'TreadOrange']
for grp in SPARKY_GROUPS:
    s.mesh('sparky_' + grp, '../assets/sparky.obj', normals='smooth', usemtl=grp)

# ---- materials ----
s.lambert('floor', color=[0.58, 0.58, 0.61])
s.lambert('wall', color=[0.30, 0.33, 0.38])
s.plastic('belt', color=[0.09, 0.09, 0.10], roughness=0.3)   # conveyor rubber
s.dielectric('dome', ior=1.5)                                # head domes
s.metal('joints', color=[0.45, 0.47, 0.5], roughness=0.35)
s.plastic('tread', color=[0.12, 0.12, 0.13], roughness=0.5)
s.plastic('screenOff', color=[0.03, 0.03, 0.035], roughness=0.08)  # dark glossy
s.plastic('coreOff', color=[0.30, 0.20, 0.06], roughness=0.2)      # unlit amber
s.emissive('screenOn', texture='sparkyScreen', intensity=3.0)
s.emissive('coreOn', color=[1.0, 0.85, 0.2], intensity=6.0)
s.emissive('panel', color=[1.0, 0.98, 0.93], intensity=18.0)
s.emissive('tube', color=[1.0, 0.99, 0.95], intensity=60.0)

# The ladder: same mold, one coat parameter apart. (name, shell color,
# coat roughness, x). Whites stay at 0.80 — plastic whites already sit
# ~10% below lambert (the coupling sent that energy into the highlight),
# and the tube streak wants the extra headroom.
TOYS = [('cherry', [0.85, 0.12, 0.10], 0.03, -3.1),
        ('tangerine', [0.95, 0.45, 0.08], 0.08, -1.55),
        ('lemon', [0.95, 0.78, 0.10], 0.15, 0.0),   # QC default -> powered on
        ('mint', [0.10, 0.65, 0.55], 0.30, 1.55),
        ('grape', [0.45, 0.20, 0.65], 0.60, 3.1)]
AWAKE = 'lemon'
for name, c, rough, _ in TOYS:
    s.plastic(name, color=c, roughness=rough)
    s.plastic(name + 'W', color=[0.80, 0.81, 0.84], roughness=rough)
    s.plastic(name + 'A', color=[c[0] * 0.45, c[1] * 0.45, c[2] * 0.45],
              roughness=rough)

# ---- geometry ----
s.add('rect', 'floor', scale(30))
s.add('rect', 'wall', scale(30, 1, 12), rotate_x(90), translate(0, 12, -7.5))
s.add('rect', 'belt', scale(5.0, 1, 1.15), translate(0, 0.01, 0))
# Soft key panel straight overhead lights the shelf; the ladder itself is
# read off a long thin tube BEHIND the camera at reflection height: Sparky's
# shells are flat boxes, so a light only shows up in them when it sits on
# the mirror path face -> camera. The tube's reflected image is one sharp
# streak on the clear-coat end and a wide bloom on the matte end.
s.add('rect', 'panel', scale(3.0, 1, 1.0), rotate_x(180), translate(0, 4.8, 0.4))
# Tube length: a flat face keeps the lateral component on reflection, so with
# shell fronts near z = 0.3 the outer toys (x = +-3.1) see the tube at about
# +-3.1 * (1 + 12.9/7.3) ~= +-8.6 (face-to-tube over camera-to-face depth) —
# the tube must span that or the ladder's end toys lose their streak.
s.add('rect', 'tube', scale(9.5, 1, 0.20), rotate_x(-90), translate(0, 1.28, 13.2))

# ---- lights ----
s.distant_light(direction=[0.3, -1.0, -0.4], radiance=[0.15, 0.16, 0.18])  # cool fill

for name, _, _, x in TOYS:
    awake = name == AWAKE
    for grp, mat in [('GlassHead', 'dome'),
                     ('ScreenFace', 'screenOn' if awake else 'screenOff'),
                     ('ScreenChest', 'screenOn' if awake else 'screenOff'),
                     ('ScreenPalm', 'screenOn' if awake else 'screenOff'),
                     ('EmitYellow', 'coreOn' if awake else 'coreOff'),
                     ('MetalGrey', 'joints'),
                     ('PlasticBlue', name),
                     ('PlasticWhite', name + 'W'),
                     ('AccentOrange', name + 'A'),
                     ('TreadOrange', 'tread')]:
        s.add('mesh:sparky_' + grp, mat, scale(1.0), translate(x, 0.02, 0))

if __name__ == "__main__":
    s.run(out="14-toy-factory.png")
