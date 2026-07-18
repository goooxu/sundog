#!/usr/bin/env python3
"""sundog scene 16-atelier — A workshop corner, everything just landed.

Cold grey walls frame a workshop corner; a procedural flame in the back
fireplace washes the stone hearth and cold ashes in warm orange. In front
of it, colored plastic bricks, metal spheres, frosted-glass orbs — and
Capsule, Spot and Sparky themselves — have all been dropped from the air,
their rest poses computed entirely by a short PhysX simulation before
OptiX draws the IAS. The multi-material robots shed parts where they hit
(each usemtl group is its own rigid body — the tumble is the point). A
shallow stone basin holds clear Beer-Lambert water front-right; a brushed
metal jug and a small silver sphere lean against the wall. A soft area
key and a tight slanted spot draw soft shadows on the floor while a dim
HDR sky brushes highlights onto metal and glass through the open side.

Run: python3 scenes/16-atelier.py
"""

import random

from scenelib import Scene, rotate_x, rotate_y, rotate_z, scale, translate

s = Scene()
s.render(width=2560, height=1440, spp=768, max_depth=12, clamp=8, seed=16,
         exposure=1.15)
s.camera(lookfrom=[5.2, 3.3, 7.0], lookat=[-0.9, 1.0, -3.0], vfov=40)
s.background_envmap('../assets/kloofendal_48d_partly_cloudy_puresky_4k.hdr',
                    rotate=200, intensity=0.15)
s.physics(gravity=[0, -9.8, 0], timestep=1 / 240, max_time=8.0,
          friction=0.55, restitution=0.3, stop_time=1.5)

# ---- textures & meshes ----
s.texture('spotSkin', 'image', file='textures/spot_texture.avif', srgb=True)
s.texture('sparkyScreen', 'image', file='textures/sparky_albedo.avif', srgb=True)
s.mesh('spot', '../assets/spot.obj', normals='smooth')
s.mesh('box', '../assets/box.obj', normals='flat')
s.mesh('brick', '../assets/brick.obj', normals='flat')

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
s.lambert('wall', color=[0.44, 0.46, 0.50])           # cold grey
s.lambert('floor', color=[0.42, 0.42, 0.45])
s.lambert('brick', color=[0.27, 0.13, 0.10])
s.lambert('stone', color=[0.42, 0.40, 0.38])          # hearth slab
s.lambert('ash', color=[0.035, 0.032, 0.032])
s.metal('jug', color=[0.55, 0.56, 0.58], roughness=0.25)
s.metal('silver', color=[0.92, 0.92, 0.94], roughness=0.05)
s.metal('gold', color=[1.0, 0.78, 0.34], roughness=0.15)
s.metal('copper', color=[0.95, 0.54, 0.38], roughness=0.3)
s.dielectric('frostBall', ior=1.5, roughness=0.25)
s.dielectric('frostBall2', ior=1.5, roughness=0.35)
s.water('basin', wave_amp=0.025, wave_freq=4.5, absorb=[0.30, 0.10, 0.05])
s.emissive('key', color=[1.0, 0.98, 0.94], intensity=10.0)
s.emissive('spotlamp', color=[1.0, 0.96, 0.88], intensity=450.0)
s.lambert('spotMat', texture='spotSkin')
# bricks: candy plastic (the v0.15 coat does the toy-brick look)
BRICK_COLORS = [('bRed', [0.85, 0.14, 0.12]), ('bOrange', [0.95, 0.46, 0.10]),
                ('bYellow', [0.94, 0.77, 0.12]), ('bMint', [0.12, 0.64, 0.54]),
                ('bBlue', [0.16, 0.42, 0.82]), ('bGrape', [0.46, 0.22, 0.64])]
for name, c in BRICK_COLORS:
    s.plastic(name, color=c, roughness=0.12)
# sparky group materials (screens off — the robot just tumbled in)
s.dielectric('dome', ior=1.5)
s.metal('joints', color=[0.45, 0.47, 0.5], roughness=0.35)
s.plastic('tread', color=[0.12, 0.12, 0.13], roughness=0.5)
s.plastic('screenOff', color=[0.03, 0.03, 0.035], roughness=0.08)
s.plastic('coreOff', color=[0.30, 0.20, 0.06], roughness=0.2)
s.plastic('sBlue', color=[0.35, 0.65, 0.9], roughness=0.15)
s.plastic('sWhite', color=[0.80, 0.81, 0.84], roughness=0.15)
s.plastic('sAccent', color=[0.95, 0.45, 0.12], roughness=0.15)
# mascot palette
s.plastic('mBody', color=[0.95, 0.82, 0.18], roughness=0.12)
s.plastic('mVisor', color=[0.05, 0.055, 0.06], roughness=0.06)
s.plastic('mEye', color=[0.85, 0.85, 0.88], roughness=0.1)
s.plastic('mBelt', color=[0.10, 0.18, 0.35], roughness=0.2)
s.plastic('mRubber', color=[0.22, 0.12, 0.06], roughness=0.45)
s.plastic('ceramicPot', color=[0.88, 0.88, 0.90], roughness=0.06)

# ---- room: two walls + floor, open on the camera sides ----
s.add('rect', 'floor', scale(12), physics={'thickness': 0.5})
s.add('rect', 'wall', scale(12, 1, 4.0), rotate_x(90), translate(0, 4.0, -8))
s.add('rect', 'wall', scale(4.0, 1, 12), rotate_z(-90), translate(-8, 4.0, 0))
s.add('rect', 'wall', scale(12), rotate_x(180), translate(0, 8.0, 0))  # ceiling

# ---- fireplace: brick surround + stone hearth + flame + ashes ----
s.add('rect', 'brick', scale(0.35, 1, 1.5), rotate_x(90), translate(-3.55, 1.5, -7.9))
s.add('rect', 'brick', scale(0.35, 1, 1.5), rotate_x(90), translate(-0.45, 1.5, -7.9))
s.add('rect', 'brick', scale(1.9, 1, 0.5), rotate_x(90), translate(-2.0, 3.0, -7.9))
s.add('rect', 'ash', scale(1.55, 1, 0.05), rotate_x(90), translate(-2.0, 2.55, -7.88))
s.add('rect', 'brick', scale(1.55, 1, 0.7), translate(-2.0, 0.02, -7.6))  # firebox floor
s.add('rect', 'ash', scale(1.5, 1, 1.25), rotate_x(90), translate(-2.0, 1.25, -7.93))
s.add('rect', 'stone', scale(2.3, 1, 0.85), translate(-2.0, 0.14, -6.55),
      physics={'thickness': 0.28})                       # hearth slab
s.add('disk', 'ash', scale(0.9), translate(-2.0, 0.155, -6.4))
s.flame(base=[-2.0, 0.18, -7.35], height=1.7, radius=0.5, intensity=15,
        sigma=4.2, noise_scale=2.9, seed=61, light_intensity=12)

# ---- the drop: bricks, spheres, orbs, and the three mascots ----
random.seed(16)
bx = [(-0.9, 1.3, -4.6), (0.2, 1.7, -4.1), (1.1, 1.4, -4.9),
      (-1.7, 2.0, -3.9), (0.7, 2.3, -3.5), (-0.2, 2.7, -4.4),
      (1.8, 1.9, -3.8), (-1.1, 3.0, -4.9), (0.3, 3.3, -3.9),
      (2.3, 2.5, -4.5)]
for i, (x, y, z) in enumerate(bx):
    name = BRICK_COLORS[i % len(BRICK_COLORS)][0]
    # dynamic bodies need uniform scale: cubes and pre-stretched bricks
    mesh = 'mesh:brick' if i % 2 else 'mesh:box'
    s.add(mesh, name, scale(random.uniform(0.38, 0.52)),
          rotate_y(random.uniform(0, 360)), translate(x, y, z),
          physics={'dynamic': True, 'density': 350,
                   'angular_velocity': [random.uniform(-2, 2),
                                        random.uniform(-2, 2),
                                        random.uniform(-2, 2)]})
for mat, r, x, y, z in (('gold', 0.42, -0.4, 1.1, -2.7),
                        ('silver', 0.3, 1.5, 1.5, -2.4),
                        ('copper', 0.36, -2.4, 1.6, -3.2),
                        ('frostBall', 0.38, 0.9, 2.0, -2.9),
                        ('frostBall2', 0.34, -1.4, 1.2, -2.2)):
    s.add('sphere', mat, scale(r), translate(x, y, z),
          physics={'dynamic': True, 'density': 900 if mat in ('gold', 'silver', 'copper') else 400,
                   'angular_velocity': [random.uniform(-3, 3), 0,
                                        random.uniform(-3, 3)]})
# Spot: single mesh, lands whole
s.add('mesh:spot', 'spotMat', scale(0.85), rotate_y(140), translate(2.6, 1.1, -2.0),
      physics={'dynamic': True, 'density': 320,
               'angular_velocity': [0.8, 1.5, -0.6]})
# Sparky: every usemtl group is its own rigid body — he lands in pieces
# on purpose (his parts nest snugly, so the tumble stays gentle; the
# mascot's deeply interpenetrating hulls would explode instead, so he
# stands by the basin and watches)
SPARKY_MATS = {'GlassHead': 'dome', 'ScreenFace': 'screenOff',
               'ScreenChest': 'screenOff', 'ScreenPalm': 'screenOff',
               'EmitYellow': 'coreOff', 'MetalGrey': 'joints',
               'PlasticBlue': 'sBlue', 'PlasticWhite': 'sWhite',
               'AccentOrange': 'sAccent', 'TreadOrange': 'tread'}
for grp in SPARKY_GROUPS:
    s.add('mesh:sparky_' + grp, SPARKY_MATS[grp], scale(0.72), rotate_y(-35),
          translate(1.6, 0.55, -5.4),
          physics={'dynamic': True, 'density': 260})
MASCOT_MATS = {'mascot_visor': 'mVisor', 'mascot_eye_left': 'mEye',
               'mascot_eye_right': 'mEye', 'mascot_belt_flange': 'mBelt',
               'mascot_glove_left': 'mRubber', 'mascot_glove_right': 'mRubber',
               'mascot_boot_left': 'mRubber', 'mascot_boot_right': 'mRubber',
               'mascot_antenna_stem': 'joints', 'mascot_antenna_tip': 'mBody'}
for grp in MASCOT_GROUPS:
    s.add('mesh:m_' + grp, MASCOT_MATS.get(grp, 'mBody'), scale(0.85),
          rotate_y(-120), translate(1.6, 0.02, 1.0))

# ---- left wall dressing: tool board and a small shelf ----
s.lambert('board', color=[0.30, 0.22, 0.15])
s.add('rect', 'board', scale(0.9, 1, 1.3), rotate_z(-90), translate(-7.9, 3.1, -3.2))
for i, tz in enumerate((-3.8, -3.2, -2.6)):
    s.add('cylinder', 'joints', scale(0.045, 0.42, 0.045),
          translate(-7.75, 3.0 - 0.12 * i, tz))
s.add('rect', 'stone', scale(0.5, 1, 1.1), translate(-7.55, 2.0, -0.4))
s.add('cylinder', 'copper', scale(0.16, 0.2, 0.16), translate(-7.6, 2.22, -0.8))
s.add('cylinder', 'ceramicPot', scale(0.14, 0.17, 0.14), translate(-7.6, 2.19, 0.1))

# ---- stone basin with clear water (front right) ----
s.add('cylinder', 'stone', scale(1.35, 0.19, 1.35), translate(3.3, 0.19, 1.9))
s.lambert('basinFloor', color=[0.20, 0.22, 0.26])
s.add('disk', 'basinFloor', scale(1.22), translate(3.3, 0.30, 1.9))
s.add('disk', 'basin', scale(1.18), translate(3.3, 0.40, 1.9))

# ---- jug and silver ball against the wall ----
s.add('cylinder', 'jug', scale(0.42, 0.55, 0.42), translate(-6.6, 0.55, -5.6))
s.add('sphere', 'silver', scale(0.26), translate(-5.7, 0.26, -6.1))

# ---- lights: soft area key overhead + tight slanted spot ----
s.add('rect', 'key', scale(1.6, 1, 1.1), rotate_x(180), translate(0.5, 6.9, -3.0))
s.add('rect', 'spotlamp', scale(0.30, 1, 0.30), rotate_x(-138), rotate_y(30),
      translate(4.2, 5.9, 1.8))

if __name__ == "__main__":
    s.run(out="16-atelier.avif")
