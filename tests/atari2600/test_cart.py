"""Exercise fixed and bank-switched Atari cartridge hardware."""
import os
import subprocess
import tempfile
import unittest

from tests.games import ROOT, built


RUNNER = os.path.join(os.environ.get("NES_BUILD", os.path.join(ROOT, "build")),
                      "atari2600_cart_tests.exe" if os.name == "nt" else "atari2600_cart_tests")


@built(RUNNER, "build the Atari 2600 cartridge tests first")
class TestCartridge(unittest.TestCase):
    def test_raw_rom_size_limits(self):
        runner = RUNNER.replace("atari2600_cart_tests", "atari2600_headless")
        rom = bytearray(2048)
        rom[:3] = bytes((0x4C, 0, 0xF0))
        rom[-4:-2] = bytes((0, 0xF0))
        with tempfile.TemporaryDirectory() as directory:
            for name, data, succeeds in (("game.a26", rom, True),
                                         ("empty.a26", b"", False),
                                         ("oversized.a26", bytes(32769), False)):
                with self.subTest(name=name):
                    path = os.path.join(directory, name)
                    with open(path, "wb") as file:
                        file.write(data)
                    result = subprocess.run([runner, path, "--frames", "1"],
                                            capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode == 0, succeeds, result.stderr)

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
