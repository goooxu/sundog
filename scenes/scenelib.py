"""sundog scene library — define a scene in Python, run the file, get a PNG.

A sundog scene is an executable Python program living next to this module:

    #!/usr/bin/env python3
    from scenelib import Scene, scale, rotate_y, translate

    s = Scene()
    s.render(width=1920, height=1080, spp=256, seed=7)
    s.camera(lookfrom=[0, 2, 8], lookat=[0, 1, 0], vfov=40)
    s.background_gradient(horizon=[0.8, 0.85, 1.0], zenith=[0.2, 0.35, 0.7])
    red = s.lambert("red", color=[0.75, 0.22, 0.18])
    s.add("sphere", red, scale(0.8), translate(0, 0.8, 0))
    if __name__ == "__main__":
        s.run(out="my-scene.png")

Running the file renders it in-process: scenelib feeds the scene through the
C ABI of the renderer library ($SUNDOG_BUILD/libsundog.so, default
/tmp/sundog-build/libsundog.so) via ctypes — call by call, with no
intermediate representation of any kind — then renders. CLI flags override
the scene's own render settings (--spp/--size/--seed/--denoise/--stats/...;
--out overrides the name passed to run()).

Determinism contract: this module never draws randomness or timestamps; the
backend call program is a pure function of the API calls. Scenes that use
random layout must call random.seed(N) themselves before the first draw. The
`render.seed` setting is the renderer's sampling seed — a separate concern.

Optional arguments left at the OMIT default are not sent across the ABI at
all, so the renderer's own defaults apply (and stay documented in one place).

Stdlib only; compatible with Python 3.6+.
"""

import argparse
import ctypes
import math
import os
import sys


class SceneError(ValueError):
    """Scene validation failed; message carries the offending call site."""


class _Omit(object):
    __slots__ = ()

    def __repr__(self):
        return "OMIT"


OMIT = _Omit()          # sentinel: "do not emit this key"

_SHAPES = ("sphere", "rect", "disk", "cylinder", "parabola")
_TEXTURE_FIELDS = {
    "solid": ("color",),
    "checker": ("a", "b", "scale"),
    "grid": ("a", "b", "scale", "width"),
    "image": ("file", "srgb"),
}
# The renderer reads the common fields for every material type and silently
# ignores the irrelevant ones; the union whitelist below only catches typos.
_MATERIAL_FIELDS = {
    "lambert": ("color", "texture"),
    "metal": ("color", "texture", "roughness"),
    "dielectric": ("ior", "absorb", "color", "texture"),
    "emissive": ("color", "texture", "intensity", "two_sided"),
    "water": ("ior", "absorb", "wave_amp", "wave_freq", "color"),
}
_OBJ_PHYSICS_FIELDS = ("dynamic", "density", "velocity", "angular_velocity",
                       "friction", "restitution", "thickness")


def _site(depth=1):
    """(file, line) of the caller's caller — attached to error messages."""
    f = sys._getframe(depth + 1)
    return (os.path.basename(f.f_code.co_filename), f.f_lineno)


def _fmt_site(site):
    return "%s:%d" % site if site else "?"


def _vec3(v, what, site):
    ok = (isinstance(v, (list, tuple)) and len(v) == 3
          and all(isinstance(c, (int, float)) and not isinstance(c, bool)
                  for c in v))
    if not ok:
        raise SceneError("%s (%s): expected a 3-number list, got %r"
                         % (what, _fmt_site(site), v))
    return list(v)


def _num(v, what, site, lo=None, lo_open=False):
    if not isinstance(v, (int, float)) or isinstance(v, bool):
        raise SceneError("%s (%s): expected a number, got %r"
                         % (what, _fmt_site(site), v))
    if lo is not None and (v < lo or (lo_open and v == lo)):
        raise SceneError("%s (%s): must be %s %s, got %r"
                         % (what, _fmt_site(site),
                            ">" if lo_open else ">=", lo, v))
    return v


def _put(d, key, value):
    if value is not OMIT:
        d[key] = value


# ---- transform step constructors (compose in any order) ---------------------

def scale(x, y=None, z=None):
    """scale(s) -> uniform; scale(x, y, z) or scale([x, y, z]) -> per-axis."""
    if y is None and z is None:
        if isinstance(x, (list, tuple)):
            return {"scale": _vec3(x, "scale", _site())}
        return {"scale": _num(x, "scale", _site())}
    return {"scale": _vec3([x, y, z], "scale", _site())}


def rotate_x(deg):
    return {"rotate_x": _num(deg, "rotate_x", _site())}


def rotate_y(deg):
    return {"rotate_y": _num(deg, "rotate_y", _site())}


def rotate_z(deg):
    return {"rotate_z": _num(deg, "rotate_z", _site())}


def translate(x, y=None, z=None):
    if y is None and z is None:
        return {"translate": _vec3(x, "translate", _site())}
    return {"translate": _vec3([x, y, z], "translate", _site())}


# ---- per-object physics constructors ----------------------------------------

def rigid_body(density=OMIT, velocity=OMIT, angular_velocity=OMIT,
               friction=OMIT, restitution=OMIT):
    p = {"dynamic": True}
    _put(p, "density", density)
    _put(p, "velocity", velocity)
    _put(p, "angular_velocity", angular_velocity)
    _put(p, "friction", friction)
    _put(p, "restitution", restitution)
    return p


def static_body(thickness=OMIT, friction=OMIT, restitution=OMIT):
    p = {}
    _put(p, "thickness", thickness)
    _put(p, "friction", friction)
    _put(p, "restitution", restitution)
    return p


class Scene(object):
    def __init__(self):
        self._render = {}
        self._camera = None
        self._background = None
        self._textures = {}
        self._materials = {}
        self._meshes = {}
        self._objects = []
        self._lights = []
        self._flames = []
        self._physics = None
        self._sites = {}        # ("objects", i) / ("materials", name) / ... -> (file, line)

    # ---- blocks --------------------------------------------------------------

    def render(self, width=OMIT, height=OMIT, spp=OMIT, max_depth=OMIT,
               clamp=OMIT, seed=OMIT, gamma=OMIT, exposure=OMIT,
               tonemap=OMIT, transparent_shadows=OMIT):
        r = self._render
        _put(r, "width", width)
        _put(r, "height", height)
        _put(r, "spp", spp)
        _put(r, "max_depth", max_depth)
        _put(r, "clamp", clamp)
        _put(r, "seed", seed)
        _put(r, "gamma", gamma)
        _put(r, "exposure", exposure)
        _put(r, "tonemap", tonemap)
        _put(r, "transparent_shadows", transparent_shadows)
        if tonemap is not OMIT and tonemap not in ("aces", "clamp"):
            raise SceneError("render (%s): tonemap must be 'aces' or 'clamp'"
                             % _fmt_site(_site()))

    def camera(self, lookfrom, lookat, up=OMIT, vfov=OMIT, aperture=OMIT,
               focus_dist=OMIT):
        site = _site()
        c = {"lookfrom": _vec3(lookfrom, "camera lookfrom", site),
             "lookat": _vec3(lookat, "camera lookat", site)}
        _put(c, "up", up)
        _put(c, "vfov", vfov)
        _put(c, "aperture", aperture)
        _put(c, "focus_dist", focus_dist)
        self._camera = c

    def background_solid(self, color):
        self._background = {"type": "solid",
                            "color": _vec3(color, "background color", _site())}

    def background_gradient(self, horizon, zenith):
        site = _site()
        self._background = {"type": "gradient",
                            "horizon": _vec3(horizon, "background horizon", site),
                            "zenith": _vec3(zenith, "background zenith", site)}

    def background_envmap(self, file, rotate=OMIT, intensity=OMIT,
                          importance=OMIT):
        if intensity is not OMIT:
            _num(intensity, "envmap intensity", _site(), lo=0)
        b = {"type": "envmap", "file": file}
        _put(b, "rotate", rotate)
        _put(b, "intensity", intensity)
        _put(b, "importance", importance)
        self._background = b

    def physics(self, gravity=OMIT, timestep=OMIT, max_time=OMIT,
                friction=OMIT, restitution=OMIT, solver_iterations=OMIT,
                sleep_threshold=OMIT, stop_time=OMIT):
        site = _site()
        if timestep is not OMIT:
            _num(timestep, "physics timestep", site, lo=0, lo_open=True)
        if max_time is not OMIT:
            _num(max_time, "physics max_time", site, lo=0, lo_open=True)
        if stop_time is not OMIT:
            _num(stop_time, "physics stop_time", site, lo=0)
        if solver_iterations is not OMIT:
            if (not isinstance(solver_iterations, (list, tuple))
                    or len(solver_iterations) != 2):
                raise SceneError("physics (%s): solver_iterations must be "
                                 "[posIters, velIters]" % _fmt_site(site))
            solver_iterations = list(solver_iterations)
        p = {}
        _put(p, "gravity", gravity)
        _put(p, "timestep", timestep)
        _put(p, "max_time", max_time)
        _put(p, "friction", friction)
        _put(p, "restitution", restitution)
        _put(p, "solver_iterations", solver_iterations)
        _put(p, "sleep_threshold", sleep_threshold)
        _put(p, "stop_time", stop_time)
        self._physics = p

    # ---- registries ----------------------------------------------------------

    def texture(self, name, type, **fields):
        site = _site()
        if type not in _TEXTURE_FIELDS:
            raise SceneError("texture %r (%s): unknown type %r (one of %s)"
                             % (name, _fmt_site(site), type,
                                "/".join(_TEXTURE_FIELDS)))
        for k in fields:
            if k not in _TEXTURE_FIELDS[type]:
                raise SceneError("texture %r (%s): %r is not a field of %s "
                                 "textures" % (name, _fmt_site(site), k, type))
        t = {"type": type}
        for k, v in fields.items():
            _put(t, k, v)
        self._textures[name] = t
        self._sites[("textures", name)] = site
        return name

    def material(self, name, type, _site_=None, **fields):
        site = _site_ or _site()
        if type not in _MATERIAL_FIELDS:
            raise SceneError("material %r (%s): unknown type %r (one of %s)"
                             % (name, _fmt_site(site), type,
                                "/".join(_MATERIAL_FIELDS)))
        for k in fields:
            if k not in _MATERIAL_FIELDS[type]:
                raise SceneError("material %r (%s): %r is not a field of %s "
                                 "materials" % (name, _fmt_site(site), k, type))
        m = {"type": type}
        for k, v in fields.items():
            _put(m, k, v)
        self._materials[name] = m
        self._sites[("materials", name)] = site
        return name

    def lambert(self, name, color=OMIT, texture=OMIT):
        return self.material(name, "lambert", _site_=_site(),
                             color=color, texture=texture)

    def metal(self, name, color=OMIT, texture=OMIT, roughness=OMIT):
        return self.material(name, "metal", _site_=_site(),
                             color=color, texture=texture, roughness=roughness)

    def dielectric(self, name, ior=OMIT, absorb=OMIT):
        return self.material(name, "dielectric", _site_=_site(),
                             ior=ior, absorb=absorb)

    def emissive(self, name, color=OMIT, texture=OMIT, intensity=OMIT,
                 two_sided=OMIT):
        return self.material(name, "emissive", _site_=_site(), color=color,
                             texture=texture, intensity=intensity,
                             two_sided=two_sided)

    def water(self, name, ior=OMIT, absorb=OMIT, wave_amp=OMIT,
              wave_freq=OMIT):
        return self.material(name, "water", _site_=_site(), ior=ior,
                             absorb=absorb, wave_amp=wave_amp,
                             wave_freq=wave_freq)

    def mesh(self, name, obj, normals=OMIT, usemtl=OMIT):
        """usemtl: load only that material group of the OBJ — multi-material
        models split into per-group sub-meshes (one mesh + add() per group,
        sharing a transform)."""
        m = {"obj": obj}
        _put(m, "normals", normals)
        _put(m, "usemtl", usemtl)
        self._meshes[name] = m
        self._sites[("meshes", name)] = _site()
        return "mesh:" + name

    # ---- objects and lights --------------------------------------------------

    def add(self, shape, material, *steps, **kw):
        site = _site()
        unknown = set(kw) - {"material_back", "cutout", "nee", "physics"}
        if unknown:
            raise SceneError("add (%s): unknown keyword(s) %s"
                             % (_fmt_site(site), ", ".join(sorted(unknown))))
        if not isinstance(shape, str) or (shape not in _SHAPES
                                          and not shape.startswith("mesh:")):
            raise SceneError("add (%s): unknown shape %r (one of %s or "
                             "'mesh:NAME')" % (_fmt_site(site), shape,
                                               "/".join(_SHAPES)))
        for st in steps:
            if not (isinstance(st, dict) and len(st) == 1 and
                    next(iter(st)) in ("scale", "rotate_x", "rotate_y",
                                       "rotate_z", "translate")):
                raise SceneError("add (%s): transform steps must come from "
                                 "scale()/rotate_*()/translate(), got %r"
                                 % (_fmt_site(site), st))
        o = {"shape": shape, "material": material}
        if steps:
            o["transform"] = list(steps)
        _put(o, "material_back", kw.get("material_back", OMIT))
        _put(o, "cutout", kw.get("cutout", OMIT))
        _put(o, "nee", kw.get("nee", OMIT))
        _put(o, "physics", kw.get("physics", OMIT))
        self._sites[("objects", len(self._objects))] = site
        self._objects.append(o)
        return o

    def point_light(self, position, intensity, radius=OMIT):
        site = _site()
        l = {"type": "point",
             "position": _vec3(position, "point light position", site),
             "intensity": _vec3(intensity, "point light intensity", site)}
        _put(l, "radius", radius)
        self._lights.append(l)

    def distant_light(self, direction, radiance):
        site = _site()
        self._lights.append({
            "type": "distant",
            "direction": _vec3(direction, "distant light direction", site),
            "radiance": _vec3(radiance, "distant light radiance", site)})

    def flame(self, base, height, radius, intensity=OMIT, sigma=OMIT,
              noise_scale=OMIT, seed=OMIT, light_intensity=OMIT):
        site = _site()
        f = {"base": _vec3(base, "flame base", site),
             "height": _num(height, "flame height", site, lo=0, lo_open=True),
             "radius": _num(radius, "flame radius", site, lo=0, lo_open=True)}
        if intensity is not OMIT:
            _num(intensity, "flame intensity", site, lo=0)
        if sigma is not OMIT:
            _num(sigma, "flame sigma", site, lo=0)
        if light_intensity is not OMIT:
            _num(light_intensity, "flame light_intensity", site, lo=0)
        _put(f, "intensity", intensity)
        _put(f, "sigma", sigma)
        _put(f, "noise_scale", noise_scale)
        _put(f, "seed", seed)
        _put(f, "light_intensity", light_intensity)
        self._flames.append(f)

    # ---- validation ----------------------------------------------------------

    def _err(self, key, msg):
        raise SceneError("%s (%s): %s"
                         % (self._key_name(key),
                            _fmt_site(self._sites.get(key)), msg))

    @staticmethod
    def _key_name(key):
        return "%s[%r]" % key if isinstance(key, tuple) else str(key)

    @staticmethod
    def _scale_product(obj):
        """Per-axis product of all scale steps (uniformity check)."""
        sx = sy = sz = 1.0
        for st in obj.get("transform", []):
            s = st.get("scale")
            if s is None:
                continue
            if isinstance(s, (int, float)):
                sx, sy, sz = sx * s, sy * s, sz * s
            else:
                sx, sy, sz = sx * s[0], sy * s[1], sz * s[2]
        return sx, sy, sz

    def _validate(self):
        if self._camera is None:
            raise SceneError("scene has no camera — call s.camera(lookfrom=…, "
                             "lookat=…) before emitting")
        if len(self._materials) >= 0xFFFF:
            raise SceneError("too many materials (%d)" % len(self._materials))
        for name, m in self._materials.items():
            tex = m.get("texture")
            if tex is not None and tex not in self._textures:
                self._err(("materials", name),
                          "references unknown texture %r" % tex)
            for k in ("absorb",):
                if k in m and any(c < 0 for c in m[k]):
                    self._err(("materials", name), "%s components must be >= 0" % k)
            for k in ("wave_amp", "wave_freq"):
                if k in m and m[k] < 0:
                    self._err(("materials", name), "%s must be >= 0" % k)

        for i, o in enumerate(self._objects):
            key = ("objects", i)
            shape = o["shape"]
            if shape.startswith("mesh:") and shape[5:] not in self._meshes:
                self._err(key, "references unknown mesh %r" % shape[5:])
            front, back = o["material"], o.get("material_back", OMIT)
            for face, mat in (("material", front), ("material_back", back)):
                if mat is not OMIT and mat is not None \
                        and mat not in self._materials:
                    self._err(key, "%s references unknown material %r"
                              % (face, mat))
            if front is None and back is None:
                self._err(key, "no material on either face (front and back "
                               "are both null)")
            if "cutout" in o and o["cutout"] not in self._textures:
                self._err(key, "cutout references unknown texture %r"
                          % o["cutout"])

            # auto area-light constraints (mirror the loader)
            is_light = (front is not None and front in self._materials
                        and self._materials[front]["type"] == "emissive"
                        and o.get("nee", True) is not False
                        and shape in ("rect", "disk", "sphere"))
            if is_light:
                sx, sy, sz = self._scale_product(o)
                if shape == "sphere" and "texture" in self._materials[front]:
                    self._err(key, "a textured emissive sphere cannot be an "
                                   "NEE light — set nee=False")
                if shape == "sphere" and not (sx == sy == sz):
                    self._err(key, "sphere area light requires uniform scale")
                if shape == "disk" and sx != sz:
                    self._err(key, "disk area light requires uniform XZ scale")

            if "physics" in o:
                phys = o["physics"]
                if not isinstance(phys, dict):
                    self._err(key, "physics must come from rigid_body()/"
                                   "static_body() or be a dict")
                if self._physics is None:
                    self._err(key, "object opts into physics but the scene "
                                   "has no physics block")
                for k in phys:
                    if k not in _OBJ_PHYSICS_FIELDS:
                        self._err(key, "unknown physics field %r" % k)
                if shape in ("disk", "cylinder", "parabola"):
                    self._err(key, "%s cannot be a physics collider (only "
                                   "sphere/rect/mesh)" % shape)
                dynamic = phys.get("dynamic", False)
                if shape == "rect" and dynamic:
                    self._err(key, "rect colliders are static-only")
                if dynamic and phys.get("density", 250.0) <= 0:
                    self._err(key, "dynamic body needs density > 0")
                if "thickness" in phys:
                    if shape != "rect":
                        self._err(key, "thickness only applies to rect "
                                       "colliders")
                    if phys["thickness"] <= 0:
                        self._err(key, "thickness must be > 0")
                if dynamic and is_light:
                    self._err(key, "a dynamic body cannot also be an NEE area "
                                   "light — set nee=False or keep it static")

    # ---- emission ------------------------------------------------------------

    @property
    def doc(self):
        """The underlying scene dict (escape hatch; no validation)."""
        d = {"render": self._render,
             "camera": self._camera if self._camera is not None else {}}
        if self._background is not None:
            d["background"] = self._background
        if self._physics is not None:
            d["physics"] = self._physics
        d["textures"] = self._textures
        d["materials"] = self._materials
        if self._meshes:
            d["meshes"] = self._meshes
        d["objects"] = self._objects
        d["lights"] = self._lights
        if self._flames:
            d["flames"] = self._flames
        return d

    # ---- backend program: ordered call list, ready for the C ABI ----------

    def _program(self, base_dir):
        """Flatten the scene into an ordered list of backend calls (pure).

        Ids for textures/materials/meshes are allocated by sorted name — the
        old JSON loader iterated a std::map, so lexicographic ids are what the
        golden images were rendered with. TODO: after a deliberate golden
        re-bless this can switch to insertion order.
        Optional values stay as None/NaN sentinels; renderer defaults live in
        the C++ side only.
        """
        tex_ids = {n: i for i, n in enumerate(sorted(self._textures))}
        mat_ids = {n: i for i, n in enumerate(sorted(self._materials))}
        mesh_ids = {n: i for i, n in enumerate(sorted(self._meshes))}
        NAN = float("nan")

        def fnum(d, key):
            return float(d[key]) if key in d else NAN

        def fint(d, key):
            return int(d[key]) if key in d else -1

        def ftri(d, key):          # tri-state bool -> -1/0/1
            return -1 if key not in d else (1 if d[key] else 0)

        def fvec(d, key):
            return [float(c) for c in d[key]] if key in d else None

        p = [("create", base_dir)]
        r = self._render
        tm = {"aces": 0, "clamp": 1}.get(r.get("tonemap"), -1)
        p.append(("set_render", fint(r, "width"), fint(r, "height"),
                  fint(r, "spp"), fint(r, "max_depth"), fnum(r, "clamp"),
                  fint(r, "seed"), fnum(r, "gamma"), fnum(r, "exposure"), tm,
                  ftri(r, "transparent_shadows")))
        if self._physics is not None:
            ph = self._physics
            si = ph.get("solver_iterations")
            p.append(("set_physics", fvec(ph, "gravity"), fnum(ph, "timestep"),
                      fnum(ph, "max_time"), fnum(ph, "friction"),
                      fnum(ph, "restitution"),
                      int(si[0]) if si else -1, int(si[1]) if si else -1,
                      fnum(ph, "sleep_threshold"), fnum(ph, "stop_time")))
        c = self._camera
        p.append(("set_camera", fvec(c, "lookfrom"), fvec(c, "lookat"),
                  fvec(c, "up"), fnum(c, "vfov"), fnum(c, "aperture"),
                  fnum(c, "focus_dist")))
        b = self._background
        if b is not None:
            if b["type"] == "solid":
                p.append(("set_background_solid", fvec(b, "color")))
            elif b["type"] == "gradient":
                p.append(("set_background_gradient", fvec(b, "horizon"),
                          fvec(b, "zenith")))
            else:
                p.append(("set_background_envmap", b["file"], fnum(b, "rotate"),
                          fnum(b, "intensity"), ftri(b, "importance")))
        for name in sorted(self._textures):
            tx = self._textures[name]
            kind = tx["type"]
            if kind == "solid":
                p.append(("add_texture_solid", fvec(tx, "color")))
            elif kind == "checker":
                p.append(("add_texture_checker", fvec(tx, "a"), fvec(tx, "b"),
                          [float(v) for v in tx["scale"]] if "scale" in tx else None))
            elif kind == "grid":
                p.append(("add_texture_grid", fvec(tx, "a"), fvec(tx, "b"),
                          [float(v) for v in tx["scale"]] if "scale" in tx else None,
                          fnum(tx, "width")))
            else:
                p.append(("add_texture_image", tx["file"], ftri(tx, "srgb")))
        for name in sorted(self._materials):
            m = self._materials[name]
            kind = m["type"]
            tid = tex_ids[m["texture"]] if "texture" in m else -1
            if kind == "lambert":
                p.append(("add_material_lambert", fvec(m, "color"), tid))
            elif kind == "metal":
                p.append(("add_material_metal", fvec(m, "color"), tid,
                          fnum(m, "roughness")))
            elif kind == "dielectric":
                p.append(("add_material_dielectric", fnum(m, "ior"),
                          fvec(m, "absorb"), fvec(m, "color"), tid))
            elif kind == "emissive":
                p.append(("add_material_emissive", fvec(m, "color"), tid,
                          fnum(m, "intensity"), ftri(m, "two_sided")))
            else:
                p.append(("add_material_water", fnum(m, "ior"), fvec(m, "absorb"),
                          fnum(m, "wave_amp"), fnum(m, "wave_freq"),
                          fvec(m, "color")))
        for name in sorted(self._meshes):
            ms = self._meshes[name]
            smooth = -1 if "normals" not in ms else (1 if ms["normals"] == "smooth" else 0)
            p.append(("add_mesh", ms["obj"], smooth, ms.get("usemtl")))
        GK = {"sphere": 0, "rect": 1, "disk": 2, "cylinder": 3, "parabola": 4}
        for o in self._objects:
            shape = o["shape"]
            if shape.startswith("mesh:"):
                gk, mesh_id = 5, mesh_ids[shape[5:]]
            else:
                gk, mesh_id = GK[shape], -1
            front = -1 if o["material"] is None else mat_ids[o["material"]]
            if "material_back" not in o:
                back = -2                       # omitted -> same as front
            elif o["material_back"] is None:
                back = -1                       # explicit null -> pass-through
            else:
                back = mat_ids[o["material_back"]]
            cutout = tex_ids[o["cutout"]] if "cutout" in o else -1
            steps = []
            for st in o.get("transform", []):
                (k, v), = st.items()
                if k == "scale":
                    if isinstance(v, (int, float)):
                        s = float(v)            # uniform: splat, narrow in C
                        steps.append((0, s, s, s))
                    else:
                        steps.append((0, float(v[0]), float(v[1]), float(v[2])))
                elif k == "translate":
                    steps.append((1, float(v[0]), float(v[1]), float(v[2])))
                else:
                    steps.append(({"rotate_x": 2, "rotate_y": 3,
                                   "rotate_z": 4}[k], float(v), 0.0, 0.0))
            nee = ftri(o, "nee")
            phys = None
            if "physics" in o:
                pb = o["physics"]
                phys = (1 if pb.get("dynamic") else 0, fnum(pb, "density"),
                        1 if "velocity" in pb else 0,
                        fvec(pb, "velocity") or [0.0, 0.0, 0.0],
                        1 if "angular_velocity" in pb else 0,
                        fvec(pb, "angular_velocity") or [0.0, 0.0, 0.0],
                        fnum(pb, "friction"), fnum(pb, "restitution"),
                        fnum(pb, "thickness"))
            p.append(("add_object", gk, mesh_id, front, back, cutout,
                      steps, nee, phys))
        for fl in self._flames:
            p.append(("add_flame", [float(v) for v in fl["base"]],
                      float(fl["height"]), float(fl["radius"]),
                      fnum(fl, "intensity"), fnum(fl, "sigma"),
                      fnum(fl, "noise_scale"), fint(fl, "seed"),
                      fnum(fl, "light_intensity")))
        for l in self._lights:
            if l["type"] == "point":
                p.append(("add_point_light", [float(v) for v in l["position"]],
                          [float(v) for v in l["intensity"]], fnum(l, "radius")))
            else:
                p.append(("add_distant_light", [float(v) for v in l["direction"]],
                          [float(v) for v in l["radiance"]]))
        return p

    def validate(self):
        """Run the full construction-time validation (raises SceneError)."""
        self._validate()

    def run(self, out, argv=None, base_dir=None):
        """Render this scene in-process: called from the scene file's
        __main__ block. Scene data crosses the C ABI of libsundog.so
        directly — no intermediate representation of any kind.

        `out` is the output PNG the scene chooses for itself; CLI flags
        override it and any render setting (--out/--spp/--size/...).
        """
        opts = _parse_args(list(sys.argv[1:] if argv is None else argv), out)
        if opts.probe:
            probe()
            sys.exit(0)
        try:
            self._validate()
        except SceneError as e:
            sys.stderr.write("sundog: fatal: %s\n" % e)
            sys.exit(1)
        if base_dir is None:
            base_dir = os.path.dirname(os.path.abspath(sys.argv[0])) or "."
        lib = _load_backend()
        sys.stdout.flush()
        sys.stderr.flush()
        h = None
        try:
            h = _apply(lib, self._program(base_dir))
            _render(lib, h, opts)
        except SceneError as e:
            sys.stderr.write("sundog: fatal: %s\n" % e)
            sys.exit(1)
        finally:
            if h:
                lib.sundog_scene_destroy(h)
        sys.exit(0)


# ---- ctypes backend ----------------------------------------------------------
# Struct layouts and enum values mirror src/sundog_api.h (hand-synced; the
# header is the source of truth).

class _XformStep(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_int32), ("a", ctypes.c_double),
                ("b", ctypes.c_double), ("c", ctypes.c_double)]


class _PhysicsBody(ctypes.Structure):
    _fields_ = [("dynamic", ctypes.c_int32), ("density", ctypes.c_double),
                ("has_velocity", ctypes.c_int32),
                ("has_angular_velocity", ctypes.c_int32),
                ("velocity", ctypes.c_double * 3),
                ("angular_velocity", ctypes.c_double * 3),
                ("friction", ctypes.c_double), ("restitution", ctypes.c_double),
                ("thickness", ctypes.c_double)]


class _RenderOptions(ctypes.Structure):
    _fields_ = [("out_path", ctypes.c_char_p), ("stats_path", ctypes.c_char_p),
                ("aov_albedo_path", ctypes.c_char_p),
                ("aov_normal_path", ctypes.c_char_p),
                ("scene_name", ctypes.c_char_p),
                ("spp", ctypes.c_int32), ("width", ctypes.c_int32),
                ("height", ctypes.c_int32), ("seed", ctypes.c_int64),
                ("denoise", ctypes.c_int32),
                ("transparent_shadows", ctypes.c_int32),
                ("clamp", ctypes.c_double), ("gamma", ctypes.c_double),
                ("tonemap", ctypes.c_int32), ("physics_time", ctypes.c_double),
                ("quiet", ctypes.c_int32)]


class _DeviceInfo(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 256),
                ("cc_major", ctypes.c_int32), ("cc_minor", ctypes.c_int32),
                ("driver_version", ctypes.c_int32),
                ("runtime_version", ctypes.c_int32),
                ("total_mem_mb", ctypes.c_uint64),
                ("optix_header_version", ctypes.c_uint32),
                ("rtcore_version", ctypes.c_uint32)]


def _load_backend():
    build = os.environ.get("SUNDOG_BUILD") or "/tmp/sundog-build"
    path = os.path.join(build, "libsundog.so")
    if not os.path.isfile(path):
        sys.stderr.write(
            "scenelib: renderer backend not found at %s\n"
            "  build it first: source scripts/env-testbox.sh && make -j16\n"
            "  (or point SUNDOG_BUILD at the build directory)\n" % path)
        sys.exit(1)
    # RTLD_GLOBAL: PhysX dlopens libPhysXGpu_64.so at runtime, which resolves
    # CUDA runtime symbols from the process's global scope — libsundog's
    # libcudart dependency must land there, not in a local scope.
    lib = ctypes.CDLL(path, mode=ctypes.RTLD_GLOBAL)
    lib.sundog_last_error.restype = ctypes.c_char_p
    lib.sundog_scene_create.restype = ctypes.c_void_p
    lib.sundog_scene_create.argtypes = [ctypes.c_char_p]
    lib.sundog_scene_destroy.argtypes = [ctypes.c_void_p]
    return lib


def _err(lib):
    return (lib.sundog_last_error() or b"unknown error").decode("utf-8", "replace")


def _d3(v):
    return (ctypes.c_double * 3)(*v) if v is not None else None


def _apply(lib, program):
    """Replay a _program() call list against the backend; returns the handle."""
    d = ctypes.c_double
    h = None
    for call in program:
        name, args = call[0], call[1:]
        if name == "create":
            h = ctypes.c_void_p(lib.sundog_scene_create(args[0].encode()))
            if not h.value:
                raise SceneError(_err(lib))
            continue
        if name == "set_render":
            w, ht, spp, md, clamp, seed, gamma, expo, tm, ts = args
            rc = lib.sundog_set_render(h, w, ht, spp, md, d(clamp),
                                       ctypes.c_int64(seed), d(gamma), d(expo),
                                       tm, ts)
        elif name == "set_physics":
            g, ts_, mt, fr, re, pi, vi, st, sp = args
            rc = lib.sundog_set_physics(h, _d3(g), d(ts_), d(mt), d(fr), d(re),
                                        pi, vi, d(st), d(sp))
        elif name == "set_camera":
            lf, la, up, vf, ap, fd = args
            rc = lib.sundog_set_camera(h, _d3(lf), _d3(la), _d3(up), d(vf),
                                       d(ap), d(fd))
        elif name == "set_background_solid":
            rc = lib.sundog_set_background_solid(h, _d3(args[0]))
        elif name == "set_background_gradient":
            rc = lib.sundog_set_background_gradient(h, _d3(args[0]), _d3(args[1]))
        elif name == "set_background_envmap":
            f, rot, inten, imp = args
            rc = lib.sundog_set_background_envmap(h, f.encode(), d(rot),
                                                  d(inten), imp)
        elif name == "add_texture_solid":
            rc = lib.sundog_add_texture_solid(h, _d3(args[0]))
        elif name == "add_texture_checker":
            a, b, sc = args
            sc2 = (ctypes.c_double * 2)(*sc) if sc is not None else None
            rc = lib.sundog_add_texture_checker(h, _d3(a), _d3(b), sc2)
        elif name == "add_texture_grid":
            a, b, sc, w = args
            sc2 = (ctypes.c_double * 2)(*sc) if sc is not None else None
            rc = lib.sundog_add_texture_grid(h, _d3(a), _d3(b), sc2, d(w))
        elif name == "add_texture_image":
            rc = lib.sundog_add_texture_image(h, args[0].encode(), args[1])
        elif name == "add_material_lambert":
            rc = lib.sundog_add_material_lambert(h, _d3(args[0]), args[1])
        elif name == "add_material_metal":
            rc = lib.sundog_add_material_metal(h, _d3(args[0]), args[1], d(args[2]))
        elif name == "add_material_dielectric":
            rc = lib.sundog_add_material_dielectric(h, d(args[0]), _d3(args[1]),
                                                    _d3(args[2]), args[3])
        elif name == "add_material_emissive":
            rc = lib.sundog_add_material_emissive(h, _d3(args[0]), args[1],
                                                  d(args[2]), args[3])
        elif name == "add_material_water":
            rc = lib.sundog_add_material_water(h, d(args[0]), _d3(args[1]),
                                               d(args[2]), d(args[3]), _d3(args[4]))
        elif name == "add_mesh":
            rc = lib.sundog_add_mesh(h, args[0].encode(), args[1],
                                     args[2].encode() if args[2] else None)
        elif name == "add_object":
            gk, mesh_id, front, back, cutout, steps, nee, phys = args
            arr = (_XformStep * max(len(steps), 1))()
            for i, (k, a, b, c) in enumerate(steps):
                arr[i] = _XformStep(k, a, b, c)
            pb = None
            if phys is not None:
                dyn, den, hv, vel, hav, ang, fr, re, th = phys
                pb = _PhysicsBody(dyn, den, hv, hav,
                                  (ctypes.c_double * 3)(*vel),
                                  (ctypes.c_double * 3)(*ang), fr, re, th)
            rc = lib.sundog_add_object(h, gk, mesh_id, front, back, cutout,
                                       arr, len(steps), nee,
                                       ctypes.byref(pb) if pb else None)
        elif name == "add_flame":
            base, ht, rad, inten, sig, ns, seed, li = args
            rc = lib.sundog_add_flame(h, _d3(base), d(ht), d(rad), d(inten),
                                      d(sig), d(ns), ctypes.c_int64(seed), d(li))
        elif name == "add_point_light":
            rc = lib.sundog_add_point_light(h, _d3(args[0]), _d3(args[1]),
                                            d(args[2]))
        elif name == "add_distant_light":
            rc = lib.sundog_add_distant_light(h, _d3(args[0]), _d3(args[1]))
        else:
            raise SceneError("unknown program call %r" % name)
        if rc is None or rc < 0 or (name.startswith("set_") and rc != 0):
            raise SceneError(_err(lib))
    return h


def _render(lib, h, opts):
    o = _RenderOptions(
        out_path=opts.out.encode(),
        stats_path=opts.stats.encode() if opts.stats else None,
        aov_albedo_path=opts.aov_albedo.encode() if opts.aov_albedo else None,
        aov_normal_path=opts.aov_normal.encode() if opts.aov_normal else None,
        scene_name=os.path.basename(sys.argv[0]).encode() or b"(scene)",
        spp=opts.spp, width=opts.width, height=opts.height, seed=opts.seed,
        denoise=opts.denoise, transparent_shadows=opts.transparent_shadows,
        clamp=opts.clamp, gamma=opts.gamma, tonemap=opts.tonemap,
        physics_time=opts.physics_time, quiet=1 if opts.quiet else 0)
    if lib.sundog_render(h, ctypes.byref(o)) != 0:
        raise SceneError(_err(lib))


def _parse_args(args, default_out):
    ap = argparse.ArgumentParser(
        prog=os.path.basename(sys.argv[0]),
        description="sundog scene — running this file renders it",
        allow_abbrev=False)
    NAN = float("nan")
    ap.add_argument("--out", default=default_out, metavar="FILE.png")
    ap.add_argument("--spp", type=int, default=-1, metavar="N")
    ap.add_argument("--size", default=None, metavar="WxH")
    ap.add_argument("--seed", type=int, default=-1, metavar="N")
    ap.add_argument("--denoise", dest="denoise", action="store_const",
                    const=1, default=-1)
    ap.add_argument("--no-denoise", dest="denoise", action="store_const", const=0)
    ap.add_argument("--opaque-shadows", dest="transparent_shadows",
                    action="store_const", const=0, default=-1)
    ap.add_argument("--clamp", type=float, default=NAN, metavar="F")
    ap.add_argument("--gamma", type=float, default=NAN, metavar="F")
    ap.add_argument("--tonemap", choices=["aces", "clamp"], default=None)
    ap.add_argument("--physics-time", dest="physics_time", type=float,
                    default=NAN, metavar="F")
    ap.add_argument("--stats", default=None, metavar="FILE.json")
    ap.add_argument("--aov-albedo", dest="aov_albedo", default=None)
    ap.add_argument("--aov-normal", dest="aov_normal", default=None)
    ap.add_argument("--probe", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    ns = ap.parse_args(args)
    ns.width = ns.height = -1
    if ns.size:
        try:
            w, _, h = ns.size.partition("x")
            ns.width, ns.height = int(w), int(h)
        except ValueError:
            ap.error("--size expects WxH")
    ns.tonemap = {"aces": 0, "clamp": 1, None: -1}[ns.tonemap]
    return ns


def probe():
    """Print GPU/driver/OptiX info (the old `sundog --probe` format)."""
    lib = _load_backend()
    di = _DeviceInfo()
    if lib.sundog_probe(ctypes.byref(di)) != 0:
        sys.stderr.write("sundog: fatal: %s\n" % _err(lib))
        sys.exit(1)
    v = di.optix_header_version
    sys.stdout.write(
        "GPU:            %s\n"
        "Compute:        sm_%d%d\n"
        "VRAM:           %d MB\n"
        "CUDA driver:    %d.%d\n"
        "CUDA runtime:   %d.%d\n"
        "OptiX header:   %d.%d.%d\n"
        "RT core:        %d.%d\n"
        % (di.name.decode(), di.cc_major, di.cc_minor, di.total_mem_mb,
           di.driver_version // 1000, (di.driver_version % 1000) // 10,
           di.runtime_version // 1000, (di.runtime_version % 1000) // 10,
           v // 10000, (v % 10000) // 100, v % 100,
           di.rtcore_version // 10, di.rtcore_version % 10))
