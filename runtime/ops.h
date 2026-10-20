/* Operações da ALU do 68000 usadas pelo código recompilado.
 * Os flags "indefinidos" seguem o Musashi, para o modo lockstep comparar limpo. */
#ifndef PM_OPS_H
#define PM_OPS_H

#include "cpu.h"

#define D(n) cpu.d[n]
#define A(n) cpu.a[n]
#define CYC(n) (cpu.cycles += (n))
#ifdef PM_LOCKSTEP
#define INSN() (cpu.icount++)
#else
#define INSN() ((void)0)
#endif

#define MSK(sz) ((sz) == 1 ? 0xFFu : (sz) == 2 ? 0xFFFFu : 0xFFFFFFFFu)
#define MSB(sz) ((sz) == 1 ? 0x80u : (sz) == 2 ? 0x8000u : 0x80000000u)
#define BITS(sz) ((sz) * 8)

#define SX8(v) ((uint32_t)(int32_t)(int8_t)(v))
#define SX16(v) ((uint32_t)(int32_t)(int16_t)(v))

#define rd8(a) bus_read8((a) & 0xFFFFFF)
#define rd16(a) bus_read16((a) & 0xFFFFFF)
#define rd32(a) bus_read32((a) & 0xFFFFFF)
#define wr8(a, v) bus_write8((a) & 0xFFFFFF, (v))
#define wr16(a, v) bus_write16((a) & 0xFFFFFF, (v))
#define wr32(a, v) bus_write32((a) & 0xFFFFFF, (v))

/* Escrita parcial em registrador de dados */
#define SETD8(n, v) (D(n) = (D(n) & 0xFFFFFF00u) | ((v) & 0xFFu))
#define SETD16(n, v) (D(n) = (D(n) & 0xFFFF0000u) | ((v) & 0xFFFFu))
#define SETD32(n, v) (D(n) = (v))

static inline void push16(uint32_t v) { A(7) -= 2; wr16(A(7), v); }
static inline void push32(uint32_t v) { A(7) -= 4; wr32(A(7), v); }
static inline uint32_t pop16(void) { uint32_t v = rd16(A(7)); A(7) += 2; return v; }
static inline uint32_t pop32(void) { uint32_t v = rd32(A(7)); A(7) += 4; return v; }

static inline uint32_t get_ccr(void) {
    return (cpu.x << 4) | (cpu.n << 3) | (cpu.z << 2) | (cpu.v << 1) | cpu.c;
}

static inline void set_ccr(uint32_t v) {
    cpu.x = (v >> 4) & 1; cpu.n = (v >> 3) & 1; cpu.z = (v >> 2) & 1;
    cpu.v = (v >> 1) & 1; cpu.c = v & 1;
}

static inline void setnz(uint32_t r, int sz) {
    cpu.n = (r & MSB(sz)) != 0;
    cpu.z = (r & MSK(sz)) == 0;
}

/* MOVE, TST, AND, OR, EOR, NOT, CLR... */
static inline uint32_t op_logic(uint32_t r, int sz) {
    r &= MSK(sz);
    setnz(r, sz);
    cpu.v = cpu.c = 0;
    return r;
}

static inline uint32_t op_add(uint32_t d, uint32_t s, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    d &= m; s &= m;
    uint32_t r = (d + s) & m;
    cpu.x = cpu.c = (((s & d) | (~r & (s | d))) & hb) != 0;
    cpu.v = (((s ^ r) & (d ^ r)) & hb) != 0;
    setnz(r, sz);
    return r;
}

static inline uint32_t op_addx(uint32_t d, uint32_t s, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    d &= m; s &= m;
    uint32_t r = (d + s + cpu.x) & m;
    cpu.x = cpu.c = (((s & d) | (~r & (s | d))) & hb) != 0;
    cpu.v = (((s ^ r) & (d ^ r)) & hb) != 0;
    cpu.n = (r & hb) != 0;
    if (r) cpu.z = 0;
    return r;
}

static inline uint32_t op_sub(uint32_t d, uint32_t s, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    d &= m; s &= m;
    uint32_t r = (d - s) & m;
    cpu.x = cpu.c = (((s & ~d) | (r & ~d) | (s & r)) & hb) != 0;
    cpu.v = (((s ^ d) & (r ^ d)) & hb) != 0;
    setnz(r, sz);
    return r;
}

static inline uint32_t op_subx(uint32_t d, uint32_t s, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    d &= m; s &= m;
    uint32_t r = (d - s - cpu.x) & m;
    cpu.x = cpu.c = (((s & ~d) | (r & ~d) | (s & r)) & hb) != 0;
    cpu.v = (((s ^ d) & (r ^ d)) & hb) != 0;
    cpu.n = (r & hb) != 0;
    if (r) cpu.z = 0;
    return r;
}

static inline void op_cmp(uint32_t d, uint32_t s, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    d &= m; s &= m;
    uint32_t r = (d - s) & m;
    cpu.c = (((s & ~d) | (r & ~d) | (s & r)) & hb) != 0;
    cpu.v = (((s ^ d) & (r ^ d)) & hb) != 0;
    setnz(r, sz);
}

static inline uint32_t op_mulu(uint32_t d, uint32_t s) {
    uint32_t r = (d & 0xFFFF) * (s & 0xFFFF);
    return op_logic(r, 4);
}

static inline uint32_t op_muls(uint32_t d, uint32_t s) {
    uint32_t r = (uint32_t)((int32_t)(int16_t)d * (int32_t)(int16_t)s);
    return op_logic(r, 4);
}

/* Retorna 0 se ok, 1 se divisão por zero (o chamador gera a exceção) */
static inline int op_divu(uint32_t *dst, uint32_t s) {
    s &= 0xFFFF;
    if (s == 0) return 1;
    uint32_t q = *dst / s, r = *dst % s;
    if (q < 0x10000) {
        cpu.z = q == 0; cpu.n = (q & 0x8000) != 0; cpu.v = cpu.c = 0;
        *dst = (q & 0xFFFF) | (r << 16);
    } else {
        cpu.v = 1;
    }
    return 0;
}

static inline int op_divs(uint32_t *dst, uint32_t s) {
    int32_t src = (int16_t)s;
    if (src == 0) return 1;
    if (*dst == 0x80000000u && src == -1) {
        cpu.z = 0; cpu.n = cpu.v = cpu.c = 0;
        *dst = 0;
        return 0;
    }
    int32_t q = (int32_t)*dst / src, r = (int32_t)*dst % src;
    if (q == (int16_t)q) {
        cpu.z = q == 0; cpu.n = (q & 0x8000) != 0; cpu.v = cpu.c = 0;
        *dst = ((uint32_t)q & 0xFFFF) | ((uint32_t)r << 16);
    } else {
        cpu.v = 1;
    }
    return 0;
}

/* Deslocamentos e rotações. cnt já vem reduzido (registrador: & 63). */
static inline uint32_t op_asl(uint32_t v, uint32_t cnt, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    v &= m;
    cpu.v = 0;
    if (cnt == 0) { cpu.c = 0; setnz(v, sz); return v; }
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t out = (v & hb) != 0;
        v = (v << 1) & m;
        if (((v & hb) != 0) != out) cpu.v = 1;
        cpu.c = cpu.x = out;
    }
    setnz(v, sz);
    return v;
}

static inline uint32_t op_lsl(uint32_t v, uint32_t cnt, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    v &= m;
    cpu.v = 0;
    if (cnt == 0) { cpu.c = 0; setnz(v, sz); return v; }
    for (uint32_t i = 0; i < cnt; i++) {
        cpu.c = cpu.x = (v & hb) != 0;
        v = (v << 1) & m;
    }
    setnz(v, sz);
    return v;
}

static inline uint32_t op_asr(uint32_t v, uint32_t cnt, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    v &= m;
    cpu.v = 0;
    if (cnt == 0) { cpu.c = 0; setnz(v, sz); return v; }
    for (uint32_t i = 0; i < cnt; i++) {
        cpu.c = cpu.x = v & 1;
        v = (v >> 1) | (v & hb);
    }
    setnz(v, sz);
    return v;
}

static inline uint32_t op_lsr(uint32_t v, uint32_t cnt, int sz) {
    uint32_t m = MSK(sz);
    v &= m;
    cpu.v = 0;
    if (cnt == 0) { cpu.c = 0; setnz(v, sz); return v; }
    for (uint32_t i = 0; i < cnt; i++) {
        cpu.c = cpu.x = v & 1;
        v >>= 1;
    }
    setnz(v, sz);
    return v;
}

static inline uint32_t op_rol(uint32_t v, uint32_t cnt, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    v &= m;
    cpu.v = 0; cpu.c = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t out = (v & hb) != 0;
        v = ((v << 1) | out) & m;
        cpu.c = out;
    }
    setnz(v, sz);
    return v;
}

static inline uint32_t op_ror(uint32_t v, uint32_t cnt, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    v &= m;
    cpu.v = 0; cpu.c = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t out = v & 1;
        v = (v >> 1) | (out ? hb : 0);
        cpu.c = out;
    }
    setnz(v, sz);
    return v;
}

static inline uint32_t op_roxl(uint32_t v, uint32_t cnt, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    v &= m;
    cpu.v = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t out = (v & hb) != 0;
        v = ((v << 1) | cpu.x) & m;
        cpu.x = out;
    }
    cpu.c = cpu.x;
    setnz(v, sz);
    return v;
}

static inline uint32_t op_roxr(uint32_t v, uint32_t cnt, int sz) {
    uint32_t m = MSK(sz), hb = MSB(sz);
    v &= m;
    cpu.v = 0;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t out = v & 1;
        v = (v >> 1) | (cpu.x ? hb : 0);
        cpu.x = out;
    }
    cpu.c = cpu.x;
    setnz(v, sz);
    return v;
}

/* BCD, com o comportamento indefinido de N/V igual ao Musashi */
static inline uint32_t op_abcd(uint32_t d, uint32_t s) {
    uint32_t r = (s & 0x0F) + (d & 0x0F) + cpu.x;
    uint32_t vtmp = ~r;
    if (r > 9) r += 6;
    r += (s & 0xF0) + (d & 0xF0);
    cpu.x = cpu.c = r > 0x99;
    if (cpu.c) r -= 0xA0;
    cpu.v = ((vtmp & r) & 0x80) != 0;
    cpu.n = (r & 0x80) != 0;
    r &= 0xFF;
    if (r) cpu.z = 0;
    return r;
}

static inline uint32_t op_sbcd(uint32_t d, uint32_t s) {
    uint32_t r = (d & 0x0F) - (s & 0x0F) - cpu.x;
    uint32_t vtmp = ~r;
    if (r > 9) r -= 6;
    r += (d & 0xF0) - (s & 0xF0);
    cpu.x = cpu.c = r > 0x99;
    if (cpu.c) r += 0xA0;
    r &= 0xFF;
    cpu.v = ((vtmp & r) & 0x80) != 0;
    cpu.n = (r & 0x80) != 0;
    if (r) cpu.z = 0;
    return r;
}

/* Retorna o novo byte; *write indica se o destino deve ser escrito */
static inline uint32_t op_nbcd(uint32_t d, int *write) {
    uint32_t r = (0x9A - d - cpu.x) & 0xFF;
    if (r != 0x9A) {
        uint32_t vtmp = ~r;
        if ((r & 0x0F) == 0x0A) r = (r & 0xF0) + 0x10;
        r &= 0xFF;
        cpu.v = ((vtmp & r) & 0x80) != 0;
        if (r) cpu.z = 0;
        cpu.c = cpu.x = 1;
        *write = 1;
    } else {
        cpu.v = cpu.c = cpu.x = 0;
        *write = 0;
    }
    cpu.n = (r & 0x80) != 0;
    return r;
}

/* Condições Bcc/Scc/DBcc */
#define CC_T  1
#define CC_F  0
#define CC_HI (!cpu.c && !cpu.z)
#define CC_LS (cpu.c || cpu.z)
#define CC_CC (!cpu.c)
#define CC_CS (cpu.c)
#define CC_NE (!cpu.z)
#define CC_EQ (cpu.z)
#define CC_VC (!cpu.v)
#define CC_VS (cpu.v)
#define CC_PL (!cpu.n)
#define CC_MI (cpu.n)
#define CC_GE (cpu.n == cpu.v)
#define CC_LT (cpu.n != cpu.v)
#define CC_GT (!cpu.z && cpu.n == cpu.v)
#define CC_LE (cpu.z || cpu.n != cpu.v)

#endif
