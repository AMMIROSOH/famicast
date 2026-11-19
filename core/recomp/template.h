/* Instruction templates, shared by every CPU's recompiler (nes/recomp,
 * gb/recomp): each instruction is a few lines of C, and every use of the PC
 * in them is a marker, never C text, so one template serves the
 * interpreter (the PC read at run time) and recompiled code (the PC and
 * the code bytes as constants):
 *   @F  operand fetch        rd(c, c->pc++)
 *   @R  read at the PC       rd(c, c->pc)
 *   @P  the PC's value       c->pc
 *   @I  step the PC          c->pc++
 *   @J  jump                 c->pc =   (from the next line on the PC is a run-time value)
 *   @B  taken branch         c->pc =   (inside a condition: on the other path the PC stays known)
 * A CPU's helpers (rd, fetch_k) take the Cpu as `c`. */
#ifndef CORE_TEMPLATE_H
#define CORE_TEMPLATE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

enum { MAX_LINES = 16, LINE_LEN = 128 };
typedef struct {
    int n;
    char s[MAX_LINES][LINE_LEN];
} Lines;

/* Append a line; stops the program on a raw c->pc (a template must use a
 * marker) or when the lines run out. */
void lines_add(Lines *o, const char *fmt, ...);

typedef struct {
    char *s;
    size_t len, cap;
} Buf;

void bprintf(Buf *b, const char *fmt, ...);
void buf_write_file(const char *path, const Buf *b);  /* exits on error */

/* One template line as C. With pc = NULL, the interpreter's C: the PC is
 * read at run time. With pc = the address the next fetch reads: code
 * fetches become fetch_k() with the byte from mem as a constant, the PC's
 * value a constant; *fetched gets bit k for each byte fetched at offset k
 * from `at`, and @J sets *jump. Addresses are 32 bits, wrapped by `mask`
 * (the 6502 and SM83: $FFFF; mem holds mask + 1 bytes).
 * shortcut: fetches are single bytes; a CPU that fetches 16-bit words
 * (the 68000) needs a marker for them and big-endian constants. */
void template_expand(Buf *o, const char *s, uint32_t *pc, const uint8_t *mem, uint32_t mask, uint32_t at,
                     unsigned *fetched, int *jump);
/* indent, the line as the interpreter's C, newline */
void template_plain(Buf *o, const char *indent, const char *line);

#endif
