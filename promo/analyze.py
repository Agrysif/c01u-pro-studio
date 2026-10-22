"""Per-frame data of the promo track for the visuals (60 fps): loudness, the 64-band spectrum laid
out exactly like the panel's analyzer (30 Hz - 20 kHz, log spaced) and onset strength.
   python promo/analyze.py promo/music/c01u-suno.mp3 promo/music/track.json"""
import json, subprocess, sys
import numpy as np

SR, FPS, N = 44100, 60, 4096
HOP = SR // FPS  # 735 samples: one analysis frame per video frame
src, dst = sys.argv[1], sys.argv[2]
raw = subprocess.run(["ffmpeg", "-v", "error", "-i", src, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                     capture_output=True, check=True).stdout
x = np.frombuffer(raw, dtype=np.float32).astype(np.float64)
x = np.concatenate([np.zeros(N // 2), x, np.zeros(N)])
frames = int((len(x) - N) // HOP)
win = np.hanning(N)
freqs = np.fft.rfftfreq(N, 1 / SR)
edges = 30 * (20000 / 30) ** (np.arange(65) / 64)
level, spec, flux, prev = [], [], [], None
for i in range(frames):
    seg = x[i * HOP:i * HOP + N]
    mag = np.abs(np.fft.rfft(seg * win))
    p = mag ** 2 / (N * N / 4)
    level.append(10 * np.log10(p.sum() + 1e-12))
    bands = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (freqs >= lo) & (freqs < hi)
        if not m.any():  # narrow low bands: take the nearest bin
            m = np.zeros_like(freqs, dtype=bool)
            m[np.argmin(abs(freqs - (lo + hi) / 2))] = True
        bands.append(10 * np.log10(p[m].mean() + 1e-14))
    spec.append(bands)
    lg = np.log1p(mag * 20)
    flux.append(0 if prev is None else float(np.maximum(0, lg - prev).sum()))
    prev = lg
spec = np.array(spec)
spec += -16 - np.percentile(spec, 99.5)  # loudest moments sit near the top of the panel's scale
flux = np.array(flux)
flux = flux / (np.percentile(flux, 99) + 1e-9)
json.dump({"fps": FPS, "duration": len(x[N // 2:-N]) / SR,
           "level": np.round(level, 1).tolist(),
           "spec": np.round(spec, 1).tolist(),
           "flux": np.round(np.clip(flux, 0, 2), 3).tolist()}, open(dst, "w"))
print(f"{frames} frames -> {dst}")
