/* Controles na tela (ver touch.h).
 *
 * Nas fases (cena $08) aparecem os controles: tudo é medido a partir da altura da janela
 * (unidade u = altura / 100), para ficar do mesmo tamanho em qualquer celular deitado. O
 * direcional fica embaixo à esquerda; os botões ficam embaixo à direita, em volta de onde o
 * polegar descansa, um por ação e com o ícone dela: pulo maior e mais à mão, ataque à esquerda
 * dele e voltteccer acima. Cada um aperta o botão do Mega Drive que a configuração CONTROLE das
 * opções do jogo dá à ação ($FFFD2F). O botão de menu fica no alto à esquerda e Start (pausa) no
 * alto à direita. Cada dedo fica preso ao controle em que encostou até soltar: no direcional a
 * direção segue o dedo (8 direções, as diagonais mais estreitas, zona morta no centro); nos
 * botões vale o que está embaixo do dedo, e entre dois botões os dois (o polegar rola de um
 * para o outro).
 *
 * Fora das fases os controles somem e a tela é tocada direto:
 * - no nosso menu (menu.c) um toque solto escolhe o que está embaixo dele (menu_tap);
 * - no título, nas opções originais ($0C) e na seleção de fases ($20) o toque vira os botões
 *   que o jogo espera (cursor até o item tocado e confirmação), em apertos de 3 quadros;
 * - nas outras telas (cenas, abertura, continue...) o dedo na tela é o Start seguro (um
 *   toque passa a cena; segurar pula a abertura). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "touch.h"
#include "runtime.h"
#include "menu.h"
#include "gamepad.h"
#include "bossrush.h"
#include "png.h"
#include "touch_icons.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SCENE_TITLE 0x04
#define SCENE_STAGE 0x08
#define SCENE_OPTIONS 0x0C
#define SCENE_SELECT 0x20

/* controles desenhados e o que um dedo segura (C_HOLD: Start invisível, fora das fases;
 * C_TAP: toque no nosso menu, vale ao soltar) */
enum { C_NONE, C_DPAD, C_JUMP, C_ATTACK, C_VOLT, C_START, C_MENU, C_FACE, C_HOLD, C_TAP, C_COUNT };

typedef struct { float x, y, r; } Circle;   /* em px da janela */

#define MAX_FINGERS 10
#define MIN_FRAMES 3   /* um toque vale ao menos 3 quadros (toques rápidos não se perdem) */
static struct { SDL_FingerID id; int ctl; float x, y, x0, y0; int used; Uint32 down_at; } fingers[MAX_FINGERS];
static struct { uint32_t bits; Uint32 until; } latch[C_COUNT];
static int win_w, win_h;
static SDL_Renderer *renderer;

/* Posição original de cada dedo (0..1 da janela). O renderer, com tamanho lógico, troca nos
 * eventos de toque a posição pela relativa à área da imagem do jogo e corta o que cai fora
 * dela (as faixas laterais, onde ficam parte dos controles). Os observadores de eventos do SDL
 * rodam na ordem em que foram instalados: este é instalado antes de criar o renderer e vê a
 * posição original. */
static struct { SDL_FingerID id; float x, y; int used, seen; } raw[MAX_FINGERS];   /* seen: já teve um dedo */

static int SDLCALL raw_watch(void *userdata, SDL_Event *ev)
{
    (void)userdata;
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP)
        return 1;
    int slot = -1, free_slot = -1;
    for (int i = 0; i < MAX_FINGERS; i++) {
        if (raw[i].seen && raw[i].id == ev->tfinger.fingerId && slot < 0)
            slot = i;
        else if (!raw[i].used && free_slot < 0)
            free_slot = i;
    }
    if (slot < 0)
        slot = free_slot;
    if (slot >= 0) {   /* fica guardada mesmo depois de soltar, até o dedo ser reaproveitado */
        raw[slot].used = ev->type != SDL_FINGERUP;
        raw[slot].seen = 1;
        raw[slot].id = ev->tfinger.fingerId;
        raw[slot].x = ev->tfinger.x;
        raw[slot].y = ev->tfinger.y;
    }
    return 1;
}

static int enabled = -1;

void touch_init(void)
{
#ifdef __ANDROID__
    enabled = 1;
#else
    enabled = getenv("PM_TOUCH") != NULL;
    if (enabled)
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");   /* o mouse faz as vezes do dedo */
#endif
    if (enabled)
        SDL_AddEventWatch(raw_watch, NULL);   /* antes do renderer (ver raw_watch) */
}

int touch_enabled(void)
{
    return enabled > 0;
}

static int scene_now(void)
{
    return work_ram[0xF803] & 0x7C;
}

/* controles na tela: só nas fases, sem o nosso menu aberto e sem controle conectado */
int touch_controls_shown(void)
{
    return enabled > 0 && !gamepad_count() && !menu_is_open() && scene_now() == SCENE_STAGE;
}

/* ---------- apertos em fila (toques nas telas do jogo) ---------- */

#define QUEUE_MAX 32
static struct { uint32_t bits; int frames; } queue[QUEUE_MAX];
static int q_head, q_len;

static void push(uint32_t bits, int frames)
{
    if (q_len < QUEUE_MAX) {
        queue[(q_head + q_len) % QUEUE_MAX].bits = bits;
        queue[(q_head + q_len) % QUEUE_MAX].frames = frames;
        q_len++;
    }
}

/* um aperto: 3 quadros seguro e 3 solto (o jogo só reage a apertos novos) */
static void press(uint32_t bits)
{
    push(bits, 3);
    push(0, 3);
}

/* ---------- posições ---------- */

/* posições dos controles para a janela atual */
static void layout(Circle *c)
{
    float u = win_h / 100.0f, W = (float)win_w, H = (float)win_h;
    if (W < H * 1.6f)   /* tela pouco larga (tablet): os controles não se encontram no meio */
        u = W / 160.0f;
    c[C_DPAD] = (Circle){25 * u, H - 28 * u, 19 * u};
    c[C_JUMP] = (Circle){W - 20 * u, H - 24 * u, 11.5f * u};
    c[C_ATTACK] = (Circle){W - 44 * u, H - 17 * u, 10 * u};
    c[C_VOLT] = (Circle){W - 28 * u, H - 48 * u, 9.5f * u};
    c[C_START] = (Circle){W - 9 * u, 9 * u, 5.5f * u};   /* Start no alto à direita, o menu à esquerda */
    c[C_MENU] = (Circle){9 * u, 9 * u, 5.5f * u};
}

static float dist(const Circle *c, float x, float y)
{
    float dx = x - c->x, dy = y - c->y;
    return sqrtf(dx * dx + dy * dy);
}

/* botão do Mega Drive de cada ação na configuração CONTROLE ($FFFD2F = 0, 8, 16 ou 24):
 * A VOLTTECCER/B ATAQUE/C PULO, A VOLTTECCER/B PULO/C ATAQUE, A ATAQUE/B PULO/C VOLTTECCER e
 * A PULO/B ATAQUE/C VOLTTECCER */
static uint32_t action_bit(int ctl)
{
    static const uint32_t jump[4] = {PAD_C, PAD_B, PAD_B, PAD_A};
    static const uint32_t attack[4] = {PAD_B, PAD_C, PAD_A, PAD_B};
    static const uint32_t volt[4] = {PAD_A, PAD_A, PAD_C, PAD_C};
    int k = (work_ram[0xFD2F] >> 3) & 3;
    return ctl == C_JUMP ? jump[k] : ctl == C_ATTACK ? attack[k] : volt[k];
}

/* botões de ação embaixo do dedo: os que ele cobre (entre dois, os dois) ou, perto do grupo,
 * o mais próximo */
static uint32_t face_bits(float x, float y)
{
    Circle c[C_COUNT];
    layout(c);
    uint32_t b = 0, nearest = 0;
    float best = 1e9f;
    for (int ctl = C_JUMP; ctl <= C_VOLT; ctl++) {
        float d = dist(&c[ctl], x, y) / c[ctl].r;
        if (d < 1.15f)
            b |= action_bit(ctl);
        if (d < best) {
            best = d;
            nearest = action_bit(ctl);
        }
    }
    return b ? b : best < 1.6f ? nearest : 0;
}

static int hit(float x, float y)
{
    Circle c[C_COUNT];
    layout(c);
    if (face_bits(x, y))
        return C_FACE;
    if (dist(&c[C_DPAD], x, y) < c[C_DPAD].r * 1.6f)
        return C_DPAD;
    for (int i = C_START; i <= C_MENU; i++)
        if (dist(&c[i], x, y) < c[i].r * 1.6f)
            return i;
    return C_NONE;
}

/* direções do dedo no direcional: as horizontais valem até 60 graus da horizontal e as
 * verticais a partir de 30 (diagonais de 30 graus, mais estreitas que as retas: andar sem
 * abaixar sem querer), com zona morta no centro */
static uint32_t dpad_dirs(float x, float y)
{
    Circle c[C_COUNT];
    layout(c);
    float dx = x - c[C_DPAD].x, dy = y - c[C_DPAD].y;
    if (sqrtf(dx * dx + dy * dy) < c[C_DPAD].r * 0.25f)
        return 0;
    uint32_t b = 0;
    if (fabsf(dy) < 1.732f * fabsf(dx))     /* tan 60 */
        b |= dx > 0 ? PAD_RIGHT : PAD_LEFT;
    if (fabsf(dy) > 0.577f * fabsf(dx))     /* tan 30 */
        b |= dy > 0 ? PAD_DOWN : PAD_UP;
    return b;
}

static uint32_t state_of(int ctl, float x, float y)
{
    switch (ctl) {
    case C_DPAD: return dpad_dirs(x, y);
    case C_FACE: return face_bits(x, y);
    case C_START:
    case C_HOLD: return PAD_START;
    default: return 0;
    }
}

/* botões seguros pelos dedos agora (e os toques curtos que ainda não duraram o mínimo) */
static uint32_t held_bits(void)
{
    uint32_t b = 0;
    Uint32 now = SDL_GetTicks();
    for (int i = 0; i < MAX_FINGERS; i++)
        if (fingers[i].used)
            b |= state_of(fingers[i].ctl, fingers[i].x, fingers[i].y);
    for (int c = 0; c < C_COUNT; c++)
        if (latch[c].bits && (Sint32)(latch[c].until - now) > 0)
            b |= latch[c].bits;
    return b;
}

uint32_t touch_buttons(void)
{
    if (enabled <= 0)
        return 0;
    uint32_t b = held_bits();
    if (q_len) {   /* um quadro da fila */
        b |= queue[q_head].bits;
        if (--queue[q_head].frames <= 0) {
            q_head = (q_head + 1) % QUEUE_MAX;
            q_len--;
        }
    }
    return b;
}

/* px da janela → px da imagem do jogo */
static void to_logical(float wx, float wy, int *lx, int *ly)
{
    float fx = wx, fy = wy;
    if (renderer)
        SDL_RenderWindowToLogical(renderer, (int)wx, (int)wy, &fx, &fy);
    *lx = (int)fx;
    *ly = (int)fy;
}

/* ---------- telas do jogo ---------- */

/* Toque numa tela do jogo fora das fases. Devolve o controle que o dedo passa a segurar. */
static int screen_down(float wx, float wy)
{
    int x, y, cx = screen_width / 2, s = scene_now();
    to_logical(wx, wy, &x, &y);

    int item = bossrush_title_item();   /* título com os itens: START, OPTIONS, BOSS RUSH */
    if (s == SCENE_TITLE && item >= 0) {
        /* itens nas linhas 17, 19 e 21 do plano (y = 136, 152, 168), em volta do meio */
        for (int k = 0; k < 3; k++)
            if (!q_len && y >= 130 + 16 * k && y < 146 + 16 * k && abs(x - cx) < 70) {
                for (int n = (k - item + 3) % 3; n > 0; n--)   /* baixo: START → OPTIONS → BOSS RUSH */
                    press(PAD_DOWN);
                press(PAD_START);
            }
        return C_NONE;
    }
    if (s == SCENE_OPTIONS) {
        /* opções originais: CONTROLE (três linhas), MÚSICA, EFEITOS, VOZ e SAIR; os nomes à
         * esquerda do meio e os valores à direita. Cursor em $FFFD30. */
        static const int top[6] = {76, 110, 128, 144, 160, 180};
        int cur = work_ram[0xFD30];
        if (q_len || cur > 4 || x < cx - 95 || x > cx + 100)
            return C_NONE;
        for (int r = 0; r < 5; r++) {
            if (y < top[r] || y >= top[r + 1])
                continue;
            if (r != cur) {   /* primeiro leva o cursor até a linha */
                for (int d = r - cur; d > 0; d--)
                    press(PAD_DOWN);
                for (int d = r - cur; d < 0; d++)
                    press(PAD_UP);
            } else if (r == 4) {
                press(PAD_START);   /* SAIR */
            } else if (r == 0 || x >= cx + 45) {
                press(PAD_RIGHT);   /* próximo valor */
            } else if (x >= cx - 12) {
                press(PAD_LEFT);    /* começo do valor: o anterior */
            } else {
                press(PAD_A);       /* nome: toca o som escolhido */
            }
        }
        return C_NONE;
    }
    if (s == SCENE_SELECT) {
        /* seleção de fases: as setas dos lados do globo trocam a fase; o meio confirma */
        if (!q_len)
            press(x < cx - 45 ? PAD_LEFT : x > cx + 45 ? PAD_RIGHT : PAD_START);
        return C_NONE;
    }
    return C_HOLD;
}

/* ---------- eventos ---------- */

int touch_event(const SDL_Event *ev, SDL_Keycode *key, int *tap_x, int *tap_y)
{
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP)
        return TOUCH_NONE;
    if (enabled <= 0 || win_w <= 0)
        return TOUCH_NONE;
    /* posição na janela: a original, guardada por raw_watch */
    float x = ev->tfinger.x * win_w, y = ev->tfinger.y * win_h;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (raw[i].seen && raw[i].id == ev->tfinger.fingerId) {   /* o id 0 é o do primeiro dedo */
            x = raw[i].x * win_w;
            y = raw[i].y * win_h;
            break;
        }
    int slot = -1;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (fingers[i].used && fingers[i].id == ev->tfinger.fingerId)
            slot = i;
    if (ev->type == SDL_FINGERDOWN && slot < 0) {
        int ctl = menu_is_open() ? C_TAP : touch_controls_shown() ? hit(x, y) :
                  gamepad_count() && scene_now() == SCENE_STAGE ? C_NONE : screen_down(x, y);
        if (ctl == C_NONE)
            return TOUCH_NONE;
        for (int i = 0; i < MAX_FINGERS; i++)
            if (!fingers[i].used) {
                slot = i;
                fingers[i].used = 1;
                fingers[i].id = ev->tfinger.fingerId;
                fingers[i].ctl = ctl;
                fingers[i].x0 = x;
                fingers[i].y0 = y;
                fingers[i].down_at = SDL_GetTicks();
                break;
            }
        if (slot >= 0 && ctl == C_MENU) {
            *key = SDLK_ESCAPE;
            return TOUCH_KEY;
        }
    }
    if (slot < 0)
        return TOUCH_NONE;
    if (ev->type != SDL_FINGERUP) {   /* ao soltar, o SDL já apagou o dedo: fica a última */
        fingers[slot].x = x;
        fingers[slot].y = y;
        return TOUCH_NONE;
    }
    fingers[slot].used = 0;
    int c = fingers[slot].ctl;
    if (c == C_TAP) {
        /* toque no menu: vale onde encostou, se o dedo não andou */
        float dx = x - fingers[slot].x0, dy = y - fingers[slot].y0;
        if (sqrtf(dx * dx + dy * dy) > win_h * 0.05f || !menu_is_open())
            return TOUCH_NONE;
        to_logical(fingers[slot].x0, fingers[slot].y0, tap_x, tap_y);
        return TOUCH_TAP;
    }
    Uint32 until = fingers[slot].down_at + MIN_FRAMES * 17;
    if ((Sint32)(until - SDL_GetTicks()) > 0) {
        latch[c].bits = state_of(c, fingers[slot].x, fingers[slot].y);
        latch[c].until = until;
    }
    return TOUCH_NONE;
}

/* ---------- desenho ---------- */

static void fill_circle(SDL_Renderer *ren, float cx, float cy, float r, SDL_Color col)
{
    enum { N = 40 };
    SDL_Vertex v[N + 2];
    int idx[N * 3];
    v[0].position = (SDL_FPoint){cx, cy};
    v[0].color = col;
    for (int i = 0; i <= N; i++) {
        float a = (float)i / N * 2 * (float)M_PI;
        v[i + 1].position = (SDL_FPoint){cx + cosf(a) * r, cy + sinf(a) * r};
        v[i + 1].color = col;
    }
    for (int i = 0; i < N; i++) {
        idx[i * 3] = 0;
        idx[i * 3 + 1] = i + 1;
        idx[i * 3 + 2] = i + 2;
    }
    SDL_RenderGeometry(ren, NULL, v, N + 2, idx, N * 3);
}

static void ring(SDL_Renderer *ren, float cx, float cy, float r, float w, SDL_Color col)
{
    enum { N = 48 };
    SDL_Vertex v[(N + 1) * 2];
    int idx[N * 6];
    for (int i = 0; i <= N; i++) {
        float a = (float)i / N * 2 * (float)M_PI, c = cosf(a), s = sinf(a);
        v[i * 2].position = (SDL_FPoint){cx + c * r, cy + s * r};
        v[i * 2 + 1].position = (SDL_FPoint){cx + c * (r - w), cy + s * (r - w)};
        v[i * 2].color = v[i * 2 + 1].color = col;
    }
    for (int i = 0; i < N; i++) {
        int b = i * 2;
        int t[6] = {b, b + 1, b + 2, b + 1, b + 3, b + 2};
        memcpy(&idx[i * 6], t, sizeof(t));
    }
    SDL_RenderGeometry(ren, NULL, v, (N + 1) * 2, idx, N * 6);
}

/* ícones dos botões de ação (touch_icons.c), em texturas criadas no primeiro desenho */
static SDL_Texture *icon_texture(SDL_Renderer *ren, int ctl)
{
    static SDL_Texture *tex[C_COUNT];
    static int tried[C_COUNT];
    if (!tex[ctl] && !tried[ctl]) {
        tried[ctl] = 1;
        const unsigned char *png = ctl == C_JUMP ? touch_icon_jump : ctl == C_ATTACK ? touch_icon_attack : touch_icon_volt;
        unsigned size = ctl == C_JUMP ? touch_icon_jump_size : ctl == C_ATTACK ? touch_icon_attack_size : touch_icon_volt_size;
        int w, h;
        uint32_t *px = png_decode(png, size, &w, &h);
        if (px) {
            tex[ctl] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            if (tex[ctl]) {
                SDL_UpdateTexture(tex[ctl], NULL, px, w * 4);
                SDL_SetTextureBlendMode(tex[ctl], SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(tex[ctl], SDL_ScaleModeLinear);
            }
            free(px);
        }
    }
    return tex[ctl];
}

static void triangle(SDL_Renderer *ren, float cx, float cy, float size, float ang, SDL_Color col)
{
    SDL_Vertex v[3];
    for (int i = 0; i < 3; i++) {
        float a = ang + i * 2 * (float)M_PI / 3;
        v[i].position = (SDL_FPoint){cx + cosf(a) * size, cy + sinf(a) * size};
        v[i].color = col;
    }
    SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0);
}

void touch_draw(SDL_Renderer *ren)
{
    renderer = ren;
    SDL_GetRendererOutputSize(ren, &win_w, &win_h);
    if (!touch_controls_shown())
        return;
    Circle c[C_COUNT];
    layout(c);
    uint32_t held = 0;
    int start_down = 0;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (fingers[i].used) {
            held |= state_of(fingers[i].ctl, fingers[i].x, fingers[i].y);
            start_down |= fingers[i].ctl == C_START;
        }
    SDL_RenderSetLogicalSize(ren, 0, 0);   /* desenha em px da janela */
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    const SDL_Color base = {10, 14, 30, 80}, edge = {230, 240, 255, 100}, lit = {79, 216, 255, 150};
    const SDL_Color label = {240, 245, 255, 160};
    float u = c[C_START].r / 5.5f;

    /* direcional: círculo com as quatro setas; a direção apertada acende */
    fill_circle(ren, c[C_DPAD].x, c[C_DPAD].y, c[C_DPAD].r, base);
    ring(ren, c[C_DPAD].x, c[C_DPAD].y, c[C_DPAD].r, 0.6f * u, edge);
    fill_circle(ren, c[C_DPAD].x, c[C_DPAD].y, c[C_DPAD].r * 0.25f, base);   /* zona morta */
    static const struct { uint32_t bit; float ang; } arrows[4] = {
        {PAD_RIGHT, 0}, {PAD_DOWN, (float)M_PI / 2}, {PAD_LEFT, (float)M_PI}, {PAD_UP, -(float)M_PI / 2},
    };
    for (int i = 0; i < 4; i++) {
        float a = arrows[i].ang, d = c[C_DPAD].r * 0.64f;
        triangle(ren, c[C_DPAD].x + cosf(a) * d, c[C_DPAD].y + sinf(a) * d, 4.4f * u, a,
                 held & arrows[i].bit ? lit : label);
    }

    for (int ctl = C_JUMP; ctl <= C_VOLT; ctl++) {
        const Circle *b = &c[ctl];
        fill_circle(ren, b->x, b->y, b->r, held & action_bit(ctl) ? lit : base);
        ring(ren, b->x, b->y, b->r, 0.6f * u, edge);
        SDL_Texture *t = icon_texture(ren, ctl);
        if (t) {
            float sz = b->r * 1.2f;
            SDL_FRect dst = {b->x - sz / 2, b->y - sz / 2, sz, sz};
            SDL_SetTextureAlphaMod(t, label.a);
            SDL_RenderCopyF(ren, t, NULL, &dst);
        }
    }

    /* Start (pausa): dois traços; menu: três traços */
    const Circle *s = &c[C_START];
    fill_circle(ren, s->x, s->y, s->r, start_down ? lit : base);
    ring(ren, s->x, s->y, s->r, 0.5f * u, edge);
    SDL_SetRenderDrawColor(ren, label.r, label.g, label.b, label.a);
    for (int i = -1; i <= 1; i += 2) {
        SDL_FRect bar = {s->x + i * 1.1f * u - 0.45f * u, s->y - 2.2f * u, 0.9f * u, 4.4f * u};
        SDL_RenderFillRectF(ren, &bar);
    }
    const Circle *m = &c[C_MENU];
    fill_circle(ren, m->x, m->y, m->r, base);
    ring(ren, m->x, m->y, m->r, 0.5f * u, edge);
    SDL_SetRenderDrawColor(ren, label.r, label.g, label.b, label.a);
    for (int i = -1; i <= 1; i++) {
        SDL_FRect bar = {m->x - 2.4f * u, m->y + i * 1.6f * u - 0.4f * u, 4.8f * u, 0.8f * u};
        SDL_RenderFillRectF(ren, &bar);
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_RenderSetLogicalSize(ren, screen_width, SCREEN_H);
}
