"""PRG banking for the recompiler (docs/02 "Banked mappers").

Recompiled code is identified by (8 KB PRG bank, CPU window); window w covers
$8000 + w * $2000. The runtime looks code up by the bank mapped at that
moment, so the layout below is only where static exploration starts: a wrong
guess costs coverage (the interpreter runs that code), never correctness.
"""


def assumed_layout(cart):
    """{window: bank} the decoder may assume: the power-on mapping of the
    windows a mapper normally keeps fixed (runtime/cart.c)."""
    n8 = len(cart.prg) // 0x2000
    if cart.mapper in (0, 3):                  # NROM, CNROM: no PRG switching
        return {w: w % n8 for w in range(4)}  # 16 KB mirrors: 0,1,0,1
    if cart.mapper in (1, 2):                  # MMC1 (mode 3), UxROM: last 16 KB fixed
        return {2: n8 - 2, 3: n8 - 1}
    if cart.mapper == 4:                       # MMC3: $E000 fixed, $C000 in mode 0
        return {2: n8 - 2, 3: n8 - 1}
    if cart.mapper in (7, 11, 66):             # 32 KB switching, bank 0 at power-on
        return {w: w % n8 for w in range(4)}
    raise ValueError(f"mapper {cart.mapper} not supported by the recompiler")


def unit_windows(cart):
    """How many 8 KB windows switch together: code may run across windows
    inside one unit (consecutive banks), never across units."""
    if cart.mapper in (7, 11, 66):
        return 4   # 32 KB
    if cart.mapper in (1, 2):
        return 2   # 16 KB (MMC1 32 KB mode = two 16 KB units with consecutive banks)
    return 1       # MMC3: 8 KB windows switch independently


def window_of(addr):
    return (addr - 0x8000) >> 13


def view(cart, layout):
    """64 KB CPU view with the given {window: bank} mapping (others zero)."""
    mem = bytearray(0x10000)
    for w, b in layout.items():
        mem[0x8000 + w * 0x2000:0xA000 + w * 0x2000] = cart.prg[b * 0x2000:(b + 1) * 0x2000]
    return mem
