#!/usr/bin/env python3
"""Procedural textures for the cover scene (12-molten-oracle).

Outputs (committed, like scenes/textures/spot_texture.png):
  scenes/textures/gear.png   RGBA 512x512 — gear alpha cutout for `disk`
                             objects. Disk UV is POLAR (u = azimuth, v =
                             radius, intersect.cuh): teeth are a square wave
                             along u at the rim, the hub bore and lightening
                             holes are v-bands. RGB doubles as a gold F0 map.
  scenes/textures/runes.png  RGB 1024x256 — black base + glowing blue
                             sci-fi rune traces for textured emissive rects
                             (black texels emit nothing).

Deterministic (fixed seed). Requires PIL.
"""
import math
import os
import random

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "scenes", "textures")

random.seed(1207)


def gen_gear(path, size=512, teeth=9):
    img = Image.new("RGBA", (size, size))
    px = img.load()
    for y in range(size):
        v = (y + 0.5) / size          # radius 0(center)..1(rim)
        for x in range(size):
            u = (x + 0.5) / size      # azimuth, wraps at u=0/1
            a = 255
            # hub bore
            if v < 0.13:
                a = 0
            # lightening holes: ring of 5 round holes at v ~ 0.42
            for k in range(5):
                cu = (k + 0.5) / 5.0
                du = min(abs(u - cu), 1.0 - abs(u - cu))  # wrap distance
                if (du * 2.2) ** 2 + ((v - 0.42) * 6.0) ** 2 < 0.055:
                    a = 0
            # spokes: keep 5 spokes between v 0.2 and 0.62, cut the rest
            if 0.2 < v < 0.62 and a:
                spoke = False
                for k in range(5):
                    cu = k / 5.0
                    du = min(abs(u - cu), 1.0 - abs(u - cu))
                    if du < 0.045:
                        spoke = True
                inner_ring = v < 0.26
                outer_ring = v > 0.56
                hole_band = abs(v - 0.42) < 0.11
                if not (spoke or inner_ring or outer_ring) and not hole_band:
                    a = 0
            # rim teeth: square wave along u in the outer band
            if v > 0.86:
                phase = (u * teeth) % 1.0
                if phase > 0.5:
                    a = 0
            # gold-copper radial gradient as the F0 color
            g = 0.55 + 0.35 * v
            r, gg, b = int(255 * min(1, g * 1.05)), int(255 * g * 0.72), int(255 * g * 0.30)
            px[x, y] = (r, gg, b, a)
    img.save(path, "PNG")
    print("wrote", path)


def gen_runes(path, w=1024, h=256):
    img = Image.new("RGB", (w, h), (2, 3, 6))
    px = img.load()

    def glow(x, y, c):
        # small soft dot so traces read as emissive tubes
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                xx, yy = (x + dx) % w, y + dy
                if 0 <= yy < h:
                    fall = max(0.0, 1.0 - (dx * dx + dy * dy) / 6.0)
                    r0, g0, b0 = px[xx, yy]
                    px[xx, yy] = (min(255, r0 + int(c[0] * fall)),
                                  min(255, g0 + int(c[1] * fall)),
                                  min(255, b0 + int(c[2] * fall)))

    blue = (40, 130, 255)
    # circuit traces: horizontal runs with right-angle jogs and node dots
    for _ in range(26):
        y = random.randint(12, h - 12)
        x = random.randint(0, w - 1)
        length = random.randint(140, 420)
        step = random.choice([1, -1])
        for i in range(length):
            x = (x + step) % w
            glow(x, y, blue)
            if random.random() < 0.02:  # right-angle jog
                jog = random.choice([-1, 1]) * random.randint(8, 30)
                y2 = min(h - 8, max(8, y + jog))
                lo, hi = sorted((y, y2))
                for yy in range(lo, hi + 1):
                    glow(x, yy, blue)
                y = y2
        # terminal node
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                if dx * dx + dy * dy <= 16:
                    glow((x + dx) % w, min(h - 1, max(0, y + dy)), (70, 170, 255))
    # a few rune glyphs: small squares/diamonds
    for _ in range(18):
        cx, cy = random.randint(10, w - 10), random.randint(16, h - 16)
        r = random.randint(4, 9)
        for t in range(0, 360, 4):
            x = cx + int(r * math.cos(math.radians(t)))
            y = cy + int(r * math.sin(math.radians(t)))
            glow(x % w, min(h - 1, max(0, y)), blue)
    img.save(path, "PNG")
    print("wrote", path)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    gen_gear(os.path.join(OUT, "gear.png"))
    gen_runes(os.path.join(OUT, "runes.png"))
