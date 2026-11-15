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


if __name__ == "__main__":
    import sys
    for n in FILES:
        print(n, "->", fetch(n))
    if "--sst" in sys.argv:  # ~1 GB download, ~150 MB on disk after conversion
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(8) as ex:
            for p in ex.map(fetch_sst, range(256)):
                print(p)
