#!/usr/bin/env python3
"""Temporary migration aid: raw ctypes smoke test of the libsundog.so C ABI
(probe + a 64x64 render of a minimal scene). Deleted once scenelib drives the
library and run-smoke covers this path."""
import ctypes
import os
import sys

lib = ctypes.CDLL(os.path.join(os.environ.get("SUNDOG_BUILD", "/tmp/sundog-build"),
                               "libsundog.so"))
lib.sundog_last_error.restype = ctypes.c_char_p
NAN = float("nan")


class DevInfo(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 256),
                ("cc_major", ctypes.c_int32), ("cc_minor", ctypes.c_int32),
                ("driver_version", ctypes.c_int32), ("runtime_version", ctypes.c_int32),
                ("total_mem_mb", ctypes.c_uint64),
                ("optix_header_version", ctypes.c_uint32),
                ("rtcore_version", ctypes.c_uint32)]


class RenderOptions(ctypes.Structure):
    _fields_ = [("out_path", ctypes.c_char_p), ("stats_path", ctypes.c_char_p),
                ("aov_albedo_path", ctypes.c_char_p), ("aov_normal_path", ctypes.c_char_p),
                ("scene_name", ctypes.c_char_p),
                ("spp", ctypes.c_int32), ("width", ctypes.c_int32), ("height", ctypes.c_int32),
                ("seed", ctypes.c_int64),
                ("denoise", ctypes.c_int32), ("transparent_shadows", ctypes.c_int32),
                ("clamp", ctypes.c_double), ("gamma", ctypes.c_double),
                ("tonemap", ctypes.c_int32), ("physics_time", ctypes.c_double),
                ("quiet", ctypes.c_int32)]


def v3(x, y, z):
    return (ctypes.c_double * 3)(x, y, z)


di = DevInfo()
rc = lib.sundog_probe(ctypes.byref(di))
assert rc == 0, lib.sundog_last_error()
print("probe: %s sm_%d%d %d MB" % (di.name.decode(), di.cc_major, di.cc_minor,
                                   di.total_mem_mb))

lib.sundog_scene_create.restype = ctypes.c_void_p
h = ctypes.c_void_p(lib.sundog_scene_create(b"scenes"))
assert h.value

# validate before camera must fail
assert lib.sundog_scene_validate(h) != 0
assert b"missing camera" in lib.sundog_last_error()
print("validate-no-camera: error surfaced OK")

rc = lib.sundog_set_render(h, 64, 64, 8, 8, ctypes.c_double(NAN),
                           ctypes.c_int64(7), ctypes.c_double(NAN),
                           ctypes.c_double(NAN), -1, -1)
assert rc == 0, lib.sundog_last_error()
rc = lib.sundog_set_camera(h, v3(0, 1.5, 5), v3(0, 0.7, 0), None,
                           ctypes.c_double(35.0), ctypes.c_double(NAN),
                           ctypes.c_double(NAN))
assert rc == 0, lib.sundog_last_error()
rc = lib.sundog_set_background_gradient(h, v3(1, 1, 1), v3(0.4, 0.6, 1.0))
assert rc == 0
mat = lib.sundog_add_material_lambert(h, v3(0.7, 0.3, 0.3), -1)
assert mat == 0, lib.sundog_last_error()
rc = lib.sundog_add_point_light(h, v3(3, 4, 2), v3(40, 40, 40),
                                ctypes.c_double(0.4))
assert rc >= 0


class Step(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_int32), ("a", ctypes.c_double),
                ("b", ctypes.c_double), ("c", ctypes.c_double)]


steps = (Step * 2)(Step(0, 0.7, 0.7, 0.7), Step(1, 0, 0.7, 0))
# phase violation: object after lights must fail
oid = lib.sundog_add_object(h, 0, -1, 0, -2, -1, steps, 2, -1, None)
assert oid == -1 and b"call order" in lib.sundog_last_error()
print("phase guard: object-after-lights rejected OK")

lib.sundog_scene_destroy(h)

# fresh scene in the right order, then render
h = ctypes.c_void_p(lib.sundog_scene_create(b"scenes"))
lib.sundog_set_render(h, 64, 64, 8, 8, ctypes.c_double(NAN), ctypes.c_int64(7),
                      ctypes.c_double(NAN), ctypes.c_double(NAN), -1, -1)
lib.sundog_set_camera(h, v3(0, 1.5, 5), v3(0, 0.7, 0), None,
                      ctypes.c_double(35.0), ctypes.c_double(NAN),
                      ctypes.c_double(NAN))
lib.sundog_set_background_gradient(h, v3(1, 1, 1), v3(0.4, 0.6, 1.0))
mat = lib.sundog_add_material_lambert(h, v3(0.7, 0.3, 0.3), -1)
oid = lib.sundog_add_object(h, 0, -1, mat, -2, -1, steps, 2, -1, None)
assert oid == 0, lib.sundog_last_error()
lib.sundog_add_point_light(h, v3(3, 4, 2), v3(40, 40, 40), ctypes.c_double(0.4))

opt = RenderOptions(out_path=b"/tmp/capi-smoke.png", stats_path=None,
                    aov_albedo_path=None, aov_normal_path=None,
                    scene_name=b"capi-smoke", spp=-1, width=-1, height=-1,
                    seed=-1, denoise=-1, transparent_shadows=-1,
                    clamp=NAN, gamma=NAN, tonemap=-1, physics_time=NAN, quiet=0)
rc = lib.sundog_render(h, ctypes.byref(opt))
assert rc == 0, lib.sundog_last_error()
lib.sundog_scene_destroy(h)
sz = os.path.getsize("/tmp/capi-smoke.png")
assert sz > 500
print("render via C ABI OK (%d bytes)" % sz)
