/* Portas de E/S: versão do console e controles de 3 botões. */
#include "runtime.h"
#include "savestate.h"

uint32_t pad_state[2];          /* bits PAD_*, 1 = pressionado */
static uint8_t port_data[3];    /* último valor escrito em A10003/5/7 */
static uint8_t port_ctrl[3];    /* direção dos bits (A10009/B/D) */

/* Valor lido no pino de dados, ativo em nível baixo */
static uint32_t pad_read(int port)
{
    uint32_t p = port < 2 ? pad_state[port] : 0;
    uint32_t th = port_data[port] & 0x40;
    uint32_t v;
    if (th) {
        /* TH=1: ?1CBRLDU */
        v = 0x40 | ((p & PAD_C) ? 0 : 0x20) | ((p & PAD_B) ? 0 : 0x10) |
            ((p & PAD_RIGHT) ? 0 : 0x08) | ((p & PAD_LEFT) ? 0 : 0x04) |
            ((p & PAD_DOWN) ? 0 : 0x02) | ((p & PAD_UP) ? 0 : 0x01);
    } else {
        /* TH=0: ?0SA00DU */
        v = ((p & PAD_START) ? 0 : 0x20) | ((p & PAD_A) ? 0 : 0x10) |
            ((p & PAD_DOWN) ? 0 : 0x02) | ((p & PAD_UP) ? 0 : 0x01);
    }
    /* bits configurados como saída devolvem o que foi escrito */
    uint32_t out = port_ctrl[port] & 0x7F;
    return (port_data[port] & 0x80) | (v & ~out & 0x7F) | (port_data[port] & out);
}

uint32_t io_read(uint32_t addr)
{
    switch (addr & 0x1F) {
    case 0x01: return 0x20;           /* Japão, NTSC, sem expansão */
    case 0x03: return pad_read(0);
    case 0x05: return pad_read(1);
    case 0x07: return pad_read(2);
    case 0x09: return port_ctrl[0];
    case 0x0B: return port_ctrl[1];
    case 0x0D: return port_ctrl[2];
    default: return 0;
    }
}

void io_write(uint32_t addr, uint32_t v)
{
    switch (addr & 0x1F) {
    case 0x03: port_data[0] = (uint8_t)v; break;
    case 0x05: port_data[1] = (uint8_t)v; break;
    case 0x07: port_data[2] = (uint8_t)v; break;
    case 0x09: port_ctrl[0] = (uint8_t)v; break;
    case 0x0B: port_ctrl[1] = (uint8_t)v; break;
    case 0x0D: port_ctrl[2] = (uint8_t)v; break;
    default: break;
    }
}

void io_state(StateIO *s)
{
    state_bytes(s, port_data, sizeof(port_data));
    state_bytes(s, port_ctrl, sizeof(port_ctrl));
}
