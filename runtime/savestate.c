/* Jogos salvos: CPU, RAM, VDP, Z80, YM2612, PSG e controles num arquivo por slot
 * (pulseman-slotN.sav), com uma miniatura da tela para o menu.
 * O código recompilado só volta ao despachante entre blocos, então entre dois quadros
 * todo o estado do 68000 está em 'cpu' e 'cpu_pc' (nada fica na pilha do C). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <wchar.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "runtime.h"
#include "savestate.h"

#define STATE_MAGIC "PMSTATE"
#define STATE_VERSION 1

static char dirs[2][1024];

void state_bytes(StateIO *s, void *p, size_t n)
{
    if (s->error)
        return;
    if (s->loading) {
        if (s->pos + n > s->cap) {
            s->error = 1;
            return;
        }
        memcpy(p, s->buf + s->pos, n);
    } else {
        if (s->pos + n > s->cap) {
            size_t cap = s->cap ? s->cap * 2 : 1 << 20;
            while (cap < s->pos + n)
                cap *= 2;
            uint8_t *b = realloc(s->buf, cap);
            if (!b) {
                s->error = 1;
                return;
            }
            s->buf = b;
            s->cap = cap;
        }
        memcpy(s->buf + s->pos, p, n);
    }
    s->pos += n;
}

/* Estado do console inteiro, na mesma ordem para salvar e carregar */
static void machine_state(StateIO *s)
{
    STATE_VAR(s, cpu);
    STATE_VAR(s, cpu_pc);
    STATE_VAR(s, overclock_remainder);
    state_bytes(s, work_ram, sizeof(work_ram));
    state_bytes(s, z80_ram, sizeof(z80_ram));
    vdp_state(s);
    sound_state(s);
    ym_state(s);
    io_state(s);
}

/* Cabeçalho do arquivo, seguido do estado */
typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t body_size;
    int64_t time;
    int32_t thumb_w;
} Header;

void state_set_dirs(const char *dir, const char *fallback)
{
    snprintf(dirs[0], sizeof(dirs[0]), "%s", dir ? dir : "");
    snprintf(dirs[1], sizeof(dirs[1]), "%s", fallback ? fallback : "");
}

static void slot_path(char *out, size_t n, int d, int slot)
{
    snprintf(out, n, "%spulseman-slot%d.sav", dirs[d], slot + 1);
}

/* Miniatura: média de cada bloco de 4x4 pixels do último quadro */
static int make_thumb(uint32_t thumb[THUMB_H][THUMB_W_MAX])
{
    int w = screen_width / 4;
    if (w > THUMB_W_MAX)
        w = THUMB_W_MAX;
    for (int y = 0; y < THUMB_H; y++)
        for (int x = 0; x < w; x++) {
            uint32_t r = 0, g = 0, b = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    uint32_t c = framebuffer[y * 4 + j][x * 4 + i];
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                }
            thumb[y][x] = 0xFF000000u | (r / 16) << 16 | (g / 16) << 8 | (b / 16);
        }
    return w;
}

static int write_file(const char *path, const Header *h, const uint32_t thumb[THUMB_H][THUMB_W_MAX],
                      const StateIO *body)
{
    SDL_RWops *f = SDL_RWFromFile(path, "wb");
    if (!f)
        return 0;
    int ok = SDL_RWwrite(f, h, sizeof(*h), 1) == 1;
    for (int y = 0; y < THUMB_H && ok; y++)
        ok = SDL_RWwrite(f, thumb[y], 4, h->thumb_w) == (size_t)h->thumb_w;
    ok = ok && SDL_RWwrite(f, body->buf, 1, body->pos) == body->pos;
    return SDL_RWclose(f) == 0 && ok;
}

int state_save_slot(int slot)
{
    static uint32_t thumb[THUMB_H][THUMB_W_MAX];
    StateIO s = {0};
    machine_state(&s);
    if (s.error) {
        free(s.buf);
        return 0;
    }
    Header h = {STATE_MAGIC, STATE_VERSION, (uint32_t)s.pos, (int64_t)time(NULL), 0};
    h.thumb_w = make_thumb(thumb);
    char path[1100];
    int ok = 0;
    for (int d = 0; d < 2 && !ok; d++) {
        if (d == 1 && (!dirs[1][0] || !strcmp(dirs[0], dirs[1])))
            break;
        slot_path(path, sizeof(path), d, slot);
        ok = write_file(path, &h, thumb, &s);
        if (ok && d == 1)   /* a pasta principal não aceita gravação: passa a usar a outra */
            strcpy(dirs[0], dirs[1]);
    }
    if (!ok)
        fprintf(stderr, "não foi possível salvar o slot %d em %s\n", slot + 1, path);
    free(s.buf);
    return ok;
}

/* Abre o arquivo do slot (pasta principal, senão a outra) e lê o cabeçalho */
static SDL_RWops *open_slot(int slot, Header *h)
{
    char path[1100];
    for (int d = 0; d < 2; d++) {
        if (!dirs[d][0] && d == 1)
            break;
        slot_path(path, sizeof(path), d, slot);
        SDL_RWops *f = SDL_RWFromFile(path, "rb");
        if (!f)
            continue;
        if (SDL_RWread(f, h, sizeof(*h), 1) == 1 && !memcmp(h->magic, STATE_MAGIC, 8) &&
            h->version == STATE_VERSION && h->thumb_w > 0 && h->thumb_w <= THUMB_W_MAX)
            return f;
        SDL_RWclose(f);
    }
    return NULL;
}

int state_slot_info(int slot, StateInfo *info)
{
    Header h;
    info->used = 0;
    SDL_RWops *f = open_slot(slot, &h);
    if (!f)
        return 0;
    info->time = h.time;
    info->thumb_w = h.thumb_w;
    int ok = 1;
    for (int y = 0; y < THUMB_H && ok; y++)
        ok = SDL_RWread(f, info->thumb[y], 4, h.thumb_w) == (size_t)h.thumb_w;
    /* vidas, pontos e cena direto da RAM salva (logo depois de cpu, cpu_pc e o resto
     * do overclock, como em machine_state) */
    Sint64 ram = SDL_RWtell(f) + sizeof(cpu) + sizeof(cpu_pc) + sizeof(overclock_remainder);
    uint8_t scene = 0, lives = 0, score[4] = {0};
    info->in_stage = 0;
    if (ok && SDL_RWseek(f, ram + 0xF803, RW_SEEK_SET) >= 0 && SDL_RWread(f, &scene, 1, 1) == 1 &&
        SDL_RWseek(f, ram + 0xFD3C, RW_SEEK_SET) >= 0 && SDL_RWread(f, score, 1, 4) == 4 &&
        SDL_RWseek(f, ram + 0xFD44, RW_SEEK_SET) >= 0 && SDL_RWread(f, &lives, 1, 1) == 1) {
        scene &= 0x7C;
        info->in_stage = scene == 0x08 || scene == 0x1C;
        info->lives = (lives >> 4) * 10 + (lives & 15);
        info->score = (uint32_t)score[0] << 24 | score[1] << 16 | score[2] << 8 | score[3];
    }
    SDL_RWclose(f);
    info->used = ok;
    return ok;
}

int state_load_slot(int slot)
{
    Header h;
    SDL_RWops *f = open_slot(slot, &h);
    if (!f)
        return 0;
    StateIO s = {0};
    s.loading = 1;
    s.cap = h.body_size;
    s.buf = malloc(s.cap ? s.cap : 1);
    int ok = s.buf && SDL_RWseek(f, (Sint64)h.thumb_w * THUMB_H * 4, RW_SEEK_CUR) >= 0 &&
             SDL_RWread(f, s.buf, 1, s.cap) == s.cap;
    SDL_RWclose(f);
    if (!ok) {
        free(s.buf);
        return 0;
    }
    /* guarda o estado atual: se o arquivo não bater (outra versão do programa), volta a ele */
    StateIO backup = {0};
    machine_state(&backup);
    machine_state(&s);
    if (s.error || s.pos != s.cap) {
        fprintf(stderr, "slot %d incompatível com esta versão\n", slot + 1);
        backup.cap = backup.pos;
        backup.pos = 0;
        backup.loading = 1;
        machine_state(&backup);
        ok = 0;
    }
    free(backup.buf);
    free(s.buf);
    return ok;
}

/* O caminho é UTF-8 (pasta com acento): no Windows o remove() usaria a página ANSI */
static int remove_utf8(const char *path)
{
#ifdef _WIN32
    wchar_t *w = (wchar_t *)SDL_iconv_string("UTF-16LE", "UTF-8", path, strlen(path) + 1);
    if (!w)
        return -1;
    int r = _wremove(w);
    SDL_free(w);
    return r;
#else
    return remove(path);
#endif
}

int state_delete_slot(int slot)
{
    char path[1100];
    int ok = 1;
    for (int d = 0; d < 2; d++) {
        if (d == 1 && (!dirs[1][0] || !strcmp(dirs[0], dirs[1])))
            break;
        slot_path(path, sizeof(path), d, slot);
        SDL_RWops *f = SDL_RWFromFile(path, "rb");
        if (!f)
            continue;   /* não existe nesta pasta */
        SDL_RWclose(f);
        if (remove_utf8(path) != 0) {
            fprintf(stderr, "não foi possível apagar %s\n", path);
            ok = 0;
        }
    }
    return ok;
}
