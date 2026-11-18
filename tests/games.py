"""Find commercial game ROMs for local-only tests. They are never committed;
tests that need one skip when it is absent. Searched: $NES_GAMES_DIR and the
folder above the repository (top level only), matched by SHA-256."""
import hashlib
import os
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BATTLE_CITY = "aa07da5c5b714dc96531fb6163e7eaec9e16b78090f52d0f3df8220ed405bc3a"  # (J), NROM-128


def find_rom(sha256):
    dirs = [os.environ.get("NES_GAMES_DIR"), os.path.dirname(ROOT)]
    for d in filter(None, dirs):
        if not os.path.isdir(d):
            continue
        for name in os.listdir(d):
            path = os.path.join(d, name)
            if name.lower().endswith(".nes") and os.path.isfile(path) and os.path.getsize(path) < 1 << 20:
                with open(path, "rb") as f:
                    if hashlib.sha256(f.read()).hexdigest() == sha256:
                        return path
    return None


def built(what, why="build it first (cmake --build build --config Release)"):
    """skipUnless for a program every build makes (a path, or True once found).
    With RETROCAST_STRICT=1 (CI) nothing is skipped: a missing program fails."""
    if os.environ.get("RETROCAST_STRICT") == "1":
        return lambda test: test
    return unittest.skipUnless(what if isinstance(what, bool) else os.path.exists(what), why)
