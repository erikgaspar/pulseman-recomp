/* Abertura do jogo em destaque (opening.c): Start nas telas de logo leva à abertura (cena
 * $14: terminal "LOG ON" e a história), que só é pulada segurando um botão. */
#ifndef PM_OPENING_H
#define PM_OPENING_H

#include <stdint.h>
#include "runtime.h"

/* Antes de cada quadro: recebe os botões do jogador e devolve os que chegam ao jogo */
uint32_t opening_filter(uint32_t pad);

/* Depois do quadro pronto: o aviso "segure para pular" (extra = colunas do widescreen) */
void opening_draw(uint32_t (*fb)[SCREEN_W_MAX], int width, int extra);

/* Reset do console */
void opening_reset(void);

#endif
