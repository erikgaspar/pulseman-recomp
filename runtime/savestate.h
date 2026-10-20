/* Jogos salvos (save states): o estado inteiro do console em 3 slots, pelo menu. */
#ifndef PM_SAVESTATE_H
#define PM_SAVESTATE_H

#include <stddef.h>
#include <stdint.h>

#define STATE_SLOTS 3
#define THUMB_W_MAX 112
#define THUMB_H 56     /* quadro do jogo reduzido 4x */

/* Leitura/gravação de um bloco de estado: o mesmo código salva e carrega */
typedef struct {
    uint8_t *buf;
    size_t pos, cap;
    int loading;
    int error;
} StateIO;

void state_bytes(StateIO *s, void *p, size_t n);
#define STATE_VAR(s, v) state_bytes((s), &(v), sizeof(v))

/* Cada módulo salva/restaura o seu estado (vdp.c, sound.c, io.c, ym_shim.cpp) */
void vdp_state(StateIO *s);
void sound_state(StateIO *s);
void io_state(StateIO *s);
void ym_state(StateIO *s);

typedef struct {
    int used;
    int64_t time;    /* quando foi salvo (time_t) */
    int in_stage;    /* salvo numa fase: vidas e pontos valem */
    int lives;       /* $FFFD44 (BCD) */
    uint32_t score;  /* $FFFD3C (BCD, 8 dígitos) */
    int thumb_w;
    uint32_t thumb[THUMB_H][THUMB_W_MAX];
} StateInfo;

/* Pastas dos arquivos (UTF-8, com a barra no fim): a primeira e, se ela não aceitar
 * gravação, a segunda (pasta de dados do usuário) */
void state_set_dirs(const char *dir, const char *fallback);
int state_save_slot(int slot);
int state_load_slot(int slot);
int state_slot_info(int slot, StateInfo *info);
int state_delete_slot(int slot);   /* apaga o arquivo do slot (nas duas pastas) */

#endif
