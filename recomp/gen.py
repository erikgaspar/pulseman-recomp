"""Recompilador estático do Pulseman (Mega Drive) para C.

Uso: py -3 recomp/gen.py <rom> <dir_saida>

Lê a ROM do usuário, descobre o código 68000 a partir da tabela de vetores
(+ recomp/extra_entries.txt) e gera:
  blocks_NN.c     funções C, uma por bloco básico
  block_table.c   tabela endereço -> função
  pulseman.lst    listagem desmontada do código descoberto
"""

import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68k import Decoder, hx  # noqa: E402

ROM_SHA1 = None  # preenchido na primeira execução bem-sucedida, ver EXPECTED_SHA1_FILE
EXPECTED_SHA1_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rom.sha1')
EXTRA_ENTRIES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'extra_entries.txt')
# Rotinas substituídas por C escrito à mão (runtime/game/): "ENDEREÇO função"
OVERRIDES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overrides.txt')
BLOCKS_PER_FILE = 1500

END_FLOWS = ('jump', 'ret', 'ijump', 'icall', 'call', 'end', 'illegal')


def load_rom(path):
    rom = open(path, 'rb').read()
    sha1 = hashlib.sha1(rom).hexdigest()
    if os.path.exists(EXPECTED_SHA1_FILE):
        expected = open(EXPECTED_SHA1_FILE).read().strip()
        if sha1 != expected:
            sys.exit('ROM inesperada: sha1 %s (esperado %s)' % (sha1, expected))
    return rom, sha1


def read_extra_entries():
    entries = set()
    if os.path.exists(EXTRA_ENTRIES_FILE):
        for line in open(EXTRA_ENTRIES_FILE):
            line = line.split('#')[0].strip()
            if line:
                entries.add(int(line, 16))
    return entries


def read_overrides():
    out = {}
    if os.path.exists(OVERRIDES_FILE):
        for line in open(OVERRIDES_FILE, encoding='utf-8'):
            line = line.split('#')[0].strip()
            if line:
                addr, fn = line.split()
                out[int(addr, 16)] = fn
    return out


def vector_entries(rom):
    vecs = struct.unpack_from('>64I', rom, 0)
    return {v & 0xFFFFFF for v in vecs[1:] if (v & 0xFFFFFF) < len(rom) and not (v & 1)}


class Program:
    def __init__(self, rom):
        self.rom = rom
        self.dec = Decoder(rom)
        self.insns = {}
        self.entries = set()
        self.indirect = []   # (addr, text) de jmp/jsr indiretos
        self.bad = set()
        self.reachable = set()
        # fim do código útil: a ROM termina com preenchimento $FF
        end = len(rom)
        while end > 0 and rom[end - 1] == 0xFF:
            end -= 1
        self.code_end = end

    def valid(self, a):
        return a % 2 == 0 and a + 2 <= len(self.rom)

    def insn(self, a):
        ins = self.insns.get(a)
        if ins is None:
            ins = self.dec.decode(a)
            self.insns[a] = ins
        return ins

    def explore(self, roots):
        work = [a for a in roots if self.valid(a)]
        self.entries.update(work)
        visited = self.reachable
        while work:
            a = work.pop()
            while self.valid(a) and a not in visited:
                visited.add(a)
                ins = self.insn(a)
                if ins.flow == 'illegal':
                    self.bad.add(a)
                    break
                for t in ins.targets:
                    if self.valid(t) and t not in self.entries:
                        self.entries.add(t)
                        work.append(t)
                if ins.flow in ('ijump', 'icall'):
                    self.indirect.append((a, ins.text))
                if ins.flow in ('call', 'icall'):
                    self.entries.add(ins.next)
                if ins.flow in ('jump', 'ret', 'ijump', 'end'):
                    break
                if ins.ends_block:
                    self.entries.add(ins.next)
                a = ins.next

    # ---------- heurísticas para saltos indiretos ----------

    def plausible_code(self, a, limit=200):
        """Caminho linear a partir de a chega a um terminador sem instrução inválida?"""
        if not self.valid(a) or a < 0x200 or a >= self.code_end:
            return False
        for _ in range(limit):
            ins = self.insn(a)
            if ins.flow == 'illegal':
                return False
            if ins.flow in ('jump', 'ret', 'ijump', 'end'):
                return True
            a = ins.next
            if not self.valid(a):
                return False
        return False

    def branch_table(self, base, stride):
        """jmp base(pc,dN.w): entradas são bra/rts/jmp em sequência."""
        found = []
        a = base
        for _ in range(256):
            if not self.valid(a):
                break
            ins = self.insn(a)
            if ins.mn not in ('bra.s', 'bra.w', 'rts', 'jmp', 'rte'):
                break
            found.append(a)
            a += stride or ins.size
        return found

    def pointer_table(self, base, max_entries=256):
        found = []
        a = base
        for _ in range(max_entries):
            if a + 4 > len(self.rom):
                break
            p = struct.unpack_from('>I', self.rom, a)[0]
            if p >> 24 or not self.plausible_code(p):
                break
            found.append(p)
            a += 4
        return found

    def heuristics(self):
        new = set()
        addrs = sorted(self.insns)
        for i, a in enumerate(addrs):
            ins = self.insns[a]
            if ins.flow == 'illegal' or a not in self.reachable:
                continue
            mn = ins.mn
            # jmp/jsr tabela(pc,dN.w): tabela de branches
            if mn in ('jmp', 'jsr') and ins.eas and ins.eas[0].kind == 'pcidx':
                prev = self.insns.get(addrs[i - 1]) if i else None
                stride = 0
                if prev is not None and prev.mn in ('lsl.w', 'asl.w', 'add.w') and prev.next == a:
                    if prev.text.startswith(('lsl.w #1', 'asl.w #1')) or prev.mn == 'add.w':
                        stride = 2
                    elif prev.text.startswith(('lsl.w #2', 'asl.w #2')):
                        stride = 4
                new.update(self.branch_table(ins.eas[0].addr, stride))
            # lea tabela,An ... movea.l d(An,Xn),Am: tabela de ponteiros
            if mn == 'lea' and ins.eas[0].kind in ('absl', 'absw', 'pcdisp'):
                an = ins.eas[1].reg if len(ins.eas) > 1 else int(ins.text[-1])
                base = ins.eas[0].addr & 0xFFFFFF
                b = ins.next
                for _ in range(6):
                    nx = self.insns.get(b)
                    if nx is None or nx.flow == 'illegal':
                        break
                    if nx.mn in ('movea.l', 'move.l') and nx.eas and nx.eas[0].kind in ('idx', 'disp', 'ind') \
                            and nx.eas[0].reg == an:
                        new.update(self.pointer_table(base + nx.eas[0].disp))
                        break
                    if nx.mn in ('jsr', 'jmp') and nx.eas and nx.eas[0].kind == 'ind' and nx.eas[0].reg == an:
                        if self.plausible_code(base):
                            new.add(base)
                        break
                    if nx.flow != 'next':
                        break
                    b = nx.next
            # move.l #rotina,<ea> / pea rotina: ponteiro para código
            if mn in ('move.l', 'pea') and ins.eas:
                e = ins.eas[0]
                v = e.value if e.kind == 'imm' else (e.addr if mn == 'pea' and e.kind in ('absl', 'absw', 'pcdisp') else None)
                if v is not None and 0x200 <= v < self.code_end and not v & 1 \
                        and (mn == 'pea' or (len(ins.eas) > 1 and ins.eas[1].kind != 'dn')):
                    if self.plausible_code(v) and not self.inside_insn(v):
                        new.add(v)
        return {a for a in new if self.valid(a)} - self.entries

    def inside_insn(self, a):
        """a cai no meio de uma instrução já decodificada (alcançável)?"""
        for back in range(2, 10, 2):
            ins = self.insns.get(a - back)
            if ins is not None and (a - back) in self.reachable and ins.size > back:
                return True
        return False

    def block(self, entry):
        """Lista de instruções do bloco que começa em entry."""
        out = []
        a = entry
        while True:
            ins = self.insn(a)
            out.append(ins)
            if ins.flow in END_FLOWS or ins.ends_block:
                break
            a = ins.next
            if a in self.entries or not self.valid(a):
                break
        return out


PATCHES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'patches.txt')


def read_patches():
    """Patches pontuais (recomp/patches.txt), neutros no modo original:
         IMM  ENDEREÇO  CONSTANTE  EXPRESSÃO_C   troca a constante no C da instrução
         HOOK ENDEREÇO  FUNÇÃO                   chama FUNÇÃO() antes da instrução
    """
    imms, hooks = {}, {}
    if os.path.exists(PATCHES_FILE):
        for line in open(PATCHES_FILE, encoding='utf-8'):
            line = line.split('#')[0].strip()
            if not line:
                continue
            kind, rest = line.split(None, 1)
            if kind == 'IMM':
                addr, const, expr = rest.split(None, 2)
                imms.setdefault(int(addr, 16), []).append((const, expr))
            elif kind == 'HOOK':
                addr, fn = rest.split()
                hooks.setdefault(int(addr, 16), []).append(fn)
            else:
                sys.exit('patches.txt: tipo desconhecido: ' + kind)
    return imms, hooks


PATCH_IMMS, PATCH_HOOKS = read_patches()


def emit_block(entry, insns):
    lines = ['uint32_t b_%06X(void)' % entry, '{']
    last = insns[-1]
    for ins in insns:
        lines.append('    /* %06X: %s */' % (ins.addr, ins.text.replace('*/', '* /')))
        body = ['INSN();']
        if ins.cycles:
            body.append('CYC(%d);' % ins.cycles)
        lines.append('    { ' + ' '.join(body))
        for fn in PATCH_HOOKS.get(ins.addr, []):
            lines.append('        %s();  /* patch */' % fn)
        code = list(ins.code)
        for const, expr in PATCH_IMMS.get(ins.addr, []):
            hits = [i for i, c in enumerate(code) if const in c]
            if len(hits) != 1:
                sys.exit('patch IMM em %06X: constante %s achada %d vezes' % (ins.addr, const, len(hits)))
            code[hits[0]] = code[hits[0]].replace(const, '(%s)' % expr)
        for c in code:
            lines.append('        ' + c)
        lines.append('    }')
    if last.flow not in END_FLOWS:
        lines.append('    return %s;' % hx(last.next))
    lines.append('}')
    return lines


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rom, sha1 = load_rom(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)

    prog = Program(rom)
    overrides = read_overrides()
    roots = vector_entries(rom) | read_extra_entries() | set(overrides)
    prog.explore(roots)
    for rnd in range(50):
        new = prog.heuristics()
        if not new:
            break
        print('heurísticas, rodada %d: %d novos pontos de entrada' % (rnd + 1, len(new)))
        prog.explore(new)

    entries = sorted(e for e in prog.entries if prog.valid(e))
    blocks = [(e, prog.block(e)) for e in entries]

    # Arquivos C
    files = []
    for i in range(0, len(blocks), BLOCKS_PER_FILE):
        name = 'blocks_%02d.c' % (i // BLOCKS_PER_FILE)
        files.append(name)
        with open(os.path.join(out, name), 'w', newline='\n') as f:
            f.write('/* Gerado por recomp/gen.py a partir da ROM do usuário. Não edite. */\n')
            f.write('#include "ops.h"\n#include "blocks.h"\n#include "game/game.h"\n\n')
            for e, insns in blocks[i:i + BLOCKS_PER_FILE]:
                f.write('\n'.join(emit_block(e, insns)))
                f.write('\n\n')
    with open(os.path.join(out, 'blocks.h'), 'w', newline='\n') as f:
        f.write('/* Gerado por recomp/gen.py. Não edite. */\n#pragma once\n#include <stdint.h>\n')
        for e, _ in blocks:
            f.write('uint32_t b_%06X(void);\n' % e)
        for fn in sorted(set(overrides.values())):
            f.write('uint32_t %s(void);\n' % fn)
    with open(os.path.join(out, 'block_table.c'), 'w', newline='\n') as f:
        f.write('/* Gerado por recomp/gen.py. Não edite. */\n#include "cpu.h"\n#include "blocks.h"\n\n')
        f.write('typedef struct { uint32_t addr; BlockFn fn; } BlockEntry;\n')
        f.write('const BlockEntry pm_blocks[] = {\n')
        for e, _ in blocks:
            f.write('    {0x%06X, %s},\n' % (e, overrides.get(e, 'b_%06X' % e)))
        f.write('};\nconst int pm_block_count = %d;\n' % len(blocks))
        # rotinas substituídas: o lockstep as compara até o rts, não por bloco
        f.write('const uint32_t pm_overrides[] = {%s};\n'
                % ', '.join(['0x%06X' % a for a in sorted(overrides)] + ['0']))
        f.write('const char pm_rom_sha1[] = "%s";\n' % sha1)
    with open(os.path.join(out, 'sources.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(files) + '\n')

    # Listagem
    with open(os.path.join(out, 'pulseman.lst'), 'w', newline='\n', encoding='utf-8') as f:
        for a in sorted(prog.insns):
            ins = prog.insns[a]
            if a in prog.entries:
                f.write('\nloc_%06X:\n' % a)
            f.write('    %06X  %-40s\n' % (a, ins.text))
    with open(os.path.join(out, 'indirect.txt'), 'w', newline='\n') as f:
        for a, t in sorted(prog.indirect):
            f.write('%06X  %s\n' % (a, t))

    code_bytes = sum(i.size for i in prog.insns.values())
    print('ROM sha1 %s' % sha1)
    print('instruções: %d (%d bytes de código), blocos: %d, saltos indiretos: %d, inválidos: %d'
          % (len(prog.insns), code_bytes, len(blocks), len(prog.indirect), len(prog.bad)))


if __name__ == '__main__':
    main()
