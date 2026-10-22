/* Ponto de entrada: carrega a ROM, roda quadros e apresenta com SDL2.
 *
 * pulseman [rom] [--headless] [--frames N] [--shot QUADRO arquivo.bmp]...
 *          [--input roteiro.txt] [--scale N] [--missing arquivo] [--wav saida.wav] [--mute]
 *
 * Roteiro de entrada: linhas "QUADRO BOTOES", ex. "300 S" ou "420 R A".
 * Botões: U D L R A B C S (start); "-" solta tudo. Vale até a próxima linha.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "runtime.h"
#include "lockstep.h"
#include "sound.h"
#include "game/game.h"
#include "menu.h"
#include "filters.h"
#include "savestate.h"
#include "intro_logos.h"
#include "border.h"
#ifdef PM_DIST
#include "dist.h"
#endif

extern const char pm_rom_sha1[];

#define MAX_SHOTS 64
#define MAX_SCRIPT 4096

static struct { int frame; const char *path; } shots[MAX_SHOTS];
static int n_shots;
static struct { int frame; uint32_t buttons; } script[MAX_SCRIPT];
static int n_script;

/* Widescreen por cena (índice em $FFF802). Todas as telas usam a tela larga; os ajustes
 * de HUD/objetos/mapa valem só nas fases ($08 e $1C), e algumas telas têm tratamento
 * próprio (seleção de fases). wide_pillarbox fica disponível para telas sem conserto. */
static int widescreen_enabled;

static void update_widescreen_for_scene(void)
{
    if (!widescreen_enabled)
        return;
    static int force_wide = -1;
    if (force_wide < 0)
        force_wide = getenv("PM_FORCE_WIDE") != NULL; /* depuração: nunca usa faixas pretas */
    uint32_t scene = work_ram[0xF803] & 0x7C;
    int stage = scene == 0x08 || scene == 0x1C;  /* fase: tela larga + ajustes de HUD/objetos */
    /* seleção de fases: o quadriculado e a moldura (plano A) cobrem a tela larga, mas o
     * plano B guarda o globo da fase vizinha logo ao lado; o B fica só na área original */
    wide_clip_planes = (scene == 0x20 && !force_wide) ? 2 : 0;
    /* logo da SEGA: as letras são montadas no plano A fora da área original e entram
     * na tela; nas colunas extras elas apareceriam antes da hora. O chuvisco (plano B)
     * continua na tela larga toda. */
    if (scene == 0x00 && !force_wide)
        wide_clip_planes = 1;
    /* cutscenes: a abertura da fase (cena $28, o símbolo do vilão e as setas que entram
     * pelas bordas) e o noticiário com a jornalista ($30) são montadas para 320 px; ficam
     * no formato original, com faixas pretas */
    wide_pillarbox = (scene == 0x28 || scene == 0x30) && !force_wide;
    map_stream_extra = stage ? 4 : 0;     /* carrega 64 px a mais de mapa de cada lado */
    hud_shift = stage ? 52 : 0;           /* energia/vidas: 16 px da borda esquerda */
    hud_shift_right = stage ? 84 : 0;     /* pontuação: 16 px da borda direita */
    wide_sprite_extra = stage ? 52 : 0;   /* objetos desenhados até a nova borda direita */
}

/* Pausa do próprio jogo: bit 5 de $FFFB07 (rotina em $272B2; ao pausar o jogo também
 * para a música via $FFF007). Só vale nas cenas de fase. */
static int game_in_stage(void)
{
    uint32_t scene = work_ram[0xF803] & 0x7C;
    return scene == 0x08 || scene == 0x1C;
}

static int game_paused(void)
{
    return game_in_stage() && (work_ram[0xFB07] & 0x20);
}

/* Quadros em que o runtime segura Start no controle (para pausar/despausar o jogo) */
static int inject_start;

/* Tela de título (cena $04): Start com o cursor em OPTIONS ($FFF807 = 8; confirmado em
 * $2946) abre o nosso menu de opções em vez da tela original. O Start não chega ao jogo;
 * "Opções do jogo" segue para a tela original com um Start virtual (inject_start). */
/* START ($FFF807 = 0) abre antes a seleção de jogo (novo jogo / slots / apagar); "Novo
 * jogo" segue com o mesmo Start virtual. O jogo só aceita o Start com $FFF804 < $23A. */
enum { TITLE_MENU_NONE, TITLE_MENU_OPTIONS, TITLE_MENU_FILES };
static int title_menu_request, title_start_held;

static uint32_t title_options_filter(uint32_t pad)
{
    int on_title = (work_ram[0xF803] & 0x7C) == 0x04;
    int ready = ((work_ram[0xF804] << 8) | work_ram[0xF805]) < 0x23A;
    int item = !on_title || !ready ? TITLE_MENU_NONE :
               work_ram[0xF807] == 8 ? TITLE_MENU_OPTIONS :
               work_ram[0xF807] == 0 ? TITLE_MENU_FILES : TITLE_MENU_NONE;
    int held = title_start_held;
    title_start_held = (pad & PAD_START) != 0;   /* só um aperto novo conta */
    if (!(pad & PAD_START) || !item)
        return pad;
    if (!held)
        title_menu_request = item;
    return pad & ~PAD_START;
}

/* Liga/desliga o widescreen com o jogo rodando. No modo original com a moldura desfocada
 * a tela também tem 424 px, mas sempre com faixas (os 320 px do meio são os do original)
 * e nenhum ajuste de widescreen no jogo; com a moldura preta fica em 320 px. */
static void set_widescreen(int on)
{
    widescreen_enabled = on;
#ifdef PM_LOCKSTEP
    int framed = 0;
#else
    int framed = !on && !settings.black_border;
#endif
    vdp_set_widescreen(on || framed ? 52 : 0);   /* 320 + 2*52 = 424 px, ~16:9 */
    if (!on) {
        wide_pillarbox = framed;
        wide_clip_planes = 0;
        map_stream_extra = hud_shift = hud_shift_right = wide_sprite_extra = 0;
    }
}

/* Fora das fases, tela cujas colunas extras saíram de uma cor só (mapa-múndi, opções, o
 * texto da abertura): o conteúdo cabe nos 320 px do meio, é uma tela em 4:3 */
static int sides_empty(void)
{
    uint32_t c = framebuffer[0][0];
    for (int y = 0; y < SCREEN_H; y++)
        for (int i = 0; i < wide_extra; i++)
            if (framebuffer[y][i] != c || framebuffer[y][screen_width - 1 - i] != c)
                return 0;
    return 1;
}

/* Reinicia o console, como o botão de reset: CPU, vídeo, som e memória */
static void system_reset(void)
{
    memset(work_ram, 0, sizeof(work_ram));
    memset(z80_ram, 0, sizeof(z80_ram));
    vdp_reset();
    sound_reset();
    cpu_reset();
    intro_logos_reset();
}

static void run_frame(void)
{
    update_widescreen_for_scene();
#ifdef PM_DIST
    dist_apply_patches();   /* ajustes do widescreen na ROM em memória (valores da cena) */
#endif
    if (game_in_stage())   /* depuração (PM_DEBUG_SPAWN): colunas visíveis além da câmera */
        debug_check_spawns(map_stream_extra ? 3 : 0, map_stream_extra ? 22 : 19);
    int64_t base = cpu.cycles;
    for (int line = 0; line < LINES_PER_FRAME; line++) {
        vdp_begin_line(line);
        cpu_run(base + (int64_t)(line + 1) * CYCLES_PER_LINE);
        sound_run_line(line);
    }
    sound_end_frame();
    /* telas em 4:3 (cutscenes, modo original, telas com as laterais vazias): moldura
     * desfocada no lugar das faixas */
    int framed = wide_extra && (wide_pillarbox || (!game_in_stage() && sides_empty()));
    if (framed && !settings.black_border)
        border_draw(framebuffer, screen_width, wide_extra);
    else
        border_reset();
}

static uint32_t menu_fb[SCREEN_H][SCREEN_W_MAX];

static int save_bmp_buf(const char *path, const uint32_t *px, int w, int h, int pitch)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    int row = (w * 3 + 3) & ~3;
    uint32_t size = 54 + row * h;
    uint8_t hdr[54] = {'B', 'M'};
    #define P32(o, v) (hdr[o] = (uint8_t)(v), hdr[o + 1] = (uint8_t)((v) >> 8), hdr[o + 2] = (uint8_t)((v) >> 16), hdr[o + 3] = (uint8_t)((v) >> 24))
    P32(2, size); P32(10, 54); P32(14, 40); P32(18, w); P32(22, h);
    hdr[26] = 1; hdr[28] = 24; P32(34, row * h);
    fwrite(hdr, 1, 54, f);
    uint8_t *line = calloc(row, 1);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * pitch + x];
            line[x * 3] = (uint8_t)c;
            line[x * 3 + 1] = (uint8_t)(c >> 8);
            line[x * 3 + 2] = (uint8_t)(c >> 16);
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    return 1;
}

static int save_bmp(const char *path, uint32_t (*fb)[SCREEN_W_MAX])
{
    return save_bmp_buf(path, &fb[0][0], screen_width, SCREEN_H, SCREEN_W_MAX);
}

static uint32_t parse_buttons(const char *s)
{
    uint32_t b = 0;
    for (; *s; s++) {
        switch (*s) {
        case 'U': b |= PAD_UP; break;
        case 'D': b |= PAD_DOWN; break;
        case 'L': b |= PAD_LEFT; break;
        case 'R': b |= PAD_RIGHT; break;
        case 'A': b |= PAD_A; break;
        case 'B': b |= PAD_B; break;
        case 'C': b |= PAD_C; break;
        case 'S': b |= PAD_START; break;
        default: break;
        }
    }
    return b;
}

static void load_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "roteiro não encontrado: %s\n", path);
        exit(1);
    }
    char line[256];
    while (fgets(line, sizeof(line), f) && n_script < MAX_SCRIPT) {
        int frame;
        char btn[128] = "";
        if (line[0] == '#' || sscanf(line, "%d %127[^\n]", &frame, btn) < 1)
            continue;
        script[n_script].frame = frame;
        script[n_script].buttons = parse_buttons(btn);
        n_script++;
    }
    fclose(f);
}

static uint32_t script_buttons(int frame)
{
    uint32_t b = 0;
    for (int i = 0; i < n_script && script[i].frame <= frame; i++)
        b = script[i].buttons;
    return b;
}

static const char *find_rom(void)
{
    /* instalado: a ROM fica ao lado do executável (caminho em UTF-8) */
    static char beside_exe[1100];
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(beside_exe, sizeof(beside_exe), "%sPulseman_Japan.md", base);
        SDL_free(base);
        SDL_RWops *rw = SDL_RWFromFile(beside_exe, "rb");
        if (rw) {
            SDL_RWclose(rw);
            return beside_exe;
        }
    }
    static const char *candidates[] = {
        "Pulseman_Japan.md", "../Pulseman_Japan.md", "../../Pulseman_Japan.md", "../../../Pulseman_Japan.md",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}

/* Depois de fechar o menu, o jogo ignora o teclado até tudo ser solto
 * (senão o Enter que confirmou "Continuar" viraria Start e pausaria o jogo) */
static int input_hold;

static uint32_t keyboard_buttons(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    if (input_hold) {
        int any = 0;
        for (int i = 0; i < SDL_NUM_SCANCODES; i++)
            any |= k[i];
        if (any)
            return 0;
        input_hold = 0;
    }
    if (k[SDL_SCANCODE_UP]) b |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) b |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= PAD_RIGHT;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_A]) b |= PAD_A;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_S]) b |= PAD_B;
    if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_D]) b |= PAD_C;
    if (k[SDL_SCANCODE_RETURN]) b |= PAD_START;
    return b;
}

/* Aplica as configurações do menu à janela, ao vídeo, à CPU e ao som */
static void apply_settings(SDL_Window *win, SDL_Texture *tex)
{
    static int black_border = -1;
    if (settings.widescreen != widescreen_enabled || settings.black_border != black_border)
        set_widescreen(settings.widescreen);   /* a moldura muda a largura no modo original */
    black_border = settings.black_border;
    cpu_overclock = settings.no_slowdown ? 8 : 1;
    sprite_unlimited = settings.no_sprite_limit;
    sound_set_volume(settings.volume * 10);
    SDL_SetTextureScaleMode(tex, settings.filter == FILTER_SMOOTH ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    if (settings.display == 3) {
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        int scale = settings.display + 2;
        SDL_SetWindowFullscreen(win, 0);
        SDL_SetWindowSize(win, screen_width * scale, SCREEN_H * scale);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

/* Grava as configurações; se a pasta do executável não aceitar, passa a usar a pasta de
 * dados do usuário (ini_path muda para lá) */
static int save_settings(char *ini_path, const char *pref_ini)
{
    if (settings_save(ini_path))
        return 1;
    if (!pref_ini[0] || !strcmp(ini_path, pref_ini) || !settings_save(pref_ini)) {
        fprintf(stderr, "não foi possível salvar as configurações em %s\n", ini_path);
        return 0;
    }
    strcpy(ini_path, pref_ini);
    return 1;
}

/* ---------- apresentação com filtro ---------- */

static SDL_Texture *ftex;    /* saída dos filtros feitos na CPU (filters.c) */
static int ftex_w, ftex_h;
static uint32_t *fbuf;

/* Tela de TV curva: a imagem filtrada vai numa malha deformada por filter_curve */
static void draw_curved(SDL_Renderer *ren, int ow, int oh)
{
    enum { N = 24 };
    static SDL_Vertex v[(N + 1) * (N + 1)];
    static int idx[N * N * 6];
    for (int j = 0; j <= N; j++)
        for (int i = 0; i <= N; i++) {
            float x, y;
            filter_curve(-1 + 2.0f * i / N, -1 + 2.0f * j / N, &x, &y);
            SDL_Vertex *p = &v[j * (N + 1) + i];
            p->position.x = (x + 1) / 2 * screen_width;
            p->position.y = (y + 1) / 2 * SCREEN_H;
            p->color.r = p->color.g = p->color.b = p->color.a = 255;
            p->tex_coord.x = (float)i / N * ow / ftex_w;
            p->tex_coord.y = (float)j / N * oh / ftex_h;
        }
    int n = 0;
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            int a = j * (N + 1) + i, b = a + 1, c = a + N + 1, d = c + 1;
            idx[n++] = a; idx[n++] = b; idx[n++] = c;
            idx[n++] = b; idx[n++] = d; idx[n++] = c;
        }
    SDL_RenderGeometry(ren, ftex, v, (N + 1) * (N + 1), idx, n);
}

static void present(SDL_Renderer *ren, SDL_Texture *tex, uint32_t (*fb)[SCREEN_W_MAX])
{
    static int logical_w;
    if (logical_w != screen_width) { /* o jogo pode trocar entre H40 e H32 */
        logical_w = screen_width;
        SDL_RenderSetLogicalSize(ren, screen_width, SCREEN_H);
    }
    int vw, vh, ow, oh, mode = settings.filter;
    SDL_GetRendererOutputSize(ren, &vw, &vh);
    SDL_RenderClear(ren);
    if (filter_output_size(mode, screen_width, SCREEN_H, vw, vh, &ow, &oh)) {
        if (!ftex || ow > ftex_w || oh > ftex_h) {
            if (ftex)
                SDL_DestroyTexture(ftex);
            ftex_w = ow > ftex_w ? ow : ftex_w;
            ftex_h = oh > ftex_h ? oh : ftex_h;
            ftex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ftex_w, ftex_h);
            SDL_SetTextureScaleMode(ftex, SDL_ScaleModeLinear);
            free(fbuf);
            fbuf = malloc(sizeof(uint32_t) * ftex_w * ftex_h);
        }
        filter_run(mode, fb, screen_width, SCREEN_H, fbuf, ow, oh, ow);
        SDL_Rect r = {0, 0, ow, oh};
        SDL_UpdateTexture(ftex, &r, fbuf, ow * 4);
        if (mode == FILTER_TV_CURVED)
            draw_curved(ren, ow, oh);
        else
            SDL_RenderCopy(ren, ftex, &r, NULL);
    } else {
        SDL_Rect src = {0, 0, screen_width, SCREEN_H};
        SDL_UpdateTexture(tex, NULL, fb, sizeof(fb[0]));
        SDL_RenderCopy(ren, tex, &src, NULL);
    }
    SDL_RenderPresent(ren);
}

#ifdef _WIN32
#include <io.h>
#include <windows.h>
/* O executável é um aplicativo de janela (sem console aberto junto pelo atalho). Rodado
 * de um terminal, as mensagens vão para o console dele; com saída redirecionada (testes),
 * os handles herdados já funcionam. */
static void attach_parent_console(void)
{
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err && err != INVALID_HANDLE_VALUE && GetFileType(err) != FILE_TYPE_UNKNOWN)
        return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    attach_parent_console();
#endif
    const char *rom_path = NULL, *input_path = NULL, *wav_path = NULL;
#ifdef PM_DIST   /* tudo roda no interpretador: não há "endereços sem bloco" a registrar */
    const char *missing_path = NULL;
#else
    const char *missing_path = "missing_entries.txt";
#endif
    int headless = 0, max_frames = -1, scale = 3, mute = 0;
    const char *renderer_name = NULL;
    int cli_widescreen = -1, cli_sprite_limit = 0, cli_filter = -1, cli_lang = -1, test_save = 0, test_delete = 0, cli_overclock = 0, cli_scale = 0;
    int menu_shot_frame = -1, reset_at = -1;   /* testes sem janela do menu */
    int test_pause_menu = 0, was_paused = 0, test_continue_at = -1;
    int intro_logos = 0;   /* logos da pasta img/ depois do logo da SEGA (sempre com janela) */
    struct { int frame; const char *path; } ramdumps[16];
    int n_ramdumps = 0;
    int save_at = -1, save_at_slot = 0, load_at = -1, load_at_slot = 0;   /* testes dos slots */
    int slots_shot_frame = -1, slots_shot_load = 0;
    const char *slots_shot_path = NULL;
    const char *menu_shot_path = NULL, *title_shot_path = NULL, *files_shot_path = NULL;
    /* testes dos filtros: --filter-shot QUADRO MODO LARGURA ALTURA arq.bmp (só a parte da
     * CPU, sem janela) e --window-shot QUADRO arq.bmp (a janela de verdade, com a GPU) */
    int fshot_frame = -1, fshot_mode = 0, fshot_w = 0, fshot_h = 0, wshot_frame = -1;
    const char *fshot_path = NULL, *wshot_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) cli_scale = scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) input_path = argv[++i];
        else if (!strcmp(argv[i], "--missing") && i + 1 < argc) missing_path = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--mute")) mute = 1;
        else if (!strcmp(argv[i], "--renderer") && i + 1 < argc) renderer_name = argv[++i];
        else if (!strcmp(argv[i], "--widescreen")) cli_widescreen = 1;
        else if (!strcmp(argv[i], "--original")) cli_widescreen = 0;
        else if (!strcmp(argv[i], "--sprite-limit")) cli_sprite_limit = 1;
        else if (!strcmp(argv[i], "--menu-shot") && i + 2 < argc) {
            menu_shot_frame = atoi(argv[++i]);
            menu_shot_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--reset-at") && i + 1 < argc) reset_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--title-menu-shot") && i + 1 < argc) title_shot_path = argv[++i];
        else if (!strcmp(argv[i], "--files-shot") && i + 1 < argc) files_shot_path = argv[++i];
        else if (!strcmp(argv[i], "--filter-shot") && i + 5 < argc) {
            fshot_frame = atoi(argv[++i]);
            fshot_mode = atoi(argv[++i]);
            fshot_w = atoi(argv[++i]);
            fshot_h = atoi(argv[++i]);
            fshot_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--window-shot") && i + 2 < argc) {
            wshot_frame = atoi(argv[++i]);
            wshot_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--filter") && i + 1 < argc) cli_filter = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) cli_lang = atoi(argv[++i]);   /* 0 pt, 1 en, 2 es */
        else if (!strcmp(argv[i], "--test-save")) test_save = 1;   /* teste: grava o .ini e sai */
        else if (!strcmp(argv[i], "--test-delete") && i + 1 < argc) test_delete = atoi(argv[++i]);   /* teste: apaga o slot e sai */
        else if (!strcmp(argv[i], "--test-pause-menu")) test_pause_menu = 1;
        else if (!strcmp(argv[i], "--intro-logos")) intro_logos = 1;   /* sem janela também */
        else if (!strcmp(argv[i], "--ramdump") && i + 2 < argc && n_ramdumps < 16) {
            ramdumps[n_ramdumps].frame = atoi(argv[++i]);   /* depuração: salva os 64 KB de RAM */
            ramdumps[n_ramdumps].path = argv[++i];
            n_ramdumps++;
        }
        else if (!strcmp(argv[i], "--save-at") && i + 2 < argc) {
            save_at = atoi(argv[++i]);   /* teste: salva no slot depois do quadro */
            save_at_slot = atoi(argv[++i]) - 1;
        }
        else if (!strcmp(argv[i], "--load-at") && i + 2 < argc) {
            load_at = atoi(argv[++i]);   /* teste: carrega o slot depois do quadro */
            load_at_slot = atoi(argv[++i]) - 1;
        }
        else if (!strcmp(argv[i], "--slots-shot") && i + 3 < argc) {
            slots_shot_frame = atoi(argv[++i]);   /* teste: submenu 0 = salvar, 1 = carregar */
            slots_shot_load = atoi(argv[++i]);
            slots_shot_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--overclock") && i + 1 < argc) cli_overclock = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && n_shots < MAX_SHOTS) {
            shots[n_shots].frame = atoi(argv[++i]);
            shots[n_shots].path = argv[++i];
            n_shots++;
        } else if (argv[i][0] != '-') rom_path = argv[i];
        else {
            fprintf(stderr, "opção desconhecida: %s\n", argv[i]);
            return 1;
        }
    }
    if (!rom_path)
        rom_path = find_rom();
    if (!rom_path) {
        fprintf(stderr, "uso: pulseman <rom do Pulseman (Japão)>\n");
        if (!headless)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pulseman",
                "ROM não encontrada.\n\nColoque a ROM do Pulseman (Japão) na pasta do jogo com o nome "
                "Pulseman_Japan.md, ou instale o jogo de novo e escolha a ROM no instalador.", NULL);
        return 1;
    }
    if (!bus_load_rom(rom_path, pm_rom_sha1)) {
        if (!headless)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pulseman",
                "Não foi possível usar a ROM.\n\nO jogo precisa da ROM original do Pulseman (Japão), "
                "sem modificações.", NULL);
        return 1;
    }
    if (input_path)
        load_script(input_path);

    /* Configurações: pulseman.ini ao lado do executável; se essa pasta não aceitar
     * gravação, na pasta de dados do usuário (%APPDATA%\Pulseman\pulseman-recomp). Só com
     * janela (os testes sem janela não dependem dele, salvo --test-save). Opções da linha
     * de comando têm prioridade. */
    char ini_path[1024] = "pulseman.ini", pref_ini[1024] = "";
    if (!headless || test_save || test_delete) {
        char *base = SDL_GetBasePath();
        char *pref = SDL_GetPrefPath("Pulseman", "pulseman-recomp");
        /* jogos salvos (pulseman-slotN.sav) ficam junto do .ini */
        state_set_dirs(base, pref);
        if (base) {
            snprintf(ini_path, sizeof(ini_path), "%spulseman.ini", base);
            SDL_free(base);
        }
        if (pref) {
            snprintf(pref_ini, sizeof(pref_ini), "%spulseman.ini", pref);
            SDL_free(pref);
        }
        if (!settings_load(ini_path) && pref_ini[0] && settings_load(pref_ini))
            snprintf(ini_path, sizeof(ini_path), "%s", pref_ini);
    }
    if (cli_widescreen >= 0)
        settings.widescreen = cli_widescreen;
    if (cli_scale)
        settings.display = cli_scale < 2 ? 0 : cli_scale > 4 ? 2 : cli_scale - 2;
    if (cli_sprite_limit)
        settings.no_sprite_limit = 0;
    if (cli_filter >= 0 && cli_filter < FILTER_COUNT)
        settings.filter = cli_filter;
    if (cli_lang >= 0 && cli_lang < LANG_COUNT)
        settings.language = cli_lang;
    sprite_unlimited = settings.no_sprite_limit;
    cpu_overclock = cli_overclock ? cli_overclock : (settings.no_slowdown ? 8 : 1);

    if (test_delete) {
        StateInfo info;
        int had = state_slot_info(test_delete - 1, &info);
        int ok = state_delete_slot(test_delete - 1) && !state_slot_info(test_delete - 1, &info);
        fprintf(stderr, "slot %d (%s): %s\n", test_delete, had ? "existia" : "vazio", ok ? "apagado" : "NÃO apagado");
        return ok ? 0 : 1;
    }
    if (test_save) {
        int ok = save_settings(ini_path, pref_ini);
        fprintf(stderr, "configurações %s em %s\n", ok ? "salvas" : "NÃO salvas", ini_path);
        return ok ? 0 : 1;
    }

    vdp_reset();
    set_widescreen(settings.widescreen);
    cpu_init(missing_path);
#ifdef PM_DIST
    dist_init();
#endif
#ifndef PM_LOCKSTEP   /* o lockstep compara com o jogo original, sem a abertura estendida */
    if (!headless || intro_logos) {
        char *base = SDL_GetBasePath();
        int n = intro_logos_load(base);
        if (n)
            fprintf(stderr, "abertura: %d logo(s) da pasta img\n", n);
        SDL_free(base);
    }
#endif
    lag_debug = getenv("PM_DEBUG_LAG") != NULL;

    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_Texture *tex = NULL;
    if (!headless) {
        SDL_SetMainReady();
        /* D3D11 (modelo flip) tem vsync em janela bem mais estável que o D3D9 padrão */
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, renderer_name ? renderer_name : "direct3d11");
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        scale = settings.display == 3 ? 3 : settings.display + 2;
        win = SDL_CreateWindow("Pulseman", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               screen_width * scale, SCREEN_H * scale, SDL_WINDOW_RESIZABLE);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        SDL_RenderSetLogicalSize(ren, screen_width, SCREEN_H);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W_MAX, SCREEN_H);
    }
    sound_init(!headless && !mute, wav_path);
    if (!headless) {
        sound_set_frame_rate(60.0);
        apply_settings(win, tex);
        if (cli_overclock)
            cpu_overclock = cli_overclock;
    }

    /* Ritmo: 60 quadros do jogo por segundo, exatos. Num monitor de ~60 Hz com vsync,
     * o intervalo medido é arredondado para 1/60 s, dando um quadro por atualização. */
    const double game_hz = 60.0;
    uint64_t freq = headless ? 1 : SDL_GetPerformanceFrequency();
    uint64_t last = headless ? 0 : SDL_GetPerformanceCounter();
    double acc = 1.0 / game_hz;
    int frame = 0;
    uint64_t start = last, prev_present = last;
    int presents = 0, multi = 0, hitches = 0, start_frame = 0;
    double worst = 0;
    int pacing_debug = getenv("PM_DEBUG_PACING") != NULL;
    int scene_debug = getenv("PM_DEBUG_SCENE") != NULL;

    while (max_frames < 0 || frame < max_frames) {
        int to_run = 1;
        if (!headless) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) {
                    max_frames = frame;
                } else if (ev.type == SDL_KEYDOWN) {
                    SDL_Keycode key = ev.key.keysym.sym;
                    int repeat = ev.key.repeat;
                    if (!menu_is_open()) {
                        if (key == SDLK_ESCAPE && !repeat) {
                            if (game_in_stage() && !game_paused()) {
                                inject_start = 5;   /* pausa o jogo; o menu abre junto com a pausa */
                            } else {
                                menu_open();
                                sound_pause(1);
                            }
                        }
                    } else if (!(repeat && (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_z ||
                                            key == SDLK_DELETE || key == SDLK_BACKSPACE))) {
                        /* segurar Enter/Delete não confirma sozinho (carregar, apagar) */
                        switch (menu_key(key)) {
                        case MENU_CHANGED:
                            apply_settings(win, tex);
                            save_settings(ini_path, pref_ini);
                            break;
                        case MENU_RESET:
                            system_reset();
                            /* fall through */
                        case MENU_CLOSE:
                            sound_pause(0);
                            input_hold = 1;
                            if (game_paused())
                                inject_start = 5;   /* despausa o jogo como se apertasse Start */
                            break;
                        case MENU_SAVE:
                            menu_slot_done(state_save_slot(menu_slot()));
                            break;
                        case MENU_LOAD:
                            if (!state_load_slot(menu_slot())) {
                                menu_slot_done(0);
                                break;
                            }
                            /* volta ao jogo carregado; se ele foi salvo na pausa, despausa */
                            menu_close();
                            sound_pause(0);
                            input_hold = 1;
                            title_menu_request = title_start_held = 0;
                            was_paused = game_paused();
                            inject_start = was_paused ? 5 : 0;
                            break;
                        case MENU_QUIT:
                            max_frames = frame;
                            break;
                        case MENU_NEW_GAME:       /* seleção de jogo: segue com o START do título */
                        case MENU_GAME_OPTIONS:   /* segue para a tela de opções original */
                            sound_pause(0);
                            input_hold = 1;
                            inject_start = 5;
                            break;
                        default:
                            break;
                        }
                    }
                }
            }
            if (menu_is_open() && (max_frames < 0 || frame < max_frames)) {
                /* jogo congelado: só redesenha o menu sobre o último quadro */
                menu_render(menu_fb, framebuffer, screen_width);
                present(ren, tex, menu_fb);
                last = SDL_GetPerformanceCounter();
                acc = 1.0 / game_hz;
                continue;
            }
            uint64_t now = SDL_GetPerformanceCounter();
            double dt = (double)(now - last) / freq;
            last = now;
            if (dt > 1.0 / game_hz - 0.0015 && dt < 1.0 / game_hz + 0.0015)
                dt = 1.0 / game_hz;
            acc += dt;
            if (acc > 0.1) /* depois de uma pausa (janela arrastada etc.), não acelera */
                acc = 1.0 / game_hz;
            to_run = (int)(acc * game_hz + 1e-6);
            acc -= to_run / game_hz;
            if (to_run == 0) {
                SDL_Delay(1);
                continue;
            }
        }

        uint64_t t_emu = headless ? 0 : SDL_GetPerformanceCounter();
        for (int k = 0; k < to_run && (max_frames < 0 || frame < max_frames); k++, frame++) {
            if (inject_start > 0) {
                /* Start virtual: solta por 2 quadros e aperta por 3. O jogo só reage a um
                 * aperto novo, e quando o menu abriu ele ainda via o Start da pausa seguro. */
                pad_state[0] = inject_start <= 3 ? PAD_START : 0;
                inject_start--;
            } else {
                pad_state[0] = title_options_filter(script_buttons(frame) |
                                                    (headless ? 0 : keyboard_buttons()));
            }
            run_frame();
            intro_logos_frame((pad_state[0] & PAD_START) != 0);   /* Start/Enter pula os logos */
            intro_logos_draw(framebuffer, screen_width);
            if (title_menu_request == TITLE_MENU_FILES) {
                title_menu_request = 0;
                menu_open_files();
                if (headless) {
                    /* teste: mostra a seleção de jogo e escolhe "Novo jogo" */
                    fprintf(stderr, "START do título no quadro %d: seleção de jogo aberta\n", frame);
                    if (files_shot_path) {
                        /* PM_FILES_TEST=arquivo,foco,apagar,confirmação escolhe o estado */
                        const char *ft = getenv("PM_FILES_TEST");
                        int fc = 0, ff = 0, fd = 0, fq = 0;
                        if (ft && sscanf(ft, "%d,%d,%d,%d", &fc, &ff, &fd, &fq) >= 1)
                            menu_files_test(fc, ff, fd, fq);
                        for (int r = 0; r < 30; r++)   /* passa a animação de entrada */
                            menu_render(menu_fb, framebuffer, screen_width);
                        save_bmp(files_shot_path, menu_fb);
                    }
                    menu_close();
                    inject_start = 5;
                } else {
                    sound_pause(1);
                }
            } else if (title_menu_request) {
                title_menu_request = 0;
                menu_open_title();
                if (headless) {
                    /* teste: mostra o menu e escolhe "Opções do jogo" */
                    fprintf(stderr, "OPTIONS do título no quadro %d: menu aberto\n", frame);
                    if (title_shot_path) {
                        for (int r = 0; r < 30; r++)   /* passa a animação de entrada */
                            menu_render(menu_fb, framebuffer, screen_width);
                        save_bmp(title_shot_path, menu_fb);
                    }
                    menu_close();
                    inject_start = 5;
                } else {
                    sound_pause(1);
                }
            }
            if (scene_debug) {
                static uint32_t last_scene = 0xFFFFFFFF;
                uint32_t scene = (work_ram[0xF802] << 8) | work_ram[0xF803];
                if (scene != last_scene) {
                    fprintf(stderr, "quadro %d: cena %04X\n", frame, scene);
                    last_scene = scene;
                }
                /* PM_DEBUG_SCENE=quadro,cena: troca de cena nesse quadro (ex.: 340,C) */
                static int goto_frame = -2, goto_scene;
                if (goto_frame == -2) {
                    const char *e = getenv("PM_DEBUG_SCENE");
                    goto_frame = -1;
                    if (strchr(e, ','))
                        sscanf(e, "%d,%x", &goto_frame, &goto_scene);
                }
                if (frame == goto_frame) {
                    work_ram[0xF800] = (uint8_t)(goto_scene >> 8);
                    work_ram[0xF801] = (uint8_t)goto_scene;
                }
            }
            if (frame == menu_shot_frame) {
                menu_open();
                /* PM_MENU_TEST=página,item,confirmação escolhe o que desenhar */
                const char *mt = getenv("PM_MENU_TEST");
                int mp = 0, mi = 0, md = 0;
                if (mt && sscanf(mt, "%d,%d,%d", &mp, &mi, &md) >= 1)
                    menu_page_test(mp, mi, md);
                for (int r = 0; r < 30; r++)   /* passa a animação de entrada */
                    menu_render(menu_fb, framebuffer, screen_width);
                save_bmp(menu_shot_path, menu_fb);
                menu_close();
            }
            if (frame == slots_shot_frame) {
                menu_open_slots(slots_shot_load);
                menu_render(menu_fb, framebuffer, screen_width);
                save_bmp(slots_shot_path, menu_fb);
                menu_close();
            }
            if (frame == reset_at)
                system_reset();
            if (frame == save_at)
                fprintf(stderr, "quadro %d: slot %d %s\n", frame, save_at_slot + 1,
                        state_save_slot(save_at_slot) ? "salvo" : "NÃO salvo");
            if (frame == load_at)
                fprintf(stderr, "quadro %d: slot %d %s\n", frame, load_at_slot + 1,
                        state_load_slot(load_at_slot) ? "carregado" : "NÃO carregado");
            for (int i = 0; i < n_ramdumps; i++)
                if (ramdumps[i].frame == frame) {
                    FILE *rf = fopen(ramdumps[i].path, "wb");
                    if (rf) {
                        fwrite(work_ram, 1, sizeof(work_ram), rf);
                        fclose(rf);
                    }
                }
            if (frame == fshot_frame) {
                int ow, oh;
                if (!filter_output_size(fshot_mode, screen_width, SCREEN_H, fshot_w, fshot_h, &ow, &oh)) {
                    ow = screen_width;
                    oh = SCREEN_H;
                }
                uint32_t *buf = malloc(sizeof(uint32_t) * ow * oh);
                uint64_t t0 = SDL_GetPerformanceCounter();
                const int reps = 200;
                for (int r = 0; r < reps; r++)
                    filter_run(fshot_mode, framebuffer, screen_width, SCREEN_H, buf, ow, oh, ow);
                double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000 / SDL_GetPerformanceFrequency() / reps;
                fprintf(stderr, "filtro %s: %dx%d em %.2f ms\n", filter_names[fshot_mode], ow, oh, ms);
                save_bmp_buf(fshot_path, buf, ow, oh, ow);
                free(buf);
            }
            for (int i = 0; i < n_shots; i++)
                if (shots[i].frame == frame && !save_bmp(shots[i].path, framebuffer))
                    fprintf(stderr, "falha ao salvar %s\n", shots[i].path);

            /* o jogo acabou de pausar (Start na fase): abre o menu do PC por cima */
            int paused_now = game_paused();
            if (paused_now && !was_paused && !menu_is_open() && (!headless || test_pause_menu)) {
                menu_open();
                if (headless) {
                    fprintf(stderr, "pausa do jogo no quadro %d: menu aberto\n", frame);
                    menu_close();       /* teste: simula "Continuar" meio segundo depois */
                    test_continue_at = frame + 1;   /* pior caso: Start da pausa ainda seguro */
                } else {
                    sound_pause(1);
                }
            }
            if (frame == test_continue_at) {
                fprintf(stderr, "teste: Continuar no quadro %d\n", frame);
                inject_start = 5;
            }
            if (test_pause_menu && was_paused && !paused_now)
                fprintf(stderr, "jogo despausado no quadro %d\n", frame);
            was_paused = paused_now;
            if (menu_is_open()) {   /* para de emular até o menu fechar */
                frame++;
                break;
            }
        }

        if (!headless) {
            uint64_t t_pres = SDL_GetPerformanceCounter();
            present(ren, tex, framebuffer);
            if (wshot_path && frame >= wshot_frame) {   /* teste: lê o que foi apresentado */
                int vw, vh;
                SDL_GetRendererOutputSize(ren, &vw, &vh);
                uint32_t *buf = malloc(sizeof(uint32_t) * vw * vh);
                SDL_RenderSetLogicalSize(ren, 0, 0);
                if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, buf, vw * 4) == 0)
                    save_bmp_buf(wshot_path, buf, vw, vh, vw);
                else
                    fprintf(stderr, "window-shot: %s\n", SDL_GetError());
                SDL_RenderSetLogicalSize(ren, screen_width, SCREEN_H);
                free(buf);
                wshot_path = NULL;
            }
            presents++;
            if (pacing_debug) {
                uint64_t t_end = SDL_GetPerformanceCounter();
                double emu_ms = (double)(t_pres - t_emu) * 1000 / freq;
                double pres_ms = (double)(t_end - t_pres) * 1000 / freq;
                double gap_ms = (double)(t_emu - prev_present) * 1000 / freq;
                if (presents == 1) {
                    SDL_RendererInfo info;
                    SDL_GetRendererInfo(ren, &info);
                    fprintf(stderr, "renderizador: %s\n", info.name);
                }
                if (emu_ms + pres_ms + gap_ms > 25)
                    fprintf(stderr, "travada no quadro %d: antes %.1f ms, emulação %.1f ms, apresentação %.1f ms\n",
                            frame, gap_ms, emu_ms, pres_ms);
            }
            /* estatística de ritmo a partir de 1 s (ignora a abertura da janela/áudio) */
            if (presents == 60) {
                start = SDL_GetPerformanceCounter();
                start_frame = frame;
            } else if (presents > 60) {
                if (to_run > 1)
                    multi++;
                uint64_t t = SDL_GetPerformanceCounter();
                double d = (double)(t - prev_present) / freq;
                if (d > 1.5 / game_hz)
                    hitches++;
                if (d > worst)
                    worst = d;
            }
            prev_present = SDL_GetPerformanceCounter();
        }
    }
    if (!headless && presents > 60) {
        double secs = (double)(SDL_GetPerformanceCounter() - start) / freq;
        fprintf(stderr, "ritmo (após 1 s): %.2f fps em %.1f s; %d atualizações com quadro duplo, "
                        "%d intervalos > 25 ms, pior %.1f ms\n",
                (frame - start_frame) / secs, secs, multi, hitches, worst * 1000);
    }

    fprintf(stderr, "instruções interpretadas (sem bloco recompilado): %d\n", cpu_interp_insns);
    if (lag_debug)
        fprintf(stderr, "quadros perdidos pelo jogo: %d de %d VBlanks\n", lag_frames, vblank_count);
#ifdef PM_LOCKSTEP
    lockstep_report();
#endif
    sound_shutdown();
    if (!headless) {
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
    }
    return 0;
}
