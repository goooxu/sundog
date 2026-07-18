#!/usr/bin/env python3
"""Report figure scene (ch17): five glass spheres, dielectric roughness
0 -> 0.6, under the HDR sky on a checker floor — refraction blurring from
mirror-sharp to full frost. Lives with the report assets, so it bootstraps
the scenelib import path itself.

Run: python3 docs/report/figures/src/frosted-ladder.py
"""
import os
import sys

_ROOT = os.path.dirname(os.path.abspath(__file__))
for _ in range(4):
    _ROOT = os.path.dirname(_ROOT)
sys.path.insert(0, os.path.join(_ROOT, "scenes"))

from scenelib import Scene, scale, translate  # noqa: E402

s = Scene()
s.render(width=1500, height=500, spp=256, max_depth=12, seed=11, clamp=10)
s.camera(lookfrom=[0, 1.1, 6.8], lookat=[0, 0.55, 0], vfov=18.8)
s.background_envmap(os.path.join(_ROOT, "assets",
                                 "kloofendal_48d_partly_cloudy_puresky_4k.hdr"),
                    rotate=180, intensity=1.0, importance=True)

s.texture("floor", "checker", a=[0.7, 0.7, 0.72], b=[0.2, 0.2, 0.24],
          scale=[24, 24])
s.lambert("ground", texture="floor")
s.add("rect", "ground", scale(30))

LADDER = [("r000", 0.0, -2.7), ("r005", 0.05, -1.35), ("r015", 0.15, 0.0),
          ("r030", 0.30, 1.35), ("r060", 0.60, 2.7)]
for name, rough, _ in LADDER:
    s.dielectric(name, ior=1.5, roughness=rough)
for name, _, x in LADDER:
    s.add("sphere", name, scale(0.55), translate(x, 0.55, 0))

if __name__ == "__main__":
    s.run(out="frosted-ladder.avif")
