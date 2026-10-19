"""Señales sintéticas para los beats (beats.py) en export_fixtures.py.

Cada caso lleva los golpes ya calculados para que el C++ sintetice la misma señal sin repetir
la lógica: ruido LCG de semilla fija y, por golpe, un bombo (seno de 60 Hz con caída
exp(−30τ) durante 0,25 s) o un charles (el mismo ruido con caída exp(−80τ) durante 0,05 s).
"""
import numpy as np

SR = 22050


def lcg(seed: int, n: int) -> np.ndarray:
    """Ruido determinista en [−1, 1): x ← 1664525·x + 1013904223 (mod 2³²)."""
    out = np.empty(n)
    x = seed & 0xFFFFFFFF
    for i in range(n):
        x = (1664525 * x + 1013904223) & 0xFFFFFFFF
        out[i] = x / 2**31 - 1
    return out


def synth(case: dict) -> np.ndarray:
    n = case["samples"]
    noise = lcg(case["seed"], n)
    y = case["noise"] * noise
    for t, kind, amp in case["hits"]:
        s = int(round(t * SR))
        if s >= n:
            continue
        span = min(n - s, int(SR * (0.25 if kind == "kick" else 0.05)))
        tau = np.arange(span) / SR
        if kind == "kick":
            y[s:s + span] += amp * np.sin(2 * np.pi * 60 * tau) * np.exp(-30 * tau)
        else:
            y[s:s + span] += amp * noise[s:s + span] * np.exp(-80 * tau)
    return y.astype(np.float32)


def pattern(bpm: float, start: float, end: float, hat: bool = False, accent: bool = False) -> list:
    beat = 60.0 / bpm
    hits, t, i = [], start, 0
    while t < end:
        hits.append([round(t, 6), "kick", 0.9 if accent and i % 4 == 0 else 0.6])
        if hat:
            hits.append([round(t + beat / 2, 6), "hat", 0.3])
        t += beat
        i += 1
    return hits


def case(name: str, seconds: float, hits: list, noise: float = 0.01, seed: int = 1, samples: int | None = None) -> dict:
    return {"name": name, "samples": samples or int(round(seconds * SR)), "noise": noise, "seed": seed, "hits": hits}


CASES = [
    case("120 BPM, bombo", 12, pattern(120, 0.3, 12)),
    case("90 BPM, bombo y charles", 15, pattern(90, 0.2, 15, hat=True), noise=0.02, seed=7),
    case("174 BPM", 10, pattern(174, 0.1, 10), seed=3),
    case("128 BPM con silencio antes y después", 14, pattern(128, 2, 12), seed=11),
    case("100 BPM con acentos", 12, pattern(100, 0.5, 12, accent=True), seed=5),
    case("60 BPM", 16, pattern(60, 0.4, 16), seed=9),
    case("140 BPM con charles", 8, pattern(140, 0, 8, hat=True), noise=0.05, seed=13),
    case("silencio", 5, [], noise=0.0),
    case("solo ruido", 6, [], noise=0.3, seed=21),
    case("demasiado corto", 0, [], samples=1000),
    case("un fotograma", 0, [[0.01, "kick", 0.8]], samples=2048 + 100),
    case("medio segundo", 0, pattern(120, 0, 0.5), samples=int(0.4 * SR)),
]
