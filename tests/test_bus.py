"""CPU bus scheduling (runtime/nes.c): the lazily caught-up PPU must still
raise every NMI when the CPU never touches the PPU, on a tiny hand-assembled
ROM (helpers from tests/test_mappers.py)."""
import os
import unittest

from tests.test_mappers import RUNNER, ines, lda_sta, peek, put, ram_loop


@unittest.skipUnless(os.path.exists(RUNNER), "build the C runtime first (see tests/test_cpu.py)")
class TestBus(unittest.TestCase):
    def test_nmi_every_frame_from_a_ram_loop(self):
        """NMI on, then a loop that touches neither the PPU nor the APU: the
        NMI line stays high from one vblank through the next batch, which
        must still end at vblank end or the next rise is missed (one NMI)."""
        prg = bytearray(0x8000)
        code = bytes([0x78]) + lda_sta(0x40, 0x4017) + lda_sta(0x80, 0x2000)  # SEI, no frame IRQ, NMI on
        code += ram_loop(0x8000 + len(code))
        put(prg, 0x0000, code)
        put(prg, 0x0100, bytes([0xE6, 0x00, 0x40]))  # NMI: INC $00, RTI
        put(prg, 0x7FFA, bytes([0x00, 0x81, 0x00, 0x80, 0x00, 0x80]))
        self.assertGreaterEqual(peek(ines(0, prg)), 9)  # 10 frames


if __name__ == "__main__":
    unittest.main()
