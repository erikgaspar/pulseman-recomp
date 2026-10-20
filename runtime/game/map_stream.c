/* Carregamento do mapa no plano A durante a rolagem (rotinas $24EBE e $24F94).
 *
 * O mapa da fase é uma grade de metatiles de 16x16 px (4 tiles), uma palavra por
 * metatile, que dá a volta na horizontal. O plano A é um anel de 64x32 tiles
 * (512x256 px); a cada passo de rolagem o jogo escreve uma coluna ou uma linha
 * de metatiles logo além da borda da tela.
 *
 * Com map_stream_extra = 0 estas funções reproduzem o código original exatamente
 * (registradores e flags no retorno), o que o pulseman_lockstep confere.
 * Com map_stream_extra = N, carregam N metatiles a mais de cada lado (widescreen).
 */
#include "ops.h"
#include "runtime.h"

int map_stream_extra = 0;

/* Campos da estrutura de câmera/mapa apontada por a5 (nomes deduzidos do uso) */
#define CAM_MAP_STRIDE  0   /* w: bytes por linha do mapa (2 por metatile) */
#define CAM_MAP_ROWS    2   /* w: altura do mapa em metatiles */
#define CAM_METATILES  14   /* l: definições dos metatiles (4 palavras de tile cada) */
#define CAM_NT_COL     18   /* w: coluna (em bytes, 0..0x7F) da câmera-1 na tabela de tiles */
#define CAM_NT_ROW     20   /* w: linha da câmera na tabela (0x100 por metatile, anel de 0x1000) */
#define CAM_VRAM_POS   22   /* w: linha+coluna da câmera, para montar o endereço de VRAM */
#define CAM_COL        32   /* w: coluna da câmera no mapa, em bytes */
#define CAM_ROW        34   /* w: linha da câmera no mapa, em metatiles */
#define CAM_MAP_PTR    36   /* l: ponteiro do mapa na posição da câmera */
#define CAM_FLAGS      46   /* b: bit 6/7 = tela mais alta/linha cheia */
#define CAM_VDP_CMD    72   /* l: comando base de escrita na VRAM do plano A */
#define CAM_TILE_ADD   76   /* w: somado a cada tile (paleta/prioridade) */

#define VDP_DATA 0xC00000
#define VDP_CTRL 0xC00004

/* Escreve as 4 palavras de tile de um metatile: 2 na linha de cima, 2 na de baixo. */
static void put_metatile_column(uint32_t def)
{
    /* coluna: ordem (0,1) na linha de cima, depois (2,3) uma linha abaixo (+0x80 bytes) */
    wr32(A(0) + 4, A(3));
    SETD16(0, rd16(def) + D(5)); wr16(A(0), D(0));
    SETD16(0, rd16(def + 2) + D(5)); wr16(A(0), D(0));
    A(3) += 0x800000;
    wr32(A(0) + 4, A(3));
    A(3) -= 0x800000;
    SETD16(0, rd16(def + 4) + D(5)); wr16(A(0), D(0));
    SETD16(0, rd16(def + 6) + D(5)); wr16(A(0), D(0));
    A(4) = def + 6;
}

/* $24EBE: escreve uma coluna de metatiles. d7 = coluna relativa à câmera, em bytes do mapa
 * (0 = borda esquerda, $28 = borda direita). */
uint32_t ov_024EBE(void)
{
    uint32_t cam = A(5);
    int16_t extra = (int16_t)(map_stream_extra * 2);
    if (extra) /* widescreen: empurra a coluna para fora da área visível maior */
        SETD16(7, (int16_t)D(7) <= 0 ? D(7) - extra : D(7) + extra);

    A(2) = rd32(cam + CAM_MAP_PTR) + SX16(D(7));
    SETD16(1, op_add(rd16(cam + CAM_COL), D(7), 2));
    SETD16(3, rd16(cam + CAM_MAP_STRIDE));
    op_cmp(D(3), D(1), 2);
    if (extra && (int16_t)D(1) < 0)
        A(2) += SX16(D(3));            /* antes do início da linha: dá a volta para o fim */
    else if (!CC_HI)
        A(2) -= SX16(D(3));            /* passou do fim da linha: volta para o começo */

    SETD16(1, rd16(cam + CAM_ROW));
    SETD16(2, rd16(cam + CAM_MAP_ROWS));
    A(2) -= SX16(D(3));                /* começa uma linha de metatiles acima da câmera */
    SETD16(1, op_sub(D(1), 1, 2));
    SETD16(4, rd16(cam + CAM_NT_ROW));
    SETD16(5, rd16(cam + CAM_VRAM_POS));
    SETD16(7, op_add(D(7), 2, 2));
    SETD16(7, op_asl(D(7), 1, 2));     /* bytes do mapa -> bytes da tabela de tiles */
    SETD16(6, op_add(rd16(cam + CAM_NT_COL), D(7), 2));
    SETD16(5, op_add(D(5), D(7), 2));
    op_cmp(D(6), 0x80, 2);
    if (!CC_LT) {                      /* passou da coluna 64: dá a volta no anel */
        SETD16(6, op_sub(D(6), 0x80, 2));
        SETD16(5, op_sub(D(5), 0x80, 2));
    } else if (extra && (int16_t)D(6) < 0) {
        SETD16(6, D(6) + 0x80);
        SETD16(5, D(5) + 0x80);
    }
    D(5) = op_add((D(5) & 0xFFFF) << 16, rd32(cam + CAM_VDP_CMD), 4);   /* swap; clr.w; add.l */
    A(3) = D(5);
    A(1) = rd32(cam + CAM_METATILES);
    SETD16(5, rd16(cam + CAM_TILE_ADD));
    cpu.z = ((rd8(cam + CAM_FLAGS) >> 6) & 1) == 0;
    SETD16(7, cpu.z ? 0x0F : 0x0D);    /* 16 ou 14 metatiles de altura */
    A(0) = VDP_DATA;
    cpu_set_sr(cpu_get_sr() | 0x700);

    int rows = 0;
    for (;;) {
        if (!(D(1) & 0x8000)) {        /* linha do mapa válida (não negativa) */
            SETD16(0, op_asl(rd16(A(2)), 3, 2));
            put_metatile_column(A(1) + SX16(D(0)));
        }
        SETD16(1, op_add(D(1), 1, 2));
        SETD16(4, op_add(D(4), 0x100, 2));
        A(3) += 0x1000000;             /* +2 linhas de tiles (256 bytes) */
        op_cmp(D(4), 0x1000, 2);
        if (!CC_LT) {                  /* fim do anel vertical de 32 tiles */
            SETD16(4, op_sub(D(4), 0x1000, 2));
            A(3) -= 0x10000000;
        }
        A(2) += SX16(D(3));
        op_cmp(D(1), D(2), 2);
        rows++;
        if (CC_GE)                     /* passou da última linha do mapa */
            break;
        uint32_t c = (D(7) - 1) & 0xFFFF;
        SETD16(7, c);
        if (c == 0xFFFF)
            break;
    }
    cpu_set_sr(cpu_get_sr() & 0xF8FF);
    cpu.cycles += 120 + rows * 150;
    return pop32();
}

/* $24F94: escreve uma linha de metatiles. d7 = linha relativa à câmera (0 = topo, $E = base). */
uint32_t ov_024F94(void)
{
    uint32_t cam = A(5);
    int extra = map_stream_extra;

    SETD16(3, op_add(rd16(cam + CAM_ROW), D(7), 2));
    if (cpu.n)
        return pop32();                /* acima do mapa */
    op_cmp(D(3), rd16(cam + CAM_MAP_ROWS), 2);
    if (CC_CC)
        return pop32();                /* abaixo do mapa */

    A(2) = rd32(cam + CAM_MAP_PTR);
    SETD16(3, rd16(cam + CAM_MAP_STRIDE));
    D(3) = op_muls(D(3), D(7));
    A(2) += D(3);
    SETD16(1, rd16(cam + CAM_COL));
    SETD16(3, rd16(cam + CAM_MAP_STRIDE));
    A(2) -= 2 + extra * 2;             /* começa 1 metatile (+extra) à esquerda da câmera */
    SETD16(1, op_sub(D(1), 2, 2));
    if (extra)
        SETD16(1, D(1) - extra * 2);
    if (D(1) & 0x8000) {
        A(2) += SX16(D(3));
        SETD16(1, op_add(D(1), D(3), 2));
    }
    SETD16(4, rd16(cam + CAM_NT_ROW));
    SETD16(5, rd16(cam + CAM_VRAM_POS));
    SETD16(7, op_add(D(7), 1, 2));
    SETD16(7, op_asl(D(7), 8, 2));
    SETD16(4, op_add(D(4), D(7), 2));
    SETD16(5, op_add(D(5), D(7), 2));
    op_cmp(D(4), 0x1000, 2);
    if (!CC_LT) {
        SETD16(4, op_sub(D(4), 0x1000, 2));
        SETD16(5, op_sub(D(5), 0x1000, 2));
    }
    SETD16(6, rd16(cam + CAM_NT_COL));
    D(5) = op_add((D(5) & 0xFFFF) << 16, rd32(cam + CAM_VDP_CMD), 4);
    A(3) = D(5);
    if (extra) {                       /* mesma volta à esquerda na tabela de tiles */
        SETD16(6, D(6) - extra * 4);
        A(3) -= (uint32_t)(extra * 4) << 16;
        if (D(6) & 0x8000) {
            SETD16(6, D(6) + 0x80);
            A(3) += 0x800000;
        }
    }
    A(1) = rd32(cam + CAM_METATILES);
    SETD16(5, rd16(cam + CAM_TILE_ADD));
    A(0) = VDP_DATA;
    /* o original testa o bit 7 e, se apagado, lê de novo para testar o bit 6 */
    int full_row = (rd8(cam + CAM_FLAGS) & 0x80) || (rd8(cam + CAM_FLAGS) & 0x40);
    if (full_row)
        SETD16(7, 0x1F);               /* linha cheia: 32 metatiles = o anel inteiro */
    else
        SETD16(7, extra ? (0x15 + extra * 2 > 0x1F ? 0x1F : 0x15 + extra * 2) : 0x15);
    cpu_set_sr(cpu_get_sr() | 0x700);
    wr16(VDP_CTRL, 0x8F80);            /* auto-incremento de 128: desce uma linha de tiles */

    int cols = 0;
    do {
        SETD16(0, op_asl(rd16(A(2)), 3, 2));
        uint32_t def = A(1) + SX16(D(0));
        A(4) = def;
        /* linha: (0,2) desce uma linha de tiles; (1,3) na coluna seguinte */
        wr32(A(0) + 4, A(3));
        SETD16(2, rd16(def) + D(5)); wr16(A(0), D(2));
        SETD16(2, rd16(def + 4) + D(5)); wr16(A(0), D(2));
        A(3) += 0x20000;
        wr32(A(0) + 4, A(3));
        A(3) -= 0x20000;
        SETD16(2, rd16(def + 2) + D(5)); wr16(A(0), D(2));
        SETD16(2, rd16(def + 6) + D(5)); wr16(A(0), D(2));
        SETD16(6, op_add(D(6), 4, 2));
        A(3) += 0x40000;
        op_cmp(D(6), 0x80, 2);
        if (!CC_LT) {
            SETD16(6, op_sub(D(6), 0x80, 2));
            A(3) -= 0x800000;
        }
        A(2) += 2;
        SETD16(1, op_add(D(1), 2, 2));
        op_cmp(D(1), D(3), 2);
        if (!CC_LT) {                  /* fim da linha do mapa: dá a volta */
            SETD16(1, op_sub(D(1), D(3), 2));
            A(2) -= SX16(D(3));
        }
        cols++;
        SETD16(7, D(7) - 1);
    } while ((D(7) & 0xFFFF) != 0xFFFF);
    wr16(VDP_CTRL, 0x8F02);
    op_logic(0x8F02, 2);               /* flags do move.w #$8F02 */
    cpu_set_sr(cpu_get_sr() & 0xF8FF);
    cpu.cycles += 150 + cols * 160;
    return pop32();
}
