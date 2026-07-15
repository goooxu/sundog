#!/usr/bin/env python3
"""Unit tests for scenes/scenelib.py (no GPU; run: python3 tests/test_scenelib.py).

Covers the emit skeleton, the OMIT pass-through contract, transform
constructors, the loader-mirroring validation, byte determinism, and the
run() entry point (--emit-json / backend discovery / arg forwarding, using a
fake backend script).
"""

import json
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


# ---- emit skeleton and OMIT pass-through ------------------------------------

s = minimal()
doc = json.loads(s.to_json())
check(sorted(doc.keys()) == ["camera", "lights", "materials", "objects",
                             "render", "textures"],
      "skeleton keys: got %s" % sorted(doc.keys()))
check(doc["render"] == {} and doc["textures"] == {} and doc["lights"] == [],
      "empty blocks stay empty")
check(doc["camera"] == {"lookfrom": [0, 1, 5], "lookat": [0, 0, 0]},
      "camera OMIT fields absent")
check(doc["objects"][0] == {"shape": "sphere", "material": "grey"},
      "object without steps has no transform key")
check(s.to_json().endswith("\n") and "\n" not in s.to_json()[:-1],
      "single-line compact JSON with trailing newline")

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
doc = json.loads(s.to_json())
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
doc = json.loads(s.to_json())
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

expect_error(lambda: Scene().to_json(), "no camera", "camera required")

s = minimal()
s.add("sphere", "nope")
expect_error(s.to_json, "unknown material", "unknown material ref")

s = minimal()
s.add("rect", None, material_back=None)
expect_error(s.to_json, "either face", "double null faces")

s = minimal()
s.add("rect", "grey", cutout="holes")
expect_error(s.to_json, "unknown texture", "unknown cutout texture")

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
expect_error(s.to_json, "textured emissive sphere", "textured emissive sphere")

s = minimal()
s.emissive("glow", color=[1, 1, 1])
s.add("disk", "glow", scale(2, 1, 3))
expect_error(s.to_json, "uniform XZ", "disk light non-uniform XZ")

s = minimal()
s.emissive("glow", color=[1, 1, 1])
s.add("sphere", "glow", scale(1, 2, 1))
expect_error(s.to_json, "uniform scale", "sphere light non-uniform")

s = minimal()  # nee=False silences the light constraints
s.emissive("glow", color=[1, 1, 1])
s.add("disk", "glow", scale(2, 1, 3), nee=False)
json.loads(s.to_json())

s = minimal()
s.add("sphere", "grey", physics=rigid_body())
expect_error(s.to_json, "no physics block", "object physics without block")

s = minimal()
s.physics(stop_time=0.5)
s.add("rect", "grey", physics=rigid_body())
expect_error(s.to_json, "static-only", "dynamic rect")

s = minimal()
s.physics(stop_time=0.5)
s.add("disk", "grey", physics=static_body())
expect_error(s.to_json, "collider", "disk collider")

s = minimal()
s.physics(stop_time=0.5)
s.add("sphere", "grey", physics=static_body(thickness=0.5))
expect_error(s.to_json, "thickness", "thickness on non-rect")

s = minimal()
s.physics(stop_time=0.5)
s.emissive("glow", color=[1, 1, 1])
s.add("sphere", "glow", physics=rigid_body(density=100))
expect_error(s.to_json, "dynamic body", "dynamic NEE light")

# error message carries the registration site (this file)
s = minimal()
s.add("sphere", "missing")
try:
    s.to_json()
    check(False, "expected SceneError")
except SceneError as e:
    check("test_scenelib.py:" in str(e), "call site in error: %r" % str(e))

# ---- determinism -------------------------------------------------------------

def build_twice():
    outs = []
    for _ in range(2):
        s = minimal()
        s.render(width=256, height=256, spp=8, seed=7)
        s.texture("g", "grid", a=[0.2, 0.2, 0.2], b=[0.1, 0.1, 0.1])
        s.lambert("floor", texture="g")
        s.add("rect", "floor", scale(5), translate(0, 0, 0))
        outs.append(s.to_json())
    return outs

a, b = build_twice()
check(a == b, "same construction => identical bytes")

# ---- run(): --emit-json, backend discovery, arg forwarding -------------------

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
env.pop("SUNDOG_BUILD", None)

# --emit-json -
out = subprocess.check_output([sys.executable, "-B", scene_py,
                               "--emit-json", "-"], env=env)
doc = json.loads(out.decode())
check(doc["render"]["width"] == 64 and doc["objects"][0]["shape"] == "sphere",
      "--emit-json - prints the scene JSON")
out2 = subprocess.check_output([sys.executable, "-B", scene_py,
                                "--emit-json", "-"], env=env)
check(out == out2, "--emit-json is byte-deterministic across runs")

# --emit-json PATH
jpath = os.path.join(TMP, "ir.json")
subprocess.check_call([sys.executable, "-B", scene_py, "--emit-json", jpath],
                      env=env)
with open(jpath) as f:
    check(json.load(f) == doc, "--emit-json PATH writes the same JSON")

# backend missing => exit 1 with a helpful message
env_bad = dict(env)
env_bad["SUNDOG_BUILD"] = os.path.join(TMP, "nonexistent")
r = subprocess.Popen([sys.executable, "-B", scene_py], env=env_bad,
                     stderr=subprocess.PIPE)
_, err = r.communicate()
check(r.returncode == 1 and b"backend not found" in err,
      "missing backend reported (rc=%d, err=%r)" % (r.returncode, err[:120]))

# fake backend: check --scene tmp json exists at call time, forwarding, --out
fake_build = os.path.join(TMP, "build")
os.makedirs(fake_build)
fake = os.path.join(fake_build, "sundog")
arglog = os.path.join(TMP, "args.txt")
with open(fake, "w") as f:
    f.write("#!/bin/sh\n"
            "printf '%s\\n' \"$@\" > " + arglog + "\n"
            "cp \"$2\" " + os.path.join(TMP, "seen.json") + " 2>/dev/null\n"
            "exit 0\n")
os.chmod(fake, 0o755)
env_fake = dict(env)
env_fake["SUNDOG_BUILD"] = fake_build

r = subprocess.Popen([sys.executable, "-B", scene_py,
                      "--spp", "16", "--quiet"], env=env_fake)
r.communicate()
check(r.returncode == 0, "fake backend run exits 0")
with open(arglog) as f:
    args = f.read().split("\n")
check(args[0] == "--scene" and args[1].endswith(".json"),
      "backend gets --scene tmp.json")
check("--spp" in args and "16" in args and "--quiet" in args,
      "extra args forwarded verbatim")
check(args[-2:] == ["--out", "mini.png"] or ["--out", "mini.png"] == args[-3:-1],
      "API out appended when --out not given: %s" % args)
with open(os.path.join(TMP, "seen.json")) as f:
    check(json.load(f) == doc, "backend saw the same JSON IR")
check(not [p for p in os.listdir(TMP) if p.startswith(".ir-")],
      "temp IR json cleaned up")

# user --out wins over API out
r = subprocess.Popen([sys.executable, "-B", scene_py,
                      "--out", "custom.png"], env=env_fake)
r.communicate()
with open(arglog) as f:
    args = f.read().split("\n")
check(args.count("--out") == 1 and "custom.png" in args
      and "mini.png" not in args, "user --out overrides API out")

import shutil  # noqa: E402
shutil.rmtree(TMP)

print("test_scenelib OK (%d checks)" % _checks[0])
