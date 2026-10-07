"""Casos sintéticos de paridad del FOTOGRAMA (gc_core frame.evaluate ↔ drawComposite de
frontend/src/features/editor/render/canvas.js, ejecutado con Node sobre un contexto que
graba lo que se dibuja).

Cada caso es una timeline, el tamaño de salida, las dimensiones del material de cada clip
(sin entrada = no hay material: el clip no se dibuja) y unos instantes. Las duraciones van
en milisegundos y a velocidad 1 o 2: el clipDur de JS no redondea y decide las ventanas
de aparición y salida.
"""
from __future__ import annotations

from kf_cases import items
from ops_cases import clip, track

_V = [track("V1", "video"), track("V2", "video"), track("T1", "text")]
_HEADS = [0, 0.1, 0.2, 0.35, 0.5, 1, 1.5, 2.2, 3.65, 3.8, 3.9]


def ov(cid, tr="V1", **kw):
    return clip(cid, tr, start=0, out=4, layout="overlay", frame="free", **kw)


def fill(cid, tr="V1", **kw):
    return clip(cid, tr, start=0, out=4, **kw)


CASES = [
    {"name": "overlay: transform, recorte libre y volteo", "out": [1080, 1920], "tracks": _V,
     "media": {"a": [1920, 1080], "b": [1080, 1920]},
     "clips": [ov("a", transform={"x": 0.3, "y": 0.6, "scale": 0.5, "rotation": 30},
                  reframe={"crop_w": 0.4, "crop_h": 0.3, "keyframes": [{"t": 0, "cx": 0.05, "cy": 0.95},
                                                                       {"t": 3, "cx": 0.6, "cy": 0.5}]}, flip_h=True),
               ov("b", "V2", transform={"x": 0.8, "y": 0.2, "scale": 0.7, "rotation": -45}, flip_v=True,
                  reframe={"zoom": 0.5})],
     "heads": _HEADS},
    {"name": "overlay: apariciones y salidas", "out": [1080, 1920], "tracks": _V,
     "media": {"z": [1920, 1080], "s": [1280, 720], "w": [640, 480]},
     "clips": [ov("z", appear="zoom", exit="pop", transform={"scale": 0.6, "rotation": 10}),
               ov("s", "V2", appear="slide_left", exit="slide_down", transform={"x": 0.4, "y": 0.4, "scale": 0.3}),
               clip("w", "V1", start=4, out=2, layout="overlay", appear="wipe", exit="wipe",
                    transform={"x": 0.5, "y": 0.5, "scale": 2, "rotation": 90})],
     "heads": _HEADS + [4, 4.1, 4.2, 4.5, 5.7, 5.9]},
    {"name": "overlay: pose animada y animaciones de catálogo", "out": [1080, 1920], "tracks": _V,
     "media": {"k": [1920, 1080], "an": [1080, 1080]},
     "clips": [ov("k", keyframes=items({"t": 0, "props": {"x": 0.2, "scale": 0.5, "rotation": -30, "cx": 0.3}},
                                       {"t": 3, "interpolation": "ease-in-out",
                                        "props": {"x": 0.8, "scale": 1.5, "rotation": 200, "cx": 0.7}}),
                  reframe={"crop_w": 0.5, "crop_h": 0.5}),
               ov("an", "V2", transform={"x": 0.5, "y": 0.3, "scale": 0.8},
                  animations={"in": {"id": "spin_in"}, "out": {"id": "fade_out"}, "loop": {"id": "pulse"}})],
     "heads": _HEADS},
    {"name": "relleno: reframe, zoom y keyframes de encuadre", "out": [1080, 1920], "tracks": _V,
     "media": {"r": [1920, 1080], "p": [3840, 2160]},
     "clips": [fill("r", reframe={"zoom": 0.6, "keyframes": [{"t": 0, "cx": 0.2, "cy": 0.3},
                                                              {"t": 2, "cx": 0.8, "zoom": 0.4},
                                                              {"t": 3, "cx": 0.1, "pan_mode": "direct"}]}),
               fill("p", "V2", keyframes=items({"t": 0, "props": {"cx": 0.1, "zoom": 0.4, "x": 0.4}},
                                               {"t": 2, "props": {"cx": 0.9, "cy": 0.2, "zoom": 1, "x": 0.6,
                                                                  "rotation": 15}}))],
     "heads": _HEADS},
    {"name": "relleno: contain en un tramo y en todos", "out": [1080, 1920], "tracks": _V,
     "media": {"m": [1920, 1080], "c": [1080, 1920]},
     "clips": [fill("m", reframe={"zoom": 0.7, "keyframes": [{"t": 0, "cx": 0.5, "fit": "contain"},
                                                              {"t": 1.5, "cx": 0.9, "zoom": 0.4},
                                                              {"t": 3, "cx": 0, "cy": 1, "fit": "contain"}]}),
               fill("c", "V2", appear="wipe", exit="fade", flip_h=True,
                    reframe={"keyframes": [{"t": 0, "cx": 0.5, "fit": "contain"}]}, transform={"scale": 0.5})],
     "heads": _HEADS},
    {"name": "relleno: pose, volteo, el atajo de escala 0,001 y contain vertical en horizontal", "out": [1920, 1080], "tracks": _V,
     "media": {"f": [1920, 1080], "q": [1080, 1920], "cp": [1080, 1920]},
     "clips": [fill("f", transform={"x": 0.4, "y": 0.6, "scale": 1.2, "rotation": -20}, flip_v=True,
                    appear="slide_up", exit="zoom"),
               fill("q", "V2", transform={"scale": 1.0005}, opacity=0.5),
               clip("cp", "V1", start=4, out=2, reframe={"keyframes": [{"t": 0, "cx": 0.5, "fit": "contain"}]})],
     "heads": _HEADS + [4.5, 5]},
    {"name": "relleno: doble encuadre vertical y horizontal", "out": [1080, 1920], "tracks": _V,
     "media": {"d1": [1920, 1080], "d2": [1920, 1080]},
     "clips": [fill("d1", reframe={"dual_crop": True, "zoom": 0.8, "keyframes": [{"t": 0, "cx": 0.3}],
                                   "keyframes2": [{"t": 0, "cx": 0.7, "zoom": 0.5}, {"t": 3, "cx": 0.2}]}),
               fill("d2", "V2", reframe={"dual_crop": True, "split_layout": "horizontal", "zoom2": 0.4,
                                         "keyframes": [{"t": 0, "cx": 0.4, "fit": "contain"}]},
                    transform={"x": 0.5, "y": 0.5, "scale": 0.5})],
     "heads": _HEADS},
    {"name": "imagen, motion, velocidad 2 y al revés", "out": [1080, 1080], "tracks": _V,
     "media": {"img": [800, 600], "mo": [1080, 1080], "sp": [1920, 1080], "rev": [1920, 1080]},
     "clips": [clip("img", "V1", "image", start=0, out=2, layout="overlay", transform={"scale": 0.4}),
               clip("mo", "V2", "motion", start=0, out=2, asset_kind="shape", layout="overlay"),
               clip("sp", "V1", start=2, inp=1, out=9, speed=2,
                    reframe={"zoom": 0.5, "keyframes": [{"t": 1, "cx": 0.1}, {"t": 9, "cx": 0.9}]}),
               clip("rev", "V2", start=2, inp=1, out=5, reverse=True,
                    reframe={"zoom": 0.5, "keyframes": [{"t": 1, "cx": 0.1}, {"t": 5, "cx": 0.9}]})],
     "heads": [0, 0.5, 1.5, 2, 2.5, 3.5, 4.9]},
    {"name": "máscara de ajuste: base y fantasma; texto y figura sin dibujo de material", "out": [1080, 1920],
     "tracks": _V, "media": {"aj": [1920, 1080]},
     "clips": [ov("aj", masks=[{"type": "rectangle", "target": "adjust", "x": 0.6}], effects={"exposure": 0.5},
                  blend_mode="multiply", transform={"scale": 0.5}),
               clip("t", "T1", "text", start=0, out=4, text="hola"),
               clip("sh", "V2", "shape", start=0, out=4, shape={"type": "rect"})],
     "heads": [0.5, 2]},
    {"name": "clip sin material: no se dibuja", "out": [1080, 1920], "tracks": _V, "media": {"b": [1920, 1080]},
     "clips": [ov("a"), ov("b", "V2", transform={"scale": 0.3})],
     "heads": [1]},
]
