#!/usr/bin/env bash
# sundog gallery render — full-quality 1920x1080 renders of the showcase
# scenes into out/gallery/, then (re)generates docs/GALLERY.md.
#
# Assumes it runs ON THE TEST BOX with $SUNDOG_BUILD/sundog built
# (default /tmp/sundog-build/sundog). Callable from any cwd.
#
# Per-scene spp: 01/02/04 -> 512, 03 -> 256 (plus a 32 spp denoised vs 32 spp
# raw comparison pair), 05 -> 128. Every render writes a .stats.json next to
# the PNG. Scenes that do not exist yet are skipped with a warning.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
SUNDOG="$SUNDOG_BUILD/sundog"
GALLERY="$ROOT/out/gallery"
SIZE="${GALLERY_SIZE:-1920x1080}"

# scene:spp — main gallery list
ENTRIES=(
  "01-prism-court:512"
  "02-cornell-lume:512"
  "03-bunny-atrium:256"
  "04-parabolica:512"
  "05-bunny-swarm:128"
)

fail() { echo "render-gallery: FAIL: $*" >&2; exit 1; }
[ -x "$SUNDOG" ] || fail "binary not found: $SUNDOG"
mkdir -p "$GALLERY"

RENDERED=()  # image stems, in display order

render() { # render STEM SCENE SPP EXTRA_ARGS...
  local stem="$1" scene="$2" spp="$3"; shift 3
  echo "== $stem ($SIZE, $spp spp) =="
  "$SUNDOG" --scene "$scene" --out "$GALLERY/$stem.png" --size "$SIZE" \
            --spp "$spp" --stats "$GALLERY/$stem.stats.json" --quiet "$@"
  [ -s "$GALLERY/$stem.png" ] || fail "empty output for $stem"
  RENDERED+=("$stem")
}

for entry in "${ENTRIES[@]}"; do
  name="${entry%%:*}"; spp="${entry##*:}"
  scene="$ROOT/scenes/$name.json"
  if [ ! -f "$scene" ]; then
    echo "render-gallery: WARNING: scene $name.json not found, skipping" >&2
    continue
  fi
  render "$name" "$scene" "$spp" --no-denoise
  if [ "$name" = "03-bunny-atrium" ]; then
    # low-spp denoiser comparison pair
    render "03-bunny-atrium-spp32-denoised" "$scene" 32 --denoise
    render "03-bunny-atrium-spp32-raw"      "$scene" 32 --no-denoise
  fi
done

[ "${#RENDERED[@]}" -gt 0 ] || fail "no scenes rendered (scenes/ empty?)"

echo "== generating docs/GALLERY.md =="
python3 - "$GALLERY" "$ROOT/docs/GALLERY.md" "${RENDERED[@]}" <<'PY'
import json, sys, datetime, os

gallery, out_md, *stems = sys.argv[1:]

DESC = {
    "01-prism-court":
        "黄昏渐变天空下的棱镜庭院：玻璃立方、抛光镜面与四档粗糙度的金属球，"
        "考验折射、多次镜面反弹与 GGX 高光。",
    "02-cornell-lume":
        "Cornell 盒变体：暖色小面积主灯加冷色低强度月光球，四档粗糙度钢球，"
        "NEE+MIS 在小光源下的收敛能力一目了然。",
    "03-bunny-atrium":
        "网格地板中庭里的三只 Stanford Bunny（陶土 / 金 / 玻璃，各 14.4 万三角形），"
        "硬件三角形求交加平滑法线。",
    "03-bunny-atrium-spp32-denoised":
        "同一场景仅 32 spp + OptiX AI 降噪（albedo/normal 引导）——低采样即可得到干净画面。",
    "03-bunny-atrium-spp32-raw":
        "对照组：同样 32 spp、不降噪的原始蒙特卡洛噪点。",
    "04-parabolica":
        "夜景抛物面聚光：金色抛物碟（背面材质成像）把发光灯珠的光束打向标牌，"
        "展示 parabola 自定义求交与双面材质语义。",
    "05-bunny-swarm":
        "4096 个实例化 bunny 的阵列——同一份三角形 GAS 通过 IAS 实例复用，"
        "展示单层实例化的规模能力。",
}

lines = [
    "# sundog 画廊",
    "",
    f"由 `scripts/render-gallery.sh` 生成于 {datetime.date.today().isoformat()}。",
    "正式图入库于 `docs/gallery/`（无损重压缩的 1080p PNG）；渲染原件在 "
    "`out/gallery/`（不入库）。",
    "",
]

rows = []
for stem in stems:
    st = json.load(open(os.path.join(gallery, f"{stem}.stats.json")))
    lines += [
        f"## {stem}",
        "",
        f"![{stem}](gallery/{stem}.png)",
        "",
        DESC.get(stem, ""),
        "",
    ]
    t = st["timings_ms"]
    rows.append((
        stem, f'{st["width"]}x{st["height"]}', st["spp"],
        "是" if st["denoised"] else "否",
        f'{t["render"] / 1000.0:.2f}',
        f'{st["mrays_per_sec"]:.0f}',
        f'{st["peak_vram_mb"]}',
    ))

lines += [
    "## 渲染统计",
    "",
    "| 图像 | 分辨率 | spp | 降噪 | 渲染时间 (s) | Mrays/s | 峰值显存 (MB) |",
    "|---|---|---|---|---|---|---|",
]
for r in rows:
    lines.append("| " + " | ".join(str(c) for c in r) + " |")
lines.append("")

with open(out_md, "w") as f:
    f.write("\n".join(lines))
print("wrote", out_md)
PY

# Sync the finals into the repo (docs/gallery) so they render on GitHub.
# Losslessly recompress via PIL when available (stb PNGs are ~40% larger);
# fall back to a plain copy.
mkdir -p "$ROOT/docs/gallery"
if python3 -c 'import PIL' 2>/dev/null; then
  python3 - "$GALLERY" "$ROOT/docs/gallery" << 'PY'
import glob, os, sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
for p in sorted(glob.glob(os.path.join(src, "*.png"))):
    out = os.path.join(dst, os.path.basename(p))
    Image.open(p).convert("RGB").save(out, "PNG", optimize=True)
    print(f"optimized {os.path.basename(p)}: {os.path.getsize(p)//1024} KB -> {os.path.getsize(out)//1024} KB")
PY
else
  cp -v "$GALLERY"/*.png "$ROOT/docs/gallery/"
fi

echo "render-gallery OK (${#RENDERED[@]} images in $GALLERY, synced to docs/gallery)"
