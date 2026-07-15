#!/usr/bin/env python3
"""sundog scene 10-suncatcher — Suncatcher: sky-only illumination study.

There is not a single explicit light in this scene: 100% of the illumination
comes from one 4k HDR clear-sky environment map (Poly Haven, CC0). Five metal
spheres form a roughness ladder across the lawn — the mirror end pulls in the
drifting clouds and the golden cow, while the rough end smears the sky into
broad highlights — and a glass sphere on a stone pedestal folds the entire
sky upside-down into its core. Environment importance sampling (a 2D CDF
prebuilt over luminance x sin(theta)) lets NEE hit the small but blazing sun
directly, so the long shadows on the grass and the soft skylight on the candy
marbles all originate from the same image.

Run: python3 scenes/10-suncatcher.py
"""

from scenelib import (Scene, rigid_body, rotate_x, rotate_y, rotate_z,
                      scale, static_body, translate)

s = Scene()
s.render(width=1920, height=1080, spp=256, max_depth=10, clamp=10, seed=7)
s.camera(lookfrom=[-4.8, 2.0, 7.2], lookat=[-0.1, 0.8, -0.1], vfov=39)
s.background_envmap('../assets/kloofendal_48d_partly_cloudy_puresky_4k.hdr', rotate=0, intensity=1.0, importance=True)  # sole light source

# ---- textures & meshes ----
s.texture('lawn', 'grid', a=[0.42, 0.52, 0.26], b=[0.3, 0.4, 0.2], scale=[24, 24], width=0.03)
s.texture('spotSkin', 'image', file='textures/spot_texture.png')
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.lambert('ground', texture='lawn')
s.metal('chrome0', color=[0.94, 0.94, 0.95], roughness=0.0)   # mirror end of the ladder
s.metal('chrome1', color=[0.94, 0.94, 0.95], roughness=0.1)
s.metal('chrome2', color=[0.94, 0.94, 0.95], roughness=0.25)
s.metal('chrome3', color=[0.94, 0.94, 0.95], roughness=0.45)
s.metal('chrome4', color=[0.94, 0.94, 0.95], roughness=0.7)   # rough end of the ladder
s.dielectric('glass', ior=1.5)
s.lambert('plinth', color=[0.82, 0.76, 0.68])
s.lambert('spot', texture='spotSkin')
s.metal('gold', color=[1.0, 0.78, 0.34], roughness=0.15)
s.lambert('coral', color=[0.85, 0.28, 0.22])
s.lambert('amber', color=[0.95, 0.62, 0.15])
s.lambert('mint', color=[0.35, 0.75, 0.5])
s.lambert('skyb', color=[0.3, 0.55, 0.85])

# ---- geometry ----
s.add('rect', 'ground', scale(30))  # grass lawn

# roughness-ladder chrome spheres: three large in a back arc, two small at right
s.add('sphere', 'chrome0', scale(0.8), translate(-2.8, 0.8, -5.5))
s.add('sphere', 'chrome1', scale(0.8), translate(0.0, 0.8, -6.0))
s.add('sphere', 'chrome2', scale(0.8), translate(2.8, 0.8, -5.5))
s.add('sphere', 'chrome3', scale(0.55), translate(3.7, 0.55, -0.8))
s.add('sphere', 'chrome4', scale(0.55), translate(4.9, 0.55, 0.0))

# centerpiece: glass sphere on a stone pedestal
s.add('cylinder', 'plinth', scale(0.5, 0.25, 0.5), translate(0.7, 0.25, 0.9))
s.add('sphere', 'glass', scale(0.75), translate(0.7, 0.95, 0.9))

# two cows: textured Spot and her golden twin
s.add('mesh:spot', 'spot', scale(1.0), rotate_y(140), translate(-3.1, 0.737, 2.5))
s.add('mesh:spot', 'gold', scale(1.12), rotate_y(160), translate(-4.2, 0.826, -1.6))

# candy marbles scattered in the foreground
s.add('sphere', 'coral', scale(0.22), translate(0.0, 0.22, 2.5))
s.add('sphere', 'mint', scale(0.2), translate(0.9, 0.2, 2.9))
s.add('sphere', 'skyb', scale(0.24), translate(1.9, 0.24, 2.2))
s.add('sphere', 'amber', scale(0.18), translate(-1.0, 0.18, 3.0))

if __name__ == "__main__":
    s.run(out="10-suncatcher.png")
