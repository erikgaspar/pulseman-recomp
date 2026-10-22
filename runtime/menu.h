/* Menu de pausa do runtime (fora do jogo) e configurações salvas em pulseman.ini. */
#ifndef PM_MENU_H
#define PM_MENU_H

#include <stdint.h>
#include "runtime.h"

typedef struct {
    int widescreen;   /* 0/1 */
    int no_slowdown;  /* 0/1: overclock do 68000 */
    int volume;       /* 0..10 (x10%) */
    int display;      /* 0 = janela 2x, 1 = 3x, 2 = 4x, 3 = tela cheia */
    int filter;       /* FILTER_* (filters.h): nítido, suave, pixel suave, scanlines, TV... */
    int no_sprite_limit; /* 0/1: sem o limite de sprites por linha do VDP */
    int language;     /* LANG_*: idioma dos menus do runtime */
    int black_border; /* 0/1: telas em 4:3 com faixas pretas em vez da moldura desfocada */
} Settings;

enum { LANG_PT, LANG_EN, LANG_ES, LANG_COUNT };

extern Settings settings;

/* Devolvem 0 se não conseguiram abrir/gravar o arquivo (caminho em UTF-8) */
int settings_load(const char *path);
int settings_save(const char *path);

enum { MENU_NONE, MENU_CLOSE, MENU_CHANGED, MENU_RESET, MENU_QUIT, MENU_GAME_OPTIONS,
       MENU_SAVE, MENU_LOAD,     /* salvar/carregar o slot menu_slot() */
       MENU_NEW_GAME };          /* seleção de jogo do título: começar um jogo novo */

int menu_is_open(void);
void menu_open(void);
void menu_open_title(void);
void menu_close(void);
int menu_slot(void);
void menu_open_slots(int load);   /* teste */
/* START no título: novo jogo, continuar um slot ou apagar */
void menu_open_files(void);
/* teste: arquivo escolhido, foco (0 = arquivos, 1 = botões), modo apagar e confirmação */
void menu_files_test(int card, int focus, int deleting, int dialog);
/* teste: página (0 principal, 1 configurações, 2 salvar, 3 carregar), item e confirmação */
void menu_page_test(int page, int item, int dialog);
/* Resultado de MENU_SAVE/MENU_LOAD, para o menu mostrar (e atualizar os slots) */
void menu_slot_done(int ok);
/* Trata uma tecla (SDL_Keycode) e diz o que o programa deve fazer */
int menu_key(int key);
/* Desenha o jogo escurecido com o menu por cima */
void menu_render(uint32_t dst[][SCREEN_W_MAX], uint32_t src[][SCREEN_W_MAX], int width);

#endif
