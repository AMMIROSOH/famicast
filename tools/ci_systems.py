"""Which systems a change needs CI to build and test (.github/workflows/ci.yml).

    python tools/ci_systems.py BASE HEAD   -> prints systems=["nes","gb"] and cmake=nes;gb

A file under a system's folder (nes/, tests/nes/) selects that system; docs
(any .md, docs/, dist/, release notes and workflow) select nothing; anything
else (core/, the root CMakeLists.txt, cmake/, shared tests and tools, the CI
workflow) is shared and selects every system. No usable BASE (a new branch,
a force push): every system.
"""
import json
import subprocess
import sys

SYSTEMS = ["nes", "gb"]  # as in CMakeLists.txt SYSTEMS
DOCS = ("docs/", "dist/", ".github/release-notes.md", ".github/workflows/release.yml", "tools/make_release.py")


def systems_for(files):
    out = set()
    for f in files:
        if f.endswith(".md") or f.startswith(DOCS) or f in ("LICENSE", ".gitignore"):
            continue
        own = [s for s in SYSTEMS if f.startswith((s + "/", "tests/%s/" % s))]
        if not own:
            return list(SYSTEMS)
        out.update(own)
    return [s for s in SYSTEMS if s in out]


def changed(base, head):
    if not base or set(base) == {"0"}:
        return None
    if subprocess.run(["git", "cat-file", "-e", base + "^{commit}"], capture_output=True).returncode:
        return None
    r = subprocess.run(["git", "diff", "--name-only", base, head], capture_output=True, text=True)
    return r.stdout.split() if r.returncode == 0 else None


if __name__ == "__main__":
    files = changed(*sys.argv[1:3]) if len(sys.argv) >= 3 else None
    picked = list(SYSTEMS) if files is None else systems_for(files)
    print("systems=" + json.dumps(picked))
    print("cmake=" + ";".join(picked))
