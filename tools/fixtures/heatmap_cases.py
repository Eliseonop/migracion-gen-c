"""Casos del heatmap de YouTube (heatmap._pick_segments) para export_fixtures.py.

Cada caso: (heatmap, duration, min_score, max_clips, max_duration, padding). Los heatmaps
largos imitan los de yt-dlp (100 puntos iguales a lo largo del vídeo) con un generador fijo.
"""
from __future__ import annotations

import math
import random


def _pt(a, b, v):
    return {"start_time": a, "end_time": b, "value": v}


def _yt_like(seed: int, duration: float, n: int = 100) -> list[dict]:
    rnd = random.Random(seed)
    step = duration / n
    out = []
    for i in range(n):
        base = 0.25 + 0.2 * math.sin(i / 7.0 + seed) + rnd.uniform(-0.15, 0.15)
        if rnd.random() < 0.08:
            base += rnd.uniform(0.3, 0.6)          # picos
        out.append(_pt(i * step, (i + 1) * step, round(min(1.0, max(0.0, base)), rnd.choice([3, 4, 17]))))
    return out


SMALL = [_pt(0, 10, 0.2), _pt(10, 20, 0.5), _pt(20, 30, 0.9), _pt(30, 40, 0.3), _pt(40, 50, 0.6),
         _pt(50, 60, 0.6), _pt(60, 70, 0.1)]

CASES = [
    ([], 100.0, 0.4, 10, 60, 2),
    ([_pt(0, 5, 0.1)], 100.0, 0.4, 10, 60, 2),                       # nada supera el umbral
    ([_pt(3, 5, 0.4)], 100.0, 0.4, 10, 60, 2),                       # igual al umbral: entra
    ([_pt(0.0, 1.5, 1.0)], 1.0, 0.4, 10, 60, 2),                     # se sale de la duración
    (SMALL, 70.0, 0.4, 10, 60, 0),
    (SMALL, 70.0, 0.4, 10, 60, 3),
    (SMALL, 70.0, 0.4, 1, 60, 3),                                    # solo el mejor
    (SMALL, 70.0, 0.4, 0, 60, 3),                                    # [:0]
    (SMALL, 70.0, 0.4, -1, 60, 3),                                   # [:-1] quita el peor
    (SMALL, 70.0, 0.4, -5, 60, 3),
    (SMALL, 70.0, 0.4, 10, 15, 3),                                   # recorta a 15 s
    (SMALL, 70.0, 0.4, 10, 0, 3),                                    # duración máxima 0
    (SMALL, 70.0, 0.0, 10, 60, 1),                                   # todo es un tramo
    (SMALL, 70.0, 1.0, 10, 60, 1),
    (SMALL, 25.0, 0.4, 10, 60, 50),                                  # fin antes del inicio con padding
    (list(reversed(SMALL)), 70.0, 0.4, 10, 60, 2),                   # desordenado
    ([_pt(10, 20, 0.5), _pt(10, 15, 0.9), _pt(5, 10, 0.1), _pt(10, 12, 0.2)], 60.0, 0.4, 10, 60, 1),  # empates de inicio
    ([_pt(0, 1, 0.7), _pt(1, 2, 0.1), _pt(2, 3, 0.7), _pt(3, 4, 0.1), _pt(4, 5, 0.7)], 5.0, 0.5, 2, 60, 0),  # empate de score
    ([_pt(0, 1, 0.7), _pt(1, 2, 0.1), _pt(2, 3, 0.7), _pt(3, 4, 0.1), _pt(4, 5, 0.7)], 5.0, 0.5, 2, 60, 1),
    ([_pt(0.125, 0.375, 0.55555), _pt(0.375, 0.625, 0.66665), _pt(0.625, 1.005, 0.12345)], 2.0, 0.5, 10, 60, 0),
    ([_pt(1.005, 2.675, 0.123456789)], 10.0, 0.1, 10, 60, 0),        # redondeos con empate decimal
    ([_pt(-3, 2, 0.8)], 10.0, 0.5, 10, 60, 0),                       # inicio negativo
    ([_pt(0, 3, 0.8), _pt(3, 6, 0.8)], 6.0, 0.5, 10, 60, -2),        # padding negativo
]
for seed, dur in [(1, 212.0), (2, 634.33), (3, 59.9), (4, 3600.0), (5, 1234.567)]:
    hm = _yt_like(seed, dur)
    CASES += [(hm, dur, 0.4, 10, 60, 2), (hm, dur, 0.55, 3, 30, 0), (hm, dur, 0.3, 5, 20, 5)]
