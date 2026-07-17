#!/usr/bin/env python3
"""Generate the three matplotlib data charts for the technical report.

Charts (written to docs/report/figures/):
  ch03-mc-convergence.png  PSNR vs spp for 02-cornell-lume (log2 x-axis)
                           + O(1/N) reference slope (+6.02 dB per 4x spp)
  ch05-fresnel-curves.png  exact dielectric Fresnel (eta=1.5) vs Schlick
                           docs/BENCHMARKS.md), grouped bars, log y

Data collection (chart 1) renders on the GPU test box and needs the sundog
binaries; run this script ON the test box:

    python3 scripts/gen-report-charts.py

Measured PSNR data is cached in docs/report/figures/src/mc-convergence.json;
re-plot without re-rendering via:

    python3 scripts/gen-report-charts.py --skip-render

Options:
  --skip-render      reuse mc-convergence.json instead of rendering
  --build-dir DIR    sundog build dir (default $SUNDOG_BUILD or /tmp/sundog-build)
  --work-dir DIR     scratch dir for intermediate renders (default /tmp/report-charts)
  --only LIST        comma list of charts: convergence,fresnel,env,aces
"""

import argparse
import json
import math
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
FIG_DIR = ROOT / "docs" / "report" / "figures"
SRC_DIR = FIG_DIR / "src"
BENCHMARKS_MD = ROOT / "docs" / "BENCHMARKS.md"
SCENE = ROOT / "scenes" / "02-cornell-lume.json"
ENV_HDR = ROOT / "assets" / "kloofendal_48d_partly_cloudy_puresky_4k.hdr"

# ---- report chart style (frozen: see docs/report/OUTLINE.md) ----------------
PRIMARY = "#2563EB"   # accent blue
SECONDARY = "#94A3B8" # secondary gray (reference lines / CPU series)
GRID = "#E2E8F0"      # light grid, drawn below data
INK = "#334155"       # text / labels
INK_MUTED = "#64748B" # de-emphasized text

# 7.0 in x 200 dpi = 1400 px wide PNG
FIGSIZE = (7.0, 4.4)
DPI = 200

SPPS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024]
REF_SPP = 4096
SEED = 7
SIZE = "480x270"


def setup_style():
    matplotlib.rcParams.update({
        "figure.facecolor": "white",
        "axes.facecolor": "white",
        "savefig.facecolor": "white",
        "font.family": "DejaVu Sans",
        "font.size": 12,
        "axes.titlesize": 13.5,
        "axes.titleweight": "bold",
        "axes.titlecolor": INK,
        "axes.labelsize": 12,
        "axes.labelcolor": INK,
        "axes.edgecolor": SECONDARY,
        "axes.linewidth": 1.0,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "axes.axisbelow": True,          # grid under the data
        "grid.color": GRID,
        "grid.linewidth": 1.0,
        "xtick.color": INK,
        "ytick.color": INK,
        "xtick.labelsize": 12,
        "ytick.labelsize": 12,
        "legend.frameon": False,
        "legend.fontsize": 12,
    })


def save(fig, name):
    out = FIG_DIR / name
    fig.savefig(out, dpi=DPI, bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)
    print(f"wrote {out}")


# ---------------------------------------------------------------- chart 1 ---

def run(cmd):
    print("  $", " ".join(str(c) for c in cmd))
    res = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f"command failed ({res.returncode}):\n{res.stdout}\n{res.stderr}")
    return res.stdout + res.stderr


def collect_convergence(build_dir, work_dir):
    sundog = build_dir / "sundog"
    img_compare = build_dir / "img_compare"
    for exe in (sundog, img_compare):
        if not exe.exists():
            sys.exit(f"missing binary: {exe} (run on the GPU test box, or --skip-render)")
    work_dir.mkdir(parents=True, exist_ok=True)

    def render(spp):
        out = work_dir / f"cornell-spp{spp}.png"
        run([sundog, "--scene", SCENE, "--out", out, "--spp", spp,
             "--size", SIZE, "--seed", SEED, "--no-denoise", "--quiet"])
        return out

    ref = render(REF_SPP)
    psnr = []
    for spp in SPPS:
        img = render(spp)
        txt = run([img_compare, img, ref])
        m = re.search(r"PSNR:\s*([0-9.]+|inf)", txt)
        if not m:
            sys.exit(f"cannot parse img_compare output:\n{txt}")
        psnr.append(float(m.group(1)))
        print(f"  spp {spp:5d}: {psnr[-1]:.2f} dB")

    data = {
        "scene": SCENE.name,
        "size": SIZE,
        "seed": SEED,
        "reference_spp": REF_SPP,
        "flags": "--no-denoise",
        "spp": SPPS,
        "psnr_db": psnr,
        "generated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }
    SRC_DIR.mkdir(parents=True, exist_ok=True)
    path = SRC_DIR / "mc-convergence.json"
    path.write_text(json.dumps(data, indent=2) + "\n")
    print(f"wrote {path}")
    return data


def chart_convergence(data):
    spp = np.array(data["spp"], dtype=float)
    psnr = np.array(data["psnr_db"], dtype=float)

    # theory: MSE ~ 1/N  =>  PSNR = C + 10*log10(N)  (+6.02 dB per 4x spp),
    # anchored at the measured midpoint
    mid = len(spp) // 2
    theory = psnr[mid] + 10.0 * np.log10(spp / spp[mid])

    fig, ax = plt.subplots(figsize=FIGSIZE)
    ax.grid(True, which="major", axis="both")
    ax.plot(spp, theory, ls="--", lw=1.8, color=SECONDARY, zorder=2,
            label="O(1/N) reference: +6.02 dB per 4x spp")
    ax.plot(spp, psnr, marker="o", ms=6.5, lw=2.2, color=PRIMARY, zorder=3,
            label=f"Measured PSNR vs {data['reference_spp']} spp reference")

    ax.set_xscale("log", base=2)
    ax.set_xticks(spp)
    ax.set_xticklabels([str(int(s)) for s in spp])
    ax.tick_params(which="minor", bottom=False)
    ax.set_xlabel("Samples per pixel (spp, log scale)")
    ax.set_ylabel("PSNR (dB)")
    ax.set_title("Monte Carlo convergence - Cornell scene (02-cornell-lume)")

    # selective direct labels on the endpoints (offset away from both lines)
    ax.annotate(f"{psnr[0]:.1f} dB", (spp[0], psnr[0]),
                textcoords="offset points", xytext=(10, -14),
                ha="left", fontsize=12, color=INK)
    ax.annotate(f"{psnr[-1]:.1f} dB", (spp[-1], psnr[-1]),
                textcoords="offset points", xytext=(0, 9),
                ha="center", fontsize=12, color=INK)
    ax.set_ylim(psnr.min() - 3, psnr.max() + 5)
    ax.legend(loc="lower right")
    save(fig, "ch03-mc-convergence.png")


# ---------------------------------------------------------------- chart 2 ---

def fresnel_exact(cos_i, eta):
    """Unpolarized dielectric Fresnel reflectance, external incidence n1=1 -> n2=eta."""
    sin_t = np.sqrt(np.maximum(0.0, 1.0 - cos_i ** 2)) / eta
    cos_t = np.sqrt(np.maximum(0.0, 1.0 - sin_t ** 2))
    rs = (cos_i - eta * cos_t) / (cos_i + eta * cos_t)          # s-polarized
    rp = (eta * cos_i - cos_t) / (eta * cos_i + cos_t)          # p-polarized
    return 0.5 * (rs ** 2 + rp ** 2)


def fresnel_schlick(cos_i, eta):
    f0 = ((eta - 1.0) / (eta + 1.0)) ** 2
    return f0 + (1.0 - f0) * (1.0 - cos_i) ** 5


def chart_fresnel():
    eta = 1.5
    theta = np.linspace(0.0, 90.0, 1801)
    cos_i = np.cos(np.radians(theta))
    exact = fresnel_exact(cos_i, eta)
    schlick = fresnel_schlick(cos_i, eta)
    diff = schlick - exact
    imax = int(np.argmax(np.abs(diff)))

    fig, ax = plt.subplots(figsize=FIGSIZE)
    ax.grid(True, axis="y")
    ax.plot(theta, exact, lw=2.2, color=PRIMARY, zorder=3,
            label="Exact dielectric Fresnel (eta = 1.5)")
    ax.plot(theta, schlick, lw=2.2, ls="--", color=SECONDARY, zorder=3,
            label="Schlick approximation")

    ax.set_xlim(0, 90)
    ax.set_ylim(0, 1.02)
    ax.set_xticks(np.arange(0, 91, 15))
    ax.set_xlabel("Angle of incidence (degrees)")
    ax.set_ylabel("Reflectance F (unpolarized)")
    ax.set_title("Fresnel reflectance: exact vs Schlick, eta = 1.5")
    ax.legend(loc="upper left", bbox_to_anchor=(0.02, 0.98))

    # annotate the maximum deviation on the main axes
    ax.annotate(
        f"max deviation {diff[imax]:+.4f}\nat {theta[imax]:.0f} deg",
        xy=(theta[imax], exact[imax]),
        xytext=(48, 0.50), ha="left", va="center",
        fontsize=12, color=INK,
        arrowprops=dict(arrowstyle="->", color=INK_MUTED, lw=1.2,
                        shrinkB=6),
    )

    # inset: signed difference Schlick - exact (below the legend)
    ins = ax.inset_axes([0.11, 0.33, 0.34, 0.28])
    ins.set_facecolor("white")
    ins.axhline(0.0, color=GRID, lw=1.0, zorder=1)
    ins.plot(theta, diff, lw=1.8, color=PRIMARY, zorder=2)
    ins.plot([theta[imax]], [diff[imax]], "o", ms=5, color=PRIMARY, zorder=3)
    ins.set_xlim(0, 90)
    ins.set_xticks([0, 30, 60, 90])
    ins.set_title("Schlick - exact", fontsize=12, color=INK)
    ins.tick_params(labelsize=12, colors=INK)
    for side in ("top", "right"):
        ins.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ins.spines[side].set_color(SECONDARY)

    save(fig, "ch05-fresnel-curves.png")


# ---------------------------------------------------------------- chart 5 ---
# ch17-coupling-energy.png: hemispherical albedo of the plastic BSDF under
# the three viable diffuse-coupling candidates of report ch17 (white base,
# coat roughness 0.15). The GGX+Schlick coat integral mirrors plasticTerms()
# (device/bsdf.cuh): stable-form D, height-correlated Smith G, VNDF-free MC
# over a shared uniform-hemisphere sample set (deterministic seed).


def _plastic_coat_albedo(cos_o, eta=1.5, roughness=0.15, n=200_000):
    """E_spec(theta_o) = int f_coat * cos dwi over the upper hemisphere."""
    alpha = roughness * roughness
    rng = np.random.default_rng(20260717)
    # one shared sample set across angles keeps the curves smooth
    u1, u2 = rng.random(n), rng.random(n)
    z = u1                                  # uniform hemisphere in cos
    r = np.sqrt(np.maximum(0.0, 1.0 - z * z))
    phi = 2.0 * np.pi * u2
    li = np.stack([r * np.cos(phi), r * np.sin(phi), z], axis=1)

    def lam(c2):
        t2 = (1.0 - c2) / np.maximum(c2, 1e-12)
        return 0.5 * (-1.0 + np.sqrt(1.0 + alpha * alpha * t2))

    out = []
    for co in np.atleast_1d(cos_o):
        lo = np.array([np.sqrt(max(0.0, 1.0 - co * co)), 0.0, co])
        h = lo[None, :] + li
        h /= np.linalg.norm(h, axis=1, keepdims=True)
        c2 = h[:, 2] ** 2
        k = alpha * alpha * c2 + (1.0 - c2)          # ggxD stable grouping
        d = alpha * alpha / (np.pi * k * k)
        f = fresnel_schlick(np.einsum("j,ij->i", lo, h), eta)
        g = 1.0 / (1.0 + lam(co * co) + lam(z * z))
        fspec = f * d * g / (4.0 * co * z)
        out.append(2.0 * np.pi * np.mean(fspec * z))  # uniform-hemisphere MC
    return np.array(out)


def chart_plastic():
    eta, albedo = 1.5, 1.0
    f0 = ((eta - 1.0) / (eta + 1.0)) ** 2
    fbar = f0 + (1.0 - f0) / 21.0                     # hemispheric Schlick mean
    theta = np.linspace(0.0, 89.0, 46)
    cos_o = np.cos(np.radians(theta))
    e_spec = _plastic_coat_albedo(cos_o)

    e_a = e_spec + albedo                             # (a) uncoupled
    e_c = e_spec + albedo * (1.0 - f0)                # (c) constant 1-F0
    e_d = e_spec + albedo * (1.0 - fresnel_schlick(cos_o, eta)) * (1.0 - fbar)

    fig, ax = plt.subplots(figsize=FIGSIZE)
    ax.grid(True, axis="y")
    ax.axhline(1.0, color=INK_MUTED, lw=1.4, ls=":", zorder=2)
    ax.plot(theta, e_a, lw=2.0, ls="--", color=SECONDARY, zorder=3,
            label="(a) uncoupled: over budget everywhere")
    ax.plot(theta, e_c, lw=2.0, ls="-.", color="#DC2626", zorder=3,
            label="(c) constant 1-F0: blows past 1 at grazing")
    ax.plot(theta, e_d, lw=2.4, color=PRIMARY, zorder=4,
            label="(d) bidirectional (1-F)(1-F): <= 1 at every angle")
    ax.annotate("energy budget = 1", xy=(4, 1.0), xytext=(4, 1.06),
                fontsize=12, color=INK_MUTED)

    ax.set_xlim(0, 89)
    ax.set_ylim(0, 1.75)
    ax.set_xticks(np.arange(0, 90, 15))
    ax.set_xlabel("Angle of incidence (degrees)")
    ax.set_ylabel("Hemispherical albedo E")
    ax.set_title("Plastic diffuse coupling: white base, coat roughness 0.15")
    ax.legend(loc="upper left", bbox_to_anchor=(0.02, 0.98))
    save(fig, "ch17-coupling-energy.png")


# ---------------------------------------------------------------- chart 3 ---

ACES_IN = np.array([[0.59719, 0.35458, 0.04823],
                    [0.07600, 0.90834, 0.01566],
                    [0.02840, 0.13383, 0.83777]])
ACES_OUT = np.array([[1.60475, -0.53108, -0.07367],
                     [-0.10208, 1.10813, -0.00605],
                     [-0.00327, -0.07276, 1.07602]])


def aces_fitted_gray(x):
    """Gray-axis ACES (Hill fit, matches src/tonemap.h) for an array of
    scalar inputs; gray in -> gray out, so one channel tells the story."""
    v = np.stack([x, x, x], axis=-1) @ ACES_IN.T
    v = (v * (v + 0.0245786) - 0.000090537) / (v * (0.983729 * v + 0.4329510) + 0.238081)
    v = v @ ACES_OUT.T
    return np.clip(v[..., 0], 0.0, 1.0)


def chart_aces():
    x = np.geomspace(0.01, 30.0, 512)
    fig, ax = plt.subplots(figsize=FIGSIZE)
    ax.semilogx(x, np.minimum(x, 1.0), color=SECONDARY, lw=2.0, ls="--",
                label="clamp: y = min(x, 1)")
    ax.semilogx(x, aces_fitted_gray(x), color=PRIMARY, lw=2.2,
                label="ACES (Hill fit)")
    for gx, name in [(0.18, "0.18"), (1.0, "1.0")]:
        gy = aces_fitted_gray(np.array([gx]))[0]
        ax.plot([gx], [gy], "o", color=INK, ms=5)
        ax.annotate(f"{name} -> {gy:.3f}", (gx, gy), textcoords="offset points",
                    xytext=(8, -14), color=INK, fontsize=11)
    ax.annotate("toe", (0.02, aces_fitted_gray(np.array([0.02]))[0]),
                textcoords="offset points", xytext=(6, 8), color=INK_MUTED, fontsize=11)
    ax.annotate("shoulder", (6.0, aces_fitted_gray(np.array([6.0]))[0]),
                textcoords="offset points", xytext=(8, -16), color=INK_MUTED, fontsize=11)
    ax.set_xlabel("linear input (log scale)")
    ax.set_ylabel("encoded output (linear sRGB, before gamma)")
    ax.set_title("Tone mapping: hard clamp vs ACES filmic S-curve")
    ax.set_ylim(0, 1.05)
    ax.grid(True, which="both")
    ax.legend(loc="upper left")
    save(fig, "ch01-aces-curve.png")


# ---------------------------------------------------------------- chart 4 ---



# ---------------------------------------------------------------- chart 3 ---

def read_hdr_rgbe(path):
    """Minimal Radiance .hdr reader (new-style RLE), matching stb's
    rgbe -> float mapping f = ldexp(1, e - 136). Returns float32 (H, W, 3)."""
    with open(path, "rb") as f:
        if not f.readline().startswith(b"#?"):
            sys.exit(f"{path}: not a Radiance file")
        while f.readline().strip():
            pass  # header key=value lines until the blank separator
        m = re.match(rb"-Y (\d+) \+X (\d+)", f.readline())
        if not m:
            sys.exit(f"{path}: unsupported resolution line")
        h, w = int(m.group(1)), int(m.group(2))
        img = np.empty((h, w, 4), np.uint8)
        for y in range(h):
            head = f.read(4)
            if head[0] != 2 or head[1] != 2 or (head[2] << 8 | head[3]) != w:
                sys.exit(f"{path}: expected new-style RLE scanline")
            for c in range(4):
                row = np.empty(w, np.uint8)
                x = 0
                while x < w:
                    n = f.read(1)[0]
                    if n > 128:  # run of one repeated byte
                        row[x:x + n - 128] = f.read(1)[0]
                        x += n - 128
                    else:        # n literal bytes
                        row[x:x + n] = np.frombuffer(f.read(n), np.uint8)
                        x += n
                img[y, :, c] = row
    scale = np.ldexp(1.0, img[:, :, 3].astype(np.int32) - 136)
    return (img[:, :, :3].astype(np.float32) * scale[:, :, None].astype(np.float32))


def chart_env():
    if not ENV_HDR.exists():
        sys.exit(f"{ENV_HDR} missing — run scripts/fetch-assets.sh first")
    rgb = read_hdr_rgbe(ENV_HDR)
    h, w = rgb.shape[:2]
    lum = 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]
    sin_t = np.sin(np.pi * (np.arange(h) + 0.5) / h)
    marginal = (lum * sin_t[:, None]).sum(axis=1)

    fig, (ax_img, ax) = plt.subplots(
        2, 1, figsize=(FIGSIZE[0], 6.2),
        gridspec_kw={"height_ratios": [1.0, 1.15], "hspace": 0.28})

    # top: tonemapped thumbnail with the sun row marked
    thumb = rgb[::4, ::4]
    tm = np.power(thumb / (1.0 + thumb), 1 / 2.2)
    ax_img.imshow(tm, extent=[0, w, h, 0], aspect="auto")
    sun_row = int(np.argmax(marginal))
    ax_img.axhline(sun_row, color=PRIMARY, lw=1.4, ls="--")
    ax_img.set_title("kloofendal 4k equirect panorama (dashed = sun row)")
    ax_img.set_xticks([]); ax_img.set_yticks([])

    # bottom: per-row marginal luminance (the CDF's target function)
    ax.semilogy(np.arange(h), marginal, color=PRIMARY, lw=1.2)
    ax.axvline(sun_row, color=SECONDARY, lw=1.0, ls="--")
    peak = marginal[sun_row]
    med = float(np.median(marginal[marginal > 0]))
    ax.annotate(f"sun row: {peak / med:,.0f}x the median row",
                xy=(sun_row, peak), xytext=(sun_row + w * 0.03, peak * 0.55),
                color=INK, fontsize=11,
                arrowprops={"arrowstyle": "->", "color": INK_MUTED})
    ax.set_xlabel("image row (0 = zenith, v = theta/pi)")
    ax.set_ylabel("row marginal: luminance x sin(theta), log")
    ax.set_title("Energy concentrates in a few rows - the 2D CDF's first level")
    ax.grid(True, which="both", axis="y")
    ax.set_xlim(0, h)
    save(fig, "ch14-env-luminance.png")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--skip-render", action="store_true",
                    help="reuse figures/src/mc-convergence.json")
    ap.add_argument("--build-dir", type=Path,
                    default=Path(os.environ.get("SUNDOG_BUILD", "/tmp/sundog-build")))
    ap.add_argument("--work-dir", type=Path, default=Path("/tmp/report-charts"))
    ap.add_argument("--only", default="convergence,fresnel,env,aces,plastic",
                    help="comma list: convergence,fresnel,env,aces,plastic")
    args = ap.parse_args()
    only = set(args.only.split(","))

    setup_style()
    FIG_DIR.mkdir(parents=True, exist_ok=True)

    if "convergence" in only:
        cache = SRC_DIR / "mc-convergence.json"
        if args.skip_render:
            if not cache.exists():
                sys.exit(f"--skip-render given but {cache} does not exist")
            data = json.loads(cache.read_text())
        else:
            data = collect_convergence(args.build_dir, args.work_dir)
        chart_convergence(data)
    if "fresnel" in only:
        chart_fresnel()
    if "env" in only:
        chart_env()
    if "aces" in only:
        chart_aces()
    if "plastic" in only:
        chart_plastic()


if __name__ == "__main__":
    main()
