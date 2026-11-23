/* Interface between the Master System runtime and recompiled Z80 code
 * (systems/mastersystem/recomp). A block runs from c->pc, an instruction at
 * a time, until SMS_STOP or a page check says the runtime must take over;
 * the generated code bakes the opcode and its operands into constants. */
#ifndef SMS_RECOMP_RT_H
#define SMS_RECOMP_RT_H

#include <stddef.h>
#include <stdint.h>

#include "z80_cpu.h"

typedef void (*SmsRecompFn)(Z80 *c);

typedef struct {
    uint32_t rom_hash;          /* FNV-1a of the ROM it was made from */
    const char *rom_name;
    /* The block for the instruction at pc with `page` mapped into its slot;
     * NULL when that instruction was not recompiled. */
    SmsRecompFn (*lookup)(uint16_t pc, unsigned page);
    const uint8_t *rom;         /* embedded, or NULL with --no-rom */
    size_t rom_size;
} SmsRecompProgram;

/* Defined by the generated code; NULL in interpreter-only builds. */
extern const SmsRecompProgram *const sms_recomp_program;

/* Back to the runtime when it would do something else before the next
 * instruction (sms_frame): the line is over, the CPU halted, or an interrupt
 * is due that the Z80 would accept (IFF1 on, not right after EI). */
#define SMS_STOP(s) ((s)->cpu.tstates >= (s)->line_end || (s)->cpu.halted || \
                     ((s)->cpu.iff1 && !(s)->cpu.ei && sms_vdp_irq(&(s)->vdp)))

/* A guaranteed tail call where the compiler has one, else back to the
 * runtime, which looks the next block up. */
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 15)
#define RECOMP_CHAIN(f) __attribute__((musttail)) return f(c)
#else
#define RECOMP_CHAIN(f) return
#endif

#endif
