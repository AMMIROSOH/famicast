"""Game Boy test ROMs (blargg, mooneye) through gb_headless (gb/tests/gb_headless.c).

The ROMs come from the c-sp/game-boy-test-roms collection, one pinned and
hash-checked zip (tests/fetch_test_roms.py fetch_gb_test_roms). Every ROM in
SUITES must pass, except the ones in OPEN, which must still fail (so a fix
shows up here and moves them out). The ROMs report on the serial port or in
cartridge RAM; no picture is needed.
"""
import os
import subprocess
import unittest

from tests.fetch_test_roms import fetch_gb_test_roms

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("NES_BUILD", os.path.join(ROOT, "build"))
RUNNER = os.path.join(BUILD, "gb_headless" + (".exe" if os.name == "nt" else ""))

SUITES = [
    "blargg/cpu_instrs", "blargg/instr_timing", "blargg/mem_timing", "blargg/mem_timing-2",
    "blargg/halt_bug.gb", "mooneye-test-suite/acceptance", "mooneye-test-suite/emulator-only",
]
OTHER_MODEL = "boot state of another model (we are a DMG ABC)"
PPU = "PPU timing (G3)"
OPEN = {
    "mooneye-test-suite/acceptance/boot_div-S.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/boot_div-dmg0.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/boot_div2-S.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/boot_hwio-S.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/boot_hwio-dmg0.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/boot_regs-dmg0.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/boot_regs-mgb.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/boot_regs-sgb.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/boot_regs-sgb2.gb": OTHER_MODEL,
    "mooneye-test-suite/acceptance/ppu/intr_2_mode0_timing.gb": PPU,
    "mooneye-test-suite/acceptance/ppu/intr_2_mode0_timing_sprites.gb": PPU,
    "mooneye-test-suite/acceptance/ppu/intr_2_mode3_timing.gb": PPU,
    "mooneye-test-suite/acceptance/ppu/intr_2_oam_ok_timing.gb": PPU,
    "mooneye-test-suite/acceptance/ppu/lcdon_timing-GS.gb": PPU,
    "mooneye-test-suite/acceptance/ppu/lcdon_write_timing-GS.gb": PPU,
    "mooneye-test-suite/acceptance/ppu/stat_lyc_onoff.gb": PPU,
    "mooneye-test-suite/acceptance/ppu/vblank_stat_intr-GS.gb": PPU,
    "mooneye-test-suite/acceptance/serial/boot_sclk_align-dmgABCmgb.gb": "the serial clock's phase at boot",
    "mooneye-test-suite/acceptance/timer/rapid_toggle.gb": "interrupt one loop late (BC $FFD8, want $FFD9): "
                                                           "TAC write timing within the cycle",
    "mooneye-test-suite/emulator-only/mbc1/multicart_rom_8Mb.gb": "MBC1 multicart wiring not supported",
}


def roms(base):
    out = []
    for s in SUITES:
        p = os.path.join(base, *s.split("/"))
        if p.endswith(".gb"):
            out.append(s)
            continue
        for d, _, files in os.walk(p):
            out += [os.path.relpath(os.path.join(d, f), base).replace(os.sep, "/") for f in files if f.endswith(".gb")]
    return sorted(out)


@unittest.skipUnless(os.path.exists(RUNNER), "build gb_headless first")
class TestGbRoms(unittest.TestCase):
    def test_roms(self):
        base = fetch_gb_test_roms()
        found = roms(base)
        self.assertGreater(len(found), 100)
        for rom in found:
            with self.subTest(rom=rom):
                r = subprocess.run([RUNNER, os.path.join(base, rom), "--frames", "4000"],
                                   capture_output=True, text=True, timeout=300)
                last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr
                if rom in OPEN:
                    self.assertNotEqual(last, "result: passed", f"passes now: take it out of OPEN ({OPEN[rom]})")
                else:
                    self.assertEqual(last, "result: passed", r.stdout[-500:] + r.stderr)
        self.assertEqual(sorted(set(OPEN) - set(found)), [], "OPEN lists ROMs that are not in the suites")


if __name__ == "__main__":
    unittest.main()
