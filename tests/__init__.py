import os

# The test programs are built only with -DTESTS=ON; without it most tests would
# quietly skip, so say so once instead.
_cache = os.path.join(os.environ.get("NES_BUILD", os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build")),
                      "CMakeCache.txt")
if os.path.exists(_cache) and "TESTS:BOOL=ON" not in open(_cache, encoding="utf-8", errors="replace").read():
    raise ImportError(_cache + ": the build has no test programs; configure it with -DTESTS=ON")
