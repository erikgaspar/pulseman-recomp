/* Menu de pausa: fonte própria 5x7, painel desenhado sobre o quadro do jogo. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "menu.h"
#include "filters.h"
#include "savestate.h"
#include "touch.h"

/* Widescreen é o padrão; o modo original (320 px) fica no menu. O lockstep compara com o
 * jogo original, então lá o padrão é o modo original. */
#ifdef PM_LOCKSTEP
Settings settings = {0, 1, 10, 1, 0, 1, LANG_PT, 0, 0};
#else
Settings settings = {1, 1, 10, 1, 0, 1, LANG_PT, 0, 0};
#endif

/* ---------- configurações ---------- */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* O caminho vem do SDL_GetBasePath em UTF-8 e a pasta pode ter acentos ("decompilação"):
 * o fopen do Windows usaria a página de código ANSI e falharia. SDL_RWFromFile aceita UTF-8. */
int settings_load(const char *path)
{
    SDL_RWops *f = SDL_RWFromFile(path, "rb");
    if (!f)
        return 0;
    char text[4096];
    size_t n = SDL_RWread(f, text, 1, sizeof(text) - 1);
    SDL_RWclose(f);
    text[n] = 0;
    char key[64];
    int v;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        if (sscanf(line, " %63[^= ] = %d", key, &v) != 2)
            continue;
        if (!strcmp(key, "modo_original")) settings.widescreen = !v;
        else if (!strcmp(key, "sem_lentidao")) settings.no_slowdown = !!v;
        else if (!strcmp(key, "volume")) settings.volume = clampi(v, 0, 10);
        else if (!strcmp(key, "tela")) settings.display = clampi(v, 0, 3);
        else if (!strcmp(key, "filtro_suave")) settings.filter = v ? FILTER_SMOOTH : FILTER_SHARP; /* antigo */
        else if (!strcmp(key, "filtro")) settings.filter = clampi(v, 0, FILTER_COUNT - 1);
        else if (!strcmp(key, "sprites_sem_limite")) settings.no_sprite_limit = !!v;
        else if (!strcmp(key, "idioma")) settings.language = clampi(v, 0, LANG_COUNT - 1);
        else if (!strcmp(key, "moldura_preta")) settings.black_border = !!v;
        else if (!strcmp(key, "boss_rush_recorde")) settings.boss_best = v > 0 ? v : 0;
    }
    return 1;
}

int settings_save(const char *path)
{
    char text[1024];
    int n = snprintf(text, sizeof(text),
        "# Configurações do Pulseman (alteradas pelo menu: Esc, ou OPTIONS no título)\n"
        "# 0 = widescreen (424 px), 1 = original (320 px)\n"
        "modo_original = %d\n"
        "sem_lentidao = %d\n"
        "volume = %d\n"
        "# 0 = janela 2x, 1 = janela 3x, 2 = janela 4x, 3 = tela cheia\n"
        "tela = %d\n"
        "# 0 = nítido, 1 = suave, 2 = pixel suave, 3 = scanlines, 4 = TV, 5 = TV curva\n"
        "filtro = %d\n"
        "# 1 = sem o limite de 20 sprites por linha (menos sprites piscando/sumindo)\n"
        "sprites_sem_limite = %d\n"
        "# idioma dos menus: 0 = português, 1 = english, 2 = español\n"
        "idioma = %d\n"
        "# telas em 4:3 no widescreen: 0 = moldura desfocada, 1 = faixas pretas\n"
        "moldura_preta = %d\n"
        "# melhor tempo do Boss Rush, em quadros (60 por segundo; 0 = nenhum)\n"
        "boss_rush_recorde = %d\n",
        !settings.widescreen, settings.no_slowdown, settings.volume, settings.display,
        settings.filter, settings.no_sprite_limit, settings.language, settings.black_border,
        settings.boss_best);
    SDL_RWops *f = SDL_RWFromFile(path, "wb");
    if (!f)
        return 0;
    int ok = SDL_RWwrite(f, text, 1, n) == (size_t)n;
    return SDL_RWclose(f) == 0 && ok;
}

/* ---------- fonte 5x7 (desenhada para este projeto) ---------- */

typedef struct { char c; uint8_t rows[7]; } Glyph;

/* Cada linha é escrita como 5 dígitos 0/1; o 1 na frente evita que vire octal */
#define B5(d) ((((d) / 10000) % 10) << 4 | (((d) / 1000) % 10) << 3 | (((d) / 100) % 10) << 2 |                (((d) / 10) % 10) << 1 | ((d) % 10))
#define R(a, b, c, d, e, f, g) {B5(1##a), B5(1##b), B5(1##c), B5(1##d), B5(1##e), B5(1##f), B5(1##g)}
static const Glyph font[] = {
    {'A', R(01110, 10001, 10001, 11111, 10001, 10001, 10001)},
    {'B', R(11110, 10001, 10001, 11110, 10001, 10001, 11110)},
    {'C', R(01110, 10001, 10000, 10000, 10000, 10001, 01110)},
    {'D', R(11110, 10001, 10001, 10001, 10001, 10001, 11110)},
    {'E', R(11111, 10000, 10000, 11110, 10000, 10000, 11111)},
    {'F', R(11111, 10000, 10000, 11110, 10000, 10000, 10000)},
    {'G', R(01110, 10001, 10000, 10111, 10001, 10001, 01111)},
    {'H', R(10001, 10001, 10001, 11111, 10001, 10001, 10001)},
    {'I', R(01110, 00100, 00100, 00100, 00100, 00100, 01110)},
    {'J', R(00111, 00010, 00010, 00010, 00010, 10010, 01100)},
    {'K', R(10001, 10010, 10100, 11000, 10100, 10010, 10001)},
    {'L', R(10000, 10000, 10000, 10000, 10000, 10000, 11111)},
    {'M', R(10001, 11011, 10101, 10101, 10001, 10001, 10001)},
    {'N', R(10001, 10001, 11001, 10101, 10011, 10001, 10001)},
    {'O', R(01110, 10001, 10001, 10001, 10001, 10001, 01110)},
    {'P', R(11110, 10001, 10001, 11110, 10000, 10000, 10000)},
    {'Q', R(01110, 10001, 10001, 10001, 10101, 10010, 01101)},
    {'R', R(11110, 10001, 10001, 11110, 10100, 10010, 10001)},
    {'S', R(01111, 10000, 10000, 01110, 00001, 00001, 11110)},
    {'T', R(11111, 00100, 00100, 00100, 00100, 00100, 00100)},
    {'U', R(10001, 10001, 10001, 10001, 10001, 10001, 01110)},
    {'V', R(10001, 10001, 10001, 10001, 10001, 01010, 00100)},
    {'W', R(10001, 10001, 10001, 10101, 10101, 10101, 01010)},
    {'X', R(10001, 10001, 01010, 00100, 01010, 10001, 10001)},
    {'Y', R(10001, 10001, 01010, 00100, 00100, 00100, 00100)},
    {'Z', R(11111, 00001, 00010, 00100, 01000, 10000, 11111)},
    {'0', R(01110, 10001, 10011, 10101, 11001, 10001, 01110)},
    {'1', R(00100, 01100, 00100, 00100, 00100, 00100, 01110)},
    {'2', R(01110, 10001, 00001, 00010, 00100, 01000, 11111)},
    {'3', R(11110, 00001, 00001, 01110, 00001, 00001, 11110)},
    {'4', R(00010, 00110, 01010, 10010, 11111, 00010, 00010)},
    {'5', R(11111, 10000, 11110, 00001, 00001, 10001, 01110)},
    {'6', R(00110, 01000, 10000, 11110, 10001, 10001, 01110)},
    {'7', R(11111, 00001, 00010, 00100, 01000, 01000, 01000)},
    {'8', R(01110, 10001, 10001, 01110, 10001, 10001, 01110)},
    {'9', R(01110, 10001, 10001, 01111, 00001, 00010, 01100)},
    {':', R(00000, 00100, 00100, 00000, 00100, 00100, 00000)},
    {'%', R(11001, 11010, 00010, 00100, 01000, 01011, 10011)},
    {'<', R(00010, 00100, 01000, 10000, 01000, 00100, 00010)},
    {'>', R(01000, 00100, 00010, 00001, 00010, 00100, 01000)},
    {'-', R(00000, 00000, 00000, 11111, 00000, 00000, 00000)},
    {'.', R(00000, 00000, 00000, 00000, 00000, 01100, 01100)},
    {'?', R(01110, 10001, 00001, 00010, 00100, 00000, 00100)},
    {'!', R(00100, 00100, 00100, 00100, 00100, 00000, 00100)},
    {'/', R(00001, 00010, 00010, 00100, 01000, 01000, 10000)},
    {',', R(00000, 00000, 00000, 00000, 01100, 00100, 01000)},
    {'(', R(00010, 00100, 01000, 01000, 01000, 00100, 00010)},
    {')', R(01000, 00100, 00010, 00010, 00010, 00100, 01000)},
    {'+', R(00000, 00100, 00100, 11111, 00100, 00100, 00000)},
    {'\'', R(00100, 00100, 01000, 00000, 00000, 00000, 00000)},
    {'"', R(01010, 01010, 01010, 00000, 00000, 00000, 00000)},
    {'&', R(01100, 10010, 10100, 01000, 10101, 10010, 01101)},
    /* setas para os botões do rodapé: \x01 cima, \x02 baixo, \x03 esquerda, \x04 direita */
    {'\x01', R(00000, 00100, 01110, 11111, 00100, 00100, 00000)},
    {'\x02', R(00000, 00100, 00100, 11111, 01110, 00100, 00000)},
    {'\x03', R(00000, 00100, 01100, 11111, 01100, 00100, 00000)},
    {'\x04', R(00000, 00100, 00110, 11111, 00110, 00100, 00000)},
    {'\x05', R(00100, 00000, 00100, 01000, 10000, 10001, 01110)},   /* ¿ */
    {'\x06', R(00100, 00000, 00100, 00100, 00100, 00100, 00100)},   /* ¡ */
};
#undef R

/* Acentos: duas linhas acima da letra; cedilha: duas abaixo */
enum { ACC_NONE, ACC_TILDE, ACC_ACUTE, ACC_CIRC, ACC_CEDILLA };
static const uint8_t accent_rows[][2] = {
    {0, 0}, {0x0D, 0x16}, {0x02, 0x04}, {0x04, 0x0A}, {0x04, 0x08},
};

#define CELL_W 6
#define CELL_H 12   /* 2 de acento + 7 da letra + 2 de cedilha + 1 de espaço */

static const uint8_t *glyph_rows(char c)
{
    static const uint8_t blank[7] = {0};
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++)
        if (font[i].c == c)
            return font[i].rows;
    return blank;
}

/* Lê um caractere UTF-8 (maiúsculas acentuadas do português e do espanhol, ¿ e ¡) */
static const char *decode(const char *s, char *base, int *accent)
{
    unsigned char c = (unsigned char)*s;
    *accent = ACC_NONE;
    if (c == 0xC3 && s[1]) {
        switch ((unsigned char)s[1]) {
        case 0x81: *base = 'A'; *accent = ACC_ACUTE; break;
        case 0x82: *base = 'A'; *accent = ACC_CIRC; break;
        case 0x83: *base = 'A'; *accent = ACC_TILDE; break;
        case 0x87: *base = 'C'; *accent = ACC_CEDILLA; break;
        case 0x89: *base = 'E'; *accent = ACC_ACUTE; break;
        case 0x8A: *base = 'E'; *accent = ACC_CIRC; break;
        case 0x8D: *base = 'I'; *accent = ACC_ACUTE; break;
        case 0x91: *base = 'N'; *accent = ACC_TILDE; break;
        case 0x93: *base = 'O'; *accent = ACC_ACUTE; break;
        case 0x94: *base = 'O'; *accent = ACC_CIRC; break;
        case 0x95: *base = 'O'; *accent = ACC_TILDE; break;
        case 0x9A: *base = 'U'; *accent = ACC_ACUTE; break;
        default: *base = '?'; break;
        }
        return s + 2;
    }
    if (c == 0xC2 && ((unsigned char)s[1] == 0xBF || (unsigned char)s[1] == 0xA1)) {
        *base = (unsigned char)s[1] == 0xBF ? '\x05' : '\x06';
        return s + 2;
    }
    *base = (char)c;
    return s + 1;
}

static int text_width(const char *s)
{
    int n = 0;
    char b;
    int a;
    while (*s) {
        s = decode(s, &b, &a);
        n++;
    }
    return n * CELL_W;
}

static uint32_t (*canvas)[SCREEN_W_MAX];
static int canvas_w;

static void put(int x, int y, uint32_t color)
{
    if (x >= 0 && x < canvas_w && y >= 0 && y < SCREEN_H)
        canvas[y][x] = color;
}

static void draw_rows(int x, int y, const uint8_t *rows, int n, uint32_t color)
{
    for (int r = 0; r < n; r++)
        for (int c = 0; c < 5; c++)
            if (rows[r] & (0x10 >> c))
                put(x + c, y + r, color);
}

static void draw_text(int x, int y, const char *s, uint32_t color)
{
    while (*s) {
        char b;
        int a;
        s = decode(s, &b, &a);
        draw_rows(x, y + 2, glyph_rows(b), 7, color);
        if (a == ACC_CEDILLA)
            draw_rows(x, y + 9, accent_rows[a], 2, color);
        else if (a != ACC_NONE)
            draw_rows(x, y, accent_rows[a], 2, color);
        x += CELL_W;
    }
}

static void fill(int x, int y, int w, int h, uint32_t color)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            put(i, j, color);
}

/* Texto traduzido por cima do jogo (translate.c): quebra em linhas de até max_w px,
 * centralizadas em cx a partir de y, com contorno escuro. Devolve o número de linhas. */
int menu_draw_caption(uint32_t (*fb)[SCREEN_W_MAX], int width, int cx, int y, int max_w,
                      const char *text, uint32_t color)
{
    canvas = fb;
    canvas_w = width;
    char line[256];
    int n = 0, max_chars = max_w / CELL_W;
    const char *p = text;
    while (*p) {
        /* junta palavras enquanto couberem (contando caracteres, não bytes) */
        const char *end = p, *q = p;
        int chars = 0, chars_at_end = 0;
        while (*q && *q != '\n') {
            const char *w = q;
            int wc = 0;
            char b;
            int a;
            while (*w && *w != ' ' && *w != '\n') {
                w = decode(w, &b, &a);
                wc++;
            }
            if (chars && chars + 1 + wc > max_chars)
                break;
            chars += (chars ? 1 : 0) + wc;
            end = w;
            chars_at_end = chars;
            q = w;
            while (*q == ' ')
                q++;
        }
        if (end == p) {   /* palavra maior que a linha: corta */
            end = q;
            chars_at_end = max_chars;
        }
        size_t len = (size_t)(end - p) < sizeof(line) - 1 ? (size_t)(end - p) : sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = 0;
        int x = cx - chars_at_end * CELL_W / 2;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy)
                    draw_text(x + dx, y + dy, line, 0xFF000000u);
        draw_text(x, y, line, color);
        y += CELL_H;
        n++;
        p = end;
        while (*p == ' ' || *p == '\n')
            p++;
    }
    return n;
}

/* Texto simples por cima do jogo, a partir de (x, y), sem contorno */
void menu_draw_text(uint32_t (*fb)[SCREEN_W_MAX], int width, int x, int y, const char *text,
                    uint32_t color)
{
    canvas = fb;
    canvas_w = width;
    draw_text(x, y, text, color);
}

const uint8_t *menu_glyph(char c)
{
    return glyph_rows(c);
}

/* Largura em px de um texto na fonte dos menus (UTF-8) */
int menu_text_width(const char *s)
{
    return text_width(s);
}

/* ---------- páginas e itens do menu ---------- */

/* Página principal (pausa, ou OPTIONS no título) */
enum { IT_CONTINUE, IT_GAME_OPTIONS, IT_SAVE, IT_LOAD, IT_SETTINGS, IT_RESET, IT_QUIT, IT_COUNT };
/* Página "Configurações" */
enum { SET_WIDE, SET_SLOW, SET_VOLUME, SET_DISPLAY, SET_FILTER, SET_BORDER, SET_SPRITES, SET_LANGUAGE, SET_COUNT };

/* Texto no idioma escolhido: T("português", "english", "español") */
#define T(pt, en, es) (settings.language == LANG_EN ? (en) : settings.language == LANG_ES ? (es) : (pt))

enum { PAGE_MAIN, PAGE_SETTINGS, PAGE_CARDS };
/* Cartões dos arquivos: seleção de jogo do título, salvar ou carregar */
enum { CARDS_FILES, CARDS_SAVE, CARDS_LOAD };
enum { DLG_NONE, DLG_DELETE, DLG_OVERWRITE, DLG_LOAD, DLG_RESET, DLG_QUIT };
enum { FOCUS_CARDS, FOCUS_BUTTONS };
enum { BTN_NEW, BTN_DELETE, BTN_COUNT };

static int open_flag;
static int title_mode;        /* aberto na tela de título (OPTIONS ou START) */
static int page;
static int items[IT_COUNT];   /* itens visíveis da página principal, na ordem da tela */
static int n_items;
static int cursor;            /* índice em items[] */
static int set_cursor;        /* item da página de configurações */
static int cards_mode;
static int card_focus, card_sel, card_button;
static int deleting;          /* "Apagar arquivo": Enter no cartão pede para apagar */
static int dialog, dialog_yes;   /* confirmação SIM/NÃO aberta (DLG_*) */
static int anim;              /* quadros desde que o menu abriu (animações) */
static int page_anim;         /* quadros desde a última troca de página (entrada) */
static float bar_y = -1;      /* posição animada da barra de seleção */
static char toast[48];        /* aviso que some sozinho */
static int toast_until;
static StateInfo slot_info[STATE_SLOTS];

int menu_is_open(void) { return open_flag; }

static void set_page(int p)
{
    page = p;
    page_anim = 0;
    bar_y = -1;
    dialog = DLG_NONE;
}

static void open_with(int title)
{
    open_flag = 1;
    title_mode = title;
    n_items = 0;
    for (int i = 0; i < IT_COUNT; i++) {
        if ((i == IT_GAME_OPTIONS && !title) || (i == IT_SAVE && title))
            continue;   /* no título não há jogo para salvar */
        items[n_items++] = i;
    }
    cursor = 0;
    set_cursor = 0;
    toast[0] = 0;
    anim = 0;
    set_page(PAGE_MAIN);
}

void menu_open(void) { open_with(0); }

/* No título, OPTIONS abre este menu: "Opções do jogo" (a tela original, com controles e
 * teste de som) já escolhido e "Voltar" no lugar de "Continuar" */
void menu_open_title(void)
{
    open_with(1);
    cursor = 1;
}

void menu_close(void)
{
    open_flag = 0;
    dialog = DLG_NONE;
}

int menu_slot(void) { return card_sel; }

static void refresh_slots(void)
{
    for (int i = 0; i < STATE_SLOTS; i++)
        state_slot_info(i, &slot_info[i]);
}

static int newest_slot(void)
{
    int best = -1;
    for (int i = 0; i < STATE_SLOTS; i++)
        if (slot_info[i].used && (best < 0 || slot_info[i].time > slot_info[best].time))
            best = i;
    return best;
}

static void show_toast(const char *msg)
{
    snprintf(toast, sizeof(toast), "%s", msg);
    toast_until = anim + 150;
}

static void open_cards(int mode)
{
    cards_mode = mode;
    refresh_slots();
    deleting = 0;
    toast[0] = 0;
    card_button = BTN_NEW;
    int n = newest_slot();
    card_sel = n < 0 ? 0 : n;
    if (mode == CARDS_SAVE)   /* salvar começa no primeiro arquivo vazio */
        for (int i = STATE_SLOTS - 1; i >= 0; i--)
            if (!slot_info[i].used)
                card_sel = i;
    /* seleção de jogo sem jogos salvos: começa em "Novo jogo" */
    card_focus = mode == CARDS_FILES && n < 0 ? FOCUS_BUTTONS : FOCUS_CARDS;
    set_page(PAGE_CARDS);
}

/* teste (--slots-shot): menu de pausa já nos cartões de salvar/carregar */
void menu_open_slots(int load)
{
    open_with(0);
    open_cards(load ? CARDS_LOAD : CARDS_SAVE);
}

/* START no título: continuar um jogo salvo, começar um novo ou apagar */
void menu_open_files(void)
{
    open_with(1);
    open_cards(CARDS_FILES);
}

/* teste (--files-shot): estados da tela para conferir o desenho */
void menu_files_test(int card, int focus, int del, int dlg)
{
    card_sel = card;
    card_focus = focus;
    card_button = del ? BTN_DELETE : BTN_NEW;
    deleting = del;
    dialog = dlg ? DLG_DELETE : DLG_NONE;
    dialog_yes = 0;
}

/* teste (--menu-shot): página (0 principal, 1 configurações, 2 salvar, 3 carregar),
 * item escolhido e uma confirmação (DLG_*) */
void menu_page_test(int p, int item, int dlg)
{
    if (p == 1) {
        set_page(PAGE_SETTINGS);
        set_cursor = clampi(item, 0, SET_COUNT - 1);
    } else if (p >= 2) {
        open_cards(p == 2 ? CARDS_SAVE : CARDS_LOAD);
        card_sel = clampi(item, 0, STATE_SLOTS - 1);
    } else {
        cursor = clampi(item, 0, n_items - 1);
    }
    dialog = dlg;
    dialog_yes = 0;
}

void menu_slot_done(int ok)
{
    char msg[48];
    if (page != PAGE_CARDS)
        return;
    if (cards_mode == CARDS_SAVE) {
        refresh_slots();
        snprintf(msg, sizeof(msg), T("JOGO SALVO NO ARQUIVO %d", "GAME SAVED TO FILE %d",
                                     "PARTIDA GUARDADA EN EL ARCHIVO %d"), card_sel + 1);
        show_toast(ok ? msg : T("ERRO AO SALVAR O JOGO", "COULD NOT SAVE THE GAME", "ERROR AL GUARDAR LA PARTIDA"));
    } else if (!ok) {
        show_toast(T("ERRO AO CARREGAR O ARQUIVO", "COULD NOT LOAD THE FILE", "ERROR AL CARGAR EL ARCHIVO"));
    }
}

/* ---------- textos ---------- */

static const char *item_label(int i)
{
    switch (i) {
    case IT_CONTINUE: return title_mode ? T("VOLTAR", "BACK", "VOLVER") : T("CONTINUAR", "CONTINUE", "CONTINUAR");
    case IT_GAME_OPTIONS: return T("OPÇÕES DO JOGO", "GAME OPTIONS", "OPCIONES DEL JUEGO");
    case IT_SAVE: return T("SALVAR JOGO", "SAVE GAME", "GUARDAR PARTIDA");
    case IT_LOAD: return T("CARREGAR JOGO", "LOAD GAME", "CARGAR PARTIDA");
    case IT_SETTINGS: return T("CONFIGURAÇÕES", "SETTINGS", "CONFIGURACIÓN");
    case IT_RESET: return T("REINICIAR", "RESET", "REINICIAR");
    default: return T("SAIR", "QUIT", "SALIR");
    }
}

static const char *item_help(int i)
{
    switch (i) {
    case IT_CONTINUE:
        if (title_mode)
            return T("VOLTA PARA A TELA DE TÍTULO.", "RETURNS TO THE TITLE SCREEN.", "VUELVE A LA PANTALLA DE TÍTULO.");
        return T("FECHA O MENU E VOLTA AO JOGO.", "CLOSES THE MENU AND RETURNS TO THE GAME.",
                 "CIERRA EL MENÚ Y VUELVE AL JUEGO.");
    case IT_GAME_OPTIONS:
        return T("CONTROLES E TESTE DE SOM, NA TELA DE OPÇÕES ORIGINAL DO JOGO.",
                 "CONTROLS AND SOUND TEST, ON THE ORIGINAL OPTIONS SCREEN OF THE GAME.",
                 "CONTROLES Y PRUEBA DE SONIDO, EN LA PANTALLA DE OPCIONES ORIGINAL DEL JUEGO.");
    case IT_SAVE:
        return T("GRAVA O PONTO ATUAL DO JOGO EM UM DOS 3 ARQUIVOS.", "SAVES YOUR CURRENT PROGRESS TO ONE OF 3 FILES.",
                 "GUARDA EL PUNTO ACTUAL DEL JUEGO EN UNO DE LOS 3 ARCHIVOS.");
    case IT_LOAD:
        return T("CONTINUA A PARTIR DE UM JOGO SALVO.", "CONTINUES FROM A SAVED GAME.",
                 "CONTINÚA DESDE UNA PARTIDA GUARDADA.");
    case IT_SETTINGS:
        return T("VÍDEO, SOM, DESEMPENHO E IDIOMA. AS MUDANÇAS VALEM NA HORA E FICAM GRAVADAS.",
                 "VIDEO, SOUND, PERFORMANCE AND LANGUAGE. CHANGES APPLY RIGHT AWAY AND ARE SAVED.",
                 "VÍDEO, SONIDO, RENDIMIENTO E IDIOMA. LOS CAMBIOS SE APLICAN AL INSTANTE Y SE GUARDAN.");
    case IT_RESET:
        return T("REINICIA O CONSOLE, COMO O BOTÃO DE RESET. O PROGRESSO NÃO SALVO SE PERDE.",
                 "RESETS THE CONSOLE, LIKE THE RESET BUTTON. UNSAVED PROGRESS IS LOST.",
                 "REINICIA LA CONSOLA, COMO EL BOTÓN DE RESET. SE PIERDE EL PROGRESO NO GUARDADO.");
    case IT_QUIT:
        return T("FECHA O JOGO. O PROGRESSO NÃO SALVO SE PERDE.", "CLOSES THE GAME. UNSAVED PROGRESS IS LOST.",
                 "CIERRA EL JUEGO. SE PIERDE EL PROGRESO NO GUARDADO.");
    default: return "";
    }
}

static const char *set_label(int i)
{
    switch (i) {
    case SET_WIDE: return T("MODO DE TELA", "DISPLAY MODE", "MODO DE PANTALLA");
    case SET_SLOW: return T("SEM LENTIDÃO", "NO SLOWDOWN", "SIN RALENTIZACIÓN");
    case SET_VOLUME: return T("VOLUME", "VOLUME", "VOLUMEN");
    case SET_DISPLAY: return T("TAMANHO", "SIZE", "TAMAÑO");
    case SET_FILTER: return T("FILTRO", "FILTER", "FILTRO");
    case SET_BORDER: return T("MOLDURA", "BORDER", "MARCO");
    case SET_SPRITES: return "SPRITES";
    default: return T("IDIOMA", "LANGUAGE", "IDIOMA");
    }
}

static const char *set_help(int i)
{
    switch (i) {
    case SET_WIDE:
        return T("WIDESCREEN MOSTRA MAIS CENÁRIO DOS LADOS (424 PX). ORIGINAL USA OS 320 PX DO MEGA DRIVE.",
                 "WIDESCREEN SHOWS MORE OF THE LEVEL ON THE SIDES (424 PX). ORIGINAL USES THE 320 PX OF THE MEGA DRIVE.",
                 "PANORÁMICO MUESTRA MÁS ESCENARIO A LOS LADOS (424 PX). ORIGINAL USA LOS 320 PX DE LA MEGA DRIVE.");
    case SET_SLOW:
        return T("ACELERA O PROCESSADOR PARA ELIMINAR A LENTIDÃO DO JOGO ORIGINAL QUANDO HÁ MUITA COISA NA TELA.",
                 "SPEEDS UP THE CPU TO REMOVE THE SLOWDOWN OF THE ORIGINAL GAME WHEN THERE IS A LOT ON SCREEN.",
                 "ACELERA EL PROCESADOR PARA ELIMINAR LA RALENTIZACIÓN DEL JUEGO ORIGINAL CUANDO HAY MUCHO EN PANTALLA.");
    case SET_VOLUME:
        return T("VOLUME DA MÚSICA E DOS EFEITOS.", "MUSIC AND SOUND EFFECTS VOLUME.", "VOLUMEN DE LA MÚSICA Y LOS EFECTOS.");
    case SET_DISPLAY:
        return T("TAMANHO DA JANELA (2X, 3X OU 4X) OU TELA CHEIA.", "WINDOW SIZE (2X, 3X OR 4X) OR FULL SCREEN.",
                 "TAMAÑO DE LA VENTANA (2X, 3X O 4X) O PANTALLA COMPLETA.");
    case SET_FILTER:
        return T("APARÊNCIA DA IMAGEM: PIXELS NÍTIDOS, SUAVES, SCANLINES OU EFEITO DE TV ANTIGA.",
                 "IMAGE LOOK: SHARP OR SMOOTH PIXELS, SCANLINES OR AN OLD TV EFFECT.",
                 "ASPECTO DE LA IMAGEN: PÍXELES NÍTIDOS, SUAVES, SCANLINES O EFECTO DE TV ANTIGUA.");
    case SET_BORDER:
        return T("TELAS EM 4:3 (MODO ORIGINAL, NOTICIÁRIO...): LATERAIS COM A CENA DESFOCADA OU FAIXAS PRETAS.",
                 "4:3 SCREENS (ORIGINAL MODE, THE NEWS...): SIDES WITH THE SCENE BLURRED OR BLACK BARS.",
                 "PANTALLAS EN 4:3 (MODO ORIGINAL, NOTICIERO...): LATERALES CON LA ESCENA DIFUMINADA O FRANJAS NEGRAS.");
    case SET_SPRITES:
        return T("SEM LIMITE EVITA QUE OS SPRITES PISQUEM OU SUMAM QUANDO HÁ MUITOS NA MESMA LINHA.",
                 "NO LIMIT KEEPS SPRITES FROM FLICKERING OR VANISHING WHEN MANY SHARE A LINE.",
                 "SIN LÍMITE EVITA QUE LOS SPRITES PARPADEEN O DESAPAREZCAN CUANDO HAY MUCHOS EN LA MISMA LÍNEA.");
    case SET_LANGUAGE:
        return T("IDIOMA DOS MENUS E DOS TEXTOS DO JOGO (LEGENDAS EM JAPONÊS E TEXTOS EM INGLÊS).",
                 "LANGUAGE OF THE MENUS AND OF THE GAME TEXT (THE JAPANESE CAPTIONS ARE TRANSLATED).",
                 "IDIOMA DE LOS MENÚS Y DE LOS TEXTOS DEL JUEGO (SUBTÍTULOS EN JAPONÉS Y TEXTOS EN INGLÉS).");
    default: return "";
    }
}

static int set_is_toggle(int i)
{
    return i == SET_SLOW;
}

static const char *filter_label(int f)
{
    switch (f) {
    case FILTER_SHARP: return T("NÍTIDO", "SHARP", "NÍTIDO");
    case FILTER_SMOOTH: return T("SUAVE", "SMOOTH", "SUAVE");
    case FILTER_PIXEL: return T("PIXEL SUAVE", "SMOOTH PIXEL", "PÍXEL SUAVE");
    case FILTER_TV_CURVED: return T("TV CURVA", "CURVED TV", "TV CURVA");
    default: return filter_names[f];   /* SCANLINES, TV */
    }
}

static const char *set_value(int i)
{
    switch (i) {
    case SET_WIDE: return settings.widescreen ? T("WIDESCREEN", "WIDESCREEN", "PANORÁMICO") : "ORIGINAL";
    case SET_SLOW: return settings.no_slowdown ? T("LIGADO", "ON", "ACTIVADO") : T("DESLIGADO", "OFF", "DESACTIVADO");
    case SET_DISPLAY:
        if (settings.display == 3)
            return T("TELA CHEIA", "FULL SCREEN", "PANTALLA COMPLETA");
        return settings.display == 0 ? T("JANELA 2X", "WINDOW 2X", "VENTANA 2X") :
               settings.display == 1 ? T("JANELA 3X", "WINDOW 3X", "VENTANA 3X") : T("JANELA 4X", "WINDOW 4X", "VENTANA 4X");
    case SET_FILTER: return filter_label(settings.filter);
    case SET_BORDER: return settings.black_border ? T("PRETA", "BLACK", "NEGRO") : T("DESFOCADA", "BLURRED", "DIFUMINADO");
    case SET_SPRITES: return settings.no_sprite_limit ? T("SEM LIMITE", "NO LIMIT", "SIN LÍMITE") : "ORIGINAL";
    case SET_LANGUAGE: {   /* cada idioma com o próprio nome */
        static const char *names[LANG_COUNT] = {"PORTUGUÊS (BR)", "ENGLISH", "ESPAÑOL"};
        return names[settings.language];
    }
    default: return "";
    }
}

/* Muda o valor da configuração: dir = -1 (esquerda) ou +1 (direita/enter) */
static int change(int i, int dir)
{
    switch (i) {
    case SET_WIDE: settings.widescreen = !settings.widescreen; return 1;
    case SET_SLOW: settings.no_slowdown = !settings.no_slowdown; return 1;
    case SET_VOLUME: {
        int v = clampi(settings.volume + dir, 0, 10);
        if (v == settings.volume) return 0;
        settings.volume = v;
        return 1;
    }
    case SET_DISPLAY: settings.display = (settings.display + dir + 4) % 4; return 1;
    case SET_FILTER: settings.filter = (settings.filter + dir + FILTER_COUNT) % FILTER_COUNT; return 1;
    case SET_BORDER: settings.black_border = !settings.black_border; return 1;
    case SET_SPRITES: settings.no_sprite_limit = !settings.no_sprite_limit; return 1;
    case SET_LANGUAGE: settings.language = (settings.language + dir + LANG_COUNT) % LANG_COUNT; return 1;
    default: return 0;
    }
}

/* ---------- teclas ---------- */

static int is_enter(int key)
{
    return key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_z;
}

static void ask(int kind)
{
    dialog = kind;
    dialog_yes = 0;   /* "NÃO" escolhido de início */
}

static void ask_delete(void)
{
    if (!slot_info[card_sel].used)
        show_toast(T("ESTE ARQUIVO ESTÁ VAZIO", "THIS FILE IS EMPTY", "ESTE ARCHIVO ESTÁ VACÍO"));
    else
        ask(DLG_DELETE);
}

static int dialog_key(int key)
{
    if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN) {
        dialog_yes = !dialog_yes;
        return MENU_NONE;
    }
    if (key == SDLK_ESCAPE) {
        dialog = DLG_NONE;
        return MENU_NONE;
    }
    if (!is_enter(key))
        return MENU_NONE;
    int d = dialog;
    dialog = DLG_NONE;
    if (!dialog_yes)
        return MENU_NONE;
    switch (d) {
    case DLG_DELETE: {
        char msg[48];
        int ok = state_delete_slot(card_sel);
        refresh_slots();
        snprintf(msg, sizeof(msg), T("ARQUIVO %d APAGADO", "FILE %d DELETED", "ARCHIVO %d BORRADO"), card_sel + 1);
        show_toast(ok ? msg : T("ERRO AO APAGAR O ARQUIVO", "COULD NOT DELETE THE FILE", "ERROR AL BORRAR EL ARCHIVO"));
        if (newest_slot() < 0)   /* nada mais para apagar */
            deleting = 0;
        return MENU_NONE;
    }
    case DLG_OVERWRITE: return MENU_SAVE;
    case DLG_LOAD: return MENU_LOAD;
    case DLG_RESET: menu_close(); return MENU_RESET;
    case DLG_QUIT: menu_close(); return MENU_QUIT;
    default: return MENU_NONE;
    }
}

static int main_key(int key)
{
    switch (key) {
    case SDLK_ESCAPE:
        menu_close();
        return MENU_CLOSE;
    case SDLK_UP:
    case SDLK_DOWN:
        cursor = (cursor + (key == SDLK_UP ? n_items - 1 : 1)) % n_items;
        return MENU_NONE;
    default:
        break;
    }
    if (!is_enter(key))
        return MENU_NONE;
    switch (items[cursor]) {
    case IT_CONTINUE: menu_close(); return MENU_CLOSE;
    case IT_GAME_OPTIONS: menu_close(); return MENU_GAME_OPTIONS;
    case IT_SAVE: open_cards(CARDS_SAVE); return MENU_NONE;
    case IT_LOAD: open_cards(CARDS_LOAD); return MENU_NONE;
    case IT_SETTINGS: set_page(PAGE_SETTINGS); return MENU_NONE;
    case IT_RESET: ask(DLG_RESET); return MENU_NONE;
    case IT_QUIT: ask(DLG_QUIT); return MENU_NONE;
    default: return MENU_NONE;
    }
}

static int settings_key(int key)
{
    switch (key) {
    case SDLK_ESCAPE:
        set_page(PAGE_MAIN);
        return MENU_NONE;
    case SDLK_UP:
    case SDLK_DOWN:
        set_cursor = (set_cursor + (key == SDLK_UP ? SET_COUNT - 1 : 1)) % SET_COUNT;
        return MENU_NONE;
    case SDLK_LEFT:
    case SDLK_RIGHT:
        return change(set_cursor, key == SDLK_LEFT ? -1 : 1) ? MENU_CHANGED : MENU_NONE;
    default:
        return is_enter(key) && change(set_cursor, 1) ? MENU_CHANGED : MENU_NONE;
    }
}

static int cards_key(int key)
{
    int files = cards_mode == CARDS_FILES;
    switch (key) {
    case SDLK_ESCAPE:
        if (deleting) {
            deleting = 0;
        } else if (files) {
            menu_close();   /* volta à tela de título */
            return MENU_CLOSE;
        } else {
            set_page(PAGE_MAIN);
        }
        return MENU_NONE;
    case SDLK_LEFT:
    case SDLK_RIGHT: {
        int d = key == SDLK_LEFT ? -1 : 1;
        if (card_focus == FOCUS_CARDS)
            card_sel = (card_sel + d + STATE_SLOTS) % STATE_SLOTS;
        else
            card_button = (card_button + d + BTN_COUNT) % BTN_COUNT;
        return MENU_NONE;
    }
    case SDLK_UP:
    case SDLK_DOWN:
        if (files && !deleting)
            card_focus = card_focus == FOCUS_CARDS ? FOCUS_BUTTONS : FOCUS_CARDS;
        return MENU_NONE;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:   /* atalho: apagar o arquivo escolhido */
        if (card_focus == FOCUS_CARDS && cards_mode != CARDS_SAVE)
            ask_delete();
        return MENU_NONE;
    default:
        break;
    }
    if (!is_enter(key))
        return MENU_NONE;
    int used = slot_info[card_sel].used;
    if (card_focus == FOCUS_BUTTONS) {
        if (card_button == BTN_NEW) {
            menu_close();
            return MENU_NEW_GAME;
        }
        if (newest_slot() < 0) {
            show_toast(T("NENHUM JOGO SALVO", "NO SAVED GAMES", "NO HAY PARTIDAS GUARDADAS"));
            return MENU_NONE;
        }
        deleting = 1;
        card_focus = FOCUS_CARDS;
        if (!used)
            card_sel = newest_slot();
        return MENU_NONE;
    }
    if (deleting) {
        ask_delete();
        return MENU_NONE;
    }
    if (cards_mode == CARDS_SAVE) {
        if (used) {   /* sobrescrever pede confirmação */
            ask(DLG_OVERWRITE);
            return MENU_NONE;
        }
        return MENU_SAVE;
    }
    if (!used) {
        if (files) {   /* arquivo vazio na seleção de jogo: começa um jogo novo */
            menu_close();
            return MENU_NEW_GAME;
        }
        show_toast(T("ESTE ARQUIVO ESTÁ VAZIO", "THIS FILE IS EMPTY", "ESTE ARCHIVO ESTÁ VACÍO"));
        return MENU_NONE;
    }
    if (!title_mode) {   /* com um jogo em andamento, ele seria perdido */
        ask(DLG_LOAD);
        return MENU_NONE;
    }
    return MENU_LOAD;
}

int menu_key(int key)
{
    if (!open_flag)
        return MENU_NONE;
    if (dialog)
        return dialog_key(key);
    switch (page) {
    case PAGE_SETTINGS: return settings_key(key);
    case PAGE_CARDS: return cards_key(key);
    default: return main_key(key);
    }
}

/* ---------- toque ---------- */

/* Áreas tocáveis, anotadas a cada desenho (em px da imagem): item da página principal,
 * configuração, cartão, botão, SIM/NÃO da confirmação (Z_BLOCK: a caixa dela) e as teclas do
 * rodapé */
enum { Z_KEY, Z_MAIN, Z_SET, Z_CARD, Z_BUTTON, Z_DLG, Z_BLOCK };
#define MAX_ZONES 40
static struct { int x, y, w, h, kind, arg; } zones[MAX_ZONES];
static int n_zones;

static void zone(int x, int y, int w, int h, int kind, int arg)
{
    if (n_zones < MAX_ZONES) {
        zones[n_zones].x = x;
        zones[n_zones].y = y;
        zones[n_zones].w = w;
        zones[n_zones].h = h;
        zones[n_zones].kind = kind;
        zones[n_zones].arg = arg;
        n_zones++;
    }
}

/* toque numa configuração: no valor, a seta da esquerda volta e o resto avança (no volume, o
 * ponto da barra); no nome, o primeiro toque escolhe e o seguinte avança */
static int settings_tap(int i, int x, int zx, int zw)
{
    int was = set_cursor == i, right = zx + zw - 10;
    set_cursor = i;
    if (i == SET_VOLUME) {
        int bx = right - 36 - 10 - 10 * 8;   /* < barra > 100%, como em render_settings */
        if (x >= bx && x < bx + 10 * 8) {
            int v = (x - bx) / 8 + 1;
            if (v == settings.volume)
                v--;   /* tocar no último aceso apaga ele (até o mudo) */
            settings.volume = v;
            return MENU_CHANGED;
        }
        if (x >= bx - 14 && x < bx)
            return change(i, -1) ? MENU_CHANGED : MENU_NONE;
        if (x >= bx + 10 * 8)
            return change(i, 1) ? MENU_CHANGED : MENU_NONE;
        return MENU_NONE;
    }
    if (x < zx + zw / 2 && !was)
        return MENU_NONE;
    int left_arrow = right - 12 - text_width(set_value(i)) - 12;
    int dir = !set_is_toggle(i) && x >= zx + zw / 2 && x < left_arrow + 10 ? -1 : 1;
    return change(i, dir) ? MENU_CHANGED : MENU_NONE;
}

int menu_tap(int x, int y)
{
    if (!open_flag)
        return MENU_NONE;
    for (int n = n_zones - 1; n >= 0; n--) {   /* o desenhado por último fica por cima */
        int zx = zones[n].x, zy = zones[n].y, zw = zones[n].w, zh = zones[n].h, arg = zones[n].arg;
        if (x < zx || x >= zx + zw || y < zy || y >= zy + zh)
            continue;
        int kind = zones[n].kind;
        if (dialog && kind != Z_DLG && kind != Z_BLOCK && kind != Z_KEY)
            continue;   /* com a confirmação aberta, só ela */
        switch (kind) {
        case Z_KEY: return menu_key(arg);
        case Z_MAIN:
            cursor = arg;
            return main_key(SDLK_RETURN);
        case Z_SET: return settings_tap(arg, x, zx, zw);
        case Z_CARD:
            if (card_focus == FOCUS_CARDS && card_sel == arg)
                return cards_key(SDLK_RETURN);   /* segundo toque: abre, salva ou apaga */
            card_focus = FOCUS_CARDS;
            card_sel = arg;
            return MENU_NONE;
        case Z_BUTTON:
            card_focus = FOCUS_BUTTONS;
            card_button = arg;
            return cards_key(SDLK_RETURN);
        case Z_DLG:
            dialog_yes = arg;
            return dialog_key(SDLK_RETURN);
        default:
            return MENU_NONE;   /* caixa da confirmação */
        }
    }
    if (dialog)   /* fora da caixa: cancela */
        return dialog_key(SDLK_ESCAPE);
    return MENU_NONE;
}

/* ---------- desenho: peças ---------- */

static uint32_t mix(uint32_t a, uint32_t b, int t)   /* t = 0..256 */
{
    if (t <= 0) return a;
    if (t >= 256) return b;
    uint32_t r = 0xFF000000u;
    for (int sh = 0; sh < 24; sh += 8) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        r |= (uint32_t)(ca + (cb - ca) * t / 256) << sh;
    }
    return r;
}

static void blend(int x, int y, uint32_t color, int t)
{
    if (x >= 0 && x < canvas_w && y >= 0 && y < SCREEN_H)
        canvas[y][x] = mix(canvas[y][x], color, t);
}

static void blend_rect(int x, int y, int w, int h, uint32_t color, int t)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            blend(i, j, color, t);
}

static void gradient_rect(int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int j = 0; j < h; j++)
        fill(x, y + j, w, 1, mix(top, bottom, h > 1 ? j * 256 / (h - 1) : 0));
}

static void outline(int x, int y, int w, int h, int t, uint32_t color)
{
    fill(x, y, w, t, color);
    fill(x, y + h - t, w, t, color);
    fill(x, y, t, h, color);
    fill(x + w - t, y, t, h, color);
}

/* Texto ampliado (sc x sc por pixel), com degradê de cima para baixo */
static void draw_text_big(int x, int y, const char *s, uint32_t top, uint32_t bottom, int sc)
{
    while (*s) {
        char b;
        int a;
        s = decode(s, &b, &a);
        const uint8_t *rows = glyph_rows(b);
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (rows[r] & (0x10 >> c))
                    fill(x + c * sc, y + (2 + r) * sc, sc, sc, mix(top, bottom, r * 256 / 6));
        if (a != ACC_NONE) {
            int ay = a == ACC_CEDILLA ? 9 : 0;
            for (int r = 0; r < 2; r++)
                for (int c = 0; c < 5; c++)
                    if (accent_rows[a][r] & (0x10 >> c))
                        fill(x + c * sc, y + (ay + r) * sc, sc, sc, a == ACC_CEDILLA ? bottom : top);
        }
        x += CELL_W * sc;
    }
}

/* Título grande com sombra */
static void draw_title(int x, int y, const char *s, uint32_t top, uint32_t bottom)
{
    draw_text_big(x + 2, y + 2, s, 0xFF000000u, 0xFF000000u, 2);
    draw_text_big(x, y, s, top, bottom, 2);
}

static void draw_text_shadow(int x, int y, const char *s, uint32_t color)
{
    draw_text(x + 1, y + 1, s, 0xFF02040Cu);
    draw_text(x, y, s, color);
}

static void draw_text_center(int cx, int y, const char *s, uint32_t color)
{
    draw_text_shadow(cx - text_width(s) / 2, y, s, color);
}

static int utf8_chars(const char *s, int bytes)
{
    int n = 0;
    for (int i = 0; i < bytes; i++)
        n += ((unsigned char)s[i] & 0xC0) != 0x80;
    return n;
}

/* Texto quebrado em linhas de até max_chars caracteres; devolve o número de linhas */
static int draw_wrapped(int x, int y, int max_chars, int line_h, const char *s, uint32_t color)
{
    char line[128];
    int n = 0, chars = 0, lines = 0;
    while (*s) {
        const char *w = s;
        while (*s && *s != ' ')
            s++;
        int wb = (int)(s - w), wc = utf8_chars(w, wb);
        if (chars && chars + 1 + wc > max_chars) {
            line[n] = 0;
            draw_text_shadow(x, y + lines++ * line_h, line, color);
            n = chars = 0;
        }
        if (n + wb + 2 < (int)sizeof(line)) {
            if (chars) {
                line[n++] = ' ';
                chars++;
            }
            memcpy(line + n, w, wb);
            n += wb;
            chars += wc;
        }
        while (*s == ' ')
            s++;
    }
    if (chars) {
        line[n] = 0;
        draw_text_shadow(x, y + lines++ * line_h, line, color);
    }
    return lines;
}

/* Brilho que pulsa (0..256), para o que está selecionado */
static int pulse(void)
{
    return (int)(128 + 127 * sin(anim * 0.12));
}

#define FS_CYAN    0xFF48D8FFu
#define FS_RED     0xFFFF5A5Au
#define FS_GOLD    0xFFFFD45Au
#define FS_PANEL_T 0xFF1A2C62u
#define FS_PANEL_B 0xFF0C1534u
#define FS_EDGE    0xFF2C3E78u
#define FS_TEXT    0xFFF2F6FFu
#define FS_SOFT    0xFFAFC0E8u
#define FS_DIM     0xFF6A7AA8u

#define CARD_W 100
#define CARD_H 116
#define CARD_GAP 8
#define LAYOUT_W (3 * CARD_W + 2 * CARD_GAP)   /* largura útil, cabe nos 320 px originais */

/* Fundo: degradê azul-escuro, faixas diagonais que andam e vinheta nas bordas. Com
 * game_alpha > 0, o quadro do jogo aparece por baixo (menu de pausa). */
static void background(uint32_t src[][SCREEN_W_MAX], int w, int game_alpha)
{
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t base = mix(0xFF0D1A44u, 0xFF03060Fu, y * 256 / SCREEN_H);
        float dy = (y - SCREEN_H / 2) / (float)(SCREEN_H / 2);
        for (int x = 0; x < w; x++) {
            uint32_t c = base;
            if (game_alpha)
                c = mix(c, src[y][x], game_alpha);
            int d = (x + y + anim / 3) % 24;
            if (d < 2)
                c = mix(c, 0xFF22408Cu, d == 0 ? 60 : 30);
            float dx = (x - w / 2) / (float)(w / 2);
            int v = (int)((dx * dx * 0.6f + dy * dy * 0.4f) * 150);
            canvas[y][x] = mix(c, 0xFF000000u, v);
        }
    }
}

/* Onda elétrica (o tema do Pulseman): some nas pontas */
static void pulse_wave(int x0, int w, int y0, uint32_t color)
{
    for (int x = 0; x < w; x++) {
        float t = x / (float)w;
        float env = sinf(t * 3.14159f);
        float yv = sinf(x * 0.11f - anim * 0.15f) * 2.5f * env +
                   sinf(x * 0.37f + anim * 0.31f) * 1.0f * env;
        int y = y0 + (int)lrintf(yv);
        int a = (int)(env * 256);
        blend(x0 + x, y - 1, color, a / 4);
        blend(x0 + x, y + 1, color, a / 4);
        blend(x0 + x, y, mix(color, 0xFFFFFFFFu, 120), a);
    }
}

static void down_arrow(int cx, int y, uint32_t color)
{
    for (int r = 0; r < 4; r++)
        fill(cx - 3 + r, y + r, 7 - 2 * r, 1, color);
}

static void right_arrow(int x, int cy, uint32_t color)
{
    for (int c = 0; c < 4; c++)
        fill(x + c, cy - 3 + c, 1, 7 - 2 * c, color);
}

/* Painel com degradê, sombra e borda (selecionado: borda que pulsa) */
static void panel(int x, int y, int w, int h, int sel, uint32_t accent)
{
    blend_rect(x + 3, y + 4, w, h, 0xFF000000u, 120);
    gradient_rect(x, y, w, h, sel ? 0xFF24408Au : FS_PANEL_T, sel ? 0xFF101C46u : FS_PANEL_B);
    if (sel) {
        outline(x - 2, y - 2, w + 4, h + 4, 1, mix(accent, 0xFF000000u, 160));
        outline(x, y, w, h, 2, mix(accent, 0xFFFFFFFFu, pulse() / 2));
    } else {
        outline(x, y, w, h, 1, FS_EDGE);
    }
}

static void draw_button(int x, int y, int w, const char *label, int sel, uint32_t accent)
{
    const int h = 17;
    if (sel) {
        gradient_rect(x, y, w, h, mix(accent, 0xFF1A3A90u, 140), 0xFF13286Au);
        outline(x - 2, y - 2, w + 4, h + 4, 1, mix(accent, 0xFF000000u, 160));
        outline(x, y, w, h, 2, mix(accent, 0xFFFFFFFFu, pulse() / 2));
    } else {
        blend_rect(x + 2, y + 3, w, h, 0xFF000000u, 110);
        gradient_rect(x, y, w, h, FS_PANEL_T, FS_PANEL_B);
        outline(x, y, w, h, 1, FS_EDGE);
    }
    draw_text_center(x + w / 2, y + 3, label, sel ? FS_TEXT : FS_DIM);
}

static void format_score(uint32_t bcd, char *out)
{
    int n = 0, started = 0;
    for (int i = 7; i >= 0; i--) {
        int dgt = (bcd >> (i * 4)) & 15;
        if (dgt > 9) dgt = 9;
        if (dgt || started || i == 0) {
            out[n++] = (char)('0' + dgt);
            started = 1;
        }
    }
    out[n] = 0;
}

/* Barra de comandos no rodapé: teclas desenhadas como botões, seguidas da ação */
static int key_hint_width(const char *key, const char *action)
{
    return text_width(key) + 6 + 4 + text_width(action) + 12;
}

static int hint_key(const char *key)
{
    return !strcmp(key, "ESC") ? SDLK_ESCAPE : !strcmp(key, "DEL") ? SDLK_DELETE :
           !strcmp(key, "ENTER") ? SDLK_RETURN : 0;
}

static void footer(int w, int n, const char **keys, const char **actions)
{
    const int y = 203;
    gradient_rect(0, y, w, SCREEN_H - y, 0xFF0A1230u, 0xFF04070Eu);
    fill(0, y, w, 1, FS_EDGE);
    if (touch_enabled()) {
        /* no toque: as ações viram botões (Esc, Del); escolher e confirmar é tocar no item */
        int bw[4], m = 0, total = 0;
        const char *act[4];
        int key[4];
        for (int i = 0; i < n && m < 4; i++) {
            int k = hint_key(keys[i]);
            if (k == SDLK_ESCAPE || k == SDLK_DELETE) {
                act[m] = actions[i];
                key[m] = k;
                bw[m] = text_width(actions[i]) + 24;
                total += bw[m] + (m ? 10 : 0);
                m++;
            }
        }
        int x = (w - total) / 2;
        for (int i = 0; i < m; i++) {
            gradient_rect(x, y + 5, bw[i], 15, 0xFF3A4C88u, 0xFF1E2A56u);
            outline(x, y + 5, bw[i], 15, 1, 0xFF7E94D8u);
            draw_text_center(x + bw[i] / 2, y + 8, act[i], FS_TEXT);
            zone(x - 6, y, bw[i] + 12, SCREEN_H - y, Z_KEY, key[i]);
            x += bw[i] + 10;
        }
        return;
    }
    int total = -12;
    for (int i = 0; i < n; i++)
        total += key_hint_width(keys[i], actions[i]);
    int x = (w - total) / 2;
    for (int i = 0; i < n; i++) {
        int kw = text_width(keys[i]) + 6;
        gradient_rect(x, y + 7, kw, 12, 0xFF3A4C88u, 0xFF1E2A56u);
        outline(x, y + 7, kw, 12, 1, 0xFF7E94D8u);
        draw_text(x + 3, y + 8, keys[i], FS_TEXT);
        draw_text_shadow(x + kw + 4, y + 8, actions[i], FS_SOFT);
        x += key_hint_width(keys[i], actions[i]);
    }
}

static void draw_toast(int w, int y)
{
    if (!toast[0] || anim >= toast_until)
        return;
    int tw = text_width(toast) + 16, tx = (w - tw) / 2;
    int a = toast_until - anim < 20 ? (toast_until - anim) * 12 : 230;
    blend_rect(tx, y, tw, 13, 0xFF000000u, a);
    blend_rect(tx, y, tw, 1, FS_GOLD, a / 2);
    blend_rect(tx, y + 12, tw, 1, FS_GOLD, a / 2);
    draw_text(tx + 8, y + 1, toast, mix(0xFF000000u, FS_GOLD, a + 26));
}

/* Confirmação SIM/NÃO por cima da página */
static void draw_dialog(int w)
{
    char q[48];
    const char *sub = T("O PROGRESSO NÃO SALVO SERÁ PERDIDO", "UNSAVED PROGRESS WILL BE LOST",
                        "SE PERDERÁ EL PROGRESO NO GUARDADO");
    uint32_t accent = FS_RED;
    switch (dialog) {
    case DLG_DELETE:
        snprintf(q, sizeof(q), T("APAGAR O ARQUIVO %d?", "DELETE FILE %d?", "¿BORRAR EL ARCHIVO %d?"), card_sel + 1);
        sub = T("ESTA AÇÃO NÃO PODE SER DESFEITA", "THIS CANNOT BE UNDONE", "ESTA ACCIÓN NO SE PUEDE DESHACER");
        break;
    case DLG_OVERWRITE:
        snprintf(q, sizeof(q), T("SOBRESCREVER O ARQUIVO %d?", "OVERWRITE FILE %d?", "¿SOBRESCRIBIR EL ARCHIVO %d?"),
                 card_sel + 1);
        sub = T("O JOGO SALVO NELE SERÁ SUBSTITUÍDO", "ITS SAVED GAME WILL BE REPLACED",
                "SE REEMPLAZARÁ LA PARTIDA GUARDADA");
        accent = FS_GOLD;
        break;
    case DLG_LOAD:
        snprintf(q, sizeof(q), T("CARREGAR O ARQUIVO %d?", "LOAD FILE %d?", "¿CARGAR EL ARCHIVO %d?"), card_sel + 1);
        accent = FS_CYAN;
        break;
    case DLG_RESET: snprintf(q, sizeof(q), "%s", T("REINICIAR O JOGO?", "RESET THE GAME?", "¿REINICIAR EL JUEGO?")); break;
    default: snprintf(q, sizeof(q), "%s", T("SAIR DO JOGO?", "QUIT THE GAME?", "¿SALIR DEL JUEGO?")); break;
    }
    blend_rect(0, 0, w, SCREEN_H, 0xFF000000u, 150);
    const int dw = 228, dh = 82;
    int x = (w - dw) / 2, y = 62;
    blend_rect(x + 4, y + 5, dw, dh, 0xFF000000u, 140);
    gradient_rect(x, y, dw, dh, mix(0xFF141C40u, accent, 30), 0xFF080C1Cu);
    outline(x, y, dw, dh, 2, accent);
    outline(x + 3, y + 3, dw - 6, dh - 6, 1, mix(0xFF141C40u, accent, 80));
    draw_text_center(x + dw / 2, y + 10, q, FS_TEXT);
    draw_text_center(x + dw / 2, y + 26, sub, FS_DIM);
    const int bw = 70, gap = 20;
    int bx = x + (dw - 2 * bw - gap) / 2, by = y + 50;
    draw_button(bx, by, bw, T("SIM", "YES", "SÍ"), dialog_yes, accent);
    draw_button(bx + bw + gap, by, bw, T("NÃO", "NO", "NO"), !dialog_yes, FS_CYAN);
    zone(x, y, dw, dh, Z_BLOCK, 0);
    zone(bx - 4, by - 6, bw + 8, 29, Z_DLG, 1);
    zone(bx + bw + gap - 4, by - 6, bw + 8, 29, Z_DLG, 0);
}

static void dialog_footer(int w)
{
    static const char *k[] = {"\x03 \x04", "ENTER", "ESC"};
    const char *a[] = {T("ESCOLHER", "SELECT", "ELEGIR"), T("CONFIRMAR", "CONFIRM", "CONFIRMAR"),
                       T("CANCELAR", "CANCEL", "CANCELAR")};
    footer(w, 3, k, a);
}

/* Entrada da página: o conteúdo sobe um pouco nos primeiros quadros */
static int page_rise(void)
{
    return page_anim < 12 ? (12 - page_anim) * (12 - page_anim) / 12 : 0;
}

/* ---------- página principal ---------- */

static void render_main(int w)
{
    int x0 = (w - LAYOUT_W) / 2, rise = page_rise();
    /* faixa escura à esquerda, que some aos poucos na borda */
    int band = x0 + 156;
    for (int x = 0; x < band + 24; x++) {
        int a = x < band ? 150 : 150 * (band + 24 - x) / 24;
        for (int y = 0; y < 203; y++)
            blend(x, y, 0xFF02040Au, a);
    }
    fill(band, 0, 1, 203, mix(0xFF02040Au, FS_EDGE, 120));

    const char *title = title_mode ? T("OPÇÕES", "OPTIONS", "OPCIONES") : T("PAUSA", "PAUSE", "PAUSA");
    draw_title(x0 + 8, 4 - rise, title, 0xFFFFFFFFu, 0xFF8FD8FFu);
    pulse_wave(x0, 150, 31, FS_CYAN);

    /* lista: a barra de seleção desliza até o item */
    const int y0 = 46, row_h = 17;
    int it = items[cursor];
    int danger = it == IT_RESET || it == IT_QUIT;
    uint32_t accent = danger ? FS_RED : FS_CYAN;
    float target = (float)(y0 + cursor * row_h);
    bar_y = bar_y < 0 ? target : bar_y + (target - bar_y) * 0.35f;
    int by = (int)lrintf(bar_y);
    for (int x = 0; x < 150; x++)
        blend_rect(x0 + 2 + x, by - 3, 1, 15, mix(accent, 0xFF1A3A90u, 120), 170 - x);
    fill(x0 + 2, by - 3, 2, 15, mix(accent, 0xFFFFFFFFu, pulse() / 2));
    for (int r = 0; r < n_items; r++) {
        int sel = r == cursor, y = y0 + r * row_h + rise * (r + 2) / 3;
        int i = items[r];
        if (i == IT_RESET && r > 0)   /* separa as ações do sistema */
            fill(x0 + 10, y - 6, 130, 1, mix(FS_EDGE, 0xFF000000u, 80));
        if (sel)
            right_arrow(x0 + 9, y + 5, FS_GOLD);
        draw_text_shadow(x0 + (sel ? 19 : 16), y, item_label(i), sel ? FS_TEXT : FS_SOFT);
        zone(x0 - 8, y - 5, 164, row_h, Z_MAIN, r);
    }

    /* painel à direita: o que o item faz e, numa fase, vidas e pontos */
    int px = x0 + 172, pw = LAYOUT_W - 172, py = 46 + rise;
    panel(px, py, pw, 84, 0, accent);
    gradient_rect(px + 2, py + 2, pw - 4, 13, mix(accent, 0xFF1A3A90u, 170), 0xFF16244Cu);
    draw_text_center(px + pw / 2, py + 3, item_label(it), FS_TEXT);
    draw_wrapped(px + 7, py + 21, (pw - 12) / CELL_W, 11, item_help(it), FS_SOFT);
    int scene = work_ram[0xF803] & 0x7C;
    if (!title_mode && (scene == 0x08 || scene == 0x1C)) {
        int sy = py + 94;
        panel(px, sy, pw, 40, 0, accent);
        char v[16];
        uint8_t lv = work_ram[0xFD44];
        snprintf(v, sizeof(v), "%d", (lv >> 4) * 10 + (lv & 15));
        draw_text_shadow(px + 8, sy + 8, T("VIDAS", "LIVES", "VIDAS"), FS_DIM);
        draw_text_shadow(px + pw - 8 - text_width(v), sy + 8, v, FS_TEXT);
        uint32_t sc = (uint32_t)work_ram[0xFD3C] << 24 | work_ram[0xFD3D] << 16 | work_ram[0xFD3E] << 8 | work_ram[0xFD3F];
        format_score(sc, v);
        draw_text_shadow(px + 8, sy + 22, T("PONTOS", "SCORE", "PUNTOS"), FS_DIM);
        draw_text_shadow(px + pw - 8 - text_width(v), sy + 22, v, FS_GOLD);
    }

    if (dialog) {
        draw_dialog(w);
        dialog_footer(w);
        return;
    }
    static const char *k[] = {"\x01 \x02", "ENTER", "ESC"};
    const char *a[] = {T("ESCOLHER", "SELECT", "ELEGIR"), T("CONFIRMAR", "CONFIRM", "CONFIRMAR"),
                       title_mode ? T("VOLTAR", "BACK", "VOLVER") : T("CONTINUAR", "RESUME", "CONTINUAR")};
    footer(w, 3, k, a);
}

/* ---------- configurações ---------- */

static void render_settings(int w)
{
    int x0 = (w - LAYOUT_W) / 2, rise = page_rise();
    const char *title = T("CONFIGURAÇÕES", "SETTINGS", "CONFIGURACIÓN");
    /* um pouco mais alto: a cedilha não encosta na onda */
    draw_title((w - text_width(title) * 2) / 2, 4 - rise, title, 0xFFFFFFFFu, 0xFF8FD8FFu);
    pulse_wave(x0 - 20, LAYOUT_W + 40, 31, FS_CYAN);

    const int y0 = 40, row_h = 16;   /* 8 itens até a descrição em y = 172 */
    for (int i = 0; i < SET_COUNT; i++) {
        int sel = i == set_cursor && !dialog, y = y0 + i * row_h + rise * (i + 2) / 3;
        if (sel) {
            gradient_rect(x0, y, LAYOUT_W, 14, 0xFF24408Au, 0xFF13286Au);
            outline(x0 - 2, y - 2, LAYOUT_W + 4, 18, 1, mix(FS_CYAN, 0xFF000000u, 160));
            outline(x0, y, LAYOUT_W, 14, 2, mix(FS_CYAN, 0xFFFFFFFFu, pulse() / 2));
        } else {
            blend_rect(x0, y, LAYOUT_W, 14, 0xFF000000u, 100);
            outline(x0, y, LAYOUT_W, 14, 1, mix(FS_EDGE, 0xFF000000u, 60));
        }
        draw_text_shadow(x0 + 10, y + 2, set_label(i), sel ? FS_TEXT : FS_SOFT);
        zone(x0, y - 1, LAYOUT_W, row_h, Z_SET, i);

        int right = x0 + LAYOUT_W - 10;
        int nudge = sel && (anim / 10) % 2;
        if (i == SET_VOLUME) {
            char v[16];
            if (settings.volume)
                snprintf(v, sizeof(v), "%d%%", settings.volume * 10);
            else
                snprintf(v, sizeof(v), "%s", T("MUDO", "MUTE", "SILENCIO"));
            draw_text_shadow(right - text_width(v), y + 2, v, sel ? FS_TEXT : FS_SOFT);
            int bx = right - 36 - 10 - 10 * 8;   /* < barra > 100% */
            for (int s = 0; s < 10; s++) {
                int on = s < settings.volume;
                uint32_t c = on ? mix(FS_CYAN, FS_GOLD, s * 256 / 9) : 0xFF1A2448u;
                fill(bx + s * 8, y + 2, 6, 8, sel || !on ? c : mix(c, 0xFF000000u, 70));
            }
            if (sel) {
                draw_text(bx - 10 - nudge, y + 2, "\x03", FS_GOLD);
                draw_text(bx + 10 * 8 + 2 + nudge, y + 2, "\x04", FS_GOLD);
            }
            continue;
        }
        const char *v = set_value(i);
        int vw = text_width(v);
        if (set_is_toggle(i)) {   /* chave: LIGADO aceso, DESLIGADO apagado */
            int on = settings.no_slowdown;
            int pw2 = vw + 12, pxx = right - pw2;
            fill(pxx, y + 1, pw2, 12, on ? mix(FS_CYAN, 0xFF0A1530u, 120) : 0xFF141C38u);
            outline(pxx, y + 1, pw2, 12, 1, on ? FS_CYAN : FS_EDGE);
            draw_text(pxx + 6, y + 2, v, on ? FS_TEXT : FS_DIM);
            continue;
        }
        draw_text_shadow(right - 12 - vw, y + 2, v, sel ? FS_TEXT : FS_SOFT);
        if (sel) {
            draw_text(right - 12 - vw - 10 - nudge, y + 2, "\x03", FS_GOLD);
            draw_text(right - 4 + nudge, y + 2, "\x04", FS_GOLD);
        }
    }

    /* descrição da configuração escolhida */
    int dy = 172;
    blend_rect(x0, dy, LAYOUT_W, 27, 0xFF000000u, 120);
    outline(x0, dy, LAYOUT_W, 27, 1, mix(FS_EDGE, 0xFF000000u, 40));
    fill(x0, dy, 2, 27, FS_CYAN);
    draw_wrapped(x0 + 9, dy + 4, (LAYOUT_W - 14) / CELL_W, 10, set_help(set_cursor), FS_SOFT);

    static const char *k[] = {"\x03 \x04", "\x01 \x02", "ESC"};
    const char *a[] = {T("MUDAR", "CHANGE", "CAMBIAR"), T("ESCOLHER", "SELECT", "ELEGIR"), T("VOLTAR", "BACK", "VOLVER")};
    footer(w, 3, k, a);
}

/* ---------- cartões dos arquivos ---------- */

static void draw_card(int i, int x, int y)
{
    StateInfo *si = &slot_info[i];
    int sel = card_focus == FOCUS_CARDS && card_sel == i && !dialog;
    uint32_t accent = deleting ? FS_RED : FS_CYAN;
    panel(x, y, CARD_W, CARD_H, sel, accent);

    /* faixa do topo com o número do arquivo */
    gradient_rect(x + 2, y + 2, CARD_W - 4, 13, sel ? mix(accent, 0xFF1A3A90u, 150) : 0xFF22346Cu,
                  sel ? 0xFF1A3A90u : 0xFF16244Cu);
    char label[16];
    snprintf(label, sizeof(label), T("ARQUIVO %d", "FILE %d", "ARCHIVO %d"), i + 1);
    draw_text_center(x + CARD_W / 2, y + 3, label, sel ? FS_TEXT : FS_SOFT);

    /* miniatura (recortada no meio se for mais larga que o cartão) */
    const int tx = x + 4, ty = y + 18, tw = CARD_W - 8;
    fill(tx - 1, ty - 1, tw + 2, THUMB_H + 2, 0xFF000000u);
    if (si->used) {
        int sx = (si->thumb_w - tw) / 2;
        for (int yy = 0; yy < THUMB_H; yy++)
            for (int xx = 0; xx < tw; xx++) {
                int u = xx + sx;
                if (u < 0 || u >= si->thumb_w)
                    continue;
                uint32_t c = si->thumb[yy][u];
                put(tx + xx, ty + yy, sel ? c : mix(c, 0xFF050A1Au, 110));
            }
        if (sel)   /* brilho que passa pela imagem */
            for (int yy = 0; yy < THUMB_H; yy++) {
                int bx = (anim * 2) % (tw + 120) - 40 + yy / 2;
                for (int k = 0; k < 6; k++)
                    if (bx + k >= 0 && bx + k < tw)
                        blend(tx + bx + k, ty + yy, 0xFFFFFFFFu, 40);
            }
    } else {
        for (int yy = 0; yy < THUMB_H; yy++)
            for (int xx = 0; xx < tw; xx++)
                put(tx + xx, ty + yy, ((xx + yy) / 4) % 2 ? 0xFF0A1128u : 0xFF0C142Eu);
        draw_text_center(x + CARD_W / 2, ty + THUMB_H / 2 - 6, T("VAZIO", "EMPTY", "VACÍO"), sel ? FS_SOFT : FS_DIM);
    }

    /* data e informações do jogo */
    int ly = y + 78;
    fill(x + 6, ly - 2, CARD_W - 12, 1, FS_EDGE);
    if (!si->used) {
        const char *hint = cards_mode == CARDS_FILES ? T("NOVO JOGO", "NEW GAME", "NUEVA PARTIDA") :
                           cards_mode == CARDS_SAVE ? T("SALVAR AQUI", "SAVE HERE", "GUARDAR AQUÍ") : "";
        draw_text_center(x + CARD_W / 2, ly + 2, "- - -", FS_DIM);
        draw_text_center(x + CARD_W / 2, ly + 18, hint, sel && !deleting ? FS_GOLD : FS_DIM);
        return;
    }
    char when[24] = "";
    time_t t = (time_t)si->time;
    struct tm *tm = localtime(&t);
    if (tm)
        strftime(when, sizeof(when), T("%d/%m/%y %H:%M", "%m/%d/%y %H:%M", "%d/%m/%y %H:%M"), tm);
    draw_text_center(x + CARD_W / 2, ly, when, FS_SOFT);
    if (si->in_stage) {
        char v[16];
        snprintf(v, sizeof(v), "%d", si->lives);
        draw_text_shadow(x + 7, ly + 12, T("VIDAS", "LIVES", "VIDAS"), FS_DIM);
        draw_text_shadow(x + CARD_W - 7 - text_width(v), ly + 12, v, FS_TEXT);
        format_score(si->score, v);
        draw_text_shadow(x + 7, ly + 24, T("PONTOS", "SCORE", "PUNTOS"), FS_DIM);
        draw_text_shadow(x + CARD_W - 7 - text_width(v), ly + 24, v, FS_GOLD);
    } else {
        draw_text_center(x + CARD_W / 2, ly + 18, T("FORA DE FASE", "NOT IN A STAGE", "FUERA DE NIVEL"), FS_DIM);
    }
}

static void render_cards(int w)
{
    int x0 = (w - LAYOUT_W) / 2, rise = page_rise();
    const char *title = deleting ? T("APAGAR ARQUIVO", "DELETE FILE", "BORRAR ARCHIVO") :
                        cards_mode == CARDS_SAVE ? T("SALVAR JOGO", "SAVE GAME", "GUARDAR PARTIDA") :
                        cards_mode == CARDS_LOAD ? T("CARREGAR JOGO", "LOAD GAME", "CARGAR PARTIDA") :
                        T("SELECIONE O ARQUIVO", "SELECT A FILE", "SELECCIONA UN ARCHIVO");
    int tx = (w - text_width(title) * 2) / 2;
    if (deleting)
        draw_title(tx, 6 + rise, title, 0xFFFFE0E0u, FS_RED);
    else
        draw_title(tx, 6 + rise, title, 0xFFFFFFFFu, 0xFF8FD8FFu);
    pulse_wave(x0 - 20, LAYOUT_W + 40, 30 + rise, deleting ? FS_RED : FS_CYAN);

    for (int i = 0; i < STATE_SLOTS; i++) {
        int sel = card_focus == FOCUS_CARDS && card_sel == i && !dialog;
        int cx = x0 + i * (CARD_W + CARD_GAP), cy = 44 + rise * (i + 2) / 2 - (sel ? 2 : 0);
        draw_card(i, cx, cy);
        zone(cx, cy, CARD_W, CARD_H, Z_CARD, i);
        if (sel)
            down_arrow(cx + CARD_W / 2, cy - 8 + (int)(1.5 * sin(anim * 0.2)), deleting ? FS_RED : FS_GOLD);
    }

    if (cards_mode == CARDS_FILES) {
        const int bw = (LAYOUT_W - 16) / 2, by = 166 + rise * 3;
        int bsel = card_focus == FOCUS_BUTTONS && !dialog;
        draw_button(x0, by, bw, T("NOVO JOGO", "NEW GAME", "NUEVA PARTIDA"), bsel && card_button == BTN_NEW, FS_CYAN);
        draw_button(x0 + bw + 16, by, bw, T("APAGAR ARQUIVO", "DELETE FILE", "BORRAR ARCHIVO"),
                    bsel && card_button == BTN_DELETE, FS_RED);
        zone(x0, by - 4, bw, 25, Z_BUTTON, BTN_NEW);
        zone(x0 + bw + 16, by - 4, bw, 25, Z_BUTTON, BTN_DELETE);
    } else {
        const char *help = cards_mode == CARDS_SAVE ?
            T("ESCOLHA ONDE GRAVAR O JOGO", "CHOOSE WHERE TO SAVE", "ELIGE DÓNDE GUARDAR LA PARTIDA") :
            T("ESCOLHA O JOGO PARA CONTINUAR", "CHOOSE A GAME TO CONTINUE", "ELIGE LA PARTIDA PARA CONTINUAR");
        draw_text_center(w / 2, 170 + rise * 3, help, FS_DIM);
    }
    draw_toast(w, 187);

    if (dialog) {
        draw_dialog(w);
        dialog_footer(w);
    } else {
        const char *k[3], *a[3];
        int n = 0, used = slot_info[card_sel].used, on_card = card_focus == FOCUS_CARDS;
        if (deleting) {
            k[n] = "ENTER"; a[n++] = T("APAGAR", "DELETE", "BORRAR");
            k[n] = "ESC"; a[n++] = T("CANCELAR", "CANCEL", "CANCELAR");
        } else {
            k[n] = "ENTER";
            a[n++] = !on_card ? T("CONFIRMAR", "CONFIRM", "CONFIRMAR") :
                     cards_mode == CARDS_SAVE ? T("SALVAR", "SAVE", "GUARDAR") :
                     used ? T("CARREGAR", "LOAD", "CARGAR") :
                     cards_mode == CARDS_FILES ? T("NOVO JOGO", "NEW GAME", "NUEVA PARTIDA") : T("CONFIRMAR", "CONFIRM", "CONFIRMAR");
            if (on_card && used && cards_mode != CARDS_SAVE) {
                k[n] = "DEL"; a[n++] = T("APAGAR", "DELETE", "BORRAR");
            }
            k[n] = "ESC"; a[n++] = T("VOLTAR", "BACK", "VOLVER");
        }
        footer(w, n, k, a);
    }

    /* seleção de jogo do título: a tela clareia ao abrir */
    if (cards_mode == CARDS_FILES && anim < 14)
        blend_rect(0, 0, w, SCREEN_H, 0xFF000000u, (14 - anim) * 256 / 14);
}

/* ---------- desenho ---------- */

void menu_render(uint32_t dst[][SCREEN_W_MAX], uint32_t src[][SCREEN_W_MAX], int width)
{
    canvas = dst;
    canvas_w = width;
    n_zones = 0;
    anim++;
    page_anim++;
    /* a seleção de jogo do título tem fundo próprio; nas outras páginas o jogo aparece
     * apagado por baixo */
    int files = page == PAGE_CARDS && cards_mode == CARDS_FILES;
    background(src, width, files ? 0 : 70);
    switch (page) {
    case PAGE_SETTINGS: render_settings(width); break;
    case PAGE_CARDS: render_cards(width); break;
    default: render_main(width); break;
    }
}
