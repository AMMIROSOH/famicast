# Atari 2600 verification log

What was checked, how, and the result. Newest first. A claim without a
reproducible check is not "verified".

Checks of shared parts: [docs/verification-log.md](../../../docs/verification-log.md).

## 2026-11-22 · Atari 2600: interpreter, recompiler, release kit
- The Atari tests cover cartridge banking, bus and RIOT behavior, save states,
  recording and replay, and recompiler lockstep and fallback.
- Adventure (USA and Europe), Combat (Europe), and Asteroids matched the
  interpreter over 600 scripted frames each.
- The shared NMOS 6502 instruction and bus-cycle vectors were checked,
  including decimal arithmetic. NES CPU and recompiler regressions passed.
- A Windows release kit was built and its packaged interpreter and recompiler
  were smoke-tested.
