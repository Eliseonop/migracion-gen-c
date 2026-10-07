"""Casos sintéticos de paridad del ENCUADRE (gc_core clip_layout ↔ clip_layout.py).

Cada clip se evalúa con varios aspectos e instantes: ventana de recorte
(crop_window) y puntos de la fuente llevados a la salida (source_point_to_output),
además de is_overlay, clip_flip y flip_filters. Aparte, rectángulos de destino
(dest_rect, dest_rect_even) y recortes en píxeles (source_crop_px) sueltos.
"""
from __future__ import annotations

from kf_cases import clip, items

CLIPS = [
    {"name": "relleno sin reframe", "clip": clip("a")},
    {"name": "relleno con zoom y keyframes suaves", "clip": clip("z", reframe={"zoom": 0.5, "keyframes": [
        {"t": 0, "cx": 0.2, "cy": 0.3}, {"t": 2, "cx": 0.8, "cy": 0.7, "zoom": 0.3},
        {"t": 3, "cx": 0.1, "pan_mode": "direct"}]})},
    {"name": "reframe directo y con contain", "clip": clip("dc", reframe={
        "zoom": 0.7, "pan_mode": "direct", "keyframes": [
            {"t": 0, "cx": 0.5, "fit": "contain"}, {"t": 1.5, "cx": 0.9, "zoom": 0.4, "pan_mode": "smooth"},
            {"t": 3, "cx": 0, "cy": 1, "fit": "contain"}]})},
    {"name": "keyframes de encuadre mandan sobre el reframe", "clip": clip(
        "k", reframe={"zoom": 0.6, "keyframes": [{"t": 0, "cx": 0.3, "fit": "contain"}]},
        keyframes=items({"t": 0, "props": {"cx": 0.1, "zoom": 0.4}},
                        {"t": 2, "interpolation": "ease-in-out", "props": {"cx": 0.9, "cy": 0.2, "zoom": 1}}))},
    {"name": "keyframes sin encuadre: base del reframe, pero cubre", "clip": clip(
        "kb", reframe={"zoom": 0.8, "keyframes": [{"t": 0, "cx": 0.7, "fit": "contain"}]},
        keyframes=items({"t": 0, "props": {"x": 0.3}}, {"t": 2, "props": {"x": 0.7, "rotation": 90}}))},
    {"name": "keyframes con items que no son lista", "clip": clip(
        "kn", keyframes={"enabled": True, "items": {"a": 1}},
        reframe={"keyframes": [{"t": 0, "cx": 0.2, "fit": "contain"}]})},
    {"name": "keyframes desactivados", "clip": clip(
        "kd", keyframes={"enabled": False, "items": [{"t": 0, "props": {"cx": 0}}]}, reframe={"zoom": 0.5})},
    {"name": "overlay con recorte libre", "clip": clip(
        "o", layout="overlay", transform={"x": 0.3, "y": 0.6, "scale": 0.5, "rotation": 30},
        reframe={"crop_w": 0.4, "crop_h": 0.3, "keyframes": [{"t": 0, "cx": 0.05, "cy": 0.95},
                                                            {"t": 3, "cx": 0.6, "cy": 0.5}]})},
    {"name": "overlay con recorte completo y mínimo", "clip": clip(
        "of", layout="overlay", transform={"scale": 2}, flip_h=True, reframe={"crop_w": 1, "crop_h": 0.05})},
    {"name": "overlay sin recorte libre", "clip": clip(
        "on", layout="overlay", transform={"x": 0.8, "y": 0.2, "scale": 0.7, "rotation": -45}, flip_v=True,
        reframe={"zoom": 0.5, "crop_w": 0.5})},
    {"name": "overlay sin reframe, volteado en los dos ejes", "clip": clip(
        "ov", layout="overlay", flip_h=True, flip_v=True, transform={"rotation": 180})},
    {"name": "pose animada con keyframes y volteo", "clip": clip(
        "p", flip_h=True, reframe={"zoom": 0.9},
        keyframes=items({"t": 0, "props": {"x": 0.2, "scale": 0.5, "rotation": -30}},
                        {"t": 3, "interpolation": "back-out", "props": {"x": 0.8, "scale": 1.5, "rotation": 200,
                                                                        "cx": 0.3}}))},
    {"name": "pistas anim (legado)", "clip": clip("l", anim={
        "x": [{"t": 0, "v": 0.2}, {"t": 2, "v": 0.8, "ease": "direct"}],
        "rotation": [{"t": 0, "v": 0}, {"t": 4, "v": 360}], "scale": [{"t": 1, "v": 2}]})},
    {"name": "imagen overlay con recorte", "clip": clip(
        "i", kind="image", asset_kind="images", filename="a.png", layout="overlay",
        transform={"x": 0.5, "y": 0.4, "scale": 0.25}, reframe={"crop_w": 0.6, "crop_h": 0.6,
                                                                 "keyframes": [{"t": 0, "cx": 0.9, "cy": 0.1}]})},
    {"name": "texto: no se voltea con filtros", "clip": clip(
        "t", kind="text", asset_kind="text", filename="", flip_h=True,
        style={"x": 0.4, "y": 0.8, "scale": 1.2, "rotation": 10})},
    {"name": "figura volteada", "clip": clip(
        "f", kind="shape", asset_kind="shape", filename="", flip_v=True, shape={"x": 0.1, "y": 0.2, "rotation": 45})},
    {"name": "zoom de keyframes fuera de 0.1–1", "clip": clip(
        "zk", keyframes=items({"t": 0, "props": {"zoom": 0.05, "cx": -1}}, {"t": 2, "props": {"zoom": 1.5, "cy": 2}}))},
    {"name": "reframe con zoom mínimo y pan_mode vacío", "clip": clip("zm", reframe={
        "zoom": 0.1, "pan_mode": "", "keyframes": [{"t": 0, "cx": 0.2}, {"t": 2, "cx": 0.8, "cy": 0}]})},
    {"name": "overlay con recorte libre intermedio y keyframes de encuadre", "clip": clip(
        "oc", layout="overlay", transform={"x": 0.6, "scale": 0.8},
        reframe={"crop_w": 0.5, "crop_h": 0.999, "keyframes": [{"t": 0, "cx": 0.1}]},
        keyframes=items({"t": 0, "props": {"cx": 0.05, "cy": 0.9, "zoom": 0.5}},
                        {"t": 3, "interpolation": "cubic-in-out", "props": {"cx": 0.7, "cy": 0.3}}))},
    {"name": "overlay con recorte libre pequeño", "clip": clip(
        "os", layout="overlay", reframe={"crop_w": 0.3, "crop_h": 0.06, "keyframes": [{"t": 1, "cx": 0.99, "cy": 0}]})},
    {"name": "keyframes activos sin items y reframe contain", "clip": clip(
        "ke", keyframes={"enabled": True, "items": []},
        reframe={"zoom": 0.4, "keyframes": [{"t": 0, "cx": 0.3, "fit": "contain"}]})},
    {"name": "transform con ceros y textos", "clip": clip(
        "tz", layout="overlay", transform={"x": 0, "y": "0.25", "scale": "1.5", "rotation": "15"})},
]

# (aspecto de la fuente, aspecto de la salida)
ASPECTS = [(16 / 9, 9 / 16), (9 / 16, 9 / 16), (4 / 3, 16 / 9), (1.0, 9 / 16), (9 / 16, 16 / 9)]
# (src_t, local_t)
TIMES = [(-1, -1), (0, 0), (0.5, 0.25), (1, 1), (1.5, 2), (2, 1.5), (2.5, 3), (3, 3.5), (5, 5)]
POINT_TIMES = [(0, 0), (1, 1), (2, 1.5), (3, 3.5)]
POINTS = [(0.5, 0.5), (0, 0), (1, 1), (0.25, 0.8), (-0.2, 1.3)]
# (src_w, src_h, out_w, out_h)
DIMS = [(1920, 1080, 1080, 1920), (1080, 1920, 1080, 1920), (640, 480, 1920, 1080), (3840, 2160, 720, 1280)]

TRANSFORMS = [None, {}, [], {"x": 0}, {"x": 0.1, "y": 0.9, "scale": 0.33, "rotation": -30},
              {"x": "0.3", "scale": "2"}, {"scale": 0}, {"scale": None, "rotation": "45"},
              {"x": True, "y": False}, {"x": -0.4, "y": -0.3, "scale": 0.1}, {"x": 1.4, "scale": 3},
              {"y": ""}, {"extra": 1},
              # dest_rect da NaN/inf y solo falla dest_rect_even
              {"scale": "nan"}, {"scale": "inf"},
              # errores en los dos
              {"x": "abc"}, {"rotation": [1]}, [1, 2], "texto"]
# (crop_sw, crop_sh, out_w, out_h): empates de redondeo, tamaños < 2 y medios píxeles
DEST_SIZES = [(1080, 1920, 1080, 1920), (607.5, 1080, 1080, 1920), (3, 5, 720, 1280), (1, 1, 720, 1280),
              (1000.5, 333.25, 1080, 1920), (2.5, 3.5, 100, 100), (2, 2, 100, 100), (6, 10, 100, 100)]

# (crop_w, crop_h, cx, cy, src_w, src_h)
CROP_PX = [(0.5, 1, 0.5, 0.5, 1920, 1080), (0.3, 0.3, 0, 0, 1920, 1080), (0.3, 0.3, 1, 1, 1920, 1080),
           (1, 1, 0.2, 0.9, 640, 480), (1.2, 0.5, 0.5, 0.5, 100, 100), (0.05, 0.05, 0.51, 0.49, 3840, 2160)]
