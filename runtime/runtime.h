/* Interfaces internas do runtime (fora do código gerado). */
#ifndef PM_RUNTIME_H
#define PM_RUNTIME_H

#include <stdint.h>
#include <stdio.h>
#include "cpu.h"

/* Ciclos do 68000 por linha (NTSC: 3420 ciclos mestres / 7) e linhas por quadro */
#define CYCLES_PER_LINE 488
#define LINES_PER_FRAME 262

extern uint32_t cpu_pc;
extern int cpu_interp_insns;
extern int lag_frames, vblank_count, lag_debug;
extern int cpu_overclock;   /* fator de velocidade do 68000 (1 = original) */
extern int64_t overclock_remainder;

/* cpu.c */
void cpu_init(const char *missing_path);
void cpu_run(int64_t until);
void cpu_reset(void);
void musashi_load_state(void);
void musashi_store_state(void);
uint32_t lockstep_block(BlockFn fn, uint32_t pc, int routine);

/* game/: rotinas do jogo reescritas em C */
extern int map_stream_extra;   /* metatiles extras carregados de cada lado (widescreen) */

/* bus.c */
extern uint8_t *rom_data;
extern uint32_t rom_size;
extern uint8_t work_ram[0x10000];
extern uint8_t z80_ram[0x2000];
int bus_load_rom(const char *path, const char *expected_sha1);
uint32_t bus_read16_raw(uint32_t addr);
uint32_t bus_read8_raw(uint32_t addr);
uint32_t bus_read32_raw(uint32_t addr);
void bus_write8_raw(uint32_t addr, uint32_t v);
void bus_write16_raw(uint32_t addr, uint32_t v);
void bus_write32_raw(uint32_t addr, uint32_t v);

/* vdp.c */
#define SCREEN_W_MAX 448
#define SCREEN_H 224
extern uint32_t framebuffer[SCREEN_H][SCREEN_W_MAX];
extern int screen_width, base_width, wide_extra, wide_pillarbox, wide_clip_planes;
extern int hide_planes;        /* planos (1=A, 2=B) escondidos na tela toda */
extern int sprite_unlimited;   /* sem limite de sprites/pixels por linha (melhoria) */
void vdp_set_widescreen(int extra);
void vdp_reset(void);
void vdp_dump(FILE *f);
uint32_t vdp_vram_read16(uint32_t a);          /* runtime: VRAM, plano A e largura dos planos */
void vdp_vram_write16(uint32_t a, uint32_t v);
uint32_t vdp_plane_a_base(void);
int vdp_plane_width(void);   /* depuração: VRAM, CRAM, VSRAM e registradores */
uint32_t vdp_read_data(void);
uint32_t vdp_read_status(void);
uint32_t vdp_read_hv(void);
void vdp_write_data(uint32_t v);
void vdp_write_control(uint32_t v);
void vdp_begin_line(int line);
int irq_pending_level(void);
void irq_acknowledge(int level);

/* io.c */
enum {
    PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8,
    PAD_B = 16, PAD_C = 32, PAD_A = 64, PAD_START = 128,
};
extern uint32_t pad_state[2];
uint32_t io_read(uint32_t addr);
void io_write(uint32_t addr, uint32_t v);

/* sha1.c */
void sha1_hex(const uint8_t *data, size_t len, char out[41]);

#endif
