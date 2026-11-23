/* Zip reading and inflate (zip.h). Inflate follows RFC 1951: canonical
 * Huffman codes decoded a bit at a time from per-length counts (as zlib's
 * puff.c describes the method), stored, fixed and dynamic blocks. Every
 * read is checked against the input's end, every write against cap. */
#include "zip.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

uint32_t zip_crc32(const uint8_t *p, size_t n) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ c >> 1 : c >> 1;
            table[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = table[(c ^ *p++) & 0xFF] ^ c >> 8;
    return c ^ 0xFFFFFFFFu;
}

/* ---- inflate ---- */

enum { MAXBITS = 15 };

typedef struct {
    const uint8_t *in;
    size_t n, pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t cap, len;
    int bad;
} Inf;

typedef struct {
    short count[MAXBITS + 1];  /* codes of each length */
    short symbol[288];         /* symbols ordered by code */
} Huff;

static int bits(Inf *s, int need) {
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->n) {
            s->bad = 1;
            return 0;
        }
        v |= (uint32_t)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1u << need) - 1));
}

static int decode(Inf *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (s->bad) return -1;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->bad = 1;  /* no code this long */
    return -1;
}

/* 0, or -1 when the lengths over-subscribe the code space. An incomplete code
 * is allowed (a single distance code is common); decode rejects unused codes. */
static int build(Huff *h, const short *length, int n) {
    short offs[MAXBITS + 1];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[length[i]]++;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left = (left << 1) - h->count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (length[i]) h->symbol[offs[length[i]]++] = (short)i;
    return 0;
}

static int put(Inf *s, uint8_t b) {
    if (s->len >= s->cap) return s->bad = 1, -1;
    s->out[s->len++] = b;
    return 0;
}

static int codes(Inf *s, const Huff *lit, const Huff *dist) {
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (put(s, (uint8_t)sym)) return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return s->bad = 1, -1;
            int len = lbase[sym] + bits(s, lext[sym]);
            int d = decode(s, dist);
            if (d < 0 || d >= 30) return s->bad = 1, -1;
            size_t back = (size_t)dbase[d] + (size_t)bits(s, dext[d]);
            if (s->bad || back > s->len) return s->bad = 1, -1;
            while (len--)  /* byte by byte: the copy may overlap its own output */
                if (put(s, s->out[s->len - back])) return -1;
        }
    }
}

static int stored(Inf *s) {
    s->bitbuf = 0, s->bitcnt = 0;  /* to the byte boundary */
    if (s->pos + 4 > s->n) return s->bad = 1, -1;
    uint32_t len = rd16(s->in + s->pos), nlen = rd16(s->in + s->pos + 2);
    s->pos += 4;
    if (len != (~nlen & 0xFFFF) || s->pos + len > s->n || s->len + len > s->cap) return s->bad = 1, -1;
    memcpy(s->out + s->len, s->in + s->pos, len);
    s->pos += len, s->len += len;
    return 0;
}

static int fixed(Inf *s) {
    static Huff lit, dist;
    static int ready;
    if (!ready) {
        short l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lit, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&dist, l, 30);
        ready = 1;
    }
    return codes(s, &lit, &dist);
}

static int dynamic(Inf *s) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short l[320];
    Huff lencode, lit, dist;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (s->bad || nlen > 286 || ndist > 30) return s->bad = 1, -1;
    int i = 0;
    for (; i < ncode; i++) l[order[i]] = (short)bits(s, 3);
    for (; i < 19; i++) l[order[i]] = 0;
    if (s->bad || build(&lencode, l, 19)) return s->bad = 1, -1;
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lencode);
        if (sym < 0) return -1;
        if (sym < 16) {
            l[i++] = (short)sym;
            continue;
        }
        short v = 0;
        int rep;
        if (sym == 16) {
            if (i == 0) return s->bad = 1, -1;
            v = l[i - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (s->bad || i + rep > nlen + ndist) return s->bad = 1, -1;
        while (rep--) l[i++] = v;
    }
    if (l[256] == 0) return s->bad = 1, -1;  /* no end-of-block code */
    if (build(&lit, l, nlen) || build(&dist, l + nlen, ndist)) return s->bad = 1, -1;
    return codes(s, &lit, &dist);
}

long inflate_raw(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    Inf s = {in, n, 0, 0, 0, out, cap, 0, 0};
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (s.bad) return -1;
        int r = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (r || s.bad) return -1;
    } while (!last);
    return (long)s.len;
}

/* ---- the zip ---- */

int zip_open(Zip *z, const uint8_t *data, size_t size) {
    memset(z, 0, sizeof *z);
    if (size < 22) return -1;
    size_t eocd = size - 22, stop = size > 22 + 65535 ? size - 22 - 65535 : 0;
    while (rd32(data + eocd) != 0x06054B50u) {  /* the end record, before a comment of up to 64 KB */
        if (eocd == stop) return -1;
        eocd--;
    }
    uint32_t count = rd16(data + eocd + 10), dir_size = rd32(data + eocd + 12), dir = rd32(data + eocd + 16);
    if (count == 0xFFFF || dir == 0xFFFFFFFFu || (size_t)dir + dir_size > eocd) return -1;  /* ZIP64 or damaged */
    z->entries = calloc(count ? count : 1, sizeof *z->entries);
    if (!z->entries) return -1;
    size_t p = dir;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 46 > eocd || rd32(data + p) != 0x02014B50u) return zip_close(z), -1;
        ZipEntry *e = &z->entries[i];
        e->method = (uint16_t)rd16(data + p + 10);
        e->crc = rd32(data + p + 16);
        e->csize = rd32(data + p + 20);
        e->size = rd32(data + p + 24);
        e->name_len = (uint16_t)rd16(data + p + 28);
        e->header = rd32(data + p + 42);
        e->name = (const char *)data + p + 46;
        p += 46 + (size_t)e->name_len + rd16(data + p + 30) + rd16(data + p + 32);
        if (p > eocd) return zip_close(z), -1;
    }
    z->data = data, z->size = size, z->count = (int)count;
    return 0;
}

void zip_close(Zip *z) {
    free(z->entries);
    memset(z, 0, sizeof *z);
}

const ZipEntry *zip_find_crc(const Zip *z, uint32_t crc, uint32_t size) {
    for (int i = 0; i < z->count; i++)
        if (z->entries[i].crc == crc && z->entries[i].size == size) return &z->entries[i];
    return NULL;
}

int zip_read(const Zip *z, const ZipEntry *e, uint8_t *out) {
    size_t h = e->header;
    if (h + 30 > z->size || rd32(z->data + h) != 0x04034B50u) return -1;
    size_t at = h + 30 + rd16(z->data + h + 26) + rd16(z->data + h + 28);
    if (at > z->size || e->csize > z->size - at) return -1;
    const uint8_t *in = z->data + at;
    if (e->method == 0) {
        if (e->csize != e->size) return -1;
        memcpy(out, in, e->size);
    } else if (e->method == 8) {
        if (inflate_raw(in, e->csize, out, e->size) != (long)e->size) return -1;
    } else {
        return -1;
    }
    return zip_crc32(out, e->size) == e->crc ? 0 : -1;
}
