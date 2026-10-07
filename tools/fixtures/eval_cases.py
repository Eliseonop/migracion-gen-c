"""Casos sintéticos de paridad de la EVALUACIÓN por clip (lo que se dibuja en cada instante).

Cada clip se evalúa en varios instantes: animaciones de catálogo (fx_anims:
animation_delta, render_pose, anim_opacity), aparición/salida (clip_fx.clip_fx_at),
y una vez por clip: animaciones válidas y sus duraciones, instantes de muestreo,
modo de fusión (clip_blend), matriz de color y pasadas de ajuste (clip_adjust).
Aparte: curvas de las animaciones, pistas sueltas, ventanas de aparición y efectos
de color sueltos.
"""
from __future__ import annotations

from kf_cases import clip, items

# Máscaras: una de recorte, una de ajuste y una desactivada (el modelo exige que sean dicts).
_CLIP_MASK = {"type": "circle", "x": 0.4, "w": 0.3, "h": 0.3}
_ADJ_MASK = {"type": "rectangle", "target": "adjust", "x": 0.6, "y": 0.4, "feather": 0.1}
_OFF_MASK = {"type": "linear", "enabled": False, "target": "adjust"}

CLIPS = [
    {"name": "sin nada", "clip": clip("n")},
    {"name": "entrada y salida que no caben: se reparten", "clip": clip(
        "r", out_point=0.6, animations={"in": {"id": "zoom_in", "dur": 0.5}, "out": {"id": "fade_out", "dur": 0.5}})},
    {"name": "duraciones raras: texto, negativa, NaN, nula", "clip": clip(
        "d", animations={"in": {"id": "fade_in", "dur": "0.3"}, "out": {"id": "spin_out", "dur": -1},
                         "loop": {"id": "pulse", "dur": "nan"}})},
    {"name": "duración inf y bool", "clip": clip(
        "di", animations={"in": {"id": "pop_in", "dur": "inf"}, "out": {"id": "shrink_out", "dur": True}})},
    {"name": "id de otra ranura, desconocido o no texto", "clip": clip(
        "o", animations={"in": {"id": "fade_out"}, "out": {"id": "nope"}, "loop": {"id": 5},
                         "combo": {"id": "ken_burns_in"}})},
    {"name": "bucle corto (más allá del clip también)", "clip": clip("l", animations={"loop": {"id": "wiggle", "dur": 0.37}})},
    {"name": "bucle con giro y combo", "clip": clip(
        "lc", animations={"loop": {"id": "spin_loop"}, "combo": {"id": "drift_left"}})},
    {"name": "combo sobre keyframes de pose", "clip": clip(
        "ck", animations={"combo": {"id": "pop_hold_out"}},
        keyframes=items({"t": 0, "props": {"x": 0.2, "scale": 0.5, "rotation": -30, "opacity": 0.8}},
                        {"t": 3, "interpolation": "ease-in-out", "props": {"x": 0.8, "scale": 1.5, "rotation": 200}}))},
    {"name": "pistas anim (legado) con entrada", "clip": clip(
        "al", anim={"x": [{"t": 0, "v": 0.2}, {"t": 2, "v": 0.8, "ease": "direct"}],
                    "opacity": [{"t": 1, "v": 0.5}, {"t": 3, "v": 1}]},
        animations={"in": {"id": "elastic_in"}, "out": {"id": "drop_out"}})},
    {"name": "rebote, giro y balanceo", "clip": clip(
        "b", animations={"in": {"id": "bounce_in"}, "out": {"id": "spin_out", "dur": 1.2},
                         "loop": {"id": "sway"}})},
    {"name": "animaciones nulas", "clip": clip("nd", animations=None)},
    {"name": "entradas que no son dict", "clip": clip(
        "ne", animations={"in": "fade_in", "out": None, "loop": [1], "combo": {"id": "ken_burns_out"}})},
    {"name": "animaciones vacías", "clip": clip("v", animations={})},
    {"name": "clip de duración 0", "clip": clip(
        "z", in_point=1, out_point=1, animations={"in": {"id": "fade_in"}, "combo": {"id": "ken_burns_in"}},
        appear="zoom", exit="fade")},
    {"name": "velocidad 2 al revés", "clip": clip(
        "s", speed=2, reverse=True, transform={"x": 0, "y": -0.0, "scale": 0.5, "rotation": 0},
        animations={"in": {"id": "slide_left_in"}, "out": {"id": "fly_up_out"}, "loop": {"id": "bob"}})},
    {"name": "texto con animación y pose estática", "clip": clip(
        "t", kind="text", asset_kind="text", filename="", text="hola", opacity=0.5,
        transform={"x": 0.3, "y": 0.8, "scale": 2, "rotation": 15},
        animations={"in": {"id": "twist_in"}, "out": {"id": "zoom_in_out"}})},
    # Aparición y salida
    {"name": "fundidos", "clip": clip("f1", appear="fade", exit="dissolve", look="vintage")},
    {"name": "pop", "clip": clip("f2", appear="pop", exit="pop")},
    {"name": "zoom y fundido", "clip": clip("f3", appear="zoom", exit="fade")},
    {"name": "deslizar", "clip": clip("f4", appear="slide_left", exit="slide_down")},
    {"name": "deslizar al revés", "clip": clip("f5", appear="slide_up", exit="slide_right")},
    {"name": "cortinillas", "clip": clip("f6", appear="wipe", exit="wipe")},
    {"name": "zoom de salida y disolver", "clip": clip("f7", appear="dissolve", exit="zoom")},
    {"name": "vacíos y desconocidos", "clip": clip("f8", appear="", exit="raro", look="")},
    {"name": "clip corto (ventanas de 0,15 s)", "clip": clip("f9", out_point=0.3, appear="pop", exit="wipe")},
    {"name": "cortinilla y animaciones a la vez", "clip": clip(
        "f10", appear="wipe", exit="slide_right", animations={"in": {"id": "fade_in"}, "out": {"id": "fade_out"}})},
    # Fusión
    {"name": "fusión multiplicar", "clip": clip("m1", blend_mode="multiply")},
    {"name": "fusión luz suave", "clip": clip("m2", blend_mode="soft_light")},
    {"name": "fusión no válida (mayúsculas)", "clip": clip("m3", blend_mode="MULTIPLY")},
    {"name": "fusión vacía", "clip": clip("m4", blend_mode="")},
    # Pasadas de ajuste
    {"name": "máscara de ajuste con ajustes: dos pasadas", "clip": clip(
        "a1", masks=[_CLIP_MASK, _ADJ_MASK, _OFF_MASK],
        effects={"blur": 2, "exposure": 0.5, "contrast": 0.2, "hue": 30, "grain": 3})},
    {"name": "máscara de ajuste en una imagen, solo brillo", "clip": clip(
        "a2", kind="image", asset_kind="images", filename="a.png", masks=[_ADJ_MASK], effects={"brightness": -0.1})},
    {"name": "figura con máscara de ajuste y temperatura", "clip": clip(
        "a3", kind="shape", asset_kind="shape", filename="", shape={"type": "rect"},
        masks=[{**_ADJ_MASK, "id": "mx"}], effects={"temperature": 0.7, "whites": 0.4})},
    {"name": "texto con máscara de ajuste: una pasada", "clip": clip(
        "a4", kind="text", asset_kind="text", filename="", text="x", masks=[_ADJ_MASK], effects={"exposure": 1})},
    {"name": "máscara de ajuste sin ajustes de color", "clip": clip("a5", masks=[_ADJ_MASK], effects={"blur": 3})},
    {"name": "máscara de ajuste desactivada", "clip": clip("a6", masks=[_OFF_MASK], effects={"exposure": 1})},
    {"name": "ajustes a cero o falsos", "clip": clip(
        "a7", masks=[_ADJ_MASK], effects={"exposure": 0, "hue": False, "whites": "", "contrast": None})},
    {"name": "ajustes como texto, bool, NaN y fuera de rango", "clip": clip(
        "a8", masks=[_ADJ_MASK],
        effects={"exposure": "0.25", "hue": True, "temperature": "nan", "whites": 2, "saturation": [1]})},
]

# Instantes locales: antes, dentro y después de las ventanas de 0,4–0,5 s y del clip de 4 s.
TIMES = [-1, 0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.37, 0.4, 0.45, 0.5, 0.6, 1, 1.7, 2, 3.5, 3.55, 3.6, 3.75,
         3.9, 4, 4.5]

EASE_NAMES = ["linear", "inQuad", "outQuad", "inOutQuad", "inCubic", "outCubic", "inOutCubic", "inOutSine",
              "inBack", "outBack", "outElastic", "outBounce", "hold", "raro", ""]
EASE_X = [-0.5, 0, 0.1, 0.3, 0.36, 0.3636, 0.37, 0.5, 0.7, 0.72, 0.75, 0.9, 0.91, 0.95, 1, 1.5]

TRACKS = [
    None,
    [],
    [[0, 1]],
    [[0, 0], [1, 1, "outBounce"]],
    [[0.2, 5], [0.2, 7], [0.8, 9, "hold"]],
    [[0, "2"], [1, "4", "inQuad"]],
    [[0, 0], [1, 1, 5]],
    [[0, 0], [1, 1, None]],
    [[0, 0], [0.5, 1], [0.5, 3], [1, 4, "outBack"]],
    [[0.3, 1], [0.6, -1, "inOutSine"]],
]
TRACK_U = [-0.5, 0, 0.1, 0.2, 0.25, 0.3, 0.45, 0.5, 0.55, 0.6, 0.8, 0.9, 1, 2]

FX_WINDOWS = [-1, 0, 0.1, 0.5, 0.8, 0.81, 1, 5]

EFFECTS = [
    None, {}, [1], {"exposure": 0.5}, {"exposure": -1.5}, {"whites": 0.5}, {"whites": -0.5},
    {"temperature": 1}, {"temperature": -0.3, "hue": -90}, {"hue": 180}, {"hue": 200},
    {"exposure": "0.25", "hue": True, "temperature": "nan", "whites": 2, "saturation": [1]},
    {"exposure": "inf", "hue": "-inf", "whites": "abc"},
    {"brightness": 0.1, "contrast": -0.2, "blur": 4, "exposure": 0, "vhs": True},
    {"exposure": -0.0, "hue": 0.0},
    {"exposure": False, "hue": None, "whites": ""},
]
