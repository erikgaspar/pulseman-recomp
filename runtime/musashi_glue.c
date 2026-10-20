/* Callbacks de memória do Musashi.
 * Modo normal: vão direto para o barramento (fallback do interpretador).
 * Modo replay (lockstep): leituras fora da ROM vêm do registro feito pelo
 * bloco recompilado, e escritas são comparadas com as dele. */
#include <stdio.h>
#include <stdlib.h>

#include "runtime.h"
#include "lockstep.h"
#include "m68k.h"

int musashi_replay;

unsigned int m68k_read_memory_8(unsigned int a)
{
    a &= 0xFFFFFF;
    if (musashi_replay && a >= 0x400000)
        return lockstep_replay_read(a, 1);
    return bus_read8(a);
}

unsigned int m68k_read_memory_16(unsigned int a)
{
    a &= 0xFFFFFF;
    if (musashi_replay && a >= 0x400000)
        return lockstep_replay_read(a, 2);
    return bus_read16(a);
}

unsigned int m68k_read_memory_32(unsigned int a)
{
    a &= 0xFFFFFF;
    if (musashi_replay && a >= 0x400000)
        return lockstep_replay_read(a, 4);
    return bus_read32(a);
}

void m68k_write_memory_8(unsigned int a, unsigned int v)
{
    a &= 0xFFFFFF;
    if (musashi_replay) {
        lockstep_replay_write(a, 1, v & 0xFF);
        return;
    }
    bus_write8(a, v);
}

void m68k_write_memory_16(unsigned int a, unsigned int v)
{
    a &= 0xFFFFFF;
    if (musashi_replay) {
        lockstep_replay_write(a, 2, v & 0xFFFF);
        return;
    }
    bus_write16(a, v);
}

void m68k_write_memory_32(unsigned int a, unsigned int v)
{
    a &= 0xFFFFFF;
    if (musashi_replay) {
        lockstep_replay_write(a, 4, v);
        return;
    }
    bus_write32(a, v);
}

/* Desmontagem usada nas mensagens de divergência */
unsigned int m68k_read_disassembler_8(unsigned int a) { return bus_read8(a & 0xFFFFFF); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return bus_read16(a & 0xFFFFFF); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return bus_read32(a & 0xFFFFFF); }
