#!/usr/bin/env bash
# sundog gallery render — full-quality 1920x1080 renders of the showcase
# scenes into out/gallery/, then (re)generates docs/GALLERY.md.
#
# Assumes it runs ON THE TEST BOX with $SUNDOG_BUILD/sundog built
# (default /tmp/sundog-build/sundog). Callable from any cwd.
#
# Per-scene spp: see ENTRIES; 09 additionally renders a 16 spp denoised vs
# raw comparison pair. Every render writes a .stats.json next to
# the PNG. Scenes that do not exist yet are skipped with a warning.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
BACKEND="$SUNDOG_BUILD/libsundog.so"
GALLERY="$ROOT/out/gallery"
SIZE="${GALLERY_SIZE:-1920x1080}"

# scene:spp — main gallery list
ENTRIES=(
  "01-marble-run:512"
  "02-cornell-lume:512"
  "03-spot-atrium:256"
  "04-parabolica:512"
  "05-spot-swarm:128"
  "06-spot-cascade:256"
  "07-campfire:512"
  "08-lakeside:512"
  "09-ember-shore:512"
  "10-suncatcher:512"
  "11-glasswork:512"
  "12-molten-oracle:1024"
  "13-frosted-veil:1024"
  "14-toy-factory:384"
)

fail() { echo "render-gallery: FAIL: $*" >&2; exit 1; }
[ -f "$BACKEND" ] || fail "backend not found: $BACKEND"
mkdir -p "$GALLERY"

RENDERED=()  # image stems, in display order

# JOBS>1 runs renders as a background pool, round-robin over the visible
# GPUs (multi-GPU boxes; JOBS=1 keeps the classic serial behavior). Output
# files are per-stem so parallel renders never collide; failures surface in
# the post-wait verification pass, since a fail() inside a background job
# cannot abort the parent.
JOBS="${JOBS:-1}"
NGPU="$(nvidia-smi -L 2>/dev/null | wc -l)"; [ "$NGPU" -ge 1 ] || NGPU=1
SLOT=0

render() { # render STEM SCENE SPP EXTRA_ARGS...
  local stem="$1" scene="$2" spp="$3"; shift 3
  echo "== $stem ($SIZE, $spp spp) =="
  if [ "$JOBS" -gt 1 ]; then
    CUDA_VISIBLE_DEVICES="$((SLOT % NGPU))" \
      python3 "$scene" --out "$GALLERY/$stem.png" --size "$SIZE" \
              --spp "$spp" --stats "$GALLERY/$stem.stats.json" --quiet "$@" &
    SLOT=$((SLOT + 1))
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n || true; done
  else
    python3 "$scene" --out "$GALLERY/$stem.png" --size "$SIZE" \
              --spp "$spp" --stats "$GALLERY/$stem.stats.json" --quiet "$@"
    [ -s "$GALLERY/$stem.png" ] || fail "empty output for $stem"
  fi
  RENDERED+=("$stem")
}

# Sections: scenes (the per-scene catalog), hero (the 2K flagship), compare
# (the feature ON/OFF pairs). SECTIONS=hero,compare renders incrementally —
# the GALLERY.md generator reads whatever renders exist in out/gallery, so
# catalog entries rendered on a previous run (and their stats) are kept.
SECTIONS="${SECTIONS:-scenes,hero,compare}"
has() { case ",$SECTIONS," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }

if has scenes; then
for entry in "${ENTRIES[@]}"; do
  name="${entry%%:*}"; spp="${entry##*:}"
  scene="$ROOT/scenes/$name.py"
  if [ ! -f "$scene" ]; then
    echo "render-gallery: WARNING: scene $name.py not found, skipping" >&2
    continue
  fi
  if [ "$name" = "06-spot-cascade" ]; then
    # hero = mid-pour freeze-frame (PhysX baked at t=1.0 s); companion = the
    # same initial conditions simulated to rest
    render "06-spot-cascade"         "$scene" "$spp" --no-denoise --physics-time 1.0
    render "06-spot-cascade-settled" "$scene" "$spp" --no-denoise
  else
    render "$name" "$scene" "$spp" --no-denoise
  fi
  if [ "$name" = "09-ember-shore" ]; then
    # low-spp denoiser comparison pair: flame volume + water + soft shadows
    # are all heavy noise sources at 16 spp
    render "09-ember-shore-spp16-denoised" "$scene" 16 --denoise
    render "09-ember-shore-spp16-raw"      "$scene" 16 --no-denoise
  fi
done
fi

# ---- flagship demo (class 1): every feature in one 2K frame ---------------
if has hero && [ -f "$ROOT/scenes/15-assembly-hall.py" ]; then
  SIZE=2560x1440 render "15-assembly-hall" "$ROOT/scenes/15-assembly-hall.py" \
    768 --no-denoise
fi

wait || true
for stem in "${RENDERED[@]}"; do
  [ -s "$GALLERY/$stem.png" ] || fail "empty output for $stem"
done

# ---- feature ON/OFF comparison pairs (class 2): 1080p, same scene, same
# camera, one switch apart. Plain pairs flip a CLI switch; variant pairs
# load the scene via runpy and mutate s.doc (original scenes untouched —
# the render-report-figures.sh recipe). Serial: each frame is sub-second.
CMP="$GALLERY/compare"
CMP_STEMS=()

cr() { # cr STEM SCENE EXTRA_ARGS...
  local stem="$1" scene="$2"; shift 2
  echo "== compare/$stem (1920x1080) =="
  python3 "$scene" --out "$CMP/$stem.png" --size 1920x1080 --quiet "$@"
  [ -s "$CMP/$stem.png" ] || fail "empty compare output for $stem"
  CMP_STEMS+=("$stem")
}

if has compare; then
mkdir -p "$CMP"

# 1. transparent shadows: glass caustic alley, boolean occlusion vs Fresnel
#    + Beer-Lambert transmission (no flames in 11, so the switch is pure)
cr transparent-shadows-on  "$ROOT/scenes/11-glasswork.py" --spp 512 --no-denoise
cr transparent-shadows-off "$ROOT/scenes/11-glasswork.py" --spp 512 --no-denoise \
   --opaque-shadows

# 2. flame volumetric shadow: the cover smoke column under the dome light
#    (--opaque-shadows also disables transparent shadows; the visible delta
#    in this framing is the smoke column's shadow)
cr flame-shadow-on  "$ROOT/scenes/12-molten-oracle.py" --spp 512 --no-denoise
cr flame-shadow-off "$ROOT/scenes/12-molten-oracle.py" --spp 512 --no-denoise \
   --opaque-shadows

# 3. env-map importance sampling: equal-spp sun-lit scene, uniform-sphere
#    NEE vs 2D-CDF importance sampling
cr env-importance-on "$ROOT/scenes/10-suncatcher.py" --spp 64 --no-denoise
echo "== compare/env-importance-off (variant) =="
python3 - "$ROOT" "$CMP/env-importance-off.png" <<'PY'
import os, runpy, sys
root, out = sys.argv[1:]
scenes = os.path.join(root, "scenes")
sys.path.insert(0, scenes)
g = runpy.run_path(os.path.join(scenes, "10-suncatcher.py"))
s = g["s"]
s.doc["background"]["importance"] = False
s.run(out=out, argv=["--size", "1920x1080", "--spp", "64",
                     "--no-denoise", "--quiet"], base_dir=scenes)
PY
[ -s "$CMP/env-importance-off.png" ] || fail "empty env-importance-off"
CMP_STEMS+=("env-importance-off")

# 4. AI denoiser: 16 spp raw Monte Carlo vs the same 16 spp denoised
cr ai-denoise-on  "$ROOT/scenes/09-ember-shore.py" --spp 16 --denoise
cr ai-denoise-off "$ROOT/scenes/09-ember-shore.py" --spp 16 --no-denoise

# 5. mesh NEE lights: scene 03 at night — explicit lights removed and the
#    sky dimmed so Sparky's screens are the only light source. BOTH sides
#    are this darkened variant; they differ only in nee on the screens.
#    (In the stock daytime scene the sun drowns the screens' contribution.)
for side in on off; do
  echo "== compare/mesh-light-$side (night variant) =="
  python3 - "$ROOT" "$CMP/mesh-light-$side.png" "$side" <<'PY'
import os, runpy, sys
root, out, side = sys.argv[1:]
scenes = os.path.join(root, "scenes")
sys.path.insert(0, scenes)
g = runpy.run_path(os.path.join(scenes, "03-spot-atrium.py"))
s = g["s"]
# s.doc rebuilds its dict per access — whole-key swaps do not stick. Mutate
# the Scene's own fields: clear the shared lights list in place, replace
# the background attribute, and edit object dicts (shared references).
s._lights[:] = []                        # no sun, no fill: screens only
s._background = {"type": "gradient",
                 "horizon": [0.010, 0.012, 0.02],
                 "zenith": [0.002, 0.003, 0.006]}
# boost the screens so the lighting difference reads at a glance
# (material dicts are shared references — element edits stick)
s.doc["materials"]["sparkyScreen"]["intensity"] = 24.0
s.doc["materials"]["sparkyGlow"]["intensity"] = 36.0
# equal-spp NEE comparisons are about VARIANCE, not brightness (the
# estimator is unbiased either way) — so lift the exposure to make the
# night readable and disable the firefly clamp on both sides, letting the
# BSDF-only variance show itself honestly
s.doc["render"]["exposure"] = 2.2
s.doc["render"]["clamp"] = 0
if side == "off":
    n = 0
    for o in s.doc["objects"]:
        if o.get("material") in ("sparkyScreen", "sparkyGlow"):
            o["nee"] = False
            n += 1
    assert n, "no emissive mesh groups found"
s.run(out=out, argv=["--size", "1920x1080", "--spp", "256",
                     "--no-denoise", "--quiet"], base_dir=scenes)
PY
  [ -s "$CMP/mesh-light-$side.png" ] || fail "empty mesh-light-$side"
  CMP_STEMS+=("mesh-light-$side")
done

# 6. ACES tone mapping vs linear clamp: the campfire core
cr aces-tonemap-on  "$ROOT/scenes/07-campfire.py" --spp 512 --no-denoise
cr aces-tonemap-off "$ROOT/scenes/07-campfire.py" --spp 512 --no-denoise \
   --tonemap clamp

# 7. NEE: equal-spp Cornell, light sampling on vs BSDF-only paths
cr nee-on "$ROOT/scenes/02-cornell-lume.py" --spp 64 --no-denoise
echo "== compare/nee-off (variant) =="
python3 - "$ROOT" "$CMP/nee-off.png" <<'PY'
import os, runpy, sys
root, out = sys.argv[1:]
scenes = os.path.join(root, "scenes")
sys.path.insert(0, scenes)
g = runpy.run_path(os.path.join(scenes, "02-cornell-lume.py"))
s = g["s"]
doc = s.doc
emissive = {k for k, m in doc["materials"].items() if m.get("type") == "emissive"}
n = 0
for o in doc["objects"]:
    if o.get("material") in emissive:
        o["nee"] = False
        n += 1
assert n, "no emissive objects found"
s.run(out=out, argv=["--size", "1920x1080", "--spp", "64",
                     "--no-denoise", "--quiet"], base_dir=scenes)
PY
[ -s "$CMP/nee-off.png" ] || fail "empty nee-off"
CMP_STEMS+=("nee-off")

# 8. rough dielectric: the frosted-veil screens at their ladder roughness
#    vs every pane forced smooth (delta glass)
cr frosted-glass-on "$ROOT/scenes/13-frosted-veil.py" --spp 512 --no-denoise
echo "== compare/frosted-glass-off (variant) =="
python3 - "$ROOT" "$CMP/frosted-glass-off.png" <<'PY'
import os, runpy, sys
root, out = sys.argv[1:]
scenes = os.path.join(root, "scenes")
sys.path.insert(0, scenes)
g = runpy.run_path(os.path.join(scenes, "13-frosted-veil.py"))
s = g["s"]
n = 0
for name, m in s.doc["materials"].items():
    if name.startswith("frost") and m.get("type") == "dielectric":
        m["roughness"] = 0.0
        n += 1
assert n == 5, "expected the five ladder panes"
s.run(out=out, argv=["--size", "1920x1080", "--spp", "512",
                     "--no-denoise", "--quiet"], base_dir=scenes)
PY
[ -s "$CMP/frosted-glass-off.png" ] || fail "empty frosted-glass-off"
CMP_STEMS+=("frosted-glass-off")
fi

echo "== generating docs/GALLERY.md =="
python3 - "$GALLERY" "$ROOT/docs/GALLERY.md" <<'PY'
import json, sys, datetime, os

gallery, out_md = sys.argv[1:]

# Display catalog (fixed order). The generator reads whatever renders exist
# in out/gallery — entries whose PNG is missing are skipped with a warning,
# so incremental SECTIONS runs keep previously rendered catalog entries.
HERO = "15-assembly-hall"
CATALOG = ["01-marble-run", "02-cornell-lume", "03-spot-atrium",
           "04-parabolica", "05-spot-swarm", "06-spot-cascade",
           "06-spot-cascade-settled", "07-campfire", "08-lakeside",
           "09-ember-shore", "09-ember-shore-spp16-denoised",
           "09-ember-shore-spp16-raw", "10-suncatcher", "11-glasswork",
           "12-molten-oracle", "13-frosted-veil", "14-toy-factory"]

# (key, title, blurb, footnote) — key maps to compare/KEY-{on,off}.png
COMPARE = [
    ("transparent-shadows", "透明阴影",
     "开：阴影线沿直线穿过玻璃，逐界面菲涅尔 × Beer–Lambert 衰减——有色"
     "玻璃投下透明彩影；关：布尔遮挡，玻璃投实心黑影。", "11 号场景 · 512 spp"),
    ("flame-shadow", "火焰体积阴影",
     "开：阴影线按火焰透射率衰减，黑烟柱在神光下投出体积阴影；关：阴影线"
     "无视参与介质，烟柱下的地面光斑同亮（--opaque-shadows 同时关闭两类"
     "透射阴影，此构图的可见差异来自烟柱）。", "12 号场景 · 512 spp"),
    ("env-importance", "环境光重要性采样",
     "同 64 spp：开——按亮度 × sinθ 的 2D CDF 直接命中小而炽烈的太阳，"
     "硬影干净；关——均匀球面采样几乎永远打不中太阳，直射日光沦为噪声。",
     "10 号场景 · 64 spp"),
    ("ai-denoise", "OptiX AI 降噪",
     "同 16 spp：体积火焰 + 波纹水面的重噪声被一次网络推理抹平"
     "（HDR + albedo/normal 引导层）。", "09 号场景 · 16 spp"),
    ("mesh-light", "网格 NEE 灯",
     "同 256 spp 的深夜变体（撤去太阳与补光，Sparky 的发光屏是唯一光源）："
     "开——发光网格按三角形面积 CDF 被 NEE 主动采样，屏光照明干净；"
     "关——同样的发光网格只能被 BSDF 路径偶然撞中，照明塌暗、噪声爆炸。",
     "03 号场景深夜变体 · 256 spp"),
    ("aces-tonemap", "ACES 色调映射",
     "开：高光沿肩部渐进滚降，火心保住层次与色相；关：线性截断，火心"
     "撞墙成死白色块。", "07 号场景 · 512 spp"),
    ("nee", "下一事件估计（NEE）",
     "同 64 spp：开——每次弹跳主动向光源连线；关——只靠 BSDF 路径撞灯，"
     "小光源下噪声爆炸。", "02 号场景 · 64 spp"),
    ("frosted-glass", "粗糙电介质（磨砂玻璃）",
     "开：五扇屏按粗糙度阶梯 0 → 0.6，屏后火苗逐扇糊成光晕；关：五扇"
     "全部强制光滑，火苗扇扇清晰。", "13 号场景 · 512 spp"),
]

DESC = {
    "01-marble-run":
        "晨光弹珠乐园：一串彩色弹珠沿弹跳弧线定格——落下、触地、穿过红绿"
        "拱门、落进金色抛物面碗；糖果色朗伯球、粗糙度阶梯金属球与玻璃弹珠"
        "同场——纯 quadric（零三角形），五种解析图元全部到场。",
    "02-cornell-lume":
        "Cornell 盒变体：暖色小面积主灯加冷色低强度月光球，四档粗糙度钢球，"
        "NEE+MIS 在小光源下的收敛能力一目了然。",
    "03-spot-atrium":
        "机器人 Sparky 与三只 Spot 奶牛同台：一个 OBJ 的十个 usemtl 材质组"
        "拆成同变换子网格——玻璃头罩、发光像素屏（纹理化网格区域光，"
        "经 NEE 采样照亮周遭）、金属关节、塑料壳；"
        "奶牛照旧演示硬件三角形、UV 纹理与平滑法线。",

    "04-parabolica":
        "夜景抛物面聚光：金色抛物碟（背面材质成像）把发光灯珠聚成一道光束"
        "扫过暗色地面，展示 parabola 自定义求交与双面材质语义。",
    "05-spot-swarm":
        "32768 个实例化 Spot 卡通奶牛的阵列（约 1.9 亿等效三角形）——同一份三角形 GAS 通过 IAS 实例复用，"
        "展示单层实例化的规模能力。",
    "06-spot-cascade":
        "512 只 Spot 倾泻到第 1.0 秒的锐利定格：场景只声明初始位姿与速度，"
        "加载时由 NVIDIA PhysX GPU 刚体模拟（eENABLE_GPU_DYNAMICS）推进到指定瞬间"
        "（--physics-time）烘焙渲染——下层已开始堆积，上方牛雨仍在翻滚下落，"
        "墙外有被弹飞的散兵。",
    "06-spot-cascade-settled":
        "同一份初始条件模拟到全体休眠的静止堆（对照）：不同时刻、同一物理，"
        "堆叠形态完全出自模拟。",
    "07-campfire":
        "篝火夜景：火焰是程序化的发射型参与介质（发射+吸收，raygen 内解析"
        "圆柱界定后光线行进积分），也是全场唯一主光源——照明由火焰内嵌的暖色"
        "软阴影点光经 NEE 完成。五只 Spot 围坐，微弱月光勾勒轮廓。",
    "08-lakeside":
        "黄昏湖畔：water 材质三件套——ior 1.33 电介质界面、fbm 波纹法线"
        "（倒影破碎与落日波光）、Beer–Lambert 水体吸收（深水偏蓝绿）。"
        "岸边奶牛的倒影被缓涌揉碎，太阳波光路径直铺到镜头前。",
    "09-ember-shore":
        "余烬湖岸：夜色水边的篝火——体积火焰的光经波纹水面反射，火光倒影"
        "在浪里揉碎；火焰、水面与软阴影同框，是低采样噪声最重的场景，"
        "也因此是 AI 降噪的对比载体。",
    "09-ember-shore-spp16-denoised":
        "同一场景仅 16 spp + OptiX AI 降噪（albedo/normal 引导）——体积火焰"
        "与水面反射的重噪声被一次网络推理抹平。",
    "09-ember-shore-spp16-raw":
        "对照组：同样 16 spp、不降噪的原始蒙特卡洛噪点。",
    "10-suncatcher":
        "晴空捕日：全场零显式灯，照明百分之百来自一张 4k HDR 晴日天空"
        "（Poly Haven，CC0）。五只粗糙度渐变的金属球列成弧线（镜面端收进"
        "流云与金牛，粗糙端把天空糊成高光），中央玻璃球把整片天空倒扣进"
        "球心；按亮度×sinθ 预构建 2D CDF 的环境光重要性采样让 NEE 直接"
        "命中小而炽烈的太阳——草地上的长影与糖果弹珠的软天光同源一张图。",
    "11-glasswork":
        "琉璃静物：玻璃球里嵌着水球、水球里悬着气泡——三层嵌套介质由"
        "介质栈与相对折射率逐界面算对，藏在球后的奶牛经两重界面折射，"
        "倒过来又正回去，最终立在气泡里。三颗有色玻璃珠（Beer–Lambert "
        "吸收）在桌面投下玫瑰、金、青三色的透明亮影——阴影线不再把玻璃"
        "当不透明，而是沿直线累积菲涅尔与介质衰减。",
    "12-molten-oracle":
        "封面场景「熔岩圣殿的机械先知」：机械奶牛在祭坛烈焰上被无形之力"
        "击碎，PhysX GPU 在 0.20 秒定格 49 个刚体的爆裂瞬间——金铜齿轮"
        "（极坐标 alpha 镂空圆盘）悬浮其间；破晓阳光越过后墙从穹顶破口"
        "斜射而入（envmap 重要性采样），在地面拉出被碎片凿碎的光斑长影，"
        "与祭坛双火焰的地狱暖光冷暖对切；纯吸收的黑烟柱（零发射体积）在"
        "天窗前升腾、在神光下投出体积阴影，石壁符文（纹理化发光体）泛着"
        "幽蓝，右侧下沉水池从苔绿清浅坠入幽蓝——除磨砂玻璃外一图汇演全书机制。",

    "13-frosted-veil":
        "霜幕屏风：五扇磨砂玻璃屏一字排开，粗糙度自左向右 0 → 0.6 逐扇"
        "递增，每扇背后同位各燃一小簇体积火焰——镜面端火苗清晰如无物，"
        "磨砂端只剩一团暖色光晕，GGX 微表面透射（VNDF 采样 + Walter "
        "BTDF）把“看得见”连续糊成“只看得见光”；最清一扇后立着奶牛，"
        "火光勾出暖色轮廓。屏前地面的火光光斑却逐扇一样锐利——阴影线仍"
        "按光滑菲涅尔直线透射，这笔近似的账记在报告第 16 章。",

    "14-toy-factory":
        "玩具工厂：五只同模具糖果色 Sparky 玩具在质检台一字排开，塑料壳"
        "（新材质：漫反射底 + GGX 电介质涂层的双瓣混合）的涂层粗糙度自左"
        "向右 0.03 → 0.6 逐只递增——相机正后方的细长灯管在清漆端映出一条"
        "锐利亮条，逐只糊成宽晕直至摊平成哑光，除涂层参数外一切恒定。中间"
        "那只（出厂默认 0.15）被质检唤醒，纹理发光屏做网格 NEE 灯；其余四"
        "只睡眠，熄灭的深色高光泽屏幕同样是塑料。金属关节、玻璃头罩与胶质"
        "履带混搭成料；针孔相机成图（光圈盘采样的结构性伪影就此规避）。",

    "15-assembly-hall":
        "玩具工厂总装大厅：一帧收纳渲染器的全部能力。正午阳光从天窗涌进"
        "大厅（HDR 环境光重要性采样），在混凝土地面铺出亮斑；熔炉的纯吸收"
        "烟柱升入光束，在光斑里投下体积阴影（阴影线的火焰透射率）。熔炉角"
        "燃着两簇发射型体积火焰，其中一簇只透过磨砂玻璃隔间显形为一团暖晕"
        "（GGX 微表面透射）；传送带上糖果色塑料 Sparky 列队驶过（涂层双瓣"
        "混合 BSDF），被质检唤醒的那只亮起纹理网格屏（NEE 网格灯），黄色"
        "胶囊吉祥物在带边督工（塑料 + 发光天线顶灯），线尾一只 UV 纹理"
        "Spot 等待装箱；右前方 PhysX GPU 把一箱玩具奶牛定格在倾泻半空"
        "（--physics-time），左前的水冷池用 fbm 波纹与 Beer–Lambert 吸收"
        "倒映整座大厅。金属桁架横贯屋顶，齿轮标志以 alpha 镂空圆盘悬挂——"
        "spot、sparky、capsule_mascot 三份网格资产同框。",
}

lines = [
    "# sundog 画廊",
    "",
    f"由 `scripts/render-gallery.sh` 生成于 {datetime.date.today().isoformat()}。",
    "正式图入库于 `docs/gallery/`（无损重压缩 PNG，旗舰 2K、其余 1080p）；"
    "渲染原件在 `out/gallery/`（不入库）。",
    "",
]

def exists(rel):
    return os.path.exists(os.path.join(gallery, rel))

# ---- class 1: the flagship demo ----
if exists(HERO + ".png"):
    lines += [
        "## 旗舰演示",
        "",
        f"![{HERO}](gallery/{HERO}.png)",
        "",
        DESC.get(HERO, ""),
        "",
    ]

# ---- class 2: feature ON/OFF pairs ----
have_cmp = [c for c in COMPARE if exists(f"compare/{c[0]}-on.png")
            and exists(f"compare/{c[0]}-off.png")]
if have_cmp:
    lines += [
        "## 特性对比",
        "",
        "同一场景、同一机位，一个开关之差——左列开、右列关。",
        "",
    ]
    for key, title, blurb, note in have_cmp:
        lines += [
            f"### {title}",
            "",
            "| 开 | 关 |",
            "|:---:|:---:|",
            f"| ![{key}-on](gallery/compare/{key}-on.png) "
            f"| ![{key}-off](gallery/compare/{key}-off.png) |",
            "",
            f"{blurb}（{note}）",
            "",
        ]

# ---- full scene catalog + stats ----
lines += ["## 全部场景", ""]
rows = []
for stem in [*CATALOG, HERO]:
    if not exists(stem + ".png"):
        print(f"WARNING: {stem}.png missing from out/gallery — skipped")
        continue
    if stem != HERO:  # the hero already opens the page
        lines += [
            f"### {stem}",
            "",
            f"![{stem}](gallery/{stem}.png)",
            "",
            DESC.get(stem, ""),
            "",
        ]
    sp = os.path.join(gallery, f"{stem}.stats.json")
    if not os.path.exists(sp):
        continue
    st = json.load(open(sp))
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
lines += [
    "",
    "> 口径注：15-assembly-hall 与 compare/ 对比图当前由 GB200"
    "（DEVARCH=compute_100）渲染，统计数字不与上表其余行（RTX 5090 + "
    "驱动 615.36 口径）比较；待分配到 5090 测试机后统一重渲复核。",
    "",
]

with open(out_md, "w") as f:
    f.write("\n".join(lines))
print("wrote", out_md)
PY

# Sync the finals into the repo (docs/gallery) so they render on GitHub.
# Only the stems rendered THIS run are synced (out/gallery may hold stale
# files from renamed scenes). Losslessly recompress via PIL when available
# (stb PNGs are ~40% larger); fall back to a plain copy.
mkdir -p "$ROOT/docs/gallery"
SYNC=("${RENDERED[@]}")
for stem in "${CMP_STEMS[@]:-}"; do
  [ -n "$stem" ] && SYNC+=("compare/$stem")
done
if [ "${#SYNC[@]}" -gt 0 ] && python3 -c 'import PIL' 2>/dev/null; then
  python3 - "$GALLERY" "$ROOT/docs/gallery" "${SYNC[@]}" << 'PY'
import os, sys
from PIL import Image
src, dst, *stems = sys.argv[1:]
for stem in stems:
    p = os.path.join(src, stem + ".png")
    out = os.path.join(dst, stem + ".png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    Image.open(p).convert("RGB").save(out, "PNG", optimize=True)
    print(f"optimized {stem}.png: {os.path.getsize(p)//1024} KB -> {os.path.getsize(out)//1024} KB")
PY
else
  for stem in "${SYNC[@]}"; do
    mkdir -p "$(dirname "$ROOT/docs/gallery/$stem")"
    cp -v "$GALLERY/$stem.png" "$ROOT/docs/gallery/$stem.png"
  done
fi

echo "render-gallery OK (${#SYNC[@]} images synced to docs/gallery)"
