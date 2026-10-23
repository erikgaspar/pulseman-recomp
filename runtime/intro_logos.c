/* Logos extras na abertura, depois do logo da SEGA, sobre a estática do próprio jogo.
 *
 * A cena da SEGA ($00, rotina $2240) conta $FFF804 para baixo a cada quadro e, ao chegar
 * em zero, pede o título ($2340: move.w #4,$FFF800). Quando o contador está para acabar,
 * o runtime o segura em 2: o jogo continua rodando a cena (estática e chiado), o plano A
 * com o logo da SEGA é escondido e cada imagem da pasta img/ "sintoniza" por cima, como o
 * logo da SEGA faz. No fim (ou com Start/Enter), o contador é solto e o jogo segue para o
 * título como sempre. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#ifdef _WIN32
#include <io.h>
#include <wchar.h>
#else
#include <dirent.h>
#endif

#include "runtime.h"
#include "intro_logos.h"
#include "png.h"

#define MAX_LOGOS 8
#define BOX_W 220   /* tamanho máximo do logo na tela (cabe nos 320 px originais) */
#define BOX_H 110

/* quadros de cada logo: estática pura, entrada, parado, saída */
#define T_GAP 24
#define T_IN 50
#define T_HOLD 150
#define T_OUT 36
#define T_LOGO (T_GAP + T_IN + T_HOLD + T_OUT)
#define T_END 30   /* estática pura antes do título */

typedef struct {
    int w, h;
    uint32_t *px;   /* ARGB já reduzido; o alfa diz o que é logo */
} Logo;

static Logo logos[MAX_LOGOS];
static int n_logos;

enum { ST_WAIT, ST_PLAY, ST_RELEASE, ST_LEAVE, ST_DONE };
static int state = ST_WAIT;
static int leave_frames;   /* o título ainda mostra o plano A da SEGA no primeiro quadro */
static int t;   /* quadros desde o início da sequência */

/* ---------- carregamento ---------- */

static uint8_t *read_file(const char *path, size_t *n)
{
    SDL_RWops *f = SDL_RWFromFile(path, "rb");
    if (!f)
        return NULL;
    Sint64 size = SDL_RWsize(f);
    uint8_t *buf = size > 0 && size < (64 << 20) ? malloc((size_t)size) : NULL;
    if (buf && SDL_RWread(f, buf, 1, (size_t)size) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    SDL_RWclose(f);
    *n = buf ? (size_t)size : 0;
    return buf;
}

static uint32_t *load_image(const char *path, int *w, int *h)
{
    size_t n;
    uint8_t *data = read_file(path, &n);
    if (!data)
        return NULL;
    uint32_t *px = png_decode(data, n, w, h);
    if (!px) {   /* não é PNG: tenta BMP pelo SDL */
        SDL_Surface *s = SDL_LoadBMP_RW(SDL_RWFromConstMem(data, (int)n), 1);
        SDL_Surface *c = s ? SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0) : NULL;
        if (c) {
            px = malloc(sizeof(uint32_t) * c->w * c->h);
            for (int y = 0; px && y < c->h; y++)
                memcpy(px + (size_t)y * c->w, (uint8_t *)c->pixels + y * c->pitch, c->w * 4);
            *w = c->w;
            *h = c->h;
        }
        SDL_FreeSurface(c);
        SDL_FreeSurface(s);
    }
    free(data);
    return px;
}

/* Prepara a imagem: o que é logo vira alfa (pelo canal alfa, ou pela diferença para a cor
 * do fundo quando a imagem é opaca), corta as margens e reduz para caber na tela. O logo
 * fica com as cores da imagem, direto sobre a estática, como o da SEGA. */
static int prepare(Logo *L, const uint32_t *src, int w, int h)
{
    int transparent = 0;
    for (int i = 0; i < w * h && !transparent; i++)
        transparent = (src[i] >> 24) < 250;
    uint32_t bg = src[0];
    uint8_t *alpha = malloc((size_t)w * h);
    if (!alpha)
        return 0;
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = src[(size_t)y * w + x];
            int a;
            if (transparent) {
                a = c >> 24;
            } else {
                int d = 0;
                for (int sh = 0; sh < 24; sh += 8) {
                    int dd = abs((int)((c >> sh) & 255) - (int)((bg >> sh) & 255));
                    d = dd > d ? dd : d;
                }
                /* o que é quase da cor do fundo (brilho fraco, sombra) some de vez */
                a = (d - 24) * 3;
                a = a < 0 ? 0 : a > 255 ? 255 : a;
            }
            alpha[(size_t)y * w + x] = (uint8_t)a;
            if (a > 24) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
        }
    if (x1 < 0) {
        free(alpha);
        return 0;
    }
    int cw = x1 - x0 + 1, chh = y1 - y0 + 1;
    double sc = 1.0;
    if (cw * sc > BOX_W) sc = (double)BOX_W / cw;
    if (chh * sc > BOX_H) sc = (double)BOX_H / chh;
    L->w = (int)(cw * sc + 0.5);
    L->h = (int)(chh * sc + 0.5);
    if (L->w < 1) L->w = 1;
    if (L->h < 1) L->h = 1;
    L->px = malloc(sizeof(uint32_t) * L->w * L->h);
    if (!L->px) {
        free(alpha);
        return 0;
    }
    /* redução por média da área; traços finos ficam um pouco mais fortes para não sumirem */
    for (int y = 0; y < L->h; y++) {
        int sy0 = y0 + (int)(y / sc), sy1 = y0 + (int)((y + 1) / sc);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int x = 0; x < L->w; x++) {
            int sx0 = x0 + (int)(x / sc), sx1 = x0 + (int)((x + 1) / sc);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            double r = 0, g = 0, b = 0, a = 0;
            int cnt = 0;
            for (int yy = sy0; yy < sy1 && yy < h; yy++)
                for (int xx = sx0; xx < sx1 && xx < w; xx++) {
                    uint32_t c = src[(size_t)yy * w + xx];
                    double aa = alpha[(size_t)yy * w + xx];
                    r += ((c >> 16) & 255) * aa;
                    g += ((c >> 8) & 255) * aa;
                    b += (c & 255) * aa;
                    a += aa;
                    cnt++;
                }
            uint32_t out = 0;
            if (a > 0) {   /* cor média do que é logo (ponderada pelo alfa) */
                int ia = (int)(a * 3 / (cnt * 2));
                out = (uint32_t)(ia > 255 ? 255 : ia) << 24 | (uint32_t)(r / a) << 16 |
                      (uint32_t)(g / a) << 8 | (uint32_t)(b / a);
            }
            L->px[y * L->w + x] = out;
        }
    }
    free(alpha);
    return 1;
}

static int has_image_ext(const char *name)
{
    size_t n = strlen(name);
    if (n < 4 || !SDL_strncasecmp(name, "wallpaper.", 10))   /* fundo do instalador, não é logo */
        return 0;
    const char *e = name + n - 4;
    return !SDL_strcasecmp(e, ".png") || !SDL_strcasecmp(e, ".bmp");
}

static int cmp_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Nomes das imagens da pasta (UTF-8), em ordem alfabética */
static int list_images(const char *dir, char **names, int max)
{
    int n = 0;
#ifdef _WIN32
    char pattern[1100];
    snprintf(pattern, sizeof(pattern), "%s*", dir);
    wchar_t *wpat = (wchar_t *)SDL_iconv_string("UTF-16LE", "UTF-8", pattern, strlen(pattern) + 1);
    if (!wpat)
        return 0;
    struct _wfinddata_t fd;
    intptr_t h = _wfindfirst(wpat, &fd);
    SDL_free(wpat);
    if (h == -1)
        return 0;
    do {
        if (fd.attrib & _A_SUBDIR)
            continue;
        char *name = SDL_iconv_string("UTF-8", "UTF-16LE", (const char *)fd.name,
                                      (wcslen(fd.name) + 1) * sizeof(wchar_t));
        if (name && has_image_ext(name) && n < max)
            names[n++] = name;
        else
            SDL_free(name);
    } while (_wfindnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max)
        if (has_image_ext(e->d_name))
            names[n++] = SDL_strdup(e->d_name);
    closedir(d);
#endif
    qsort(names, n, sizeof(char *), cmp_names);
    return n;
}

int intro_logos_load(const char *base)
{
    /* img/ ao lado do executável; na pasta do projeto (build/Release/../../img); ou a atual */
    char dirs[3][1024];
    snprintf(dirs[0], sizeof(dirs[0]), "%simg/", base ? base : "");
    snprintf(dirs[1], sizeof(dirs[1]), "%s../../img/", base ? base : "");
    snprintf(dirs[2], sizeof(dirs[2]), "img/");
    for (int d = 0; d < 3 && !n_logos; d++) {
        char *names[MAX_LOGOS];
        int n = list_images(dirs[d], names, MAX_LOGOS);
        for (int i = 0; i < n; i++) {
            char path[1400];
            snprintf(path, sizeof(path), "%s%s", dirs[d], names[i]);
            int w, h;
            uint32_t *px = load_image(path, &w, &h);
            if (px && prepare(&logos[n_logos], px, w, h))
                n_logos++;
            else
                fprintf(stderr, "abertura: não foi possível ler %s\n", path);
            free(px);
            SDL_free(names[i]);
        }
    }
    return n_logos;
}

void intro_logos_reset(void)
{
    state = ST_WAIT;
    t = 0;
    hide_planes = 0;
}

/* Start nos logos (opening.c): a sequência termina e o contador da SEGA fica livre */
void intro_logos_skip(void)
{
    if (state == ST_WAIT || state == ST_PLAY)
        state = ST_RELEASE;
}

int intro_logos_playing(void)
{
    return state == ST_PLAY;
}

/* ---------- sequência ---------- */

void intro_logos_frame(int skip)
{
    if (!n_logos || state == ST_DONE)
        return;
    int scene = work_ram[0xF803] & 0x7C;
    int timer = work_ram[0xF804] << 8 | work_ram[0xF805];
    if (scene != 0x00) {   /* saiu da cena da SEGA: mais alguns quadros sem o plano A */
        if (state == ST_PLAY || state == ST_RELEASE) {
            state = ST_LEAVE;
            leave_frames = 6;
        }
        if (state == ST_LEAVE && --leave_frames <= 0) {
            hide_planes = 0;
            state = ST_DONE;
        }
        return;
    }
    if (state == ST_WAIT) {
        if (timer < 1 || timer > 2 || !(work_ram[0xF802] & 0x80))
            return;
        state = ST_PLAY;
        t = 0;
    }
    if (state == ST_PLAY) {
        t++;
        if (skip || t >= n_logos * T_LOGO + T_END)
            state = ST_RELEASE;   /* solta o contador: o jogo vai para o título */
        else
            work_ram[0xF804] = 0, work_ram[0xF805] = 2;
    }
    hide_planes = 1;   /* sem o logo da SEGA (plano A) até a troca de cena */
}

/* ---------- desenho ---------- */

static uint32_t hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static float smooth(float x)
{
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3 - 2 * x);
}

void intro_logos_draw(uint32_t fb[][SCREEN_W_MAX], int width)
{
    if (state != ST_PLAY)
        return;
    int cur = t / T_LOGO, lt = t % T_LOGO;
    if (cur >= n_logos)
        return;   /* estática pura antes do título */
    const Logo *L = &logos[cur];
    float sig;    /* força do "sinal": 0 = só estática, 1 = logo limpo */
    if (lt < T_GAP) sig = 0;
    else if (lt < T_GAP + T_IN) sig = smooth((lt - T_GAP) / (float)T_IN);
    else if (lt < T_GAP + T_IN + T_HOLD) sig = 1;
    else sig = 1 - smooth((lt - T_GAP - T_IN - T_HOLD) / (float)T_OUT);
    if (sig <= 0)
        return;
    float noise = 1 - sig;
    int lx = (width - L->w) / 2, ly = (SCREEN_H - L->h) / 2;

    /* o logo, com as linhas tremendo enquanto o sinal entra ou sai */
    for (int y = 0; y < L->h; y++) {
        uint32_t hs = hash((uint32_t)(y * 7919 + t * 104729 + cur * 31));
        int jitter = (int)(((int)(hs & 63) - 32) * noise * noise * 1.6f);
        if (noise > 0.05f && ((hs >> 8) & 15) == 0)   /* às vezes uma linha escapa mais */
            jitter += (int)(((int)((hs >> 12) & 63) - 32) * noise);
        float row_a = sig;
        if (noise > 0.02f)   /* cintila e mostra linhas de varredura */
            row_a *= (0.55f + 0.45f * ((hs >> 20) & 255) / 255.0f) * (((y + t) % 3) ? 1.0f : 1 - 0.5f * noise);
        int sy = ly + y;
        if (sy < 0 || sy >= SCREEN_H)
            continue;
        for (int x = 0; x < L->w; x++) {
            uint32_t c = L->px[y * L->w + x];
            int a = (int)((c >> 24) * row_a);
            int sx = lx + x + jitter;
            if (a <= 0 || sx < 0 || sx >= width)
                continue;
            uint32_t d = fb[sy][sx], o = 0xFF000000u;
            for (int sh = 0; sh < 24; sh += 8) {
                int cd = (d >> sh) & 255, cs = (c >> sh) & 255;
                o |= (uint32_t)(cd + (cs - cd) * a / 255) << sh;
            }
            fb[sy][sx] = o;
        }
    }
}
