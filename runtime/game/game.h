/* Ajustes do jogo usados pelo código gerado (overrides, hooks e constantes de patch). */
#ifndef PM_GAME_H
#define PM_GAME_H

extern int map_stream_extra;   /* map_stream.c: metatiles extras de cada lado */
extern int hud_shift;          /* hud.c: px que o HUD da esquerda vai para a esquerda */
extern int hud_shift_right;    /* hud.c: px que o HUD da direita vai para a direita */
extern int wide_sprite_extra;  /* objects.c: px extras de área de desenho à direita */

/* objects.c */
void hook_object_screen_x(void);
void hook_debug_spawn(void);
void hook_debug_map(void);
void hook_sweep_begin(void);
void hook_sweep_match(void);
void hook_sweep_left_edge(void);
void hook_sweep_match_end(void);
void hook_sweep_end(void);
void debug_check_spawns(int left_cols, int right_cols);

#endif
