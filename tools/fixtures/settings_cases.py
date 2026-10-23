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
    # NaN e Infinity: json.loads los lee y json.dumps los escribe (min(240, max(0, nan)) es 0.0).
    ("write", '{"models": {"idle_minutes": NaN}, "otro": [Infinity, -Infinity, 1e999]}'),
    ("load",), ("save", {"models": {}}), ("load",),
    ("save", {"models": {"idle_minutes": "inf"}}), ("load",),
]

# Historial de YouTube (yt_history.py): ("yt_record", url, video, limit, int(time.time())) y
# ("yt_remove", url_o_id). Va en la clave yt_history de los ajustes.
VA = {"id": "a1", "title": "Vídeo A"}
STEPS_YT = [
    ("write", {}),
    ("yt_record", "", None, 40, 1000),                       # url vacía: no guarda
    ("yt_record", "   ", VA, 40, 1000),
    ("yt_record", " https://y/a ", VA, 40, 1001),
    ("yt_record", "https://y/b", None, 40, 1002),            # sin vídeo: el título es la url
    ("yt_record", "https://y/a2", {"id": "a1"}, 40, 1003),   # el mismo vídeo con otra url: lo sustituye
    ("yt_record", "https://y/b", {"id": "b1", "title": "B"}, 40, 1004),  # la misma url con vídeo
    ("yt_record", "https://y/c", {"id": 0, "title": ""}, 40, 1005),       # campos falsos: vacíos
    ("yt_record", "https://y/d", {"id": 12, "title": 3.5}, 40, 1006),     # str() de números
    # Un vídeo de texto no va: getattr("x", "title") es el método str.title y Python guardaría
    # «<built-in method title of str object at 0x…>». C++ lo trata como sin vídeo.
    ("yt_record", "https://y/e", 7, 40, 1007),               # un vídeo que no es un objeto
    ("yt_record", "https://y/f", [1, 2], 40, 1008),
    ("yt_record", NBSP + "https://y/g" + NBSP, {"id": " v9 ", "title": "G"}, 40, 1009),
    ("yt_record", "https://y/h", {"id": "v9"}, 40, 1010),    # «v9» frente a « v9 »: _key quita espacios
    ("yt_record", "https://y/i", {"id": " v9 "}, 40, 1011),  # la key con espacios no casa con la guardada
    ("yt_record", "https://y/j", None, 3, 1012),             # límite 3
    ("yt_record", "https://y/k", None, -5, 1013),            # max(1, -5)
    ("yt_record", "https://y/l", None, 0, 1014),             # 0 or 40
    ("yt_record", "https://y/m", None, 1, 1015),
    ("load",),
    ("write", {"yt_history": [
        "no es un objeto", 7, None, {"url": ""}, {"url": "   "}, {"title": "sin url"},
        {"url": 5, "title": None, "video_id": 7, "at": "12"},
        {"url": True, "title": 0, "at": 3.9},
        {"url": " https://y/n ", "title": [1, "a"], "video_id": " n1 ", "at": True},
        {"url": "https://y/o", "title": {"k": "v"}, "video_id": "", "at": " 42 "},
        {"url": "https://y/p", "at": -1.5}, {"url": "https://y/q", "at": ""}, {"url": "https://y/r", "at": 0.0},
        {"url": "https://y/s", "at": False}, {"url": "https://y/t", "at": "-7"},
        {"url": "https://y/n", "video_id": "dup"}, {"url": "https://y/u", "video_id": "n1"},
    ]}),
    ("yt_remove", ""),                                        # no quita nada, pero guarda limpio
    ("yt_remove", " n1 "),                                    # por vídeo (y la url de la otra no casa)
    ("yt_remove", "https://y/n"),                             # por url
    ("yt_remove", "5"), ("yt_remove", "True"), ("yt_remove", "no-existe"),
    ("yt_record", "https://y/o", {"id": "dup"}, 40, 2000),   # quita por url aunque el vídeo sea otro
    ("write", {"yt_history": "x"}), ("yt_remove", "a"),
    ("write", {"yt_history": {"url": "https://y/a"}}), ("yt_record", "https://y/a", None, 40, 3000),
    ("write", {"yt_history": [{"url": "https://y/a", "at": "abc"}]}),   # int("abc"): ValueError
    ("yt_record", "https://y/b", None, 40, 3001), ("yt_remove", "x"), ("yt_record", "", None, 40, 3002),
    ("write", {"yt_history": [{"url": "https://y/a", "at": "1.5"}]}), ("yt_remove", ""),
    ("write", {"yt_history": [{"url": "https://y/a", "at": [1]}]}), ("yt_remove", ""),   # TypeError
    ("write", '{"yt_history": [{"url": "https://y/a", "at": NaN}]}'), ("yt_remove", ""),
    ("write", '{"yt_history": [{"url": "https://y/a", "at": Infinity}]}'), ("yt_remove", ""),
    ("write", {"yt_history": [{"url": "https://y/a", "at": "1_000"}, {"url": "https://y/b", "at": "+8"}]}),
    ("yt_remove", ""),
    ("write", {"tts": {"voice": "x"}, "yt_history": [{"url": "https://y/z", "at": 5}]}),
    ("yt_record", "https://y/y", {"id": "y1", "title": "Y"}, 40, 4000),   # conserva el resto de ajustes
    ("load",),
]
STEPS += STEPS_YT
