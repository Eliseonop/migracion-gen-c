"""Casos de los efectos de vídeo (`clip.vfx`, motor «shader») para export_fixtures.py.

Cada efecto se pasa por `window.__fx.renderVfx` del runtime real de video-yt (el que hornea el
export en Chromium, fx_bake.py) sobre una imagen de prueba con alfa, en varios instantes y con
otros parámetros, y se guarda el PNG que sale.
"""
from __future__ import annotations

CLIP_DUR = 3.0
TIMES = (0.37, 1.53)        # con los parámetros por defecto
VARIANT_T = 0.9             # con los parámetros cambiados (variant_params)


def variant_params(params: list[dict]) -> dict:
    """Otros valores para cada parámetro: el 25 % del rango, el toggle al revés, otro color, la
    última opción y otra semilla."""
    out = {}
    for p in params:
        t = p["type"]
        if t == "range":
            out[p["id"]] = p["min"] + 0.25 * (p["max"] - p["min"])
        elif t == "toggle":
            out[p["id"]] = not p["def"]
        elif t == "color":
            out[p["id"]] = "#33cc99"
        elif t == "select":
            out[p["id"]] = p["options"][-1]["id"]
        elif t == "seed":
            out[p["id"]] = 7
    return out


def make_input(path) -> None:
    """La imagen de prueba (128×96): ondas de color, un círculo con el borde semitransparente y
    una esquina transparente. Determinista: Pillow sin perfil de color."""
    import math

    from PIL import Image
    w, h = 128, 96
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            r = int(127.5 + 127.5 * math.sin(x * 0.21 + y * 0.05))
            g = int(127.5 + 127.5 * math.sin(y * 0.33 - x * 0.07 + 1.0))
            b = int(255 * x / (w - 1))
            d = math.hypot(x - 80, y - 44)
            if d < 22:
                r, g, b = 250, 240, 40
            a = 255
            if 22 <= d < 30:
                a = int(255 * (d - 22) / 8)
            if x < 20 and y < 16:
                a = 0
            px[x, y] = (r, g, b, a)
    img.save(path, format="PNG")


# --- Lo que decide fx_bake.py antes de hornear (active_vfx, pad_allowed, baked_clip) -----------
def _clip(cid, **kw):
    c = {"id": cid, "track_id": "V1", "kind": "video", "asset_kind": "clips", "asset_id": "0",
         "filename": "a.mp4", "start": 1.0, "in_point": 2.0, "out_point": 6.0, "source_duration": 10.0}
    c.update(kw)
    return c


_FX = {"effect": "glitch_rgb", "params": {"amount": 0.3}}
_PAD = {"effect": "float_shadow"}
_RF = {"zoom": 1.0, "keyframes": [{"t": 2.5, "cx": 0.4}, {"t": 5.0, "cx": 0.6, "zoom": 0.8}, {"t": 1.0, "cx": 0.5}]}
_FREE = {"layout": "overlay", "transform": {"x": 0.5, "y": 0.5, "scale": 0.5}}

BAKE_CLIPS = [
    _clip("sin"),
    _clip("uno", vfx=[_FX]),
    _clip("raros", vfx=[None, 3, "x", {"effect": "no_existe"}, {"effect": "glitch_rgb", "enabled": False},
                        {"effect": "glitch_rgb", "enabled": 0, "from": "1.5", "to": "x", "uid": 7, "v": "3"},
                        {"effect": "crt", "from": -2, "to": -5, "v": 2.7, "params": {"amount": "9", "zz": 1}},
                        {"effect": "crt", "from": float("nan"), "to": float("nan"), "v": 0},
                        {"effect": "crt", "from": "abc", "to": None, "params": None}]),
    _clip("imagen", kind="image", asset_kind="images", filename="a.png", loop=True, vfx=[_FX]),
    _clip("texto", kind="text", asset_kind="text", vfx=[_FX]),
    _clip("motion", kind="motion", vfx=[_FX]),
    _clip("encuadre", vfx=[_FX], reframe=_RF, speed=2.0),
    _clip("reves", vfx=[_FX], reframe=_RF, speed=0.5, reverse=True, bg_removal={"chroma": {"enabled": True}}),
    _clip("margen", vfx=[_PAD, _FX], **_FREE),
    _clip("margen_dos", vfx=[_PAD, _PAD, _PAD], **_FREE),
    _clip("margen_recorte", vfx=[_PAD], layout="overlay",
          reframe={"crop_w": 0.5, "crop_h": 1.0}, transform={"x": 0.5, "y": 0.5, "scale": 0.5}),
    _clip("margen_lleno", vfx=[_PAD]),
    _clip("margen_cinco", vfx=[_PAD] * 5, **_FREE),
    # int(v) de Python fuera del try: lanza.
    _clip("version_texto", vfx=[{"effect": "crt", "v": "abc"}]),
    _clip("version_decimal", vfx=[{"effect": "crt", "v": "2.5"}]),
    _clip("version_nan", vfx=[{"effect": "crt", "v": float("nan")}]),
    _clip("version_lista", vfx=[{"effect": "crt", "v": [1]}]),                        # 0,6 → 0,5
    # Enseña la imagen entera salvo al final (el encuadre hace zoom en el último instante).
    _clip("margen_final", vfx=[_PAD], layout="overlay", transform={"x": 0.5, "y": 0.5, "scale": 0.5},
          reframe={"keyframes": [{"t": 2.0, "cx": 0.5, "zoom": 1.0}, {"t": 5.9, "cx": 0.5, "zoom": 1.0},
                                 {"t": 6.0, "cx": 0.5, "zoom": 0.5}]}),
]
# (iw, ih, W, H) para pad_allowed
PAD_DIMS = [(1920, 1080, 1080, 1920), (1080, 1920, 1080, 1920), (640, 480, 1280, 720), (0, 480, 1280, 720),
            (640, 480, 0, 720)]
