/* Z80 + YM2612 + PSG, numa linha do tempo em ciclos mestres (53.693.175 Hz, NTSC).
 * 68000 = mestre/7, Z80 = mestre/15, amostra do YM2612 = mestre/1008, PSG = mestre/240. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "runtime.h"
#include "sound.h"
#include "savestate.h"
#include "z80.h"

#define MASTER_PER_YM 1008
#define MASTER_PER_PSG 240
#define MASTER_RATE 53693175.0
#define NATIVE_RATE (MASTER_RATE / MASTER_PER_YM)
#define OUT_RATE 48000

/* ---------- Z80 ---------- */

static z80 zc;
static int z80_busreq;
static int z80_reset = 1;
static uint32_t z80_bank;
static int64_t z80_base;  /* tempo mestre correspondente a zc.cyc == 0 */
static int z80_irq_line = -1;

static int64_t z80_now(void) { return z80_base + (int64_t)zc.cyc * 15; }
static int64_t m68k_now(void) { return cpu.cycles * 7; }

/* ---------- PSG SN76489 ---------- */

static struct {
    uint16_t period[4];
    uint8_t atten[4];
    uint16_t counter[4];
    uint8_t out[4];
    uint16_t lfsr;
    uint8_t latch;
    int64_t pos;   /* tempo mestre do último tick */
    int32_t acc;   /* soma das saídas desde a última amostra */
    int nacc;
} psg;

static int16_t psg_vol[16];

static void psg_reset(void)
{
    memset(&psg, 0, sizeof(psg));
    for (int i = 0; i < 4; i++)
        psg.atten[i] = 15;
    psg.lfsr = 0x8000;
    for (int i = 0; i < 15; i++)
        psg_vol[i] = (int16_t)(2600.0 * pow(10.0, -0.1 * i));
    psg_vol[15] = 0;
}

static void psg_tick(void)
{
    for (int ch = 0; ch < 3; ch++) {
        if (psg.counter[ch] > 0)
            psg.counter[ch]--;
        if (psg.counter[ch] == 0) {
            psg.counter[ch] = psg.period[ch] ? psg.period[ch] : 1;
            psg.out[ch] ^= 1;
        }
    }
    if (psg.counter[3] > 0)
        psg.counter[3]--;
    if (psg.counter[3] == 0) {
        int rate = psg.period[3] & 3;
        psg.counter[3] = rate == 3 ? (psg.period[2] ? psg.period[2] : 1) : (0x10 << rate);
        psg.out[3] ^= 1;
        if (psg.out[3]) { /* o registrador desloca na borda de subida */
            int fb = (psg.period[3] & 4) ? ((psg.lfsr ^ (psg.lfsr >> 3)) & 1) : (psg.lfsr & 1);
            psg.lfsr = (uint16_t)((psg.lfsr >> 1) | (fb << 15));
        }
    }
    int32_t s = 0;
    for (int ch = 0; ch < 3; ch++)
        s += psg.out[ch] ? psg_vol[psg.atten[ch]] : -psg_vol[psg.atten[ch]];
    s += (psg.lfsr & 1) ? psg_vol[psg.atten[3]] : -psg_vol[psg.atten[3]];
    psg.acc += s;
    psg.nacc++;
}

static void psg_write(uint8_t v)
{
    if (v & 0x80) {
        psg.latch = (v >> 4) & 7;
        int ch = psg.latch >> 1;
        if (psg.latch & 1)
            psg.atten[ch] = v & 15;
        else if (ch == 3) {
            psg.period[3] = v & 7;
            psg.lfsr = 0x8000;
        } else
            psg.period[ch] = (psg.period[ch] & 0x3F0) | (v & 15);
    } else {
        int ch = psg.latch >> 1;
        if (psg.latch & 1)
            psg.atten[ch] = v & 15;
        else if (ch == 3) {
            psg.period[3] = v & 7;
            psg.lfsr = 0x8000;
        } else
            psg.period[ch] = (psg.period[ch] & 15) | ((v & 0x3F) << 4);
    }
}

/* ---------- linha do tempo e mixagem ---------- */

#define MAX_NATIVE 4096
static int16_t native_buf[MAX_NATIVE][2];
static int native_n;
static int64_t snd_pos;       /* tempo mestre até onde já há amostras */
static int16_t last_native[2];
static double resample_pos;   /* posição fracionária na entrada (relativa a last_native) */

static SDL_AudioDeviceID audio_dev;
static double frame_rate;   /* 0 = ritmo original (59,92 Hz), sem controle de taxa */
static int volume_percent = 100;  /* só na saída de áudio (o WAV de teste fica cheio) */

void sound_set_frame_rate(double fps)
{
    frame_rate = fps;
}

/* Estatísticas de depuração (PM_DEBUG_SOUND=1) */
static uint8_t ym_addr[2];
static uint64_t dbg_dac_writes, dbg_bank_reads, dbg_ym_writes;
static FILE *wav_file;
static uint32_t wav_bytes;

static void sound_sync(int64_t now)
{
    while (snd_pos + MASTER_PER_YM <= now) {
        snd_pos += MASTER_PER_YM;
        /* PSG até este instante */
        while (psg.pos + MASTER_PER_PSG <= snd_pos) {
            psg.pos += MASTER_PER_PSG;
            psg_tick();
        }
        int32_t p = psg.nacc ? psg.acc / psg.nacc : 0;
        psg.acc = 0;
        psg.nacc = 0;
        int32_t ym[2];
        ym_generate(ym);
        for (int c = 0; c < 2; c++) {
            /* passa-altas de ~20 Hz, como o capacitor de saída do console: remove o DC */
            static double dc_x[2], dc_y[2];
            double x = ym[c] * 1.5 + p;
            dc_y[c] = x - dc_x[c] + 0.9976 * dc_y[c];
            dc_x[c] = x;
            int32_t s = (int32_t)dc_y[c];
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            if (native_n < MAX_NATIVE)
                native_buf[native_n][c] = (int16_t)s;
        }
        if (native_n < MAX_NATIVE)
            native_n++;
    }
}

static void write_wav_header(void)
{
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
                     1, 0, 2, 0};
    uint32_t rate = OUT_RATE, byterate = OUT_RATE * 4, riff = 36 + wav_bytes;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byterate, 4);
    h[32] = 4; h[34] = 16;
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &wav_bytes, 4);
    fseek(wav_file, 0, SEEK_SET);
    fwrite(h, 1, 44, wav_file);
    fseek(wav_file, 0, SEEK_END);
}

void sound_end_frame(void)
{
    sound_sync(m68k_now());

    /* reamostragem linear de ~53267 Hz para 48000 Hz */
    static int16_t out[2048][2];
    int n_out = 0;
    double step = NATIVE_RATE / OUT_RATE;
    if (frame_rate > 0) {
        /* o jogo roda a frame_rate em vez de 59,92 Hz: comprime o áudio na mesma proporção */
        step *= frame_rate / (MASTER_RATE / (LINES_PER_FRAME * 3420.0));
        if (audio_dev) {
            /* correção fina pela fila: mira ~50 ms, no máximo ±0,5% (inaudível) */
            double target = OUT_RATE * 4 * 0.05;
            double err = (SDL_GetQueuedAudioSize(audio_dev) - target) / target;
            if (err > 1) err = 1;
            if (err < -1) err = -1;
            step *= 1.0 + 0.005 * err;
        }
    }
    while (n_out < 2048) {
        int i = (int)resample_pos;   /* 0 = last_native, k = native_buf[k-1] */
        if (i + 1 > native_n)
            break;
        double f = resample_pos - i;
        for (int c = 0; c < 2; c++) {
            double a = i == 0 ? last_native[c] : native_buf[i - 1][c];
            double b = native_buf[i][c];
            out[n_out][c] = (int16_t)(a + (b - a) * f);
        }
        n_out++;
        resample_pos += step;
    }
    if (native_n) {
        last_native[0] = native_buf[native_n - 1][0];
        last_native[1] = native_buf[native_n - 1][1];
    }
    resample_pos -= native_n;
    if (resample_pos < 0)
        resample_pos = 0;
    native_n = 0;

    if (audio_dev) {
        /* trava de segurança: descarta só se a fila passar de 200 ms */
        if (SDL_GetQueuedAudioSize(audio_dev) < OUT_RATE * 4 / 5)
        {
            if (volume_percent < 100)
                for (int i = 0; i < n_out; i++) {
                    out[i][0] = (int16_t)(out[i][0] * volume_percent / 100);
                    out[i][1] = (int16_t)(out[i][1] * volume_percent / 100);
                }
            SDL_QueueAudio(audio_dev, out, n_out * 4);
        }
    }
    if (wav_file) {
        fwrite(out, 4, n_out, wav_file);
        wav_bytes += n_out * 4;
    }
}

/* ---------- mapa de memória do Z80 ---------- */

static uint8_t z80_read(void *u, uint16_t a)
{
    (void)u;
    if (a < 0x4000)
        return z80_ram[a & 0x1FFF];
    if (a < 0x6000) {
        sound_sync(z80_now());
        return (uint8_t)ym_read_status();
    }
    if (a >= 0x8000) {
        dbg_bank_reads++;
        zc.cyc += 3; /* espera pelo barramento do 68000 (aprox.) */
        return (uint8_t)bus_read8_raw(((z80_bank << 15) | (a & 0x7FFF)) & 0xFFFFFF);
    }
    return 0xFF;
}

static void z80_write(void *u, uint16_t a, uint8_t v)
{
    (void)u;
    if (a < 0x4000) {
        z80_ram[a & 0x1FFF] = v;
    } else if (a < 0x6000) {
        sound_sync(z80_now());
        if (a & 1) {
            dbg_ym_writes++;
            if (ym_addr[(a >> 1) & 1] == 0x2A && !(a & 2))
                dbg_dac_writes++;
        } else {
            ym_addr[(a >> 1) & 1] = v;
        }
        ym_write(a & 3, v);
    } else if (a < 0x6100) {
        z80_bank = ((z80_bank >> 1) | ((v & 1u) << 8)) & 0x1FF;
    } else if (a >= 0x7F10 && a < 0x7F18) {
        if (a & 1) {
            sound_sync(z80_now());
            psg_write(v);
        }
    } else if (a >= 0x8000) {
        bus_write8_raw(((z80_bank << 15) | (a & 0x7FFF)) & 0xFFFFFF, v);
    }
}

static uint8_t z80_in(z80 *z, uint8_t port) { (void)z; (void)port; return 0xFF; }
static void z80_out(z80 *z, uint8_t port, uint8_t v) { (void)z; (void)port; (void)v; }

static void z80_do_reset(void)
{
    z80_init(&zc);
    zc.read_byte = z80_read;
    zc.write_byte = z80_write;
    zc.port_in = z80_in;
    zc.port_out = z80_out;
    z80_bank = 0;
}

/* ---------- interface com o 68000 ---------- */

uint32_t sound_68k_read8(uint32_t addr)
{
    uint32_t a = addr & 0xFFFF;
    if (a < 0x4000)
        return z80_ram[a & 0x1FFF];
    if (a < 0x6000) {
        sound_sync(m68k_now());
        return (uint32_t)ym_read_status();
    }
    return 0xFF;
}

void sound_68k_write8(uint32_t addr, uint32_t v)
{
    uint32_t a = addr & 0xFFFF;
    if (a < 0x4000) {
        z80_ram[a & 0x1FFF] = (uint8_t)v;
    } else if (a < 0x6000) {
        sound_sync(m68k_now());
        ym_write(a & 3, (int)v);
    } else if (a < 0x6100) {
        z80_bank = ((z80_bank >> 1) | ((v & 1u) << 8)) & 0x1FF;
    } else if (a >= 0x7F10 && a < 0x7F18 && (a & 1)) {
        sound_psg_write_68k(v);
    }
}

uint32_t sound_busreq_read(void)
{
    /* bit 0 = 0: barramento do Z80 cedido ao 68000 */
    return (z80_busreq || z80_reset) ? 0 : 1;
}

void sound_set_busreq(int on)
{
    z80_busreq = on;
}

void sound_set_reset(int asserted)
{
    if (asserted && !z80_reset) {
        sound_sync(m68k_now());
        ym_reset();
    }
    if (!asserted && z80_reset)
        z80_do_reset();
    z80_reset = asserted;
}

void sound_psg_write_68k(uint32_t v)
{
    sound_sync(m68k_now());
    psg_write((uint8_t)v);
}

void sound_run_line(int line)
{
    int64_t target = m68k_now();
    if (z80_irq_line >= 0 && line > z80_irq_line) {
        zc.int_pending = 0;  /* a linha de interrupção fica ativa só por uma linha */
        z80_irq_line = -1;
    }
    if (line == SCREEN_H) {
        z80_gen_int(&zc, 0xFF);
        z80_irq_line = line;
    }
    if (z80_reset || z80_busreq) {
        z80_base = target;
        zc.cyc = 0;
    } else {
        while (z80_now() < target)
            z80_step(&zc);
        z80_base = z80_now();
        zc.cyc = 0;
    }
    sound_sync(target);
}

/* Reset do console: Z80 volta ao reset, YM2612 e PSG zerados; o áudio continua aberto. */
void sound_reset(void)
{
    int64_t pos = psg.pos;
    z80_busreq = 0;
    z80_reset = 1;
    z80_do_reset();
    ym_reset();
    psg_reset();
    psg.pos = pos;
}

void sound_set_volume(int percent)
{
    volume_percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
}

void sound_pause(int paused)
{
    if (audio_dev) {
        SDL_PauseAudioDevice(audio_dev, paused);
        if (!paused)
            SDL_ClearQueuedAudio(audio_dev); /* recomeça sem o resto antigo da fila */
    }
}

void sound_init(int sdl_output, const char *wav_path)
{
    z80_do_reset();
    ym_reset();
    psg_reset();
    snd_pos = psg.pos = 0;
    if (sdl_output) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
            SDL_AudioSpec want = {0}, have;
            want.freq = OUT_RATE;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 1024;
            audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (audio_dev)
                SDL_PauseAudioDevice(audio_dev, 0);
            else
                fprintf(stderr, "sem saída de áudio: %s\n", SDL_GetError());
        }
    }
    if (wav_path) {
        wav_file = fopen(wav_path, "wb");
        if (wav_file)
            write_wav_header();
    }
}

void sound_shutdown(void)
{
    if (getenv("PM_DEBUG_SOUND"))
        fprintf(stderr, "som: %llu escritas no YM pelo Z80, %llu no DAC, %llu leituras pelo banco\n",
                (unsigned long long)dbg_ym_writes, (unsigned long long)dbg_dac_writes,
                (unsigned long long)dbg_bank_reads);
    if (wav_file) {
        write_wav_header();
        fclose(wav_file);
        wav_file = NULL;
    }
    if (audio_dev)
        SDL_CloseAudioDevice(audio_dev);
}

/* Jogo salvo: Z80, PSG e a linha do tempo do som (o YM2612 fica em ym_shim.cpp). As
 * amostras ainda não tocadas são descartadas (a fila do SDL é limpa ao fechar o menu). */
void sound_state(StateIO *s)
{
    z80 saved = zc;
    STATE_VAR(s, zc);
    if (s->loading) {   /* ponteiros de função não vêm do arquivo */
        zc.read_byte = saved.read_byte;
        zc.write_byte = saved.write_byte;
        zc.port_in = saved.port_in;
        zc.port_out = saved.port_out;
        zc.userdata = saved.userdata;
    }
    STATE_VAR(s, z80_busreq);
    STATE_VAR(s, z80_reset);
    STATE_VAR(s, z80_bank);
    STATE_VAR(s, z80_base);
    STATE_VAR(s, z80_irq_line);
    STATE_VAR(s, psg);
    STATE_VAR(s, snd_pos);
    STATE_VAR(s, last_native);
    STATE_VAR(s, resample_pos);
    STATE_VAR(s, ym_addr);
    if (s->loading)
        native_n = 0;
}
