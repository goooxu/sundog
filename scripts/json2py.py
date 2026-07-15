#!/usr/bin/env python3
"""Emit a scenelib-based .py first draft for a handwritten scene JSON.

The draft is constructively parity-exact: every literal is reproduced via
repr() of the parsed value, so `json.loads(py --emit-json) == json.load(json)`
holds before any human polish. Polish must stay literal-moving only.

Temporary migration tool; deleted together with the scene JSONs.

Usage: python3 scripts/json2py.py scenes/07-campfire.json > scenes/07-campfire.py
"""

import json
import os
import sys

STEP_FNS = {"scale": "scale", "rotate_x": "rotate_x", "rotate_y": "rotate_y",
            "rotate_z": "rotate_z", "translate": "translate"}


def lit(v):
    """A Python literal that round-trips to the same JSON value."""
    if isinstance(v, bool):
        return "True" if v else "False"
    if v is None:
        return "None"
    return repr(v)


def kwargs(d, keys):
    return ", ".join("%s=%s" % (k, lit(d[k])) for k in keys if k in d)


def step_call(st):
    (k, v), = st.items()
    if k in ("scale", "translate") and isinstance(v, list):
        return "%s(%s)" % (STEP_FNS[k], ", ".join(lit(c) for c in v))
    return "%s(%s)" % (STEP_FNS[k], lit(v))


def main():
    path = sys.argv[1]
    stem = os.path.basename(path)[:-5]
    with open(path) as f:
        doc = json.load(f)

    out = []
    w = out.append
    w("#!/usr/bin/env python3")
    w('"""sundog scene %s (auto-draft from %s.json — polish me)."""' % (stem, stem))
    w("")
    w("from scenelib import (Scene, rigid_body, rotate_x, rotate_y, rotate_z,")
    w("                      scale, static_body, translate)")
    w("")
    w("s = Scene()")

    r = doc.get("render", {})
    if r:
        w("s.render(%s)" % kwargs(r, ["width", "height", "spp", "max_depth",
                                      "clamp", "seed", "gamma", "exposure",
                                      "tonemap", "transparent_shadows"]))
    c = doc["camera"]
    w("s.camera(%s)" % kwargs(c, ["lookfrom", "lookat", "up", "vfov",
                                  "aperture", "focus_dist"]))
    b = doc.get("background")
    if b:
        t = b.get("type", "solid")
        if t == "solid":
            w("s.background_solid(color=%s)" % lit(b["color"]))
        elif t == "gradient":
            w("s.background_gradient(horizon=%s, zenith=%s)"
              % (lit(b["horizon"]), lit(b["zenith"])))
        else:
            w("s.background_envmap(%s%s)" % (lit(b["file"]),
              "".join(", %s=%s" % (k, lit(b[k]))
                      for k in ("rotate", "intensity", "importance") if k in b)))
    p = doc.get("physics")
    if p:
        w("s.physics(%s)" % kwargs(p, ["gravity", "timestep", "max_time",
                                       "friction", "restitution",
                                       "solver_iterations", "sleep_threshold",
                                       "stop_time"]))
    w("")
    for name, t in doc.get("textures", {}).items():
        ty = t.get("type", "solid")
        rest = {k: v for k, v in t.items() if k != "type"}
        w("s.texture(%s, %s%s)" % (lit(name), lit(ty),
          "".join(", %s=%s" % (k, lit(v)) for k, v in rest.items())))
    for name, m in doc.get("meshes", {}).items():
        w("s.mesh(%s, %s%s)" % (lit(name), lit(m["obj"]),
          ", normals=%s" % lit(m["normals"]) if "normals" in m else ""))
    w("")
    for name, m in doc.get("materials", {}).items():
        ty = m["type"]
        rest = {k: v for k, v in m.items() if k != "type"}
        args = "".join(", %s=%s" % (k, lit(v)) for k, v in rest.items())
        if ty in ("lambert", "metal", "dielectric", "emissive", "water"):
            w("s.%s(%s%s)" % (ty, lit(name), args))
        else:
            w("s.material(%s, %s%s)" % (lit(name), lit(ty), args))
    w("")
    for l in doc.get("lights", []):
        if l["type"] == "point":
            w("s.point_light(position=%s, intensity=%s%s)"
              % (lit(l["position"]), lit(l["intensity"]),
                 ", radius=%s" % lit(l["radius"]) if "radius" in l else ""))
        else:
            w("s.distant_light(direction=%s, radiance=%s)"
              % (lit(l["direction"]), lit(l["radiance"])))
    for fl in doc.get("flames", []):
        w("s.flame(%s)" % kwargs(fl, ["base", "height", "radius", "intensity",
                                      "sigma", "noise_scale", "seed",
                                      "light_intensity"]))
    w("")
    for o in doc["objects"]:
        args = [lit(o["shape"]), lit(o["material"])]
        args += [step_call(st) for st in o.get("transform") or []]
        for k in ("material_back", "cutout", "nee"):
            if k in o:
                args.append("%s=%s" % (k, lit(o[k])))
        if "physics" in o:
            ph = dict(o["physics"])
            if ph.pop("dynamic", False):
                args.append("physics=rigid_body(%s)" % kwargs(
                    ph, ["density", "velocity", "angular_velocity",
                         "friction", "restitution"]))
            else:
                args.append("physics=static_body(%s)" % kwargs(
                    ph, ["thickness", "friction", "restitution"]))
        w("s.add(%s)" % ", ".join(args))
    w("")
    w('if __name__ == "__main__":')
    w('    s.run(out="%s.png")' % stem)
    w("")
    sys.stdout.write("\n".join(out))


if __name__ == "__main__":
    main()
