"""Build one release archive for the platform this runs on.

    python tools/make_release.py --name retrocast-v0.1.0-windows-x64 [--work DIR] [--out DIR]

1. SDL3 (release-3.4.20), static, with the frontend's options (cmake/sdl3_options.cmake),
   installed into the staged kit/sdl3.
2. nes, nes_headless, nesrecomp, gb, gb_headless and gbrecomp, Release, linked against that SDL3.
3. Stage: the programs, the recompile kit (this project's CMakeLists.txt, cmake/,
   core/, nes/, gb/ and the prebuilt SDL3), README.txt, licenses.
4. Kit check: recompile a tiny generated ROM with the staged kit, as a user
   would, and run the result headless.
5. Archive: NAME.zip on Windows, NAME.tar.gz elsewhere, in --out.
Used by .github/workflows/release.yml; needs git, CMake >= 3.20, a C compiler.
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDL_TAG = "release-3.4.20"  # as in CMakeLists.txt
WIN = os.name == "nt"
EXE = ".exe" if WIN else ""
KIT_FILES = ["CMakeLists.txt", "cmake", "core", "cpu", "nes", "gb"]


def run(*cmd, cwd=None):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=cwd)


def cmake_build(src, build, *defs):
    """Configure and build Release (single- or multi-config generators)."""
    run("cmake", "-S", src, "-B", build, "-DCMAKE_BUILD_TYPE=Release", *defs)
    run("cmake", "--build", build, "--config", "Release", "--parallel")


def tiny_rom(path):
    """NROM-128 that loops forever: enough to build and boot a recompiled program."""
    prg = bytearray(0x4000)
    prg[0:3] = bytes([0x4C, 0x00, 0xC0])  # $C000: JMP $C000
    prg[-6:] = bytes([0x00, 0xC0, 0x00, 0xC0, 0x00, 0xC0])  # NMI, RESET, IRQ = $C000
    with open(path, "wb") as f:
        f.write(b"NES\x1a\x01\x01\x00\x00" + bytes(8) + prg + bytes(0x2000))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True, help="archive and folder name")
    ap.add_argument("--work", default=os.path.join(ROOT, "out", "release"))
    ap.add_argument("--out", default=os.path.join(ROOT, "out", "dist"))
    a = ap.parse_args()
    work, stage = os.path.abspath(a.work), os.path.join(os.path.abspath(a.work), a.name)
    if os.path.exists(work):
        shutil.rmtree(work)
    os.makedirs(stage)
    kit = os.path.join(stage, "kit")

    # 1. SDL3, installed into the kit
    sdl_src = os.path.join(work, "SDL")
    run("git", "clone", "--depth", "1", "--branch", SDL_TAG, "https://github.com/libsdl-org/SDL.git", sdl_src)
    sdl_build = os.path.join(work, "sdl-build")
    cmake_build(sdl_src, sdl_build, "-C", os.path.join(ROOT, "cmake", "sdl3_options.cmake"),
                "-DCMAKE_INSTALL_PREFIX=" + os.path.join(kit, "sdl3"))
    run("cmake", "--install", sdl_build, "--config", "Release")

    # 2. the programs, against that SDL3
    build = os.path.join(work, "build")
    cmake_build(ROOT, build, "-DSDL3_PREFIX=" + os.path.join(kit, "sdl3"))

    # 3. stage
    for prog in ("nes", "nes_headless", "nesrecomp", "gb", "gb_headless", "gbrecomp"):
        shutil.copy2(os.path.join(build, prog + EXE), stage)
    for item in KIT_FILES:
        src = os.path.join(ROOT, item)
        if os.path.isdir(src):
            shutil.copytree(src, os.path.join(kit, item))
        else:
            shutil.copy2(src, kit)
    shutil.copy2(os.path.join(ROOT, "dist", "README.txt"), stage)
    shutil.copy2(os.path.join(ROOT, "LICENSE"), stage)
    shutil.copy2(os.path.join(sdl_src, "LICENSE.txt"), os.path.join(stage, "LICENSE-SDL3.txt"))

    # 4. kit check: recompile a ROM with the staged kit, then run it
    rom = os.path.join(work, "kit_check.nes")
    tiny_rom(rom)
    kit_build = os.path.join(work, "kit-build")
    cmake_build(kit, kit_build, "-DROMS=" + rom)
    run(os.path.join(kit_build, "kit_check_headless" + EXE), "--frames", "30")
    run(os.path.join(stage, "nes_headless" + EXE), rom, "--frames", "30")

    # 5. archive
    os.makedirs(a.out, exist_ok=True)
    archive = os.path.join(os.path.abspath(a.out), a.name + (".zip" if WIN else ".tar.gz"))
    fmt = ["--format=zip"] if WIN else []
    run("cmake", "-E", "tar", "cf" if WIN else "czf", archive, *fmt, a.name, cwd=work)
    print("archive:", archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
