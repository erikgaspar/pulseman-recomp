"""Estatísticas simples de um WAV 16 bits estéreo: RMS e pico por segundo, saturação."""
import array
import math
import sys

data = open(sys.argv[1], 'rb').read()
rate = int.from_bytes(data[24:28], 'little')
s = array.array('h', data[44:])
n = len(s) // 2
print('duração %.1f s, %d Hz' % (n / rate, rate))
clip = sum(1 for v in s if v in (32767, -32768))
print('amostras saturadas: %d (%.3f%%)' % (clip, 100.0 * clip / max(1, len(s))))
for sec in range(0, n // rate):
    seg = s[sec * rate * 2:(sec + 1) * rate * 2]
    l = seg[0::2]
    r = seg[1::2]
    rms_l = math.sqrt(sum(v * v for v in l) / len(l))
    rms_r = math.sqrt(sum(v * v for v in r) / len(r))
    peak = max(abs(v) for v in seg)
    print('%3ds  rms L %6.0f  R %6.0f  pico %5d' % (sec, rms_l, rms_r, peak))
