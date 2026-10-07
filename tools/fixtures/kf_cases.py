"""Casos sintéticos de paridad de la ANIMACIÓN (gc_core keyframes/masks ↔ clip_keyframes.py, clip_mask.py).

Cada caso es un clip y unos instantes; video-yt calcula en cada instante las
propiedades (clip_props_at), el estilo de texto animado (text_style_at), las
máscaras (clip_masks_at) y el volumen. También curvas sueltas (ease_t) y
normalize_mask / with_text_style_kf.
"""
from __future__ import annotations


def clip(cid, kind="video", **kw):
    c = {"id": cid, "track_id": "V1", "kind": kind, "asset_kind": "clips", "asset_id": cid, "filename": "a.mp4",
         "start": 0, "in_point": 0, "out_point": 4, "source_duration": 10}
    c.update(kw)
    return c


def items(*specs):
    return {"enabled": True, "items": list(specs)}


INTERPS = ["linear", "ease-in", "ease-out", "ease-in-out", "cubic-in", "cubic-out", "cubic-in-out",
           "back-out", "bezier", "hold", "direct", "step", "raro", None, 5]

CLIPS = [
    {"name": "sin keyframes: valores estáticos", "clip": clip(
        "s", transform={"x": 0.2, "y": "0.7", "scale": 1.5, "rotation": -10}, opacity=0.6,
        reframe={"zoom": 0.8}, volume=1.4, audio_fx={"eq": True, "reverb": 0.3, "echo": False, "denoise": 7})},
    {"name": "keyframes desactivados", "clip": clip(
        "d", keyframes={"enabled": False, "items": [{"t": 0, "props": {"x": 0}}, {"t": 1, "props": {"x": 1}}]})},
    {"name": "todas las curvas", "clip": clip("c", keyframes=items(
        {"t": 0, "props": {"x": 0, "y": 0, "scale": 1}},
        *[{"t": 0.25 * (i + 1), "interpolation": k, "props": {"x": (i % 3) * 0.4, "y": i * 0.1}}
          for i, k in enumerate(INTERPS)]))},
    {"name": "bézier propia, rebote y degeneradas", "clip": clip("b", keyframes=items(
        {"t": 0, "props": {"scale": 1}},
        {"t": 1, "interpolation": "bezier", "bezier": [0.1, 1.8, 0.2, -0.6], "props": {"scale": 2}},
        {"t": 2, "interpolation": "bezier", "bezier": [0, 0, 0, 0], "props": {"scale": 1}},
        {"t": 3, "interpolation": "bezier", "bezier": [1, 1, 1, 1], "props": {"scale": 3}},
        {"t": 4, "interpolation": "bezier", "bezier": [2, -5, "0.3", None], "props": {"scale": 0}},
        {"t": 5, "ease": "ease-in", "props": {"scale": 1}}))},
    {"name": "items raros: sin t, t no finito, desordenados, valores inválidos", "clip": clip("r", keyframes=items(
        {"t": 3, "props": {"x": 1, "rotation": "90"}},
        "no-dict",
        {"props": {"x": 5}},
        {"t": "inf", "props": {"x": 9}},
        {"t": 1, "props": {"x": "", "y": None, "scale": "abc", "opacity": True}},
        {"t": "0.5", "props": "nada"},
        {"t": 2, "interpolation": "hold", "props": {"x": 0.5, "zoom": 0.5}}))},
    {"name": "texto: estilo animado y colores", "clip": clip(
        "t", kind="text", asset_kind="text", filename="",
        style={"x": 0.4, "y": 0.8, "rot_x": 10, "color": "#fff", "bg": "none", "border_width": 0},
        keyframes=items(
            {"t": 0, "props": {"color": "#ff0000", "shadow_color": "abc", "border_width": 2, "rot_y": 0}},
            {"t": 1, "interpolation": "ease-out", "props": {"color": "#00f", "bg_opacity": 0.5, "rot_y": 30}},
            {"t": 2, "props": {"color": " #12AB9f ", "glow_color": "#000", "curve": "0.3"}},
            {"t": 3, "interpolation": "hold", "props": {"color": "##ffffff", "bg_opacity": 1}}))},
    {"name": "figura con trazo", "clip": clip(
        "f", kind="shape", asset_kind="shape", shape={"x": 0.1, "draw": 1.7, "opacity": "0.5"},
        keyframes=items({"t": 0, "props": {"draw": 0}}, {"t": 2, "props": {"draw": 1}}))},
    {"name": "máscaras: la primera anima, la de ajuste no recorta", "clip": clip(
        "m", masks=[{"type": "rectangle", "x": 0.4, "w": 0.3, "radius": 0.2},
                    {"type": "circle", "target": "adjust"}, {"type": "star", "enabled": False}],
        keyframes=items({"t": 0, "props": {"mx": 0.2, "mw": 0.2, "mfeather": 0.5}},
                        {"t": 2, "interpolation": "ease-in-out", "props": {"mx": 0.8, "mrot": 45, "mw": 20}}))},
    {"name": "máscara de ajuste primero", "clip": clip(
        "ma", masks=[{"type": "circle", "target": "adjust"}, {"type": "heart"}],
        keyframes=items({"t": 0, "props": {"mx": 0.1}}, {"t": 1, "props": {"mx": 0.9}}))},
    {"name": "audio: volumen y efectos animados", "clip": clip(
        "a", kind="audio", asset_kind="audios", volume=3, audio_fx={"eq": 0.2},
        keyframes=items({"t": 0, "props": {"volume": 0, "eq": 0}},
                        {"t": 1, "interpolation": "ease-out", "props": {"volume": 1.8, "eq": 1, "reverb": 0.5}},
                        {"t": 3, "interpolation": "cubic-in", "props": {"volume": 5}}))},
    {"name": "un solo keyframe", "clip": clip("u", keyframes=items({"t": 1, "props": {"x": 0.9, "opacity": 0}}))},
    {"name": "items no es lista", "clip": clip("n", keyframes={"enabled": True, "items": {"a": 1}})},
]

TIMES = [-1, 0, 0.1, 0.25, 0.3, 0.5, 0.75, 1, 1.2, 1.5, 1.99, 2, 2.4, 2.5, 3, 3.3, 3.75, 4, 5, 9]

EASE = [{"u": u, "kind": k, "bezier": b}
        for k in INTERPS for u in (-0.5, 0, 0.1, 0.25, 0.49, 0.5, 0.51, 0.75, 0.9, 1, 1.5)
        for b in ([None] if k != "bezier" else [None, [0.1, 1.8, 0.2, -0.6], [0, 0, 0, 0], [1, 1, 1, 1], "x"])]

MASKS = [None, {}, {"type": "text"}, {"type": "text", "text": {"weight": 100}},
         {"type": "text", "text": {"weight": 500.5, "size": 9, "font": "", "content": 5, "align": "right"}},
         {"type": "text", "text": {"weight": 950}}, {"type": "text", "text": {"weight": "900"}},
         {"type": "brush", "brush": {"size": 0, "points": [{"x": 1, "y": 2, "m": 0}, {"x": 1, "y": 2, "m": "1"}]}},
         {"type": "brush", "brush": {"points": "ab"}},
         {"id": 0, "enabled": 0, "invert": "x", "opacity": -1, "scale_x": 50, "rotation": "nan", "target": "ADJUST"}]

STYLES = [
    ({"bg": "none", "border_width": 0, "color": "#fff"}, {"bg": "#000", "border_width": 3, "color": "#f00"}),
    ({"bg": "#123", "border_width": 2, "scale_split": True}, {"stretch_x": 1.2, "bg": "#456"}),
    ({}, {"stretch_x": 1, "stretch_y": 2}),
    (None, None),
    ({"bg": None, "border_width": "x"}, {"bg": "#fff", "border_width": 1}),
]
