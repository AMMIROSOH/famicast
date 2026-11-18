"""Save states (nes/runtime/nes.c): stopping, saving, loading and going on must
give exactly the run that never stopped; interpreter and recompiled builds
share the format."""
import os
import subprocess
import tempfile
import unittest

from tests.fetch_test_roms import fetch_ntr
from tests.nes.test_roms import RUNNER
from tests.nes.test_recomp import BC_HEADLESS
from tests.games import built

# CHR RAM: the result text (font uploaded around frame 170) is on screen by
# frame 250, so a state that lost CHR RAM or PPU fields shows in the hash
# (checked by breaking the CHR RAM restore on purpose)
ROM = "vbl_nmi_timing/2.vbl_timing.nes"


def run(exe, *args):
    r = subprocess.run([exe, *args], capture_output=True, text=True, timeout=300)
    return r.returncode, r.stdout + r.stderr


def hash_line(out):
    return [line for line in out.splitlines() if line.startswith("HASH ")]


@built(RUNNER, "build the C runtime first (see tests/nes/test_cpu.py)")
class TestStates(unittest.TestCase):
    def test_resume_equals_uninterrupted_run(self):
        rom = fetch_ntr(ROM)
        with tempfile.TemporaryDirectory() as tmp:
            state = os.path.join(tmp, "s.state")
            _, straight = run(RUNNER, rom, "--frames", "400", "--hash")
            self.assertEqual(run(RUNNER, rom, "--frames", "250", "--save-state", state)[0], 0)
            _, resumed = run(RUNNER, rom, "--frames", "150", "--load-state", state, "--hash")
        self.assertTrue(hash_line(straight))
        self.assertEqual(hash_line(resumed), hash_line(straight))  # frame and CPU cycle count

    def test_refuses_another_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = os.path.join(tmp, "s.state")
            run(RUNNER, fetch_ntr(ROM), "--frames", "10", "--save-state", state)
            code, out = run(RUNNER, fetch_ntr("vbl_nmi_timing/1.frame_basics.nes"), "--load-state", state)
        self.assertEqual(code, 2)
        self.assertIn("different game", out)

    @unittest.skipUnless(os.path.exists(BC_HEADLESS), "build with -DROMS=<Battle City ROM>")
    def test_interpreter_state_resumes_recompiled_in_lockstep(self):
        with tempfile.TemporaryDirectory() as tmp:
            state, whole, rest = (os.path.join(tmp, n) for n in ("s.state", "whole.nesi", "rest.nesi"))
            _, straight = run(BC_HEADLESS, "--play", "1", "--frames", "1500", "--hash", "--record", whole)
            self.assertEqual(run(BC_HEADLESS, "--interp", "--play", "1", "--frames", "900",
                                 "--save-state", state)[0], 0)
            with open(whole, "rb") as f:  # header + 3 bytes per frame (input_log.h)
                data = f.read()
            with open(rest, "wb") as f:
                f.write(data[:5] + data[5 + 900 * 3:])
            _, resumed = run(BC_HEADLESS, "--load-state", state, "--replay", rest, "--diff", "--hash")
        self.assertIn("DIFF OK: 600 frames", resumed)
        self.assertEqual(hash_line(resumed), hash_line(straight))


if __name__ == "__main__":
    unittest.main()
