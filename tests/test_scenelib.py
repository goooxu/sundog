#!/usr/bin/env python3
"""Unit tests for scenes/scenelib.py (no GPU, no backend library; run:
python3 tests/test_scenelib.py).

Covers the construction surface (doc skeleton, OMIT pass-through, transform
constructors), the loader-mirroring validation, determinism of the backend
call program, the CLI argument layer, the dict -> C-ABI-program marshalling
(_program), and the backend-missing error path. The live ABI itself is
exercised on the test box by run-smoke/run-golden (real renders through
libsundog.so).
"""

import math
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENES = os.path.join(ROOT, "scenes")
sys.path.insert(0, SCENES)

import scenelib  # noqa: E402
from scenelib import (OMIT, Scene, SceneError, rigid_body, rotate_x,  # noqa: E402
                      rotate_y, rotate_z, scale, static_body, translate)

_checks = [0]


def check(cond, msg):
    _checks[0] += 1
    if not cond:
        sys.stderr.write("FAIL: %s\n" % msg)
        sys.exit(1)


def expect_error(fn, needle, msg):
    try:
        fn()
    except (SceneError, TypeError) as e:
        check(needle in str(e),
              "%s: error %r does not mention %r" % (msg, str(e), needle))
        return
    sys.stderr.write("FAIL: %s: expected SceneError, none raised\n" % msg)
    sys.exit(1)


def minimal():
    s = Scene()
    s.camera(lookfrom=[0, 1, 5], lookat=[0, 0, 0])
    s.lambert("grey", color=[0.5, 0.5, 0.5])
    s.add("sphere", "grey")
    return s


# ---- doc skeleton and OMIT pass-through --------------------------------------

s = minimal()
s.validate()
doc = s.doc
check(sorted(doc.keys()) == ["camera", "lights", "materials", "objects",
                             "render", "textures"],
      "skeleton keys: got %s" % sorted(doc.keys()))
check(doc["render"] == {} and doc["textures"] == {} and doc["lights"] == [],
      "empty blocks stay empty")
check(doc["camera"] == {"lookfrom": [0, 1, 5], "lookat": [0, 0, 0]},
      "camera OMIT fields absent")
check(doc["objects"][0] == {"shape": "sphere", "material": "grey"},
      "object without steps has no transform key")

s = minimal()
s.render(spp=64, tonemap="clamp")
s.background_envmap("sky.hdr", rotate=90)
s.mesh("cow", "../assets/spot.obj", normals="smooth")
s.add("mesh:cow", "grey", scale(1.7), rotate_y(205), translate(0, 2.9, -3),
      nee=False)
s.flame(base=[0, 1, 0], height=2.0, radius=0.5, light_intensity=0)
s.physics(stop_time=0.2)
s.add("rect", "grey", physics=static_body(thickness=0.5))
s.add("sphere", "grey", physics=rigid_body(density=400, velocity=[1, 0, 0]))
s.validate()
doc = s.doc
check(doc["render"] == {"spp": 64, "tonemap": "clamp"}, "render partial emit")
check(doc["background"] == {"type": "envmap", "file": "sky.hdr", "rotate": 90},
      "background envmap emit")
check(doc["meshes"] == {"cow": {"obj": "../assets/spot.obj",
                                "normals": "smooth"}}, "meshes block emit")
check(doc["objects"][1]["transform"] == [{"scale": 1.7}, {"rotate_y": 205},
                                         {"translate": [0, 2.9, -3]}],
      "transform steps in call order")
check(doc["objects"][1]["nee"] is False, "nee=False is emitted")
check(doc["flames"] == [{"base": [0, 1, 0], "height": 2.0, "radius": 0.5,
                         "light_intensity": 0}], "flame partial emit")
check(doc["physics"] == {"stop_time": 0.2}, "physics partial emit")
check(doc["objects"][2]["physics"] == {"thickness": 0.5}, "static_body emit")
check(doc["objects"][3]["physics"] == {"dynamic": True, "density": 400,
                                       "velocity": [1, 0, 0]},
      "rigid_body emit")

# material_back three states
s = minimal()
s.metal("gold", color=[1, 0.78, 0.34], roughness=0.18)
s.add("parabola", None, material_back="gold")
s.add("rect", "grey", material_back=None)
s.validate()
doc = s.doc
check(doc["objects"][1]["material"] is None
      and doc["objects"][1]["material_back"] == "gold",
      "front-null back-material object")
check(doc["objects"][2]["material_back"] is None,
      "explicit material_back=None emitted as null")
check("material_back" not in doc["objects"][0], "omitted material_back absent")

# scale forms
check(scale(0.85) == {"scale": 0.85}, "scalar scale stays scalar")
check(scale(1, 2, 3) == {"scale": [1, 2, 3]}, "triple scale")
check(scale([4, 5, 6]) == {"scale": [4, 5, 6]}, "list scale")
check(translate([1, 2, 3]) == translate(1, 2, 3), "translate list == args")
check(rotate_x(90) == {"rotate_x": 90} and rotate_z(-14) == {"rotate_z": -14},
      "rotation steps")

# ---- validation mirrors the loader ------------------------------------------

expect_error(lambda: Scene().validate(), "no camera", "camera required")

s = minimal()
s.add("sphere", "nope")
expect_error(s.validate, "unknown material", "unknown material ref")

s = minimal()
s.add("rect", None, material_back=None)
expect_error(s.validate, "either face", "double null faces")

s = minimal()
s.add("rect", "grey", cutout="holes")
expect_error(s.validate, "unknown texture", "unknown cutout texture")

expect_error(lambda: minimal().add("torus", "grey"), "unknown shape",
             "shape enum")
expect_error(lambda: minimal().add("sphere", "grey", {"scale": 1, "x": 2}),
             "transform steps", "malformed step")
expect_error(lambda: minimal().texture("t", "perlin"), "unknown type",
             "texture type enum")
expect_error(lambda: minimal().texture("t", "solid", colr=[1, 1, 1]),
             "not a field", "texture field typo")
expect_error(lambda: minimal().material("m", "plastic"), "unknown type",
             "material type enum")
expect_error(lambda: minimal().dielectric("g", roughnes=0.1),
             "unexpected keyword", "dielectric kwarg typo is a TypeError")
expect_error(lambda: minimal().metal("m", roughnes=0.1), "unexpected keyword",
             "metal kwarg typo is a TypeError")
expect_error(lambda: minimal().render(tonemap="filmic"), "tonemap",
             "tonemap enum")
expect_error(lambda: minimal().flame(base=[0, 0, 0], height=0, radius=1),
             "must be > 0", "flame height domain")
expect_error(lambda: minimal().physics(solver_iterations=[8]),
             "solver_iterations", "solver_iterations arity")
expect_error(lambda: minimal().background_envmap("a.hdr", intensity=-1),
             "must be >= 0", "envmap intensity domain")

s = minimal()
s.emissive("glow", texture="runes", intensity=2.0)
s.texture("runes", "image", file="runes.png")
s.add("sphere", "glow")
expect_error(s.validate, "textured emissive sphere", "textured emissive sphere")

s = minimal()
s.emissive("glow", color=[1, 1, 1])
s.add("disk", "glow", scale(2, 1, 3))
expect_error(s.validate, "uniform XZ", "disk light non-uniform XZ")

s = minimal()
s.emissive("glow", color=[1, 1, 1])
s.add("sphere", "glow", scale(1, 2, 1))
expect_error(s.validate, "uniform scale", "sphere light non-uniform")

s = minimal()  # nee=False silences the light constraints
s.emissive("glow", color=[1, 1, 1])
s.add("disk", "glow", scale(2, 1, 3), nee=False)
s.validate()

s = minimal()  # an emissive mesh is an NEE light — textured included
s.texture("screen", "image", file="runes.png")
s.emissive("glow", texture="screen", intensity=3.0)
s.add(s.mesh("bot", "bot.obj", usemtl="Screen"), "glow", scale(2, 1, 3))
s.validate()  # no uniform-scale rule for meshes; textured is fine

s = minimal()  # dynamic rigid body + emissive mesh + default nee: rejected
s.physics(stop_time=0.5)
s.emissive("glow", color=[1, 1, 1])
s.add(s.mesh("bot2", "bot.obj"), "glow", physics=rigid_body())
expect_error(s.validate, "dynamic body cannot", "dynamic mesh NEE light")

s = minimal()  # cutout + NEE light: MIS sides would disagree in the holes
s.texture("holes", "image", file="runes.png")
s.emissive("glow", color=[1, 1, 1])
s.add("rect", "glow", cutout="holes")
expect_error(s.validate, "alpha-cutout", "cutout NEE light rejected")

s = minimal()  # nee=False keeps cutout emitters legal (BSDF-only)
s.texture("holes", "image", file="runes.png")
s.emissive("glow", color=[1, 1, 1])
s.add("rect", "glow", cutout="holes", nee=False)
s.validate()

s = minimal()
s.add("sphere", "grey", physics=rigid_body())
expect_error(s.validate, "no physics block", "object physics without block")

s = minimal()
s.physics(stop_time=0.5)
s.add("rect", "grey", physics=rigid_body())
expect_error(s.validate, "static-only", "dynamic rect")

s = minimal()
s.physics(stop_time=0.5)
s.add("disk", "grey", physics=static_body())
expect_error(s.validate, "collider", "disk collider")

s = minimal()
s.physics(stop_time=0.5)
s.add("sphere", "grey", physics=static_body(thickness=0.5))
expect_error(s.validate, "thickness", "thickness on non-rect")

s = minimal()
s.physics(stop_time=0.5)
s.emissive("glow", color=[1, 1, 1])
s.add("sphere", "glow", physics=rigid_body(density=100))
expect_error(s.validate, "dynamic body", "dynamic NEE light")

# error message carries the registration site (this file)
s = minimal()
s.add("sphere", "missing")
try:
    s.validate()
    check(False, "expected SceneError")
except SceneError as e:
    check("test_scenelib.py:" in str(e), "call site in error: %r" % str(e))

# ---- determinism: doc and program are pure functions of the API calls --------

def build_once():
    s = Scene()
    s.render(width=256, height=256, spp=8, seed=7)
    s.camera(lookfrom=[0, 1, 5], lookat=[0, 0, 0])
    s.texture("g", "grid", a=[0.2, 0.2, 0.2], b=[0.1, 0.1, 0.1])
    s.lambert("floor", texture="g")
    s.lambert("ball", color=[0.5, 0.5, 0.5])
    s.add("rect", "floor", scale(5))
    s.add("sphere", "ball", scale(0.7), translate(0, 0.7, 0))
    return s

a, b = build_once(), build_once()
check(a.doc == b.doc, "same construction => equal doc")
check(repr(a._program(".")) == repr(b._program(".")),
      "same construction => identical backend program")

# ---- _program marshalling -----------------------------------------------------

s = build_once()
prog = s._program("scenes")
calls = {c[0]: c for c in prog}
check(prog[0] == ("create", "scenes"), "program starts with create(base_dir)")
check(calls["set_render"][1:5] == (256, 256, 8, -1) and calls["set_render"][6] == 7,
      "set_render ints and seed")
check(math.isnan(calls["set_render"][5]), "clamp OMIT -> NaN")
check(calls["set_render"][9] == -1 and calls["set_render"][10] == -1,
      "tonemap/transparent_shadows OMIT -> -1")
cam = calls["set_camera"]
check(cam[1] == [0.0, 1.0, 5.0] and cam[3] is None, "camera vecs; up OMIT -> None")

# id allocation is by sorted name (matches the old loader's std::map order)
s = Scene()
s.camera(lookfrom=[0, 0, 1], lookat=[0, 0, 0])
s.lambert("zzz", color=[0.1, 0.1, 0.1])
s.lambert("aaa", color=[0.2, 0.2, 0.2])
s.texture("beta", "solid", color=[1, 0, 0])
s.texture("alpha", "solid", color=[0, 1, 0])
s.lambert("mid", texture="beta")
s.add("sphere", "zzz")
s.add("rect", "aaa", material_back="mid")
prog = s._program(".")
objs = [c for c in prog if c[0] == "add_object"]
check(objs[0][3] == 2, "zzz -> id 2 (sorted: aaa,mid,zzz)")
check(objs[1][3] == 0 and objs[1][4] == 1, "aaa -> 0, mid -> 1")
mats = [c for c in prog if c[0].startswith("add_material")]
check(mats[1][2] == 1, "material 'mid' references texture 'beta' as id 1 "
                       "(sorted: alpha=0, beta=1)")

# material_back tri-state / scalar splat / rotation packing / nee
s = minimal()
s.metal("gold", color=[1, 0.78, 0.34])
s.add("parabola", None, material_back="gold")
s.add("rect", "grey", material_back=None, nee=True)
s.add("disk", "grey", scale(0.85), rotate_z(-14), translate(1, 2, 3), nee=False)
prog = s._program(".")
objs = [c for c in prog if c[0] == "add_object"]
check(objs[0][3:5] == (1, -2), "omitted material_back -> -2 sentinel")
check(objs[1][3] == -1 and objs[1][4] == 0,
      "null front -> -1; named back -> id (gold sorts before grey)")
check(objs[2][4] == -1, "explicit None back -> -1")
check(objs[2][7] == 1, "explicit nee=True -> 1")
st = objs[3][6]
check(st[0] == (0, 0.85, 0.85, 0.85), "scalar scale splat to (s,s,s)")
check(st[1] == (4, -14.0, 0.0, 0.0), "rotate_z packs degrees in a")
check(st[2] == (1, 1.0, 2.0, 3.0), "translate packs vector")
check(objs[3][7] == 0, "nee=False -> 0")
check(objs[0][6] == [], "no steps -> empty list")

# physics body packing
s = minimal()
s.physics(stop_time=0.2, solver_iterations=[8, 2])
s.add("sphere", "grey", physics=rigid_body(density=400, velocity=[1, 2, 3]))
s.add("rect", "grey", physics=static_body(thickness=0.5))
prog = s._program(".")
ph = [c for c in prog if c[0] == "set_physics"][0]
check(ph[6] == 8 and ph[7] == 2, "solver_iterations ints")
objs = [c for c in prog if c[0] == "add_object"]
dyn = objs[1][8]
check(dyn[0] == 1 and dyn[1] == 400.0 and dyn[2] == 1
      and dyn[3] == [1.0, 2.0, 3.0] and dyn[4] == 0, "rigid_body packing")
check(math.isnan(dyn[6]) and math.isnan(dyn[8]), "absent friction/thickness NaN")
sta = objs[2][8]
check(sta[0] == 0 and sta[8] == 0.5, "static_body packing")

# mesh usemtl group packing
s = minimal()
s.mesh("head", "../assets/sparky.obj", normals="smooth", usemtl="GlassHead")
s.mesh("whole", "../assets/spot.obj")
s.add("mesh:head", "grey")
prog = s._program(".")
meshes = [c for c in prog if c[0] == "add_mesh"]
check(meshes[0] == ("add_mesh", "../assets/sparky.obj", 1, "GlassHead"),
      "usemtl group packed")
check(meshes[1] == ("add_mesh", "../assets/spot.obj", -1, None),
      "whole-mesh usemtl -> None")

# ---- every committed scene builds, validates, and programs -------------------

import runpy  # noqa: E402

stems = sorted(f for f in os.listdir(SCENES)
               if f.endswith(".py") and f != "scenelib.py")
check(len(stems) >= 15, "expected >= 15 scenes, saw %d" % len(stems))
for f in stems:
    g = runpy.run_path(os.path.join(SCENES, f))
    sc = g["s"]
    sc.validate()
    prog = sc._program(SCENES)
    check(any(c[0] == "add_object" for c in prog), "%s: has objects" % f)
check(True, "all %d scenes validate and program" % len(stems))

# ---- CLI argument layer -------------------------------------------------------

pa = scenelib._parse_args
ns = pa([], "default.png")
check(ns.out == "default.png" and ns.spp == -1 and ns.width == -1
      and ns.seed == -1 and ns.denoise == -1 and ns.tonemap == -1
      and math.isnan(ns.clamp) and not ns.quiet and not ns.probe,
      "defaults map to sentinels")
ns = pa(["--out", "x.png", "--spp", "16", "--size", "640x360", "--seed", "0",
         "--denoise", "--no-denoise", "--tonemap", "clamp", "--quiet"],
        "default.png")
check(ns.out == "x.png" and ns.spp == 16 and (ns.width, ns.height) == (640, 360)
      and ns.seed == 0 and ns.denoise == 0 and ns.tonemap == 1 and ns.quiet,
      "flag parsing; later --no-denoise wins")
ns = pa(["--opaque-shadows", "--physics-time", "1.0"], "d.png")
check(ns.transparent_shadows == 0 and ns.physics_time == 1.0,
      "opaque shadows + physics time")
try:
    pa(["--bogus"], "d.png")
    check(False, "unknown flag must exit")
except SystemExit as e:
    check(e.code == 2, "unknown flag exits 2")

# ---- backend-missing error path ----------------------------------------------

TMP = tempfile.mkdtemp(prefix="scenelib-test-")
SCENE_SRC = """#!/usr/bin/env python3
import sys
sys.path.insert(0, %r)
from scenelib import Scene, scale, translate
s = Scene()
s.render(width=64, height=36, spp=4, seed=7)
s.camera(lookfrom=[0, 1, 5], lookat=[0, 0, 0])
s.background_solid(color=[0, 0, 0])
s.lambert("grey", color=[0.5, 0.5, 0.5])
s.add("sphere", "grey", scale(0.5), translate(0, 0.5, 0))
if __name__ == "__main__":
    s.run(out="mini.png")
""" % SCENES
scene_py = os.path.join(TMP, "mini-scene.py")
with open(scene_py, "w") as f:
    f.write(SCENE_SRC)

env = dict(os.environ)
env["SUNDOG_BUILD"] = os.path.join(TMP, "nonexistent")
r = subprocess.Popen([sys.executable, "-B", scene_py], env=env,
                     stderr=subprocess.PIPE)
_, err = r.communicate()
check(r.returncode == 1 and b"backend not found" in err,
      "missing backend reported (rc=%d, err=%r)" % (r.returncode, err[:120]))

import shutil  # noqa: E402
shutil.rmtree(TMP)

print("test_scenelib OK (%d checks)" % _checks[0])
