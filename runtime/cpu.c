/* Despachante de blocos recompilados, exceções/interrupções e fallback para o Musashi. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ops.h"
#include "runtime.h"
#include "m68k.h"

typedef struct { uint32_t addr; BlockFn fn; } BlockEntry;
extern const BlockEntry pm_blocks[];
extern const int pm_block_count;
extern const uint32_t pm_overrides[];   /* rotinas reescritas em C, terminado em 0 */
static uint8_t *is_override;

M68kState cpu;
uint32_t cpu_pc;

#define TBL_SIZE (0x400000 / 2)
static BlockFn *block_tbl;

/* Endereços sem bloco recompilado executados pelo interpretador */
static uint8_t *missing_seen;
static FILE *missing_log;
static const char *missing_log_path;   /* aberto só no primeiro endereço (sem arquivo vazio) */
int cpu_interp_insns;

/* Quadros perdidos: VBlank chegou antes do jogo marcar $FFFB68 bit 0
 * (rotina de espera em $1F6E). Medido para o modo 60 fps. */
#define LAG_FLAG_ADDR 0xFB68
int lag_frames, vblank_count;
int lag_debug;
int cpu_overclock = 1;
int64_t overclock_remainder;   /* ciclos que sobraram da divisão pelo overclock */

uint32_t cpu_get_sr(void)
{
    return (cpu.t << 15) | (cpu.s << 13) | (cpu.ipm << 8) | get_ccr();
}

void cpu_set_sr(uint32_t sr)
{
    uint32_t s = (sr >> 13) & 1;
    if (s != cpu.s) {
        uint32_t tmp = cpu.a[7];
        cpu.a[7] = cpu.other_sp;
        cpu.other_sp = tmp;
        cpu.s = s;
    }
    cpu.t = (sr >> 15) & 1;
    cpu.ipm = (sr >> 8) & 7;
    set_ccr(sr);
}

uint32_t cpu_exception(int vector, uint32_t return_pc)
{
    uint32_t sr = cpu_get_sr();
    cpu_set_sr((sr & ~0x8000u) | 0x2000);
    push32(return_pc);
    push16(sr);
    cpu.cycles += 34;
    return rd32(vector * 4);
}

static uint32_t cpu_interrupt(int level, uint32_t pc)
{
    uint32_t sr = cpu_get_sr();
    cpu_set_sr((sr & ~0x8700u) | 0x2000 | (level << 8));
    push32(pc);
    push16(sr);
    cpu.cycles += 44;
    cpu.stopped = 0;
    return rd32((24 + level) * 4);
}

uint32_t cpu_rte(void)
{
    uint32_t sr = pop16();
    uint32_t pc = pop32();
    cpu_set_sr(sr);
    return pc;
}

uint32_t cpu_stop(uint32_t sr, uint32_t next_pc)
{
    cpu_set_sr(sr);
    cpu.stopped = 1;
    return next_pc;
}

uint32_t cpu_unimplemented(uint32_t pc)
{
    fprintf(stderr, "instrução não implementada em %06X\n", pc);
    exit(1);
}

static BlockFn lookup(uint32_t pc)
{
    pc &= 0xFFFFFF;
    if (pc < 0x400000 && !(pc & 1))
        return block_tbl[pc >> 1];
    return NULL;
}

/* ---------- Musashi: sincronização de estado ---------- */

void musashi_load_state(void)
{
    m68k_set_reg(M68K_REG_SR, cpu_get_sr());
    for (int i = 0; i < 8; i++) {
        m68k_set_reg(M68K_REG_D0 + i, cpu.d[i]);
        m68k_set_reg(M68K_REG_A0 + i, cpu.a[i]);
    }
    if (cpu.s) {
        m68k_set_reg(M68K_REG_ISP, cpu.a[7]);
        m68k_set_reg(M68K_REG_USP, cpu.other_sp);
    } else {
        m68k_set_reg(M68K_REG_USP, cpu.a[7]);
        m68k_set_reg(M68K_REG_ISP, cpu.other_sp);
    }
    m68k_set_reg(M68K_REG_PC, cpu_pc);
}

void musashi_store_state(void)
{
    for (int i = 0; i < 8; i++) {
        cpu.d[i] = m68k_get_reg(NULL, M68K_REG_D0 + i);
        cpu.a[i] = m68k_get_reg(NULL, M68K_REG_A0 + i);
    }
    uint32_t sr = m68k_get_reg(NULL, M68K_REG_SR);
    cpu.s = (sr >> 13) & 1;
    cpu.t = (sr >> 15) & 1;
    cpu.ipm = (sr >> 8) & 7;
    set_ccr(sr);
    cpu.other_sp = m68k_get_reg(NULL, cpu.s ? M68K_REG_USP : M68K_REG_ISP);
    cpu_pc = m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF;
}

static void note_missing(uint32_t pc)
{
    if (pc >= 0x400000 || missing_seen[pc >> 1])
        return;
    missing_seen[pc >> 1] = 1;
    if (!missing_log && missing_log_path) {
        missing_log = fopen(missing_log_path, "a");
        missing_log_path = NULL;
    }
    if (missing_log) {
        fprintf(missing_log, "%06X\n", pc);
        fflush(missing_log);
    }
}

/* Executa no interpretador até cair num endereço com bloco recompilado. */
static void interp_run(int64_t until)
{
    note_missing(cpu_pc);
    musashi_load_state();
    int n = 0;
    do {
        int used = m68k_execute(1);
        cpu.cycles += used;
        cpu_interp_insns++;
        n++;
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF;
        if (lookup(pc))
            break;
        if (pc < 0x400000)
            note_missing(pc);
    } while (cpu.cycles < until && n < 4096);
    musashi_store_state();
}

/* ---------- inicialização e laço principal ---------- */

void cpu_init(const char *missing_path)
{
    block_tbl = calloc(TBL_SIZE, sizeof(BlockFn));
    missing_seen = calloc(TBL_SIZE, 1);
    for (int i = 0; i < pm_block_count; i++)
        block_tbl[pm_blocks[i].addr >> 1] = pm_blocks[i].fn;
    is_override = calloc(TBL_SIZE, 1);
    for (int i = 0; pm_overrides[i]; i++)
        is_override[pm_overrides[i] >> 1] = 1;
    missing_log_path = missing_path;

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    cpu_reset();
}

/* Reset do 68000: lê pilha e PC dos vetores. O contador de ciclos continua, porque a
 * linha do tempo do som depende dele. */
void cpu_reset(void)
{
    m68k_pulse_reset();
    m68k_execute(0); /* consome os ciclos de reset sem executar instruções */

    int64_t cycles = cpu.cycles;
    memset(&cpu, 0, sizeof(cpu));
    cpu.cycles = cycles;
    cpu.s = 1;
    cpu.ipm = 7;
    cpu.a[7] = rd32(0);
    cpu_pc = rd32(4) & 0xFFFFFF;
}

void cpu_run(int64_t until)
{
    while (cpu.cycles < until) {
        int lvl = irq_pending_level();
        if (lvl && (lvl > (int)cpu.ipm || lvl == 7)) {
            irq_acknowledge(lvl);
            if (lvl == 6) {
                vblank_count++;
                if (!(work_ram[LAG_FLAG_ADDR] & 1)) {
                    lag_frames++;
                    if (lag_debug)
                        fprintf(stderr, "lag no quadro %d\n", vblank_count);
                }
            }
            cpu_pc = cpu_interrupt(lvl, cpu_pc) & 0xFFFFFF;
        }
        if (cpu.stopped) {
            cpu.cycles = until;
            break;
        }
        int64_t before = cpu.cycles;
        BlockFn fn = lookup(cpu_pc);
        if (fn) {
#ifdef PM_LOCKSTEP
            cpu_pc = lockstep_block(fn, cpu_pc, is_override[cpu_pc >> 1]) & 0xFFFFFF;
#else
            cpu_pc = fn() & 0xFFFFFF;
#endif
        } else {
            interp_run(until);
        }
        if (cpu_overclock > 1) {
            /* overclock: o trecho custa 1/N dos ciclos; o resto da divisão é acumulado */
            int64_t spent = cpu.cycles - before + overclock_remainder;
            overclock_remainder = spent % cpu_overclock;
            cpu.cycles = before + spent / cpu_overclock;
        }
    }
}
