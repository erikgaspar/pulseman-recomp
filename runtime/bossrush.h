/* Modo Boss Rush (bossrush.c): os 7 chefes em sequência, com uma vida só, cronômetro e
 * recorde. Escolhido na tela de título (terceiro item, abaixo de OPTIONS). */
#ifndef PM_BOSSRUSH_H
#define PM_BOSSRUSH_H

#include <stdint.h>
#include "runtime.h"

/* Antes de cada quadro: botões do jogador → botões que chegam ao jogo (cursor do título,
 * início automático pela seleção de fases, resultado). Devolve 1 em *start quando o jogo
 * deve receber o Start virtual que começa o jogo novo. */
uint32_t bossrush_filter(uint32_t pad, int *start);

/* Antes de cada quadro, depois do filtro: escreve o item no título e cuida da corrida */
void bossrush_frame(void);

/* Depois do quadro pronto: cronômetro, telas pretas da passagem e resultado */
void bossrush_draw(uint32_t (*fb)[SCREEN_W_MAX], int width, int extra);

/* Gancho da troca de cena ($83A, via opening.c): devolve a cena a usar no lugar de 'scene' */
int bossrush_scene_change(int scene);

/* 1 quando há recorde novo para gravar no .ini (e zera o aviso) */
int bossrush_take_record(void);

void bossrush_reset(void);

#endif
