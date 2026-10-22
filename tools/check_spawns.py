"""Confere a ativação de objetos no início de cada mapa: no widescreen tem de ativar tudo o
que o --original ativa (objeto faltando = chefe/monitor que não aparece, jogo travado).

Uso: py tools/check_spawns.py [EXE] [ULTIMO_MAPA]
     (padrão: build/Release/pulseman.exe, mapas 2..72; ROM em ../Pulseman_Japan.md)
Entra na fase 1 pela seleção de fase e troca o mapa com PM_DEBUG_MAP."""
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(root, 'build', 'Release', 'pulseman.exe')
last_map = int(sys.argv[2]) if len(sys.argv) > 2 else 72
rom = os.path.join(os.path.dirname(root), 'Pulseman_Japan.md')
pat = re.compile(r'ativa quadro (\d+): .*absoluto (-?\d+),(-?\d+) tipo ([0-9A-F]+) bit (\d+)')

# START no título (novo jogo) e START na seleção de fase (fase 1)
script = tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False)
script.write('0\n400 S\n404\n1250 S\n1254\n')
script.close()


def run(m, original):
    env = dict(os.environ, PM_DEBUG_MAP=str(m), PM_DEBUG_SPAWN='1')
    args = [exe, rom, '--headless', '--mute', '--frames', '1900', '--input', script.name]
    if original:
        args.append('--original')
    p = subprocess.run(args, env=env, cwd=os.path.dirname(exe), capture_output=True, text=True,
                       encoding='utf-8', errors='replace', timeout=600)
    load, acts = None, set()
    for line in p.stderr.splitlines():
        if line.startswith('mapa ') and load is None:
            load = int(line.split('quadro ')[1].split()[0])
        mm = pat.search(line)
        if mm and load is not None and int(mm.group(1)) <= load + 10:   # a varredura pode levar 2 quadros
            acts.add((int(mm.group(2)), int(mm.group(3)), mm.group(4), int(mm.group(5))))
    return load, acts


def check(m):
    lw, wide = run(m, False)
    lo, orig = run(m, True)
    return m, lw, lo, sorted(orig - wide)


bad = 0
with ThreadPoolExecutor(8) as ex:
    for m, lw, lo, missing in ex.map(check, range(2, last_map + 1)):
        if lw is None or lo is None:
            print('mapa %d: não carregou' % m)
            bad += 1
        elif missing:
            print('mapa %d: faltam no widescreen (coluna, linha, tipo, bit): %s' % (m, missing))
            bad += 1
os.unlink(script.name)
print('%d mapa(s) com problema' % bad if bad else 'ok: mapas 2..%d' % last_map)
sys.exit(1 if bad else 0)
