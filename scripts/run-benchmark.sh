#!/usr/bin/env bash
# sundog benchmark — three tiers, written to docs/BENCHMARKS.md.
#
# Assumes it runs ON THE TEST BOX with:
#   - $SUNDOG_BUILD/sundog built (default /tmp/sundog-build/sundog)
#   - the CPU baseline built by scripts/build-cpu-baseline.sh
#     (default /tmp/cxxrt-baseline; missing => compat rows are skipped)
# Callable from any cwd.
#
# Tiers:
#   A. compat  — cxxrt CPU examples vs the same scenes on GPU in --parity
#                mode, 1024x1024, spp in {16, 256}. CPU time is taken from
#                cxxrt's own "Rendering elapsed time" stdout line (excludes
#                scene build, same as the GPU "render" timing). NOTE: cxxrt
#                floors spp to a perfect square (floor(sqrt(n))^2), so compat
#                spp values must be perfect squares — 16 and 256 are.
#   B. feature — the five gallery scenes, quick 960x540 / 64 spp stats pass.
#   C. denoise — 02-cornell-lume at 16 spp with/without --denoise, PSNR of
#                each against a 4096 spp reference (img_compare).
# Missing scenes/binaries are skipped and noted in the report.
#
# usage: run-benchmark.sh [--quick]
#   --quick  plumbing self-test: substitutes smoke.json + tiny spp everywhere
#            and writes /tmp/sundog-bench-quick.md instead of docs/BENCHMARKS.md
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
SUNDOG="$SUNDOG_BUILD/sundog"
IMG_COMPARE="$SUNDOG_BUILD/img_compare"
CXXRT="${CXXRT_BASELINE:-/tmp/cxxrt-baseline}"
CPU_THREADS=16

QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

if [ "$QUICK" = 1 ]; then
  OUT_MD=/tmp/sundog-bench-quick.md
  COMPAT_PAIRS=("smoke:example1")
  COMPAT_SPPS=(2)
  FEATURE_SCENES=(smoke)
  DN_SCENE=smoke DN_REF_SPP=64 DN_TEST_SPP=4
else
  OUT_MD="$ROOT/docs/BENCHMARKS.md"
  COMPAT_PAIRS=("compat-01:example1" "compat-03:example3")
  COMPAT_SPPS=(16 256)
  FEATURE_SCENES=(01-prism-court 02-cornell-lume 03-bunny-atrium 04-parabolica 05-bunny-swarm)
  DN_SCENE=02-cornell-lume DN_REF_SPP=4096 DN_TEST_SPP=16
fi
DN_SIZE=960x540

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
calc() { python3 -c "print($1)"; }
psnr_of() { # psnr_of A B -> prints numeric PSNR (inf possible)
  "$IMG_COMPARE" "$1" "$2" | sed -n 's/^PSNR: \([0-9.inf]*\) dB$/\1/p'
}

# =============================== tier A: compat ==============================
echo "==== tier A: compat (CPU cxxrt vs GPU --parity) ===="
A_ROWS=()
for pair in "${COMPAT_PAIRS[@]}"; do
  scene="${pair%%:*}"; cpu_bin="${pair##*:}"
  scene_json="$ROOT/scenes/$scene.json"
  if [ ! -f "$scene_json" ]; then
    NOTES+=("compat: 场景 \`$scene.json\` 不存在，已跳过。")
    echo "  skip $scene (no scene json)"; continue
  fi
  for spp in "${COMPAT_SPPS[@]}"; do
    echo "-- $scene spp=$spp (GPU)"
    st="$TMP/$scene-$spp.stats.json"
    "$SUNDOG" --scene "$scene_json" --out "$TMP/$scene-$spp-gpu.png" \
              --size 1024x1024 --spp "$spp" --parity --gamma 2.0 --clamp 0 \
              --max-depth 50 --quiet --stats "$st"
    gpu_s=$(calc "$(jget "$st" 'd["timings_ms"]["render"]')/1000.0")
    mrays=$(jget "$st" 'round(d["mrays_per_sec"])')

    cpu_s="n/a" speedup="n/a"
    if [ -x "$CXXRT/bin/$cpu_bin" ]; then
      echo "-- $scene spp=$spp (CPU $cpu_bin, $CPU_THREADS threads)"
      t0=$(date +%s.%N)
      cpu_out=$(cd "$CXXRT" && OMP_NUM_THREADS=$CPU_THREADS \
                "./bin/$cpu_bin" "$TMP/$scene-$spp-cpu.png" "$spp")
      t1=$(date +%s.%N)
      # prefer cxxrt's own render-only timing; fall back to wall clock
      cpu_s=$(sed -n 's/.*Rendering elapsed time: \([0-9.eE+-]*\) seconds.*/\1/p' <<<"$cpu_out" | head -1)
      [ -n "$cpu_s" ] || cpu_s=$(calc "$t1-$t0")
      speedup=$(calc "round($cpu_s/$gpu_s, 1)")
      cpu_s=$(calc "round($cpu_s, 2)")
    else
      NOTES+=("compat: CPU 基线 \`$CXXRT/bin/$cpu_bin\` 未构建（先跑 scripts/build-cpu-baseline.sh），$scene spp=$spp 仅有 GPU 数据。")
    fi
    A_ROWS+=("| $scene | $spp | $cpu_s | $(calc "round($gpu_s, 3)") | $speedup | $mrays |")
  done
done

# =============================== tier B: feature =============================
echo "==== tier B: feature scenes (960x540 / 64 spp) ===="
B_ROWS=()
for scene in "${FEATURE_SCENES[@]}"; do
  scene_json="$ROOT/scenes/$scene.json"
  if [ ! -f "$scene_json" ]; then
    NOTES+=("feature: 场景 \`$scene.json\` 不存在，已跳过。")
    echo "  skip $scene (no scene json)"; continue
  fi
  echo "-- $scene"
  st="$TMP/feat-$scene.stats.json"
  "$SUNDOG" --scene "$scene_json" --out "$TMP/feat-$scene.png" \
            --size 960x540 --spp 64 --no-denoise --quiet --stats "$st"
  B_ROWS+=("| $scene | $(jget "$st" 'd["scene_stats"]["objects"]') \
| $(jget "$st" 'd["scene_stats"]["mesh_triangles"]') \
| $(jget "$st" 'd["scene_stats"]["lights"]') \
| $(jget "$st" 'round(d["timings_ms"]["render"]/1000.0, 3)') \
| $(jget "$st" 'round(d["mrays_per_sec"])') \
| $(jget "$st" 'd["peak_vram_mb"]') |")
done

# =============================== tier C: denoise =============================
echo "==== tier C: denoiser PSNR ($DN_SCENE, ref $DN_REF_SPP spp) ===="
C_ROWS=()
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
  C_ROWS+=("| 原始蒙特卡洛 | $DN_TEST_SPP | 否 | $(psnr_of "$TMP/dn-ref.png" "$TMP/dn-raw.png") |")
  C_ROWS+=("| OptiX AI 降噪 | $DN_TEST_SPP | 是 | $(psnr_of "$TMP/dn-ref.png" "$TMP/dn-dn.png") |")
fi

# =============================== report ======================================
gpu_name="unknown"
first_stats=$(ls "$TMP"/*.stats.json 2>/dev/null | head -1 || true)
[ -n "$first_stats" ] && gpu_name=$(jget "$first_stats" 'd["device"]["name"]')

mkdir -p "$(dirname "$OUT_MD")"
{
  echo "# sundog 基准"
  echo
  echo "由 \`scripts/run-benchmark.sh\` 生成于 $(date -Is)。GPU：$gpu_name；"
  echo "CPU 基线：cxxrt（\`$CXXRT\`，OMP_NUM_THREADS=$CPU_THREADS，-O3 -march=native）。"
  [ "$QUICK" = 1 ] && { echo; echo "> **--quick 自测模式**：数据仅验证脚本流程，无参考价值。"; }
  echo
  echo "## A. compat 层 — cxxrt CPU vs sundog GPU"
  echo
  echo "同一场景（cxxrt example 的 1:1 JSON 移植），GPU 端 \`--parity --gamma 2.0"
  echo "--clamp 0 --max-depth 50\`，1024x1024。两边计时都只含渲染循环（CPU 取其"
  echo "\"Rendering elapsed time\" 输出，GPU 取 stats 的 \`timings_ms.render\`）。"
  echo
  echo "| 场景 | spp | CPU (s) | GPU (s) | 加速比 | GPU Mrays/s |"
  echo "|---|---|---|---|---|---|"
  for r in "${A_ROWS[@]:-}"; do [ -n "$r" ] && echo "$r"; done
  echo
  echo "## B. 特性层 — 画廊场景（960x540 / 64 spp / 不降噪）"
  echo
  echo "| 场景 | 物体 | 三角形 | 灯 | 渲染 (s) | Mrays/s | 峰值显存 (MB) |"
  echo "|---|---|---|---|---|---|---|"
  for r in "${B_ROWS[@]:-}"; do [ -n "$r" ] && echo "$r"; done
  echo
  echo "## C. 降噪层 — $DN_SCENE（$DN_SIZE，参考 $DN_REF_SPP spp）"
  echo
  echo "| 图像 | spp | 降噪 | PSNR vs 参考 (dB) |"
  echo "|---|---|---|---|"
  for r in "${C_ROWS[@]:-}"; do [ -n "$r" ] && echo "$r"; done
  echo
  if [ "${#NOTES[@]}" -gt 0 ]; then
    echo "## 备注"
    echo
    for n in "${NOTES[@]}"; do echo "- $n"; done
    echo
  fi
} > "$OUT_MD"

echo "run-benchmark OK -> $OUT_MD"
