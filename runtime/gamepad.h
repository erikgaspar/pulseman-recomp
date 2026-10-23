/* Controles (joysticks) pela API GameController do SDL (gamepad.c): Xbox, PlayStation,
 * Switch Pro, 8BitDo e a maioria dos genéricos, conectados com o jogo aberto ou não. */
#ifndef PM_GAMEPAD_H
#define PM_GAMEPAD_H

#include <stdint.h>
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

/* Depois do SDL_Init com SDL_INIT_GAMECONTROLLER: mapeamentos extras (gamecontrollerdb.txt
 * na pasta do executável, se houver) */
void gamepad_init(const char *base_dir);

/* Um evento do SDL: conecta/desconecta controles e, para os menus do PC, traduz botões em
 * teclas (Back/Guide = Esc sempre; com o menu aberto: direcional = setas, A/Start = Enter,
 * B = Esc). Devolve 1 e a tecla em *key quando o evento vira uma tecla. */
int gamepad_event(const SDL_Event *ev, int menu_open, SDL_Keycode *key);

/* Quantos controles estão conectados (os controles na tela somem quando há algum) */
int gamepad_count(void);

/* Botões do jogo (PAD_*) de todos os controles conectados */
uint32_t gamepad_buttons(void);

/* Teste com controle virtual (PM_GAMEPAD_TEST=1): uma vez por volta do laço principal */
void gamepad_test_tick(void);

#endif
