"""Casos de la caché LRU con tope en bytes (lib/lruBytes.js de video-yt) para lru_js.mjs.

Cada caso: {name, maxBytes (None = el de por defecto, 64 MiB), ops: [[op, ...args]]}. Las claves
son texto y los valores, texto o números (en JS, `set` con el mismo valor que ya tenía la clave no
avisa del descarte: se compara con ===).
"""
from __future__ import annotations

import random


def _random_ops(seed: int, n: int, keys: int, max_bytes: int) -> list[list]:
    rnd = random.Random(seed)
    pool = [f"k{i}" for i in range(keys)]
    ops: list[list] = []
    for _ in range(n):
        r = rnd.random()
        k = rnd.choice(pool)
        if r < 0.45:
            # A veces el mismo valor que podría tener ya (no es un descarte) y a veces otro.
            ops.append(["set", k, rnd.choice([k, f"{k}-{rnd.randrange(3)}", rnd.randrange(4)]),
                        rnd.choice([0, 1, rnd.randrange(max_bytes + 1), max_bytes, max_bytes + rnd.randrange(1, 20)])])
        elif r < 0.65:
            ops.append(["get", k])
        elif r < 0.70:
            ops.append(["peek", k])
        elif r < 0.75:
            ops.append(["has", k])
        elif r < 0.85:
            ops.append(["resize", k, rnd.randrange(max_bytes + 10)])
        elif r < 0.93:
            ops.append(["delete", k])
        elif r < 0.98:
            ops.append(["purge", rnd.sample(pool, rnd.randrange(len(pool) + 1))])
        else:
            ops.append(["clear"])
    return ops


def cases() -> list[dict]:
    out = [
        # Los de lruBytes.test.mjs.
        {"name": "descarta las más antiguas", "maxBytes": 100,
         "ops": [["set", "a", 1, 40], ["set", "b", 2, 40], ["get", "a"], ["set", "c", 3, 40]]},
        {"name": "una entrada sola mayor que el tope se conserva", "maxBytes": 10,
         "ops": [["set", "x", "big", 50], ["set", "y", "big2", 50]]},
        {"name": "reemplazar no cuenta dos veces", "maxBytes": 1000,
         "ops": [["set", "k", "v1", 10], ["set", "k", "v2", 30], ["set", "k", "v2", 30]]},
        {"name": "purge y resize", "maxBytes": 100,
         "ops": [["set", "clip1", "a", 10], ["set", "clip2", "b", 10], ["set", "clip3", "c", 10],
                 ["purge", ["clip1", "clip3"]], ["resize", "clip3", 95], ["clear"]]},
        # Bordes.
        {"name": "tope 0: solo se queda la última", "maxBytes": 0,
         "ops": [["set", "a", 1, 0], ["set", "b", 2, 0], ["set", "c", 3, 5], ["set", "d", 4, 0],
                 ["resize", "c", 0], ["set", "e", 5, 1]]},
        {"name": "tope por defecto", "maxBytes": None,
         "ops": [["set", "a", 1, 64 * 1024 * 1024], ["set", "b", 2, 1], ["get", "a"], ["set", "c", 3, 1]]},
        {"name": "lo que no existe", "maxBytes": 50,
         "ops": [["get", "nada"], ["peek", "nada"], ["has", "nada"], ["resize", "nada", 99], ["delete", "nada"],
                 ["purge", []], ["clear"], ["set", "a", "x", 10], ["peek", "a"], ["purge", []]]},
        {"name": "peek no cambia el orden y get sí", "maxBytes": 30,
         "ops": [["set", "a", 1, 10], ["set", "b", 2, 10], ["set", "c", 3, 10], ["peek", "a"], ["set", "d", 4, 10],
                 ["get", "b"], ["set", "e", 5, 10]]},
        {"name": "resize que crece descarta otras y la propia no", "maxBytes": 30,
         "ops": [["set", "a", 1, 10], ["set", "b", 2, 10], ["set", "c", 3, 10], ["resize", "a", 25],
                 ["resize", "a", 40], ["set", "b", 2, 5]]},
        {"name": "set con el mismo valor reordena y recalcula", "maxBytes": 30,
         "ops": [["set", "a", "v", 10], ["set", "b", "w", 10], ["set", "a", "v", 25], ["set", "c", "x", 1]]},
    ]
    for seed, (n, keys, mx) in enumerate([(300, 6, 100), (300, 12, 64), (200, 4, 10), (200, 8, 1)]):
        out.append({"name": f"al azar {seed}", "maxBytes": mx, "ops": _random_ops(1000 + seed, n, keys, mx)})
    return out
