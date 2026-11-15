"""Download public test ROMs into tests/roms/ (git-ignored), checking SHA-256.

Sources and licenses: docs/07-sources.md. Run: python tests/fetch_test_roms.py
"""
import hashlib
import json
import os
import struct
import urllib.request

ROMS = os.path.join(os.path.dirname(__file__), "roms")

# name -> (sha256, [urls]). First URL is the original host (linked from the
# nesdev wiki "Emulator tests" page); the rest are mirrors with identical bytes.
FILES = {
    "nestest.nes": ("f67d55fd6b3cf0bad1cc85f1df0d739c65b53e79cecb7fea8f77ec0eadab0004", [
        "https://www.qmtpro.com/~nes/misc/nestest.nes",
        "https://raw.githubusercontent.com/christopherpow/nes-test-roms/master/other/nestest.nes"]),
    # CRLF original; the GitHub mirror has LF endings and therefore another hash
    "nestest.log": ("627c8e180b1a924dfa705c5dc6958fad7ab75a62de556173caf880ccc1337540", [
        "https://www.qmtpro.com/~nes/misc/nestest.log"]),
}


def fetch(name):
    """Return the local path of a test file, downloading it if needed."""
    sha, urls = FILES[name]
    path = os.path.join(ROMS, name)
    if os.path.exists(path):
        with open(path, "rb") as f:
            if hashlib.sha256(f.read()).hexdigest() == sha:
                return path
    os.makedirs(ROMS, exist_ok=True)
    for url in urls:
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "nes-recomp-tests"})
            data = urllib.request.urlopen(req, timeout=30).read()
        except OSError as e:
            print(f"  {url}: {e}")
            continue
        if hashlib.sha256(data).hexdigest() != sha:
            print(f"  {url}: hash mismatch, skipped")
            continue
        with open(path, "wb") as f:
            f.write(data)
        return path
    raise RuntimeError(f"could not download {name}")


# SingleStepTests (MIT): 10,000 single-instruction tests per opcode for the NES
# 2A03, with the expected bus access of every cycle. Pinned to a commit.
SST_COMMIT = "b9cfa8b287d2c5087cb49a6b64495b03041edca0"
SST_URL = "https://raw.githubusercontent.com/SingleStepTests/65x02/%s/nes6502/v1/%02x.json"


def _pack_state(st):
    out = struct.pack("<HBBBBBH", st["pc"], st["s"], st["a"], st["x"], st["y"], st["p"], len(st["ram"]))
    return out + b"".join(struct.pack("<HB", a, v) for a, v in st["ram"])


def fetch_sst(opcode):
    """Return the path of the binary form of one SingleStepTests file
    (format: see runtime/tests/cpu_tests.c), downloading it if needed."""
    path = os.path.join(ROMS, "sst", "%02x.bin" % opcode)
    if os.path.exists(path):
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    req = urllib.request.Request(SST_URL % (SST_COMMIT, opcode), headers={"User-Agent": "nes-recomp-tests"})
    tests = json.loads(urllib.request.urlopen(req, timeout=120).read())
    out = [struct.pack("<I", len(tests))]
    for t in tests:
        out.append(_pack_state(t["initial"]) + _pack_state(t["final"]))
        out.append(struct.pack("<H", len(t["cycles"])))
        out += [struct.pack("<HBB", a, v, kind == "write") for a, v, kind in t["cycles"]]
    with open(path + ".tmp", "wb") as f:
        f.write(b"".join(out))
    os.replace(path + ".tmp", path)
    return path


# blargg and other test ROMs from the christopherpow/nes-test-roms mirror,
# pinned to a commit (raw files at a commit cannot change).
NTR_COMMIT = "95d8f621ae55cee0d09b91519a8989ae0e64753b"
NTR_URL = "https://raw.githubusercontent.com/christopherpow/nes-test-roms/%s/%s"
# path -> how the ROM reports: "6000" (text + code at $6000), or the zero-page
# address of its result code ("f8", "f0"; 1 = pass, checked against the screen)
NTR_FILES = {
    **{f"ppu_vbl_nmi/rom_singles/{n}.nes": "6000" for n in (
        "01-vbl_basics", "02-vbl_set_time", "03-vbl_clear_time", "04-nmi_control", "05-nmi_timing",
        "06-suppression", "07-nmi_on_timing", "08-nmi_off_timing", "09-even_odd_frames",
        "10-even_odd_timing")},
    **{f"instr_test-v5/rom_singles/{n}.nes": "6000" for n in (
        "01-basics", "02-implied", "03-immediate", "04-zero_page", "05-zp_xy", "06-absolute",
        "07-abs_xy", "08-ind_x", "09-ind_y", "10-branches", "11-stack", "12-jmp_jsr", "13-rts",
        "14-rti", "15-brk", "16-special")},
    **{f"apu_test/rom_singles/{n}.nes": "6000" for n in (
        "1-len_ctr", "2-len_table", "3-irq_flag", "4-jitter", "5-len_timing", "6-irq_flag_timing",
        "7-dmc_basics", "8-dmc_rates")},
    **{f"apu_reset/{n}.nes": "6000" for n in (
        "4015_cleared", "4017_timing", "4017_written", "irq_flag_cleared", "len_ctrs_enabled",
        "works_immediately")},
    **{f"cpu_interrupts_v2/rom_singles/{n}.nes": "6000" for n in (
        "1-cli_latency", "2-nmi_and_brk", "3-nmi_and_irq", "4-irq_and_dma", "5-branch_delays_irq")},
    **{f"blargg_apu_2005.07.30/{n}.nes": "f0" for n in (
        "01.len_ctr", "02.len_table", "03.irq_flag", "04.clock_jitter", "05.len_timing_mode0",
        "06.len_timing_mode1", "07.irq_flag_timing", "08.irq_timing", "09.reset_timing",
        "10.len_halt_timing", "11.len_reload_timing")},
    "cpu_dummy_reads/cpu_dummy_reads.nes": "6000",
    "cpu_dummy_writes/cpu_dummy_writes_oam.nes": "6000",
    "cpu_dummy_writes/cpu_dummy_writes_ppumem.nes": "6000",
    "sprdma_and_dmc_dma/sprdma_and_dmc_dma.nes": "6000",
    "sprdma_and_dmc_dma/sprdma_and_dmc_dma_512.nes": "6000",
    "ppu_open_bus/ppu_open_bus.nes": "6000",
    "ppu_read_buffer/test_ppu_read_buffer.nes": "6000",
    "oam_read/oam_read.nes": "6000",
    "oam_stress/oam_stress.nes": "6000",
    **{f"blargg_ppu_tests_2005.09.15b/{n}.nes": "f0" for n in (
        "palette_ram", "sprite_ram", "vbl_clear_time", "vram_access")},
    **{f"sprite_hit_tests_2005.10.05/{n}.nes": "f8" for n in (
        "01.basics", "02.alignment", "03.corners", "04.flip", "05.left_clip", "06.right_edge",
        "07.screen_bottom", "08.double_height", "09.timing_basics", "10.timing_order",
        "11.edge_timing")},
    **{f"sprite_overflow_tests/{n}.nes": "f8" for n in (
        "1.Basics", "2.Details", "3.Timing", "4.Obscure", "5.Emulator")},
    **{f"vbl_nmi_timing/{n}.nes": "f8" for n in (
        "1.frame_basics", "2.vbl_timing", "3.even_odd_frames", "4.vbl_clear_timing")},
}


def fetch_ntr(path):
    """Return the local path of a test ROM from the pinned mirror."""
    local = os.path.join(ROMS, "ntr", *path.split("/"))
    if not os.path.exists(local):
        os.makedirs(os.path.dirname(local), exist_ok=True)
        req = urllib.request.Request(NTR_URL % (NTR_COMMIT, path), headers={"User-Agent": "nes-recomp-tests"})
        data = urllib.request.urlopen(req, timeout=60).read()
        with open(local + ".tmp", "wb") as f:
            f.write(data)
        os.replace(local + ".tmp", local)
    return local


if __name__ == "__main__":
    import sys
    for n in FILES:
        print(n, "->", fetch(n))
    for p in NTR_FILES:
        print(p, "->", fetch_ntr(p))
    if "--sst" in sys.argv:  # ~1 GB download, ~150 MB on disk after conversion
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(8) as ex:
            for p in ex.map(fetch_sst, range(256)):
                print(p)
