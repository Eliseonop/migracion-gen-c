"""Pasos de los personajes stick (stick_library.py) para export_fixtures.py.

Cada paso es una tupla (tipo, *args) que se ejecuta con stick_library.py sobre una carpeta
temporal (la raíz de fábrica de las colecciones es <carpeta>/<assets>/material):

  files [rutas]               crea archivos («x») bajo la carpeta
  file ruta contenido         escribe un archivo (texto tal cual o JSON)
  dirs [rutas]                crea carpetas
  rm ruta                     borra un archivo
  assets ruta                 config.ASSETS_DIR
  list                        list_sticks()
  detail id                   stick_detail(id)
  create nombre emoji color   create_stick(nombre, emoji, color)
  add id [[n, c]] expresión   add_files(id, [(n, BytesIO(c en UTF-8))], expresión)
  set id ruta expresión       set_expression(id, ruta, expresión)
  norm x · slug x fb          _norm(x) · _slug(x, fallback=fb)
  pretty stem · title x       _pretty_title(stem) · x.title()
  raw ruta                    el texto exacto de un archivo (sus bytes en UTF-8), o None
  tree ruta                   [ruta relativa, contenido o None si es carpeta] bajo <ruta>
"""
import json

M = "assets/material"


def manifest(**kw) -> str:
    return json.dumps({"schema": "stick/1", **kw}, ensure_ascii=False)


EXPRS = [{"id": "happy", "label": "Feliz", "emoji": "😄"}, {"id": "sad", "label": "Triste", "emoji": "😢"},
         {"id": "surprised", "label": "Sorprendido"}, {"id": "scared", "label": "Asustado", "emoji": ""},
         {"id": "neutral", "label": "Neutral"}, {"id": "otros", "label": "Otros", "emoji": "✨"},
         {"id": "sad", "label": "Otra triste"}, {"label": "sin id"}, {"id": ""}, "texto", None]

S1_ITEMS = [
    {"file": "a.png", "kind": "image", "expression": "happy", "title": "Una A"},
    {"file": "falta.png", "expression": "sad"},                         # ya no existe en disco
    {"file": "triste\\b.png", "title": ""},                             # «\» y sin expresión: la carpeta
    {"file": "a.png", "expression": "sad", "title": "Repetida"},       # la clave repetida se queda en su sitio
    {"file": "song.mp3", "expression": "happy"},                        # audio: no es del stick
    {"file": "anim.GIF", "expression": "inventada_x"},                  # GIF: imagen animada
    {"file": "./c.png", "expression": "neutral", "title": "Ce"},        # no casa con el «c.png» del escaneo
    {"file": "../plain/p.png", "expression": "happy"},                  # fuera del stick, pero existe
    {"file": "sub/deep/n.mp4"},                                         # categoría propia «sub»
    {"file": 5}, {"file": ""}, {"expression": "happy"}, "no es un dict", 7,
    {"file": "Scientist_giving_thumbs_up_1080p_20260916183212.mp4", "expression": None, "title": None},
]

S1_FILES = ["a.png", "c.png", "song.mp3", "anim.GIF", "notes.txt", ".hidden.png", "sin_ext",
            "Scientist_giving_thumbs_up_1080p_20260916183212.mp4", "Clip_720p.MOV", "x_12345678_y.webm",
            "triste/b.png", "triste/c.jpg", "triste/stick.json", "Feliz/f.png", "happy/h.png", "bailando/d1.mp4",
            "Ñandú Bailón/n.webp", "sub/deep/n.mp4", "sub/s.png", "NEUTRAL/z.bmp", "otros/o.png", "1080p/q.png"]

PRETTY = ["a_1080p", "a_1080p_b", "a_20260916183115", "x_1234567", "x_12345678", "x_123456789_y",
          "Scientist_explaining_complex_con…_1080p_20260918020501", "_720p", "a_720P", "a_٧٢٠p", "a_１０８０p",
          "a_1080p\n", "a_1080p\n\n", "a_12345678\nb", "__", "_", "", "   ", "a__1080p", "a_1080p_1080p",
          "a_10800p", "a_108p", "a_12p", "_1080p_", "a 1080p", "a_1080px", "a_123456789012", "ab_cd  ",
          "x_²³⁴p", "x_߀߁߂p", "Angry_stickman_scientist_standin…_20260915022443"]

TITLES = ["hello world", "mi_cosa".replace("_", " "), "ǆemal ǉ", "ß straße", "ﬁsh ﬂow", "ΣΑΣ σας ΟΔΟΣ",
          "aΣ b", "İstanbul iSTANBUL", "ŉ x", "ǰ", "ა ბ", "Ꭰ ꭰ", "they're bill's", "a1b2 3c", "𐐨𐐩 𐐀",
          "ꙋ ᲀ", "ᾳ ᾼ ῳ", "Ⓐⓑ ⓒ", "ǅ ǈ ǋ", "\u0345x \u0301a", ""]


def _chars(lo: int, hi: int, prefix: str = "") -> str:
    return "".join(prefix + chr(c) for c in range(lo, hi) if not 0xD800 <= c <= 0xDFFF and c not in (0x85,)
                   and not chr(c).isspace())


STEPS = [
    # Sin raíz de material.
    ("assets", "no_existe"),
    ("list",), ("detail", "s1"), ("detail", ".."), ("create", "x", "", "#00FF00"),
    ("add", "s1", [["a.png", "1"]], "otros"), ("set", "s1", "a.png", "sad"),

    # Raíz con sticks, una colección normal y manifiestos que no valen.
    ("files", [f"{M}/plain/p.png", f"{M}/plain/q.mp4"] + [f"{M}/s1/{f}" for f in S1_FILES]),
    ("file", f"{M}/s1/stick.json", manifest(name="Scientific", emoji="🔬", order=0, chroma={"color": "#00ff00"},
                                            expressions=EXPRS, items=S1_ITEMS)),
    ("files", [f"{M}/s2/x.png", f"{M}/s2/y.mp4", f"{M}/s3/x.png", f"{M}/s4/x.png", f"{M}/s5/x.png",
               f"{M}/s6/v.mp4", f"{M}/s7/i.gif", f"{M}/s8/vacía/.keep"]),
    ("file", f"{M}/s2/stick.json", manifest(name="Beta", order=5, chroma="verde")),
    ("file", f"{M}/s3/stick.json", "{ roto"),
    ("file", f"{M}/s4/stick.json", "[1]"),
    ("file", f"{M}/s5/stick.json", "\ufeff" + manifest(name="Con BOM")),
    ("file", f"{M}/s6/stick.json", manifest(name="alfa", order="3", emoji="🙂", chroma={"color": 5})),
    ("file", f"{M}/s7/stick.json", json.dumps({"order": None, "expressions": [], "items": {"i.gif": 1}})),
    ("file", f"{M}/s8/stick.json", manifest(name="Beta", order=5.9, expressions="abc", items="xyz")),
    ("files", [f"{M}/s10/x.png"]),
    ("file", f"{M}/s10/stick.json", manifest(name="alpha", order=5)),     # «Beta» < «alpha», pero no en minúsculas
    ("dirs", [f"{M}/s1/vacía_propia", f"{M}/s9"]),
    ("assets", "assets"),
    ("list",),
    ("detail", "s1"), ("detail", " s1 "), ("detail", "S1"), ("detail", "s2"), ("detail", "s6"), ("detail", "s7"),
    ("detail", "s8"), ("detail", "s3"), ("detail", "s4"), ("detail", "s5"), ("detail", "s9"), ("detail", "plain"),
    ("detail", "no_existe"), ("detail", ".."), ("detail", "s1/x"), ("detail", ""), ("detail", "s1\x00x"),

    # Manifiestos con valores con los que Python falla (o no).
    ("file", f"{M}/s9/stick.json", manifest(name="Nueve", order="x")), ("list",), ("detail", "s9"),
    ("file", f"{M}/s9/stick.json", manifest(name=5)), ("list",), ("detail", "s9"),
    ("file", f"{M}/s9/stick.json", manifest(name="N", order=True, emoji=0)), ("list",),
    ("file", f"{M}/s9/stick.json", manifest(name="N", order=" 12 ")), ("list",),
    ("file", f"{M}/s9/stick.json", manifest(name="N", order=[])), ("list",),
    ("file", f"{M}/s9/stick.json", manifest(name="N", expressions=5)), ("list",),
    ("file", f"{M}/s9/stick.json", manifest(name="N", items=5)), ("list",),
    ("file", f"{M}/s9/stick.json", manifest(name="N", expressions=[{"id": ["a"]}])), ("list",),
    ("file", f"{M}/s9/stick.json", manifest(name="N", expressions={"happy": {}})), ("detail", "s9"),
    ("files", [f"{M}/s9/a.png", f"{M}/s9/b.png", f"{M}/s9/c.png", f"{M}/s9/d.png", f"{M}/s9/e.mp4"]),
    # Ids que no son texto: True y 1 son la misma clave (se queda la primera).
    ("file", f"{M}/s9/stick.json", manifest(name="N", expressions=[{"id": 1, "label": "uno"}, {"id": "two"}],
                                            items=[{"file": "a.png", "expression": True},
                                                   {"file": "b.png", "expression": 1.0},
                                                   {"file": "c.png", "expression": "mi_cosa_rara"},
                                                   {"file": "e.mp4", "expression": 1}])),
    ("list",), ("detail", "s9"),
    # Expresiones que no son del stick: van al final, ordenadas.
    ("file", f"{M}/s9/stick.json", manifest(name="N", items=[{"file": "a.png", "expression": "zz_b"},
                                                             {"file": "b.png", "expression": "aa_a"},
                                                             {"file": "e.mp4", "expression": "mm"},
                                                             {"file": "c.png", "expression": "happy"}])),
    ("detail", "s9"),
    ("file", f"{M}/s9/stick.json", manifest(name="N", items=[{"file": "a.png", "expression": 7},
                                                             {"file": "b.png", "expression": 2.5},
                                                             {"file": "c.png", "expression": True}])),
    ("detail", "s9"),
    ("file", f"{M}/s9/stick.json", manifest(name="N", items=[{"file": "a.png", "expression": 7},
                                                             {"file": "b.png", "expression": "zz"}])),
    ("list",), ("detail", "s9"),
    ("file", f"{M}/s9/stick.json", manifest(name="N", items=[{"file": "a.png", "expression": 7}])), ("detail", "s9"),
    ("file", f"{M}/s9/stick.json", manifest(name="N", items=[{"file": "a.png", "expression": ["x"]}])),
    ("list",), ("detail", "s9"),
    ("file", f"{M}/s9/stick.json", manifest(name="N", expressions=[{"id": 9007199254740993}, {"id": "b"}],
                                            items=[{"file": "a.png", "expression": 9007199254740992.0},
                                                   {"file": "b.png", "expression": 9007199254740993},
                                                   {"file": "c.png", "expression": 18446744073709551615},
                                                   {"file": "d.png", "expression": 1.5e19}])),
    ("detail", "s9"),
    ("rm", f"{M}/s9/stick.json"),

    # Crear sticks: el primero de una raíz sin sticks (o solo con los de order 999) lleva 0.
    ("dirs", ["otra/material/sin_order"]), ("file", "otra/material/sin_order/stick.json", manifest(name="S")),
    ("assets", "otra"), ("create", "Primero", "", ""), ("create", "Segundo", "", ""), ("list",),
    ("assets", "assets"),
    ("dirs", [f"{M}/Mi_Stick_Nu"]),
    ("create", "Mi Stick Ñu", "🔬🔬🔬🔬🔬", "#abcdef"), ("create", "Mi Stick Ñu", "", "red"),
    ("create", "  ", "", "#00FF00"), ("create", None, "", "#00FF00"), ("create", "", "", "#00FF00"),
    ("create", 5, 7, None), ("create", "!!!", " x ", " #ABCDEF "), ("create", "ÁRBOL  ñ--ü", None, "#12345"),
    ("create", "a" * 45 + " b", "", "#1234567"), ("create", "_" * 39 + "x" * 5, "", ""),
    ("create", "Ɛ ǆ ﬁ Ⅻ ①", "", "#00FF00"), ("create", "Ω", "", "#00FF00"),
    ("list",), ("tree", f"{M}/mi_stick_nu_2"), ("raw", f"{M}/mi_stick_nu_2/stick.json"),
    ("raw", f"{M}/5/stick.json"),

    # Subir archivos a una expresión.
    ("files", [f"{M}/s1/asustado"]),
    ("add", "s1", [["x.png", "1"], ["x.png", "2"], ["a.mp3", "3"], ["notes.txt", "4"], [None, "5"],
                   ["C:\\dir\\clip.MP4", "6"], ["../../evil.png", "7"], ["a:b.png", "8"], ["  .png", "9"],
                   ["anim.gif", "10"], ["x.png.", "11"], [5, "12"], ["", "13"], ["Ñandú.JPEG", "14"]], "sad"),
    ("add", "s1", [["h.png", "1"]], "happy"), ("add", "s1", [["d.mp4", "1"]], "bailando"),
    ("add", "s1", [["s.png", "1"]], "surprised"), ("add", "s1", [["o.png", "1"]], None),
    ("add", "s1", [["o.png", "1"]], ""), ("add", "s1", [["q.png", "1"]], "nope"), ("add", "s1", [["q.png", "1"]], 5),
    ("add", "s1", [["q.png", "1"]], "scared"), ("add", "s1", [], "scared"),("add", "s1", [["q.png", "1"]], "nandu_bailon"),
    ("add", "s1", [], "sub"), ("add", "s1", [["v.png", "1"]], "inventada_x"),
    ("add", "plain", [["q.png", "1"]], "otros"), ("add", "..", [], "otros"), ("add", "s2", [["q.png", "1"]], "sad"),
    ("add", "s6", [["w.png", "1"]], "otros"),
    ("tree", f"{M}/s1"), ("tree", f"{M}/s2"), ("raw", f"{M}/s1/stick.json"),

    # Cambiar la expresión de un archivo (sin moverlo).
    ("set", "s1", "c.png", "sad"), ("raw", f"{M}/s1/stick.json"),
    ("set", "s1", "c.png", "happy"), ("set", "s1", "triste\\b.png", "neutral"), ("set", "s1", "//a.png", "otros"),
    ("set", "s1", "./c.png", "sad"), ("set", "s1", "falta.png", "sad"), ("set", "s1", "", "sad"),
    ("set", "s1", None, "sad"), ("set", "s1", "..", "sad"), ("set", "s1", ".", "sad"), ("set", "s1", "triste", "sad"),
    ("set", "s1", "../plain/p.png", "sad"), ("set", "s1", "notes.txt", "sad"), ("set", "s1", "song.mp3", "sad"),
    ("set", "s1", "c.png", "nope"), ("set", "s1", "c.png", None), ("set", "s1", "c.png", 5),
    ("set", "s1", "bailando/d1.mp4", "bailando"), ("set", "s1", "c.png", "bailando"),
    ("set", "s1", "Ñandú Bailón/n.webp", "nandu_bailon"), ("set", "s1", "anim.GIF", "inventada_x"),
    ("set", "s1", "C.PNG", "neutral"), ("set", "s1", "sin_ext", "sad"), ("set", "S1", "c.png", "sad"),
    ("raw", f"{M}/s1/stick.json"),
    ("set", "s2", "x.png", "sad"), ("set", "s7", "i.gif", "neutral"), ("set", "s8", "vacía/.keep", "sad"),
    ("files", [f"{M}/s8/propia/p.png"]), ("set", "s8", "propia/p.png", "propia"),
    ("raw", f"{M}/s2/stick.json"), ("raw", f"{M}/s7/stick.json"), ("raw", f"{M}/s8/stick.json"),
    ("set", "plain", "p.png", "sad"), ("set", "no_existe", "p.png", "sad"), ("set", "a/b", "p.png", "sad"),
    ("list",), ("detail", "s1"),

    # Nombres sueltos.
    *[("pretty", s) for s in PRETTY],
    *[("title", s) for s in TITLES],
    ("title", _chars(0x80, 0x3000)), ("title", _chars(0x80, 0x3000, "a")), ("title", _chars(0x10400, 0x10500, "1")),
    ("title", _chars(0x1E900, 0x1E960) + _chars(0xA640, 0xA6A0, " ")),
    *[("norm", x) for x in ["Feliz", "  TRISTE ", "Ñandú_Bailón", "ﬁ", "", None, 5, 0, ["a"], "x-y z"]],
    *[("slug", x, "stick") for x in ["Mi Stick Ñu", "__a__b__", "", None, "!!!", "a" * 50, "_" * 39 + "abc",
                                     "x" * 39 + "_y", "Ǆ", 5.0, True, "a\u0301_\u0301b"]],
    ("slug", "", "otros"), ("norm", _chars(0x80, 0x3000)), ("slug", _chars(0x80, 0x3000), "f"),
    ("norm", _chars(0x3000, 0x10000)), ("norm", _chars(0x10000, 0x30000)),
]
