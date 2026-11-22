"""Which system a file is for, from its bytes (core/detect.h): the C
detectors (system_detect, as retrocast picks the system) and the CMake rules
for the ROMS list (cmake/detect_system.cmake) on the same made-up files; both
must give the same answer."""
import os
import shutil
import subprocess
import tempfile
import unittest

from tests.games import ROOT, built

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
RUNNER = os.path.join(BUILD, "system_detect" + EXE)
CMAKE_ID = {"nes": "nes", "gb": "gb", "genesis": "md", "atari2600": "a26"}
LOGO = bytes.fromhex("ceed6666cc0d000b03730083000c000d0008111f8889000edccc6ee6ddddd999"
                     "bbbb67636e0eecccdddc999fbbb9333e")


def gb(checksum_ok=True):
    rom = bytearray(0x8000)
    rom[0x104:0x134] = LOGO
    rom[0x134:0x13F] = b"TEST"
    x = 0
    for b in rom[0x134:0x14D]:
        x = (x - b - 1) & 0xFF
    rom[0x14D] = x if checksum_ok else x ^ 1
    return bytes(rom)


def genesis(sega=b"SEGA MEGA DRIVE "):
    rom = bytearray(0x400)
    rom[0x100:0x100 + len(sega)] = sega
    return bytes(rom)


def atari(size=4096, vector_ok=True):
    rom = bytearray(size)
    rom[-3] = 0xF0 if vector_ok else 0x00  # reset vector's high byte: $Fxxx is in $1000-$1FFF
    return bytes(rom)


def nes():
    return b"NES\x1a\x01\x01" + bytes(10) + bytes(0x4000 + 0x2000)


def both_headers():  # an iNES header and "SEGA" at $100: two sure answers
    rom = bytearray(nes()[:0x600])
    rom[0x100:0x104] = b"SEGA"
    return bytes(rom)


CASES = [  # file name, bytes, expected id (None: refused)
    ("game.nes", nes(), "nes"),
    ("disk.fds", b"FDS\x1a\x01" + bytes(11) + bytes(65500), "nes"),
    ("raw.fds", b"\x01*NINTENDO-HVC*" + bytes(65500), "nes"),
    ("game.gb", gb(), "gb"),
    ("game.gbc", gb(), "gb"),
    ("no_extension", gb(), "gb"),
    ("hacked.gb", gb(checksum_ok=False), "gb"),  # logo only: 70, + the extension
    ("game.md", genesis(), "md"),
    ("game.bin", genesis(b" SEGA GENESIS"), "md"),
    ("game.a26", atari(), "a26"),
    ("bank.a26", atari(8192, vector_ok=False), "a26"),  # weak, the extension decides
    ("game.bin", atari(2048), None),  # weak: without .a26 (or --system) no system is picked
    ("both.nes", both_headers(), "nes"),  # two sure answers, the extension decides
    ("both.rom", both_headers(), None),  # two sure answers, nothing decides: --system
    ("random.bin", bytes(range(256)) * 5, None),
    ("odd.a26", atari(3000), None),  # not a cartridge size
    ("tiny.nes", b"NES", None),
]


@built(RUNNER, "build system_detect first")
class TestDetect(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def write(self, i, name, data):
        d = os.path.join(self.tmp.name, str(i))
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def test_c_detectors(self):
        built_ids = subprocess.run([RUNNER, "--systems"], capture_output=True, text=True).stdout.split()
        if sorted(built_ids) != sorted(CMAKE_ID.values()):
            self.skipTest("needs every system built (SYSTEMS), not " + " ".join(built_ids))
        for i, (name, data, want) in enumerate(CASES):
            with self.subTest(file=name, want=want):
                r = subprocess.run([RUNNER, self.write(i, name, data)], capture_output=True, text=True)
                if want is None:
                    self.assertEqual(r.returncode, 1, r.stdout)
                    self.assertIn("--system", r.stderr)
                else:
                    self.assertEqual((r.returncode, r.stdout.strip()), (0, want), r.stderr)

    @unittest.skipUnless(shutil.which("cmake"), "no cmake")
    def test_cmake_rules_agree(self):
        script = os.path.join(ROOT, "cmake", "detect_system.cmake")
        for i, (name, data, want) in enumerate(CASES):
            with self.subTest(file=name, want=want):
                r = subprocess.run(["cmake", "-DFILE=" + self.write(i, name, data), "-P", script],
                                   capture_output=True, text=True)
                if want is None:
                    self.assertNotEqual(r.returncode, 0, r.stderr)
                else:
                    self.assertEqual(r.returncode, 0, r.stderr)
                    self.assertEqual(CMAKE_ID[r.stderr.strip()], want)  # message() prints to stderr


if __name__ == "__main__":
    unittest.main()
