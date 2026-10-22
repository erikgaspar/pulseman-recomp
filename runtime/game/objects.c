/* Desenho de objetos (rotina de sprites de objetos em ~$C400).
 *
 * Cada objeto aponta (a1) para uma lista de pedaços de sprite. Pedaços com X de tela
 * fora de -63..319 são descartados ($C4BC: subi #$41 / cmpi #$17F); no widescreen o
 * limite direito cresce wide_sprite_extra px (patch IMM em patches.txt).
 *
 * Objetos com o bit 0 do byte 2 ligado ficam presos à tela (sem subtrair a câmera):
 * o medidor de energia do HUD, mas também chefes e seus tiros (ver is_hud_object).
 */
#include <stdio.h>
#include <stdlib.h>

#include "ops.h"
#include "runtime.h"
#include "game.h"

int wide_sprite_extra = 0;

/* Objetos do HUD (tipo = primeira palavra & $7FFC, índice da tabela de rotinas em $4ECA6):
 * $08 é o medidor de energia ($214D6, guardado em $FFFA4E) e $3C a peça presa a ele
 * ($DDBC). Outros objetos presos à tela, como chefes e os tiros deles ($21EB4..$220AC),
 * ficam onde estão: estão alinhados com a área original e a colisão usa essa posição. */
static int is_hud_object(uint32_t type)
{
    return type == 0x08 || type == 0x3C;
}

/* Antes de $C43E: d3 = X do objeto (com 128 somado), a0 = objeto. */
void hook_object_screen_x(void)
{
    static int debug = -1;
    if (debug < 0)
        debug = getenv("PM_DEBUG_FIXED_OBJ") != NULL;
    if (!hud_shift && !debug)
        return;
    /* leitura direta (sem passar pelo registro do lockstep): o original não faz esta leitura */
    if (!(bus_read8_raw((A(0) + 2) & 0xFFFFFF) & 1))
        return;                        /* objeto do mundo: a câmera é subtraída depois */
    uint32_t type = bus_read16_raw(A(0) & 0xFFFFFF) & 0x7FFC;
    int hud = is_hud_object(type);
    if (hud_shift && hud) {
        /* medidor de energia: afasta para a borda do seu lado */
        uint32_t x = D(3) & 0xFFFF;
        SETD16(3, x < 0x80 + 160 ? x - hud_shift : x + hud_shift_right);
    }
    if (debug) {
        static uint32_t seen[64];
        static int n;
        uint32_t key = A(0) ^ (bus_read32_raw((A(0) + 4) & 0xFFFFFF) << 8);
        for (int i = 0; i < n; i++)
            if (seen[i] == key)
                return;
        if (n < 64)
            seen[n++] = key;
        fprintf(stderr, "objeto fixo na tela (tipo %03X%s) quadro %d: a0=%06X x=%d y=%d bytes:", type,
                hud ? ", HUD" : "", vblank_count, A(0) & 0xFFFFFF,
                (int)(D(3) & 0xFFFF) - 128, (int)(D(2) & 0xFFFF) - 128);
        for (int i = 0; i < 16; i++)
            fprintf(stderr, " %02X", bus_read8_raw((A(0) + i) & 0xFFFFFF));
        fprintf(stderr, "\n");
    }
}

/* Depuração (PM_DEBUG_SPAWN): em $24B58 o objeto da lista de ativação acabou de ser criado.
 * Mostra a coluna dele em relação à câmera; a tela vai de 0 a 20 metatiles (26,5 no
 * widescreen, a partir de -3,25). */
void hook_debug_spawn(void)
{
    static int debug = -1;
    if (debug < 0)
        debug = getenv("PM_DEBUG_SPAWN") != NULL;
    if (!debug)
        return;
    uint32_t e = A(0) + (int16_t)D(1);
    int col = (int16_t)bus_read16_raw(e & 0xFFFFFF);
    int row = (int16_t)bus_read16_raw((e + 2) & 0xFFFFFF);
    int cam_col = (int16_t)bus_read16_raw(0xFFCB56) / 2;
    int cam_row = (int16_t)bus_read16_raw(0xFFCB58);
    fprintf(stderr, "ativa quadro %d: objeto em coluna %+d linha %+d (relativo à câmera; "
            "absoluto %d,%d tipo %04X bit %d)\n", vblank_count, col - cam_col, row - cam_row,
            col, row, bus_read16_raw((e + 4) & 0xFFFFFF), bus_read16_raw((e + 6) & 0xFFFFFF));
}

/* Depuração (PM_DEBUG_SPAWN): a cada quadro de fase, procura na lista de ativação
 * ($FFCAE8, ordenada por coluna; 8 bytes: coluna, linha, tipo, bit em $FFCAF8) objetos
 * dentro da área visível que ainda não foram ativados, ou seja, que vão "surgir" na tela. */
void debug_check_spawns(int left_cols, int right_cols)
{
    static int debug = -1;
    if (debug < 0)
        debug = getenv("PM_DEBUG_SPAWN") != NULL;
    if (!debug)
        return;
    uint32_t list = bus_read32_raw(0xFFCAE8) & 0xFFFFFF;
    int last = (int16_t)bus_read16_raw(0xFFCAF2);
    if (list < 0xFF0000 || last <= 0 || last > 0x800)
        return;
    int cam_col = (int16_t)bus_read16_raw(0xFFCB56) / 2;
    int cam_row = (int16_t)bus_read16_raw(0xFFCB58);
    static uint8_t reported[256];
    for (int i = 8; i <= last; i += 8) {
        int col = (int16_t)bus_read16_raw(list + i) - cam_col;
        int row = (bus_read16_raw(list + i + 2) & 0xFFF) - cam_row;
        int bit = bus_read16_raw(list + i + 6) & 0xFFFF;
        if (col < -left_cols || col > right_cols || row < 1 || row > 12 || bit >= 2048)
            continue;
        if (bus_read8_raw(0xFFCAF8 + (bit >> 3)) & (1 << (bit & 7)))
            continue;
        if (reported[bit >> 3] & (1 << (bit & 7)))
            continue;
        reported[bit >> 3] |= 1 << (bit & 7);
        fprintf(stderr, "visível sem ativar quadro %d: coluna %+d linha %+d (absoluto %d,%d tipo %04X bit %d)\n",
                vblank_count, col, row, col + cam_col, row + cam_row, bus_read16_raw(list + i + 4), bit);
    }
}

/* Depuração: em $24552 o jogo vai carregar o mapa $FFFD4A (tabela em $100000, $1C bytes
 * por mapa). PM_DEBUG_MAP=1 mostra cada mapa carregado; PM_DEBUG_MAP=N (N > 1) troca o
 * primeiro mapa de fase carregado por N, para chegar direto a uma área (ex.: um chefe). */
void hook_debug_map(void)
{
    static int map = -1, used;
    if (map < 0) {
        const char *e = getenv("PM_DEBUG_MAP");
        map = e ? atoi(e) : 0;
    }
    if (!map)
        return;
    if (map > 1 && !used) {
        used = 1;
        bus_write16_raw(0xFFFD4A, map);
    }
    fprintf(stderr, "mapa %d carregado no quadro %d (fase %d)\n", (int)bus_read16_raw(0xFFFD4A),
            vblank_count, bus_read8_raw(0xFFF9AA));
}


/* Widescreen: ativação no início do mapa ($245CC). A câmera anda $1E colunas para a direita
 * e volta para a esquerda; um contador (-2(a6), bytes do mapa) desce 2 por passo e o jogo só
 * chama $249FC quando ele coincide com a câmera ($FFCB56). $249FC ativa só a coluna exata da
 * borda esquerda (câmera - 2 metatiles; no widescreen - 2 - map_stream_extra) e pula as
 * entradas à direita dela. Se a câmera travou no limite direito ou deu a volta (mapa de uma
 * tela só: salas e arenas de chefe), o contador passa por passos sem coincidir e a primeira
 * borda testada fica map_stream_extra colunas à esquerda da do original: os objetos dessas
 * colunas nunca eram criados (o chefe da fase 1 com o óculos no laboratório, mapa 8, coluna
 * 16, e o jogo travava esperando por ele). Nesse primeiro encontro a borda passa a ativar
 * também as colunas até a borda do original (sem passar da primeira borda do original sem
 * travar). Sem widescreen não faz nada. */
static int sweep_waiting;      /* esperando o primeiro encontro contador == câmera */
static int sweep_first_counter, sweep_cap;
static int sweep_limit = -1;   /* >= 0: coluna mais à direita a ativar neste encontro */

void hook_sweep_begin(void)   /* $245DE: d0 = coluna inicial + $1E + extra - 1 */
{
    if (!map_stream_extra)
        return;
    int col = (int16_t)D(0);
    sweep_waiting = 1;
    sweep_first_counter = 2 * col;
    sweep_cap = col - map_stream_extra - 2;   /* primeira borda do original */
    sweep_limit = -1;
}

void hook_sweep_match(void)   /* $2463C: contador == câmera, antes de chamar $249FC */
{
    if (!sweep_waiting)
        return;
    sweep_waiting = 0;
    int counter = (int16_t)bus_read16_raw((A(6) - 2) & 0xFFFFFF);
    if (counter == sweep_first_counter)
        return;   /* a câmera andou livre: a varredura já cobre tudo */
    int orig = ((int16_t)bus_read16_raw(0xFFCB56) - 4) >> 1;
    if (orig < 0)
        orig = 0;
    sweep_limit = orig < sweep_cap ? orig : sweep_cap;
}

void hook_sweep_left_edge(void)   /* $24A6C: cmp.w 0(a0,d1.w),d0 (d0 = borda esquerda) */
{
    if (sweep_limit < 0)
        return;
    int edge = ((int16_t)bus_read16_raw(0xFFCB56) - 4 - 2 * map_stream_extra) >> 1;
    if (edge < 0)
        edge = 0;
    int col = (int16_t)bus_read16_raw((A(0) + (int16_t)D(1)) & 0xFFFFFF);
    if (col > edge && col <= sweep_limit)
        edge = col;   /* cmp dá igual: o jogo ativa a entrada e segue para a próxima */
    D(0) = (D(0) & 0xFFFF0000u) | (uint16_t)edge;
}

void hook_sweep_match_end(void)   /* $24640: depois de $249FC */
{
    sweep_limit = -1;
}

void hook_sweep_end(void)   /* $24664: fim da varredura horizontal */
{
    sweep_waiting = 0;
    sweep_limit = -1;
}
