/* Tradução dos textos do jogo (ver translate.h e docs/TRADUCAO.md).
 *
 * Cada legenda é um grupo de sprites de 32 px de altura no pé da tela (Y = 176), com a frase
 * desenhada nos tiles. A assinatura de cada sprite (FNV-1a dos bytes dos tiles, ver vdp.c)
 * identifica a legenda; os sprites dela não são desenhados e a frase traduzida vai no mesmo
 * lugar, com a fonte dos menus. PM_DEBUG_CAPTIONS=1 lista os sprites candidatos sempre que
 * mudam, para catalogar novas legendas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "translate.h"
#include "menu.h"

typedef struct {
    const char *id;            /* como em docs/TRADUCAO.md */
    uint32_t hashes[8];        /* assinaturas dos sprites da legenda (0 encerra) */
    const char *text[LANG_COUNT];
} Caption;

static const Caption captions[] = {
    /* história da abertura (depois do terminal; G.G. = Galaxy Gang) */
    {"I1", {0x25732185, 0xC67DC42A, 0x9FBCC6E7, 0xB59004E7, 0xDA1DE398, 0xEA1E203F},
     {"1999... O biólogo matemático Dr. Yoshiyama",
      "1999... Mathematical biologist Dr. Yoshiyama",
      "1999... El biólogo matemático Dr. Yoshiyama"}},
    {"I2", {0x1BB15316, 0x48FEF064, 0xF9ED9AA5, 0x05FEE1F8, 0x147FB073, 0x71DD3AB4, 0xC5FB26E5},
     {"se apaixona por uma garota de vida artificial que ele criou dentro do computador.",
      "falls in love with an artificial life-form girl he created inside the computer.",
      "se enamora de una chica de vida artificial que creó dentro del ordenador."}},
    {"I3", {0x52C343B7, 0x23CFCC1C, 0x00A76F78, 0x8FDE7BAA, 0x161A2562, 0xA5C37DF6, 0xB5491DFD},
     {"Da união dos dois no espaço da memória, nasceu...",
      "Born to the two, united within memory space, was...",
      "De la unión de ambos en el espacio de memoria, nació..."}},
    {"I4", {0x57A0A402, 0x86C23CDE, 0xA525C1BF, 0xA333177B, 0xACB8BD5D, 0x381C225E, 0x027DA0F8},
     {"capaz de ir e vir livremente entre o mundo real e o espaço do computador...",
      "able to travel freely between the real world and computer space...",
      "capaz de ir y venir libremente entre el mundo real y el espacio informático..."}},
    {"I5", {0x1B6D087C, 0xE8DFE745, 0x0F35646D, 0x3EA582E1, 0x374FD73C},
     {"o garoto que controla a eletricidade: \"Pulseman\"!",
      "the boy who controls electricity: \"Pulseman\"!",
      "el chico que controla la electricidad: \"Pulseman\"."}},
    {"I6", {0x2C3D6392, 0xEAB0841C, 0x300F176D, 0x8B4710A7, 0x9A1F5476, 0x51E371E6, 0x598F85EF},
     {"Nessa época... pelo mundo todo, a gangue do misterioso Doc Waruyama,",
      "Meanwhile... all over the world, the mysterious Doc Waruyama's gang,",
      "Mientras tanto... en todo el mundo, la banda del misterioso Doc Waruyama,"}},
    {"I7", {0xC981B69D, 0xE9D8B87F, 0x348D826E, 0x5A9584F8, 0x70710C23, 0xEDCC729B, 0xA3D9266C},
     {"a \"Galaxy Gang\", cometia crime atrás de crime.",
      "the \"Galaxy Gang\", was committing crime after crime.",
      "la \"Galaxy Gang\", cometía un crimen tras otro."}},
    /* noticiário depois da fase 1 */
    {"N1", {0xC2F53D3F, 0xF7004F10, 0xC98D366A, 0x6A249D76, 0x95539F61, 0x235E5FA7, 0x1454BE9D},
     {"Graças ao Pulseman, o plano maligno da G.G. foi frustrado!!",
      "Thanks to Pulseman, G.G.'s evil scheme has been foiled!!",
      "¡¡Gracias a Pulseman, el malvado plan de G.G. fue frustrado!!"}},
    /* noticiário do final */
    {"E1", {0x884BBE6B, 0xE8D2D776, 0xC440C3DE, 0xD93B907F, 0xBA261489, 0x42515813},
     {"A sede da organização criminosa internacional G.G. foi destruída.",
      "The headquarters of the international crime syndicate G.G. has been destroyed.",
      "La sede de la organización criminal internacional G.G. ha sido destruida."}},
    {"E2", {0x7E784581, 0xF4FC7470, 0xAC29EF31, 0x88243DDF, 0xD0C680B6, 0x5AC5401C, 0x6CE5FC46},
     {"Porém... o Pulseman, que foi pego pela explosão junto com Waruyama...",
      "However... Pulseman, who was caught in the explosion along with Waruyama...",
      "Sin embargo... Pulseman, que quedó atrapado en la explosión junto con Waruyama..."}},
    {"E3", {0x2355EC28, 0xF2B18B74, 0xC7680CF4, 0x75E937D9, 0x8CA7D144, 0x81CBB99D, 0xC9930FFD},
     {"Será que ele está bem...?",
      "Is he safe...?",
      "¿Estará bien...?"}},
    {"E4", {0xA75D7516, 0x258F4BAB, 0x1E2DB10B, 0xED974EF5, 0x53810E1E},
     {"É o Pulseman!! Ele está vivo!!",
      "It's Pulseman!! He's alive!!",
      "¡¡Es Pulseman!! ¡¡Está vivo!!"}},
    /* seleção de fases, uma por fase */
    {"S1", {0x68799385, 0x869F1D51, 0x4E46ED34, 0xA1F90949, 0x1949F22B, 0x2C309C4C, 0xCD9C0074},
     {"A G.G. invadiu a emissora de TV! Parece que querem controlar as pessoas pelas ondas!!",
      "G.G. broke into the TV station! Looks like they want to control people over the airwaves!!",
      "¡G.G. entró en la cadena de TV! ¡¡Parece que quieren controlar a la gente con las ondas!!"}},
    {"S2", {0x4960AE52, 0x4651EC14, 0xEC451A74, 0xDF033C2B, 0x04908FA6, 0xB43ADCE9, 0x4926945C},
     {"A floresta tropical está sendo devastada!! Eles querem mecanizar a natureza...",
      "The rainforest is being ravaged!! They mean to mechanize nature...",
      "¡¡Están arrasando la selva tropical!! Quieren mecanizar la naturaleza..."}},
    {"S3", {0xF96D2807, 0xA8DA88E8, 0x38D07392, 0xAF55C0A8, 0xD951B45F, 0x1C690D22, 0x6A0331D4},
     {"O Doc Waruyama apareceu no cassino!! Com certeza ele quer roubar o dinheiro!",
      "Doc Waruyama showed up at the casino!! I bet he's after the money!",
      "¡¡Doc Waruyama apareció en el casino!! ¡Seguro que quiere robar el dinero!"}},
    {"S4", {0x4D8EEBB1, 0xB359852D, 0xF862291E, 0x29017B50, 0xD681F649, 0x90A76A26, 0xB20BDCBA},
     {"Ruínas antigas emergiram no Oceano Índico!! É mais um plano da G.G.?",
      "Ancient ruins have surfaced in the Indian Ocean!! Is it another G.G. plot?",
      "¡¡Unas ruinas antiguas emergieron en el océano Índico!! ¿Otro plan de G.G.?"}},
    {"S5", {0x6CB87547, 0x186FA51D, 0xC11AAAD6, 0x76106C4A, 0x9D728D61, 0xA1576BC2, 0xA4AFC673},
     {"Acharam uma base secreta no Alasca!! ...Estou com um mau pressentimento...",
      "A secret base was found in Alaska!! ...I've got a bad feeling about this...",
      "¡¡Descubrieron una base secreta en Alaska!! ...Tengo un mal presentimiento..."}},
    {"S6", {0xEFC031C2, 0xBC94EBD9, 0xB05D483E, 0x982C477D, 0x76B177A7, 0xADD72D1A, 0x977FEE0B},
     {"A G.G. está construindo um submarino. Agora é no fundo do mar... Pulse, você vai ficar bem?",
      "G.G. is building a submarine. This time it's the seafloor... Will you be OK, Pulse?",
      "G.G. está construyendo un submarino. Ahora es el fondo del mar... ¿Estarás bien, Pulse?"}},
    {"S7", {0xD0BC0D4C, 0xBCAEBBCD, 0xE76062B1, 0x975EDE08, 0xE4622ADE, 0x55C13591, 0xB4E40D35},
     {"Escuta, escuta, Pulseman!! Descobri onde fica a sede da G.G.!!",
      "Listen, listen, Pulseman!! I found out where G.G.'s headquarters is!!",
      "¡¡Escucha, escucha, Pulseman!! ¡¡Descubrí dónde está la sede de G.G.!!"}},
    {NULL, {0}, {NULL}},
};

#define N_CAPTIONS ((int)(sizeof(captions) / sizeof(captions[0])) - 1)

static struct { int on, x0, y0, x1, y1; } found[N_CAPTIONS > 0 ? N_CAPTIONS : 1];

/* Textos em inglês da ROM escritos com a fonte do jogo nos planos (ver vdp.c: scan_texts).
 * O original (com os espaços do começo, como na ROM) é procurado na tabela de tiles como
 * código ASCII + base da fonte, só nas cenas em que aparece; em inglês ficam como estão.
 * center: centraliza a tradução no lugar do original (créditos). */
#define SC(s) (1u << ((s) >> 2))
#define SC_TITLE SC(0x04)
#define SC_STAGE (SC(0x08) | SC(0x1C))
#define SC_OPTIONS SC(0x0C)
#define SC_TERMINAL (SC(0x14) | SC(0x30))   /* abertura e final */

typedef struct {
    const char *orig;
    uint32_t scenes;
    int center;
    const char *pt, *es;
} AsciiText;

static const AsciiText texts[] = {
    /* título */
    {"START", SC_TITLE, 0, "Jogar", "Jugar"},
    {"OPTIONS", SC_TITLE | SC_OPTIONS, 0, "Opções", "Opciones"},
    {"BOSS RUSH", SC_TITLE, 0, "Boss Rush", "Boss Rush"},   /* item do runtime (bossrush.c) */
    /* opções do jogo (VOLTTECCER é o nome do golpe; as músicas ficam com os nomes) */
    {"CONTROL", SC_OPTIONS, 0, "Controle", "Control"},
    {"BGM", SC_OPTIONS, 0, "Música", "Música"},
    {"SE", SC_OPTIONS, 0, "Efeitos", "Efectos"},
    {"VOICE", SC_OPTIONS, 0, "Voz", "Voz"},
    {"EXIT", SC_OPTIONS, 0, "Sair", "Salir"},
    {"A ATTACK", SC_OPTIONS, 0, "A Ataque", "A Ataque"},
    {"B ATTACK", SC_OPTIONS, 0, "B Ataque", "B Ataque"},
    {"C ATTACK", SC_OPTIONS, 0, "C Ataque", "C Ataque"},
    {"A JUMP", SC_OPTIONS, 0, "A Pulo", "A Salto"},
    {"B JUMP", SC_OPTIONS, 0, "B Pulo", "B Salto"},
    {"C JUMP", SC_OPTIONS, 0, "C Pulo", "C Salto"},
    /* terminal da abertura e do final */
    {">LOG ON:", SC_TERMINAL, 0, ">Acesso:", ">Acceso:"},
    {">PASSWORD:", SC_TERMINAL, 0, ">Senha:", ">Contraseña:"},
    {">VERIFYING VOICE PATTERN:", SC_TERMINAL, 0, ">Verificando padrão de voz:", ">Verificando patrón de voz:"},
    {">PATTERN MATCH", SC_TERMINAL, 0, ">Padrão confere", ">Patrón correcto"},
    {" VOICE RECOGNIZED", SC_TERMINAL, 0, "Voz reconhecida", "Voz reconocida"},
    {" PROCEED", SC_TERMINAL, 0, "Prossiga", "Adelante"},
    /* créditos (no final) */
    {" PULSEMAN STAFF", SC_TERMINAL, 0, "Equipe Pulseman", "Equipo Pulseman"},
    {"    STAFF CREDIT", SC_TERMINAL, 1, "Créditos", "Créditos"},
    {"DIRECTION & GAME DESIGN", SC_TERMINAL, 1, "Direção e design do jogo", "Dirección y diseño del juego"},
    {"PROGRAM", SC_TERMINAL, 1, "Programação", "Programación"},
    {"GRAPHICS", SC_TERMINAL, 1, "Gráficos", "Gráficos"},
    {"MUSIC & SOUND EFFECTS", SC_TERMINAL, 1, "Música e efeitos sonoros", "Música y efectos de sonido"},
    {"STORY PLOT", SC_TERMINAL, 1, "Roteiro", "Guion"},
    {"SPECIAL THANKS", SC_TERMINAL, 1, "Agradecimentos", "Agradecimientos"},
    /* fases: fliperama da fase 7 e fase bônus */
    {"PUSH START BUTTON", SC_STAGE, 1, "Aperte Start", "Pulsa Start"},
    {"PERFECT BONUS!", SC_STAGE, 1, "Bônus perfeito!", "¡Bonus perfecto!"},
};

#define N_TEXTS ((int)(sizeof(texts) / sizeof(texts[0])))

static struct { int id, shown, x, y; uint32_t color; } text_found[32];
static int n_text_found;

int translate_text_count(void)
{
    return settings.language == LANG_EN ? 0 : N_TEXTS;
}

const char *translate_text_orig(int i)
{
    uint32_t scene = work_ram[0xF803] & 0x7C;
    return texts[i].scenes & SC(scene) ? texts[i].orig : NULL;
}

void translate_text_found(int i, int shown, int x, int y, uint32_t color)
{
    if (n_text_found >= 32)
        return;
    text_found[n_text_found].id = i;
    text_found[n_text_found].shown = shown;
    text_found[n_text_found].x = x;
    text_found[n_text_found].y = y;
    text_found[n_text_found].color = color;
    n_text_found++;
}

/* candidatos do quadro (depuração) */
static struct { uint32_t hash; int x, y, w, h; } seen[32], last_seen[32];
static int n_seen, n_last_seen;

static int debug_captions(void)
{
    static int d = -1;
    if (d < 0)
        d = getenv("PM_DEBUG_CAPTIONS") != NULL;
    return d;
}

int translate_enabled(void)
{
#ifdef PM_LOCKSTEP
    return 0;
#else
    return 1;
#endif
}

void translate_begin_frame(void)
{
    if (debug_captions()) {
        int same = n_seen == n_last_seen && !memcmp(seen, last_seen, sizeof(seen[0]) * n_seen);
        if (!same && n_seen) {
            fprintf(stderr, "legenda? quadro %d:", vblank_count);
            for (int i = 0; i < n_seen; i++)
                fprintf(stderr, " %d,%d,%dx%d:%08X", seen[i].x, seen[i].y, seen[i].w, seen[i].h,
                        seen[i].hash);
            fprintf(stderr, "\n");
        }
        memcpy(last_seen, seen, sizeof(seen));
        n_last_seen = n_seen;
        n_seen = 0;
    }
    memset(found, 0, sizeof(found));
    n_text_found = 0;
}

int translate_sprite(uint32_t hash, int x, int y, int w, int h)
{
    if (debug_captions() && n_seen < 32) {
        seen[n_seen].hash = hash;
        seen[n_seen].x = x;
        seen[n_seen].y = y;
        seen[n_seen].w = w;
        seen[n_seen].h = h;
        n_seen++;
    }
    for (int c = 0; c < N_CAPTIONS; c++)
        for (int k = 0; k < 8 && captions[c].hashes[k]; k++)
            if (captions[c].hashes[k] == hash) {
                if (!found[c].on) {
                    found[c].on = 1;
                    found[c].x0 = x;
                    found[c].y0 = y;
                    found[c].x1 = x + w;
                    found[c].y1 = y + h;
                } else {
                    if (x < found[c].x0) found[c].x0 = x;
                    if (y < found[c].y0) found[c].y0 = y;
                    if (x + w > found[c].x1) found[c].x1 = x + w;
                    if (y + h > found[c].y1) found[c].y1 = y + h;
                }
                return 1;
            }
    return 0;
}

/* A fonte dos menus só tem maiúsculas: passa o texto (UTF-8) para maiúsculas */
static void to_upper(const char *s, char *out, size_t size)
{
    size_t n = 0;
    for (; *s && n + 2 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') {
            out[n++] = (char)(c - 32);
        } else if (c == 0xC3 && (unsigned char)s[1] >= 0xA0 && (unsigned char)s[1] <= 0xBE) {
            out[n++] = (char)c;
            out[n++] = (char)((unsigned char)s[1] - 0x20);   /* á → Á, ç → Ç, ñ → Ñ */
            s++;
        } else {
            out[n++] = (char)c;
        }
    }
    out[n] = 0;
}

void translate_draw(uint32_t (*fb)[SCREEN_W_MAX], int width, int extra)
{
    for (int c = 0; c < N_CAPTIONS; c++) {
        if (!found[c].on)
            continue;
        const char *t = captions[c].text[settings.language];
        if (!t)
            continue;
        char up[512];
        to_upper(t, up, sizeof(up));
        /* duas linhas de 12 px centralizadas na altura da legenda original; a largura usa a
         * tela original quase toda (a frase traduzida é mais longa que a japonesa) */
        int cy = (found[c].y0 + found[c].y1) / 2;
        int cx = extra + base_width / 2;
        int max_w = base_width - 40, total = menu_text_width(up);
        int lines = (total + max_w - 1) / max_w;
        if (lines < 1)
            lines = 1;
        /* linhas equilibradas: cada uma perto de total/lines, com folga para as palavras */
        int wrap = total / lines + 6 * 6;
        if (wrap > max_w || lines == 1)
            wrap = max_w;
        menu_draw_caption(fb, width, cx, cy - lines * 6 - 1, wrap, up, 0xFFFFFFFFu);
    }
    for (int i = 0; i < n_text_found; i++) {
        const AsciiText *t = &texts[text_found[i].id];
        const char *tr = settings.language == LANG_ES ? t->es : t->pt;
        char up[256], part[256];
        to_upper(tr, up, sizeof(up));
        /* o terminal digita letra a letra: mostra a mesma fração da tradução */
        const char *o = t->orig;
        while (*o == ' ')
            o++;
        int len = (int)strlen(o), n_chars = 0;
        const char *p = up;
        int total = menu_text_width(up) / 6, want = text_found[i].shown >= len ? total :
                    (text_found[i].shown * total + len - 1) / len;
        while (*p && n_chars < want) {
            p += ((unsigned char)*p == 0xC3 || (unsigned char)*p == 0xC2) ? 2 : 1;
            n_chars++;
        }
        size_t n = (size_t)(p - up);
        memcpy(part, up, n);
        part[n] = 0;
        int x = extra + text_found[i].x;
        if (t->center)   /* centro do original (8 px por letra) */
            x += (len * 8 - menu_text_width(up)) / 2;
        for (int dy = -1; dy <= 1; dy++)   /* contorno escuro, como a fonte do jogo */
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy)
                    menu_draw_text(fb, width, x + dx, text_found[i].y - 1 + dy, part, 0xFF000000u);
        menu_draw_text(fb, width, x, text_found[i].y - 1, part, text_found[i].color);
    }
}
