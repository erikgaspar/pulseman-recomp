/* Estado da CPU 68000 compartilhado entre o código recompilado e o runtime. */
#ifndef PM_CPU_H
#define PM_CPU_H

#include <stdint.h>

typedef struct {
    uint32_t d[8];
    uint32_t a[8];      /* a[7] é a pilha ativa (SSP ou USP conforme o bit S) */
    uint32_t other_sp;  /* a pilha inativa */
    uint32_t x, n, z, v, c;  /* flags, sempre 0 ou 1 */
    uint32_t s;         /* bit de supervisor */
    uint32_t t;         /* bit de trace (não emulado, só preservado) */
    uint32_t ipm;       /* máscara de interrupção 0..7 */
    int64_t  cycles;    /* ciclos de 68000 executados desde o boot */
    uint32_t stopped;   /* executou STOP, aguardando interrupção */
    uint32_t icount;    /* instruções executadas (usado no modo lockstep) */
} M68kState;

extern M68kState cpu;

typedef uint32_t (*BlockFn)(void);

/* Barramento (bus.c) */
uint32_t bus_read8(uint32_t addr);
uint32_t bus_read16(uint32_t addr);
uint32_t bus_read32(uint32_t addr);
void bus_write8(uint32_t addr, uint32_t val);
void bus_write16(uint32_t addr, uint32_t val);
void bus_write32(uint32_t addr, uint32_t val);

/* cpu.c */
uint32_t cpu_get_sr(void);
void cpu_set_sr(uint32_t sr);
uint32_t cpu_exception(int vector, uint32_t return_pc);
uint32_t cpu_rte(void);
uint32_t cpu_stop(uint32_t sr, uint32_t next_pc);
uint32_t cpu_unimplemented(uint32_t pc);

#endif
