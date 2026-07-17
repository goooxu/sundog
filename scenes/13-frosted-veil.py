#!/usr/bin/env python3
"""sundog scene 13-frosted-veil — Frosted screen, fire behind.

A five-panel glass folding screen stands in a night clearing, one small
volumetric flame burning behind each panel at the same offset. Panel
roughness steps 0 -> 0.6 left to right: the mirror-clear end shows a
crisp flame (delta glass, byte-identical to the old path), the frosted
end only a warm bloom -- GGX microfacet transmission (VNDF-sampled
Walter BTDF) smearing "what you see" into "only the light". A Spot cow
stands silhouetted behind the clearest panel. The firelight pools on
the floor in front of the screen stay equally sharp across the ladder:
shadow rays still cross the glass with the smooth-Fresnel straight-line
approximation (report ch16).

Run: python3 scenes/13-frosted-veil.py
"""

from scenelib import Scene, rotate_x, rotate_y, rotate_z, scale, translate

s = Scene()
s.render(width=1920, height=1080, spp=512, max_depth=12, clamp=8, seed=7,
         exposure=0.6)
s.camera(lookfrom=[0.0, 1.55, 5.8], lookat=[0.0, 0.95, -0.3], vfov=36)
s.background_gradient(horizon=[0.02, 0.03, 0.06], zenith=[0.004, 0.006, 0.014])

# ---- textures & meshes ----
s.texture('spotSkin', 'image', file='textures/spot_texture.png')
s.mesh('spot', '../assets/spot.obj', normals='smooth')

# ---- materials ----
s.lambert('ground', color=[0.15, 0.15, 0.17])
s.lambert('frame', color=[0.16, 0.11, 0.07])  # dark lacquered wood
s.lambert('spot_skin', texture='spotSkin')
# frosted ladder, mirror-clear -> fully frosted: (name, roughness, x)
LADDER = [("frost000", 0.0, -2.4), ("frost008", 0.08, -1.2),
          ("frost018", 0.18, 0.0), ("frost035", 0.35, 1.2),
          ("frost060", 0.60, 2.4)]
for name, rough, _ in LADDER:
    s.dielectric(name, ior=1.5, roughness=rough)

# ---- lights: one flame per panel, same geometry, seed varies ----
s.distant_light(direction=[0.4, -1.0, -0.35],
                radiance=[0.008, 0.011, 0.02])  # faint moon rim
for i, (_, _, x) in enumerate(LADDER):
    s.flame(base=[x, 0.06, -1.15], height=1.05, radius=0.28, intensity=16,
            sigma=5.0, noise_scale=3.0, seed=11 + i, light_intensity=15)

# ---- geometry ----
s.add('disk', 'ground', scale(15))

# glass panels: a two-rect slab per panel (1.12 wide x 1.7 tall, 0.05 thick)
# so the medium stack pushes on the front face and pops on the back
for name, _, x in LADDER:
    s.add('rect', name, scale(0.56, 1, 0.85), rotate_x(90),
          translate(x, 0.9, 0.025))
    s.add('rect', name, scale(0.56, 1, 0.85), rotate_x(-90),
          translate(x, 0.9, -0.025))

# screen frame: posts between/around panels + top rail
for x in (-3.0, -1.8, -0.6, 0.6, 1.8, 3.0):
    s.add('cylinder', 'frame', scale(0.05, 0.875, 0.05),
          translate(x, 0.875, 0))
s.add('cylinder', 'frame', scale(0.04, 3.1, 0.04), rotate_z(90),
      translate(0, 1.79, 0))

# cow silhouetted behind the clearest panel, backlit by its flame
s.add('mesh:spot', 'spot_skin', scale(0.8), rotate_y(90),
      translate(-2.62, 0.56, -0.55))

if __name__ == "__main__":
    s.run(out="13-frosted-veil.png")
