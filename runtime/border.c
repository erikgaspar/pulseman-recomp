/* Moldura desfocada das telas em 4:3 (ver border.h).
 *
 * A imagem do meio (320 px) é reduzida a uma grade de blocos de 8x8 px, desfocada com
 * duas passadas de média 3x3 e ampliada de novo (bilinear) com zoom, como se a cena
 * continuasse por trás das laterais. A grade é suavizada entre quadros para a estática
 * e as trocas de imagem não piscarem na moldura. Só muda o que é mostrado: a emulação,
 * os jogos salvos e o lockstep não passam por aqui. */
#include <string.h>

#include "border.h"

#define CELL 8
#define GW (SCREEN_W_MAX / CELL)
#define GH (SCREEN_H / CELL)
#define ZOOM 1.5f      /* a cena por trás aparece maior que a imagem do meio */
#define DARK 0.42f     /* brilho da moldura */
#define SHADOW 12      /* px de sombra junto da imagem */

static float grid[GH][GW][3];
static int have_grid;

void border_reset(void)
{
    have_grid = 0;
}

static void blur(float (*g)[GW][3], int gw, int gh)
{
    static float tmp[GH][GW][3];
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            for (int c = 0; c < 3; c++) {
                float s = 0;
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int yy = y + dy, xx = x + dx;
                        if (yy < 0 || yy >= gh || xx < 0 || xx >= gw)
                            continue;
                        s += g[yy][xx][c];
                        n++;
                    }
                tmp[y][x][c] = s / n;
            }
    memcpy(g, tmp, sizeof(tmp));
}

void border_draw(uint32_t (*fb)[SCREEN_W_MAX], int width, int extra)
{
    int inner = width - 2 * extra;   /* 320 */
    int gw = inner / CELL, gh = SCREEN_H / CELL;
    static float cur[GH][GW][3];
    for (int gy = 0; gy < gh; gy++)
        for (int gx = 0; gx < gw; gx++) {
            uint32_t r = 0, g = 0, b = 0;
            for (int y = gy * CELL; y < gy * CELL + CELL; y++)
                for (int x = extra + gx * CELL; x < extra + gx * CELL + CELL; x++) {
                    uint32_t p = fb[y][x];
                    r += p >> 16 & 0xFF;
                    g += p >> 8 & 0xFF;
                    b += p & 0xFF;
                }
            cur[gy][gx][0] = r / (float)(CELL * CELL);
            cur[gy][gx][1] = g / (float)(CELL * CELL);
            cur[gy][gx][2] = b / (float)(CELL * CELL);
        }
    blur(cur, gw, gh);
    blur(cur, gw, gh);
    float keep = have_grid ? 0.75f : 0.0f;
    for (int gy = 0; gy < gh; gy++)
        for (int gx = 0; gx < gw; gx++)
            for (int c = 0; c < 3; c++)
                grid[gy][gx][c] = grid[gy][gx][c] * keep + cur[gy][gx][c] * (1 - keep);
    have_grid = 1;

    for (int y = 0; y < SCREEN_H; y++) {
        /* posição na imagem do meio, em blocos (centro do bloco = .5) */
        float sy = ((SCREEN_H / 2) + (y + 0.5f - SCREEN_H / 2) / ZOOM) / CELL - 0.5f;
        if (sy < 0) sy = 0;
        if (sy > gh - 1) sy = (float)(gh - 1);
        int y0 = (int)sy, y1 = y0 + 1 < gh ? y0 + 1 : y0;
        float fy = sy - y0;
        for (int side = 0; side < 2; side++)
            for (int i = 0; i < extra; i++) {
                int x = side ? width - extra + i : i;
                int dist = side ? i + 1 : extra - i;   /* distância até a imagem */
                float sx = (inner / 2 + (x + 0.5f - width / 2.0f) / ZOOM) / CELL - 0.5f;
                if (sx < 0) sx = 0;
                if (sx > gw - 1) sx = (float)(gw - 1);
                int x0 = (int)sx, x1 = x0 + 1 < gw ? x0 + 1 : x0;
                float fx = sx - x0;
                float k = DARK;
                if (dist <= SHADOW)
                    k *= 0.45f + 0.55f * dist / SHADOW;
                uint32_t out = 0xFF000000u;
                for (int c = 0; c < 3; c++) {
                    float top = grid[y0][x0][c] + (grid[y0][x1][c] - grid[y0][x0][c]) * fx;
                    float bot = grid[y1][x0][c] + (grid[y1][x1][c] - grid[y1][x0][c]) * fx;
                    int v = (int)((top + (bot - top) * fy) * k + 0.5f);
                    out |= (uint32_t)(v > 255 ? 255 : v) << (16 - 8 * c);
                }
                fb[y][x] = out;
            }
    }
}
