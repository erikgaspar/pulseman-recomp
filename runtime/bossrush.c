/* Modo Boss Rush (ver bossrush.h).
 *
 * Título ($27FE): START e OPTIONS são textos na fonte do jogo, escritos no plano A pela
 * rotina $1FE8 (registros em $2908: linhas 17 e 19, coluna 16), e o cursor é o caractere ':'
 * na coluna 14 da linha do item ($FFF807 = 0 ou 8; pisca pelo bit 4 de $FFF805). Cima/baixo
 * só alternam o bit 3 de $FFF807. O terceiro item, "BOSS RUSH", vai na linha 21 com a mesma
 * fonte (base e atributos lidos do 'S' de START); com ele escolhido o jogo continua achando
 * que o cursor está em OPTIONS, e o cursor dele é apagado e desenhado na linha 21.
 *
 * Início: um jogo novo pelo caminho normal (Start virtual com o cursor em START). Na seleção
 * de fases (cena $20), coberta por uma tela preta, a fase ($FFF9AA) e o checkpoint dela
 * ($FFF9AC + fase) apontam para a arena do primeiro chefe e o Start é apertado sozinho: o
 * mapa inicial é 1 + tamanhos das fases anteriores ($FFF9B4) + checkpoint ($3AD6).
 *
 * Vitória: o jogo só pula o Stage Clear em alguns mapas ($21A8C: $11, $41, $45, $46, que ligam
 * o bit 0 de $FFFAEE direto); na corrida a comparação com $46 vale o mapa da arena (IMM em
 * $21AA0), então a arena sai sozinha, sem o Stage Clear.
 *
 * Corrida: as vidas extras ($FFFD44, BCD) ficam em zero, então morrer leva ao continue ($18),
 * que vira o fim da corrida. Toda arena sai por $230AE quando o chefe foi derrotado (bit 0
 * de $FFFAEE), que avança $FFFD4A e pede a próxima cena: no gancho de troca de cena ($83A)
 * o mapa, a fase e o checkpoint passam a ser os do próximo chefe e a cena vira $08 (fase).
 * Depois do último, volta ao título com o resultado. A energia segue a do jogo.
 *
 * O cronômetro conta os quadros de fase (60 por segundo), parado na pausa do jogo. O
 * recorde fica no .ini (boss_rush_recorde, em quadros). O lockstep não passa por aqui. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bossrush.h"
#include "menu.h"

#define SCENE_TITLE 0x04
#define SCENE_STAGE 0x08
#define SCENE_CONTINUE 0x18
#define SCENE_SELECT 0x20

/* arenas: fase (0..6), checkpoint dentro da fase e mapa ($FFFD4A) */
static const struct { int stage, offset, map; } bosses[] = {
    {0, 6, 7},     /* fase 1: o chefe de óculos, no mundo virtual */
    {1, 4, 15},
    {2, 6, 23},
    {3, 6, 31},
    {4, 14, 47},
    {5, 8, 57},    /* fase 6: o submarino */
    {6, 11, 70},   /* fase 7: Doc Waruyama */
};
#define N_BOSSES ((int)(sizeof(bosses) / sizeof(bosses[0])))

enum { BR_OFF, BR_STARTING, BR_RUN, BR_RESULT };
static int state = BR_OFF;
static int sel;               /* título: 1 = cursor em BOSS RUSH */
static int idx;               /* chefe atual */
static int frames;            /* cronômetro */
static int won, new_record, record_pending, result_frames;
static int prev_pad;

void bossrush_reset(void)
{
    state = BR_OFF;
    sel = idx = frames = won = new_record = record_pending = result_frames = 0;
    prev_pad = 0;
}

/* IMM em $21AA0: o mapa da arena atual durante a corrida (vence sem Stage Clear) */
int bossrush_no_clear_map(void)
{
#ifndef PM_LOCKSTEP
    if (state == BR_RUN)
        return bosses[idx].map;
#endif
    return 0x46;
}

int bossrush_take_record(void)
{
    int r = record_pending;
    record_pending = 0;
    return r;
}

static int scene_now(void)
{
    return work_ram[0xF803] & 0x7C;
}

/* título montado e com os itens na tela (o jogo só aceita Start com $FFF804 < $23A) */
static int title_ready(void)
{
    return scene_now() == SCENE_TITLE && (work_ram[0xF802] & 0x80) && !work_ram[0xF808] &&
           (work_ram[0xF804] << 8 | work_ram[0xF805]) < 0x23A;
}

static void set_target(void)
{
    work_ram[0xF9AA] = (uint8_t)bosses[idx].stage;
    work_ram[0xF9AC + bosses[idx].stage] = (uint8_t)bosses[idx].offset;
}

static uint32_t filter(uint32_t pad, int *start)
{
    *start = 0;
#ifdef PM_LOCKSTEP
    return pad;
#else
    int pressed = (int)pad & ~prev_pad;
    prev_pad = (int)pad;
    int scene = scene_now();

    if (state == BR_RESULT) {
        /* resultado por cima do título: some com um botão (que não chega ao jogo) ou em 10 s */
        if (scene != SCENE_TITLE)
            state = BR_OFF;
        else if (++result_frames > 600 || (pressed & (PAD_START | PAD_A | PAD_B | PAD_C) && result_frames > 60))
            state = BR_OFF, pad &= ~(uint32_t)(PAD_START | PAD_A | PAD_B | PAD_C);
        else
            return pad & ~(uint32_t)(PAD_START | PAD_A | PAD_B | PAD_C | PAD_UP | PAD_DOWN);
    }

    if (state == BR_OFF) {
        if (!title_ready()) {
            sel = 0;
            return pad;
        }
        int cur = work_ram[0xF807];   /* 0 = START, 8 = OPTIONS */
        if (sel) {
            if (pressed & PAD_UP) {                  /* BOSS RUSH → OPTIONS */
                sel = 0;
                return pad & ~(uint32_t)PAD_UP;
            }
            if (pressed & PAD_DOWN) {                /* BOSS RUSH → START */
                sel = 0;
                work_ram[0xF807] = 0;
                return pad & ~(uint32_t)PAD_DOWN;
            }
            if (pressed & PAD_START) {
                state = BR_STARTING;
                sel = 0;
                idx = 0;
                frames = 0;
                won = new_record = 0;
                work_ram[0xF807] = 0;                /* jogo novo */
                *start = 1;
            }
            return pad & ~(uint32_t)(PAD_START | PAD_UP | PAD_DOWN);
        }
        if (cur == 8 && (pressed & PAD_DOWN)) {      /* OPTIONS → BOSS RUSH */
            sel = 1;
            return pad & ~(uint32_t)PAD_DOWN;
        }
        if (cur == 0 && (pressed & PAD_UP)) {        /* START → BOSS RUSH */
            sel = 1;
            work_ram[0xF807] = 8;
            return pad & ~(uint32_t)PAD_UP;
        }
        return pad;
    }

    if (state == BR_STARTING) {
        if (scene == SCENE_SELECT && (work_ram[0xF802] & 0x80) &&
            (work_ram[0xF800] << 8 | work_ram[0xF801]) == 0xFFFF) {
            /* seleção de fases montada: faz o que a confirmação de fase faz ($38AE: mapa
             * por $3AD6, bit 7 de $FFFB66 desligado, bit 0 de $FFFD32 ligado, cena $08),
             * sem esperar o mapa-múndi */
            set_target();
            work_ram[0xFD4A] = (uint8_t)(bosses[idx].map >> 8);
            work_ram[0xFD4B] = (uint8_t)bosses[idx].map;
            work_ram[0xFB66] &= 0x7F;
            work_ram[0xFD32] |= 0x01;
            work_ram[0xF800] = 0;
            work_ram[0xF801] = SCENE_STAGE;
            return pad & ~(uint32_t)(PAD_START | PAD_A | PAD_B | PAD_C);
        }
        if (scene == SCENE_STAGE && (work_ram[0xFD4A] << 8 | work_ram[0xFD4B]) == bosses[idx].map) {
            state = BR_RUN;
            return pad;
        }
        if (scene == SCENE_TITLE && (work_ram[0xF802] & 0x80) && work_ram[0xF808])
            ;   /* jogo novo saindo do título */
        return pad & ~(uint32_t)(PAD_START | PAD_A | PAD_B | PAD_C);
    }

    /* BR_RUN */
    if (scene == SCENE_TITLE || scene == 0x00) {   /* reset ou volta ao título pelo menu */
        state = BR_OFF;
        return pad;
    }
    return pad;
#endif
}

/* Uma tecla que o modo tirou do jogo continua fora dele enquanto estiver segura: senão, no
 * quadro seguinte o jogo a veria como um aperto novo (o cursor andava duas vezes). */
uint32_t bossrush_filter(uint32_t pad, int *start)
{
    static uint32_t eaten;
    uint32_t out = filter(pad, start);
    eaten = (eaten | (pad & ~out)) & pad;
    return out & ~eaten;
}

void bossrush_frame(void)
{
#ifndef PM_LOCKSTEP
    /* o copyright do título ($2C38: posição na VRAM e texto, escrito por $2C1A) vai da linha
     * 23 para a 25, para não ficar colado no item BOSS RUSH (linha 21) */
    if (rom_data[0x2C38] == 0x4B && rom_data[0x2C39] == 0x8E)
        rom_data[0x2C38] = 0x4C;
    /* cursor do jogo em OPTIONS ($293A: 499C 0003 ':'): espaço com BOSS RUSH escolhido */
    if (rom_data[0x293A] == 0x49 && rom_data[0x293B] == 0x9C)
        rom_data[0x293E] = state == BR_OFF && sel ? ' ' : ':';
    if (state == BR_RUN) {
        work_ram[0xFD44] = 0;   /* uma chance só */
        int paused = work_ram[0xFB07] & 0x20;
        if (scene_now() == SCENE_STAGE && !paused)
            frames++;
    }
    if (state == BR_RESULT && scene_now() == SCENE_TITLE && (work_ram[0xF802] & 0x80)) {
        /* o título conta $FFF804 até chamar a abertura: parado enquanto o resultado está na
         * tela (senão a abertura começava por baixo da tela preta) */
        work_ram[0xF804] = 0x02;
        work_ram[0xF805] = 0x58;
    }
    if (state != BR_OFF || !title_ready())
        return;
    /* item BOSS RUSH na linha 21, com a fonte e os atributos do 'S' de START (linha 17) */
    int pw = vdp_plane_width();
    uint32_t base = vdp_plane_a_base();
    uint32_t s = vdp_vram_read16(base + (17 * pw + 16) * 2);
    int fbase = (int)(s & 0x7FF) - 'S';
    if (fbase < 0 || fbase + 0x60 > 0x7FF)
        return;   /* título ainda sem os itens */
    uint32_t attr = s & 0xF800;
    static const char item[] = "BOSS RUSH";
    for (int i = 0; item[i]; i++)
        vdp_vram_write16(base + (21 * pw + 16 + i) * 2, attr | (uint32_t)(fbase + item[i]));
    int blink_on = !(work_ram[0xF805] & 0x10);
    vdp_vram_write16(base + (21 * pw + 14) * 2, attr | (uint32_t)(fbase + (sel && blink_on ? ':' : ' ')));
    if (sel) {
        /* o jogo continua com o cursor em OPTIONS e o redesenha a cada quadro (às vezes
         * depois desta escrita): o registro dele ($293A: posição, ':') passa a escrever um
         * espaço enquanto BOSS RUSH estiver escolhido. O de START (vindo de START direto
         * para cá) o jogo não apaga: apaga aqui. */
        vdp_vram_write16(base + (19 * pw + 14) * 2, attr | (uint32_t)(fbase + ' '));
        vdp_vram_write16(base + (17 * pw + 14) * 2, attr | (uint32_t)(fbase + ' '));
    }
#endif
}

int bossrush_scene_change(int scene)
{
#ifdef PM_LOCKSTEP
    return scene;
#else
    static int debug = -1;
    if (debug < 0)
        debug = getenv("PM_DEBUG_BOSSRUSH") != NULL;
    if (debug)
        fprintf(stderr, "boss rush: troca de cena %02X no quadro %d (estado %d, chefe %d, mapa %d)\n", scene,
                vblank_count, state, idx + 1, work_ram[0xFD4A] << 8 | work_ram[0xFD4B]);
    if (state != BR_RUN)
        return scene;
    int map = work_ram[0xFD4A] << 8 | work_ram[0xFD4B];
    if (scene == SCENE_CONTINUE) {   /* morreu sem vidas: fim da corrida */
        state = BR_RESULT;
        won = 0;
        result_frames = 0;
        return SCENE_TITLE;
    }
    if (map != bosses[idx].map + 1)
        return scene;                 /* morreu e recomeça a arena, ou outra troca */
    if (++idx >= N_BOSSES) {          /* último chefe derrotado */
        state = BR_RESULT;
        won = 1;
        result_frames = 0;
        if (!settings.boss_best || frames < settings.boss_best) {
            settings.boss_best = frames;
            new_record = record_pending = 1;
        }
        bus_write32_raw(0xFFDEA2, 0);
        return SCENE_TITLE;
    }
    work_ram[0xFD4A] = (uint8_t)(bosses[idx].map >> 8);
    work_ram[0xFD4B] = (uint8_t)bosses[idx].map;
    set_target();
    return SCENE_STAGE;
#endif
}

static void format_time(int f, char *out, size_t n)
{
    int cs = f % 60 * 100 / 60, s = f / 60;
    snprintf(out, n, "%02d:%02d.%02d", s / 60, s % 60, cs);
}

static void text_outline(uint32_t (*fb)[SCREEN_W_MAX], int width, int x, int y, const char *t, uint32_t c)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy)
                menu_draw_text(fb, width, x + dx, y + dy, t, 0xFF000000u);
    menu_draw_text(fb, width, x, y, t, c);
}

static void text_center(uint32_t (*fb)[SCREEN_W_MAX], int width, int cx, int y, const char *t, uint32_t c)
{
    text_outline(fb, width, cx - menu_text_width(t) / 2, y, t, c);
}

#define T(pt, en, es) (settings.language == LANG_EN ? (en) : settings.language == LANG_ES ? (es) : (pt))

void bossrush_draw(uint32_t (*fb)[SCREEN_W_MAX], int width, int extra)
{
    int scene = scene_now();
    int cx = extra + base_width / 2;
    char t[64], b[64];
    if (state == BR_STARTING && scene != SCENE_TITLE) {
        /* a seleção de fases passa por baixo: tela preta com o nome do modo */
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < width; x++)
                fb[y][x] = 0xFF000000u;
        text_center(fb, width, cx, 100, "BOSS RUSH", 0xFFFFD84Fu);
        text_center(fb, width, cx, 116, T("7 CHEFES, UMA VIDA", "7 BOSSES, ONE LIFE", "7 JEFES, UNA VIDA"), 0xFFB4BECEu);
        return;
    }
    if (state == BR_RUN && scene == SCENE_STAGE) {
        format_time(frames, t, sizeof(t));
        snprintf(b, sizeof(b), "%s %d/%d  %s", T("CHEFE", "BOSS", "JEFE"), idx + 1, N_BOSSES, t);
        text_center(fb, width, cx, 4, b, 0xFFFFFFFFu);
        return;
    }
    if (state == BR_RESULT && scene == SCENE_TITLE) {
        /* tela preta só para o resultado: o título se monta por baixo e aparece quando ela
         * some (botão ou 10 s); o texto entra depois do fim da fase escurecer */
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < width; x++)
                fb[y][x] = 0xFF000000u;
        if (result_frames < 40)
            return;
        int y0 = 76;
        text_center(fb, width, cx, y0, "BOSS RUSH", 0xFFB4BECEu);
        format_time(frames, t, sizeof(t));
        if (won) {
            text_center(fb, width, cx, y0 + 20, T("COMPLETO!", "CLEARED!", "¡COMPLETADO!"), 0xFFFFD84Fu);
            snprintf(b, sizeof(b), "%s %s", T("TEMPO", "TIME", "TIEMPO"), t);
            text_center(fb, width, cx, y0 + 40, b, 0xFFFFFFFFu);
        } else {
            text_center(fb, width, cx, y0 + 20, "GAME OVER", 0xFFFF6A6Au);
            snprintf(b, sizeof(b), "%s %d/%d  %s", T("CHEFE", "BOSS", "JEFE"), idx + 1, N_BOSSES, t);
            text_center(fb, width, cx, y0 + 40, b, 0xFFFFFFFFu);
        }
        if (new_record) {
            text_center(fb, width, cx, y0 + 60, T("NOVO RECORDE!", "NEW RECORD!", "¡NUEVO RÉCORD!"), 0xFF4FD8FFu);
        } else if (settings.boss_best) {
            format_time(settings.boss_best, t, sizeof(t));
            snprintf(b, sizeof(b), "%s %s", T("RECORDE", "RECORD", "RÉCORD"), t);
            text_center(fb, width, cx, y0 + 60, b, 0xFFB4BECEu);
        }
        return;
    }
    if (state == BR_OFF && settings.boss_best && title_ready() && sel) {
        /* recorde ao lado do item */
        format_time(settings.boss_best, t, sizeof(t));
        snprintf(b, sizeof(b), "%s %s", T("RECORDE", "RECORD", "RÉCORD"), t);
        text_center(fb, width, cx, 200, b, 0xFFB4BECEu);
    }
}
