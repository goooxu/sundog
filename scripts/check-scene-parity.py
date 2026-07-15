#!/usr/bin/env python3
"""Migration gate: every scenes/*.py must emit a scene dict exactly equal to
its committed scenes/*.json counterpart (object-level: key presence and list
order included). Also checks byte determinism (two runs) and cwd independence.

Temporary tool for the JSON -> Python scene migration; deleted with the JSON.

Usage: python3 scripts/check-scene-parity.py [stem ...]   (default: all *.py)
"""

import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENES = os.path.join(ROOT, "scenes")


def deep_diff(a, b, path="$"):
    """Return the first divergence as (path, a-side, b-side), or None."""
    if type(a) is not type(b) and not (
            isinstance(a, (int, float)) and isinstance(b, (int, float))
            and not isinstance(a, bool) and not isinstance(b, bool)):
        return (path, "type %s: %r" % (type(a).__name__, a),
                "type %s: %r" % (type(b).__name__, b))
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                return (path + "." + k, "<missing>", repr(b[k])[:120])
            if k not in b:
                return (path + "." + k, repr(a[k])[:120], "<missing>")
            d = deep_diff(a[k], b[k], path + "." + k)
            if d:
                return d
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return (path, "len %d" % len(a), "len %d" % len(b))
        for i, (x, y) in enumerate(zip(a, b)):
            d = deep_diff(x, y, "%s[%d]" % (path, i))
            if d:
                return d
        return None
    if a != b:
        return (path, repr(a), repr(b))
    return None


def emit(py_path, cwd):
    return subprocess.check_output(
        [sys.executable, "-B", py_path, "--emit-json", "-"], cwd=cwd)


def main():
    stems = sys.argv[1:]
    if not stems:
        stems = sorted(f[:-3] for f in os.listdir(SCENES)
                       if f.endswith(".py") and f != "scenelib.py")
    failed = []
    for stem in stems:
        py = os.path.join(SCENES, stem + ".py")
        js = os.path.join(SCENES, stem + ".json")
        if not os.path.isfile(js):
            print("%-24s SKIP (no reference json)" % stem)
            continue
        out_root = emit(py, ROOT)
        out_scenes = emit(py, SCENES)
        new = json.loads(out_root.decode())
        with open(js) as f:
            old = json.load(f)
        problems = []
        d = deep_diff(new, old)
        if d:
            problems.append("DIFF at %s\n    py:   %s\n    json: %s" % d)
        if out_root != emit(py, ROOT):
            problems.append("NOT DETERMINISTIC (two runs differ)")
        if out_root != out_scenes:
            problems.append("CWD-DEPENDENT (repo root vs scenes/ differ)")
        if problems:
            failed.append(stem)
            print("%-24s FAIL" % stem)
            for p in problems:
                print("  " + p)
        else:
            print("%-24s OK (%d objects)" % (stem, len(new["objects"])))
    if failed:
        print("parity FAILED: %s" % ", ".join(failed))
        return 1
    print("parity OK (%d scenes)" % len(stems))
    return 0


if __name__ == "__main__":
    sys.exit(main())
