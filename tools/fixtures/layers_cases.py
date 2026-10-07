"""Casos sintéticos de paridad de las CAPAS de un instante (gc_core layers ↔ layersAt de
frontend/src/lib/clipLayout.js, y stack_layers/buried_text_track_ids de track_stack.py).

Cada caso es una timeline y unos instantes. El layersAt del preview (margen de 0,02 s,
orden por capa e índice) lo ejecuta Node sobre el JS real; la pila y las pistas de texto
enterradas salen del Python. Las duraciones van en milisegundos y las figuras a
velocidad 1, porque el clipEnd de JS no redondea y trata las figuras como velocidad 1.
"""
from __future__ import annotations

from ops_cases import clip, track

_STACK = [track("V1", "video"), track("T1", "text"), track("V2", "video"), track("A1", "audio"),
          track("T2", "text")]

CASES = [
    {"name": "todos los tipos en la pila", "tracks": _STACK, "clips": [
        clip("v", "V1", start=0, out=4),
        clip("img", "V2", "image", start=1, out=2),
        clip("sh", "V2", "shape", start=3.5, out=1),
        clip("mo", "V1", "motion", start=4, out=2, asset_kind="shape"),
        clip("adj", "V2", "adjustment", start=0.5, out=4, asset_kind="shape"),
        clip("t1", "T1", "text", start=0, out=3),
        clip("t2", "T2", "text", start=2, out=3),
        clip("au", "A1", "audio", start=0, out=6),
        clip("raro", "V1", "sticker", start=0, out=6),
    ], "heads": [0, 0.5, 1, 1.5, 2, 2.999, 3, 3.5, 4, 5.999, 6]},
    {"name": "pista oculta, inexistente y clip desactivado", "tracks": [
        track("V1", "video"), track("V2", "video", hidden=True), track("T1", "text", hidden=True)], "clips": [
        clip("a", "V1", start=0, out=2),
        clip("b", "V2", start=0, out=2),
        clip("c", "V9", start=0, out=2),
        clip("d", "V1", start=0, out=2, disabled=True),
        clip("t", "T1", "text", start=0, out=2),
    ], "heads": [0, 1, 1.999]},
    {"name": "margen de 0,02 s antes del inicio y fin abierto", "tracks": [track("V1", "video")], "clips": [
        clip("a", "V1", start=0, out=1),
        clip("b", "V1", start=1, out=1),
        clip("c", "V1", start=3, out=0.5),
    ], "heads": [-0.03, -0.02, -0.01, 0, 0.97, 0.979, 0.98, 0.981, 0.99, 1, 1.5, 1.98, 1.999, 2, 2.97,
                 2.985, 3.5, 3.6]},
    {"name": "solape en una pista: manda la lista, no el inicio", "tracks": [track("V1", "video")], "clips": [
        clip("tarde", "V1", start=1.985, out=1),
        clip("pronto", "V1", start=0, out=2),
        clip("medio", "V1", start=1.5, out=1),
    ], "heads": [1, 1.6, 1.99, 2, 2.5]},
    {"name": "ids de pista repetidos: gana la última", "tracks": [
        track("V1", "video"), track("T1", "text"), track("V1", "video", hidden=True), track("V2", "video")],
     "clips": [clip("a", "V1", start=0, out=2), clip("b", "V2", start=0, out=2), clip("t", "T1", "text", start=0, out=2)],
     "heads": [0, 1]},
    {"name": "ids de clip repetidos: el índice es el del último", "tracks": [track("V1", "video"), track("V2", "video")],
     "clips": [clip("x", "V2", start=0, out=2), clip("y", "V2", start=0.5, out=2), clip("x", "V1", start=0, out=2),
               clip("x", "V2", start=1, out=2)],
     "heads": [0, 0.6, 1.2, 2.5]},
    {"name": "clip visual en pista de audio: capa −1, al fondo", "tracks": [track("V1", "video"), track("A1", "audio")],
     "clips": [clip("v", "V1", start=0, out=2), clip("va", "A1", start=0, out=2), clip("ta", "A1", "text", start=0, out=2)],
     "heads": [0, 1]},
    {"name": "velocidad, al revés y velocidad fuera de rango", "tracks": [track("V1", "video"), track("V2", "video"),
                                                                       track("V3", "video"), track("V4", "video")],
     "clips": [clip("x2", "V1", start=0, out=4, speed=2), clip("rev", "V2", start=0, out=3, speed=0.5, reverse=True),
               clip("x20", "V3", start=0, out=4, speed=20), clip("x0", "V4", start=0, out=1, speed=0),
               clip("img", "V4", "image", start=2, out=1, speed=4), clip("txt", "V3", "text", start=1, out=1, speed=3)],
     "heads": [0, 0.39, 0.4, 0.5, 0.999, 1, 1.5, 1.999, 2, 2.5, 2.999, 3, 5.999, 6]},
    {"name": "pistas de texto enterradas", "tracks": [
        track("T0", "text"), track("V1", "video"), track("T1", "text"), track("A1", "audio"), track("T2", "text"),
        track("V2", "video"), track("T3", "text")],
     "clips": [clip(f"t{i}", f"T{i}", "text", start=0, out=1) for i in range(4)] + [clip("v1", "V1"), clip("v2", "V2")],
     "heads": [0, 0.5]},
    {"name": "sin pistas", "tracks": [], "clips": [clip("a", "V1", start=0, out=1)], "heads": [0]},
    {"name": "solo audio", "tracks": [track("A1", "audio"), track("A2", "audio")],
     "clips": [clip("a", "A1", "audio", start=0, out=1)], "heads": [0]},
    {"name": "duración 0 y clip corto", "tracks": [track("V1", "video")],
     "clips": [clip("z", "V1", start=1, inp=2, out=2), clip("c", "V1", start=2, out=0.01)],
     "heads": [0.98, 0.99, 1, 1.98, 1.99, 2, 2.005, 2.01]},
]
