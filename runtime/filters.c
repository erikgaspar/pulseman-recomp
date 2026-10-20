/* Filtros de imagem (menu FILTRO).
 *
 * Nítido e suave são só o modo de escala da GPU. Os outros rodam na CPU:
 *  - Pixel suave: Scale2x (AdvMAME2x) aplicado duas vezes, 4x; a GPU suaviza o resto.
 *  - Scanlines e TV: calculados direto na resolução da área de exibição, então as linhas
 *    caem sempre no lugar certo (sem moiré) em qualquer tamanho de janela. As contas
 *    são feitas em luz linear (gama 2,2) com o feixe de cada linha do vídeo em gaussiana.
 *  - TV: além disso, o borrão horizontal do vídeo composto (que mistura os pontilhados
 *    usados como transparência), máscara de fósforo RGB e um pouco de brilho difuso.
 *  - TV curva: a curvatura é feita na GPU por uma malha (ver filter_curve); aqui só
 *    entram os cantos escurecidos.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "filters.h"

/* ---------- divisão do trabalho entre threads ---------- */

#define MAX_WORKERS 8
static int n_workers = -1;
static SDL_sem *work_go[MAX_WORKERS], *work_done;
static void (*work_fn)(int i0, int i1);
static int work_n;

static void run_slice(int part, int parts)
{
    int i0 = work_n * part / parts, i1 = work_n * (part + 1) / parts;
    if (i1 > i0)
        work_fn(i0, i1);
}

static int worker_main(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (;;) {
        SDL_SemWait(work_go[id]);
        run_slice(id + 1, n_workers + 1);
        SDL_SemPost(work_done);
    }
    return 0;
}

/* Roda fn(i0, i1) cobrindo 0..n-1, dividido entre a thread atual e as auxiliares */
static void parallel_for(int n, void (*fn)(int i0, int i1))
{
    if (n_workers < 0) {
        int cpus = SDL_GetCPUCount();
        n_workers = cpus - 1 < MAX_WORKERS ? cpus - 1 : MAX_WORKERS;
        if (n_workers < 0)
            n_workers = 0;
        work_done = SDL_CreateSemaphore(0);
        for (int i = 0; i < n_workers; i++) {
            work_go[i] = SDL_CreateSemaphore(0);
            if (!work_go[i] || !SDL_CreateThread(worker_main, "filtro", (void *)(intptr_t)i)) {
                n_workers = i;
                break;
            }
        }
    }
    work_fn = fn;
    work_n = n;
    for (int i = 0; i < n_workers; i++)
        SDL_SemPost(work_go[i]);
    run_slice(0, n_workers + 1);
    for (int i = 0; i < n_workers; i++)
        SDL_SemWait(work_done);
}

const char *const filter_names[FILTER_COUNT] = {
    "NÍTIDO", "SUAVE", "PIXEL SUAVE", "SCANLINES", "TV", "TV CURVA",
};

#define LIN_MAX 4095
static uint16_t to_lin[256];
static uint8_t from_lin[LIN_MAX + 1];

static void init_tables(void)
{
    static int done;
    if (done)
        return;
    done = 1;
    for (int i = 0; i < 256; i++)
        to_lin[i] = (uint16_t)(pow(i / 255.0, 2.2) * LIN_MAX + 0.5);
    for (int i = 0; i <= LIN_MAX; i++)
        from_lin[i] = (uint8_t)(pow((double)i / LIN_MAX, 1 / 2.2) * 255 + 0.5);
}

int filter_output_size(int mode, int src_w, int src_h, int view_w, int view_h, int *out_w, int *out_h)
{
    switch (mode) {
    case FILTER_PIXEL:
        *out_w = src_w * 4;
        *out_h = src_h * 4;
        return 1;
    case FILTER_SCANLINES:
    case FILTER_TV:
    case FILTER_TV_CURVED: {
        /* mesma conta do SDL_RenderSetLogicalSize: maior escala que cabe, sem distorcer */
        double s = (double)view_w / src_w;
        if ((double)view_h / src_h < s)
            s = (double)view_h / src_h;
        *out_w = (int)(src_w * s + 0.5);
        *out_h = (int)(src_h * s + 0.5);
        if (*out_w > 3840) { *out_h = *out_h * 3840 / *out_w; *out_w = 3840; }
        if (*out_h > 2160) { *out_w = *out_w * 2160 / *out_h; *out_h = 2160; }
        if (*out_w < src_w || *out_h < src_h) {
            *out_w = src_w;
            *out_h = src_h;
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* ---------- Scale2x ---------- */

static void scale2x(const uint32_t *src, int sw, int sh, int spitch, uint32_t *dst, int dpitch)
{
    for (int y = 0; y < sh; y++) {
        const uint32_t *row = src + y * spitch;
        const uint32_t *up = src + (y > 0 ? y - 1 : y) * spitch;
        const uint32_t *dn = src + (y < sh - 1 ? y + 1 : y) * spitch;
        uint32_t *d0 = dst + (2 * y) * dpitch, *d1 = d0 + dpitch;
        for (int x = 0; x < sw; x++) {
            uint32_t e = row[x], b = up[x], h = dn[x];
            uint32_t d = row[x > 0 ? x - 1 : x], f = row[x < sw - 1 ? x + 1 : x];
            if (b != h && d != f) {
                d0[2 * x] = d == b ? d : e;
                d0[2 * x + 1] = b == f ? f : e;
                d1[2 * x] = d == h ? d : e;
                d1[2 * x + 1] = h == f ? f : e;
            } else {
                d0[2 * x] = d0[2 * x + 1] = d1[2 * x] = d1[2 * x + 1] = e;
            }
        }
    }
}

static void run_pixel(uint32_t (*src)[SCREEN_W_MAX], int w, int h, uint32_t *dst, int dst_pitch)
{
    static uint32_t tmp[SCREEN_H * 2][SCREEN_W_MAX * 2];
    scale2x(&src[0][0], w, h, SCREEN_W_MAX, &tmp[0][0], SCREEN_W_MAX * 2);
    scale2x(&tmp[0][0], w * 2, h * 2, SCREEN_W_MAX * 2, dst, dst_pitch);
}

/* ---------- scanlines / TV ---------- */

/* Buffers que dependem do tamanho de saída */
static int cur_w, cur_h, cur_sw, cur_sh, cur_mode;
static uint16_t *hrow;        /* [src_h][out_w][3]: linhas do vídeo já esticadas na horizontal */
static int *col_src;          /* por coluna de saída: pixel de origem da esquerda */
static uint16_t *col_frac;    /* e o peso (0..256) do pixel da direita */
static uint16_t *col_mul;     /* [out_w][3]: máscara de fósforo x canto escuro (8.8) */
static int *row_src;          /* por linha de saída: linha de origem de cima */
static uint16_t *row_w0, *row_w1, *row_flat;  /* pesos do feixe das duas linhas (8.8) */

static double gauss(double d, double s)
{
    return exp(-0.5 * (d / s) * (d / s));
}

/* Escurecimento dos cantos (TV curva), de 0 a 1, para t em -1..1 */
static double vignette(double t)
{
    double a = fabs(t);
    return a < 0.75 ? 1.0 : 1.0 - 0.55 * pow((a - 0.75) / 0.25, 2.0);
}

static void setup(int mode, int sw, int sh, int ow, int oh)
{
    if (mode == cur_mode && sw == cur_sw && sh == cur_sh && ow == cur_w && oh == cur_h)
        return;
    cur_mode = mode; cur_sw = sw; cur_sh = sh; cur_w = ow; cur_h = oh;
    free(hrow); free(col_src); free(col_frac); free(col_mul);
    free(row_src); free(row_w0); free(row_w1); free(row_flat);
    hrow = malloc(sizeof(uint16_t) * 3 * sh * ow);
    col_src = malloc(sizeof(int) * ow);
    col_frac = malloc(sizeof(uint16_t) * ow);
    col_mul = malloc(sizeof(uint16_t) * 3 * ow);
    row_src = malloc(sizeof(int) * oh);
    row_w0 = malloc(sizeof(uint16_t) * oh);
    row_w1 = malloc(sizeof(uint16_t) * oh);
    row_flat = malloc(sizeof(uint16_t) * oh);

    int tv = mode != FILTER_SCANLINES, curved = mode == FILTER_TV_CURVED;
    double sx = (double)ow / sw, sy = (double)oh / sh;

    for (int x = 0; x < ow; x++) {
        double p = (x + 0.5) / sx - 0.5;
        int i = (int)floor(p);
        double f = p - i;
        if (!tv) /* scanlines: pixels nítidos, só a borda entre dois pixels é suavizada */
            f = f * sx - (sx - 1) / 2, f = f < 0 ? 0 : f > 1 ? 1 : f;
        if (i < 0) i = 0, f = 0;
        if (i >= sw - 1) i = sw - 2, f = 1;
        col_src[x] = i;
        col_frac[x] = (uint16_t)(f * 256 + 0.5);

        /* máscara de fósforo (grade de abertura): cada coluna realça um canal. Com pouca
         * resolução a grade vira listras coloridas, então fica mais fraca. */
        double m = !tv ? 1.0 : sx >= 3 ? 0.72 : sx >= 2 ? 0.85 : 1.0;
        double gain = 3.0 / (1 + 2 * m);
        double v = curved ? vignette(2.0 * (x + 0.5) / ow - 1) : 1.0;
        for (int c = 0; c < 3; c++) {
            double k = (x % 3 == c) ? 1.0 : m;
            col_mul[x * 3 + c] = (uint16_t)(k * gain * v * 256 + 0.5);
        }
    }

    /* feixe: gaussiana em volta do centro de cada linha do vídeo; o ganho compensa a
     * média escurecida pelas linhas (o pico satura, como num tubo) */
    double s = tv ? 0.34 : 0.27;
    double avg = 0;
    for (int i = 0; i < 64; i++) {
        double f = (i + 0.5) / 64;
        avg += (gauss(f, s) + gauss(1 - f, s)) / 64;
    }
    double gain = (tv ? 1.0 : 0.92) / avg;
    double flat = tv ? 0.06 : 0.0;   /* brilho difuso (halação) */
    for (int y = 0; y < oh; y++) {
        double p = (y + 0.5) / sy - 0.5;
        int i = (int)floor(p);
        double f = p - i;
        double w0 = gauss(f, s) * gain, w1 = gauss(1 - f, s) * gain;
        double fl = flat;
        if (i < 0) { i = 0; w0 += w1; w1 = 0; }
        if (i >= sh - 1) { i = sh - 2; w1 += w0; w0 = 0; }
        double v = curved ? vignette(2.0 * (y + 0.5) / oh - 1) : 1.0;
        row_src[y] = i;
        row_w0[y] = (uint16_t)(w0 * (1 - fl) * v * 256 + 0.5);
        row_w1[y] = (uint16_t)(w1 * (1 - fl) * v * 256 + 0.5);
        row_flat[y] = (uint16_t)(fl * v * 256 + 0.5);
    }
}

/* parâmetros do quadro atual, para as threads */
static uint32_t (*crt_src)[SCREEN_W_MAX];
static uint32_t *crt_dst;
static int crt_sw, crt_ow, crt_pitch, crt_tv;

/* 1) cada linha do vídeo em luz linear, borrada (TV) e esticada para out_w */
static void crt_lines(int y0, int y1)
{
    uint32_t (*src)[SCREEN_W_MAX] = crt_src;
    int sw = crt_sw, ow = crt_ow, tv = crt_tv;
    uint16_t lin[SCREEN_W_MAX][3];
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < sw; x++) {
            uint32_t c = src[y][x];
            lin[x][0] = to_lin[(c >> 16) & 0xFF];
            lin[x][1] = to_lin[(c >> 8) & 0xFF];
            lin[x][2] = to_lin[c & 0xFF];
        }
        if (tv) { /* vídeo composto: borrão 1-2-1, que mistura pontilhados xadrez */
            uint16_t prev[3] = {lin[0][0], lin[0][1], lin[0][2]};
            for (int x = 0; x < sw; x++) {
                int r = x < sw - 1 ? x + 1 : x;
                for (int c = 0; c < 3; c++) {
                    uint16_t cur = lin[x][c];
                    lin[x][c] = (uint16_t)((prev[c] + 2 * cur + lin[r][c] + 2) >> 2);
                    prev[c] = cur;
                }
            }
        }
        uint16_t *out = hrow + (size_t)y * ow * 3;
        for (int x = 0; x < ow; x++) {
            int i = col_src[x], f = col_frac[x];
            for (int c = 0; c < 3; c++)
                out[x * 3 + c] = (uint16_t)((lin[i][c] * (256 - f) + lin[i + 1][c] * f) >> 8);
        }
    }
}

/* 2) cada linha de saída: feixe das duas linhas vizinhas, máscara, volta ao sRGB */
static void crt_rows(int y0, int y1)
{
    int ow = crt_ow;
    for (int y = y0; y < y1; y++) {
        const uint16_t *a = hrow + (size_t)row_src[y] * ow * 3;
        const uint16_t *b = a + (size_t)ow * 3;
        uint32_t w0 = row_w0[y], w1 = row_w1[y], wf = row_flat[y];
        uint32_t *d = crt_dst + (size_t)y * crt_pitch;
        for (int x = 0; x < ow; x++) {
            uint32_t px = 0xFF000000u;
            for (int c = 0; c < 3; c++) {
                uint32_t va = a[x * 3 + c], vb = b[x * 3 + c];
                uint32_t v = (va * w0 + vb * w1) >> 8;
                v = (v * col_mul[x * 3 + c]) >> 8;
                if (wf)
                    v += (((va + vb) >> 1) * wf) >> 8;
                if (v > LIN_MAX)
                    v = LIN_MAX;
                px |= (uint32_t)from_lin[v] << (16 - 8 * c);
            }
            d[x] = px;
        }
    }
}

static void run_crt(int mode, uint32_t (*src)[SCREEN_W_MAX], int sw, int sh,
                    uint32_t *dst, int ow, int oh, int dst_pitch)
{
    setup(mode, sw, sh, ow, oh);
    crt_src = src;
    crt_dst = dst;
    crt_sw = sw;
    crt_ow = ow;
    crt_pitch = dst_pitch;
    crt_tv = mode != FILTER_SCANLINES;
    parallel_for(sh, crt_lines);
    parallel_for(oh, crt_rows);
}

void filter_run(int mode, uint32_t (*src)[SCREEN_W_MAX], int src_w, int src_h,
                uint32_t *dst, int out_w, int out_h, int dst_pitch)
{
    init_tables();
    if (mode == FILTER_PIXEL)
        run_pixel(src, src_w, src_h, dst, dst_pitch);
    else
        run_crt(mode, src, src_w, src_h, dst, out_w, out_h, dst_pitch);
}

void filter_curve(float u, float v, float *x, float *y)
{
    /* as bordas se curvam para fora no meio, como o vidro de um tubo */
    const float kx = 0.030f, ky = 0.045f;
    *x = u * (1 - kx * v * v);
    *y = v * (1 - ky * u * u);
}
