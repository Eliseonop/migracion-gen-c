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
