"""Lockstep and state exchange for generated Atari cartridge programs."""
import os
import re
import subprocess
import tempfile
import unittest

from tests.games import ROOT, built

BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
EXE = ".exe" if os.name == "nt" else ""
FIXED = os.path.join(BUILD, "a26_fixed_headless" + EXE)
BANKED = os.path.join(BUILD, "a26_banked_headless" + EXE)
INTERPRETER = os.path.join(BUILD, "atari2600_headless" + EXE)
EXTERNAL = os.path.join(BUILD, "a26_external_headless" + EXE)
UNTRACED = os.path.join(BUILD, "a26_untraced_headless" + EXE)
PAL = os.path.join(BUILD, "a26_pal_headless" + EXE)
SMALL = os.path.join(BUILD, "a26_small_headless" + EXE)
F6 = os.path.join(BUILD, "a26_f6_headless" + EXE)
F4 = os.path.join(BUILD, "a26_f4_headless" + EXE)
RECOMPILER = os.path.join(BUILD, "atari2600recomp" + EXE)
BAD = os.path.join(BUILD, "atari2600_bad_recomp_tests" + EXE)


def run(program, *arguments):
    return subprocess.run([program, *arguments], capture_output=True, text=True, check=False)


@built(FIXED, "build Atari recompiler fixtures first")
class TestRecompiler(unittest.TestCase):
    def test_record_and_replay(self):
        rom = os.path.join(BUILD, "gen", "a26_fixed.a26")
        with tempfile.TemporaryDirectory() as directory:
            record = os.path.join(directory, "game.rec")
            copy = os.path.join(directory, "copy.rec")
            first_frame = os.path.join(directory, "first.pgm")
            replay_frame = os.path.join(directory, "replay.pgm")
            original = run(INTERPRETER, rom, "--frames", "72", "--play",
                           "--record", record, "--ppm", first_frame)
            self.assertEqual(original.returncode, 0, original.stderr)
            replay = run(FIXED, "--replay", record, "--record", copy, "--diff", "--ppm", replay_frame)
            self.assertEqual(replay.returncode, 0, replay.stderr)
            self.assertEqual(replay.stdout.splitlines()[0], original.stdout.strip())
            with open(record, "rb") as source, open(copy, "rb") as target:
                recording = source.read()
                self.assertEqual(recording, target.read())
                self.assertEqual(recording[:9], b"RCIN\x02a26\x00")
                self.assertIn(b"\x08\x00\x00\x00\x00", recording[9:])
            with open(first_frame, "rb") as source, open(replay_frame, "rb") as target:
                self.assertEqual(source.read(), target.read())

            with open(copy, "wb") as reset:
                inputs = bytearray(recording)
                inputs[9 + 40 * 5 + 4] = 1
                reset.write(inputs)
            restarted = run(FIXED, "--replay", copy, "--diff")
            baseline = run(INTERPRETER, rom, "--frames", "32")
            self.assertEqual(restarted.returncode, 0, restarted.stderr)
            self.assertEqual(restarted.stdout.splitlines()[0], baseline.stdout.strip())

            with open(copy, "wb") as invalid:
                invalid.write(recording[:5] + b"nes\x00" + recording[9:])
            result = run(FIXED, "--replay", copy)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cannot load Atari 2600 recording", result.stderr)

    def test_lockstep_and_interpreter_fallback(self):
        for program in (SMALL, FIXED, BANKED, F6, F4):
            with self.subTest(program=program):
                result = run(program, "--frames", "4", "--diff", "--coverage")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                coverage = re.search(r"coverage=([0-9.]+)%", result.stdout)
                self.assertIsNotNone(coverage, result.stdout)
                self.assertGreater(float(coverage.group(1)), 0)
                self.assertLess(float(coverage.group(1)), 100)

    def test_states_transfer_both_directions(self):
        rom = os.path.join(BUILD, "gen", "a26_banked.a26")
        with tempfile.TemporaryDirectory() as directory:
            for saver, save_args in ((BANKED, ()), (INTERPRETER, (rom,))):
                state = os.path.join(directory, "game.state")
                result = run(saver, *save_args, "--frames", "2", "--state-at", "2", state)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                compiled = run(BANKED, "--load-state", state, "--frames", "2", "--diff")
                interpreted = run(INTERPRETER, rom, "--load-state", state, "--frames", "2")
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                self.assertEqual(interpreted.returncode, 0, interpreted.stdout + interpreted.stderr)
                self.assertEqual(compiled.stdout.splitlines()[0], interpreted.stdout.splitlines()[0])

    def test_configuration_mismatch_fails(self):
        result = run(FIXED, "--region", "pal", "--frames", "1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match", result.stderr)

    def test_diff_reports_first_bad_instruction(self):
        rom = os.path.join(BUILD, "gen", "a26_fixed.a26")
        result = run(BAD, rom, "--frames", "1", "--diff")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CPU diverged at PC $F000", result.stderr)
        self.assertIn("compiled:", result.stderr)
        self.assertIn("reference:", result.stderr)

    def test_hotspot_fetches_use_interpreter(self):
        source = os.path.join(BUILD, "gen", "a26_banked.c")
        with open(source, encoding="utf-8") as file:
            generated = file.read()
        self.assertNotIn("static void b1_ff7(", generated)
        self.assertNotIn("static void b0_ff9(", generated)
        result = run(BANKED, "--frames", "4", "--diff")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_external_rom_and_disabled_tracing(self):
        rom = os.path.join(BUILD, "gen", "a26_fixed.a26")
        external = run(EXTERNAL, rom, "--frames", "2", "--diff", "--coverage")
        self.assertEqual(external.returncode, 0, external.stdout + external.stderr)
        self.assertIn("compiled_cycles=", external.stdout)
        self.assertNotEqual(run(EXTERNAL, "--frames", "1").returncode, 0)
        untraced = run(UNTRACED, "--frames", "2", "--diff", "--coverage")
        self.assertEqual(untraced.returncode, 0, untraced.stdout + untraced.stderr)
        match = re.search(r"compiled_cycles=(\d+)", untraced.stdout)
        self.assertIsNotNone(match, untraced.stdout)
        self.assertGreater(int(match.group(1)), 0)

    def test_recompiler_hints(self):
        rom = os.path.join(BUILD, "gen", "a26_fixed.a26")
        with tempfile.TemporaryDirectory() as directory:
            hints = os.path.join(directory, "game.toml")
            output = os.path.join(directory, "game.c")
            with open(hints, "w", encoding="utf-8") as file:
                file.write('region = "pal"\nmapper = "4k"\n')
            result = run(RECOMPILER, rom, "-o", output, "--hints", hints)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with open(output, encoding="utf-8") as file:
                self.assertIn(", 2, 1, lookup}", file.read())
            with open(hints, "w", encoding="utf-8") as file:
                file.write('region = "invalid"\n')
            self.assertNotEqual(run(RECOMPILER, rom, "-o", output, "--hints", hints).returncode, 0)

    def test_build_uses_configuration_hints(self):
        result = run(PAL, "--frames", "2", "--diff", "--coverage")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotEqual(run(PAL, "--region", "ntsc", "--frames", "1").returncode, 0)
