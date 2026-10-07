"""Casos sintéticos de paridad del HISTORIAL (gc_core History + history_store ↔ timeline_history.py).

Una secuencia de pasos sobre unas timelines pequeñas. El exportador la reproduce con
timeline_history (y con la lógica de timeline_store.restore_checkpoint) y guarda las
pilas tras cada paso; al final, el historial entero tal como se escribe en disco.
"""
from __future__ import annotations

from ops_cases import clip, track

TIMELINES = {
    "A": {"schema_version": 5, "tracks": [track("V1", "video")], "clips": [clip("a1", "V1", out=3)]},
    "B": {"schema_version": 5, "tracks": [track("V1", "video"), track("T1", "text")],
          "clips": [clip("b1", "V1", out=2), clip("b2", "T1", kind="text", start=1, out=2, text="hola")]},
    "C": {"schema_version": 5, "width": 1080, "height": 1920},
    "D": {"schema_version": 5, "fps": 60, "markers": [{"id": "m1", "t": 1.5, "label": "aquí"}]},
}

STEPS = [
    ("snapshot", "A"), ("snapshot", "B"),
    ("checkpoint", "uno", "C"),
    ("undo", "C"), ("redo", "B"),
    ("checkpoint", "dos", "A"),
    ("checkpoint", "uno", "D"),       # mismo nombre: se actualiza en su sitio
    ("undo", "A"), ("undo", "B"), ("undo", "C"),   # el último no tiene nada que deshacer
    ("redo", "D"),
    ("snapshot", "C"),                 # una edición nueva vacía el rehacer
    ("restore", "dos", "B"),           # el restore se puede deshacer: apila el estado actual
    ("restore", "nada", "B"),          # checkpoint inexistente
    *[("snapshot", "ABCD"[i % 4]) for i in range(53)],   # pasa del límite de 50
    ("undo", "A"), ("undo", "B"),
]

# Un historial escrito a mano: timelines antiguas (v4) que se migran al leerlas, entradas
# que no son timelines (se descartan) y pilas que faltan o tienen otro tipo.
RAW = {
    "undo": [
        {"schema_version": 4, "tracks": [track("V1", "video"), track("T1", "text")],
         "clips": [clip("v", "V1", out=2), clip("t", "T1", kind="text", out=2, text="viejo")]},
        "no es una timeline",
        {"clips": "tampoco"},
        TIMELINES["C"],
    ],
    "redo": {"a": 1},
    "checkpoints": {"bueno": TIMELINES["D"], "malo": 5},
}
