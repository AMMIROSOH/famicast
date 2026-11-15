"""Download public test ROMs into tests/roms/ (git-ignored), checking SHA-256.

Sources and licenses: docs/07-sources.md. Run: python tests/fetch_test_roms.py
"""
import hashlib
import os
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


if __name__ == "__main__":
    for n in FILES:
        print(n, "->", fetch(n))
