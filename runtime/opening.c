/* Abertura do jogo em destaque (ver opening.h).
 *
 * No original a abertura (cena $14: o terminal "LOG ON" e a história do Dr. Yoshiyama) só
 * aparece se ninguém apertar nada no título ($29E4), qualquer botão a interrompe e depois
 * dela vem a demonstração ($1C). Aqui:
 * - Start na cena da SEGA (inclusive durante os logos extras) leva à abertura pelo caminho
 *   do próprio jogo: a SEGA termina ($FFF804 = 1, logos pulados), o título se monta (a
 *   estática e a paleta dele vêm desse caminho; os quadros dele ficam pretos) e o contador
 *   de espera do título ($FFF804) vai a 1, então o jogo chama a abertura ($29DA);
 * - vinda dos logos, ela começa direto na história: o terminal (estados 0-6 de $FFFACE,
 *   tabela em $98FE) é pulado indo para o estado 7 ($9C5E: efeito sonoro $4F a cada quadro,
 *   escurece depois de $FFFAD0 quadros; aqui 1, já que a tela está preta) e o estado 8
 *   ($9C8E) monta a história do zero; até lá a tela fica preta.
 *   A música da história é a da fase 2 ($0B) no lugar da original ($22, com um longo
 *   silêncio no começo). O jogo a pede no fim do terminal (IMM em $9B10); vinda dos logos, o
 *   título de passagem já a pede no lugar da dele (IMM em $2AFA).
 *   Pelo título parado ela roda completa, como no original;
 * - na abertura os botões não chegam ao jogo; segurar Start, A, B ou C por HOLD_FRAMES deixa
 *   passar um aperto de Start (mantido até o jogo aceitar) e o próprio jogo pula para o título ($98C0: zera a rolagem
 *   vertical em $FFDEA2, escurece a paleta com $1146 e pede a cena $04). O aviso, com uma
 *   barra que enche, só aparece depois de um aperto. Depois do Start nos logos e do pulo,
 *   os botões só voltam a valer depois de soltos (o Start ainda seguro não abre a seleção
 *   de jogo no título);
 * - no fim ela vai ao título, sem a demonstração: hook_scene_change troca o pedido de cena
 *   $1C por $04 quando o jogo o aceita ($83A) e zera a rolagem vertical, como o pulo faz.
 * O lockstep compara com o jogo original e não passa por aqui. */
#include <string.h>

#include "ops.h"
#include "opening.h"
#include "menu.h"
#include "game/game.h"
#include "intro_logos.h"
#include "bossrush.h"

#define SCENE_SEGA 0x00
#define SCENE_TITLE 0x04
#define SCENE_OPENING 0x14
#define SCENE_DEMO 0x1C
#define HOLD_FRAMES 60        /* 1 s segurando */
#define PROMPT_FRAMES 180     /* o aviso só aparece com um aperto e fica 3 s depois dele */
#define SKIP_BUTTONS (PAD_START | PAD_A | PAD_B | PAD_C)
#define STATE_STORY_FADE 7    /* $FFFACE: fim do terminal, antes da história */
#define STATE_STORY 8
#define MUSIC_STORY 0x0B      /* música da história: a da fase 2 (no original, $22) */

static int from_logo;         /* a abertura veio dos logos: sem o terminal */
static int held, prompt, prev_pad, was_opening;
static int wait_release;      /* ignora os botões até o jogador soltar todos */
static int go_opening;        /* Start nos logos: SEGA → título → abertura em andamento */
static int skip_start;        /* pulo: Start apertado até o jogo aceitar (pode estar carregando) */

void opening_reset(void)
{
    from_logo = held = prompt = prev_pad = was_opening = wait_release = go_opening = skip_start = 0;
}

/* contador de espera da cena atual (SEGA e título): em 1, o jogo segue no próximo quadro */
static void expire_timer(void)
{
    if (work_ram[0xF802] & 0x80) {   /* cena já montada */
        work_ram[0xF804] = 0;
        work_ram[0xF805] = 1;
    }
}

uint32_t opening_filter(uint32_t pad)
{
#ifdef PM_LOCKSTEP
    return pad;
#else
    int scene = work_ram[0xF803] & 0x7C;
    int pressed = pad & ~prev_pad;
    prev_pad = (int)pad;
    int pending = work_ram[0xF800] << 8 | work_ram[0xF801];
    if (go_opening) {
        if (scene == SCENE_SEGA) {
            intro_logos_skip();
            expire_timer();
        } else if (scene == SCENE_TITLE) {
            expire_timer();
        } else {
            go_opening = 0;
        }
    }
    if (skip_start) {
        if (scene == SCENE_OPENING && pending == 0xFFFF)
            return (pad & ~SKIP_BUTTONS) | PAD_START;
        skip_start = 0;      /* o jogo pediu o título: agora espera o jogador soltar */
        wait_release = 1;
    }
    if (wait_release) {
        if (pad & SKIP_BUTTONS)
            return pad & ~SKIP_BUTTONS;
        wait_release = 0;
        pressed = 0;
    }
    if (scene == SCENE_SEGA && pending == 0xFFFF && (pressed & PAD_START) && !go_opening) {
        go_opening = 1;
        intro_logos_skip();
        expire_timer();
        from_logo = 1;
        held = 0;
        wait_release = 1;
        return pad & ~SKIP_BUTTONS;
    }
    int opening = scene == SCENE_OPENING;
    if (opening && from_logo && (work_ram[0xF802] & 0x80) && work_ram[0xFACE] < STATE_STORY_FADE) {
        work_ram[0xFACE] = STATE_STORY_FADE;   /* sem o terminal: direto para a história */
        work_ram[0xFAD0] = 0x01;   /* 1 quadro: o estado 7 repete o efeito $4F a cada quadro */
    }
    was_opening = opening;
    if (!opening) {
        held = 0;
        prompt = 0;
        if (scene != SCENE_SEGA && !go_opening)
            from_logo = 0;
        return pad;
    }
    if (pressed & SKIP_BUTTONS)
        prompt = PROMPT_FRAMES;
    else if (prompt > 0 && !(pad & SKIP_BUTTONS))
        prompt--;
    held = pad & SKIP_BUTTONS ? held + 1 : 0;
    if (held >= HOLD_FRAMES && pending == 0xFFFF) {
        /* um aperto novo de Start, mantido até o jogo reagir: ele faz o próprio pulo (em
         * alguns trechos ele passa quadros carregando sem ler o controle) */
        from_logo = 0;
        held = 0;
        prompt = 0;
        skip_start = 1;
        return (pad & ~SKIP_BUTTONS) | PAD_START;
    }
    return pad & ~SKIP_BUTTONS;
#endif
}

/* IMM em $9B10 (move.w #$22,d7, fim do terminal): música da história da abertura */
int opening_music(void)
{
#ifdef PM_LOCKSTEP
    return 0x22;
#else
    return MUSIC_STORY;
#endif
}

/* IMM em $2AFA (move.w #6,d7 antes de $1D38, na montagem do título): vinda dos logos, o
 * título só passa por baixo a caminho da abertura, então já pede a música dela (senão um
 * pedaço da música do título tocava no começo da abertura) */
int opening_title_music(void)
{
#ifndef PM_LOCKSTEP
    if (go_opening)
        return MUSIC_STORY;
#endif
    return 0x06;
}

/* $83A: o jogo aceitou o pedido de cena (d0) e vai trocar */
void hook_scene_change(void)
{
#ifndef PM_LOCKSTEP
    {
        int s0 = (int)(D(0) & 0xFFFF), s1 = bossrush_scene_change(s0);
        if (s1 != s0)
            D(0) = (D(0) & 0xFFFF0000u) | (uint32_t)s1;
    }
    if ((work_ram[0xF803] & 0x7C) == SCENE_OPENING && (D(0) & 0xFFFF) == SCENE_DEMO) {
        D(0) = (D(0) & 0xFFFF0000u) | SCENE_TITLE;   /* fim da abertura: título */
        bus_write32_raw(0xFFDEA2, 0);                  /* rolagem vertical, como em $98D4 */
        from_logo = 0;
    }
#endif
}

void opening_draw(uint32_t (*fb)[SCREEN_W_MAX], int width, int extra)
{
    int scene = work_ram[0xF803] & 0x7C;
    if ((go_opening && scene == SCENE_TITLE) ||
        (scene == SCENE_OPENING && from_logo && work_ram[0xFACE] < STATE_STORY)) {
        /* o título só passa por baixo, a caminho da abertura; o terminal pulado também */
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < width; x++)
                fb[y][x] = 0xFF000000u;
        return;
    }
    if ((work_ram[0xF803] & 0x7C) != SCENE_OPENING || (!prompt && !held))
        return;
    static const char *text[LANG_COUNT] = {
        "SEGURE PARA PULAR", "HOLD TO SKIP", "MANTÉN PARA SALTAR",
    };
    const char *t = text[settings.language];
    /* discreto: cinza apagado no canto, fundo levemente escurecido e uma barra de 1 px que
     * enche enquanto o botão está seguro (o texto clareia um pouco nesse tempo) */
    int w = menu_text_width(t), x = extra + base_width - 8 - w, y = SCREEN_H - 15;
    for (int yy = y + 1; yy < y + 13 && yy < SCREEN_H; yy++)
        for (int xx = x - 3; xx < x + w + 2 && xx < width; xx++) {
            if (xx < 0)
                continue;
            uint32_t p = fb[yy][xx];
            fb[yy][xx] = 0xFF000000u | ((p >> 1) & 0x7F7F7F);
        }
    menu_draw_text(fb, width, x, y, t, held ? 0xFFB4BECEu : 0xFF7A8294u);
    int by = y + 11, fill = w * (held > HOLD_FRAMES ? HOLD_FRAMES : held) / HOLD_FRAMES;
    if (by < SCREEN_H)
        for (int xx = x; xx < x + w && xx < width; xx++)
            if (xx >= 0)
                fb[by][xx] = xx - x < fill ? 0xFF4FB8E0u : 0xFF2A303Cu;
}
