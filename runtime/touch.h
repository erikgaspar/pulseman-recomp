/* Controles na tela para celular (touch.c): direcional, A/B/C, Start e menu desenhados por
 * cima do jogo, com vários toques ao mesmo tempo. Ligados no Android (e no PC com PM_TOUCH=1,
 * com o mouse fazendo as vezes do dedo) e escondidos enquanto houver um controle conectado. */
#ifndef PM_TOUCH_H
#define PM_TOUCH_H

#include <stdint.h>
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

/* Antes de criar a janela (no PC com PM_TOUCH=1, pede ao SDL o toque simulado pelo mouse) */
void touch_init(void);

/* Controles na tela em uso agora */
int touch_enabled(void);

/* Um evento do SDL (toques). Nos menus do PC os botões viram teclas (direcional = setas,
 * A/Start = Enter, B = Esc) e o botão de menu é o Esc sempre: devolve 1 e a tecla em *key. */
int touch_event(const SDL_Event *ev, int menu_open, SDL_Keycode *key);

/* Botões do jogo (PAD_*) apertados na tela */
uint32_t touch_buttons(void);

/* Desenha os controles por cima do quadro já apresentado (coordenadas da janela) */
void touch_draw(SDL_Renderer *ren);

#endif
