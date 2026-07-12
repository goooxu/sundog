#!/usr/bin/env bash
# sundog benchmark — two tiers, written to docs/BENCHMARKS.md.
#
# Assumes it runs ON THE TEST BOX with $SUNDOG_BUILD/sundog built
# (default /tmp/sundog-build/sundog). Callable from any cwd.
#
# Tiers:
#   A. feature — the seven gallery scenes at their native 1920x1080 / 64 spp
#                (render time, ray throughput, VRAM; 06's PhysX settling is
#                reported under timings_ms.physics, not render).
#   B. denoise — 02-cornell-lume at 16 spp with/without --denoise, PSNR of
#                each against a 4096 spp reference (img_compare).
# Missing scenes are skipped and noted in the report.
#
# usage: run-benchmark.sh [--quick]
#   --quick  plumbing self-test: substitutes smoke.json + tiny spp everywhere
#            and writes /tmp/sundog-bench-quick.md instead of docs/BENCHMARKS.md
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
SUNDOG="$SUNDOG_BUILD/sundog"
IMG_COMPARE="$SUNDOG_BUILD/img_compare"

QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

if [ "$QUICK" = 1 ]; then
  OUT_MD=/tmp/sundog-bench-quick.md
  FEATURE_SCENES=(smoke)
  DN_SCENE=smoke DN_REF_SPP=64 DN_TEST_SPP=4
else
  OUT_MD="$ROOT/docs/BENCHMARKS.md"
  FEATURE_SCENES=(01-prism-court 02-cornell-lume 03-spot-atrium 04-parabolica 05-spot-swarm 06-spot-cascade 07-campfire)
  DN_SCENE=02-cornell-lume DN_REF_SPP=4096 DN_TEST_SPP=16
fi
DN_SIZE=1920x1080

fail() { echo "run-benchmark: FAIL: $*" >&2; exit 1; }
[ -x "$SUNDOG" ] || fail "binary not found: $SUNDOG"

if [ ! -x "$IMG_COMPARE" ]; then
  echo "== building img_compare =="
  make -C "$ROOT" SUNDOG_BUILD="$SUNDOG_BUILD" "$IMG_COMPARE" \
    || fail "could not build $IMG_COMPARE"
fi

TMP="$(mktemp -d /tmp/sundog-bench.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
NOTES=()

# --- tiny helpers -----------------------------------------------------------
jget() { # jget FILE PYEXPR   (d = parsed json)
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))' "$1" "$2"
}
psnr_of() { # psnr_of A B -> prints numeric PSNR (inf possible)
  "$IMG_COMPARE" "$1" "$2" | sed -n 's/^PSNR: \([0-9.inf]*\) dB$/\1/p'
}

# =============================== tier A: feature =============================
echo "==== tier A: feature scenes (1920x1080 / 64 spp) ===="
A_ROWS=()
for scene in "${FEATURE_SCENES[@]}"; do
  scene_json="$ROOT/scenes/$scene.json"
  if [ ! -f "$scene_json" ]; then
    NOTES+=("feature: 场景 \`$scene.json\` 不存在，已跳过。")
    echo "  skip $scene (no scene json)"; continue
  fi
  echo "-- $scene"
  st="$TMP/feat-$scene.stats.json"
  "$SUNDOG" --scene "$scene_json" --out "$TMP/feat-$scene.png" \
            --size 1920x1080 --spp 64 --no-denoise --quiet --stats "$st"
  A_ROWS+=("| $scene | $(jget "$st" 'd["scene_stats"]["objects"]') \
| $(jget "$st" 'd["scene_stats"]["mesh_triangles"]') \
| $(jget "$st" 'd["scene_stats"]["lights"]') \
| $(jget "$st" 'round(d["timings_ms"]["render"]/1000.0, 3)') \
| $(jget "$st" 'round(d["mrays_per_sec"])') \
| $(jget "$st" 'd["peak_vram_mb"]') |")
done

# =============================== tier B: denoise =============================
echo "==== tier B: denoiser PSNR ($DN_SCENE, ref $DN_REF_SPP spp) ===="
B_ROWS=()
dn_json="$ROOT/scenes/$DN_SCENE.json"
if [ ! -f "$dn_json" ]; then
  NOTES+=("denoise: 场景 \`$DN_SCENE.json\` 不存在，整层跳过。")
  echo "  skip (no scene json)"
else
  echo "-- reference ($DN_REF_SPP spp)"
  "$SUNDOG" --scene "$dn_json" --out "$TMP/dn-ref.png" --size "$DN_SIZE" \
            --spp "$DN_REF_SPP" --no-denoise --quiet
  echo "-- noisy ($DN_TEST_SPP spp)"
  "$SUNDOG" --scene "$dn_json" --out "$TMP/dn-raw.png" --size "$DN_SIZE" \
            --spp "$DN_TEST_SPP" --no-denoise --quiet
  echo "-- denoised ($DN_TEST_SPP spp + --denoise)"
  "$SUNDOG" --scene "$dn_json" --out "$TMP/dn-dn.png" --size "$DN_SIZE" \
            --spp "$DN_TEST_SPP" --denoise --quiet
  B_ROWS+=("| 原始蒙特卡洛 | $DN_TEST_SPP | 否 | $(psnr_of "$TMP/dn-ref.png" "$TMP/dn-raw.png") |")
  B_ROWS+=("| OptiX AI 降噪 | $DN_TEST_SPP | 是 | $(psnr_of "$TMP/dn-ref.png" "$TMP/dn-dn.png") |")
fi

# =============================== report ======================================
gpu_name="unknown"
first_stats=$(ls "$TMP"/*.stats.json 2>/dev/null | head -1 || true)
[ -n "$first_stats" ] && gpu_name=$(jget "$first_stats" 'd["device"]["name"]')

mkdir -p "$(dirname "$OUT_MD")"
{
  echo "# sundog 基准"
  echo
  echo "由 \`scripts/run-benchmark.sh\` 生成于 $(date -Is)。GPU：$gpu_name。"
  [ "$QUICK" = 1 ] && { echo; echo "> **--quick 自测模式**：数据仅验证脚本流程，无参考价值。"; }
  echo
  echo "## A. 特性层 — 画廊场景（1920x1080 / 64 spp / 不降噪）"
  echo
  echo "| 场景 | 物体 | 三角形 | 灯 | 渲染 (s) | Mrays/s | 峰值显存 (MB) |"
  echo "|---|---|---|---|---|---|---|"
  for r in "${A_ROWS[@]:-}"; do [ -n "$r" ] && echo "$r"; done
  echo
  echo "## B. 降噪层 — $DN_SCENE（$DN_SIZE，参考 $DN_REF_SPP spp）"
  echo
  echo "| 图像 | spp | 降噪 | PSNR vs 参考 (dB) |"
  echo "|---|---|---|---|"
  for r in "${B_ROWS[@]:-}"; do [ -n "$r" ] && echo "$r"; done
  echo
  if [ "${#NOTES[@]}" -gt 0 ]; then
    echo "## 备注"
    echo
    for n in "${NOTES[@]}"; do echo "- $n"; done
    echo
  fi
} > "$OUT_MD"

echo "run-benchmark OK -> $OUT_MD"
