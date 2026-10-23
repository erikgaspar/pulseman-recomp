/* Controles (ver gamepad.h).
 *
 * Mapeamento no jogo (posições do controle, nomes do SDL): direcional e analógico esquerdo =
 * direções; os três botões seguem a ordem do controle de 3 botões do Mega Drive, como nos
 * emuladores: X (esquerda) = A, A (baixo) = B, B (direita) = C. Com o padrão do jogo (A
 * Voltteccer, B ataque, C pulo), ataque e pulo ficam nos dois botões principais. Y (cima)
 * também vale A. Start = Start; Back (Select/Share/-) e Guide abrem o menu do PC, como o Esc.
 * Teclado e controle funcionam juntos. */
#include <stdio.h>

#include "gamepad.h"
#include "runtime.h"

#define MAX_PADS 8
#define STICK_ON 16000      /* ~50% do curso: direção ligada */
#define STICK_OFF 12000     /* histerese para os menus */

static SDL_GameController *pads[MAX_PADS];

void gamepad_init(const char *base_dir)
{
    if (base_dir) {   /* mapeamentos extras opcionais (formato do SDL_GameControllerDB) */
        char path[1024];
        snprintf(path, sizeof(path), "%sgamecontrollerdb.txt", base_dir);
        SDL_RWops *f = SDL_RWFromFile(path, "rb");
        if (f) {
            int n = SDL_GameControllerAddMappingsFromRW(f, 1);
            if (n > 0)
                fprintf(stderr, "controles: %d mapeamentos de %s\n", n, path);
        }
    }
}

static void open_pad(int device)
{
    if (!SDL_IsGameController(device))
        return;
    for (int i = 0; i < MAX_PADS; i++)
        if (!pads[i]) {
            pads[i] = SDL_GameControllerOpen(device);
            if (pads[i])
                fprintf(stderr, "controle conectado: %s\n", SDL_GameControllerName(pads[i]));
            return;
        }
}

static void close_pad(SDL_JoystickID id)
{
    for (int i = 0; i < MAX_PADS; i++)
        if (pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == id) {
            SDL_GameControllerClose(pads[i]);
            pads[i] = NULL;
        }
}

int gamepad_event(const SDL_Event *ev, int menu_open, SDL_Keycode *key)
{
    switch (ev->type) {
    case SDL_CONTROLLERDEVICEADDED:
        open_pad(ev->cdevice.which);
        return 0;
    case SDL_CONTROLLERDEVICEREMOVED:
        close_pad(ev->cdevice.which);
        return 0;
    case SDL_CONTROLLERBUTTONDOWN:
        switch (ev->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_BACK:
        case SDL_CONTROLLER_BUTTON_GUIDE:
            *key = SDLK_ESCAPE;
            return 1;
        default:
            break;
        }
        if (!menu_open)
            return 0;   /* no jogo os botões chegam por gamepad_buttons */
        switch (ev->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: *key = SDLK_UP; return 1;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: *key = SDLK_DOWN; return 1;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: *key = SDLK_LEFT; return 1;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: *key = SDLK_RIGHT; return 1;
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_START: *key = SDLK_RETURN; return 1;
        case SDL_CONTROLLER_BUTTON_B: *key = SDLK_ESCAPE; return 1;
        default: return 0;
        }
    case SDL_CONTROLLERAXISMOTION: {
        /* analógico esquerdo nos menus: uma tecla a cada vez que passa da metade */
        static int dir_x, dir_y;
        int v = ev->caxis.value;
        int *dir;
        SDL_Keycode neg, pos;
        if (ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            dir = &dir_x; neg = SDLK_LEFT; pos = SDLK_RIGHT;
        } else if (ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            dir = &dir_y; neg = SDLK_UP; pos = SDLK_DOWN;
        } else {
            return 0;
        }
        int now = v >= STICK_ON ? 1 : v <= -STICK_ON ? -1 : (v < STICK_OFF && v > -STICK_OFF) ? 0 : *dir;
        int fire = now != 0 && now != *dir;
        *dir = now;
        if (!menu_open || !fire)
            return 0;
        *key = now > 0 ? pos : neg;
        return 1;
    }
    default:
        return 0;
    }
}

uint32_t gamepad_buttons(void)
{
    uint32_t b = 0;
    for (int i = 0; i < MAX_PADS; i++) {
        SDL_GameController *c = pads[i];
        if (!c)
            continue;
        int lx = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly <= -STICK_ON) b |= PAD_UP;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly >= STICK_ON) b |= PAD_DOWN;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx <= -STICK_ON) b |= PAD_LEFT;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx >= STICK_ON) b |= PAD_RIGHT;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X) ||
            SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y)) b |= PAD_A;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A)) b |= PAD_B;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B)) b |= PAD_C;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START)) b |= PAD_START;
    }
    return b;
}

/* Teste (PM_GAMEPAD_TEST=1, com janela): conecta um controle virtual do SDL e aperta botões
 * em tempos fixos desde o início (o menu do PC congela os quadros do jogo, por isso o tempo
 * é o do relógio; com os logos da pasta img o título aparece aos ~15 s): baixo/cima e Start
 * no título, baixo/cima e A na seleção de jogo, Start na seleção de fases, Back (menu do PC)
 * e B para fechar. Cada aperto aparece no stderr. A janela do teste pode abrir sem foco, e o
 * SDL ignora controles fora de foco: o teste liga SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS. */
static int test_joy = -1;

void gamepad_test_tick(void)
{
    static int init = -1;
    static SDL_Joystick *joy;
    if (init < 0) {
        init = SDL_getenv("PM_GAMEPAD_TEST") != NULL;
        if (init) {
            SDL_VirtualJoystickDesc desc;
            SDL_zero(desc);
            desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            desc.naxes = SDL_CONTROLLER_AXIS_MAX;
            desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;   /* senão o mapeamento */
            desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;       /* sai sem botões */
            desc.name = "Controle virtual (teste)";
            test_joy = SDL_JoystickAttachVirtualEx(&desc);
            joy = test_joy >= 0 ? SDL_JoystickOpen(test_joy) : NULL;
            fprintf(stderr, "teste de controle: virtual %s\n", joy ? "conectado" : SDL_GetError());
        }
    }
    if (!init || !joy)
        return;
    static const struct { Uint32 ms; int button; const char *name; } steps[] = {
        {4000, SDL_CONTROLLER_BUTTON_A, "A (logos: não faz nada)"},
        {17000, SDL_CONTROLLER_BUTTON_DPAD_DOWN, "baixo (título: OPTIONS)"},
        {18000, SDL_CONTROLLER_BUTTON_DPAD_UP, "cima (título: START)"},
        {19000, SDL_CONTROLLER_BUTTON_START, "Start (título: seleção de jogo)"},
        {20500, SDL_CONTROLLER_BUTTON_DPAD_DOWN, "baixo (seleção de jogo)"},
        {21200, SDL_CONTROLLER_BUTTON_DPAD_UP, "cima (seleção de jogo)"},
        {22000, SDL_CONTROLLER_BUTTON_A, "A (novo jogo)"},
        {36000, SDL_CONTROLLER_BUTTON_START, "Start (seleção de fases)"},
        {44000, SDL_CONTROLLER_BUTTON_BACK, "Back (menu do PC)"},
        {46000, SDL_CONTROLLER_BUTTON_B, "B (fecha o menu)"},
    };
    Uint32 now = SDL_GetTicks();
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        int down = now >= steps[i].ms && now < steps[i].ms + 150;
        static int was[16];
        if (down != was[i]) {
            SDL_JoystickSetVirtualButton(joy, steps[i].button, (Uint8)down);
            if (down)
                fprintf(stderr, "teste de controle: %u ms: %s\n", (unsigned)now, steps[i].name);
            was[i] = down;
        }
    }
}
