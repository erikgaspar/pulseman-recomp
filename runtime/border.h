/* Moldura das telas em 4:3 no widescreen (menu MOLDURA): em vez das faixas pretas, as
 * laterais mostram a própria cena ampliada, desfocada e escurecida. */
#ifndef PM_BORDER_H
#define PM_BORDER_H

#include "runtime.h"

/* Desenha a moldura nas colunas 0..extra-1 e width-extra..width-1 do quadro, a partir da
 * imagem do meio. Chamada a cada quadro em que a tela está com faixas. */
void border_draw(uint32_t (*fb)[SCREEN_W_MAX], int width, int extra);

/* A tela saiu do formato com faixas: a próxima moldura começa sem herdar a anterior */
void border_reset(void);

#endif
