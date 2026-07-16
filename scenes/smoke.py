#!/usr/bin/env python3
"""sundog scene smoke — Minimal smoke-test scene.

The smallest end-to-end sanity check for the renderer: a single matte red
sphere resting on a checkered ground plane under a white-to-blue gradient
sky, lit by one spherical point light for soft shadows. It touches exactly
one instance of each core pipeline stage (gradient background, checker
texture, lambert materials, point light, rect + sphere geometry) at a tiny
256x256 / 16 spp budget, so it renders in seconds. Used by
scripts/run-smoke.sh for GPU sanity runs and asserted field-by-field in
tests/host/test_scene_json.cpp.

Run: python3 scenes/smoke.py
"""

from scenelib import Scene, scale, translate

s = Scene()
s.render(width=256, height=256, spp=16, max_depth=8, seed=7)
s.camera(lookfrom=[0, 1.5, 5], lookat=[0, 0.7, 0], vfov=35)
s.background_gradient(horizon=[1.0, 1.0, 1.0], zenith=[0.4, 0.6, 1.0])

# ---- materials ----
s.texture('floor', 'checker', a=[0.85, 0.85, 0.85], b=[0.15, 0.15, 0.15], scale=[8, 8])

s.lambert('ground', texture='floor')            # checkered floor
s.lambert('ball', color=[0.7, 0.3, 0.3])        # matte red sphere

# ---- lights ----
s.point_light(position=[3, 4, 2], intensity=[40, 40, 40], radius=0.4)  # soft key light

# ---- geometry ----
s.add('rect', 'ground', scale(4))                          # ground plane
s.add('sphere', 'ball', scale(0.7), translate(0, 0.7, 0))  # sphere resting on floor

if __name__ == "__main__":
    s.run(out="smoke.png")
