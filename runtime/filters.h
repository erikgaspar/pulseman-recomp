/* Filtros de imagem (menu FILTRO): suavização de pixel art, scanlines e TV de tubo. */
#ifndef PM_FILTERS_H
#define PM_FILTERS_H

#include <stdint.h>
#include "runtime.h"

enum {
    FILTER_SHARP,       /* pixels nítidos (GPU, vizinho mais próximo) */
    FILTER_SMOOTH,      /* bilinear (GPU) */
    FILTER_PIXEL,       /* Scale2x duas vezes (4x) + bilinear: bordas arredondadas */
    FILTER_SCANLINES,   /* linhas escuras entre as linhas do vídeo */
    FILTER_TV,          /* TV de tubo: composto, scanlines, máscara de fósforo, brilho */
    FILTER_TV_CURVED,   /* TV com tela curva e cantos escurecidos */
    FILTER_COUNT
};

extern const char *const filter_names[FILTER_COUNT];

/* Tamanho da imagem que filter_run produz para uma tela src_w x src_h mostrada numa
 * área de view_w x view_h pixels. Devolve 0 se o modo não usa a CPU (nítido/suave). */
int filter_output_size(int mode, int src_w, int src_h, int view_w, int view_h, int *out_w, int *out_h);

/* Aplica o filtro; dst tem out_w x out_h pixels ARGB, com dst_pitch pixels por linha */
void filter_run(int mode, uint32_t (*src)[SCREEN_W_MAX], int src_w, int src_h,
                uint32_t *dst, int out_w, int out_h, int dst_pitch);

/* Curvatura da TV: leva um ponto da imagem (-1..1) para a posição na tela (-1..1) */
void filter_curve(float u, float v, float *x, float *y);

#endif
