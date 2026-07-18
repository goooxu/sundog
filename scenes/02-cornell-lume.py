#!/usr/bin/env python3
"""sundog scene 02-cornell-lume — Cornell box with a warm key lamp and a cool moon orb.

A Cornell-box variant lit by two contrasting sources: a tiny warm-toned
ceiling lamp (small-area rect at high intensity) and a large, faint blue
"moon orb" hovering in the upper-left corner. Four steel plates step down
through the box with graded roughness (0.02 / 0.08 / 0.2 / 0.4), catching
both lights in reflections from mirror-sharp to brushed. The small key
light makes the NEE+MIS convergence behavior obvious at a glance.

Run: python3 scenes/02-cornell-lume.py
"""

from scenelib import Scene, rotate_x, rotate_z, scale, translate

s = Scene()
s.render(width=1920, height=1080, spp=256, max_depth=16, clamp=10, seed=5, exposure=0.25)
s.camera(lookfrom=[0, 1.0, 3.6], lookat=[0, 0.85, 0], vfov=36, aperture=0.0)
s.background_solid(color=[0, 0, 0])

# ---- materials ----
s.lambert('white', color=[0.6, 0.6, 0.6])
s.lambert('red', color=[0.65, 0.06, 0.06])
s.lambert('green', color=[0.12, 0.48, 0.1])
s.metal('steel1', color=[0.9, 0.9, 0.92], roughness=0.02)
s.metal('steel2', color=[0.9, 0.9, 0.92], roughness=0.08)
s.metal('steel3', color=[0.9, 0.9, 0.92], roughness=0.2)
s.metal('steel4', color=[0.9, 0.9, 0.92], roughness=0.4)
s.emissive('keylamp', color=[1.0, 0.87, 0.65], intensity=220.0)  # warm key light
s.emissive('moonorb', color=[0.62, 0.75, 1.0], intensity=0.9)    # cool fill glow

# ---- box walls ----
s.add('rect', 'white', scale(1.6, 1, 1))                                # floor
s.add('rect', 'white', scale(1.6, 1, 1), rotate_x(180), translate(0, 2, 0))   # ceiling
s.add('rect', 'white', scale(1.6, 1, 1), rotate_x(90), translate(0, 1, -1))   # back wall
s.add('rect', 'red', rotate_z(-90), translate(-1.6, 1, 0))              # left wall
s.add('rect', 'green', rotate_z(90), translate(1.6, 1, 0))              # right wall

# ---- lights ----
s.add('rect', 'keylamp', scale(0.09), rotate_x(180), translate(0.55, 1.995, -0.3))  # key light plate
s.add('sphere', 'moonorb', scale(0.38), translate(-0.95, 1.5, -0.5))                # moon orb

# ---- steel plate cascade (roughness 0.02 -> 0.4) ----
s.add('rect', 'steel1', scale(1.3, 1, 0.11), rotate_x(48), translate(0, 0.78, -0.5))
s.add('rect', 'steel2', scale(1.3, 1, 0.11), rotate_x(38), translate(0, 0.58, -0.1))
s.add('rect', 'steel3', scale(1.3, 1, 0.11), rotate_x(30), translate(0, 0.38, 0.3))
s.add('rect', 'steel4', scale(1.3, 1, 0.11), rotate_x(23), translate(0, 0.18, 0.7))

if __name__ == "__main__":
    s.run(out="02-cornell-lume.avif")
