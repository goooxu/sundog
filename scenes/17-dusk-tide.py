#!/usr/bin/env python3
"""sundog scene 17-dusk-tide — Observatory on the shore, just after sunset.

A coastal platform after sundown. Spot stands on the central inspection
table; Sparky operates a multi-material optical instrument (chrome tube,
glass objective, metal tripod, a lit indicator screen); the capsule
mascot calibrates a textured metal sample plate on the side bench. A
low warm sun (distant light a few degrees up) and a cold zenith fill
carve ceramic, rough metal, chrome, low-roughness coated shells and
textured surfaces out of the dusk; the sky itself is a twilight
gradient. The foreground wave pool mirrors it all through fbm normal
perturbation and RGB Beer absorption — cold sky and the warm flame
beacon interleaved in one reflection. (Renderer-honest approximations:
no normal maps, so the sample plate is an albedo grid texture on metal;
the water is a smooth Fresnel interface with perturbed normals, not a
microfacet one; the sunset is gradient + distant, no HDRI.)

Run: python3 scenes/17-dusk-tide.py
"""

from scenelib import Scene, rotate_x, rotate_y, rotate_z, scale, translate

s = Scene()
s.render(width=2560, height=1440, spp=768, max_depth=12, clamp=8, seed=17,
         exposure=1.2)
s.camera(lookfrom=[0.6, 1.6, 8.7], lookat=[-0.6, 1.15, -5.0], vfov=41)
s.background_gradient(horizon=[0.42, 0.16, 0.06], zenith=[0.03, 0.035, 0.11])

# ---- textures & meshes ----
s.texture('spotSkin', 'image', file='textures/spot_texture.avif', srgb=True)
s.texture('sparkyScreen', 'image', file='textures/sparky_albedo.avif', srgb=True)
s.texture('swatch', 'grid', a=[0.85, 0.86, 0.88], b=[0.22, 0.24, 0.28],
          scale=[10, 7], width=0.09)
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

# ---- materials (the description's material roll-call) ----
s.lambert('deck', color=[0.30, 0.30, 0.34])            # concrete platform
s.lambert('stone', color=[0.30, 0.28, 0.27])
s.metal('roughSteel', color=[0.40, 0.42, 0.46], roughness=0.45)  # rough metal
s.metal('chrome', color=[0.90, 0.91, 0.93], roughness=0.02)      # chrome
s.metal('sample', texture='swatch', roughness=0.2)     # textured metal plate
s.plastic('ceramic', color=[0.88, 0.88, 0.90], roughness=0.06)   # ceramic
s.plastic('shell', color=[0.16, 0.20, 0.30], roughness=0.08)     # optics shell
s.dielectric('lens', ior=1.5)
s.water('sea', wave_amp=0.06, wave_freq=2.2, absorb=[0.55, 0.18, 0.09])
s.emissive('worklamp', color=[1.0, 0.92, 0.80], intensity=26.0)
s.emissive('indicator', texture='sparkyScreen', intensity=4.0)
s.lambert('spotMat', texture='spotSkin')
# sparky (operator: screens lit)
s.dielectric('dome', ior=1.5)
s.metal('joints', color=[0.45, 0.47, 0.5], roughness=0.35)
s.plastic('tread', color=[0.12, 0.12, 0.13], roughness=0.5)
s.emissive('screenOn', texture='sparkyScreen', intensity=3.0)
s.emissive('coreOn', color=[1.0, 0.85, 0.2], intensity=5.0)
s.plastic('sBlue', color=[0.35, 0.65, 0.9], roughness=0.15)
s.plastic('sWhite', color=[0.80, 0.81, 0.84], roughness=0.15)
s.plastic('sAccent', color=[0.95, 0.45, 0.12], roughness=0.15)
# mascot
s.plastic('mBody', color=[0.95, 0.82, 0.18], roughness=0.12)
s.plastic('mVisor', color=[0.05, 0.055, 0.06], roughness=0.06)
s.plastic('mEye', color=[0.85, 0.85, 0.88], roughness=0.1)
s.plastic('mBelt', color=[0.10, 0.18, 0.35], roughness=0.2)
s.plastic('mRubber', color=[0.22, 0.12, 0.06], roughness=0.45)

# ---- lights: low warm sun + cold zenith fill + the work lamp ----
s.distant_light(direction=[-0.86, -0.10, -0.50], radiance=[3.2, 1.35, 0.5])
s.distant_light(direction=[0.35, -0.75, 0.56], radiance=[0.05, 0.07, 0.13])

# ---- sea + platform ----
s.add('rect', 'sea', scale(45), translate(0, 0.0, 0))
s.add('rect', 'deck', scale(6.5, 1, 5.2), translate(0, 0.52, -4.4))   # main deck
s.add('rect', 'roughSteel', scale(6.6, 1, 0.27), rotate_x(90),
      translate(0, 0.27, 0.82))                        # deck fascia toward camera
s.add('rect', 'deck', scale(1.6, 1, 2.2), translate(-4.2, 0.52, 1.4))  # side jetty
for px, pz in ((-6.2, -8.2), (6.2, -8.2), (-6.2, -0.6), (6.2, -0.6)):
    s.add('cylinder', 'stone', scale(0.32, 0.9, 0.32), translate(px, 0.0, pz))

# ---- central inspection table with Spot ----
s.add('cylinder', 'roughSteel', scale(1.45, 0.42, 1.45), translate(-0.6, 0.94, -4.6))
s.add('mesh:spot', 'spotMat', scale(0.95), rotate_y(195),
      translate(-0.6, 2.06, -4.6))
# ceramic roll-call: two insulator stacks on the table edge
for cx, cz in ((0.6, -3.9), (0.35, -5.3)):
    s.add('cylinder', 'ceramic', scale(0.13, 0.16, 0.13), translate(cx, 1.52, cz))
    s.add('sphere', 'ceramic', scale(0.11), translate(cx, 1.8, cz))

# ---- Sparky at the optical instrument (right of the table) ----
SPARKY_POSE = [scale(0.95), rotate_y(-58), translate(2.9, 0.53, -3.4)]
SPARKY_MATS = {'GlassHead': 'dome', 'ScreenFace': 'screenOn',
               'ScreenChest': 'screenOn', 'ScreenPalm': 'screenOn',
               'EmitYellow': 'coreOn', 'MetalGrey': 'joints',
               'PlasticBlue': 'sBlue', 'PlasticWhite': 'sWhite',
               'AccentOrange': 'sAccent', 'TreadOrange': 'tread'}
for grp in SPARKY_GROUPS:
    s.add('mesh:sparky_' + grp, SPARKY_MATS[grp], *SPARKY_POSE)
# instrument: tripod + chrome tube + glass objective + coated shell + screen
s.add('cylinder', 'roughSteel', scale(0.42, 0.05, 0.42), translate(4.35, 0.58, -4.3))
s.add('cylinder', 'roughSteel', scale(0.08, 0.62, 0.08), translate(4.35, 1.25, -4.3))
s.add('cylinder', 'shell', scale(0.30, 0.20, 0.30), translate(4.35, 2.05, -4.3))
s.add('cylinder', 'chrome', scale(0.16, 0.72, 0.16), rotate_x(62),
      translate(4.35, 2.45, -4.75))
s.add('sphere', 'lens', scale(0.19), translate(4.35, 2.79, -5.32))
s.add('rect', 'indicator', scale(0.30, 1, 0.20), rotate_x(75), rotate_y(-30),
      translate(3.75, 1.7, -3.7))

# ---- Capsule at the side bench with the textured sample plate ----
MASCOT_POSE = [scale(0.95), rotate_y(128), translate(-3.95, 0.53, 1.5)]
MASCOT_MATS = {'mascot_visor': 'mVisor', 'mascot_eye_left': 'mEye',
               'mascot_eye_right': 'mEye', 'mascot_belt_flange': 'mBelt',
               'mascot_glove_left': 'mRubber', 'mascot_glove_right': 'mRubber',
               'mascot_boot_left': 'mRubber', 'mascot_boot_right': 'mRubber',
               'mascot_antenna_stem': 'joints', 'mascot_antenna_tip': 'mBody'}
for grp in MASCOT_GROUPS:
    s.add('mesh:m_' + grp, MASCOT_MATS.get(grp, 'mBody'), *MASCOT_POSE)
s.add('rect', 'sample', scale(0.55, 1, 0.4), rotate_x(72), rotate_y(24),
      translate(-2.75, 0.92, 0.30))                     # leaning sample plate
s.add('rect', 'roughSteel', scale(0.6, 1, 0.06), rotate_x(90),
      translate(-2.7, 0.62, 0.55))                      # plate rest

# ---- work lamp over the table (limited local light) ----
s.add('cylinder', 'roughSteel', scale(0.06, 1.3, 0.06), translate(-2.6, 1.82, -6.4))
s.add('rect', 'worklamp', scale(0.34, 1, 0.24), rotate_x(168),
      translate(-2.55, 3.18, -6.35))

# ---- sunset afterglow slab (far off-camera, nee off): its grazing
# reflection draws the warm lane across the water ----
s.emissive('afterglow', color=[1.0, 0.35, 0.10], intensity=3.5)
# a thin bright band hugging the horizon far out: reads as the sunset
# afterglow itself, and its grazing reflection draws the lane on the sea
s.add('rect', 'afterglow', scale(20, 1, 0.5), rotate_x(90),
      translate(-6, 1.15, -120), nee=False)

# ---- flame beacon on the far side ----
s.add('cylinder', 'stone', scale(0.5, 1.8, 0.5), translate(-10.5, 1.8, -14.5))
s.add('disk', 'stone', scale(0.62), translate(-10.5, 3.61, -14.5))
s.flame(base=[-10.5, 3.64, -14.5], height=1.7, radius=0.45, intensity=15,
        sigma=3.6, noise_scale=3.0, seed=71, light_intensity=11)

if __name__ == "__main__":
    s.run(out="17-dusk-tide.avif")
