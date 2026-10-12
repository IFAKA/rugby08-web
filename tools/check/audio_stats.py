#!/usr/bin/env python3
"""Stats of a 16-bit PCM WAV (rugby08 --audio-capture). One summary line.
  tools/check/audio_stats.py FILE.wav [--not-silent]
--not-silent: exit 1 unless some 100 ms window has RMS above -50 dBFS (startup silence is fine)."""

import math
import sys
import wave

path = sys.argv[1]
need_sound = "--not-silent" in sys.argv[2:]
try:
    w = wave.open(path, "rb")
except (OSError, wave.Error, EOFError) as e:
    print(f"audio_stats: FAIL ({path}: {e})")
    sys.exit(1)
ch, rate, width, n = w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes()
if width != 2:
    print(f"audio_stats: FAIL ({path}: {8 * width}-bit, expected 16)")
    sys.exit(1)
raw = w.readframes(n)
samples = memoryview(raw).cast("h")
win = max(1, rate // 10) * ch
best, peak = 0.0, 0
for i in range(0, len(samples), win):
    chunk = samples[i : i + win]
    if not len(chunk):
        break
    rms = math.sqrt(sum(s * s for s in chunk) / len(chunk))
    best = max(best, rms)
    peak = max(peak, max(abs(s) for s in chunk))
db = 20 * math.log10(best / 32768) if best else float("-inf")
ok = not need_sound or db > -50
print(
    f"audio_stats: {'ok' if ok else 'FAIL'} ({n / rate if rate else 0:.1f} s, {ch} ch {rate} Hz, loudest 100 ms {db:.1f} dBFS, peak {peak})"
)
sys.exit(0 if ok else 1)
