"""Gera o ícone do executável (.ico) a partir de um PNG quadrado.

    py tools/make_icon.py icon/icone.png icon/pulseman.ico

Só Python puro: lê o PNG (8 bits, sem entrelaçamento), reduz para cada tamanho que o
Windows usa (média da área; o 256 é ampliado com interpolação), arredonda os cantos e
grava o .ico com cada tamanho em PNG (aceito desde o Windows Vista).
"""
import struct
import sys
import zlib

SIZES = [16, 24, 32, 48, 64, 128, 256]


def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'não é PNG'
    pos, idat = 8, b''
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype in (2, 6) and not interlace, 'PNG RGB/RGBA de 8 bits'
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    ch = 4 if ctype == 6 else 3
    raw, stride = zlib.decompress(idat), w * ch
    px, prev = [], bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b, c = prev[i], prev[i - ch] if i >= ch else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        px.append([tuple(line[x * ch:x * ch + 3]) + ((line[x * ch + 3],) if ch == 4 else (255,)) for x in range(w)])
        prev = line
    return w, h, px


def resize(src, sw, sh, n):
    out = []
    for y in range(n):
        row = []
        for x in range(n):
            if n <= sw:   # redução: média da área de origem
                x0, x1 = x * sw // n, max(x * sw // n + 1, (x + 1) * sw // n)
                y0, y1 = y * sh // n, max(y * sh // n + 1, (y + 1) * sh // n)
                acc = [0, 0, 0, 0]
                for yy in range(y0, y1):
                    for xx in range(x0, x1):
                        for k in range(4):
                            acc[k] += src[yy][xx][k]
                cnt = (x1 - x0) * (y1 - y0)
                row.append(tuple(v // cnt for v in acc))
            else:         # ampliação: bilinear
                fx, fy = (x + 0.5) * sw / n - 0.5, (y + 0.5) * sh / n - 0.5
                ix, iy = max(0, min(sw - 2, int(fx))), max(0, min(sh - 2, int(fy)))
                tx, ty = min(max(fx - ix, 0), 1), min(max(fy - iy, 0), 1)
                p = [src[iy][ix], src[iy][ix + 1], src[iy + 1][ix], src[iy + 1][ix + 1]]
                row.append(tuple(int(p[0][k] * (1 - tx) * (1 - ty) + p[1][k] * tx * (1 - ty) +
                                     p[2][k] * (1 - tx) * ty + p[3][k] * tx * ty + 0.5) for k in range(4)))
        out.append(row)
    return out


def round_corners(img, n):
    """Cantos arredondados (raio de 18%), com borda suave."""
    r = n * 0.18
    for y in range(n):
        for x in range(n):
            cx = min(max(x + 0.5, r), n - r)
            cy = min(max(y + 0.5, r), n - r)
            d = ((x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2) ** 0.5
            cover = min(max(r - d + 0.5, 0), 1)
            if cover < 1:
                p = img[y][x]
                img[y][x] = (p[0], p[1], p[2], int(p[3] * cover))
    return img


def png_bytes(img, n):
    raw = b''.join(b'\0' + bytes(v for p in row for v in p) for row in img)
    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', n, n, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    src_path, out_path = sys.argv[1], sys.argv[2]
    w, h, px = read_png(src_path)
    if w != h:   # corta o centro para ficar quadrado
        s = min(w, h)
        x0, y0 = (w - s) // 2, (h - s) // 2
        px = [row[x0:x0 + s] for row in px[y0:y0 + s]]
        w = h = s
    images = [png_bytes(round_corners(resize(px, w, h, n), n), n) for n in SIZES]
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b''
    for n, img in zip(SIZES, images):
        entries += struct.pack('<BBBBHHII', n % 256, n % 256, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    open(out_path, 'wb').write(header + entries + b''.join(images))
    print('%s: %s' % (out_path, ', '.join('%dx%d' % (n, n) for n in SIZES)))


if __name__ == '__main__':
    main()
