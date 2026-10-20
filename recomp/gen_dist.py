"""Tabelas da versão distribuível (sem código do jogo): python gen_dist.py SAÍDA_DIR

Não lê a ROM. Na versão distribuível o código do jogo roda no interpretador (Musashi)
direto da ROM do jogador; só entram as nossas rotinas em C (overrides.txt), os ajustes de
constantes (IMM de patches.txt, aplicados na ROM em memória por runtime/dist.c) e os
ganchos (HOOK de patches.txt, chamados antes da instrução). O SHA-1 esperado vem de rom.sha1.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def read_overrides():
    out = []
    for line in open(os.path.join(HERE, 'overrides.txt'), encoding='utf-8'):
        line = line.split('#')[0].strip()
        if line:
            addr, fn = line.split()
            out.append((int(addr, 16), fn))
    return sorted(out)


def read_patches():
    imms, hooks = [], []
    for line in open(os.path.join(HERE, 'patches.txt'), encoding='utf-8'):
        line = line.split('#')[0].strip()
        if not line:
            continue
        kind, rest = line.split(None, 1)
        if kind == 'IMM':
            addr, const, expr = rest.split(None, 2)
            imms.append((int(addr, 16), int(const, 0), expr))
        elif kind == 'HOOK':
            addr, fn = rest.split()
            hooks.append((int(addr, 16), fn))
        else:
            sys.exit('patches.txt: tipo desconhecido: ' + kind)
    return imms, hooks


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    sha1 = open(os.path.join(HERE, 'rom.sha1')).read().strip().lower()
    overrides = read_overrides()
    imms, hooks = read_patches()
    lines = [
        '/* Gerado por recomp/gen_dist.py (sem a ROM). Não edite. */',
        '#include <stdint.h>',
        '#include "cpu.h"',
        '#include "dist.h"',
        '#include "game/game.h"',
        '',
    ]
    for fn in sorted(set(f for _, f in overrides)):
        lines.append('uint32_t %s(void);' % fn)
    for fn in sorted(set(f for _, f in hooks)):
        lines.append('void %s(void);' % fn)
    lines += ['', 'typedef struct { uint32_t addr; BlockFn fn; } BlockEntry;', 'const BlockEntry pm_blocks[] = {']
    lines += ['    {0x%06X, %s},' % (a, f) for a, f in overrides]
    lines += ['};', 'const int pm_block_count = %d;' % len(overrides),
              'const uint32_t pm_overrides[] = {%s};' % ', '.join(['0x%06X' % a for a, _ in overrides] + ['0']),
              'const char pm_rom_sha1[] = "%s";' % sha1, '']
    for i, (_, _, expr) in enumerate(imms):
        lines.append('static int imm_%d(void) { return (int)(%s); }' % (i, expr))
    lines += ['', 'const DistImm dist_imms[] = {']
    lines += ['    {0x%06X, 0x%X, imm_%d},' % (a, c, i) for i, (a, c, _) in enumerate(imms)]
    lines += ['    {0, 0, 0},', '};', 'const DistHook dist_hooks[] = {']
    lines += ['    {0x%06X, %s},' % (a, f) for a, f in hooks]
    lines += ['    {0, 0},', '};', '']
    with open(os.path.join(out_dir, 'dist_table.c'), 'w', newline='\n', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print('gen_dist: %d rotinas em C, %d constantes, %d ganchos' % (len(overrides), len(imms), len(hooks)))


if __name__ == '__main__':
    main()
