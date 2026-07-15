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

Running the file renders it: scenelib serializes the scene to the renderer's
internal JSON form (a temp file beside the scene, so relative asset paths
resolve), invokes the backend binary ($SUNDOG_BUILD/sundog, default
/tmp/sundog-build/sundog), and forwards any extra CLI arguments verbatim
(--spp/--size/--seed/--denoise/--stats/... override the scene's own render
settings; --out overrides the name passed to run()). `--emit-json PATH|-`
prints the JSON instead of rendering (used by tests and tooling).

Determinism contract: this module never draws randomness or timestamps; the
JSON it emits is a pure function of the API calls. Scenes that use random
layout must call random.seed(N) themselves before the first draw. The
`render.seed` setting is the renderer's sampling seed — a separate concern.

Optional arguments left at the OMIT default are not written into the JSON at
all, so the renderer's own defaults apply (and stay documented in one place).

Stdlib only; compatible with Python 3.6+.
"""

import json
import os
import subprocess
import sys
import tempfile


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

    def mesh(self, name, obj, normals=OMIT):
        m = {"obj": obj}
        _put(m, "normals", normals)
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

    def to_json(self):
        self._validate()
        return json.dumps(self.doc, separators=(",", ":")) + "\n"

    def save(self, path):
        text = self.to_json()
        with open(path, "w") as f:
            f.write(text)
        sys.stderr.write("wrote %s (%d objects)\n" % (path, len(self._objects)))

    def run(self, out, argv=None):
        """Render this scene: called from the scene file's __main__ block.

        `out` is the output PNG the scene chooses for itself; a caller may
        override it (and any render setting) with the backend's own flags,
        which are forwarded verbatim: --out/--spp/--size/--seed/--denoise/...
        `--emit-json PATH` (or `-` for stdout) prints the scene JSON instead
        of rendering.
        """
        args = list(sys.argv[1:] if argv is None else argv)

        if "--emit-json" in args:
            i = args.index("--emit-json")
            if i + 1 >= len(args):
                sys.stderr.write("scenelib: --emit-json needs a path (or -)\n")
                sys.exit(2)
            target = args[i + 1]
            text = self.to_json()
            if target == "-":
                sys.stdout.write(text)
            else:
                with open(target, "w") as f:
                    f.write(text)
            return

        build = os.environ.get("SUNDOG_BUILD") or "/tmp/sundog-build"
        backend = os.path.join(build, "sundog")
        if not (os.path.isfile(backend) and os.access(backend, os.X_OK)):
            sys.stderr.write(
                "scenelib: renderer backend not found at %s\n"
                "  build it first: source scripts/env-testbox.sh && make -j16\n"
                "  (or point SUNDOG_BUILD at the build directory)\n" % backend)
            sys.exit(1)

        scene_path = os.path.abspath(sys.argv[0])
        scene_dir = os.path.dirname(scene_path) or "."
        text = self.to_json()
        fd, tmp = tempfile.mkstemp(prefix=".ir-", suffix=".json",
                                   dir=scene_dir)
        try:
            with os.fdopen(fd, "w") as f:
                f.write(text)
            cmd = [backend, "--scene", tmp] + args
            if "--out" not in args:
                cmd += ["--out", out]
            rc = subprocess.call(cmd)
        finally:
            try:
                os.unlink(tmp)
            except OSError:
                pass
        sys.exit(rc)
