#include "template.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lines_add(Lines *o, const char *fmt, ...) {
    if (o->n == MAX_LINES) { fprintf(stderr, "template: too many lines\n"); exit(1); }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(o->s[o->n], LINE_LEN, fmt, ap);
    va_end(ap);
    if (strstr(o->s[o->n++], "c->pc")) {  /* template_expand could not make it a constant */
        fprintf(stderr, "template: c->pc in a template, use a marker: %s\n", o->s[o->n - 1]);
        exit(1);
    }
}

void bprintf(Buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (b->len + (size_t)n + 1 > b->cap) {
        b->cap = (b->len + (size_t)n + 1) * 2;
        b->s = realloc(b->s, b->cap);
    }
    va_start(ap, fmt);
    vsnprintf(b->s + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

void buf_write_file(const char *path, const Buf *b) {
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(b->s, 1, b->len, f) != b->len || fclose(f)) { perror(path); exit(2); }
}

void template_expand(Buf *o, const char *s, uint32_t *pc, const uint8_t *mem, uint32_t mask, uint32_t at,
                     unsigned *fetched, int *jump) {
    for (; *s; s++) {
        if (*s != '@') { bprintf(o, "%c", *s); continue; }
        switch (*++s) {
        case 'F': case 'R':
            if (!pc) { bprintf(o, *s == 'F' ? "rd(c, c->pc++)" : "rd(c, c->pc)"); break; }
            *fetched |= 1u << ((*pc - at) & mask & 15);
            bprintf(o, "fetch_k(c, 0x%04X, 0x%02X)", (unsigned)(*pc & mask), mem[*pc & mask]);
            if (*s == 'F') *pc = (*pc + 1) & mask;
            break;
        case 'P': if (pc) bprintf(o, "0x%04X", (unsigned)(*pc & mask)); else bprintf(o, "c->pc"); break;
        case 'J': if (jump) *jump = 1; /* fall through */
        case 'B': bprintf(o, "c->pc ="); break;
        case 'I': bprintf(o, "c->pc++"); break;
        default: fprintf(stderr, "template: unknown marker @%c\n", *s); exit(2);
        }
    }
}

void template_plain(Buf *o, const char *indent, const char *line) {
    bprintf(o, "%s", indent);
    template_expand(o, line, NULL, NULL, 0, 0, NULL, NULL);
    bprintf(o, "\n");
}
