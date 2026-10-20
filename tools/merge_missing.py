"""Junta os endereços registrados pelo runtime (missing_entries.txt) em recomp/extra_entries.txt."""
import os
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
extra = os.path.join(root, 'recomp', 'extra_entries.txt')


def read(path):
    out = set()
    if os.path.exists(path):
        for line in open(path):
            line = line.split('#')[0].strip()
            if line:
                out.add(int(line, 16))
    return out


known = read(extra)
new = set()
for p in sys.argv[1:]:
    new |= read(p)
new -= known
with open(extra, 'w', newline='\n') as f:
    f.write('# Pontos de entrada descobertos em tempo de execução (tools/merge_missing.py)\n')
    for a in sorted(known | new):
        f.write('%06X\n' % a)
print('%d novos, %d no total' % (len(new), len(known | new)))
