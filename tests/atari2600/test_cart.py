"""Exercise fixed and bank-switched Atari cartridge hardware."""
import os
import base64
import subprocess
import tempfile
import unittest

from tests.games import ROOT, built


RUNNER = os.path.join(os.environ.get("ATARI2600_BUILD", os.path.join(ROOT, "build")),
                      "atari2600_cart_tests.exe" if os.name == "nt" else "atari2600_cart_tests")


@built(RUNNER, "build the Atari 2600 cartridge tests first")
class TestCartridge(unittest.TestCase):
    def test_archive_member_selection(self):
        runner = RUNNER.replace("atari2600_cart_tests", "atari2600_headless")
        rom = bytearray(2048)
        rom[:3] = bytes((0x4C, 0, 0xF0))
        rom[-4:-2] = bytes((0, 0xF0))
        with tempfile.TemporaryDirectory() as directory:
            for name, content in (("game.a26", rom), ("another.bin", rom),
                                  ("notes.txt", b"test"), ("oversized.a26", bytes(32769))):
                with open(os.path.join(directory, name), "wb") as file:
                    file.write(content)
            for members, error in (
                (("game.a26", "notes.txt"), None),
                (("game.a26", "another.bin"), "multiple ROM members"),
                (("notes.txt",), "no regular ROM member"),
                (("oversized.a26",), "unsupported ROM size"),
            ):
                with self.subTest(members=members):
                    archive = os.path.join(directory, "game.7z")
                    subprocess.run(["cmake", "-E", "tar", "cf", archive, "--format=7zip", *members],
                                   cwd=directory, check=True, capture_output=True)
                    result = subprocess.run([runner, archive, "--frames", "1"],
                                            capture_output=True, text=True, check=False)
                    if error:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(error, result.stderr)
                    else:
                        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cartridge_banks_and_mirroring(self):
        result = subprocess.run([RUNNER], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_bus_and_riot(self):
        runner = RUNNER.replace("atari2600_cart_tests", "atari2600_bus_tests")
        result = subprocess.run([runner], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_runtime_and_save_states(self):
        runner = RUNNER.replace("atari2600_cart_tests", "atari2600_runtime_tests")
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([runner, os.path.join(directory, "game.state")],
                                    capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_archive_matches_raw_and_corruption_fails(self):
        runner = RUNNER.replace("atari2600_cart_tests", "atari2600_headless")
        archive = base64.b64decode(
            "N3q8ryccAANrzb2pJwAAAAAAAABpAAAAAAAAAEYf+z8AJgAaEA2feH3XI5dQ0m4Wpbd9HnhwomdTe6Hb2xaglMD//3gQAAABBAYAAQknAAcLAQABIwMBAQVdAACAAAyQAAAICgGm+aG9AAAFARETAHQAZQBzAHQALgBhADIANgAAABQKAQBc+DNqd3rdARIKAQBc+DNqd3rdARMKAQBc+DNqd3rdARUGAQAggLaBAAA=")
        rom = bytearray(4096)
        rom[:3] = bytes((0x4C, 0, 0xF0))
        rom[-4:-2] = bytes((0, 0xF0))
        with tempfile.TemporaryDirectory() as directory:
            raw = os.path.join(directory, "game.a26")
            packed = os.path.join(directory, "game.7Z")
            broken = os.path.join(directory, "broken.7z")
            for path, contents in ((raw, rom), (packed, archive), (broken, archive[:30])):
                with open(path, "wb") as f:
                    f.write(contents)
            def run(path):
                return subprocess.run([runner, path, "--frames", "2"], capture_output=True, text=True, check=False)
            plain, compressed, corrupt = run(raw), run(packed), run(broken)
        self.assertEqual(plain.returncode, 0, plain.stderr)
        self.assertEqual(compressed.returncode, 0, compressed.stderr)
        self.assertEqual(plain.stdout, compressed.stdout)
        self.assertNotEqual(corrupt.returncode, 0)
