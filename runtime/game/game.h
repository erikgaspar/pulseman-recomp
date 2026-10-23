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
void hook_scene_change(void);   /* opening.c */
int opening_music(void);         /* opening.c: música da história da abertura ($22 no original) */
int opening_title_music(void);   /* opening.c: música do título ($06; a da abertura na passagem) */
void hook_map_edit_column(void); /* map_stream.c */
int bossrush_no_clear_map(void); /* bossrush.c: mapa sem Stage Clear ($46 fora do Boss Rush) */
void debug_check_spawns(int left_cols, int right_cols);

#endif
