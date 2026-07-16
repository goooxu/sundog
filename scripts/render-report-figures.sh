#!/usr/bin/env bash
# sundog report figures — renders the 15 comparison PNGs from the
# docs/report/OUTLINE.md "渲染图" table.
#
# Assumes it runs ON THE TEST BOX with $SUNDOG_BUILD/libsundog.so built
# (scenes render in-process through scenelib/ctypes). Callable from any cwd.
#
# Raw renders land in out/report/ (not committed); the labeled /
# stitched / cropped finals are losslessly recompressed via PIL into
# docs/report/figures/. Set REUSE=1 to keep existing raw renders and
# only redo the compositing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
RAW="$ROOT/out/report"
FIG="$ROOT/docs/report/figures"
REUSE="${REUSE:-0}"

fail() { echo "render-report-figures: FAIL: $*" >&2; exit 1; }
[ -f "$SUNDOG_BUILD/libsundog.so" ] || fail "backend not found: $SUNDOG_BUILD/libsundog.so (set SUNDOG_BUILD)"
python3 -c 'import PIL' 2>/dev/null || \
  pip3 install --user --break-system-packages pillow || fail "pillow unavailable"
mkdir -p "$RAW" "$FIG"

# ---------------------------------------------------------------- compose.py
COMPOSE="$(mktemp /tmp/report-compose-XXXXXX.py)"
trap 'rm -f "$COMPOSE"' EXIT
cat > "$COMPOSE" <<'PY'
"""Stitch / crop / label report figure panels with PIL.

usage:
  compose.py strip  OUT [--gutter N] [--crop X0,Y0,X1,Y1] [--upscale F]
                    [--label-size N] "IMG|LABEL" ["IMG|LABEL" ...]
      Horizontal strip; per-panel white-background black-text tag in the
      top-left corner (empty label = none). --crop/--upscale apply to
      every panel before tagging.
  compose.py ladder OUT IMG "TOPLABEL" LABEL1 [LABEL2 ...]
      Single image; optional top-left tag plus N bottom-centered tags,
      one per horizontal N-th (for evenly spaced objects in one render).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

CJK_FONTS = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
]


def font(size):
    for p in CJK_FONTS:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except OSError:
                pass
    return ImageFont.load_default()  # ASCII-only fallback


def tag(img, text, size, anchor="tl", cx=None):
    """White box + black text. anchor: tl=top-left, bc=bottom-center@cx."""
    d = ImageDraw.Draw(img)
    f = font(size)
    pad = max(4, size // 4)
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=f)
    w, h = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
    if anchor == "tl":
        bx, by = 0, 0
    else:
        bx, by = int(cx - w / 2), img.height - h - max(6, size // 3)
    d.rectangle([bx, by, bx + w, by + h], fill=(255, 255, 255))
    d.text((bx + pad - x0, by + pad - y0), text, font=f, fill=(0, 0, 0))


def autosize(img):
    return max(16, min(40, img.height // 16))


def save(img, out):
    img.convert("RGB").save(out, "PNG", optimize=True)
    print(f"wrote {out} ({img.width}x{img.height}, "
          f"{os.path.getsize(out) // 1024} KB)")


def cmd_strip(out, rest):
    gutter, crop, upscale, lsize = 4, None, 1.0, None
    panels = []
    i = 0
    while i < len(rest):
        a = rest[i]
        if a == "--gutter":
            gutter = int(rest[i + 1]); i += 2
        elif a == "--crop":
            crop = tuple(int(v) for v in rest[i + 1].split(",")); i += 2
        elif a == "--upscale":
            upscale = float(rest[i + 1]); i += 2
        elif a == "--label-size":
            lsize = int(rest[i + 1]); i += 2
        else:
            path, _, label = a.partition("|")
            panels.append((path, label)); i += 1
    assert panels, "strip: no panels given"
    imgs = []
    for path, label in panels:
        im = Image.open(path).convert("RGB")
        if crop:
            im = im.crop(crop)
        if upscale != 1.0:
            im = im.resize((round(im.width * upscale),
                            round(im.height * upscale)), Image.LANCZOS)
        if label:
            tag(im, label, lsize or autosize(im))
        imgs.append(im)
    w = sum(im.width for im in imgs) + gutter * (len(imgs) - 1)
    h = max(im.height for im in imgs)
    canvas = Image.new("RGB", (w, h), (255, 255, 255))
    x = 0
    for im in imgs:
        canvas.paste(im, (x, 0))
        x += im.width + gutter
    save(canvas, out)


def cmd_ladder(out, rest):
    src, top, *labels = rest
    im = Image.open(src).convert("RGB")
    size = autosize(im)
    if top:
        tag(im, top, size)
    for k, txt in enumerate(labels):
        tag(im, txt, size, anchor="bc", cx=(k + 0.5) * im.width / len(labels))
    save(im, out)


cmd, out, *rest = sys.argv[1:]
{"strip": cmd_strip, "ladder": cmd_ladder}[cmd](out, rest)
PY

render() { # render RAW_STEM SCENE EXTRA_ARGS...
  local stem="$1" scene="$2"; shift 2
  if [ "$REUSE" = 1 ] && [ -s "$RAW/$stem.png" ]; then
    echo "== $stem (reusing existing render) =="
    return
  fi
  echo "== render $stem =="
  python3 "$scene" --out "$RAW/$stem.png" --no-denoise --quiet "$@"
  [ -s "$RAW/$stem.png" ] || fail "empty output: $stem"
}

# reuse gate for the inline scenelib variants below
skip_reuse() {
  [ "$REUSE" = 1 ] && [ -s "$RAW/$1.png" ] \
    && { echo "== $1 (reusing existing render) =="; return 0; }
  echo "== render $1 =="
  return 1
}

# ------------------------------------------------ ch01-spp-convergence.png
# 02-cornell-lume at 1/4/16/64/256 spp, 480x270 each, horizontal strip.
for spp in 1 4 16 64 256; do
  render "ch01-spp$spp" "$ROOT/scenes/02-cornell-lume.py" \
         --size 480x270 --spp "$spp"
done
python3 "$COMPOSE" strip "$FIG/ch01-spp-convergence.png" \
  "$RAW/ch01-spp1.png|1 spp"   "$RAW/ch01-spp4.png|4 spp" \
  "$RAW/ch01-spp16.png|16 spp" "$RAW/ch01-spp64.png|64 spp" \
  "$RAW/ch01-spp256.png|256 spp"

# ------------------------------------------------------------ ch01-gamma.png
# smoke.json with gamma 1.0 vs 2.2 (default), side by side. Rendered with
# --tonemap clamp so gamma is the ONLY nonlinearity being compared (the
# default ACES curve would contaminate the "linear" panel).
render "ch01-gamma10" "$ROOT/scenes/smoke.py" \
       --size 512x512 --spp 64 --gamma 1.0 --tonemap clamp
render "ch01-gamma22" "$ROOT/scenes/smoke.py" \
       --size 512x512 --spp 64 --gamma 2.2 --tonemap clamp
python3 "$COMPOSE" strip "$FIG/ch01-gamma.png" --label-size 26 \
  "$RAW/ch01-gamma10.png|gamma 1.0" \
  "$RAW/ch01-gamma22.png|gamma 2.2（默认）"

# ---------------------------------------------------------- ch01-tonemap.png
# 07-campfire: clamp (highlights clip to white) vs ACES (filmic shoulder
# keeps the fire core's orange gradient). The scene's strongest saturated
# highlight in the gallery.
render "ch01-tonemap-clamp" "$ROOT/scenes/07-campfire.py" \
       --size 960x540 --spp 64 --tonemap clamp
render "ch01-tonemap-aces" "$ROOT/scenes/07-campfire.py" \
       --size 960x540 --spp 64
python3 "$COMPOSE" strip "$FIG/ch01-tonemap.png" --label-size 26 \
  "$RAW/ch01-tonemap-clamp.png|截断（tonemap:\"clamp\"）" \
  "$RAW/ch01-tonemap-aces.png|ACES（默认）"

# -------------------------------------------------------------- ch04-nee.png
# 02-cornell-lume with NEE on (default) vs off: temp scene variant with
# "nee": false on every emissive object. Original scene untouched.
render "ch04-nee-on"  "$ROOT/scenes/02-cornell-lume.py" \
       --size 960x540 --spp 64
skip_reuse "ch04-nee-off" || python3 - "$ROOT" "$RAW/ch04-nee-off.png" <<'PY'
import os, runpy, sys
root, out = sys.argv[1:]
scenes = os.path.join(root, "scenes")
sys.path.insert(0, scenes)
g = runpy.run_path(os.path.join(scenes, "02-cornell-lume.py"))
s = g["s"]
doc = s.doc  # live view: mutating it mutates the scene
emissive = {k for k, m in doc["materials"].items() if m.get("type") == "emissive"}
n = 0
for o in doc["objects"]:
    if o.get("material") in emissive:
        o["nee"] = False
        n += 1
assert n, "no emissive objects found"
s.run(out=out, argv=["--size", "960x540", "--spp", "64",
                     "--no-denoise", "--quiet"], base_dir=scenes)
PY
python3 "$COMPOSE" strip "$FIG/ch04-nee.png" --label-size 26 \
  "$RAW/ch04-nee-on.png|NEE 开（默认）· 64 spp" \
  "$RAW/ch04-nee-off.png|NEE 关（发光体 nee:false）· 64 spp"

# ------------------------------------------------------------ ch04-clamp.png
# 04-parabolica at low spp: --clamp 0 (fireflies) vs --clamp 5.
# NOTE: OUTLINE says "clamp 0 vs 默认", but the scene default (30) is not
# visibly different from clamp 0 at 24 spp: measured 59 dB PSNR under ACES —
# the shoulder flattens whatever survives above 1.0 (pre-ACES the two were
# bit-identical: 30/24 > 1 clipped straight to white). clamp 5 remains the
# smallest deviation that makes the intended firefly suppression visible.
render "ch04-clamp-off" "$ROOT/scenes/04-parabolica.py" \
       --size 640x360 --spp 24 --clamp 0
render "ch04-clamp-on" "$ROOT/scenes/04-parabolica.py" \
       --size 640x360 --spp 24 --clamp 5
python3 "$COMPOSE" strip "$FIG/ch04-clamp.png" --label-size 20 \
  "$RAW/ch04-clamp-off.png|clamp 0（关闭）· 24 spp" \
  "$RAW/ch04-clamp-on.png|clamp 5 · 24 spp"

# -------------------------------------------------- ch05-roughness-ladder.png
# Dedicated scene: five metal spheres, roughness 0/0.1/0.25/0.45/0.7,
# one big rect area light overhead, dark gray floor.
render "ch05-roughness-ladder" \
       "$ROOT/docs/report/figures/src/roughness-ladder.py" --spp 512
python3 "$COMPOSE" ladder "$FIG/ch05-roughness-ladder.png" \
  "$RAW/ch05-roughness-ladder.png" "金属球 · 512 spp" \
  "roughness 0" "roughness 0.1" "roughness 0.25" \
  "roughness 0.45" "roughness 0.7"

# ------------------------------------------------------- ch06-primitives.png
# features.json: sphere / cylinder / parabola / disk / rect in one frame.
render "ch06-primitives" "$ROOT/scenes/features.py" \
       --size 1280x800 --spp 256
python3 "$COMPOSE" strip "$FIG/ch06-primitives.png" \
  "$RAW/ch06-primitives.png|"

# -------------------------------------------------------------- ch09-aov.png
# 03-spot-atrium beauty + albedo/normal guide AOVs, three panels.
if [ "$REUSE" = 1 ] && [ -s "$RAW/ch09-beauty.png" ] && \
   [ -s "$RAW/ch09-albedo.png" ] && [ -s "$RAW/ch09-normal.png" ]; then
  echo "== ch09-beauty/albedo/normal (reusing existing renders) =="
else
  echo "== render ch09-beauty (+ albedo/normal AOVs) =="
  python3 "$ROOT/scenes/03-spot-atrium.py" \
          --out "$RAW/ch09-beauty.png" --size 800x450 --spp 64 \
          --aov-albedo "$RAW/ch09-albedo.png" \
          --aov-normal "$RAW/ch09-normal.png" --no-denoise --quiet
  [ -s "$RAW/ch09-albedo.png" ] || fail "empty output: ch09-albedo"
fi
python3 "$COMPOSE" strip "$FIG/ch09-aov.png" --label-size 24 \
  "$RAW/ch09-beauty.png|beauty · 64 spp" \
  "$RAW/ch09-albedo.png|albedo AOV" \
  "$RAW/ch09-normal.png|normal AOV"

# --------------------------------------------------- ch12-freeze-sequence.png
# 06-spot-cascade frozen at four instants plus the settled state: the same
# initial conditions, different --physics-time. Small panels, modest spp.
for t in 0.3 0.7 1.0 1.4; do
  render "ch12-freeze-$t" "$ROOT/scenes/06-spot-cascade.py" \
         --size 480x270 --spp 24 --physics-time "$t"
done
render "ch12-freeze-settled" "$ROOT/scenes/06-spot-cascade.py" \
       --size 480x270 --spp 24
python3 "$COMPOSE" strip "$FIG/ch12-freeze-sequence.png" --label-size 20 \
  "$RAW/ch12-freeze-0.3.png|t = 0.3 s" \
  "$RAW/ch12-freeze-0.7.png|t = 0.7 s" \
  "$RAW/ch12-freeze-1.0.png|t = 1.0 s（画廊主图）" \
  "$RAW/ch12-freeze-1.4.png|t = 1.4 s" \
  "$RAW/ch12-freeze-settled.png|沉降静止 · 8.75 s"

# --------------------------------------------------- ch13-noise-anatomy.png
# Flame close-up at noise_scale 0 / 1.5 / 3: smooth teardrop profile -> mild
# warp -> full licks. Temp scene generated inline (same recipe as ch04-nee).
for ns in 0.0 1.5 3.0; do
  skip_reuse "ch13-flame-ns$ns" || python3 - "$ROOT" "$RAW/ch13-flame-ns$ns.png" "$ns" <<'PY'
import os, sys
root, out, ns = sys.argv[1], sys.argv[2], float(sys.argv[3])
sys.path.insert(0, os.path.join(root, "scenes"))
from scenelib import Scene, scale
s = Scene()
s.render(width=480, height=640, spp=48, max_depth=4, seed=7, clamp=0)
s.camera(lookfrom=[0, 0.9, 3.2], lookat=[0, 0.85, 0], vfov=36)
s.background_solid(color=[0.01, 0.01, 0.015])
s.lambert("ground", color=[0.25, 0.22, 0.2])
s.add("rect", "ground", scale(6))
s.flame(base=[0, 0.05, 0], height=1.6, radius=0.45, intensity=20, sigma=4,
        noise_scale=ns, seed=1, light_intensity=12)
s.run(out=out, argv=["--no-denoise", "--quiet"], base_dir=".")
PY
done
python3 "$COMPOSE" strip "$FIG/ch13-noise-anatomy.png" --label-size 22 \
  "$RAW/ch13-flame-ns0.0.png|noise_scale 0（纯轮廓）" \
  "$RAW/ch13-flame-ns1.5.png|noise_scale 1.5" \
  "$RAW/ch13-flame-ns3.0.png|noise_scale 3（默认）"

# ---------------------------------------------- ch13-flame-shadow.png
# 12-molten-oracle with shadow rays blind to flames vs marching them: the
# zero-emission smoke column casts no shadow vs a visible volumetric one
# under the altar fires and the skylight beam.
render "ch13-fshadow-opq" "$ROOT/scenes/12-molten-oracle.py" \
       --size 960x540 --spp 96 --opaque-shadows
render "ch13-fshadow-vol" "$ROOT/scenes/12-molten-oracle.py" \
       --size 960x540 --spp 96
python3 "$COMPOSE" strip "$FIG/ch13-flame-shadow.png" --label-size 26 \
  "$RAW/ch13-fshadow-opq.png|旧口径（--opaque-shadows，烟柱不挡光）" \
  "$RAW/ch13-fshadow-vol.png|体积阴影（默认，烟柱投影）"

# -------------------------------------------------------- ch14-anatomy.png
# Water close-up (checker lake bed, sun sphere) in three variants: flat
# mirror (wave_amp 0), default waves, no absorption. Temp scenes inline.
for v in flat waves noabsorb; do
  skip_reuse "ch14-water-$v" || python3 - "$ROOT" "$RAW/ch14-water-$v.png" "$v" <<'PY'
import os, sys
root, out, variant = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, os.path.join(root, "scenes"))
from scenelib import Scene, rotate_x, scale, translate
s = Scene()
s.render(width=640, height=360, spp=64, max_depth=10, seed=7, clamp=10)
s.camera(lookfrom=[0, 1.1, 9], lookat=[0, 0.1, 0], vfov=40)
s.background_gradient(horizon=[0.9, 0.45, 0.18], zenith=[0.05, 0.10, 0.28])
s.texture("bed", "checker", a=[0.65, 0.6, 0.5], b=[0.35, 0.32, 0.28],
          scale=[24, 24])
s.lambert("bedmat", texture="bed")
s.lambert("red", color=[0.7, 0.15, 0.1])
s.emissive("sun", color=[1.0, 0.55, 0.25], intensity=60)
WATER = {"flat":     dict(absorb=[0.7, 0.14, 0.06], wave_amp=0.0),
         "waves":    dict(absorb=[0.7, 0.14, 0.06], wave_amp=0.09, wave_freq=1.2),
         "noabsorb": dict(absorb=[0, 0, 0], wave_amp=0.09, wave_freq=1.2)}
s.water("water", **WATER[variant])
s.add("rect", "water", scale(22))
s.add("rect", "bedmat", scale(22), rotate_x(-6), translate(0, -1.2, 0))
s.add("sphere", "red", scale(0.7), translate(2.4, 0.45, 0.5))
s.add("sphere", "sun", scale(9), translate(-30, 6, -200))
s.distant_light(direction=[0.3, -1.0, 0.5], radiance=[0.25, 0.18, 0.12])
s.run(out=out, argv=["--no-denoise", "--quiet"], base_dir=".")
PY
done
python3 "$COMPOSE" strip "$FIG/ch14-anatomy.png" --label-size 20 \
  "$RAW/ch14-water-flat.png|wave_amp 0（静水镜面）" \
  "$RAW/ch14-water-waves.png|默认（波纹 + 波光）" \
  "$RAW/ch14-water-noabsorb.png|absorb 0（无水色）"

# --------------------------------------- ch15-uniform-vs-importance.png
# 10-suncatcher with importance:false (uniform sphere NEE) vs default, at
# 16 and 256 spp. The variant lives in /tmp, so relative asset paths are
# rewritten to absolute ones first.
for spp in 16 256; do
  skip_reuse "ch15-uni-$spp" || python3 - "$ROOT" "$RAW/ch15-uni-$spp.png" "$spp" <<'PY'
import os, runpy, sys
root, out, spp = sys.argv[1], sys.argv[2], sys.argv[3]
scenes = os.path.join(root, "scenes")
sys.path.insert(0, scenes)
g = runpy.run_path(os.path.join(scenes, "10-suncatcher.py"))
s = g["s"]
s.doc["background"]["importance"] = False   # uniform-sphere NEE variant
s.run(out=out, argv=["--size", "480x270", "--spp", spp,
                     "--no-denoise", "--quiet"], base_dir=scenes)
PY
  render "ch15-imp-$spp" "$ROOT/scenes/10-suncatcher.py"  --size 480x270 --spp "$spp"
done
python3 "$COMPOSE" strip "$FIG/ch15-uniform-vs-importance.png" --label-size 18 \
  "$RAW/ch15-uni-16.png|均匀采样 · 16 spp" \
  "$RAW/ch15-imp-16.png|重要性采样 · 16 spp" \
  "$RAW/ch15-uni-256.png|均匀采样 · 256 spp" \
  "$RAW/ch15-imp-256.png|重要性采样 · 256 spp"

# ---------------------------------------------- ch16-shadow-compare.png
# 11-glasswork with legacy binary occlusion vs transparent shadows: the
# tinted marbles cast solid dark blobs vs rose/gold/teal light pools.
render "ch16-shadow-opq" "$ROOT/scenes/11-glasswork.py" \
       --size 960x540 --spp 96 --opaque-shadows
render "ch16-shadow-xpr" "$ROOT/scenes/11-glasswork.py" \
       --size 960x540 --spp 96
python3 "$COMPOSE" strip "$FIG/ch16-shadow-compare.png" --label-size 26 \
  "$RAW/ch16-shadow-opq.png|布尔遮挡（--opaque-shadows）" \
  "$RAW/ch16-shadow-xpr.png|透明阴影（默认）"

# ------------------------------------------------ ch16-snell-window.png
# Underwater camera looking straight up: the sky compresses into Snell's
# window (half-angle asin(1/1.33) ~ 48.6 deg); outside it, total internal
# reflection mirrors the lake floor. Temp scene in /tmp (absolute asset path).
skip_reuse "ch16-snell" || python3 - "$ROOT" "$RAW/ch16-snell.png" <<'PY'
import os, sys
root, out = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(root, "scenes"))
from scenelib import Scene, scale, translate
hdr = os.path.join(root, "assets", "kloofendal_48d_partly_cloudy_puresky_4k.hdr")
s = Scene()
s.render(width=960, height=540, spp=128, max_depth=12, seed=7, clamp=10)
# camera 2 units below a calm water surface, looking straight up
s.camera(lookfrom=[0, -2.0, 0], lookat=[0, 0, 0.001], up=[0, 0, 1], vfov=85)
s.background_envmap(hdr, rotate=180)
s.texture("bed", "checker", a=[0.5, 0.42, 0.3], b=[0.28, 0.24, 0.18],
          scale=[16, 16])
s.water("water", wave_amp=0.0, absorb=[0.2, 0.05, 0.02])
s.lambert("floor", texture="bed")
s.lambert("coral", color=[0.85, 0.35, 0.25])
s.add("rect", "water", scale(40))
s.add("rect", "floor", scale(40), translate(0, -4, 0))
s.add("sphere", "coral", scale(0.6), translate(1.5, -3.4, 1.2))
s.run(out=out, argv=["--size", "960x540", "--spp", "128",
                     "--no-denoise", "--quiet"], base_dir=".")
PY
python3 "$COMPOSE" strip "$FIG/ch16-snell-window.png" --label-size 22 \
  "$RAW/ch16-snell.png|水下仰视：斯涅尔窗口内是天空，窗外全内反射映出水底"

echo "render-report-figures OK ($(ls "$FIG"/*.png | wc -l) PNGs in $FIG)"
