#!/usr/bin/env bash
# sundog report figures — renders the comparison figures from the
# docs/report/OUTLINE.md "渲染图" table, all AVIF (v0.18).
#
# Assumes it runs ON THE TEST BOX with $SUNDOG_BUILD/libsundog.so built
# (scenes render in-process through scenelib/ctypes). Callable from any cwd.
#
# Raw renders land in out/report/ (not committed); the labeled /
# stitched / cropped finals go to docs/report/figures/ as PQ AVIF —
# PIL composites on the 8-bit PQ code values via an img2avif PPM
# bridge, and the result is re-encoded losslessly with the same
# BT.2020/PQ CICP the renderer stamps. Set REUSE=1 to keep existing
# raw renders and only redo the compositing.
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
export IMG2AVIF="$SUNDOG_BUILD/img2avif"
[ -x "$IMG2AVIF" ] || fail "img2avif not built: $IMG2AVIF (make it first)"
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
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

IMG2AVIF = os.environ["IMG2AVIF"]
# Panels are the renderer's 12-bit PQ AVIF output, bridged to PIL as 8-bit
# PQ code values (PPM). Label boxes and the canvas must use PQ reference
# white — 203 nits encodes to ~0.58, i.e. code 148; code 255 would be a
# blinding 10,000-nit patch on an HDR display.
PQ_WHITE = (148, 148, 148)


def load(path):
    tmp = tempfile.NamedTemporaryFile(suffix=".ppm", delete=False)
    tmp.close()
    subprocess.check_call([IMG2AVIF, "decode", path, tmp.name],
                          stdout=subprocess.DEVNULL)
    im = Image.open(tmp.name).convert("RGB")
    im.load()
    os.unlink(tmp.name)
    return im

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


LABEL_BG = PQ_WHITE  # cmd_strip swaps this for sRGB composites


def tag(img, text, size, anchor="tl", cx=None):
    """Label box + black text. anchor: tl=top-left, bc=bottom-center@cx."""
    d = ImageDraw.Draw(img)
    f = font(size)
    pad = max(4, size // 4)
    x0, y0, x1, y1 = d.textbbox((0, 0), text, font=f)
    w, h = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
    if anchor == "tl":
        bx, by = 0, 0
    else:
        bx, by = int(cx - w / 2), img.height - h - max(6, size // 3)
    d.rectangle([bx, by, bx + w, by + h], fill=LABEL_BG)
    d.text((bx + pad - x0, by + pad - y0), text, font=f, fill=(0, 0, 0))


def autosize(img):
    return max(16, min(40, img.height // 16))


def save(img, out, cicp="pq"):
    tmp = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
    tmp.close()
    img.convert("RGB").save(tmp.name, "PNG")
    cmd = [IMG2AVIF, "encode", tmp.name, out]
    if cicp == "pq":
        cmd.append("pq")
    subprocess.check_call(cmd, stdout=subprocess.DEVNULL)
    os.unlink(tmp.name)
    print(f"wrote {out} ({img.width}x{img.height}, {cicp}, "
          f"{os.path.getsize(out) // 1024} KB)")


def pq_to_srgb(im):
    """Convert PQ code values to sRGB code values (linear light matched
    at the 203-nit reference white; highlights above it clip). Used when
    a strip mixes the renderer's PQ beauty with sRGB AOV panels — the
    whole composite is then saved with sRGB CICP."""
    m1, m2 = 2610.0 / 16384, 2523.0 / 4096 * 128
    c1, c2, c3 = 3424.0 / 4096, 2413.0 / 4096 * 32, 2392.0 / 4096 * 32
    lut = []
    for v in range(256):
        e = v / 255.0
        p = pow(max(e, 0.0), 1.0 / m2)
        num = max(p - c1, 0.0)
        lin = pow(num / (c2 - c3 * p), 1.0 / m1) * 10000.0 / 203.0
        lin = min(lin, 1.0)
        s = 12.92 * lin if lin <= 0.0031308 else 1.055 * lin ** (1 / 2.4) - 0.055
        lut.append(round(s * 255))
    return im.point(lut * 3)


def cmd_strip(out, rest):
    gutter, crop, upscale, lsize, cicp = 4, None, 1.0, None, "pq"
    panels = []
    i = 0
    while i < len(rest):
        a = rest[i]
        if a == "--cicp":
            cicp = rest[i + 1]; i += 2
        elif a == "--gutter":
            gutter = int(rest[i + 1]); i += 2
        elif a == "--crop":
            crop = tuple(int(v) for v in rest[i + 1].split(",")); i += 2
        elif a == "--upscale":
            upscale = float(rest[i + 1]); i += 2
        elif a == "--label-size":
            lsize = int(rest[i + 1]); i += 2
        else:
            parts = a.split("|")
            panels.append((parts[0], parts[1] if len(parts) > 1 else "",
                           len(parts) > 2 and parts[2] == "pq2srgb"))
            i += 1
    assert panels, "strip: no panels given"
    global LABEL_BG
    if cicp != "pq":
        LABEL_BG = (255, 255, 255)
    imgs = []
    for path, label, conv in panels:
        im = load(path)
        if conv:
            im = pq_to_srgb(im)
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
    canvas = Image.new("RGB", (w, h),
                       PQ_WHITE if cicp == "pq" else (255, 255, 255))
    x = 0
    for im in imgs:
        canvas.paste(im, (x, 0))
        x += im.width + gutter
    save(canvas, out, cicp)


def cmd_ladder(out, rest):
    src, top, *labels = rest
    im = load(src)
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
  if [ "$REUSE" = 1 ] && [ -s "$RAW/$stem.avif" ]; then
    echo "== $stem (reusing existing render) =="
    return
  fi
  echo "== render $stem =="
  python3 "$scene" --out "$RAW/$stem.avif" --no-denoise --quiet "$@"
  [ -s "$RAW/$stem.avif" ] || fail "empty output: $stem"
}

# reuse gate for the inline scenelib variants below
skip_reuse() {
  [ "$REUSE" = 1 ] && [ -s "$RAW/$1.avif" ] \
    && { echo "== $1 (reusing existing render) =="; return 0; }
  echo "== render $1 =="
  return 1
}

# ------------------------------------------------ ch01-spp-convergence.avif
# 02-cornell-lume at 1/4/16/64/256 spp, 480x270 each, horizontal strip.
for spp in 1 4 16 64 256; do
  render "ch01-spp$spp" "$ROOT/scenes/02-cornell-lume.py" \
         --size 480x270 --spp "$spp"
done
python3 "$COMPOSE" strip "$FIG/ch01-spp-convergence.avif" \
  "$RAW/ch01-spp1.avif|1 spp"   "$RAW/ch01-spp4.avif|4 spp" \
  "$RAW/ch01-spp16.avif|16 spp" "$RAW/ch01-spp64.avif|64 spp" \
  "$RAW/ch01-spp256.avif|256 spp"

# -------------------------------------------------------------- ch04-nee.avif
# 02-cornell-lume with NEE on (default) vs off: temp scene variant with
# "nee": false on every emissive object. Original scene untouched.
render "ch04-nee-on"  "$ROOT/scenes/02-cornell-lume.py" \
       --size 960x540 --spp 64
skip_reuse "ch04-nee-off" || python3 - "$ROOT" "$RAW/ch04-nee-off.avif" <<'PY'
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
  [ -s "$RAW/ch04-nee-off.avif" ] || fail "empty output: ch04-nee-off"
python3 "$COMPOSE" strip "$FIG/ch04-nee.avif" --label-size 26 \
  "$RAW/ch04-nee-on.avif|NEE 开（默认）· 64 spp" \
  "$RAW/ch04-nee-off.avif|NEE 关（发光体 nee:false）· 64 spp"

# ------------------------------------------------------------ ch04-clamp.avif
# 04-parabolica at low spp: --clamp 0 (fireflies) vs --clamp 5.
# NOTE: OUTLINE says "clamp 0 vs 默认", but the scene default (30) is not
# visibly distinct from clamp 0 at 24 spp (measured 59 dB apart back in the
# ACES/8-bit era; the PQ pipeline preserves highlights, so the gap stays
# subtle). clamp 5 remains the smallest deviation that makes the intended
# firefly suppression visible.
render "ch04-clamp-off" "$ROOT/scenes/04-parabolica.py" \
       --size 640x360 --spp 24 --clamp 0
render "ch04-clamp-on" "$ROOT/scenes/04-parabolica.py" \
       --size 640x360 --spp 24 --clamp 5
python3 "$COMPOSE" strip "$FIG/ch04-clamp.avif" --label-size 20 \
  "$RAW/ch04-clamp-off.avif|clamp 0（关闭）· 24 spp" \
  "$RAW/ch04-clamp-on.avif|clamp 5 · 24 spp"

# -------------------------------------------------- ch05-roughness-ladder.avif
# Dedicated scene: five metal spheres, roughness 0/0.1/0.25/0.45/0.7,
# one big rect area light overhead, dark gray floor.
render "ch05-roughness-ladder" \
       "$ROOT/docs/report/figures/src/roughness-ladder.py" --spp 512
python3 "$COMPOSE" ladder "$FIG/ch05-roughness-ladder.avif" \
  "$RAW/ch05-roughness-ladder.avif" "金属球 · 512 spp" \
  "roughness 0" "roughness 0.1" "roughness 0.25" \
  "roughness 0.45" "roughness 0.7"

# ------------------------------------------------------- ch06-primitives.avif
# features.json: sphere / cylinder / parabola / disk / rect in one frame.
render "ch06-primitives" "$ROOT/scenes/features.py" \
       --size 1280x800 --spp 256
python3 "$COMPOSE" strip "$FIG/ch06-primitives.avif" \
  "$RAW/ch06-primitives.avif|"

# -------------------------------------------------------------- ch09-aov.avif
# 03-spot-atrium beauty + albedo/normal guide AOVs, three panels.
if [ "$REUSE" = 1 ] && [ -s "$RAW/ch09-beauty.avif" ] && \
   [ -s "$RAW/ch09-albedo.avif" ] && [ -s "$RAW/ch09-normal.avif" ]; then
  echo "== ch09-beauty/albedo/normal (reusing existing renders) =="
else
  echo "== render ch09-beauty (+ albedo/normal AOVs) =="
  python3 "$ROOT/scenes/03-spot-atrium.py" \
          --out "$RAW/ch09-beauty.avif" --size 800x450 --spp 64 \
          --aov-albedo "$RAW/ch09-albedo.avif" \
          --aov-normal "$RAW/ch09-normal.avif" --no-denoise --quiet
  [ -s "$RAW/ch09-albedo.avif" ] || fail "empty output: ch09-albedo"
fi
# The AOV panels are sRGB (CICP 1/13) while beauty is PQ — unify the strip
# in the sRGB domain (beauty converted via pq2srgb, highlights clip) so the
# composite's CICP matches every panel's actual encoding.
python3 "$COMPOSE" strip "$FIG/ch09-aov.avif" --label-size 24 --cicp srgb \
  "$RAW/ch09-beauty.avif|beauty · 64 spp|pq2srgb" \
  "$RAW/ch09-albedo.avif|albedo AOV" \
  "$RAW/ch09-normal.avif|normal AOV"

# --------------------------------------------------- ch18-freeze-sequence.avif
# 06-spot-cascade frozen at four instants plus the settled state: the same
# initial conditions, different --physics-time. Small panels, modest spp.
for t in 0.3 0.7 1.0 1.4; do
  render "ch18-freeze-$t" "$ROOT/scenes/06-spot-cascade.py" \
         --size 480x270 --spp 24 --physics-time "$t"
done
render "ch18-freeze-settled" "$ROOT/scenes/06-spot-cascade.py" \
       --size 480x270 --spp 24
python3 "$COMPOSE" strip "$FIG/ch18-freeze-sequence.avif" --label-size 20 \
  "$RAW/ch18-freeze-0.3.avif|t = 0.3 s" \
  "$RAW/ch18-freeze-0.7.avif|t = 0.7 s" \
  "$RAW/ch18-freeze-1.0.avif|t = 1.0 s（画廊主图）" \
  "$RAW/ch18-freeze-1.4.avif|t = 1.4 s" \
  "$RAW/ch18-freeze-settled.avif|沉降静止 · 8.75 s"

# --------------------------------------------------- ch12-noise-anatomy.avif
# Flame close-up at noise_scale 0 / 1.5 / 3: smooth teardrop profile -> mild
# warp -> full licks. Temp scene generated inline (same recipe as ch04-nee).
for ns in 0.0 1.5 3.0; do
  skip_reuse "ch12-flame-ns$ns" || python3 - "$ROOT" "$RAW/ch12-flame-ns$ns.avif" "$ns" <<'PY'
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
  [ -s "$RAW/ch12-flame-ns$ns.avif" ] || fail "empty output: ch12-flame-ns$ns"
done
python3 "$COMPOSE" strip "$FIG/ch12-noise-anatomy.avif" --label-size 22 \
  "$RAW/ch12-flame-ns0.0.avif|noise_scale 0（纯轮廓）" \
  "$RAW/ch12-flame-ns1.5.avif|noise_scale 1.5" \
  "$RAW/ch12-flame-ns3.0.avif|noise_scale 3（默认）"

# ---------------------------------------------- ch12-flame-shadow.avif
# 12-molten-oracle with shadow rays blind to flames vs marching them: the
# zero-emission smoke column casts no shadow vs a visible volumetric one
# under the altar fires and the skylight beam.
render "ch12-fshadow-opq" "$ROOT/scenes/12-molten-oracle.py" \
       --size 960x540 --spp 96 --opaque-shadows
render "ch12-fshadow-vol" "$ROOT/scenes/12-molten-oracle.py" \
       --size 960x540 --spp 96
python3 "$COMPOSE" strip "$FIG/ch12-flame-shadow.avif" --label-size 26 \
  "$RAW/ch12-fshadow-opq.avif|旧口径（--opaque-shadows，烟柱不挡光）" \
  "$RAW/ch12-fshadow-vol.avif|体积阴影（默认，烟柱投影）"

# -------------------------------------------------------- ch13-anatomy.avif
# Water close-up (checker lake bed, sun sphere) in three variants: flat
# mirror (wave_amp 0), default waves, no absorption. Temp scenes inline.
for v in flat waves noabsorb; do
  skip_reuse "ch13-water-$v" || python3 - "$ROOT" "$RAW/ch13-water-$v.avif" "$v" <<'PY'
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
  [ -s "$RAW/ch13-water-$v.avif" ] || fail "empty output: ch13-water-$v"
done
python3 "$COMPOSE" strip "$FIG/ch13-anatomy.avif" --label-size 20 \
  "$RAW/ch13-water-flat.avif|wave_amp 0（静水镜面）" \
  "$RAW/ch13-water-waves.avif|默认（波纹 + 波光）" \
  "$RAW/ch13-water-noabsorb.avif|absorb 0（无水色）"

# --------------------------------------- ch14-uniform-vs-importance.avif
# 10-suncatcher with importance:false (uniform sphere NEE) vs default, at
# 16 and 256 spp. The variant lives in /tmp, so relative asset paths are
# rewritten to absolute ones first.
for spp in 16 256; do
  skip_reuse "ch14-uni-$spp" || python3 - "$ROOT" "$RAW/ch14-uni-$spp.avif" "$spp" <<'PY'
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
  [ -s "$RAW/ch14-uni-$spp.avif" ] || fail "empty output: ch14-uni-$spp"
  render "ch14-imp-$spp" "$ROOT/scenes/10-suncatcher.py"  --size 480x270 --spp "$spp"
done
python3 "$COMPOSE" strip "$FIG/ch14-uniform-vs-importance.avif" --label-size 18 \
  "$RAW/ch14-uni-16.avif|均匀采样 · 16 spp" \
  "$RAW/ch14-imp-16.avif|重要性采样 · 16 spp" \
  "$RAW/ch14-uni-256.avif|均匀采样 · 256 spp" \
  "$RAW/ch14-imp-256.avif|重要性采样 · 256 spp"

# ---------------------------------------------- ch15-shadow-compare.avif
# 11-glasswork with legacy binary occlusion vs transparent shadows: the
# tinted marbles cast solid dark blobs vs rose/gold/teal light pools.
render "ch15-shadow-opq" "$ROOT/scenes/11-glasswork.py" \
       --size 960x540 --spp 96 --opaque-shadows
render "ch15-shadow-xpr" "$ROOT/scenes/11-glasswork.py" \
       --size 960x540 --spp 96
python3 "$COMPOSE" strip "$FIG/ch15-shadow-compare.avif" --label-size 26 \
  "$RAW/ch15-shadow-opq.avif|布尔遮挡（--opaque-shadows）" \
  "$RAW/ch15-shadow-xpr.avif|透明阴影（默认）"

# ------------------------------------------------ ch15-snell-window.avif
# Underwater camera looking straight up: the sky compresses into Snell's
# window (half-angle asin(1/1.33) ~ 48.6 deg); outside it, total internal
# reflection mirrors the lake floor. Temp scene in /tmp (absolute asset path).
skip_reuse "ch15-snell" || python3 - "$ROOT" "$RAW/ch15-snell.avif" <<'PY'
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
  [ -s "$RAW/ch15-snell.avif" ] || fail "empty output: ch15-snell"
python3 "$COMPOSE" strip "$FIG/ch15-snell-window.avif" --label-size 22 \
  "$RAW/ch15-snell.avif|水下仰视：斯涅尔窗口内是天空，窗外全内反射映出水底"

# -------------------------------------------------- ch16-frosted-ladder.avif
# Dedicated scene: five glass spheres, dielectric roughness 0/0.05/0.15/
# 0.3/0.6, HDR sky over a checker floor — refraction blurring to frost.
render "ch16-frosted-ladder" \
       "$ROOT/docs/report/figures/src/frosted-ladder.py" --spp 256
python3 "$COMPOSE" ladder "$FIG/ch16-frosted-ladder.avif" \
  "$RAW/ch16-frosted-ladder.avif" "磨砂玻璃 · 256 spp" \
  "roughness 0" "roughness 0.05" "roughness 0.15" \
  "roughness 0.3" "roughness 0.6"

# ----------------------------------------------------- ch17-toy-ladder.avif
# Scene 14 as-is: five plastic Sparkys, coat roughness 0.03 -> 0.6 — the
# tube reflection streak smearing from clear-coat line to matte bloom.
render "ch17-toy-ladder" "$ROOT/scenes/14-toy-factory.py" --spp 384
python3 "$COMPOSE" ladder "$FIG/ch17-toy-ladder.avif" \
  "$RAW/ch17-toy-ladder.avif" "塑料涂层 · 384 spp" \
  "roughness 0.03" "roughness 0.08" "roughness 0.15" \
  "roughness 0.3" "roughness 0.6"

echo "render-report-figures OK ($(ls "$FIG"/*.avif | wc -l) AVIFs in $FIG, incl. gen-report-charts output)"
