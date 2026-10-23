/* Tradução dos textos do jogo (ver docs/TRADUCAO.md). As frases em japonês são imagens
 * prontas mostradas como sprites: o VDP passa cada sprite da parte de baixo da tela com uma
 * assinatura do conteúdo, os que formam uma legenda conhecida deixam de ser desenhados e a
 * frase traduzida é escrita no lugar, depois do quadro pronto. */
#ifndef PM_TRANSLATE_H
#define PM_TRANSLATE_H

#include <stdint.h>
#include "runtime.h"

/* Tradução ligada para o quadro atual (opção do menu; nunca no lockstep) */
int translate_enabled(void);

/* Início do quadro (VDP, linha 0): esquece as legendas do quadro anterior */
void translate_begin_frame(void);

/* Um sprite da lista (coordenadas do jogo, px) com a assinatura do conteúdo dos tiles.
 * Devolve 1 se ele faz parte de uma legenda traduzida e não deve ser desenhado. */
int translate_sprite(uint32_t hash, int x, int y, int w, int h);

/* Textos em inglês da ROM (fonte do jogo nos planos): quantos procurar neste quadro (0 em
 * inglês), o original de cada um, e um encontrado na tela com quantas letras já aparecem
 * (o terminal digita), a posição da primeira letra (px do jogo) e a cor da fonte */
int translate_text_count(void);
const char *translate_text_orig(int i);
void translate_text_found(int i, int shown, int x, int y, uint32_t color);

/* Fim do quadro: escreve as legendas traduzidas no quadro (extra = colunas do widescreen) */
void translate_draw(uint32_t (*fb)[SCREEN_W_MAX], int width, int extra);

#endif
