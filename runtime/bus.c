/* Mapa de memória do Mega Drive visto pelo 68000. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "runtime.h"
#include "lockstep.h"
#include "sound.h"

uint8_t *rom_data;
uint32_t rom_size;
uint8_t work_ram[0x10000];
uint8_t z80_ram[0x2000];


int bus_load_rom(const char *path, const char *expected_sha1)
{
    /* caminho em UTF-8 (SDL_GetBasePath, pasta com acento); da linha de comando ele vem na
     * página de código do Windows, então o fopen fica de reserva */
    rom_size = 0x400000;
    rom_data = calloc(rom_size, 1);
    long size = -1;
    SDL_RWops *f = SDL_RWFromFile(path, "rb");
    if (f) {
        size = (long)SDL_RWsize(f);
        if (size <= 0 || size > (long)rom_size || SDL_RWread(f, rom_data, 1, size) != (size_t)size)
            size = 0;
        SDL_RWclose(f);
    } else {   /* o SDL2 de Windows não aceita FILE* (SDL_RWFromFP): lê pelo C mesmo */
        FILE *cf = fopen(path, "rb");
        if (cf) {
            fseek(cf, 0, SEEK_END);
            size = ftell(cf);
            fseek(cf, 0, SEEK_SET);
            if (size <= 0 || size > (long)rom_size || fread(rom_data, 1, size, cf) != (size_t)size)
                size = 0;
            fclose(cf);
        }
    }
    if (size < 0) {
        fprintf(stderr, "não consegui abrir a ROM: %s\n", path);
        return 0;
    }
    if (size == 0) {
        fprintf(stderr, "ROM com tamanho inválido: %s\n", path);
        return 0;
    }
    char sha[41];
    sha1_hex(rom_data, size, sha);
    if (strcmp(sha, expected_sha1) != 0) {
        fprintf(stderr, "ROM diferente da usada para gerar o código.\n  esperado %s\n  recebido %s\n",
                expected_sha1, sha);
        return 0;
    }
    /* espelha ROMs menores que 4 MB */
    for (long i = size; i < (long)rom_size; i++)
        rom_data[i] = rom_data[i % size];
    return 1;
}

/* A área do Z80 (A00000-A0FFFF) é tratada em sound.c */

static uint32_t z80_area_read8(uint32_t addr) { return sound_68k_read8(addr); }
static void z80_area_write8(uint32_t addr, uint32_t v) { sound_68k_write8(addr, v); }

/* ---------- leitura ---------- */

static uint32_t ctrl_read8(uint32_t addr)
{
    if (addr >= 0xA10000 && addr < 0xA10020)
        return io_read(addr);
    if ((addr & ~1u) == 0xA11100) {
        if (addr & 1)
            return 0;
        return sound_busreq_read();
    }
    return 0;
}

uint32_t bus_read8_raw(uint32_t addr)
{
    if (addr < 0x400000)
        return rom_data[addr];
    if (addr >= 0xE00000)
        return work_ram[addr & 0xFFFF];
    if (addr >= 0xC00000 && addr < 0xE00000) {
        uint32_t w = bus_read16_raw(addr & ~1u);
        return (addr & 1) ? (w & 0xFF) : (w >> 8);
    }
    if (addr >= 0xA00000 && addr < 0xA10000)
        return z80_area_read8(addr);
    if (addr >= 0xA10000 && addr < 0xA20000)
        return ctrl_read8(addr);
    return 0;
}

uint32_t bus_read16_raw(uint32_t addr)
{
    if (addr < 0x400000)
        return (rom_data[addr] << 8) | rom_data[addr + 1];
    if (addr >= 0xE00000) {
        uint32_t a = addr & 0xFFFF;
        return (work_ram[a] << 8) | work_ram[(a + 1) & 0xFFFF];
    }
    if (addr >= 0xC00000 && addr < 0xE00000) {
        switch (addr & 0x1F) {
        case 0x00: case 0x02: return vdp_read_data();
        case 0x04: case 0x06: return vdp_read_status();
        case 0x08: case 0x0A: case 0x0C: case 0x0E: return vdp_read_hv();
        default: return 0xFFFF;
        }
    }
    if (addr >= 0xA00000 && addr < 0xA10000) {
        uint32_t b = z80_area_read8(addr);
        return (b << 8) | b;
    }
    if (addr >= 0xA10000 && addr < 0xA20000) {
        if (addr >= 0xA10000 && addr < 0xA10020) {
            uint32_t b = io_read(addr | 1);
            return (b << 8) | b;
        }
        return (ctrl_read8(addr) << 8) | ctrl_read8(addr | 1);
    }
    return 0;
}

uint32_t bus_read32_raw(uint32_t addr)
{
    uint32_t hi = bus_read16_raw(addr);
    return (hi << 16) | bus_read16_raw((addr + 2) & 0xFFFFFF);
}

/* ---------- escrita ---------- */

static void ctrl_write16(uint32_t addr, uint32_t v)
{
    if (addr == 0xA11100)
        sound_set_busreq((v >> 8) & 1);
    else if (addr == 0xA11200)
        sound_set_reset(!((v >> 8) & 1));
}

void bus_write8_raw(uint32_t addr, uint32_t v)
{
    v &= 0xFF;
    if (addr >= 0xE00000) {
        work_ram[addr & 0xFFFF] = (uint8_t)v;
        return;
    }
    if (addr >= 0xC00000 && addr < 0xE00000) {
        uint32_t port = addr & 0x1F;
        if (port >= 0x10) {
            if (port < 0x18 && (port & 1))
                sound_psg_write_68k(v);
            return;
        }
        bus_write16_raw(addr & ~1u, (v << 8) | v);
        return;
    }
    if (addr >= 0xA00000 && addr < 0xA10000) {
        z80_area_write8(addr, v);
        return;
    }
    if (addr >= 0xA10000 && addr < 0xA10020) {
        io_write(addr, v);
        return;
    }
    if (addr == 0xA11100 || addr == 0xA11200)
        ctrl_write16(addr, v << 8);
}

/* Depuração (PM_WATCH=inicio-fim, em hex): registra escritas de palavra na RAM */
static uint32_t watch_lo = 1, watch_hi = 0;

static void watch_init(void)
{
    static int done;
    if (done)
        return;
    done = 1;
    const char *s = getenv("PM_WATCH");
    if (s && sscanf(s, "%x-%x", &watch_lo, &watch_hi) != 2)
        watch_lo = 1, watch_hi = 0;
}

void bus_write16_raw(uint32_t addr, uint32_t v)
{
    v &= 0xFFFF;
    if (addr >= 0xE00000) {
        uint32_t a = addr & 0xFFFF;
        watch_init();
        if (a >= watch_lo && a <= watch_hi)
            fprintf(stderr, "watch %04X=%04X pc=%06X\n", a, v, cpu_pc);
        work_ram[a] = (uint8_t)(v >> 8);
        work_ram[(a + 1) & 0xFFFF] = (uint8_t)v;
        return;
    }
    if (addr >= 0xC00000 && addr < 0xE00000) {
        switch (addr & 0x1F) {
        case 0x00: case 0x02: vdp_write_data(v); break;
        case 0x04: case 0x06: vdp_write_control(v); break;
        case 0x10: case 0x12: case 0x14: case 0x16:
            sound_psg_write_68k(v & 0xFF); /* PSG em escrita de palavra: byte baixo */
            break;
        default: break; /* HV é somente leitura */
        }
        return;
    }
    if (addr >= 0xA00000 && addr < 0xA10000) {
        z80_area_write8(addr, v >> 8);
        return;
    }
    if (addr >= 0xA10000 && addr < 0xA10020) {
        io_write(addr | 1, v & 0xFF);
        return;
    }
    if (addr >= 0xA11000 && addr < 0xA12000)
        ctrl_write16(addr, v);
}

void bus_write32_raw(uint32_t addr, uint32_t v)
{
    bus_write16_raw(addr, v >> 16);
    bus_write16_raw((addr + 2) & 0xFFFFFF, v & 0xFFFF);
}

/* ---------- pontos de entrada usados pela CPU ---------- */
/* Fora da ROM, os acessos são gravados no modo lockstep para o replay no Musashi. */

static uint32_t rec_read(uint32_t addr, int size, uint32_t v)
{
    if (lockstep_recording && addr >= 0x400000)
        lockstep_record(0, addr, size, v);
    return v;
}

static void rec_write(uint32_t addr, int size, uint32_t v)
{
    if (lockstep_recording)
        lockstep_record(1, addr, size, v);
}

uint32_t bus_read8(uint32_t addr) { return rec_read(addr, 1, bus_read8_raw(addr)); }
uint32_t bus_read16(uint32_t addr) { return rec_read(addr, 2, bus_read16_raw(addr)); }
uint32_t bus_read32(uint32_t addr) { return rec_read(addr, 4, bus_read32_raw(addr)); }
void bus_write8(uint32_t addr, uint32_t v) { rec_write(addr, 1, v & 0xFF); bus_write8_raw(addr, v); }
void bus_write16(uint32_t addr, uint32_t v) { rec_write(addr, 2, v & 0xFFFF); bus_write16_raw(addr, v); }
void bus_write32(uint32_t addr, uint32_t v) { rec_write(addr, 4, v); bus_write32_raw(addr, v); }
