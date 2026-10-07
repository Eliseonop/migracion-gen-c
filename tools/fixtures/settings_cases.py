"""Casos sintéticos de paridad de los AJUSTES (gc_core SettingsFile ↔ settings.py).

Una secuencia de operaciones sobre un settings.json temporal. Tras cada paso se guarda
lo que devuelve y el archivo tal como queda. Todas las claves son FALSAS.
"""
from __future__ import annotations

NBSP = chr(0xA0)
DOTS = chr(0x2022) * 4   # «••••», una de las máscaras

STEPS = [
    ("load",),                                    # sin archivo: los de fábrica
    ("public",),
    ("keys_for", "pexels"),
    ("write", "{ esto no es json"),
    ("load",),
    ("write", "\ufeff{\"tts\": {\"voice\": \"bom\"}}"),  # BOM de UTF-8: json.loads falla → de fábrica
    ("load",),
    ("write", [1, 2]),                            # JSON que no es un objeto
    ("load",),
    ("write", {"tts": {"voice": "x"}, "favorites": {"sfx": [1, "a"], "audios": "ab", "textStyles": ["s1", {"k": 1}]},
               "api_keys": {" pexels ": " pk-1 ", "giphy": "********", "gemini": True, "": "x", "unsplash": None,
                            "pixabay": DOTS, "foundry": NBSP + "fk-1" + NBSP},
               "gemini_api_key": "gm-legacy", "extra": {"a": 1}, "export": {"fps": "60", "quality": "high"}}),
    ("load",), ("public",),
    ("api_key", "pexels"), ("api_key", " gemini "), ("api_key", "giphy"), ("api_key", ""),
    ("save", {"api_keys": {"pexels": "********", "giphy": "gk-2", "unsplash": "", "nuevo": 5, "bool": True,
                           " ": "x", "mask": "TRUE"},
              "gemini_api_key_set": True, "export": {"fps": 23}, "transcribe": {"model": "small"},
              "bg_removal": {"device": "metal", "provider": "rvm_resnet50"}, "models": {"idle_minutes": "500"},
              "otro": [1]}),
    ("load",), ("public",),
    ("save", {"api_keys": {"giphy": ""}}),            # texto vacío: borra una clave que había
    ("save", {"gemini_api_key": "  ", "models": {"idle_minutes": "x"}, "transcribe": None}),
    ("save", {"gemini_api_key": "gm-nueva", "models": {"idle_minutes": -3}, "bg_removal": {"device": "cuda"}}),
    ("keys_for", "gemini"), ("add_key", "pexels", "pk-extra"), ("add_key", "pexels", " pk-3 "),
    ("keys_for", "pexels"), ("public",),
    ("set_key", "pexels", 1, "pk-2b"), ("set_key", "pexels", 9, "pk-9"), ("set_key", "pexels", -1, "pk-neg"),
    ("remove_key", "pexels", 0), ("remove_key", "pexels", 99), ("keys_for", "pexels"), ("public",),
    ("add_key", "", "x"), ("add_key", "giphy", "  "), ("set_key", "giphy", 0, ""),
    ("remove_key", "giphy", 0), ("remove_key", "giphy", 0), ("public",),
    ("add_key", "gemini", "gm-extra"), ("remove_key", "gemini", 0), ("remove_key", "gemini", 0), ("load",),
    ("save", None), ("save", {"export": None, "bg_removal": {}}),
    ("write", {"export": [1], "transcribe": "x", "bg_removal": 3}),
    ("load",), ("public",),
    ("save", {"export": {"fps": 24}}),
    ("save", {"transcribe": {"model": "large-v3"}}),
    ("write", {"export": {"fps": 50, "quality": ["x"]}}),
    ("public",),
    ("write", {"api_keys": [1], "favorites": {"sfx": 5}}),
    ("load",), ("keys_for", "pexels"), ("add_key", "pexels", "x"),
    ("write", {"api_keys": {"pexels": "pk"}, "api_keys_extra": {"vacio": ["", "  "], "pexels": ["pk-2"]}}),
    ("public",),                                       # «vacio» no tiene ninguna clave: no cuenta
    ("write", {"transcribe": None, "model": "tiny"}),
    ("public",),
]
