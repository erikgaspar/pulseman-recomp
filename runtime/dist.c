/* Versão distribuível (PM_DIST): nenhum código gerado a partir da ROM no executável.
 *
 * O código do jogo roda no Musashi direto da ROM do jogador (o mesmo interpretador que o
 * lockstep usa como referência). As nossas rotinas em C (overrides.txt) continuam no
 * despachante; os ajustes de patches.txt, que na versão recompilada mudam o C gerado,
 * aqui são feitos na ROM em memória (IMM) e por um gancho antes da instrução (HOOK). */
#include <stdio.h>
#include <string.h>

#include "runtime.h"
#include "dist.h"
#include "m68k.h"

enum { HK_CALL, HK_QUICK };

typedef struct {
    uint32_t addr;
    int kind;
    void (*fn)(void);         /* HK_CALL */
    const DistImm *imm;       /* HK_QUICK: addq/subq com a constante nos bits da instrução */
    int reg, sub;
} Hook;

typedef struct { uint32_t at; const DistImm *imm; } WordPatch;

static Hook hooks[32];
static int n_hooks;
static uint8_t hook_map[0x400000 / 16];   /* 1 bit por palavra da ROM: há gancho nela */
static WordPatch words[64];
static int n_words;

static uint32_t rom16(uint32_t a)
{
    return a + 1 < rom_size ? (uint32_t)rom_data[a] << 8 | rom_data[a + 1] : 0;
}

static void add_hook(Hook h)
{
    if (n_hooks >= (int)(sizeof(hooks) / sizeof(hooks[0])))
        return;
    if (h.addr >= 0x400000)
        return;
    hooks[n_hooks++] = h;
    hook_map[h.addr >> 4] |= 1 << ((h.addr >> 1) & 7);
}

/* Antes de cada instrução interpretada */
static void instr_hook(unsigned int pc)
{
    pc &= 0xFFFFFF;
    if (pc >= 0x400000 || !(hook_map[pc >> 4] & 1 << ((pc >> 1) & 7)))
        return;
    for (int i = 0; i < n_hooks; i++) {
        const Hook *h = &hooks[i];
        if (h->addr != pc)
            continue;
        if (h->kind == HK_CALL) {
            /* as rotinas usam o estado em 'cpu' (D(), A()): sincroniza nos dois sentidos */
            musashi_store_state();
            h->fn();
            musashi_load_state();
        } else {
            /* subq/addq #n,Dn só cabem 1..8: o resto da diferença vai no registrador antes
             * da instrução (as flags dela são refeitas pela instrução seguinte, ver dist_init) */
            int delta = h->imm->value() - (int)h->imm->orig;
            if (delta) {
                uint32_t d = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + h->reg));
                uint32_t w = (d + (h->sub ? -delta : delta)) & 0xFFFF;
                m68k_set_reg((m68k_register_t)(M68K_REG_D0 + h->reg), (d & 0xFFFF0000u) | w);
            }
        }
    }
}

int dist_init(void)
{
    int ok = 1;
    for (const DistHook *h = dist_hooks; h->fn; h++) {
        Hook k = {h->addr, HK_CALL, h->fn, NULL, 0, 0};
        add_hook(k);
    }
    for (const DistImm *p = dist_imms; p->value; p++) {
        uint32_t op = rom16(p->addr);
        if (rom16(p->addr + 2) == (p->orig & 0xFFFF)) {   /* constante de 16 bits logo após o código */
            words[n_words].at = p->addr + 2;
            words[n_words].imm = p;
            n_words++;
            continue;
        }
        int quick = (op & 0xF000) == 0x5000 && ((op >> 6) & 3) == 1 && ((op >> 3) & 7) == 0;
        int data = (op >> 9) & 7 ? (op >> 9) & 7 : 8;
        uint32_t next = rom16(p->addr + 2);
        if (quick && (uint32_t)data == p->orig && (next & 0xF000) == 0xE000) {   /* seguida de shift */
            Hook k = {p->addr, HK_QUICK, NULL, p, (int)(op & 7), (op & 0x100) != 0};
            add_hook(k);
            continue;
        }
        fprintf(stderr, "dist: ajuste em %06X não confere com a ROM (ignorado)\n", p->addr);
        ok = 0;
    }
    m68k_set_instr_hook_callback(instr_hook);
    return ok;
}

void dist_apply_patches(void)
{
    for (int i = 0; i < n_words; i++) {
        uint32_t v = (uint32_t)words[i].imm->value() & 0xFFFF;
        rom_data[words[i].at] = (uint8_t)(v >> 8);
        rom_data[words[i].at + 1] = (uint8_t)v;
    }
}
