/* Logos extras na abertura, sobre a estática da cena da SEGA (intro_logos.c). */
#ifndef PM_INTRO_LOGOS_H
#define PM_INTRO_LOGOS_H

#include <stdint.h>
#include "runtime.h"

/* Carrega as imagens (PNG/BMP) da pasta img/; devolve quantas. base = pasta do executável */
int intro_logos_load(const char *base);
void intro_logos_reset(void);   /* reset do console: a abertura toca de novo */
int intro_logos_playing(void);
void intro_logos_skip(void);    /* Start nos logos: pula a sequência (opening.c) */
/* Depois de cada quadro: segura a cena da SEGA enquanto os logos passam (skip = pular) */
void intro_logos_frame(int skip);
/* Desenha o logo atual sobre o quadro pronto */
void intro_logos_draw(uint32_t fb[][SCREEN_W_MAX], int width);

#endif
