#!/usr/bin/env python3
"""sundog scene 01-marble-run — Morning-light marble run.

A string of colorful marbles frozen along a bouncing arc: dropping in from
the upper right, kissing the ground, hopping through a red and a green arch,
and finally coming to rest in a golden parabolic bowl.  Candy-colored
Lambertian spheres, a roughness ladder of metal balls (mirror silver to
brushed steel), and glass marbles share the grid-lined court — pure quadric
geometry (zero triangles), with all five analytic primitives on stage:
rect, cylinder, parabola, sphere and disk.

Run: python3 scenes/01-marble-run.py
"""

from scenelib import Scene, rotate_y, rotate_z, scale, translate

s = Scene()
s.render(width=1920, height=1080, spp=256, max_depth=12, clamp=10, seed=3)
s.camera(lookfrom=[5.9, 1.7, 6.3], lookat=[1.5, 0.9, -0.7], vfov=41, aperture=0.035)
s.background_gradient(horizon=[1.0, 0.82, 0.58], zenith=[0.35, 0.55, 0.85])

# ---- textures ----
s.texture('court', 'grid', a=[0.8, 0.77, 0.72], b=[0.68, 0.64, 0.58], scale=[22, 22], width=0.03)

# ---- materials ----
s.lambert('floor', texture='court')
s.metal('rail', color=[0.85, 0.86, 0.88], roughness=0.15)
s.metal('bowl', color=[1.0, 0.78, 0.34], roughness=0.12)
s.lambert('arch', color=[0.85, 0.3, 0.24])           # red arch
s.lambert('coral', color=[0.85, 0.28, 0.22])
s.lambert('amber', color=[0.95, 0.62, 0.15])
s.lambert('mint', color=[0.35, 0.75, 0.5])
s.lambert('skyb', color=[0.3, 0.55, 0.85])
s.lambert('lilac', color=[0.6, 0.45, 0.8])
s.dielectric('glass', ior=1.5)
# roughness ladder: mirror silver -> polished gold -> satin copper -> brushed steel
s.metal('silver0', color=[0.95, 0.95, 0.96], roughness=0.0)
s.metal('gold1', color=[1.0, 0.78, 0.34], roughness=0.1)
s.metal('copper2', color=[0.95, 0.54, 0.38], roughness=0.25)
s.metal('steel3', color=[0.75, 0.78, 0.82], roughness=0.45)
s.emissive('sun', color=[1.0, 0.92, 0.78], intensity=42.0)
s.lambert('arch2', color=[0.35, 0.75, 0.5])          # green arch

# ---- geometry: court, arches and catch bowl ----
s.add('rect', 'floor', scale(24))                                                              # grid-lined court floor
s.add('cylinder', 'arch', scale(0.95, 0.3, 0.95), rotate_z(90), rotate_y(12), translate(2.92, 0.42, -1.08))   # red arch (cylinder on its side)
s.add('cylinder', 'arch2', scale(0.62, 0.24, 0.62), rotate_z(90), rotate_y(25), translate(1.33, 0.28, -0.2))  # green arch, smaller
s.add('parabola', 'bowl', scale(1.0), translate(-0.55, 0.1, 0.85), material_back='bowl')       # golden catch bowl (gold on both faces)

# ---- geometry: marbles resting in the bowl ----
s.add('sphere', 'skyb', scale(0.26), translate(-0.75, 0.42, 0.72))
s.add('sphere', 'copper2', scale(0.24), translate(-0.32, 0.4, 0.95))
s.add('sphere', 'lilac', scale(0.22), translate(-0.6, 0.6, 1.1))

# ---- geometry: the bouncing arc, upper right toward the bowl ----
s.add('sphere', 'glass', scale(0.3), translate(4.5, 2.0, -2.0))       # incoming glass marble at the top of the arc
s.add('sphere', 'coral', scale(0.3), translate(3.75, 0.55, -1.55))    # first descent
s.add('sphere', 'mint', scale(0.3), translate(3.42, 0.8, -1.19))      # rebound
s.add('sphere', 'gold1', scale(0.3), translate(2.1, 0.32, -0.62))     # skimming the ground past the red arch
s.add('sphere', 'amber', scale(0.3), translate(0.95, 0.58, -0.03))    # hopping through the green arch
s.add('sphere', 'silver0', scale(0.3), translate(0.15, 0.72, 0.55))   # final hop toward the bowl

# ---- geometry: scattered marbles and floor markers ----
s.add('sphere', 'steel3', scale(0.34), translate(-2.6, 0.34, -2.6))   # stray brushed-steel marble
s.add('sphere', 'glass', scale(0.38), translate(0.4, 0.38, 3.4))      # large glass marble in the foreground
s.add('sphere', 'coral', scale(0.24), translate(4.6, 0.24, -2.4))
s.add('sphere', 'mint', scale(0.2), translate(-1.2, 0.2, -3.0))
s.add('disk', 'coral', scale(0.55), translate(0.0, 0.012, -3.6))      # painted floor marker
s.add('disk', 'skyb', scale(0.48), translate(5.6, 0.01, -0.8))        # painted floor marker
s.add('disk', 'amber', scale(0.42), translate(-3.2, 0.011, 2.2))      # painted floor marker

# ---- lighting ----
s.add('sphere', 'sun', scale(5.0), translate(-28, 34, -22))           # distant emissive sun ball

if __name__ == "__main__":
    s.run(out="01-marble-run.avif")
