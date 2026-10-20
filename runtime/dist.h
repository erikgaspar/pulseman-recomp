/* Versão distribuível (PM_DIST): o código do jogo roda no interpretador, direto da ROM do
 * jogador, sem nada gerado a partir dela no executável (dist.c, recomp/gen_dist.py). */
#ifndef PM_DIST_H
#define PM_DIST_H

#include <stdint.h>

/* IMM de patches.txt: constante ORIG da instrução em ADDR passa a valer VALUE() */
typedef struct { uint32_t addr; uint32_t orig; int (*value)(void); } DistImm;
/* HOOK de patches.txt: FN() antes da instrução em ADDR */
typedef struct { uint32_t addr; void (*fn)(void); } DistHook;

extern const DistImm dist_imms[];
extern const DistHook dist_hooks[];

int dist_init(void);              /* depois de carregar a ROM; 0 se algum ajuste não bater */
void dist_apply_patches(void);    /* a cada quadro: os valores mudam com o widescreen */

#endif
