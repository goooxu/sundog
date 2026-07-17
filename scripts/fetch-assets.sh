#!/usr/bin/env bash
# sundog: idempotent asset fetcher.
# All meshes and textures are committed in-tree (assets/, scenes/textures/;
# provenance in assets/LICENSES.md) — the only download left is the 20 MB
# sunny-sky HDRI "Kloofendal 48d Partly Cloudy (Pure Sky)" (Poly Haven,
# CC0 1.0), needed by scenes 10/11/12/15/16. Safe to re-run: exits early if valid.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="$ROOT/assets"

HDR="$ASSETS/kloofendal_48d_partly_cloudy_puresky_4k.hdr"
HDR_URL="https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/kloofendal_48d_partly_cloudy_puresky_4k.hdr"
HDR_MIN_BYTES=10000000

mkdir -p "$ASSETS"

valid_hdr() {
  # Radiance magic + expected 4k equirect resolution + plausible size.
  [ -f "$1" ] || return 1
  head -c 11 "$1" | grep -aq '^#?RADIANCE' || return 1
  head -c 256 "$1" | grep -aq -- '-Y 2048 +X 4096' || return 1
  [ "$(stat -c %s "$1")" -ge "$HDR_MIN_BYTES" ]
}

# ---- sky HDRI ----------------------------------------------------------------
if valid_hdr "$HDR"; then
  echo "[fetch-assets] $HDR ($(stat -c %s "$HDR") bytes) already present."
else
  echo "[fetch-assets] fetching $HDR_URL"
  curl -fSL --retry 3 --connect-timeout 20 -o "$HDR.part" "$HDR_URL"
  if ! valid_hdr "$HDR.part"; then
    echo "[fetch-assets] ERROR: downloaded HDR failed validation (Radiance magic / 4096x2048 / >= $HDR_MIN_BYTES bytes)" >&2
    rm -f "$HDR.part"
    exit 1
  fi
  mv "$HDR.part" "$HDR"
  echo "[fetch-assets] wrote $HDR ($(stat -c %s "$HDR") bytes)"
fi
