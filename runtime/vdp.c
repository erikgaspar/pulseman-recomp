/* VDP (Sega 315-5313): portas, DMA, interrupções e renderização por linha. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "savestate.h"
#include "translate.h"

uint32_t framebuffer[SCREEN_H][SCREEN_W_MAX];
int screen_width = 320;   /* largura de saída (inclui as colunas extras do widescreen) */
int base_width = 320;     /* largura que o jogo vê: 320 (H40) ou 256 (H32) */
int wide_extra = 0;       /* colunas extras de cada lado no widescreen */
int wide_pillarbox = 0;   /* widescreen ligado, mas a tela atual não suporta: faixas pretas */
int wide_clip_planes = 0; /* widescreen: planos (1=A, 2=B) só dentro da área original */
int sprite_unlimited = 0; /* sem limite de sprites por linha: menos sprites piscando/sumindo */
int hide_planes = 0;      /* planos (1=A, 2=B) escondidos na tela toda (abertura) */

void vdp_set_widescreen(int extra)
{
    wide_extra = extra;
    screen_width = base_width + 2 * wide_extra;
}

static uint8_t vram[0x10000];
static uint16_t cram[64];
static uint16_t vsram[40];
static uint8_t reg[32];

static uint32_t addr;      /* endereço atual (16 bits + bit 16 ignorado) */
static uint32_t code;      /* CD5..CD0 */
static int pending;        /* aguardando a segunda palavra do comando */
static int fill_pending;   /* DMA de preenchimento aguardando escrita de dados */

static int cur_line;
static int64_t line_start;
static int hint_counter;
static int vint_pending, hint_pending;
static int vblank_flag;
static uint32_t status_extra;  /* colisão/overflow de sprites */
static int odd_frame;

static uint32_t palette_rgb[3][64]; /* normal, sombra, realce */

static void update_color(int i);

void vdp_reset(void)
{
    memset(vram, 0, sizeof(vram));
    memset(cram, 0, sizeof(cram));
    memset(vsram, 0, sizeof(vsram));
    memset(reg, 0, sizeof(reg));
    addr = code = 0;
    pending = fill_pending = 0;
    vint_pending = hint_pending = 0;
    hint_counter = 0;
    vblank_flag = 0;
    status_extra = 0;
    for (int i = 0; i < 64; i++)
        update_color(i);
    base_width = 320;
    screen_width = base_width + 2 * wide_extra;
}

static void update_color(int i)
{
    static const uint8_t lv_normal[8] = {0, 52, 87, 116, 144, 172, 206, 255};
    static const uint8_t lv_shadow[8] = {0, 29, 52, 70, 87, 101, 116, 130};
    static const uint8_t lv_hilite[8] = {130, 144, 158, 172, 187, 206, 228, 255};
    uint16_t c = cram[i];
    int r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
    palette_rgb[0][i] = 0xFF000000u | (lv_normal[r] << 16) | (lv_normal[g] << 8) | lv_normal[b];
    palette_rgb[1][i] = 0xFF000000u | (lv_shadow[r] << 16) | (lv_shadow[g] << 8) | lv_shadow[b];
    palette_rgb[2][i] = 0xFF000000u | (lv_hilite[r] << 16) | (lv_hilite[g] << 8) | lv_hilite[b];
}

/* ---------- acesso à memória do VDP ---------- */

static void write_vram_byte(uint32_t a, uint32_t v)
{
    vram[a & 0xFFFF] = (uint8_t)v;
}

/* Depuração (PM_TRACE_NT=arquivo): registra escritas nas tabelas de tiles dos planos A/B,
 * com a coluna/linha afetada e o bloco do jogo que estava executando. */
static FILE *nt_trace;
static int frame_no;
static void plane_size(int *w, int *h);

static void trace_nametable(uint32_t a, uint32_t v)
{
    int pw, ph;
    plane_size(&pw, &ph);
    uint32_t size = pw * ph * 2;
    uint32_t bases[2] = {(uint32_t)(reg[2] & 0x38) << 10, (uint32_t)(reg[4] & 7) << 13};
    for (int p = 0; p < 2; p++) {
        if (a >= bases[p] && a < bases[p] + size) {
            uint32_t cell = (a - bases[p]) / 2;
            fprintf(nt_trace, "%d %d %c %d %d %06X %04X\n", frame_no, cur_line, p ? 'B' : 'A',
                    cell % pw, cell / pw, cpu_pc, v & 0xFFFF);
        }
    }
}

/* Depuração (PM_TRACE_VRAM=início-fim, hexa): escritas da CPU nessa faixa da VRAM, uma
 * linha por quadro e bloco do jogo */
static void trace_vram(uint32_t a)
{
    static int init;
    static uint32_t lo = 1, hi;
    static int last_frame = -1;
    static uint32_t last_pc;
    if (!init) {
        init = 1;
        const char *s = getenv("PM_TRACE_VRAM");
        if (s)
            sscanf(s, "%x-%x", &lo, &hi);
    }
    if (a < lo || a > hi || (frame_no == last_frame && cpu_pc == last_pc))
        return;
    last_frame = frame_no;
    last_pc = cpu_pc;
    fprintf(stderr, "vram %04X quadro %d linha %d pc=%06X\n", a, frame_no, cur_line, cpu_pc);
}

static void data_write(uint32_t v)
{
    switch (code & 0xF) {
    case 1: { /* VRAM: endereço ímpar troca os bytes */
        uint32_t a = addr & 0xFFFF;
        if (nt_trace)
            trace_nametable(a & ~1u, (a & 1) ? ((v & 0xFF) << 8) | ((v >> 8) & 0xFF) : v);
        trace_vram(a);
        if (a & 1)
            v = ((v & 0xFF) << 8) | (v >> 8);
        vram[a & ~1u] = (uint8_t)(v >> 8);
        vram[(a & ~1u) + 1] = (uint8_t)v;
        break;
    }
    case 3:
        cram[(addr >> 1) & 0x3F] = v & 0xEEE;
        update_color((addr >> 1) & 0x3F);
        break;
    case 5:
        if (((addr >> 1) & 0x3F) < 40)
            vsram[(addr >> 1) & 0x3F] = v & 0x7FF;
        break;
    default:
        break;
    }
    addr = (addr + reg[15]) & 0x1FFFF;
}

static uint32_t dma_length(void)
{
    uint32_t len = reg[19] | (reg[20] << 8);
    return len ? len : 0x10000;
}

static void dma_68k(void)
{
    uint32_t len = dma_length();
    uint32_t src = ((reg[21] | (reg[22] << 8) | ((reg[23] & 0x7F) << 16)) << 1);
    if (nt_trace)
        fprintf(nt_trace, "dma %d %c dest=%04X src=%06X palavras=%u pc=%06X\n", frame_no,
                "?V?C?S"[code & 7], addr & 0xFFFF, src, len, cpu_pc);
    for (uint32_t i = 0; i < len; i++) {
        uint32_t w = bus_read16_raw(src & 0xFFFFFF);
        /* a fonte dá a volta dentro de blocos de 128 KB */
        src = (src & 0xFE0000) | ((src + 2) & 0x1FFFF);
        data_write(w);
    }
    cpu.cycles += len / 2; /* estimativa do roubo de barramento */
    reg[19] = reg[20] = 0;
    uint32_t s = (src >> 1);
    reg[21] = s & 0xFF;
    reg[22] = (s >> 8) & 0xFF;
}

static void dma_fill(uint32_t v)
{
    uint32_t len = dma_length();
    uint32_t fillbyte = v >> 8;
    for (uint32_t i = 0; i < len; i++) {
        if ((code & 0xF) == 1)
            write_vram_byte(addr, fillbyte);
        else if ((code & 0xF) == 3) {
            cram[(addr >> 1) & 0x3F] = v & 0xEEE;
            update_color((addr >> 1) & 0x3F);
        } else if ((code & 0xF) == 5 && ((addr >> 1) & 0x3F) < 40)
            vsram[(addr >> 1) & 0x3F] = v & 0x7FF;
        addr = (addr + reg[15]) & 0x1FFFF;
    }
    reg[19] = reg[20] = 0;
}

static void dma_copy(void)
{
    uint32_t len = dma_length();
    uint32_t src = reg[21] | (reg[22] << 8);
    for (uint32_t i = 0; i < len; i++) {
        write_vram_byte(addr, vram[src & 0xFFFF]);
        src = (src + 1) & 0xFFFF;
        addr = (addr + reg[15]) & 0x1FFFF;
    }
    reg[19] = reg[20] = 0;
    reg[21] = src & 0xFF;
    reg[22] = src >> 8;
}

void vdp_write_data(uint32_t v)
{
    pending = 0;
    if (fill_pending) {
        fill_pending = 0;
        data_write(v);
        dma_fill(v);
        return;
    }
    data_write(v);
}

uint32_t vdp_read_data(void)
{
    uint32_t v = 0;
    pending = 0;
    switch (code & 0xF) {
    case 0: {
        uint32_t a = addr & 0xFFFE;
        v = (vram[a] << 8) | vram[a + 1];
        break;
    }
    case 8: v = cram[(addr >> 1) & 0x3F]; break;
    case 4: v = ((addr >> 1) & 0x3F) < 40 ? vsram[(addr >> 1) & 0x3F] : 0; break;
    default: break;
    }
    addr = (addr + reg[15]) & 0x1FFFF;
    return v;
}

static void write_reg(int r, uint32_t v)
{
    if (r >= 24)
        return;
    reg[r] = (uint8_t)v;
    if (r == 12)
    {
        base_width = (v & 0x81) ? 320 : 256;
        screen_width = base_width + 2 * wide_extra;
    }
}

void vdp_write_control(uint32_t v)
{
    if (!pending) {
        if ((v & 0xC000) == 0x8000) {
            write_reg((v >> 8) & 0x1F, v & 0xFF);
            code = 0;
            return;
        }
        code = (code & 0x3C) | (v >> 14);
        addr = (addr & 0x1C000) | (v & 0x3FFF);
        pending = 1;
        return;
    }
    pending = 0;
    addr = (addr & 0x3FFF) | ((v & 7) << 14);
    code = (code & 3) | ((v >> 2) & 0x3C);
    if ((code & 0x20) && (reg[1] & 0x10)) {
        switch (reg[23] >> 6) {
        case 0: case 1: dma_68k(); break;
        case 2: fill_pending = 1; break;
        case 3: dma_copy(); break;
        }
    }
}

/* ---------- status, contadores e interrupções ---------- */

static int line_pos(void)
{
    int64_t p = cpu.cycles - line_start;
    if (p < 0) p = 0;
    if (p >= CYCLES_PER_LINE) p = CYCLES_PER_LINE - 1;
    return (int)p;
}

uint32_t vdp_read_status(void)
{
    pending = 0;
    uint32_t s = 0x3400 | 0x200; /* FIFO vazia */
    if (vint_pending) s |= 0x80;
    s |= status_extra;
    status_extra = 0;
    if (odd_frame) s |= 0x10;
    if (vblank_flag || !(reg[1] & 0x40)) s |= 0x08;
    int p = line_pos();
    if (p >= 400 || p < 16) s |= 0x04;
    return s;
}

uint32_t vdp_read_hv(void)
{
    int v = cur_line;
    if (v > 0xEA) v -= 6; /* NTSC: 0xEA salta para 0xE5 */
    int h = line_pos() * (base_width == 320 ? 210 : 171) / CYCLES_PER_LINE;
    if (base_width == 320 && h > 0xB6) h += 0xE4 - 0xB7;
    if (base_width == 256 && h > 0x93) h += 0xE9 - 0x94;
    return ((v & 0xFF) << 8) | (h & 0xFF);
}

int irq_pending_level(void)
{
    if (vint_pending && (reg[1] & 0x20))
        return 6;
    if (hint_pending && (reg[0] & 0x10))
        return 4;
    return 0;
}

void irq_acknowledge(int level)
{
    if (level == 6)
        vint_pending = 0;
    else if (level == 4)
        hint_pending = 0;
}

/* ---------- renderização ---------- */

/* Pixel de um plano: bits 0-5 cor, bit 6 opaco, bit 7 prioridade */
static uint8_t line_a[SCREEN_W_MAX], line_b[SCREEN_W_MAX], line_s[SCREEN_W_MAX];

static inline uint8_t tile_pixel(uint32_t entry, int px, int py)
{
    uint32_t tile = entry & 0x7FF;
    if (entry & 0x0800) px = 7 - px;
    if (entry & 0x1000) py = 7 - py;
    uint8_t b = vram[(tile * 32 + py * 4 + (px >> 1)) & 0xFFFF];
    uint8_t c = (px & 1) ? (b & 0xF) : (b >> 4);
    if (!c)
        return (entry & 0x8000) ? 0x80 : 0;
    return (uint8_t)(0x40 | ((entry >> 9) & 0x30) | c | ((entry & 0x8000) ? 0x80 : 0));
}

static void plane_size(int *w, int *h)
{
    static const int sizes[4] = {32, 64, 32, 128};
    *w = sizes[reg[16] & 3];
    *h = sizes[(reg[16] >> 4) & 3];
}

static int hscroll_value(int line, int plane)
{
    uint32_t base = (reg[13] & 0x3F) << 10;
    int idx;
    switch (reg[11] & 3) {
    case 0: idx = 0; break;
    case 1: idx = line & 7; break;
    case 2: idx = line & ~7; break;
    default: idx = line; break;
    }
    uint32_t a = (base + idx * 4 + plane * 2) & 0xFFFF;
    return ((vram[a] << 8) | vram[a + 1]) & 0x3FF;
}

/* Os índices i dos buffers de linha são de saída; a coordenada do jogo é gx = i - wide_extra. */

/* células dos planos com texto traduzido por cima (scan_texts, mais abaixo) */
static uint8_t cell_hidden[2][128 * 128];
static int any_cell_hidden;

static void render_plane(uint8_t *out, int line, int plane, int i0, int i1)
{
    int pw, ph;
    plane_size(&pw, &ph);
    uint32_t base = plane == 0 ? (uint32_t)(reg[2] & 0x38) << 10 : (uint32_t)(reg[4] & 7) << 13;
    int hs = hscroll_value(line, plane);
    int wmask = pw * 8 - 1, hmask = ph * 8 - 1;
    for (int i = i0; i < i1; i++) {
        int gx = i - wide_extra;
        int vs;
        if (reg[11] & 4) {
            int col = gx >> 4;
            if (col < 0) col = 0;
            if (col > 19) col = 19;
            vs = vsram[col * 2 + plane];
        } else {
            vs = vsram[plane];
        }
        int py = (line + vs) & hmask;
        int px = (gx - hs) & wmask;
        uint32_t ea = (base + ((py >> 3) * pw + (px >> 3)) * 2) & 0xFFFF;
        uint32_t entry = (vram[ea] << 8) | vram[ea + 1];
        if (any_cell_hidden && cell_hidden[plane][(py >> 3) * pw + (px >> 3)])
            out[i] = 0;   /* texto traduzido por cima (scan_texts) */
        else
            out[i] = tile_pixel(entry, px & 7, py & 7);
    }
}

static void render_window(uint8_t *out, int line, int i0, int i1)
{
    int ww = base_width == 320 ? 64 : 32;
    uint32_t base = (uint32_t)(reg[3] & (base_width == 320 ? 0x3C : 0x3E)) << 10;
    for (int i = i0; i < i1; i++) {
        int gx = i - wide_extra;
        uint32_t ea = (base + ((line >> 3) * ww + (gx >> 3)) * 2) & 0xFFFF;
        uint32_t entry = (vram[ea] << 8) | vram[ea + 1];
        out[i] = tile_pixel(entry, gx & 7, line & 7);
    }
}

/* Tradução (translate.c): no início do quadro, a assinatura do conteúdo de cada sprite da
 * parte de baixo da tela; os que formam uma legenda traduzida não são desenhados */
static uint8_t sprite_hidden[128];

static void scan_captions(void)
{
    memset(sprite_hidden, 0, sizeof(sprite_hidden));
    translate_begin_frame();
    if (!translate_enabled())
        return;
    uint32_t sat = (uint32_t)(reg[5] & (base_width == 320 ? 0x7E : 0x7F)) << 9;
    int link = 0, count = 0, max_sprites = base_width == 320 ? 80 : 64;
    do {
        int idx = link;
        uint32_t e = (sat + link * 8) & 0xFFFF;
        int y = (((vram[e] << 8) | vram[e + 1]) & 0x1FF) - 128;
        int size = vram[e + 2];
        link = vram[e + 3] & 0x7F;
        uint32_t attr = (vram[e + 4] << 8) | vram[e + 5];
        int x = (((vram[e + 6] << 8) | vram[e + 7]) & 0x1FF) - 128;
        int wc = ((size >> 2) & 3) + 1, hc = (size & 3) + 1;
        if (y < 150 || y >= SCREEN_H || x <= -32 || x >= base_width)
            continue;
        uint32_t h = 2166136261u, any = 0;
        for (int t = 0; t < wc * hc; t++) {
            uint32_t a = (((attr & 0x7FF) + t) & 0x7FF) * 32;
            for (int b = 0; b < 32; b++) {
                h = (h ^ vram[a + b]) * 16777619u;
                any |= vram[a + b];
            }
        }
        if (any && translate_sprite(h, x, y, wc * 8, hc * 8))
            sprite_hidden[idx] = 1;
    } while (link && ++count < max_sprites);
}

/* Tradução dos textos em inglês da ROM (translate.c): o jogo escreve cada letra na tabela
 * de tiles como código ASCII + base da fonte. Cada texto conhecido é procurado nos planos A
 * e B (com qualquer base, mesma paleta/prioridade em todas as letras, ao menos 4 letras ou
 * o texto inteiro, para aceitar o terminal digitando); as células achadas ficam
 * transparentes e a tradução é escrita por cima. Os mais longos primeiro ("PUSH START
 * BUTTON" antes de "START"). */
static void scan_texts(void)
{
    if (any_cell_hidden) {
        memset(cell_hidden, 0, sizeof(cell_hidden));
        any_cell_hidden = 0;
    }
    int n = translate_text_count();
    if (!n)
        return;
    int order[64], m = 0;
    for (int i = 0; i < n && m < 64; i++)
        if (translate_text_orig(i))   /* só os desta cena */
            order[m++] = i;
    for (int i = 1; i < m; i++)   /* mais longos primeiro */
        for (int j = i; j > 0 && strlen(translate_text_orig(order[j])) > strlen(translate_text_orig(order[j - 1])); j--) {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    int pw, ph;
    plane_size(&pw, &ph);
    for (int plane = 0; plane < 2; plane++) {
        uint32_t base = plane == 0 ? (uint32_t)(reg[2] & 0x38) << 10 : (uint32_t)(reg[4] & 7) << 13;
        for (int k = 0; k < m; k++) {
            const char *orig = translate_text_orig(order[k]);
            /* os espaços do começo também têm de casar (o terminal escreve " VOICE ..." e
             * o rótulo "VOICE" das opções não tem o espaço) */
            int o0 = 0;
            while (orig[o0] == ' ')
                o0++;
            const char *str = orig;
            int sl = (int)strlen(orig), need = sl < o0 + 4 ? sl : o0 + 4;
            for (int r = 0; r < ph; r++)
                for (int c = 0; c + need <= pw; c++) {
                    uint32_t a0 = (base + (r * pw + c) * 2) & 0xFFFF;
                    uint32_t e0 = (vram[a0] << 8) | vram[a0 + 1];
                    int fbase = (int)(e0 & 0x7FF) - (unsigned char)str[0];
                    if (fbase < 0 || fbase + 0x20 > 0x7FF || cell_hidden[plane][r * pw + c])
                        continue;
                    int got = 1;
                    while (got < sl && c + got < pw) {
                        uint32_t a = (base + (r * pw + c + got) * 2) & 0xFFFF;
                        uint32_t e = (vram[a] << 8) | vram[a + 1];
                        if ((e & 0xF800) != (e0 & 0xF800) || (int)(e & 0x7FF) != fbase + (unsigned char)str[got])
                            break;
                        got++;
                    }
                    if (got < need)
                        continue;
                    /* posição na tela (planos parados nessas telas: rolagem da linha) */
                    int vs = vsram[plane] & 0x3FF;
                    int line = (r * 8 - vs) & (ph * 8 - 1);
                    if (line >= SCREEN_H)
                        continue;
                    int gx = (c * 8 + hscroll_value(line, plane)) & (pw * 8 - 1);
                    if (gx >= pw * 8 - 64)
                        gx -= pw * 8;
                    if (gx < -64 || gx >= base_width + 64)
                        continue;
                    /* cor da fonte: primeiro pixel aceso do tile da primeira letra */
                    uint32_t ta = (uint32_t)(fbase + (unsigned char)str[o0]) * 32, color = 0xFFFFFFFFu;
                    for (int b = 0; b < 64; b++) {
                        int nib = (vram[ta + b / 2] >> (b & 1 ? 0 : 4)) & 15;
                        if (nib) {
                            color = palette_rgb[0][((e0 >> 13) & 3) * 16 + nib];
                            break;
                        }
                    }
                    for (int i = 0; i < got; i++)
                        cell_hidden[plane][r * pw + c + i] = 1;
                    any_cell_hidden = 1;
                    if (got <= o0)
                        continue;
                    translate_text_found(order[k], got - o0, gx + o0 * 8, line, color);
                }
        }
    }
}

static void render_sprites(int line)
{
    memset(line_s, 0, sizeof(line_s));
    int max_sprites = base_width == 320 ? 80 : 64;
    /* limites por linha proporcionais à largura (no widescreen, sem cortar sprites nas laterais) */
    int max_per_line = (base_width == 320 ? 20 : 16) * screen_width / base_width;
    int max_dots = screen_width;
    if (sprite_unlimited)   /* a máscara (sprite em X=0) continua valendo */
        max_per_line = max_dots = 1 << 20;
    static int debug = -1;  /* PM_DEBUG_SPRITES: linhas que passariam do limite original */
    if (debug < 0)
        debug = getenv("PM_DEBUG_SPRITES") != NULL;
    int orig_per_line = base_width == 320 ? 20 : 16, orig_dots = base_width, n_on = 0, n_dots = 0;
    uint32_t sat = (uint32_t)(reg[5] & (base_width == 320 ? 0x7E : 0x7F)) << 9;
    int link = 0, count = 0, on_line = 0, dots = 0, masked = 0, prev_on_line = 0;
    do {
        int hidden = sprite_hidden[link];
        uint32_t e = (sat + link * 8) & 0xFFFF;
        int y = (((vram[e] << 8) | vram[e + 1]) & 0x1FF) - 128;
        int size = vram[e + 2];
        link = vram[e + 3] & 0x7F;
        uint32_t attr = (vram[e + 4] << 8) | vram[e + 5];
        int xraw = ((vram[e + 6] << 8) | vram[e + 7]) & 0x1FF;
        int wc = ((size >> 2) & 3) + 1, hc = (size & 3) + 1;
        if (line >= y && line < y + hc * 8) {
            if (xraw == 0 && prev_on_line)
                masked = 1;
            prev_on_line = 1;
            n_on++;
            n_dots += wc * 8;
            if (++on_line > max_per_line) {
                status_extra |= 0x40;
                break;
            }
            int x = xraw - 128;
            int row = line - y;
            if (attr & 0x1000) row = hc * 8 - 1 - row;
            for (int cx = 0; cx < wc && !masked && !hidden; cx++) {
                int col = (attr & 0x0800) ? (wc - 1 - cx) : cx;
                uint32_t tile = ((attr & 0x7FF) + col * hc + (row >> 3)) & 0x7FF;
                uint32_t entry = (attr & 0xF800 & ~0x1000) | tile;
                for (int px = 0; px < 8; px++) {
                    int sx = x + cx * 8 + px + wide_extra;
                    if (++dots > max_dots) break;
                    if (sx < 0 || sx >= screen_width) continue;
                    uint8_t p = tile_pixel(entry, px, row & 7);
                    if (!(p & 0x40)) continue;
                    if (line_s[sx] & 0x40) {
                        status_extra |= 0x20;
                        continue;
                    }
                    line_s[sx] = p;
                }
            }
            if (dots > max_dots) break;
        }
    } while (link && ++count < max_sprites);
    if (debug && (n_on > orig_per_line || n_dots > orig_dots)) {
        static int last_frame = -1;
        if (vblank_count != last_frame)
            fprintf(stderr, "limite de sprites: quadro %d linha %d: %d sprites, %d px\n",
                    vblank_count, line, n_on, n_dots);
        last_frame = vblank_count;
    }
}

static void render_line(int line)
{
    uint32_t *out = framebuffer[line];
    int w = screen_width;
    if (!(reg[1] & 0x40)) {
        uint32_t bg = palette_rgb[0][reg[7] & 0x3F];
        for (int x = 0; x < w; x++) out[x] = bg;
        return;
    }
    /* Janela: linha inteira ou faixa horizontal, em coordenadas do jogo (0..base_width),
     * convertida para índices de saída. Fora da área original fica o plano A. */
    int bw = base_width;
    int wv = (reg[18] & 0x1F) * 8;
    int in_v = (reg[18] & 0x80) ? line >= wv : line < wv;
    int wx0 = 0, wx1 = 0;
    if (in_v) {
        wx0 = 0; wx1 = bw;
    } else {
        int wh = (reg[17] & 0x1F) * 16;
        if (wh > bw) wh = bw;
        if (reg[17] & 0x80) { wx0 = wh; wx1 = bw; }
        else { wx0 = 0; wx1 = wh; }
    }
    if (wx1 > wx0) {
        wx0 += wide_extra;
        wx1 += wide_extra;
    } else {
        wx0 = wx1 = 0;
    }
    render_plane(line_b, line, 1, 0, w);
    if (wx0 > 0) render_plane(line_a, line, 0, 0, wx0);
    if (wx1 > wx0) render_window(line_a, line, wx0, wx1);
    if (wx1 < w) render_plane(line_a, line, 0, wx1, w);
    render_sprites(line);
    {
        /* depuração (PM_HIDE=ABSW): esconde camadas para ver o que está em cada uma */
        static const char *hide = (const char *)1;
        if (hide == (const char *)1)
            hide = getenv("PM_HIDE");
        if (hide) {
            for (int x = 0; x < w; x++) {
                int in_win = x >= wx0 && x < wx1;
                if (strchr(hide, 'A') && !in_win) line_a[x] = 0;
                if (strchr(hide, 'W') && in_win) line_a[x] = 0;
                if (strchr(hide, 'B')) line_b[x] = 0;
                if (strchr(hide, 'S')) line_s[x] = 0;
            }
        }
    }
    if (wide_extra && wide_clip_planes) {
        /* planos marcados aparecem só na área original (ex.: na seleção de fases o plano B
         * guarda o globo da fase vizinha logo ao lado) */
        for (int x = 0; x < w; x++) {
            if (x >= wide_extra && x < wide_extra + base_width)
                continue;
            if (wide_clip_planes & 1) line_a[x] = 0;
            if (wide_clip_planes & 2) line_b[x] = 0;
        }
    }
    if (hide_planes & 1)
        memset(line_a, 0, sizeof(line_a));
    if (hide_planes & 2)
        memset(line_b, 0, sizeof(line_b));

    int sh = reg[12] & 0x08;
    uint8_t bgc = reg[7] & 0x3F;
    for (int x = 0; x < w; x++) {
        uint8_t a = line_a[x], b = line_b[x], s = line_s[x];
        uint8_t pix = bgc;
        int src = 0; /* 0 fundo/plano, 1 sprite */
        /* do fundo para a frente */
        if (b & 0x40 && !(b & 0x80)) pix = b & 0x3F;
        if (a & 0x40 && !(a & 0x80)) pix = a & 0x3F;
        int s_op = sh && (s & 0x3E) == 0x3E; /* paleta 3, cores 14/15 */
        if (s & 0x40 && !(s & 0x80) && !s_op) { pix = s & 0x3F; src = 1; }
        if (b & 0x40 && (b & 0x80)) { pix = b & 0x3F; src = 0; }
        if (a & 0x40 && (a & 0x80)) { pix = a & 0x3F; src = 0; }
        if (s & 0x40 && (s & 0x80) && !s_op) { pix = s & 0x3F; src = 1; }
        int lvl = 0;
        if (sh) {
            int top_plane_prio = (a & 0x80) || (b & 0x80);
            lvl = top_plane_prio ? 0 : 1;
            if (src == 1 && (s & 0x80)) lvl = 0;
            if (s_op && (s & 0x40)) {
                if ((s & 0x0F) == 0x0E) lvl = lvl == 1 ? 0 : 2;
                else lvl = 1;
            }
        }
        out[x] = palette_rgb[lvl][pix];
    }
    if (wide_extra && wide_pillarbox) {
        /* tela que não suporta widescreen: faixas pretas nas laterais */
        for (int x = 0; x < wide_extra; x++)
            out[x] = out[w - 1 - x] = 0xFF000000u;
    }
    if ((reg[0] & 0x20) && (!wide_extra || wide_pillarbox)) {
        /* o jogo apaga a coluna 0..7 (emenda da rolagem); no widescreen ela fica visível */
        int x0 = wide_extra;
        for (int x = x0; x < x0 + 8 && x < w; x++) out[x] = palette_rgb[0][bgc];
    }
}

void vdp_begin_line(int line)
{
    cur_line = line;
    if (line == 0) {
        static int trace_init;
        if (!trace_init) {
            trace_init = 1;
            const char *path = getenv("PM_TRACE_NT");
            if (path)
                nt_trace = fopen(path, "w");
        }
        frame_no++;
        scan_captions();
        scan_texts();
        /* Depuração (PM_DUMP_SAT=quadro): lista os sprites na ordem da lista ligada */
        static int dump_frame = -2;
        if (dump_frame == -2) {
            const char *s = getenv("PM_DUMP_SAT");
            dump_frame = s ? atoi(s) : -1;
        }
        if (frame_no == dump_frame) {
            uint32_t sat = (uint32_t)(reg[5] & (base_width == 320 ? 0x7E : 0x7F)) << 9;
            int link = 0, n = 0;
            fprintf(stderr, "SAT em %04X (quadro %d): idx x y tam tile attr\n", sat, frame_no);
            do {
                uint32_t e = (sat + link * 8) & 0xFFFF;
                int y = (((vram[e] << 8) | vram[e + 1]) & 0x1FF) - 128;
                int size = vram[e + 2];
                uint32_t attr = (vram[e + 4] << 8) | vram[e + 5];
                int x = (((vram[e + 6] << 8) | vram[e + 7]) & 0x1FF) - 128;
                fprintf(stderr, "  %2d x=%4d y=%4d %dx%d tile=%03X attr=%04X\n", link, x, y,
                        ((size >> 2) & 3) + 1, (size & 3) + 1, attr & 0x7FF, attr & 0xF800);
                link = vram[e + 3] & 0x7F;
            } while (link && ++n < 80);
        }
        if (nt_trace) {
            int pw, ph;
            plane_size(&pw, &ph);
            fprintf(nt_trace, "# quadro %d planoA=%04X planoB=%04X tam=%dx%d r11=%02X hscroll=%04X hsA=%d vsA=%d\n",
                    frame_no, (reg[2] & 0x38) << 10, (reg[4] & 7) << 13, pw, ph, reg[11], (reg[13] & 0x3F) << 10,
                    hscroll_value(112, 0), vsram[0]);
        }
    }
    line_start = cpu.cycles;
    int height = (reg[1] & 0x08) ? 240 : 224;
    if (line == 0) {
        vblank_flag = 0;
        odd_frame ^= 1;
    }
    if (line < SCREEN_H)
        render_line(line);
    if (line <= height) {
        if (--hint_counter < 0) {
            hint_counter = reg[10];
            hint_pending = 1;
        }
    } else {
        hint_counter = reg[10];
    }
    if (line == height) {
        vblank_flag = 1;
        vint_pending = 1;
    }
}

/* Acesso do runtime à VRAM (bossrush.c escreve um item na tela de título) */
uint32_t vdp_vram_read16(uint32_t a)
{
    a &= 0xFFFE;
    return (uint32_t)vram[a] << 8 | vram[a + 1];
}

void vdp_vram_write16(uint32_t a, uint32_t v)
{
    a &= 0xFFFE;
    vram[a] = (uint8_t)(v >> 8);
    vram[a + 1] = (uint8_t)v;
}

uint32_t vdp_plane_a_base(void)
{
    return (uint32_t)(reg[2] & 0x38) << 10;
}

int vdp_plane_width(void)
{
    int w, h;
    plane_size(&w, &h);
    return w;
}

/* Depuração (--ramdump): VRAM (64 KB), CRAM (64 palavras), VSRAM (40) e registradores */
void vdp_dump(FILE *f)
{
    fwrite(vram, 1, sizeof(vram), f);
    fwrite(cram, 1, sizeof(cram), f);
    fwrite(vsram, 1, sizeof(vsram), f);
    fwrite(reg, 1, sizeof(reg), f);
}

void vdp_state(StateIO *s)
{
    state_bytes(s, vram, sizeof(vram));
    state_bytes(s, cram, sizeof(cram));
    state_bytes(s, vsram, sizeof(vsram));
    state_bytes(s, reg, sizeof(reg));
    STATE_VAR(s, addr);
    STATE_VAR(s, code);
    STATE_VAR(s, pending);
    STATE_VAR(s, fill_pending);
    STATE_VAR(s, cur_line);
    STATE_VAR(s, line_start);
    STATE_VAR(s, hint_counter);
    STATE_VAR(s, vint_pending);
    STATE_VAR(s, hint_pending);
    STATE_VAR(s, vblank_flag);
    STATE_VAR(s, status_extra);
    STATE_VAR(s, odd_frame);
    if (s->loading && !s->error) {
        for (int i = 0; i < 64; i++)
            update_color(i);
        base_width = (reg[12] & 0x81) ? 320 : 256;
        screen_width = base_width + 2 * wide_extra;
    }
}
