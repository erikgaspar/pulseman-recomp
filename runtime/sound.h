/* Som: Z80 (driver de som do jogo), YM2612 (ymfm) e PSG SN76489. */
#ifndef PM_SOUND_H
#define PM_SOUND_H

#include <stdint.h>

/* ym_shim.cpp */
void ym_reset(void);
void ym_write(int port, int data);
int ym_read_status(void);
void ym_generate(int32_t out[2]);

/* sound.c */
void sound_init(int sdl_output, const char *wav_path);
void sound_shutdown(void);
void sound_set_frame_rate(double fps);   /* ritmo real dos quadros, para o controle de taxa */
void sound_reset(void);                  /* reset do console (Z80, YM2612, PSG) */
void sound_set_volume(int percent);      /* volume da saída de áudio, 0..100 */
void sound_pause(int paused);            /* pausa a saída (menu aberto) */

/* Acessos do 68000 à área do Z80 (A00000-A0FFFF) e às linhas de controle */
uint32_t sound_68k_read8(uint32_t addr);
void sound_68k_write8(uint32_t addr, uint32_t v);
uint32_t sound_busreq_read(void);
void sound_set_busreq(int on);
void sound_set_reset(int asserted);
void sound_psg_write_68k(uint32_t v);

/* Laço de quadro */
void sound_run_line(int line);   /* avança o Z80 até o fim da linha atual do 68000 */
void sound_end_frame(void);

#endif
