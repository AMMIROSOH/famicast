"""Interpreter vs independent references (docs/02, verification ladder 2-3).

Needs the C build: cmake -S . -B build -G Ninja && cmake --build build
SingleStepTests: python tests/fetch_test_roms.py --sst (opcodes not yet
downloaded are skipped; set NES_SST_REQUIRED=1 to fail instead).
"""
import os
import re
import subprocess
import unittest

from recomp.ops import OPS
from tests.fetch_test_roms import ROMS, fetch

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
RUNNER = os.path.join(BUILD, "cpu_tests.exe" if os.name == "nt" else "cpu_tests")
LOG_STATE = re.compile(r"^([0-9A-F]{4}).*(A:\w\w X:\w\w Y:\w\w P:\w\w SP:\w\w).*CYC:(\d+)")


def run(*args):
    return subprocess.run([RUNNER, *args], capture_output=True, text=True, check=False)


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see module docstring)")
class TestNestest(unittest.TestCase):
    def test_trace_matches_nintendulator_log(self):
        with open(fetch("nestest.log")) as f:
            want = []
            for line in f:
                m = LOG_STATE.match(line)
                want.append(f"{m.group(1)} {m.group(2)} CYC:{m.group(3)}")
        got = run("nestest", fetch("nestest.nes"), str(len(want))).stdout.splitlines()
        self.assertEqual(len(got), len(want))
        for i, (g, w) in enumerate(zip(got, want)):
            if g != w:
                self.fail(f"nestest.log line {i + 1}:\n want {w}\n got  {g}\n previous {want[i - 1]}")


# Opcodes where we follow a hardware-verified test ROM instead of SingleStepTests.
SST_DISAGREE = {
    0xAB: "LXA is unstable on hardware; blargg instr_test-v5 03-immediate (verified on a "
          "real NES) needs A = X = operand, SST assumes (A | $EE) & operand",
}


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see module docstring)")
class TestSingleStep(unittest.TestCase):
    def test_every_opcode(self):
        required = os.environ.get("NES_SST_REQUIRED") == "1"
        for op in OPS:
            if op.mn == "KIL" or op.code in SST_DISAGREE:
                continue  # KIL: registers match; the 11-cycle bus pattern of a jammed CPU is not modelled
            path = os.path.join(ROMS, "sst", "%02x.bin" % op.code)
            with self.subTest(op=f"${op.code:02X} {op.mn} {op.mode}"):
                if not os.path.exists(path):
                    if required:
                        self.fail("missing " + path)
                    continue
                r = run("sst", path)
                self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
