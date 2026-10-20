/* Sprites do HUD de vidas e pontuação (sub-rotinas $87C2 e $87EC).
 *
 * A rotina do HUD ($8664-$87B6) chama estas duas para gravar cada sprite no buffer
 * de sprites (a0). A posição vem empacotada em d1: X na palavra alta, Y na baixa;
 * depois de cada sprite, X avança 8 px. $FFFB66 conta os sprites (campo link).
 *
 * Com hud_shift = 0 reproduzem o original (o lockstep confere). No widescreen, o X
 * gravado vai para a borda do seu lado. As margens originais são assimétricas (energia
 * a 16 px da esquerda, pontuação a 48 px da direita), por isso a direita anda mais.
 */
#include "ops.h"
#include "runtime.h"

int hud_shift = 0;        /* px que o grupo da esquerda (energia, vidas) vai para a esquerda */
int hud_shift_right = 0;  /* px que o grupo da direita (pontuação) vai para a direita */

#define SPRITE_COUNT 0xFFFB66   /* b: número de sprites já gravados no quadro */

static uint32_t hud_x(uint32_t x)
{
    x &= 0xFFFF;
    if (!hud_shift)
        return x;
    /* X do sprite tem 128 somado: metade esquerda da tela original vai para a esquerda */
    return x < 0x80 + 160 ? x - hud_shift : x + hud_shift_right;
}

static uint32_t swap32(uint32_t v)
{
    return (v >> 16) | (v << 16);
}

/* Grava X do sprite, incrementa o contador e avança X em 8 (comum às duas rotinas). */
static uint32_t finish_sprite(void)
{
    D(1) = swap32(D(1));
    wr16(A(0), hud_x(D(1))); A(0) += 2;
    wr8(SPRITE_COUNT, op_add(rd8(SPRITE_COUNT), 1, 1));
    SETD16(1, op_add(D(1), 8, 2));
    D(1) = op_logic(swap32(D(1)), 4);
    cpu.cycles += 100;
    return pop32();
}

/* $87C2: sprite 1x1. d2 = tile - 1 (incrementado aqui), d1 = X:Y. */
uint32_t ov_0087C2(void)
{
    D(3) = op_logic(0, 4);
    SETD16(2, op_add(D(2), 1, 2));
    wr16(A(0), D(1)); A(0) += 2;                 /* Y */
    SETD8(3, rd8(SPRITE_COUNT));
    wr16(A(0), D(3)); A(0) += 2;                 /* tamanho 1x1 + link */
    SETD16(2, D(2) | 0x8000);
    wr16(A(0), D(2)); A(0) += 2;                 /* tile, prioridade alta */
    return finish_sprite();
}

/* $87EC: sprite de tamanho d0 (bits de tamanho já deslocados). d2 = tile, d1 = X:Y. */
uint32_t ov_0087EC(void)
{
    D(3) = op_logic(0, 4);
    wr16(A(0), D(1)); A(0) += 2;                 /* Y */
    SETD8(3, rd8(SPRITE_COUNT));
    SETD16(3, D(3) | D(0));
    wr16(A(0), D(3)); A(0) += 2;                 /* tamanho + link */
    SETD16(2, D(2) | 0x8000);
    wr16(A(0), D(2)); A(0) += 2;                 /* tile, prioridade alta */
    return finish_sprite();
}
