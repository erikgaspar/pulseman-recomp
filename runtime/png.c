/* Leitor de PNG pequeno (inflate próprio, sem dependências) para as imagens da abertura.
 * Aceita PNG sem entrelaçamento: cinza, RGB, paleta, cinza+alfa e RGBA, de 1 a 16 bits. */
#include <stdlib.h>
#include <string.h>

#include "png.h"

/* ---------- inflate (RFC 1951), no estilo do puff.c do zlib ---------- */

typedef struct {
    const uint8_t *src, *end;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t n, cap;
    int err;
} Inflate;

typedef struct {
    short count[16];
    short symbol[288];
} Huffman;

static int bits(Inflate *s, int need)
{
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->src >= s->end) {
            s->err = 1;
            return 0;
        }
        val |= (uint32_t)(*s->src++) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static void emit(Inflate *s, uint8_t b)
{
    if (s->n >= s->cap) {
        s->err = 1;
        return;
    }
    s->out[s->n++] = b;
}

static int decode(Inflate *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= bits(s, 1);
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = 1;
    return -1;
}

static void construct(Huffman *h, const short *length, int n)
{
    short offs[16];
    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++)
        h->count[length[i]]++;
    offs[1] = 0;
    for (int len = 1; len < 15; len++)
        offs[len + 1] = offs[len] + h->count[len];
    for (int i = 0; i < n; i++)
        if (length[i])
            h->symbol[offs[length[i]]++] = (short)i;
}

static void codes(Inflate *s, const Huffman *lencode, const Huffman *distcode)
{
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                    8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                   7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    while (!s->err) {
        int sym = decode(s, lencode);
        if (sym < 0)
            return;
        if (sym < 256) {
            emit(s, (uint8_t)sym);
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) {
                s->err = 1;
                return;
            }
            int len = lbase[sym] + bits(s, lext[sym]);
            int dsym = decode(s, distcode);
            if (dsym < 0 || dsym >= 30) {
                s->err = 1;
                return;
            }
            size_t dist = (size_t)(dbase[dsym] + bits(s, dext[dsym]));
            if (dist > s->n) {
                s->err = 1;
                return;
            }
            while (len-- && !s->err)
                emit(s, s->out[s->n - dist]);
        }
    }
}

static void stored(Inflate *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->end - s->src < 4) {
        s->err = 1;
        return;
    }
    unsigned len = s->src[0] | s->src[1] << 8;
    unsigned nlen = s->src[2] | s->src[3] << 8;
    s->src += 4;
    if (len != (~nlen & 0xFFFF) || (size_t)(s->end - s->src) < len) {
        s->err = 1;
        return;
    }
    while (len--)
        emit(s, *s->src++);
}

static void fixed(Inflate *s)
{
    static Huffman lencode, distcode;
    static int built;
    if (!built) {
        short lengths[288];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        construct(&lencode, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        construct(&distcode, lengths, 30);
        built = 1;
    }
    codes(s, &lencode, &distcode);
}

static void dynamic(Inflate *s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    Huffman lencode, distcode;
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (nlen > 286 || ndist > 30) {
        s->err = 1;
        return;
    }
    int index;
    for (index = 0; index < ncode; index++)
        lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++)
        lengths[order[index]] = 0;
    construct(&lencode, lengths, 19);
    index = 0;
    while (index < nlen + ndist && !s->err) {
        int sym = decode(s, &lencode);
        if (sym < 0)
            return;
        if (sym < 16) {
            lengths[index++] = (short)sym;
            continue;
        }
        int len = 0, rep;
        if (sym == 16) {
            if (index == 0) {
                s->err = 1;
                return;
            }
            len = lengths[index - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (index + rep > nlen + ndist) {
            s->err = 1;
            return;
        }
        while (rep--)
            lengths[index++] = (short)len;
    }
    if (s->err || lengths[256] == 0) {
        s->err = 1;
        return;
    }
    construct(&lencode, lengths, nlen);
    construct(&distcode, lengths + nlen, ndist);
    codes(s, &lencode, &distcode);
}

/* zlib (RFC 1950) com saída de tamanho conhecido */
static int zlib_inflate(const uint8_t *src, size_t n, uint8_t *out, size_t cap)
{
    if (n < 2 || (src[0] & 0x0F) != 8 || ((src[0] << 8) | src[1]) % 31)
        return 0;
    Inflate s = {src + 2, src + n, 0, 0, out, 0, cap, 0};
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        if (type == 0) stored(&s);
        else if (type == 1) fixed(&s);
        else if (type == 2) dynamic(&s);
        else s.err = 1;
    } while (!last && !s.err);
    return !s.err && s.n == cap;
}

/* ---------- PNG ---------- */

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

uint32_t *png_decode(const uint8_t *data, size_t size, int *out_w, int *out_h)
{
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (size < 33 || memcmp(data, sig, 8))
        return NULL;
    uint32_t w = 0, h = 0;
    int depth = 0, type = -1;
    uint8_t palette[256][4];
    memset(palette, 0xFF, sizeof(palette));
    uint8_t *idat = NULL;
    size_t idat_n = 0;
    int trns_gray = -1, trns_rgb[3] = {-1, -1, -1};
    for (size_t p = 8; p + 12 <= size;) {
        uint32_t len = be32(data + p);
        const uint8_t *t = data + p + 4, *d = data + p + 8;
        if (len > size - p - 12)
            break;
        if (!memcmp(t, "IHDR", 4) && len >= 13) {
            w = be32(d);
            h = be32(d + 4);
            depth = d[8];
            type = d[9];
            if (d[12] != 0) {   /* entrelaçado: não suportado */
                free(idat);
                return NULL;
            }
        } else if (!memcmp(t, "PLTE", 4)) {
            for (uint32_t i = 0; i < len / 3 && i < 256; i++) {
                palette[i][0] = d[i * 3];
                palette[i][1] = d[i * 3 + 1];
                palette[i][2] = d[i * 3 + 2];
            }
        } else if (!memcmp(t, "tRNS", 4)) {
            if (type == 3)
                for (uint32_t i = 0; i < len && i < 256; i++)
                    palette[i][3] = d[i];
            else if (type == 0 && len >= 2)
                trns_gray = d[0] << 8 | d[1];
            else if (type == 2 && len >= 6)
                for (int c = 0; c < 3; c++)
                    trns_rgb[c] = d[c * 2] << 8 | d[c * 2 + 1];
        } else if (!memcmp(t, "IDAT", 4)) {
            uint8_t *b = realloc(idat, idat_n + len);
            if (!b) {
                free(idat);
                return NULL;
            }
            idat = b;
            memcpy(idat + idat_n, d, len);
            idat_n += len;
        } else if (!memcmp(t, "IEND", 4)) {
            break;
        }
        p += 12 + len;
    }
    static const int channels_of[7] = {1, 0, 3, 1, 2, 0, 4};
    if (!idat || !w || !h || w > 8192 || h > 8192 || type < 0 || type > 6 || !channels_of[type] ||
        (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)) {
        free(idat);
        return NULL;
    }
    int ch = channels_of[type];
    size_t stride = ((size_t)w * ch * depth + 7) / 8;
    int bpp = (ch * depth + 7) / 8;
    size_t raw_n = (stride + 1) * h;
    uint8_t *raw = malloc(raw_n);
    uint32_t *px = malloc(sizeof(uint32_t) * w * h);
    if (!raw || !px || !zlib_inflate(idat, idat_n, raw, raw_n)) {
        free(idat);
        free(raw);
        free(px);
        return NULL;
    }
    free(idat);

    /* desfaz os filtros de cada linha, no próprio buffer */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *line = raw + y * (stride + 1) + 1, f = line[-1];
        const uint8_t *prev = y ? line - (stride + 1) : NULL;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? line[i - bpp] : 0, b = prev ? prev[i] : 0;
            int c = prev && i >= (size_t)bpp ? prev[i - bpp] : 0;
            switch (f) {
            case 1: line[i] = (uint8_t)(line[i] + a); break;
            case 2: line[i] = (uint8_t)(line[i] + b); break;
            case 3: line[i] = (uint8_t)(line[i] + ((a + b) >> 1)); break;
            case 4: line[i] = (uint8_t)(line[i] + paeth(a, b, c)); break;
            default: break;
            }
        }
    }

    /* amostra de 'depth' bits no índice k da linha (valores de 16 bits ficam inteiros) */
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *line = raw + y * (stride + 1) + 1;
        for (uint32_t x = 0; x < w; x++) {
            int v[4];
            for (int c = 0; c < ch; c++) {
                size_t k = (size_t)x * ch + c;
                if (depth == 16) {
                    v[c] = line[k * 2] << 8 | line[k * 2 + 1];
                } else if (depth == 8) {
                    v[c] = line[k];
                } else {
                    size_t bit = k * depth;
                    v[c] = (line[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
                }
            }
            int maxv = (1 << depth) - 1;
            int r, g, b, a = 255;
            if (type == 3) {
                const uint8_t *e = palette[v[0] & 255];
                r = e[0]; g = e[1]; b = e[2]; a = e[3];
            } else if (type == 0 || type == 4) {
                r = g = b = v[0] * 255 / maxv;
                if (type == 4) a = v[1] * 255 / maxv;
                else if (v[0] == trns_gray) a = 0;
            } else {
                r = v[0] * 255 / maxv;
                g = v[1] * 255 / maxv;
                b = v[2] * 255 / maxv;
                if (type == 6) a = v[3] * 255 / maxv;
                else if (v[0] == trns_rgb[0] && v[1] == trns_rgb[1] && v[2] == trns_rgb[2]) a = 0;
            }
            px[(size_t)y * w + x] = (uint32_t)a << 24 | r << 16 | g << 8 | b;
        }
    }
    free(raw);
    *out_w = (int)w;
    *out_h = (int)h;
    return px;
}
