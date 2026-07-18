#!/usr/bin/env python3
"""Report figure scene (ch05): five metal spheres, roughness 0 -> 0.7, one
rect area light overhead, dark gray floor. Lives with the report assets, so
it bootstraps the scenelib import path itself.

Run: python3 docs/report/figures/src/roughness-ladder.py
"""
import os
import sys

_ROOT = os.path.dirname(os.path.abspath(__file__))
for _ in range(4):
    _ROOT = os.path.dirname(_ROOT)
sys.path.insert(0, os.path.join(_ROOT, "scenes"))

from scenelib import Scene, rotate_x, scale, translate  # noqa: E402

s = Scene()
s.render(width=1500, height=500, spp=512, max_depth=8, seed=11, clamp=10)
s.camera(lookfrom=[0, 1.1, 6.8], lookat=[0, 0.55, 0], vfov=18.8, aperture=0.0)
s.background_solid(color=[0.03, 0.03, 0.035])

s.lambert("floor", color=[0.23, 0.23, 0.24])
s.emissive("panel", color=[1.0, 0.98, 0.92], intensity=6.0)
LADDER = [("metal000", 0.0, -2.7), ("metal010", 0.1, -1.35),
          ("metal025", 0.25, 0.0), ("metal045", 0.45, 1.35),
          ("metal070", 0.7, 2.7)]
for name, rough, _ in LADDER:
    s.metal(name, color=[0.92, 0.92, 0.94], roughness=rough)

s.add("rect", "floor", scale(40))
s.add("rect", "panel", scale(4.8, 1, 1.2), rotate_x(180), translate(0, 3.5, 0))
for name, _, x in LADDER:
    s.add("sphere", name, scale(0.55), translate(x, 0.55, 0))

if __name__ == "__main__":
    s.run(out="roughness-ladder.avif")
