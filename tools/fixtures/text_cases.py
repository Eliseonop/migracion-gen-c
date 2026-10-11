"""Casos sintéticos del estilo de texto (text_ass.py) y de las métricas de fuente (font_metrics.py).

Los usa export_fixtures.py → tests/fixtures/text/synthetic.json. Los estilos son el estilo
EFECTIVO de un clip de texto (pista + clip) con valores raros a propósito: tipos cambiados,
textos numéricos, "nan"/"inf", nulos y listas, para que el C++ dé lo mismo que Python o,
donde Python falla, se sepa.
"""
from __future__ import annotations

import struct

_CLASSIC = {"size": 0.048, "bold": True, "align": "center", "x": 0.5, "y": 0.86, "w": 0.88, "bg": "none",
            "bg_opacity": 0.55, "shadow": False, "glow": False, "inactive_opacity": 0.55, "active_opacity": 1,
            "max_words": 3, "font": "Arial Black", "color": "#ffffff", "border_width": 5,
            "border_color": "#111111", "highlight_color": "#ffe566", "word_fx": "highlight",
            "block_appear": "fade", "theme": "classic", "opacity": 1}
_NEON = {**_CLASSIC, "font": "Segoe UI Black", "glow": True, "shadow_color": "#00e5ff", "word_fx": "glow",
         "inactive_opacity": 0.74, "color": "#f4fbff", "border_width": 2}
_BOX = {**_CLASSIC, "font": "Tahoma", "bg": "#111318", "bg_opacity": 0.72, "block_appear": "pop",
        "inactive_opacity": 0.45, "border_width": 0}

_ODD = [None, "abc", "nan", "inf", "-inf", " 0.25 ", True, False, [], {}, -1, 2, "1e-3", 0]

STYLES: list[dict] = [
    {},
    _CLASSIC, _NEON, _BOX,
    # Opacidad del estilo y de la palabra (activa / inactiva, con y sin efecto de palabra).
    *({"opacity": v} for v in _ODD),
    *({**_CLASSIC, "opacity": v, "inactive_opacity": w}
      for v, w in ((0.5, 0.5), ("0.3", None), (None, "nan"), (0.8, "inf"), (2, -3), ("x", 0.25))),
    *({"word_fx": fx, "active_opacity": 0.9, "inactive_opacity": 0.2}
      for fx in ("none", "", None, 5, "pop", "glow", "highlight", "HIGHLIGHT", ["glow", "pop", "x"],
                 ["highlight"], [], [["glow"]], {"glow": 1}, ["none"], ("pop",), True)),
    {"word_fx": "pop", "active_opacity": None, "inactive_opacity": "0.1"},
    {"word_fx": "none", "active_opacity": "0.6", "inactive_opacity": 0.1, "opacity": 0.5},
    # Alfa ASS: (1 − op) · 255 con redondeo al par (0,5 → 127,5 → 128).
    *({"opacity": v} for v in (0.5, 1 / 3, 0.998, 0.002, 1 - 0.5 / 255, 1 - 1.5 / 255, 0.1)),
    # Escala X / Y (Escala uniforme apagada).
    *({"scale_split": on, "stretch_x": sx, "stretch_y": sy}
      for on, sx, sy in ((True, 2, "0.5"), (True, 0, -5), (True, 1000, "nan"), (True, None, "inf"),
                         (True, "-inf", [1]), (False, 3, 3), ("yes", 1.5, 0.75), (0, 4, 4), (1, "abc", 2))),
    # Fondo: color (3 / 6 cifras, espacios, «#» repetidos, mayúsculas de Unicode) y opacidad.
    *({"bg": bg} for bg in ("#111", "none", "", None, "#12345", "#abcdef", "  #ABC ", "##abc", "ßßßßßß",
                            "#1234567", 0, "ÿÿÿ", "#ǆǆǆ", " #a1b2c3 ", "#", "x", "#ffff")),
    *({"bg": "#202122", "bg_opacity": v, "opacity": o}
      for v, o in ((0.3, 1), ("0.3", 0.5), (None, 1), ("nan", 1), (2, 1), (-1, 1), ([], 0.4), (0.55, "inf"))),
    # Fondo dibujado (bloque, redondeado, Alto / Ancho, desplazamiento, texto animado).
    *({"bg": "#000", **extra}
      for extra in ({"_bg_draw": True}, {"_bg_draw": 0}, {"bg_style": "block"}, {"bg_style": "line"},
                    {"bg_radius": 0.2}, {"bg_pad_x": "x"}, {"bg_pad_y": "0.1"}, {"bg_dx": "nan"},
                    {"bg_dy": "inf"}, {"bg_radius": 0}, {"bg_dx": False}, {"bg_dy": True})),
    {"bg": "none", "bg_style": "block", "_bg_draw": True},
    # Posición (centro de la caja en px): límites −10…11, texto, nulos y empates al par.
    *({"x": x, "y": y} for x, y in ((0.5, 0.5), (1.2345, -0.75), (-20, 20), ("nan", None), ("0.25", True),
                                     (0.0125, 0.3125), (0.5 / 1080, 1.5 / 1920), (2.5 / 360, 0.5 / 640),
                                     ("inf", "-inf"), ([], {}), (11, -10))),
    # Sombra paralela.
    *({"shadow": True, **extra}
      for extra in ({}, {"shadow_opacity": 0}, {"shadow_opacity": "0.4", "shadow_distance": 3},
                    {"shadow_angle": "90", "shadow_blur": 0.5}, {"shadow_angle": "nan"},
                    {"shadow_angle": -135, "shadow_distance": "nan", "shadow_blur": "inf"},
                    {"shadow_color": None}, {"shadow_color": "#ff0000", "shadow_opacity": None},
                    {"shadow_distance": -1, "shadow_blur": -1}, {"shadow_angle": 720.5},
                    {"size": 0.1, "scale": "2"}, {"size": "nan", "scale": 0}, {"size": -1, "scale": -1},
                    {"shadow_angle": "inf"})),
    {"shadow": 0, "shadow_opacity": 1},
    # Brillo (gana a la sombra).
    *({"glow": True, **extra}
      for extra in ({}, {"shadow": True}, {"glow_intensity": 0}, {"glow_intensity": 0, "shadow": True},
                    {"glow_range": 2, "glow_dx": "0.5", "glow_dy": -3}, {"glow_style": "strong"},
                    {"glow_color": "#123456"}, {"glow_color": "", "shadow_color": "#654321"},
                    {"glow_intensity": "nan", "glow_range": "nan"}, {"glow_dx": "inf", "glow_dy": "-inf"},
                    {"glow_intensity": None, "glow_style": "soft"}, {"size": 0.07, "scale": 1.5})),
    # Curva (sin Fondo).
    *({"curve_on": on, "curve": k, **extra}
      for on, k, extra in ((True, 0.5, {}), (True, 0.004, {}), (True, -0.0049, {}), (True, -0.006, {}),
                           (True, 0.005, {}), (True, -0.005, {}),
                           (True, -2, {}), (True, 3, {}), (True, "nan", {}), (True, "0.3", {}),
                           (False, 0.5, {}), (True, 0.5, {"bg": "#000"}), (True, 0.5, {"bg": "none"}),
                           (1, None, {}), ("on", 0.7, {"bg": ""}))),
    # Tamaño, escala, espaciado e interlineado (text_metrics).
    *({"size": s, "scale": sc, "letter_spacing": ls, "line_height": lh}
      for s, sc, ls, lh in ((0.05, 1, 0.1, 1.5), ("0.06", "0.5", "-1", "9"), (None, None, None, None),
                            ("nan", "nan", "nan", "nan"), (-0.1, "inf", "inf", "-inf"), (0.04, True, 2, 0.1))),
]

# Pares (pista, clip) para effective_text_style: el clip pisa la pista campo a campo.
EFFECTIVE = [
    (None, None), ({}, None), (None, {}), ({"a": 1, "b": 2}, {"b": 3, "c": 4}),
    ({"color": "#fff", "size": 0.05}, {"size": 0.06}), ({"x": 1}, {"x": None}),
    ({"z": 1, "a": 2}, {"a": 3, "y": 4, "z": 5}),
]

# May./min.: textos × modos.
CASE_TEXTS = [
    "hola MUNDO ñandú", "straße ǆungla", "ΟΔΟΣ οδός ΣΑΣ", "  a\tb c d  ", "123abc ABC-def",
    "", "ﬁne ǈj ŉx", "İstanbul IĞDIR", "línea uno\nlínea DOS", "x​y 　z", "¡HOLA! ¿qué?",
    "ǅemal ΆΈΉ", "aßb", "aͅb ᾳ",
]
CASE_MODES = ["upper", "lower", "title", None, "none", 5, "UPPER"]

# Palabra activa: (nº de palabras, t local, duración).
ACTIVE_WORD = [(n, t, d) for n in (0, 1, 3, 7) for t in (-1.0, -0.0, 0.0, 0.5, 1.999, 2.0, 3.0, 1e-300)
               for d in (0.0, -1.0, 2.0, 1e-9)]


# Texto de las filas, cajas de Fondo y letras de la Curva de cada estilo (en 1080×1920).
ROWS_TEXT = "uno dos tres cuatro cinco seis siete ocho\nnueve\n\ndiez ñú"


# --- Clips de texto (pose, palabras, contenido) -----------------------------------------------

def text_clip(cid, text="hola mundo que tal", style=None, **kw):
    c = {"id": cid, "track_id": "T1", "kind": "text", "asset_kind": "text", "asset_id": cid, "filename": "",
         "text": text, "style": dict(_CLASSIC) if style is None else style,
         "start": 1, "in_point": 0, "out_point": 4, "source_duration": 4}
    c.update(kw)
    return c


def _kf(*specs):
    return {"enabled": True, "items": list(specs)}


def _words(text, starts):
    return [{"text": w, "start": s, "end": s + 0.3} for w, s in zip(text.split(), starts)]


TEXT_CLIPS: list[dict] = [
    {"name": "quieto, sin words", "clip": text_clip("q")},
    {"name": "words que cuadran (repetida, fuera del clip, negativa)", "clip": text_clip(
        "w", words=_words("hola mundo que tal", [-0.5, 0.5, 0.5, 7]))},
    {"name": "words que no cuadran", "clip": text_clip("wn", words=_words("hola mundo", [0, 1]))},
    {"name": "words ordenadas", "clip": text_clip("wo", words=_words("hola mundo que tal", [0, 0.8, 1.6, 3.1]))},
    {"name": "keyframes de pose y de estilo", "clip": text_clip(
        "k", keyframes=_kf({"t": 0, "props": {"x": 0.2, "y": 0.3, "scale": 0.5, "rotation": -20, "opacity": 0.4,
                                               "rot_x": 0, "color": "#ff0000", "border_width": 2}},
                           {"t": 2, "interpolation": "ease-out", "props": {"x": 0.8, "scale": 1.5, "rot_x": 40,
                                                                           "rot_y": "15", "color": "#00f"}},
                           {"t": 3, "interpolation": "hold", "props": {"opacity": 1, "bg_opacity": 0.2}}))},
    {"name": "keyframes quietos", "clip": text_clip(
        "kq", keyframes=_kf({"t": 0, "props": {"x": 0.3}}, {"t": 2, "props": {"x": 0.3}}))},
    {"name": "keyframes de estilo solo", "clip": text_clip(
        "ks", keyframes=_kf({"t": 0, "props": {"color": "#ffffff"}}, {"t": 2, "props": {"color": "#000000"}}))},
    {"name": "pistas anim (legado) que mueven", "clip": text_clip(
        "a", anim={"x": [{"t": 0, "v": 0.2}, {"t": 2, "v": 0.8}], "opacity": [{"t": 1, "v": 0.5}]})},
    {"name": "pistas anim quietas", "clip": text_clip("aq", anim={"x": [{"t": 0, "v": 0.5}], "y": []})},
    {"name": "pistas anim vacías", "clip": text_clip("av", anim={"x": [], "y": None})},
    {"name": "animaciones de entrada y salida", "clip": text_clip(
        "an", animations={"in": {"id": "fade_in"}, "out": {"id": "pop_out"}})},
    {"name": "animaciones sin opacidad en el estilo", "clip": text_clip(
        "ano", style={"x": 0.4, "word_fx": "pop"}, animations={"in": {"id": "zoom_in", "dur": 0.6}})},
    {"name": "animaciones y keyframes", "clip": text_clip(
        "ak", animations={"loop": {"id": "pulse"}},
        keyframes=_kf({"t": 0, "props": {"opacity": 0.5}}, {"t": 4, "props": {"opacity": 1}}))},
    {"name": "escribir (typing) con espacios delante", "clip": text_clip(
        "ty", text="   hola  mundo", style={**_CLASSIC, "block_appear": "typing"})},
    {"name": "una palabra con efecto", "clip": text_clip("u", text="hola")},
    {"name": "sin efecto de palabra", "clip": text_clip("se", style={**_CLASSIC, "word_fx": "none"})},
    {"name": "volteado y giro 3D del estilo", "clip": text_clip(
        "f", flip_h=True, flip_v=True, style={**_CLASSIC, "rot_x": "nan", "rot_y": "15"})},
    {"name": "inicio negativo y duración 0", "clip": text_clip("z", start=-1, in_point=2, out_point=2)},
    {"name": "saltos de línea y Unicode", "clip": text_clip(
        "nl", text="línea uno\nñandú ΟΔΟΣ\n\nfin", words=_words("línea uno ñandú ΟΔΟΣ fin", [0, 0.2, 0.4, 0.6, 0.8]))},
    {"name": "transform estático", "clip": text_clip(
        "t", transform={"x": 0.3, "y": 0.7, "scale": 2, "rotation": 15}, opacity=0.5)},
    {"name": "giro 3D del estilo sin keyframes", "clip": text_clip("g3", style={**_CLASSIC, "rot_x": 25, "rot_y": -10})},
    {"name": "keyframes que mueven poco (por encima del umbral)", "clip": text_clip(
        "kp", keyframes=_kf({"t": 0, "props": {"x": 0.5}}, {"t": 2, "props": {"x": 0.51}}))},
    {"name": "pico de 10 ms que solo ve el muestreo a 24 fps", "clip": text_clip(
        "pk", keyframes=_kf({"t": 0, "props": {"x": 0.5}}, {"t": 0.12, "props": {"x": 0.5}},
                            {"t": 0.125, "props": {"x": 0.6}}, {"t": 0.13, "props": {"x": 0.5}}))},
    {"name": "estilo nulo", "clip": text_clip("sn", style=None)},
]
TEXT_CLIPS[-1]["clip"]["style"] = None

TEXT_TIMES = [-0.5, 0, 0.1, 0.5, 1, 1.5, 2, 2.5, 3.2, 3.99, 4, 5]


# --- Fuentes ----------------------------------------------------------------------------------

def font_bytes(tables: dict[bytes, bytes], num: int | None = None, cut: int | None = None,
               offsets: dict[bytes, int] | None = None) -> bytes:
    """Un TTF mínimo: directorio de tablas + tablas. ``num`` falsea el nº de tablas, ``cut``
    trunca el archivo y ``offsets`` falsea dónde empieza cada tabla."""
    head = struct.pack(">IHHHH", 0x00010000, len(tables) if num is None else num, 0, 0, 0)
    dir_size = 12 + 16 * len(tables)
    body, entries, pos = b"", b"", dir_size
    for tag, data in tables.items():
        off = (offsets or {}).get(tag, pos)
        entries += struct.pack(">4sIII", tag, 0, off, len(data))
        body += data
        pos += len(data)
    out = head + entries + body
    return out if cut is None else out[:cut]


def _head(upm: int) -> bytes:
    return bytes(18) + struct.pack(">H", upm) + bytes(34)


def _os2(asc: int, desc: int, size: int = 96) -> bytes:
    data = bytearray(size)
    if size >= 78:
        data[74:78] = struct.pack(">HH", asc, desc)
    return bytes(data)


def _hhea(asc: int, desc: int) -> bytes:
    return bytes(4) + struct.pack(">hh", asc, desc) + bytes(28)


FONT_FILES: list[tuple[str, bytes]] = [
    ("os2", font_bytes({b"head": _head(2048), b"OS/2": _os2(1854, 434)})),
    ("os2 justo 78", font_bytes({b"head": _head(1000), b"OS/2": _os2(1100, 300, 78)})),
    ("os2 corta → hhea", font_bytes({b"head": _head(1000), b"OS/2": _os2(0, 0, 77), b"hhea": _hhea(900, -250)})),
    ("sin os2 → hhea", font_bytes({b"head": _head(2048), b"hhea": _hhea(1900, -500)})),
    ("hhea con alto ≤ 0", font_bytes({b"head": _head(1000), b"hhea": _hhea(-10, 10)})),
    ("upm 0", font_bytes({b"head": _head(0), b"OS/2": _os2(1000, 200)})),
    ("alto 0", font_bytes({b"head": _head(1000), b"OS/2": _os2(0, 0)})),
    ("sin head", font_bytes({b"OS/2": _os2(1000, 200)})),
    ("sin os2 ni hhea", font_bytes({b"head": _head(1000)})),
    ("head corta", font_bytes({b"head": bytes(19), b"OS/2": _os2(1000, 200)})),
    ("hhea corta", font_bytes({b"head": _head(1000), b"hhea": bytes(7)})),
    ("directorio truncado", font_bytes({b"head": _head(1000), b"OS/2": _os2(1000, 200)}, cut=30)),
    ("nº de tablas de más", font_bytes({b"head": _head(1000), b"OS/2": _os2(1000, 200)}, num=5)),
    ("tabla fuera del archivo", font_bytes({b"head": _head(1000), b"OS/2": _os2(1000, 200)},
                                           offsets={b"OS/2": 100000})),
    ("os2 cortada por el final", font_bytes({b"head": _head(1000), b"OS/2": _os2(1000, 200)}, cut=120)),
    ("tabla repetida", font_bytes({b"head": _head(1000), b"OS/2": _os2(1000, 200)})
     .replace(b"head", b"OS/2", 1)),
    ("vacío", b""),
    ("cinco bytes", b"\x00\x01\x00\x00\x00"),
    ("os2 máxima", font_bytes({b"head": _head(16), b"OS/2": _os2(65535, 65535)})),
]

# Disposición de carpetas para resolve_font_path: archivos que existen en cada una.
FONT_LAYOUTS: list[dict] = [
    {"system": ["arial.ttf", "arialbd.ttf", "impact.ttf", "segoeui.ttf", "seguibl.ttf"], "bundled": ["Anton-Regular.ttf"]},
    {"system": ["arial.ttf"], "bundled": []},
    {"system": [], "bundled": []},
    {"system": ["georgia.ttf", "georgiab.ttf", "consola.ttf"], "bundled": ["Anton-Regular.ttf"]},
]
FONT_NAMES = ["Arial", "Arial Black", "Impact", "Georgia", "Verdana", "Times New Roman", "Courier New",
              "Comic Sans MS", "Trebuchet MS", "Segoe UI", "Segoe UI Black", "Calibri", "Bahnschrift",
              "Tahoma", "Consolas", "Anton", "anton", "Desconocida", "", "arial"]
