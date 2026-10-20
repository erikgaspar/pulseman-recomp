/* Executa cada bloco recompilado e depois o mesmo trecho no Musashi,
 * partindo do mesmo estado, e compara registradores, flags, PC e acessos à memória. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"
#include "lockstep.h"
#include "ops.h"
#include "m68k.h"

int lockstep_recording;

typedef struct { uint8_t write, size; uint32_t addr, value; } Access;

#define MAX_LOG 65536
static Access acc[MAX_LOG];
static int n_acc;
static int replay_r, replay_w;   /* posições já consumidas no replay */
static int replay_bad;
static char replay_msg[256];

static uint64_t blocks_checked, blocks_failed;
static int max_failures = 20;

void lockstep_record(int is_write, uint32_t addr, int size, uint32_t value)
{
    if (n_acc < MAX_LOG)
        acc[n_acc++] = (Access){(uint8_t)is_write, (uint8_t)size, addr, value};
}

/* Próximo acesso do tipo pedido no registro, preservando a ordem relativa */
static Access *next_access(int is_write, int *pos)
{
    while (*pos < n_acc && acc[*pos].write != is_write)
        (*pos)++;
    return *pos < n_acc ? &acc[(*pos)++] : NULL;
}

uint32_t lockstep_replay_read(uint32_t addr, int size)
{
    Access *a = next_access(0, &replay_r);
    if (!a || a->addr != addr || a->size != size) {
        if (!replay_bad)
            snprintf(replay_msg, sizeof(replay_msg), "leitura: Musashi leu %06X/%d, recompilado %s %06X/%d",
                     addr, size, a ? "leu" : "não leu", a ? a->addr : 0, a ? a->size : 0);
        replay_bad = 1;
        return bus_read32(addr) >> (32 - size * 8); /* aproximação só para seguir */
    }
    return a->value;
}

void lockstep_replay_write(uint32_t addr, int size, uint32_t value)
{
    Access *a = next_access(1, &replay_w);
    if (!a || a->addr != addr || a->size != size || a->value != value) {
        if (!replay_bad)
            snprintf(replay_msg, sizeof(replay_msg),
                     "escrita: Musashi %06X/%d=%X, recompilado %s %06X/%d=%X",
                     addr, size, value, a ? "" : "(nada)", a ? a->addr : 0, a ? a->size : 0, a ? a->value : 0);
        replay_bad = 1;
    }
}

static void dump_block(uint32_t pc, int n)
{
    char buf[128];
    for (int i = 0; i < n && i < 64; i++) {
        unsigned len = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
        fprintf(stderr, "    %06X  %s\n", pc, buf);
        pc += len;
    }
}

static void diff_reg(const char *name, uint32_t mine, uint32_t ref, int *bad)
{
    if (mine != ref) {
        fprintf(stderr, "    %-4s recompilado=%08X musashi=%08X\n", name, mine, ref);
        *bad = 1;
    }
}

/* routine = 1: fn substitui a rotina inteira (override em C); o Musashi roda até o rts. */
uint32_t lockstep_block(BlockFn fn, uint32_t pc, int routine)
{
    M68kState before = cpu;

    n_acc = 0;
    cpu.icount = 0;
    lockstep_recording = 1;
    uint32_t next = fn() & 0xFFFFFF;
    lockstep_recording = 0;
    M68kState after = cpu;

    /* Mesmo trecho no Musashi */
    cpu = before;
    cpu_pc = pc;
    musashi_load_state();
    musashi_replay = 1;
    replay_r = replay_w = 0;
    replay_bad = 0;
    if (routine) {
        uint32_t ret = bus_read32_raw(before.a[7] & 0xFFFFFF) & 0xFFFFFF;
        uint32_t sp = before.a[7] + 4;
        for (int i = 0; i < 1000000; i++) {
            m68k_execute(1);
            if ((m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF) == ret && m68k_get_reg(NULL, M68K_REG_A7) == sp)
                break;
        }
    } else {
        for (uint32_t i = 0; i < after.icount; i++)
            m68k_execute(1);
    }
    musashi_replay = 0;
    musashi_store_state();
    M68kState ref = cpu;
    uint32_t ref_pc = cpu_pc;

    blocks_checked++;
    int bad = replay_bad;
    if (!bad) {
        /* tudo que o recompilado leu/escreveu foi consumido pelo Musashi? */
        Access *r = next_access(0, &replay_r);
        Access *w = next_access(1, &replay_w);
        if (r || w) {
            snprintf(replay_msg, sizeof(replay_msg), "recompilado fez acessos a mais (%s %06X)",
                     r ? "leitura" : "escrita", r ? r->addr : w->addr);
            bad = 1;
        }
    }
    int regbad = 0;
    if (bad || next != ref_pc || memcmp(after.d, ref.d, sizeof(ref.d)) || memcmp(after.a, ref.a, sizeof(ref.a)) ||
        after.x != ref.x || after.n != ref.n || after.z != ref.z || after.v != ref.v || after.c != ref.c ||
        after.s != ref.s || after.ipm != ref.ipm || after.other_sp != ref.other_sp) {
        blocks_failed++;
        if (blocks_failed <= (uint64_t)max_failures) {
            fprintf(stderr, "\n*** divergência no bloco %06X (%u instruções) ***\n", pc, after.icount);
            if (routine)
                fprintf(stderr, "    (rotina reescrita em C, comparada até o rts)\n");
            else
                dump_block(pc, after.icount);
            if (bad)
                fprintf(stderr, "    %s\n", replay_msg);
            diff_reg("pc", next, ref_pc, &regbad);
            char nm[8];
            for (int i = 0; i < 8; i++) {
                snprintf(nm, sizeof(nm), "d%d", i);
                diff_reg(nm, after.d[i], ref.d[i], &regbad);
            }
            for (int i = 0; i < 8; i++) {
                snprintf(nm, sizeof(nm), "a%d", i);
                diff_reg(nm, after.a[i], ref.a[i], &regbad);
            }
            diff_reg("osp", after.other_sp, ref.other_sp, &regbad);
            diff_reg("ccr", (after.x << 4) | (after.n << 3) | (after.z << 2) | (after.v << 1) | after.c,
                     (ref.x << 4) | (ref.n << 3) | (ref.z << 2) | (ref.v << 1) | ref.c, &regbad);
            diff_reg("s", after.s, ref.s, &regbad);
            diff_reg("ipm", after.ipm, ref.ipm, &regbad);
            fprintf(stderr, "    estado antes: ");
            for (int i = 0; i < 8; i++) fprintf(stderr, "d%d=%08X ", i, before.d[i]);
            fprintf(stderr, "\n                  ");
            for (int i = 0; i < 8; i++) fprintf(stderr, "a%d=%08X ", i, before.a[i]);
            fprintf(stderr, "ccr=%02X\n",
                    (before.x << 4) | (before.n << 3) | (before.z << 2) | (before.v << 1) | before.c);
        }
        if (blocks_failed == (uint64_t)max_failures)
            fprintf(stderr, "(limite de divergências exibidas atingido)\n");
    }
    /* segue com o resultado do recompilado */
    cpu = after;
    return next;
}

void lockstep_report(void)
{
    fprintf(stderr, "lockstep: %llu blocos verificados, %llu divergências\n",
            (unsigned long long)blocks_checked, (unsigned long long)blocks_failed);
}
