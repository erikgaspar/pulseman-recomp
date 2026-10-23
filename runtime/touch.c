/* Controles na tela (ver touch.h).
 *
 * Tudo é medido a partir da altura da janela (unidade u = altura / 100), para ficar do mesmo
 * tamanho em qualquer celular deitado: direcional embaixo à esquerda, A/B/C em arco embaixo à
 * direita (na ordem do controle de 3 botões do Mega Drive, B e C mais à mão), o
 * botão de menu no alto à esquerda e Start no alto à direita. Cada dedo fica preso ao controle em que encostou
 * até soltar (no direcional a direção segue o dedo, com 8 direções e zona morta no centro). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "touch.h"
#include "runtime.h"
#include "menu.h"
#include "gamepad.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { C_NONE, C_DPAD, C_A, C_B, C_C, C_START, C_MENU, C_COUNT };

typedef struct { float x, y, r; } Circle;   /* em px da janela */

#define MAX_FINGERS 10
#define MIN_FRAMES 3   /* um toque vale ao menos 3 quadros (toques rápidos não se perdem) */
static struct { SDL_FingerID id; int ctl; float x, y; int used; Uint32 down_at; } fingers[MAX_FINGERS];
static struct { uint32_t bits; Uint32 until; } latch[C_COUNT];
static int win_w, win_h;

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
    return enabled > 0 && !gamepad_count();
}

/* posições dos controles para a janela atual */
static void layout(Circle *c)
{
    float u = win_h / 100.0f, W = (float)win_w, H = (float)win_h;
    c[C_DPAD] = (Circle){22 * u, H - 24 * u, 17 * u};
    c[C_C] = (Circle){W - 12 * u, H - 34 * u, 8.5f * u};
    c[C_B] = (Circle){W - 25 * u, H - 22 * u, 8.5f * u};
    c[C_A] = (Circle){W - 40 * u, H - 14 * u, 8.5f * u};
    c[C_START] = (Circle){W - 8 * u, 8 * u, 5 * u};   /* Start no alto à direita, o menu à esquerda */
    c[C_MENU] = (Circle){8 * u, 8 * u, 5 * u};
}

static int hit(float x, float y)
{
    Circle c[C_COUNT];
    layout(c);
    int best = C_NONE;
    float best_d = 1e9f;
    for (int i = C_DPAD; i < C_COUNT; i++) {
        float dx = x - c[i].x, dy = y - c[i].y, d = sqrtf(dx * dx + dy * dy);
        float reach = c[i].r * (i == C_DPAD ? 1.5f : 1.35f);   /* folga em volta do desenho */
        if (d < reach && d / c[i].r < best_d) {
            best = i;
            best_d = d / c[i].r;
        }
    }
    return best;
}

/* direções do dedo no direcional (8 direções, zona morta no centro) */
static uint32_t dpad_dirs(float x, float y)
{
    Circle c[C_COUNT];
    layout(c);
    float dx = x - c[C_DPAD].x, dy = y - c[C_DPAD].y;
    if (sqrtf(dx * dx + dy * dy) < c[C_DPAD].r * 0.25f)
        return 0;
    float a = atan2f(dy, dx);   /* 0 = direita, cresce para baixo */
    int oct = (int)floorf((a + (float)M_PI / 8) / ((float)M_PI / 4)) & 7;
    static const uint32_t dirs[8] = {
        PAD_RIGHT, PAD_RIGHT | PAD_DOWN, PAD_DOWN, PAD_DOWN | PAD_LEFT,
        PAD_LEFT, PAD_LEFT | PAD_UP, PAD_UP, PAD_UP | PAD_RIGHT,
    };
    return dirs[oct];
}

static uint32_t state_of(int ctl, float x, float y)
{
    switch (ctl) {
    case C_DPAD: return dpad_dirs(x, y);
    case C_A: return PAD_A;
    case C_B: return PAD_B;
    case C_C: return PAD_C;
    case C_START: return PAD_START;
    default: return 0;
    }
}

uint32_t touch_buttons(void)
{
    if (!touch_enabled())
        return 0;
    uint32_t b = 0;
    Uint32 now = SDL_GetTicks();
    for (int i = 0; i < MAX_FINGERS; i++)
        if (fingers[i].used)
            b |= state_of(fingers[i].ctl, fingers[i].x, fingers[i].y);
    for (int c = 0; c < C_COUNT; c++)   /* toques já soltos que ainda não duraram o mínimo */
        if (latch[c].bits && (Sint32)(latch[c].until - now) > 0)
            b |= latch[c].bits;
    return b;
}

/* tecla dos menus para um aperto novo */
static int menu_key_for(uint32_t pressed, SDL_Keycode *key)
{
    if (pressed & PAD_UP) *key = SDLK_UP;
    else if (pressed & PAD_DOWN) *key = SDLK_DOWN;
    else if (pressed & PAD_LEFT) *key = SDLK_LEFT;
    else if (pressed & PAD_RIGHT) *key = SDLK_RIGHT;
    else if (pressed & (PAD_A | PAD_START)) *key = SDLK_RETURN;
    else if (pressed & PAD_B) *key = SDLK_ESCAPE;
    else return 0;
    return 1;
}

int touch_event(const SDL_Event *ev, int menu_open, SDL_Keycode *key)
{
    if (ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        return 0;
    if (ev->type != SDL_FINGERDOWN && ev->type != SDL_FINGERMOTION && ev->type != SDL_FINGERUP)
        return 0;
    if (!touch_enabled() || win_w <= 0)
        return 0;
    /* posição na janela: a original, guardada por raw_watch */
    float x = ev->tfinger.x * win_w, y = ev->tfinger.y * win_h;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (raw[i].seen && raw[i].id == ev->tfinger.fingerId) {   /* o id 0 é o do primeiro dedo */
            x = raw[i].x * win_w;
            y = raw[i].y * win_h;
            break;
        }
    uint32_t before = touch_buttons();
    int slot = -1, menu_tap = 0;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (fingers[i].used && fingers[i].id == ev->tfinger.fingerId)
            slot = i;
    if (ev->type == SDL_FINGERDOWN) {
        int ctl = hit(x, y);
        if (ctl != C_NONE && slot < 0)
            for (int i = 0; i < MAX_FINGERS; i++)
                if (!fingers[i].used) {
                    slot = i;
                    fingers[i].used = 1;
                    fingers[i].id = ev->tfinger.fingerId;
                    fingers[i].ctl = ctl;
                    fingers[i].down_at = SDL_GetTicks();
                    break;
                }
        menu_tap = ctl == C_MENU;
    }
    if (slot >= 0) {
        if (ev->type != SDL_FINGERUP) {   /* ao soltar, o SDL já apagou o dedo: fica a última */
            fingers[slot].x = x;
            fingers[slot].y = y;
        } else {
            Uint32 until = fingers[slot].down_at + MIN_FRAMES * 17;
            int c = fingers[slot].ctl;
            if ((Sint32)(until - SDL_GetTicks()) > 0) {
                latch[c].bits = state_of(c, fingers[slot].x, fingers[slot].y);
                latch[c].until = until;
            }
            fingers[slot].used = 0;
        }
    }
    if (menu_tap) {
        *key = SDLK_ESCAPE;
        return 1;
    }
    if (!menu_open)
        return 0;   /* no jogo os botões chegam por touch_buttons */
    return menu_key_for(touch_buttons() & ~before, key);
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

/* letra da fonte dos menus (5x7) centralizada em (cx, cy), com pixels de s px */
static void glyph(SDL_Renderer *ren, char ch, float cx, float cy, float s, SDL_Color col)
{
    const uint8_t *rows = menu_glyph(ch);
    SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (rows[r] & (0x10 >> c)) {
                SDL_FRect px = {cx + (c - 2.5f) * s, cy + (r - 3.5f) * s, s, s};
                SDL_RenderFillRectF(ren, &px);
            }
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
    SDL_GetRendererOutputSize(ren, &win_w, &win_h);
    if (!touch_enabled())
        return;
    Circle c[C_COUNT];
    layout(c);
    int down[C_COUNT] = {0};
    uint32_t dirs = 0;
    for (int i = 0; i < MAX_FINGERS; i++)
        if (fingers[i].used) {
            down[fingers[i].ctl] = 1;
            if (fingers[i].ctl == C_DPAD)
                dirs |= dpad_dirs(fingers[i].x, fingers[i].y);
        }
    SDL_RenderSetLogicalSize(ren, 0, 0);   /* desenha em px da janela */
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    const SDL_Color base = {10, 14, 30, 90}, edge = {230, 240, 255, 110}, lit = {79, 216, 255, 150};
    const SDL_Color label = {240, 245, 255, 170};
    float u = win_h / 100.0f;

    /* direcional: círculo com as quatro setas; a direção apertada acende */
    fill_circle(ren, c[C_DPAD].x, c[C_DPAD].y, c[C_DPAD].r, base);
    ring(ren, c[C_DPAD].x, c[C_DPAD].y, c[C_DPAD].r, 0.6f * u, edge);
    static const struct { uint32_t bit; float ang; } arrows[4] = {
        {PAD_RIGHT, 0}, {PAD_DOWN, (float)M_PI / 2}, {PAD_LEFT, (float)M_PI}, {PAD_UP, -(float)M_PI / 2},
    };
    for (int i = 0; i < 4; i++) {
        float a = arrows[i].ang, d = c[C_DPAD].r * 0.62f;
        triangle(ren, c[C_DPAD].x + cosf(a) * d, c[C_DPAD].y + sinf(a) * d, 4.2f * u, a,
                 dirs & arrows[i].bit ? lit : label);
    }

    static const struct { int ctl; char ch; } buttons[] = {
        {C_A, 'A'}, {C_B, 'B'}, {C_C, 'C'},
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        const Circle *b = &c[buttons[i].ctl];
        fill_circle(ren, b->x, b->y, b->r, down[buttons[i].ctl] ? lit : base);
        ring(ren, b->x, b->y, b->r, 0.6f * u, edge);
        glyph(ren, buttons[i].ch, b->x, b->y, 1.3f * u, label);
    }

    /* Start: pílula pequena; menu: três traços */
    const Circle *s = &c[C_START];
    fill_circle(ren, s->x, s->y, s->r, down[C_START] ? lit : base);
    ring(ren, s->x, s->y, s->r, 0.5f * u, edge);
    triangle(ren, s->x + 0.4f * u, s->y, 2.2f * u, 0, label);
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
