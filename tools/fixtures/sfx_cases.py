"""Pasos de la biblioteca de SFX en disco (sfx.py) para export_fixtures.py.

Cada paso es una tupla (tipo, *args) que se ejecuta con sfx.py sobre una carpeta temporal
(«{TMP}» en los ajustes y en las rutas es esa carpeta, con «/»):

  files [rutas]            crea archivos («x») bajo la carpeta
  file ruta contenido      escribe un archivo (texto o JSON)
  bytes ruta texto         escribe los bytes latin-1 de `texto` (UTF-8 inválido)
  dirs [rutas]             crea carpetas
  remove ruta              borra un archivo o una carpeta vacía
  default ruta             la carpeta de fábrica (config.SFX_DIR)
  settings contenido       settings.json (texto o JSON); settings.load() lo lee
  index                    load_index()
  scan                     _scan(get_base())
  search q categoría lím   search(q, category, limit)
  resolve ruta             resolve(ruta), como ruta relativa a la carpeta (o None)
  library                  la biblioteca de sound_design: search("", 100000) y la duración de
                           cada sonido que resuelve (sonda falsa: el número que contiene)
  favorite_ids             _favorite_sfx_ids(), ordenados
  entry carpeta archivo categoría uso nombre    _entry(...)
  filter items q categoría favoritos            filter_sfx_items(...)
  favcat categorías items favoritos             with_favorites_category(...)
  audio nombre             Path(nombre).suffix.lower() in _AUDIO_EXT
"""

SIGMA = "ΟΔΟΣ"            # ΟΔΟΣ: lower() con sigma final → οδος
FINAL = "οδος"            # οδος (ς)
MEDIAL = "οδοσ"           # οδοσ (σ)

INDEX = {
    "categorias": {
        "01_IMPACTOS": {"etiqueta": "Impactos", "cantidad": 3},
        "02_AIRE": {},
        "03_RARO": None,
        "04_NULO": {"etiqueta": None, "cantidad": "muchos"},
        "favoritos": {"etiqueta": "Mis favoritos"},
    },
    "sonidos": [
        {"sonido": "Golpe seco", "archivo": "golpe.mp3", "carpeta": "01_IMPACTOS", "categoria": "Impactos",
         "uso_tipico": "puñetazos y caídas"},
        {"archivo": "whoosh.wav", "carpeta": "02_AIRE"},                  # sin nombre: el stem
        {"sonido": "", "archivo": "raro.name.ogg", "carpeta": ""},        # en la raíz
        {"sonido": 5, "archivo": "num.mp3", "carpeta": 7, "categoria": 0, "uso_tipico": None},
        {"sonido": ["a"], "archivo": None, "carpeta": True},              # id «True/None»
        {"archivo": "Ñandú ü.mp3", "carpeta": "03_RARO/sub"},
        {},                                                               # todo por defecto
        {"archivo": "dup.mp3", "carpeta": "01_IMPACTOS"},
        {"archivo": "dup.mp3", "carpeta": "01_IMPACTOS", "sonido": "Otra vez"},
        {"sonido": "Real", "archivo": 1.5, "carpeta": "f"},               # id «f/1.5»
        {"sonido": SIGMA, "archivo": "s.mp3", "carpeta": "g r#ú%"},       # URL codificada
    ],
}

FILES = [
    "b.mp3", "A.WAV", "a/z.mp3", "a.mp3", "_x.ogg", "Z.flac", "é.m4a", "E.aac", "ß.mp3", "SS.mp3",
    "İx.mp3", "iz.mp3", f"{SIGMA}/A{SIGMA}.mp3", "sub/deep/x.Mp3", "sub/Readme.txt", "sub/.mp3",
    "sub/noext", "sub/name.with.dots.wav", "sub/a b%#&+.mp3", "otro/fuera.mp3",
]

STEPS = [
    # Sin carpeta: ni la de los ajustes ni la de fábrica existen.
    ("default", "no_existe"),
    ("index",), ("search", "", "", 300), ("resolve", "b.mp3"), ("library",), ("favorite_ids",),

    # Carpeta de fábrica sin índice: escaneo, en el orden de sorted() de WindowsPath.
    ("files", [f"lib/{f}" for f in FILES if not f.startswith("otro/")] + ["otro/fuera.mp3"]),
    ("dirs", ["lib/vacía", "lib/sub/carpeta.mp3"]),
    ("default", "lib"),
    ("index",), ("scan",),
    ("search", "", "", 300), ("search", "", "", 3), ("search", "a", "", 300), ("search", "  Z  ", "", 300),
    ("search", "sub deep", "", 300), ("search", "", "SUB", 300), ("search", "", " sub/deep ", 300),
    ("search", "x", "deep", 300), ("search", "", "favoritos", 300), ("search", FINAL, "", 300),
    ("search", MEDIAL, "", 300), ("search", " Z ", "", 300), ("search", "nada de nada", "", 300),
    ("resolve", "b.mp3"), ("resolve", "B.MP3"), ("resolve", "a/z.mp3"), ("resolve", "a\\z.mp3"),
    ("resolve", "../otro/fuera.mp3"), ("resolve", "a/../b.mp3"), ("resolve", "a/../../lib/b.mp3"),
    ("resolve", "no.mp3"), ("resolve", ""), ("resolve", "."), ("resolve", "sub"), ("resolve", "sub/"),
    ("resolve", "{TMP}/lib/b.mp3"), ("resolve", "{TMP}/otro/fuera.mp3"), ("resolve", "{TMP}/lib"),
    ("resolve", "/"), ("resolve", "sub/../../otro/../lib/a.mp3"), ("resolve", "vacía"),
    ("resolve", "../LIB/b.mp3"), ("resolve", "{TMP}/LIB/A/Z.MP3"),  # la carpeta con otras mayúsculas
    ("library",),

    # Duraciones (la sonda falsa lee el número del archivo; si no hay, 0).
    ("file", "lib/b.mp3", "1.23456"), ("file", "lib/A.WAV", "2.0005"), ("file", "lib/_x.ogg", " 7 \n"),
    ("file", "lib/a/z.mp3", "0.0625"), ("library",),

    # Favoritos de los ajustes (settings.load los pasa a texto).
    ("settings", {"favorites": {"sfx": ["b.mp3", "a/z.mp3", "no/existe.mp3", 5]}}),
    ("favorite_ids",), ("search", "", "favoritos", 300), ("search", "", " FAVORITOS ", 300),
    ("search", "z", "favoritos", 300), ("search", "", "", 300),
    ("settings", {"favorites": {"sfx": "b.mp3"}}), ("favorite_ids",),    # texto: cada carácter
    ("settings", {"favorites": {"sfx": {"b.mp3": 1}}}), ("favorite_ids",),  # dict: sus claves
    ("settings", {"favorites": 3}), ("favorite_ids",),
    ("settings", "{ roto"), ("favorite_ids",),

    # La carpeta de los ajustes (sfx_folder).
    ("files", ["custom/c.mp3", "custom/d/e.wav"]),
    ("settings", {"sfx_folder": "{TMP}/custom", "favorites": {"sfx": ["c.mp3"]}}),
    ("index",), ("search", "", "", 300), ("resolve", "c.mp3"), ("resolve", "../lib/b.mp3"), ("library",),
    ("settings", {"sfx_folder": "{TMP}//custom/./d/.."}), ("index",),
    ("settings", {"sfx_folder": "{TMP}\\custom\\"}), ("index",),
    ("settings", {"sfx_folder": "{TMP}/no_existe"}), ("index",),           # no existe: la de fábrica
    ("settings", {"sfx_folder": "{TMP}/custom/c.mp3"}), ("index",),        # no es carpeta
    ("settings", {"sfx_folder": ""}), ("index",),
    ("settings", {"sfx_folder": None}), ("index",),
    ("settings", {}),

    # Con sfx_library.json.
    ("files", ["lib/01_IMPACTOS/golpe.mp3", "lib/02_AIRE/whoosh.wav", "lib/03_RARO/sub/Ñandú ü.mp3"]),
    ("file", "lib/02_AIRE/whoosh.wav", "0.5"),
    ("file", "lib/sfx_library.json", INDEX),
    ("index",), ("search", "golpe", "", 300), ("search", "", "impactos", 300), ("search", "", "01", 300),
    ("search", "5", "", 300), ("search", "true", "", 300), ("search", "none", "", 300),
    ("search", "ñandú", "", 300), ("search", "ÑANDÚ Ü", "", 300), ("search", "", "favoritos", 300),
    ("search", FINAL, "", 300), ("search", "", "0", 300), ("search", "", "", 2),
    ("resolve", "01_IMPACTOS/golpe.mp3"), ("resolve", "03_RARO/sub/Ñandú ü.mp3"), ("resolve", "f/1.5"),
    ("library",),

    # Índices que fallan: los sonidos salen del escaneo y las categorías leídas se quedan.
    ("file", "lib/sfx_library.json", "{ roto"), ("index",),
    ("file", "lib/sfx_library.json", "﻿{\"sonidos\": []}"), ("index",),           # BOM
    ("bytes", "lib/sfx_library.json", "{\"sonidos\": [{\"archivo\": \"\xe9.mp3\"}]}"), ("index",),  # latin-1
    ("file", "lib/sfx_library.json", []), ("index",),
    ("file", "lib/sfx_library.json", "null"), ("index",),
    ("file", "lib/sfx_library.json", "\"texto\""), ("index",),
    ("file", "lib/sfx_library.json", {"categorias": [1]}), ("index",),
    ("file", "lib/sfx_library.json", {"categorias": {"a": {"etiqueta": "A"}, "b": 5, "c": {}}}), ("index",),
    ("file", "lib/sfx_library.json", {"categorias": {"a": {}}, "sonidos": {"x": 1}}), ("index",),
    ("file", "lib/sfx_library.json", {"sonidos": "abc"}), ("index",),
    ("file", "lib/sfx_library.json", {"sonidos": 5}), ("index",),
    ("file", "lib/sfx_library.json", {"sonidos": [{"archivo": "ok.mp3"}, {"archivo": None}]}), ("index",),
    ("file", "lib/sfx_library.json", {"sonidos": [{"archivo": 5, "sonido": "x"}]}), ("index",),
    ("file", "lib/sfx_library.json", {"sonidos": [{"archivo": 5, "sonido": "x", "carpeta": "c"}]}), ("index",),
    ("file", "lib/sfx_library.json", {"sonidos": [{"archivo": "", "sonido": None}]}), ("index",),
    ("file", "lib/sfx_library.json", {"sonidos": [1]}), ("index",),
    ("file", "lib/sfx_library.json", {"sonidos": [], "categorias": {}}), ("index",),      # vacío, sin escaneo
    ("file", "lib/sfx_library.json", {"sonidos": 0, "categorias": None}), ("index",),
    ("file", "lib/sfx_library.json", {"categorias": {"x": {"etiqueta": "X"}}, "sonidos": False}), ("index",),
    ("file", "lib/sfx_library.json",
     "{\"categorias\": {\"a\": {\"etiqueta\": \"1\"}, \"a\": {\"etiqueta\": \"2\"}, \"b\": {}}}"), ("index",),
    ("file", "lib/sfx_library.json", "{\"sonidos\": [{\"archivo\": \"x.mp3\"}], \"sonidos\": []}"), ("index",),
    ("remove", "lib/sfx_library.json"), ("dirs", ["lib/sfx_library.json"]), ("index",),  # es una carpeta
    ("remove", "lib/sfx_library.json"),

    # Sueltos.
    ("entry", "", "x.mp3", "", "", None), ("entry", "a", "b.c.mp3", "Cat", "uso", "Nombre"),
    ("entry", None, "x", "", "", None), ("entry", 0, "y.wav", "", "", None), ("entry", [], "z", "", "", None),
    ("entry", {"a": 1}, "w.mp3", "", "", None), ("entry", 1.0, 2, "", "", None), ("entry", 1.0, 2, "", "", "n"),
    ("entry", True, "t.mp3", False, None, ""), ("entry", "", None, "", "", None), ("entry", "", 5, "", "", "n"),
    ("entry", "", "", "", "", None), ("entry", "a/b", "c d#.mp3", "", "", None), ("entry", -0.0, "x", "", "", 0),
    ("entry", 1e16, "x", "", "", None), ("entry", "", ".hidden", "", "", None), ("entry", "", "..", "", "", []),
    ("entry", "", "C:x.mp3", "", "", None), ("entry", "", "a\\b.mp3", "", "", None),
]

ITEMS = [
    {"id": "a", "name": "Alfa", "category": "Cat", "uso": "golpe fuerte", "folder": "f"},
    {"id": 5, "name": None},
    {"name": SIGMA},
    {"id": "b", "folder": None, "category": None, "name": ["x"]},
    {},
    {"id": "c", "name": "Beta", "folder": "Sub/Dir", "category": "", "uso": 1.5},
]

STEPS += [
    ("filter", ITEMS, "", "", []), ("filter", ITEMS, "none", "", []), ("filter", ITEMS, FINAL, "", []),
    ("filter", ITEMS, MEDIAL, "", []), ("filter", ITEMS, "", "favoritos", ["a", "5", "zz"]),
    ("filter", ITEMS, "", "cat", []), ("filter", ITEMS, "", "none", []), ("filter", ITEMS, "", "sub/dir", []),
    ("filter", ITEMS, " alfa golpe　", "", []), ("filter", ITEMS, "x", "", []),
    ("filter", ITEMS, "1.5", "", []), ("filter", ITEMS, "golpe", "favoritos", ["a"]),
    ("filter", ITEMS, "", "  ", []), ("filter", [], "a", "b", []),
    ("favcat", [{"id": "favoritos", "label": "x"}, {"id": "a"}, {"label": "sin id"}], ITEMS, ["a", "c", "5"]),
    ("favcat", [], [], []),
    *[("audio", n) for n in ["x.mp3", "x.MP3", "x.Mp3", ".mp3", "x.mp3.txt", "x.", "x", "x.FLAC", "x.aac",
                             "x.m4a", "x.ogg", "x.wav", "x.wma", "x.mp4", "a/b.mp3", "a.mp3/b", "x.ＭＰ３",
                             "..mp3", "x..mp3", "C:x.wav"]],
]
