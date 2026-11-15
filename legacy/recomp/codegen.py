# -*- coding: utf-8 -*-
"""x86_64 codegen for the lifted 6502 program (GAS, Intel syntax).

State ABI (matches docs/02-architecture.md):
  A = rax/al, X = rdx/dl, Y = rcx/cl, S = r8/r8b (always zero-extended)
  P byte in memory: Z=0x01 C=0x02 I=0x04 D=0x08 B=0x10 V=0x40 N=0x80
  ram base = r9, &rt_cpu = r15 (loaded once per block)
  temps: rbx, rsi, rdi, r10, r11
  PC: folded as immediates; stored to rt_cpu.pc only at block exits.

Every 6502 instruction is followed by the NMI latch check (3 x86 instrs),
so pending NMI is taken at the next instruction boundary — the 2A03 takes
interrupts only between instructions.
"""

from . import ops

# P bits
Z, C, I, D, B, V, N = 0x01, 0x02, 0x04, 0x08, 0x10, 0x40, 0x80


class Asm:
    def __init__(self):
        self.lines = []
        self._tmp = 0

    def emit(self, *ls):
        for l in ls:
            self.lines.append(l)

    def c(self, s):
        # gas (this toolchain) does NOT treat ';' as a comment in intel mode
        self.lines.append(f"\t# {s}")

    def label(self, name):
        self.lines.append(f"{name}:")

    def text(self, s):
        self.lines.append(f"\t{s}")

    def render(self):
        return "\n".join(self.lines) + "\n"


class Codegen:
    def __init__(self, ram, blocks):
        self.ram = ram
        self.blocks = blocks
        self.a = Asm()
        self.block_names = {}      # block index -> rt_bN
        self.label_of_pc = {}      # start pc -> .Lb label
        self.block_of_pc = {}      # any instr pc -> block index (for targets)

    # ---------- shared sequences ----------
    def _mat_nzcv(self):
        """P <- (P & 0x3C) | N | Z | C<<1 | V.  Pre: r11b = result; x86 ZF set
        by `test r11b, r11b`; dil = 6502 C already shifted into bit 1;
        r10b = V (0/1).  setz MUST precede the N-mask `and` (it sets ZF).
        See docs/03 — flag-clobber ordering."""
        a = self.a
        a.text("movzx r14d, r11b")
        a.text("setz sil")                   # Z, before the N-mask and clobbers ZF
        a.text("and r14b, 0x80")             # N
        a.text("or sil, dil")                # C<<1
        a.text("or sil, r10b")               # V
        a.text("or sil, r14b")               # N
        a.text("and sil, 0xFF")
        a.text("and BYTE PTR [r15+4], 0x3C")
        a.text("or BYTE PTR [r15+4], sil")

    def _mat_nz(self, reg8):
        """P <- (P & 0x7E) | Z | N from reg8.  C,V,B,I,D kept.
        setz runs BEFORE the N-mask `and r10b,0x80` — that and sets ZF, and
        the old ordering (test; and; setz) made Z = !(reg&0x80) instead of
        reg==0.  See docs/03 — flag-clobber ordering."""
        a = self.a
        a.text(f"movzx r11d, {reg8}")
        a.text("test r11b, r11b")
        a.text("setz sil")                   # Z, before the and below clobbers ZF
        a.text(f"movzx r10d, {reg8}")
        a.text("and r10b, 0x80")             # N
        a.text("or sil, r10b")
        a.text("and sil, 0xFF")
        a.text("and BYTE PTR [r15+4], 0x7E")
        a.text("or BYTE PTR [r15+4], sil")

    def _bcd_guard(self):
        self.a.text("movzx rbx, BYTE PTR [r15+4]")
        self.a.text("test bl, 0x08")
        self.a.text("jnz .L_bcd_trap")

    def _nmi_check(self, ins, pc_next):
        self.a.c(f"nmi latch after: {ins}")
        self.a.text(f"mov WORD PTR [r15+8], {pc_next}")
        self.a.text("movzx r10d, BYTE PTR [r15+5]")
        self.a.text("test r10b, r10b")
        self.a.text("jnz .L_nmi_entry")

    # ---------- addressing modes -> EA ----------
    def _ea(self, ins):
        """Leave effective address in r11 (zero-extended)."""
        m, op = ins.mode, ins.operand
        if m == "ZP":
            self.a.text(f"mov r11d, {op}")  # GAS: no movzx-from-imm
        elif m == "ZPX":
            self.a.text(f"movzx r11d, dl")
            self.a.text(f"add r11b, {op if op < 128 else op - 256}")
        elif m == "ZPY":
            self.a.text(f"movzx r11d, cl")
            self.a.text(f"add r11b, {op if op < 128 else op - 256}")
        elif m == "ABS":
            self.a.text(f"mov r11d, {op}")  # GAS: no movzx-from-imm
        elif m == "ABSX":
            self.a.text(f"movzx r11d, WORD PTR [r9+{op}]")
            self.a.text("add r11w, dx")
        elif m == "ABSY":
            self.a.text(f"movzx r11d, WORD PTR [r9+{op}]")
            self.a.text("add r11w, cx")
        elif m == "ZPI":
            # (zp,X) indirect: pointer word at ((op + X) & 0xFF), with the
            # classic zero-page wrap: if (op + X) == 0xFF the word is at $0000
            # (the hi byte always comes from $0000).
            a = self.a
            a.text("movzx r11d, dl")
            a.text(f"add r11b, {op if op < 128 else op - 256}")
            a.text("movzx r10d, WORD PTR [r9+r11]")
            a.text("cmp r11b, 0xFF")
            a.text("jne .Lzpi_ok")
            a.text("movzx r10d, WORD PTR [r9+0x0000]")
            a.label(".Lzpi_ok")
            a.text("movzx r11d, WORD PTR [r9+r10]")
        elif m == "ABSI":
            # (abs),Y: target = word at `op` plus Y.  The one exception is
            # 0x6C JMP (abs) - same mode tag, no index - detected via mn.
            a = self.a
            a.text(f"movzx r11d, WORD PTR [r9+{op}]")
            if ins.mn != "JMP":
                a.text("add r11w, cx")
        elif m == "IMM":
            pass
        else:
            raise NotImplementedError(m)

    def _mem(self, ea_reg="r11", width=8):
        return f"BYTE PTR [r9+{ea_reg}]" if width == 8 else f"WORD PTR [r9+{ea_reg}]"

    # ---------- load / store / op helpers ----------
    def _load_src(self, ins, dst8):
        """Load the source operand byte into an 8-bit register or r11."""
        m, op = ins.mode, ins.operand
        if m == "IMM":
            self.a.text(f"mov {dst8}, {op}")
        elif m == "ZP":
            self.a.text(f"mov {dst8}, BYTE PTR [r9+{op}]")
        elif m in ("ZPX", "ZPY"):
            self._ea(ins)
            self.a.text(f"mov {dst8}, {self._mem()}")
        elif m == "ABS":
            self.a.text(f"mov {dst8}, BYTE PTR [r9+{op}]")
        elif m in ("ABSX", "ABSY", "ABSI"):
            self._ea(ins)
            self.a.text(f"mov {dst8}, {self._mem()}")
        elif m == "ZPI":
            self._ea(ins)
            self.a.text(f"mov {dst8}, {self._mem()}")

    def _store_dst(self, ins, src8):
        m, op = ins.mode, ins.operand
        if m == "ZP":
            self.a.text(f"mov BYTE PTR [r9+{op}], {src8}")
        elif m in ("ZPX", "ZPY"):
            self._ea(ins)
            self.a.text(f"mov {self._mem()}, {src8}")
        elif m == "ABS":
            self.a.text(f"mov BYTE PTR [r9+{op}], {src8}")
        elif m in ("ABSX", "ABSY", "ABSI"):
            self._ea(ins)
            self.a.text(f"mov {self._mem()}, {src8}")
        elif m == "ZPI":
            self._ea(ins)
            self.a.text(f"mov {self._mem()}, {src8}")

    # ---------- per-mnemonic emission ----------
    def emit_instr(self, ins):
        a = self.a
        mn, m, op = ins.mn, ins.mode, ins.operand
        self.a.c(f"${ins.pc:04X}  {ins}")

        if m == "IMP":
            self._emit_imp(mn, ins)
            return

        if mn in ("LDA", "LDX", "LDY"):
            dst = {"LDA": "al", "LDX": "dl", "LDY": "cl"}[mn]
            self._load_src(ins, dst)
            self._mat_nz(dst)                # materialize N,Z (order-safe)
            return

        if mn in ("STA", "STX", "STY"):
            src = {"STA": "al", "STX": "dl", "STY": "cl"}[mn]
            self._store_dst(ins, src)
            return

        if mn in ("AND", "ORA", "EOR"):
            x86 = {"AND": "and", "ORA": "or", "EOR": "xor"}[mn]
            if m == "IMM":
                a.text(f"{x86} al, {op if op < 128 else op - 256}")  # imm8 signed
            else:
                self._ea(ins)
                a.text(f"movzx r10d, {self._mem()}")
                a.text(f"{x86} al, r10b")
            self._mat_nz("al")               # materialize N,Z (order-safe)
            return

        if mn in ("ADC", "SBC"):
            # Internal ALU: ADC computes A + B + C; SBC computes A + ~B + C.
            # The 16-bit sum is <= 511.  rbx keeps the ORIGINAL operand byte
            # for the V rule below.
            if m == "IMM":
                a.text(f"mov r11d, {op}")  # imm32 zext (NOT sign-ext: B hi-bit)
            else:
                self._ea(ins)
                a.text(f"movzx r11d, {self._mem()}")
            if mn == "SBC":
                a.text("movzx rbx, r11b")   # rbx = B
                a.text("movzx r10d, BYTE PTR [r15+4]")
                a.text("and r10b, 0x02")
                a.text("shr r10b, 1")        # r10b = C
                a.text("not r11b")           # r11b = ~B
                a.text("add r11w, r10w")     # r11w = ~B + C (exact in 16 bits;
                                              # the 6502 adds ~B, NOT -B)
            else:
                a.text("movzx rbx, r11b")   # rbx = B
                a.text("movzx r10d, BYTE PTR [r15+4]")
                a.text("and r10b, 0x02")
                a.text("shr r10b, 1")        # r10b = C
                a.text("add r11w, r10w")     # r11w = B + C
            a.text("movzx r14d, r10b")      # r14b = C (keep for c7in)
            a.text("movzx r10d, al")        # r10 = a
            a.text("add r11w, ax")          # r11w = sum (<= 511)
            a.text("mov al, r11b")          # A = result
            a.text("cmp r11d, 0x100")       # C6502 = (sum > 0xFF)
            a.text("seta dil")              # dil = C6502 = carry OUT of bit 7
            a.text("movzx r12d, dil")       # r12b = c7out
            a.text("shl dil, 1")            # C -> internal bit 1
            # V = carry-in(bit 7) XOR carry-out(bit 7) - the hardware rule,
            # correct for both ADC and SBC.  The ~(a^b)&(a^r) shortcut is
            # SBC-incorrect (the extra borrow shifts the overflow boundary;
            # 65536/131072 counterexamples, e.g. A=0 B=0 C=0; see docs/03).
            a.text("movzx r13d, r10b")      # r13 = a
            a.text("and r13w, 0x7F")
            if mn == "SBC":
                a.text("movzx r10d, bl")    # X7 = ~B & 0x7F
                a.text("and r10w, 0x7F")
                a.text("not r10w")
                a.text("and r10w, 0x7F")
            else:
                a.text("movzx r10d, bl")    # X7 = B & 0x7F
                a.text("and r10w, 0x7F")
            a.text("add r13w, r10w")
            a.text("add r13w, r14w")        # + C
            a.text("shr r13b, 7")           # r13b = c7in
            a.text("movzx r10d, r12b")
            a.text("xor r10b, r13b")        # V -> r10b
            a.text("movzx r11d, al")
            a.text("test r11b, r11b")
            self._mat_nzcv()
            self._bcd_guard()
            return

        if mn in ("CMP", "CPX", "CPY"):
            # N,Z,C set from (dst - src); V unchanged.
            dst = {"CMP": "al", "CPX": "dl", "CPY": "cl"}[mn]
            if m == "IMM":
                a.text(f"movzx r11d, {dst}")
                a.text(f"sub r11b, {op if op < 128 else op - 256}")  # imm8 signed
            else:
                self._ea(ins)
                a.text(f"movzx r11d, {self._mem()}")
                a.text(f"sub r11b, {dst}")
            a.text("setnc dil")             # C6502 = !borrow — IMMEDIATELY after
                                            # the sub; `test` below clears CF
            a.text("shl dil, 1")            # C -> bit 1
            a.text("test r11b, r11b")
            a.text("setz sil")              # Z
            a.text("or sil, dil")
            a.text("movzx r14d, r11b")      # N = sign bit of (dst - src)
            a.text("and r14b, 0x80")
            a.text("or sil, r14b")
            a.text("and sil, 0xFF")
            a.text("and BYTE PTR [r15+4], 0x7C")   # clear N,Z,C (V,B,I,D kept)
            a.text("or BYTE PTR [r15+4], sil")
            return

        if mn == "BIT":
            # Z = !(A & M); N, V from M bit 7/6
            self._ea(ins)
            a.text(f"movzx r11d, {self._mem()}")
            a.text("movzx r10d, r11b")     # r10 = M (for N|V)
            a.text("and r11b, al")          # A & M
            a.text("test r11b, r11b")
            a.text("setz sil")
            a.text("and r10b, 0xC0")
            a.text("and BYTE PTR [r15+4], 0x3F")   # clear Z,N (V re-OR'd); C,I,D,B kept
            a.text("or BYTE PTR [r15+4], sil")
            a.text("or BYTE PTR [r15+4], r10b")
            return

        if mn in ("DEC", "INC", "ASL", "ROL", "LSR", "ROR"):
            self._emit_shift(mn, ins)
            return

        if mn in ("JSR", "JMP"):
            return  # handled by block exit

        if mn in ops.BRANCH_OPS:
            return  # handled by block exit

        raise NotImplementedError(f"{mn} {m}")

    def _emit_imp(self, mn, ins):
        a = self.a
        # accumulator shift forms 0x0A/0x2A/0x4A/0x6A (MOS: ASL A, ROL A,
        # LSR A, ROR A) — mode IMP routes here before the shift table
        if mn in ("DEC", "INC", "ASL", "ROL", "LSR", "ROR"):
            self._emit_shift(mn, ins)
            return
        if mn == "CLC":
            a.text("and BYTE PTR [r15+4], 0xFD")
        elif mn == "SEC":
            a.text("or BYTE PTR [r15+4], 0x02")
        elif mn == "CLI":
            a.text("and BYTE PTR [r15+4], 0xFB")
        elif mn == "SEI":
            a.text("or BYTE PTR [r15+4], 0x04")
        elif mn == "CLV":
            a.text("and BYTE PTR [r15+4], 0xBF")
        elif mn == "SEV":
            a.text("or BYTE PTR [r15+4], 0x40")
        elif mn == "CLD":
            a.text("and BYTE PTR [r15+4], 0xF7")
        elif mn == "SED":
            a.text("or BYTE PTR [r15+4], 0x08")
        elif mn in ("TAX", "TAY", "TXA", "TYA"):
            dst = {"TAX": "dl", "TAY": "cl", "TXA": "al", "TYA": "al"}[mn]
            a.text({"TAX": "movzx edx, al", "TAY": "movzx ecx, al",
                    "TXA": "movzx eax, dl", "TYA": "movzx eax, cl"}[mn])
            self._nz_h(dst)                   # N,Z set from destination
        elif mn == "TSX":
            a.text("movzx dl, r8b")            # X <- S  (was inverted!)
            self._nz_h("dl")                 # N,Z from new X; C,V unchanged
        elif mn == "TXS":
            a.text("movzx r8d, dl")           # S <- X
            self._nz_h("dl")                 # N,Z from X; C,V unchanged
        elif mn == "INX":
            a.text("inc dl")
            self._nz_h("dl")
        elif mn == "INY":
            a.text("inc cl")
            self._nz_h("cl")
        elif mn == "DEX":
            a.text("dec dl")
            self._nz_h("dl")
        elif mn == "DEY":
            a.text("dec cl")
            self._nz_h("cl")
        # 6502 stack lives at 0x100..0x1FF (docs/02): push = [0x100+S] <- v;
        # S <- S-1 (pre-decrement; first push after reset lands at $01FF);
        # pop = v <- [0x100+((S+1)&0xFF)]; S <- S+1 (page wraps mod 256).
        # After JSR/BRK/NMI: [S+1]=PCL, [S+2]=PCH, [S+3]=P.
        elif mn == "PHA":
            self._stk_push("al")
        elif mn == "PLA":
            self._stk_pop("al")
            self._mat_nz("al")
        elif mn == "PHX":
            self._stk_push("dl")
        elif mn == "PLX":
            self._stk_pop("dl")
        elif mn == "PHY":
            self._stk_push("cl")
        elif mn == "PLY":
            self._stk_pop("cl")
        elif mn == "PHP":
            a.text("movzx r10d, BYTE PTR [r15+4]")
            a.text("or r10b, 0x10")
            self._stk_push("r10b")
        elif mn == "PLP":
            self._stk_pop("r11b")
            a.text("mov r10b, r11b")
            a.text("and r10b, 0xDF")      # B <- 0 (D restored from stack)
            a.text("or r10b, 0x20")       # bit 5 <- 1 (6502 pull rule)
            a.text("mov BYTE PTR [r15+4], r10b")
        elif mn in ("NOP",):
            a.text("nop")
        elif mn in ("RTS", "RTI", "BRK", "JMP"):
            self._emit_exit(mn, ins)
        else:
            raise NotImplementedError(mn)

    def _nz_h(self, reg8):
        """Set N,Z from an 8-bit reg; C,V unchanged (mask 0x7E).  Used by
        INX/INY/DEX/DEY/INC/DEC (mem)/TSX/TXS/TAX/TAY/TXA/TYA.  (The MOS 6502
        does NOT clear C or V on these - an earlier 0xBD proposal was wrong.)"""
        a = self.a
        a.text(f"movzx r10d, {reg8}")
        a.text("test r10b, r10b")
        a.text("setz sil")
        a.text("movzx r11d, r10b")
        a.text("and r11b, 0x80")
        a.text("or sil, r11b")
        a.text("and sil, 0xFF")
        a.text("and BYTE PTR [r15+4], 0x7E")
        a.text("or BYTE PTR [r15+4], sil")

    def _stk_push(self, v8):
        """6502 push: [0x100+S] <- v; S <- S-1 (pre-decrement store)."""
        self.a.text(f"mov BYTE PTR [r9+r8+0x100], {v8}")
        self.a.text("dec r8b")

    def _stk_pop(self, dst8):
        """6502 pop: v <- [0x100+((S+1)&0xFF)]; S <- S+1. The +1 wraps mod 256
        (S=0xFF pops $0100), so the index is computed in a temp; an 8-bit
        register is not encodable as an x86 address index."""
        a = self.a
        a.text("movzx r10d, r8b")
        a.text("add r10b, 1")
        a.text(f"mov {dst8}, BYTE PTR [r9+r10+0x100]")
        a.text("inc r8b")

    def _stk_pop16(self):
        """r11w <- PCL|(PCH<<8) read from 0x100+((S+1)&0xFF) and
        0x100+((S+2)&0xFF) with the 256-byte page wrap; S += 2."""
        a = self.a
        a.text("movzx r10d, r8b")
        a.text("add r10b, 1")
        a.text("movzx r11d, BYTE PTR [r9+r10+0x100]")   # PCL
        a.text("movzx r10d, r8b")
        a.text("add r10b, 2")
        a.text("movzx r13d, BYTE PTR [r9+r10+0x100]")   # PCH
        a.text("shl r13d, 8")
        a.text("or r11w, r13w")
        a.text("inc r8b")
        a.text("inc r8b")

    def _mat_shift(self, res8):
        """Shift-family flags (ASL/LSR/ROL/ROR): N,Z from result, C from the
        pre-shifted dil (C<<1), V unchanged.  Pre: dil = C<<1, res8 = result."""
        a = self.a
        a.text(f"movzx r14d, {res8}")
        a.text("and r14b, 0x80")            # N
        a.text(f"test {res8}, {res8}")
        a.text("setz sil")
        a.text("or sil, dil")               # C<<1
        a.text("or sil, r14b")
        a.text("and sil, 0xFF")
        a.text("and BYTE PTR [r15+4], 0x3C")
        a.text("or BYTE PTR [r15+4], sil")

    def _emit_shift(self, mn, ins):
        a = self.a
        acc = mn in ("LSR", "ROR", "ASL", "ROL") and ins.mode == "IMP"
        if acc:
            # operate on al directly
            if mn == "ASL":
                a.text("test al, 0x80")
                a.text("setc dil")
                a.text("shl dil, 1")
                a.text("shl al, 1")
            elif mn == "LSR":
                a.text("test al, 1")
                a.text("setc dil")
                a.text("shr al, 1")
            elif mn == "ROL":
                a.text("movzx r10d, BYTE PTR [r15+4]")
                a.text("and r10b, 0x02")
                a.text("shr r10b, 1")
                a.text("test al, 0x80")
                a.text("setc dil")
                a.text("shl dil, 1")
                a.text("shl al, 1")
                a.text("or al, r10b")
            elif mn == "ROR":
                a.text("movzx r10d, BYTE PTR [r15+4]")
                a.text("and r10b, 0x02")
                a.text("shr r10b, 1")
                a.text("shl r10b, 7")
                a.text("test al, 1")
                a.text("setc dil")
                a.text("shl dil, 1")
                a.text("shr al, 1")
                a.text("or al, r10b")
            self._mat_shift("al")
            return
        # memory form: r11 = EA
        self._ea(ins)
        if ins.mode == "ZPX":
            # (zp,X) RMW page-cross: +1 when (op + X) >= 0x100.  Data-
            # dependent, so CF from the _ea add feeds a runtime penalty
            # (driver consumes rt_extra; nothing added in the decoder).
            a.text("adc DWORD PTR [rip + rt_extra], 0")
        mem = self._mem()
        if mn == "ASL":
            a.text(f"movzx r11d, {mem}")
            a.text("test r11b, 0x80")
            a.text("setc dil")
            a.text("shl dil, 1")
            a.text("shl r11b, 1")
            self._mat_shift("r11b")
            a.text(f"mov {mem}, r11b")
        elif mn == "LSR":
            a.text(f"movzx r11d, {mem}")
            a.text("test r11b, 1")
            a.text("setc dil")
            a.text("shr r11b, 1")
            self._mat_shift("r11b")
            a.text(f"mov {mem}, r11b")
        elif mn == "ROL":
            a.text(f"movzx r11d, {mem}")
            a.text("movzx rbx, r11b")
            a.text("movzx r10d, BYTE PTR [r15+4]")
            a.text("and r10b, 0x02")
            a.text("shr r10b, 1")
            a.text("test rbx, 0x80")
            a.text("setc dil")
            a.text("shl dil, 1")
            a.text("shl r11b, 1")
            a.text("or r11b, r10b")
            self._mat_shift("r11b")
            a.text(f"mov {mem}, r11b")
        elif mn == "ROR":
            a.text(f"movzx r11d, {mem}")
            a.text("movzx rbx, r11b")
            a.text("movzx r10d, BYTE PTR [r15+4]")
            a.text("and r10b, 0x02")
            a.text("shr r10b, 1")
            a.text("shl r10b, 7")
            a.text("test rbx, 1")
            a.text("setc dil")
            a.text("shl dil, 1")
            a.text("shr r11b, 1")
            a.text("or r11b, r10b")
            self._mat_shift("r11b")
            a.text(f"mov {mem}, r11b")
        elif mn in ("DEC", "INC"):
            a.text(f"movzx r11d, {mem}")
            a.text("dec r11b" if mn == "DEC" else "inc r11b")
            a.text(f"mov {mem}, r11b")
            self._nz_h("r11b")

    # ---------- block emission ----------
    def build(self):
        a = self.a
        a.emit(".intel_syntax noprefix")
        a.emit(".section .text")
        a.emit(".globl rt_nmi_entry")
        a.c("=== generated by nes/src/recomp/codegen.py — do not edit ===")
        a.c("NMI entry: push PC hi, PC lo, P|I (6502 pre-decrement stores),")
        a.c("then set pc to the NMI vector ($FFFA-$FFFB; $7FFC is the IRQ vector);")
        a.c("the DRIVER dispatches the first handler block via the normal loop")
        a.label("rt_nmi_entry")
        a.text("lea r9, QWORD PTR [rip + rt_ram]")   # address, NOT load!
        a.text("lea r15, QWORD PTR [rip + rt_cpu]")  # &struct, NOT first 8 bytes
        a.text("movzx r8d, BYTE PTR [r15+3]")     # S
        a.text("movzx r11d, WORD PTR [r15+8]")    # PC
        a.text("movzx r10d, r11w")
        a.text("shr r10d, 8")
        self._stk_push("r10b")                     # [0x100+S] <- PC hi
        a.text("mov r10b, r11b")
        self._stk_push("r10b")                     # PC lo
        a.text("movzx r10d, BYTE PTR [r15+4]")
        a.text("or r10b, 0x04")                   # I flag (hardware interrupt)
        self._stk_push("r10b")                     # P|I
        a.text("mov BYTE PTR [r15+3], r8b")        # commit S (3 pre-decrement pushes above)
        a.text("or BYTE PTR [r15+4], 0x04")        # live I bit (hw sets it on NMI entry)
        a.text("movzx r11d, WORD PTR [r9+0xFFFA]")  # NMI vector $FFFA-$FFFB (in-bank; $7FFC is IRQ)
        a.text("mov WORD PTR [r15+8], r11w")        # pc = NMI vec; the driver's normal
        a.text("ret")                               # loop dispatches the first handler
                                                    # block, so its cycles are credited
        a.c("RTS/RTI/JMP(abs,ind)/NMI dispatch: r11 = target pc")
        a.label(".L_dispatch")
        a.text("lea r10, QWORD PTR [rip + rt_jmp]")  # table base, not entry [0]
        a.text("jmp QWORD PTR [r10+r11*8]")
        a.label(".L_bcd_trap")
        a.text("xor eax, eax")
        a.text("jmp rt_bcd_trap")

        n = len(self.blocks)
        for i, blk in enumerate(self.blocks):
            self.block_names[i] = f"rt_b{i}"
            self.label_of_pc[blk["start"]] = f".Lb{i}"
            for ins in blk["instrs"]:
                self.block_of_pc.setdefault(ins.pc, i)

        for i, blk in enumerate(self.blocks):
            a.c(f"b{i}: ${blk['start']:04X}  ({len(blk['instrs'])} instrs, {blk['cycles']} cyc)")
            a.emit(f".globl {self.block_names[i]}")  # g_entries in gen_blocks.h
            a.label(self.block_names[i])
            a.label(self.label_of_pc[blk["start"]])
            a.text("lea r9, QWORD PTR [rip + rt_ram]")   # address, NOT load!
            a.text("lea r15, QWORD PTR [rip + rt_cpu]")  # &struct, NOT first 8 bytes
            a.text("movzx eax, BYTE PTR [r15+0]")
            a.text("movzx edx, BYTE PTR [r15+1]")
            a.text("movzx ecx, BYTE PTR [r15+2]")
            a.text("movzx r8d, BYTE PTR [r15+3]")
            last = blk["instrs"][-1]
            a.label(f".Lep{i}")
            for ins in blk["instrs"]:
                if ins is last and ins.mn in ("RTS", "RTI", "BRK", "JMP", "JSR"):
                    continue  # handled by the exit path below
                if ops.is_ppu_io(ins.mn, ins.mode, ins.operand):
                    a.c(f"PPU I/O stub: {ins} (driver performs the access)")
                    a.text("nop")
                else:
                    self.emit_instr(ins)
                # NMI latch + PC store after every instruction; a pending NMI
                # skips straight to the epilogue (driver takes it at boundary)
                self.a.c(f"after: {ins}")
                self.a.text(f"mov WORD PTR [r15+8], {ins.pc + ins.size}")
                self.a.text("movzx r10d, BYTE PTR [r15+5]")
                self.a.text("test r10b, r10b")
                self.a.text(f"jnz .Lep{i}")
            # epilogue: store A/X/Y/S, then exit. Every block RETURNS to the
            # driver (which advances cycles/PPU and re-dispatches).
            a.text("mov BYTE PTR [r15+0], al")
            a.text("mov BYTE PTR [r15+1], dl")
            a.text("mov BYTE PTR [r15+2], cl")
            a.text("mov BYTE PTR [r15+3], r8b")
            if last.mn == "RTS":
                self._stk_pop16()                   # r11w = (pop16)
                a.text("inc r11w")                   # RTS: pc = (pop16) + 1
                a.text("mov BYTE PTR [r15+3], r8b")  # commit S (pop16 did S+=2; the
                # epilogue store above ran BEFORE this pop)
                a.text("mov WORD PTR [r15+8], r11w")
                a.text("ret")
            elif last.mn == "RTI":
                self._stk_pop("r11b")                # P <- [0x100+S+1]
                a.text("and r11b, 0xDF")      # B <- 0 (D restored from stack)
                a.text("or r11b, 0x20")       # bit 5 <- 1 (6502 pull rule)
                a.text("mov BYTE PTR [r15+4], r11b")
                self._stk_pop16()                   # r11w = (pop16)
                a.text("mov BYTE PTR [r15+3], r8b")  # commit S (pop+pop16 did S+=3)
                a.text("mov WORD PTR [r15+8], r11w")
                a.text("ret")
            elif last.mn == "BRK":
                pc2 = last.pc + 2
                a.text(f"mov r10b, {(pc2 >> 8) if (pc2 >> 8) < 128 else (pc2 >> 8) - 256}")
                self._stk_push("r10b")             # [0x100+S] <- PC+2 hi
                a.text(f"mov r10b, {pc2 & 0xFF if (pc2 & 0xFF) < 128 else (pc2 & 0xFF) - 256}")
                self._stk_push("r10b")             # PC+2 lo
                a.text("movzx r10d, BYTE PTR [r15+4]")
                a.text("or r10b, 0x10")
                self._stk_push("r10b")             # P|B
                a.text("mov BYTE PTR [r15+3], r8b")  # commit S (3 pushes)
                a.text("movzx r10d, WORD PTR [r9+0xFFFC]")  # BRK vector = IRQ vector $FFFC
                a.text("mov WORD PTR [r15+8], r10w")
                a.text("ret")
            elif last.mn == "JMP":
                if last.mode == "ABS":
                    a.text(f"mov WORD PTR [r15+8], {last.operand}")
                else:  # ABSI: JMP (abs) - the word at `operand` IS the target
                    a.text(f"movzx r11d, WORD PTR [r9+{last.operand}]")
                    a.text("mov WORD PTR [r15+8], r11w")
                a.text("ret")
            elif last.mn == "JSR":
                pc2 = last.pc + 2
                a.text(f"mov r11b, {(pc2 >> 8) if (pc2 >> 8) < 128 else (pc2 >> 8) - 256}")
                self._stk_push("r11b")             # [0x100+S] <- PC+2 hi (hi first)
                a.text(f"mov r11b, {pc2 & 0xFF if (pc2 & 0xFF) < 128 else (pc2 & 0xFF) - 256}")
                self._stk_push("r11b")             # PC+2 lo
                a.text("mov BYTE PTR [r15+3], r8b")  # commit S (2 pushes)
                a.text(f"mov WORD PTR [r15+8], {last.operand}")
                a.text("ret")
            elif last.mn in ops.BRANCH_OPS:
                tgt = (last.pc + last.size + last.operand) & 0xFFFF
                bit, j = ops.BRANCH_TEST[last.mn]   # (P-bit, jz/jnz)
                a.text(f"test BYTE PTR [r15+4], {bit}")
                a.text(f"{j} .Lt{i}")
                a.text("ret")                       # not taken: pc = fallthrough
                a.label(f".Lt{i}")
                a.text(f"mov WORD PTR [r15+8], {tgt}")
                a.text("add DWORD PTR [rip + rt_extra], 1")   # branch-taken +1
                a.text("ret")
            else:
                a.text("ret")                        # fallthrough
        return self.a.render()

    def block_entries(self):
        """For the C dispatch table: list of (start_pc, name) + sentinel flag
        for every 16-bit offset in the compiled range."""
        entries = {}
        for i, blk in enumerate(self.blocks):
            entries[blk["start"]] = self.block_names[i]
        return entries
