"""Casos de model_manager.py (modelos de IA residentes en RAM) para tests/fixtures/ai/model_manager.json.

Cada caso es una secuencia de pasos que export_fixtures.py ejecuta sobre el módulo REAL con su reloj
falso (`_set_clock`), sin el hilo que descarga (`_ensure_reaper`) y con `settings.load` sustituido:

  ["clock", t]                  el reloj pasa a valer t
  ["register", name, heavy]     registrar; su descarga anota el nombre
  ["register_boom", name, heavy] lo mismo, pero la descarga lanza después de anotarlo
  ["touch", name] · ["forget", name] · ["unload", name] · ["unload_all"]
  ["unload_idle", s | None]     s en segundos ("nan"/"inf" como texto); None = los de los ajustes
  ["enter", name] · ["exit"]    `with in_use(name)`: entrar y salir del último abierto
  ["loaded"] · ["idle_minutes"] · ["report"] (memory_report sin rss_bytes)
  ["settings", valor]           lo que devuelve settings.load() desde aquí
"""
from __future__ import annotations

_S1 = {"models": {"idle_minutes": 1}}

CASES = [
    ("descarga por inactividad con los ajustes", [
        ["settings", _S1], ["clock", 0.0], ["register", "rvm", False], ["clock", 10.0], ["touch", "rvm"],
        ["loaded"], ["clock", 69.9], ["unload_idle", None], ["clock", 70.0], ["unload_idle", None], ["loaded"],
    ]),
    ("uno pesado a la vez", [
        ["clock", 1.0], ["register", "sam", True], ["register", "whisper", False], ["clock", 2.0],
        ["register", "birefnet", True], ["loaded"], ["register", "kokoro", True], ["loaded"],
    ]),
    ("un pesado en uso no se descarga", [
        ["register", "sam", True], ["enter", "sam"], ["register", "birefnet", True], ["loaded"], ["exit"],
        ["register", "rvm", True], ["loaded"],
    ]),
    ("volver a registrar no cambia el orden", [
        ["register", "a", False], ["register", "b", False], ["clock", 5.0], ["register", "a", True], ["loaded"],
        ["unload_all"],
    ]),
    ("volver a registrar uno en uso lo deja en uso", [
        ["clock", 1.0], ["register", "a", False], ["enter", "a"], ["clock", 4.0], ["register", "a", False],
        ["unload_all"], ["loaded"], ["exit"], ["unload_all"],
    ]),
    ("volver a registrar uno pesado no se descarga a sí mismo", [
        ["register", "sam", True], ["clock", 3.0], ["register", "sam", True], ["loaded"],
    ]),
    ("olvidar y volver a registrar va al final", [
        ["register", "a", False], ["register", "b", False], ["forget", "a"], ["forget", "nadie"],
        ["register", "a", False], ["unload_all"],
    ]),
    ("in_use de uno que no estaba al entrar", [
        ["clock", 1.0], ["enter", "x"], ["register", "x", False], ["clock", 9.0], ["exit"], ["loaded"],
        ["clock", 20.0], ["unload_idle", 11.0], ["unload_idle", 10.0],
    ]),
    ("in_use anidado", [
        ["register", "a", False], ["enter", "a"], ["enter", "a"], ["clock", 100.0], ["exit"],
        ["unload_idle", 1.0], ["unload_all"], ["loaded"], ["exit"], ["clock", 200.0], ["unload_idle", 1.0],
    ]),
    ("in_use de otro registro con el mismo nombre", [
        ["register", "a", False], ["enter", "a"], ["unload", "a"], ["register", "a", False], ["exit"],
        ["loaded"],
    ]),
    ("una descarga que falla", [
        ["register_boom", "boom", False], ["register", "ok", False], ["unload", "boom"], ["unload", "boom"],
        ["loaded"], ["register_boom", "boom2", True], ["register", "big", True], ["loaded"],
    ]),
    ("unload_idle con topes raros", [
        ["register", "a", False], ["clock", 50.0], ["unload_idle", 0.0], ["unload_idle", -5.0],
        ["unload_idle", "nan"], ["unload_idle", "inf"], ["unload_idle", 50.0], ["loaded"],
    ]),
    ("descargar uno que no está", [["unload", "nadie"], ["unload_all"], ["unload_idle", 1.0], ["loaded"]]),
    ("segundos redondeados como round(…, 1)", [
        ["clock", 0.0], ["register", "a", False], ["clock", 0.25], ["loaded"], ["clock", 0.35], ["loaded"],
        ["clock", 1.25], ["loaded"], ["clock", 2.675], ["loaded"], ["clock", 10.05], ["loaded"],
        ["touch", "a"], ["clock", 10.15], ["loaded"],
    ]),
    ("loaded por nombre", [
        ["register", "b", False], ["register", "A", False], ["register", "á", False], ["register", "a", True],
        ["register", "a b", False], ["register", "Z", False], ["loaded"], ["report"],
    ]),
    ("unload_all salta los que están en uso", [
        ["register", "a", False], ["register", "b", True], ["enter", "b"], ["unload_all"], ["loaded"],
        ["exit"], ["unload_all"],
    ]),
    ("unload_idle sin ajustes de descarga (0 = nunca)", [
        ["settings", {"models": {"idle_minutes": 0}}], ["register", "a", False], ["clock", 1e9],
        ["unload_idle", None], ["loaded"],
    ]),
]

# Lo que puede devolver settings.load() para idle_minutes(): (nombre, ajustes).
SETTINGS = [
    ("2 minutos", {"models": {"idle_minutes": 2}}),
    ("con decimales", {"models": {"idle_minutes": 0.5}}),
    ("texto numérico", {"models": {"idle_minutes": "3.5"}}),
    ("texto con espacios", {"models": {"idle_minutes": " 7 "}}),
    ("texto con guion bajo", {"models": {"idle_minutes": "1_0"}}),
    ("negativo", {"models": {"idle_minutes": -1}}),
    ("cero", {"models": {"idle_minutes": 0}}),
    ("None", {"models": {"idle_minutes": None}}),
    ("texto que no es un número", {"models": {"idle_minutes": "abc"}}),
    ("True", {"models": {"idle_minutes": True}}),
    ("False", {"models": {"idle_minutes": False}}),
    ("nan", {"models": {"idle_minutes": "nan"}}),
    ("inf", {"models": {"idle_minutes": "inf"}}),
    ("-inf", {"models": {"idle_minutes": "-inf"}}),
    ("lista", {"models": {"idle_minutes": [1]}}),
    ("sin la clave", {"models": {}}),
    ("models None", {"models": None}),
    ("models lista vacía", {"models": []}),
    ("models lista", {"models": [1]}),
    ("models texto", {"models": "x"}),
    ("models 0", {"models": 0}),
    ("models 1", {"models": 1}),
    ("sin models", {}),
    ("otra clave", {"models": {"idle": 9}}),
]
