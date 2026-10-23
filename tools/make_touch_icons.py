# Gera runtime/touch_icons.c: os ícones dos botões de toque (assets/touch/*.svg) em PNG de
# 256 px, branco com transparência, embutidos no executável.
#
#   py tools/make_touch_icons.py
#
# Os SVGs são rasterizados pelo Microsoft Edge em modo headless (branco sobre preto; o brilho
# vira a transparência). O .c gerado fica no repositório: o build não precisa do Edge.
#
# Ícones: pulo = arrow-fat-up (Phosphor Icons, MIT); ataque = punch-blast e voltteccer =
# unstable-orb (Lorc, game-icons.net, CC BY 3.0).
import os, struct, subprocess, sys, tempfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 256
ICONS = [('jump', 'pulo'), ('attack', 'ataque'), ('volt', 'voltteccer')]
EDGE = [r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
        r'C:\Program Files\Microsoft\Edge\Application\msedge.exe']


def read_png(path):
    d = open(path, 'rb').read()
    i, idat = 8, b''
    while i < len(d):
        n = struct.unpack('>I', d[i:i + 4])[0]
        t, c = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        if t == b'IHDR':
            w, h, depth, ct = struct.unpack('>IIBB', c[:10])
        elif t == b'IDAT':
            idat += c
        i += 12 + n
    assert depth == 8 and ct in (2, 6)
    raw, bpp = zlib.decompress(idat), 4 if ct == 6 else 3
    st, p, prev, rows = w * bpp, 0, bytearray(w * bpp), []
    for _ in range(h):
        f, line = raw[p], bytearray(raw[p + 1:p + 1 + st])
        p += 1 + st
        for x in range(st):
            a = line[x - bpp] if x >= bpp else 0
            b, c = prev[x], prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line[x * bpp] for x in range(w)))   # canal vermelho = brilho
        prev = line
    return w, h, rows


def write_alpha_png(alpha_rows):
    """PNG cinza com alfa (tipo 4): cinza 255, alfa = brilho"""
    raw = b''.join(b'\x00' + b''.join(bytes((255, a)) for a in row) for row in alpha_rows)
    def chunk(t, data):
        return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 4, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def render(svg_path, out_png):
    svg = open(svg_path, encoding='utf-8').read()
    svg = svg.replace('<svg ', '<svg width="%d" height="%d" ' % (SIZE, SIZE), 1)
    html = ('<!doctype html><html><body style="margin:0;background:#000;color:#fff">'
            '<div style="width:%dpx;height:%dpx">%s</div></body></html>' % (SIZE, SIZE, svg))
    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, 'icon.html')
        open(page, 'w', encoding='utf-8').write(html)
        edge = next(p for p in EDGE if os.path.exists(p))
        subprocess.run([edge, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
                        '--window-size=600,600', f'--screenshot={out_png}', 'file:///' + page.replace('\\', '/')],
                       check=True, capture_output=True)


def main():
    out = ['/* Gerado por tools/make_touch_icons.py a partir de assets/touch/*.svg: não editar.',
           ' * pulo: arrow-fat-up (Phosphor Icons, MIT); ataque: punch-blast e voltteccer: unstable-orb',
           ' * (Lorc, game-icons.net, CC BY 3.0). PNG de %d px, branco com transparência. */' % SIZE,
           '#include "touch_icons.h"', '']
    with tempfile.TemporaryDirectory() as tmp:
        for name, src in ICONS:
            shot = os.path.join(tmp, name + '.png')
            render(os.path.join(ROOT, 'assets', 'touch', src + '.svg'), shot)
            w, h, rows = read_png(shot)
            png = write_alpha_png([row[:SIZE] for row in rows[:SIZE]])
            out.append('const unsigned char touch_icon_%s[%d] = {' % (name, len(png)))
            for i in range(0, len(png), 20):
                out.append('    ' + ', '.join('0x%02X' % b for b in png[i:i + 20]) + ',')
            out.append('};')
            out.append('const unsigned int touch_icon_%s_size = sizeof(touch_icon_%s);' % (name, name))
            out.append('')
            print('%s: %d bytes' % (name, len(png)))
    open(os.path.join(ROOT, 'runtime', 'touch_icons.c'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out))


if __name__ == '__main__':
    main()
