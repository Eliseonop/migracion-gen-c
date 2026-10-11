"""Casos sintéticos de la envolvente de un sonido (clip_motion.py: downsample_envelope,
normalize_amps, overlap_window, audio_src_at y el núcleo de sample_rms_envelope).

Los usa export_fixtures.py → tests/fixtures/motion/synthetic.json.
"""
from __future__ import annotations

import random
import struct

NAN = float("nan")
INF = float("inf")


def _ramp(n: int) -> list[tuple[float, float]]:
    return [(round(i / 20, 3), (i * 37 % 101) / 100) for i in range(n)]


# downsample_envelope(points, max_n): longitudes a ambos lados de max_n y max_n raros
# (negativo, 0, 1 → ZeroDivisionError con más de un punto).
DOWNSAMPLE = [(_ramp(n), m) for n in (0, 1, 2, 3, 23, 24, 25, 47, 48, 49, 100, 1001)
              for m in (-3, -1, 0, 1, 2, 3, 7, 24, 25)]

# normalize_amps(points): pico 0 (y −0.0, falsos), negativos, NaN delante y detrás, inf,
# por encima de 1 y el suelo de 0,06.
NORMALIZE = [
    [],
    [(0.0, 0.0)],
    [(0.0, -0.0), (0.1, 0.0)],
    [(0.0, 0.0), (0.05, 0.0), (0.1, 0.0)],
    [(0.0, 0.5)],
    [(0.0, 0.06), (0.1, 1.0), (0.2, 0.0)],
    [(0.0, 0.01), (0.1, 0.02), (0.2, 0.03)],
    [(0.0, 0.2), (0.1, 0.7), (0.2, 0.4), (0.3, 0.7)],
    [(0.0, 2.5), (0.1, 5.0), (0.2, 1.0)],
    [(0.0, -0.5), (0.1, -0.2)],
    [(0.0, -0.5), (0.1, 0.3)],
    [(0.0, NAN), (0.1, 0.5), (0.2, 1.0)],
    [(0.0, 0.5), (0.1, NAN), (0.2, 1.0)],
    [(0.0, 0.5), (0.1, 1.0), (0.2, NAN)],
    [(0.0, 0.5), (0.1, INF), (0.2, 1.0)],
    [(0.0, INF), (0.1, INF)],
    [(0.0, -INF), (0.1, 0.25)],
    [(0.0, 1e-300), (0.1, 3e-300)],
    [(0.0, 0.0564), (0.1, 0.94), (0.2, 0.9999999999999999)],
    _ramp(60),
]


def _clip(id_: str, kind: str = "video", start=0.0, in_point=0.0, out_point=1.0, speed=1.0, **extra) -> dict:
    asset = {"video": "clips", "audio": "audios", "image": "images", "text": "text", "shape": "shape"}[kind]
    c = {"id": id_, "kind": kind, "asset_kind": asset, "asset_id": id_, "track_id": "A1" if kind == "audio" else "V1",
         "filename": "" if kind == "text" else f"{id_}.mp4", "start": start, "in_point": in_point,
         "out_point": out_point, "speed": speed, "source_duration": 0.0 if kind in ("text", "image") else 30.0}
    c.update(extra)
    return c


# overlap_window(visual, audio) y audio_src_at(audio, t): cada timeline es una lista de clips;
# el exportador prueba todos los pares (visual, audio) de la misma timeline.
TIMELINES = [
    ("solape normal", [
        _clip("v1", start=1.0, out_point=4.0),
        _clip("s1", "audio", start=2.0, out_point=5.0),
        _clip("s2", "audio", start=8.0, out_point=9.0),
        _clip("s3", "audio", start=4.97, out_point=1.0),   # 0,03 s de solape
        _clip("s4", "audio", start=4.98, out_point=1.0),   # 0,02 s: no cuenta
        _clip("s5", "audio", start=4.99, in_point=0.5, out_point=0.6),
    ]),
    ("velocidades e in_point", [
        _clip("v1", start=0.0, in_point=2.0, out_point=10.0, speed=2.0),
        _clip("s1", "audio", start=0.5, in_point=1.25, out_point=3.0, speed=0.5),
        _clip("s2", "audio", start=-1.0, in_point=0.0, out_point=6.0, speed=1.5),
        _clip("s3", "video", start=3.0, in_point=0.333, out_point=7.777, speed=3.0, reverse=True),
        _clip("s4", "audio", start=0.0, in_point=0.0, out_point=2.0, speed=0.0),
    ]),
    ("texto, imagen y figura", [
        _clip("t1", "text", start=0.25, out_point=2.0, speed=4.0),
        _clip("i1", "image", start=1.5, out_point=3.0, speed=0.5),
        _clip("f1", "shape", start=0.0, out_point=1.0, speed=2.0),
        _clip("s1", "audio", start=0.1, in_point=0.7, out_point=1.9),
        _clip("s2", "audio", start=2.0, in_point=0.0, out_point=0.01),
        _clip("s3", "audio", start=0.0, in_point=0.0, out_point=0.02),  # 0,02 s justos con la figura
    ]),
    ("bordes y redondeos", [
        _clip("v1", start=0.1, out_point=0.30000000000000004),
        _clip("v2", start=1e9, out_point=1e9 + 1),
        _clip("s1", "audio", start=0.1 + 0.2, in_point=0.0, out_point=0.1),
        _clip("s2", "audio", start=1e9 + 0.5, in_point=0.123456789, out_point=10.0),
        _clip("s3", "audio", start=0.0, in_point=0.0, out_point=0.0),
    ]),
]

# Instantes de timeline para audio_src_at: relativos al inicio del audio y algunos absolutos.
SRC_OFFSETS = [-1.0, -0.001, 0.0, 0.001, 0.5, 1.0 / 3.0, 2.0, 1e6]
SRC_ABS = [0.0, 0.75, 3.5, NAN, -INF, INF]


def _f32(values) -> list[float]:
    """Valores representables en float32 (lo que da ffmpeg con -f f32le)."""
    return list(struct.unpack(f"<{len(values)}f", struct.pack(f"<{len(values)}f", *values)))


def _noise(n: int, seed: int, scale: float = 1.0) -> list[float]:
    rnd = random.Random(seed)
    return _f32([rnd.uniform(-1.0, 1.0) * scale * (1.0 + 3.0 * (i % 37 == 0)) for i in range(n)])


# Núcleo de sample_rms_envelope: (muestras a 400 Hz, duración, rate). Longitudes que cortan
# el último trozo, duraciones raras (0, NaN, negativas, menos que el audio) y rates raros.
RMS = [
    ([], 1.0, 20),
    (_f32([0.5]), 1.0, 20),
    (_f32([0.5, -0.5]), 0.0, 20),
    (_f32([0.5, -0.5]), NAN, 20),
    (_f32([0.25] * 20), 0.05, 20),
    (_f32([0.25] * 21), 0.05, 20),
    (_noise(19, 1), 1.0, 20),
    (_noise(400, 2), 1.0, 20),
    (_noise(400, 3), 1.03, 20),
    (_noise(400, 4), 0.98, 20),
    (_noise(401, 5), 1.0, 20),
    (_noise(1234, 6), 3.085, 20),
    (_noise(1234, 7), 10.0, 20),
    (_noise(1500, 8, 1e-3), 4.0, 20),
    (_noise(1500, 9, 3e4), 4.0, 20),
    (_noise(237, 10), 0.6, 1),
    (_noise(237, 11), 0.6, 0),
    (_noise(237, 12), 0.6, -5),
    (_noise(237, 13), 0.6, 3),
    (_noise(237, 14), 0.6, 7),
    (_noise(237, 15), 0.6, 400),
    (_noise(237, 16), 0.6, 1000),
    (_noise(100, 17), -2.0, 20),
    (_f32([0.0] * 300), 0.75, 20),
    (_f32([0.0] * 150 + [0.9] * 10 + [0.0] * 140), 0.75, 20),
    (_f32([1e-30, 1e30, 3e38, -3e38] * 25), 0.25, 20),
    (_f32([1.0, INF, 0.5] * 30), 0.25, 20),
    # Trozos finitos y luego uno con infinito: la compensación (NaN) no se suma y el pico es inf.
    (_f32([0.5] * 45 + [INF] + [0.25] * 34), 0.2, 20),
    (_f32([0.5, NAN, 0.5] * 30), 0.25, 20),
    (_f32([1e16, 1.0, -1e16, 1.0] * 50), 0.5, 20),
    (_f32([0.1] * 1500), 4.0, 20),
]
