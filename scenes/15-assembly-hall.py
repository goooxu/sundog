#!/usr/bin/env python3
"""sundog scene 15-assembly-hall — The toy works, all features on the floor.

One wide look across the toy factory's assembly hall, built to put every
renderer capability in a single frame. Noon sun from an HDR sky pours
through the roof skylight (env importance sampling) into a bright pool on
the concrete floor; a pure-absorption smoke column from the furnace rises
through the beam and drops a volumetric shadow into the pool (flame
transmittance on shadow rays). The furnace corner glows with two emissive
flames, one of them seen only as a warm bloom through a frosted-glass
partition (GGX microfacet transmission). Candy-colored plastic Sparkys
ride the conveyor (coated two-lobe BSDF); the one QC woke up lights its
screens as textured mesh NEE lights. The capsule mascot supervises in
yellow plastic with an emissive antenna beacon, a UV-textured Spot waits
by the cooling pool, and a PhysX GPU pour freezes a crateful of toy
cows mid-tumble (--physics-time). A cooling pool up front reflects it all
through fbm waves and Beer-Lambert absorption; metal trusses span the
ceiling and the factory's gear logo hangs as an alpha-cutout disk. Three
mesh assets share the frame: spot, sparky, capsule_mascot.

Run: python3 scenes/15-assembly-hall.py
"""

import random

from scenelib import Scene, rotate_x, rotate_y, rotate_z, scale, translate

s = Scene()
s.render(width=2560, height=1440, spp=768, max_depth=12, clamp=8, seed=15,
         exposure=1.25)
s.camera(lookfrom=[1.0, 2.8, 10.2], lookat=[-1.2, 1.9, -2.0], vfov=44)
s.background_envmap('../assets/kloofendal_48d_partly_cloudy_puresky_4k.hdr',
                    rotate=165, intensity=1.2)
s.physics(gravity=[0, -9.8, 0], timestep=1 / 240, max_time=8.0,
          friction=0.6, restitution=0.25, stop_time=0.5)

# ---- textures & meshes ----
s.texture('spotSkin', 'image', file='textures/spot_texture.png', srgb=True)
s.texture('sparkyScreen', 'image', file='textures/sparky_albedo.png', srgb=True)
s.texture('runes', 'image', file='textures/runes.png', srgb=True)
s.texture('gear', 'image', file='textures/gear.png', srgb=True)
s.mesh('spot', '../assets/spot.obj', normals='smooth')

SPARKY_GROUPS = ['GlassHead', 'ScreenFace', 'ScreenChest', 'ScreenPalm',
                 'EmitYellow', 'MetalGrey', 'PlasticBlue', 'PlasticWhite',
                 'AccentOrange', 'TreadOrange']
for grp in SPARKY_GROUPS:
    s.mesh('sparky_' + grp, '../assets/sparky.obj', normals='smooth', usemtl=grp)

MASCOT_GROUPS = ['mascot_torso', 'mascot_visor', 'mascot_eye_left',
                 'mascot_eye_right', 'mascot_belt_flange', 'mascot_arm_left',
                 'mascot_glove_left', 'mascot_arm_right', 'mascot_glove_right',
                 'mascot_leg_left', 'mascot_boot_left', 'mascot_leg_right',
                 'mascot_boot_right', 'mascot_antenna_stem',
                 'mascot_antenna_tip']
for grp in MASCOT_GROUPS:
    s.mesh('m_' + grp, '../assets/capsule_mascot.obj', normals='smooth',
           usemtl=grp)

# ---- materials ----
s.lambert('floor', color=[0.52, 0.52, 0.54])          # concrete
s.lambert('wall', color=[0.38, 0.40, 0.44])
s.lambert('brick', color=[0.30, 0.16, 0.12])          # furnace shell
s.metal('truss', color=[0.42, 0.44, 0.48], roughness=0.35)
s.metal('beltFrame', color=[0.35, 0.37, 0.40], roughness=0.3)
s.plastic('belt', color=[0.09, 0.09, 0.10], roughness=0.3)
s.dielectric('frost', ior=1.5, roughness=0.35)        # partition pane
s.dielectric('dome', ior=1.5)                          # sparky head domes
s.metal('joints', color=[0.45, 0.47, 0.5], roughness=0.35)
s.plastic('tread', color=[0.12, 0.12, 0.13], roughness=0.5)
s.plastic('screenOff', color=[0.03, 0.03, 0.035], roughness=0.08)
s.plastic('coreOff', color=[0.30, 0.20, 0.06], roughness=0.2)
s.emissive('screenOn', texture='sparkyScreen', intensity=3.0)
s.emissive('coreOn', color=[1.0, 0.85, 0.2], intensity=6.0)
s.emissive('runeSign', texture='runes', intensity=0.7)
s.lambert('spotMat', texture='spotSkin')
s.water('pool', wave_amp=0.035, wave_freq=3.0)
# mascot palette (MTL Kd intent, plastic where it looks molded)
s.plastic('mBody', color=[0.95, 0.82, 0.18], roughness=0.12)
s.plastic('mVisor', color=[0.05, 0.055, 0.06], roughness=0.06)
s.plastic('mEye', color=[0.85, 0.85, 0.88], roughness=0.1)
s.plastic('mBelt', color=[0.10, 0.18, 0.35], roughness=0.2)
s.plastic('mRubber', color=[0.22, 0.12, 0.06], roughness=0.45)
s.emissive('mBeacon', color=[1.0, 0.9, 0.3], intensity=8.0)
# conveyor toys: candy plastic ladder (subset of scene 14's palette)
TOYS = [('cherry', [0.85, 0.12, 0.10], 0.05, -10.3),
        ('tangerine', [0.95, 0.45, 0.08], 0.12, -8.0),
        ('lemon', [0.95, 0.78, 0.10], 0.15, -5.7),    # the awake one
        ('mint', [0.10, 0.65, 0.55], 0.35, -3.4)]
AWAKE = 'lemon'
for name, c, rough, _ in TOYS:
    s.plastic(name, color=c, roughness=rough)
    s.plastic(name + 'W', color=[0.80, 0.81, 0.84], roughness=rough)
    s.plastic(name + 'A', color=[c[0] * 0.45, c[1] * 0.45, c[2] * 0.45],
              roughness=rough)

# ---- hall shell (28 wide x ~13 high x 28 deep; skylight gap in the roof) ----
s.add('rect', 'floor', scale(16), physics={'thickness': 0.5})
s.add('rect', 'wall', scale(16, 1, 6.5), rotate_x(90), translate(0, 6.5, -14))
# west wall with three tall windows (y 5.5-10, width 2.2): the due-west
# 48-deg sun drops three light stripes onto the floor around x -9..-5,
# brushing the cooling pool and the waiting Spot
s.add('rect', 'wall', scale(2.75, 1, 15), rotate_z(-90), translate(-14, 2.75, 0))
s.add('rect', 'wall', scale(1.5, 1, 15), rotate_z(-90), translate(-14, 11.5, 0))
for seg_c, seg_hw in ((-12.05, 1.95), (-5.5, 2.4), (1.5, 2.4), (10.05, 3.95)):
    s.add('rect', 'wall', scale(2.25, 1, seg_hw), rotate_z(-90),
          translate(-14, 7.75, seg_c))
s.add('rect', 'wall', scale(6.5, 1, 15), rotate_z(90), translate(14, 6.5, 0))
# roof as four slabs around a 7x5 skylight opening centered at (-6, z=-2.25):
# with the sun due west at 48 deg (rotate=165), the beam through the opening
# lands mid-right around x~5.6 where the mascot and the pour crate stand,
# and the smoke column (x=3.2) rises through its mid-section.
s.add('rect', 'wall', scale(16, 1, 4.625), rotate_x(180), translate(0, 12.9, -9.375))
s.add('rect', 'wall', scale(16, 1, 6.875), rotate_x(180), translate(0, 12.9, 7.125))
s.add('rect', 'wall', scale(2.25, 1, 2.5), rotate_x(180), translate(-11.75, 12.9, -2.25))
s.add('rect', 'wall', scale(8.25, 1, 2.5), rotate_x(180), translate(5.75, 12.9, -2.25))
# trusses under the roof
for tz in (-9, -2.5, 4):
    s.add('cylinder', 'truss', scale(0.18, 5.2, 0.18), rotate_z(90),
          translate(0, 11.6, tz))

# ---- gear logo: alpha-cutout disk hung INSIDE the sun shaft — the beam
# projects its cog silhouette into the floor pool below ----
s.add('disk', 'truss', scale(1.6), rotate_z(48), translate(-1.6, 8.6, -2.4),
      cutout='gear', nee=False)

# ---- furnace corner (right back): two flames + smoke column ----
s.add('rect', 'brick', scale(2.6, 1, 2.2), rotate_x(90), translate(9.0, 2.2, -11.75))
s.add('rect', 'brick', scale(2.6, 1, 2.6), translate(9.0, 4.4, -12.0))  # mantel
s.flame(base=[6.0, 0.05, -11.6], height=2.6, radius=0.6, intensity=16,
        sigma=4.0, noise_scale=2.8, seed=51, light_intensity=13)
s.flame(base=[10.0, 0.05, -11.5], height=1.8, radius=0.42, intensity=12,
        sigma=4.5, noise_scale=3.2, seed=52, light_intensity=9)
# pure-absorption smoke column rising through the shaft mid-section (the
# beam occupies y 0..6.6 at x=3.2), dropping its volumetric shadow into
# the floor pool right of the mascot
s.flame(base=[3.2, 0.05, -3.3], height=10.5, radius=0.7, intensity=0.0,
        sigma=1.3, noise_scale=2.0, seed=53, light_intensity=0.001)

# ---- frosted partition screening the furnace corner ----
s.add('rect', 'frost', scale(2.4, 1, 2.1), rotate_x(90), translate(5.6, 2.1, -9.2))
s.add('rect', 'beltFrame', scale(0.12, 1, 2.15), rotate_x(90),
      translate(3.1, 2.15, -9.2))
s.add('rect', 'beltFrame', scale(0.12, 1, 2.15), rotate_x(90),
      translate(8.1, 2.15, -9.2))

# ---- rune sign on the back wall (textured area light) ----
s.add('rect', 'runeSign', scale(1.6, 1, 0.8), rotate_x(90),
      translate(-9.5, 5.2, -13.95))

# ---- conveyor with the plastic Sparky queue ----
s.add('rect', 'belt', scale(6.0, 1, 1.05), translate(-6.5, 1.28, -3.2))
s.add('rect', 'beltFrame', scale(6.0, 1, 0.14), rotate_x(90),
      translate(-6.5, 1.14, -2.15))
for lx in (-11.7, -8.9, -6.1, -3.3, -0.7):
    s.add('rect', 'beltFrame', scale(0.5, 1, 1.0), rotate_z(90),
          translate(lx, 0.62, -3.2))
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
        s.add('mesh:sparky_' + grp, mat, scale(0.92), rotate_y(12),
              translate(x, 1.3, -3.2))

# ---- the mascot supervisor, on the floor by the belt ----
MPOSE = [scale(1.15), rotate_y(-24), translate(2.6, 0.02, -1.0)]
MMAT = {'mascot_visor': 'mVisor', 'mascot_eye_left': 'mEye',
        'mascot_eye_right': 'mEye', 'mascot_belt_flange': 'mBelt',
        'mascot_glove_left': 'mRubber', 'mascot_glove_right': 'mRubber',
        'mascot_boot_left': 'mRubber', 'mascot_boot_right': 'mRubber',
        'mascot_antenna_stem': 'joints', 'mascot_antenna_tip': 'mBeacon'}
for grp in MASCOT_GROUPS:
    s.add('mesh:m_' + grp, MMAT.get(grp, 'mBody'), *MPOSE)

# ---- a full-size textured Spot waiting at the end of the line ----
s.add('mesh:spot', 'spotMat', scale(0.9), rotate_y(155),
      translate(-7.3, 0.665, 1.7))

# ---- PhysX pour: a crateful of toy cows frozen mid-tumble (right front) ----
s.add('rect', 'beltFrame', scale(1.9, 1, 1.9), translate(5.0, 0.02, 1.2),
      physics={'thickness': 0.3})                       # crate base plate
for wx, hh, rot in ((3.25, 0.5, 90), (6.75, 0.9, -90)):
    s.add('rect', 'beltFrame', scale(hh, 1, 1.75), rotate_z(rot),
          translate(wx, hh, 1.2), physics={'thickness': 0.25})
for wz, rot in ((-0.55, -90), (2.95, 90)):
    s.add('rect', 'beltFrame', scale(1.75, 1, 0.55 if rot == -90 else 0.9),
          rotate_x(rot), translate(5.0, 0.55 if rot == -90 else 0.9, wz),
          physics={'thickness': 0.25})
random.seed(15)
for i in range(26):
    gx = 5.0 + (i % 3 - 1) * 0.75 + random.uniform(-0.2, 0.2)
    gz = 1.2 + (i // 3 % 3 - 1) * 0.75 + random.uniform(-0.2, 0.2)
    gy = 2.6 + (i // 9) * 0.85
    s.add('mesh:spot', 'spotMat', scale(0.34), rotate_y(random.uniform(0, 360)),
          translate(gx, gy, gz),
          physics={'dynamic': True, 'density': 300,
                   'velocity': [0, -1.2, 0],
                   'angular_velocity': [random.uniform(-3, 3),
                                        random.uniform(-3, 3),
                                        random.uniform(-3, 3)]})

# ---- cooling pool (front left), metal rim ----
s.add('rect', 'pool', scale(3.8, 1, 2.3), translate(-4.6, 0.28, 4.8))
s.add('rect', 'beltFrame', scale(3.95, 1, 0.16), rotate_x(90),
      translate(-4.6, 0.15, 7.26))
s.add('rect', 'beltFrame', scale(0.16, 1, 2.45), rotate_z(90),
      translate(-0.65, 0.15, 4.8))

if __name__ == "__main__":
    s.run(out="15-assembly-hall.png")
