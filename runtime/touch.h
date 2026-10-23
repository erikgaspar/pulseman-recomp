/* Toque para celular (touch.c): nas fases, direcional, A/B/C, Start e menu desenhados por cima
 * do jogo, com vários toques ao mesmo tempo; fora delas a tela é tocada direto (itens do nosso
 * menu e das telas do jogo). Ligado no Android (e no PC com PM_TOUCH=1, com o mouse fazendo
 * as vezes do dedo); com um controle conectado os controles da tela somem. */
#ifndef PM_TOUCH_H
#define PM_TOUCH_H

#include <stdint.h>
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

/* Antes de criar a janela (no PC com PM_TOUCH=1, pede ao SDL o toque simulado pelo mouse) */
void touch_init(void);

/* Toque ligado (celular ou PM_TOUCH) */
int touch_enabled(void);

/* Controles desenhados agora (numa fase, sem menu aberto e sem controle conectado) */
int touch_controls_shown(void);

enum { TOUCH_NONE, TOUCH_KEY, TOUCH_TAP };

/* Um evento do SDL (toques). TOUCH_KEY: tecla em *key (o botão de menu é o Esc);
 * TOUCH_TAP: toque no nosso menu, em px da imagem do jogo (*tap_x, *tap_y), para menu_tap. */
int touch_event(const SDL_Event *ev, SDL_Keycode *key, int *tap_x, int *tap_y);

/* Botões do jogo (PAD_*) apertados na tela; uma vez por quadro do jogo (anda a fila dos
 * apertos gerados pelos toques nas telas do jogo) */
uint32_t touch_buttons(void);

/* Desenha os controles por cima do quadro já apresentado (coordenadas da janela) */
void touch_draw(SDL_Renderer *ren);

#endif
