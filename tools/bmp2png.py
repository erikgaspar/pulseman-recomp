"""Converte BMP 24 bits (capturas do runtime) em PNG, sem dependências."""
import struct
import sys
import zlib


def convert(src, dst):
    data = open(src, 'rb').read()
    off, = struct.unpack_from('<I', data, 10)
    w, h = struct.unpack_from('<ii', data, 18)
    row = (w * 3 + 3) & ~3
    raw = bytearray()
    for y in range(h):
        line = data[off + (h - 1 - y) * row: off + (h - 1 - y) * row + w * 3]
        raw.append(0)
        for x in range(w):
            b, g, r = line[x * 3:x * 3 + 3]
            raw += bytes((r, g, b))

    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)


if __name__ == '__main__':
    for p in sys.argv[1:]:
        convert(p, p[:-4] + '.png')
