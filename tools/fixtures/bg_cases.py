"""Casos del matte de «Eliminar fondo» leído de la caché (bg/service.py y clip_bg.py de video-yt).

Los usa export_fixtures.py:
  - tests/fixtures/media/bg_matte.json (--solo matte): build_clip_bg_mask REAL sobre cachés
    sintéticas (MATTES) y la LUT de matte_lut (LUT_CASES).
  - las escenas «eliminar fondo: …» de media_cases.RENDER_SCENES, con la caché de
    tests/fixtures/render/files/bgcache (RENDER_MATTES, que crea make_matte_cache).

Un matte crudo (nivel 1) es una secuencia PNG en gris en bgcache/matte/<base_key>/000001.png… con
su meta.json. Aquí cada fotograma es distinto (un disco suave que se mueve), así que el test puede
saber de qué fotograma crudo sale cada fotograma derivado.
"""
from __future__ import annotations

import json
from pathlib import Path


def matte_frame(w: int, h: int, i: int, n: int, shape: str):
    """Fotograma i de n de un matte crudo: un disco con el borde en rampa (cubre 0–255, así que la
    LUT de umbral y suavidad se nota) que se mueve en horizontal; `bars`, franjas verticales. La
    primera fila lleva el índice en binario (0 o 255 por bit): dos fotogramas no salen iguales
    después de la LUT, así que se sabe de qué crudo sale cada derivado."""
    import numpy as np
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    if shape == "bars":
        v = np.clip(np.floor(((x / max(1, w - 1)) * 255.0 + i * 13.0) % 256.0), 0, 255)
    else:
        cx = w * (0.3 + 0.4 * (i / max(1, n - 1)))
        cy = h * 0.5
        r = 0.38 * min(w, h)
        d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / r
        v = np.floor(np.clip((1.25 - d) * 255.0, 0, 255) + 0.5)
    out = v.astype(np.uint8)
    for bit in range(min(16, w)):
        out[0, bit] = 255 if (i >> bit) & 1 else 0
    return out


def make_matte_cache(root: Path, mattes: dict) -> None:
    """Escribe en root/matte/<base_key>/ los PNG en gris y el meta.json de cada matte de `mattes`
    ({base_key: {...}}): `frames` fotogramas desde el índice `first`, `size` (ancho, alto), `skip`
    (índices que faltan), `meta` (lo que va en meta.json; None = sin meta, "roto" = no es JSON)."""
    from PIL import Image
    for key, spec in mattes.items():
        folder = root / "matte" / key
        folder.mkdir(parents=True, exist_ok=True)
        w, h = spec["size"]
        first, frames = spec.get("first", 0), spec["frames"]
        for i in range(first, first + frames):
            if i in spec.get("skip", ()):
                continue
            Image.fromarray(matte_frame(w, h, i, first + frames, spec.get("shape", "disc")), mode="L").save(
                folder / f"{i + 1:06d}.png")
        meta = spec.get("meta", "auto")
        if meta == "auto":
            meta = {"base_key": key, "provider": "u2net", "model_version": "u2net-1", "mask_fps": spec.get("fps", 15),
                    "mask_height": h, "width": w, "height": h, "source_duration": spec.get("source_duration", -1.0),
                    "source_id": f"{key}.png:1:1:0", "range": [first, first + frames - 1], "device": "cpu",
                    "updated_at": 1789539484}
        if meta == "roto":
            (folder / "meta.json").write_text("{no es json", encoding="utf-8")
        elif meta is not None:
            (folder / "meta.json").write_text(json.dumps(meta, ensure_ascii=False, indent=1), encoding="utf-8")


# --- build_clip_bg_mask: cachés sintéticas y clips ---------------------------------------------

def _meta(key, first, last, fps=15, source_duration=-1.0, **kw):
    return {"base_key": key, "provider": "u2net", "model_version": "u2net-1", "mask_fps": fps, "mask_height": 48,
            "width": 64, "height": 48, "source_duration": source_duration, "source_id": f"{key}:1:1:0",
            "range": [first, last], **kw}


MATTES = {
    "img": {"size": (64, 48), "frames": 1},
    "vid": {"size": (64, 48), "frames": 61},                      # 4 s a 15 fps: 0..60
    "vid10": {"size": (64, 48), "frames": 41, "fps": 10, "meta": _meta("vid10", 0, 40, fps=10)},
    "corto": {"size": (64, 48), "first": 10, "frames": 6},          # solo 10..15
    "hueco": {"size": (64, 48), "frames": 31, "skip": {12}},        # falta el 12
    "sin_lo": {"size": (64, 48), "frames": 31, "skip": {0}},         # falta el primero del rango
    "gif": {"size": (64, 48), "frames": 31, "source_duration": 2.0},  # GIF de 2 s
    "gif_corto": {"size": (64, 48), "frames": 31, "meta": _meta("gif_corto", 0, 30, source_duration=1.0)},
    "sin_meta": {"size": (64, 48), "frames": 5, "meta": None},
    "meta_rota": {"size": (64, 48), "frames": 5, "meta": "roto"},
    "meta_lista": {"size": (64, 48), "frames": 5, "meta": [1, 2]},
    "sin_rango": {"size": (64, 48), "frames": 5, "meta": {"base_key": "sin_rango", "mask_fps": 15}},
    "rango_raro": {"size": (64, 48), "frames": 5, "meta": _meta("rango_raro", 0, 4) | {"range": [0, 1, 2]}},
    "rango_texto": {"size": (64, 48), "frames": 5, "meta": _meta("rango_texto", 0, 4) | {"range": ["1", 3.7]}},
    "fps_cero": {"size": (64, 48), "frames": 31, "meta": _meta("fps_cero", 0, 30) | {"mask_fps": 0}},
    "fps_texto": {"size": (64, 48), "frames": 31, "meta": _meta("fps_texto", 0, 30) | {"mask_fps": "12"}},
}


def _auto(key, **kw):
    return {"enabled": True, "mode": "auto",
            "auto": {"enabled": True, "provider": "u2net", "model_version": "u2net-1", "base_key": key,
                     "status": "ready", "mask_fps": 15, "mask_height": 48, **kw}}


def _clip(kind, filename, out_point, bg, **kw):
    asset = {"video": "clips", "image": "images", "audio": "audio", "text": "text"}[kind]
    return {"id": "c1", "track_id": "V1", "kind": kind, "asset_kind": asset, "asset_id": "a1", "filename": filename,
            "start": 0.0, "in_point": 0.0, "out_point": out_point, "source_duration": 0.0 if kind == "image" else 4.0,
            "layout": "overlay", "bg_removal": bg, **kw}


# (nombre, clip): el clip va por schemas.TimelineClip; los fps de la timeline, 30.
SPEC_CLIPS = [
    ("imagen", _clip("image", "a.png", 2.0, _auto("img"))),
    ("vídeo entero", _clip("video", "a.mp4", 4.0, _auto("vid"))),
    ("vídeo con in_point", _clip("video", "a.mp4", 3.1, _auto("vid"), in_point=1.27)),
    ("vídeo a velocidad 2", _clip("video", "a.mp4", 3.0, _auto("vid"), in_point=0.5, speed=2.0)),
    ("vídeo a velocidad 0,5", _clip("video", "a.mp4", 1.3, _auto("vid"), in_point=0.2, speed=0.5)),
    ("vídeo al revés", _clip("video", "a.mp4", 2.5, _auto("vid"), in_point=0.4, reverse=True)),
    ("vídeo con un matte a 10 fps", _clip("video", "a.mp4", 3.0, _auto("vid10", mask_fps=24), in_point=0.35)),
    ("vídeo más largo que la caché", _clip("video", "a.mp4", 2.0, _auto("corto"), in_point=0.3)),
    ("vídeo antes de la caché", _clip("video", "a.mp4", 0.5, _auto("corto"))),
    ("vídeo después de la caché", _clip("video", "a.mp4", 3.5, _auto("corto"), in_point=3.0)),
    ("vídeo con un fotograma que falta", _clip("video", "a.mp4", 1.5, _auto("hueco"))),
    ("vídeo sin el primer fotograma", _clip("video", "a.mp4", 1.5, _auto("sin_lo"))),
    ("vídeo muy corto", _clip("video", "a.mp4", 0.01, _auto("vid"), in_point=1.0)),
    ("GIF", _clip("image", "a.gif", 3.0, _auto("gif"))),
    ("GIF más corto que la caché", _clip("image", "a.gif", 3.0, _auto("gif_corto"))),
    ("sin meta.json", _clip("video", "a.mp4", 1.0, _auto("sin_meta"))),
    ("meta.json roto", _clip("video", "a.mp4", 1.0, _auto("meta_rota"))),
    ("meta.json que no es un objeto", _clip("video", "a.mp4", 1.0, _auto("meta_lista"))),
    ("meta.json sin rango", _clip("video", "a.mp4", 1.0, _auto("sin_rango"))),
    ("rango de tres", _clip("video", "a.mp4", 1.0, _auto("rango_raro"))),
    ("rango con texto y reales", _clip("video", "a.mp4", 1.0, _auto("rango_texto"))),
    ("mask_fps 0 en el meta", _clip("video", "a.mp4", 1.0, _auto("fps_cero", mask_fps=20))),
    ("mask_fps de texto en el meta", _clip("video", "a.mp4", 1.0, _auto("fps_texto"))),
    ("base_key que no existe", _clip("video", "a.mp4", 1.0, _auto("no_existe"))),
    ("matte sin terminar", _clip("video", "a.mp4", 1.0, _auto("vid", status="running"))),
    ("matte apagado", _clip("video", "a.mp4", 1.0, _auto("vid", enabled=False))),
    ("eliminar fondo apagado", _clip("video", "a.mp4", 1.0, {**_auto("vid"), "enabled": False})),
    ("sin base_key", _clip("video", "a.mp4", 1.0, _auto(""))),
    ("modo croma con el matte listo", _clip("video", "a.mp4", 1.0, {**_auto("vid"), "mode": "chroma"})),
    ("un audio", _clip("audio", "a.wav", 1.0, _auto("vid"))),
    ("con pluma y expansión", _clip("video", "a.mp4", 1.0, _auto("vid", feather=0.05, expansion=0.2))),
]

# matte_lut: umbral, suavidad e invertir (normalize_auto los acota a [0, 1]).
LUT_CASES = [{"threshold": t, "softness": s, "invert": inv}
             for t in (0.0, 0.1, 0.35, 0.5, 0.501, 0.75, 1.0)
             for s in (0.0, 0.003, 0.004, 0.1, 0.25, 0.6, 1.0)
             for inv in (False, True)] + [
    {"threshold": "0.4", "softness": None, "invert": 1}, {"threshold": float("nan"), "softness": 0.2, "invert": 0},
    {"threshold": 2, "softness": -1, "invert": "x"}, {}]

# derive_matte (clip_bg.py) más allá de la LUT: pluma, expansión, opacidad y el pincel. Cada caso:
# (nombre, tamaño del crudo, forma, `auto` sin normalizar). El crudo es matte_frame(w, h, 3, 8, forma).
def _stroke(op, pts, size=0.08, **kw):
    return {"op": op, "size": size, "points": [dict(zip(("x", "y"), p)) | ({"m": 1} if len(p) > 2 else {})
                                               for p in pts], **kw}


DERIVE_CASES = [
    ("solo la LUT", (96, 54), "disc", {}),
    ("pluma pequeña (σ ≤ 0,3: nada)", (96, 54), "disc", {"feather": 0.005}),
    ("pluma", (96, 54), "disc", {"feather": 0.05}),
    ("pluma máxima", (96, 54), "disc", {"feather": 0.15}),
    ("pluma con reducción (σ ≥ 32)", (360, 240), "disc", {"feather": 0.15}),
    ("pluma con reducción ×3", (480, 360), "disc", {"feather": 0.14}),
    # Con un factor no entero y franjas (no un disco suave), INTER_AREA no es INTER_LINEAR.
    ("pluma con reducción no entera ×2, franjas", (351, 231), "bars", {"feather": 0.15}),
    ("pluma con reducción no entera ×3, franjas", (481, 361), "bars", {"feather": 0.14}),
    # Un trazo grueso: extremos de radio ≥ 15 px (EllipseEx con 5° por lado).
    ("pincel grueso", (160, 90), "disc", {"edits": [_stroke("keep", [(0.3, 0.5), (0.6, 0.4)], 0.4),
                                                    _stroke("erase", [(0.85, 0.2)], 0.45)]}),
    ("expansión", (96, 54), "disc", {"expansion": 0.4}),
    ("contracción", (96, 54), "disc", {"expansion": -0.6}),
    ("expansión mínima (|e| < 1e-4: nada)", (96, 54), "disc", {"expansion": 0.00005}),
    ("expansión pequeña (σ ≤ 0,3)", (96, 54), "disc", {"expansion": 0.05}),
    ("expansión y pluma", (160, 90), "bars", {"expansion": 0.3, "feather": 0.03}),
    ("opacidad", (96, 54), "disc", {"opacity": 0.4}),
    ("opacidad 0", (96, 54), "disc", {"opacity": 0.0}),
    ("umbral, invertir y pluma", (96, 54), "disc", {"threshold": 0.3, "softness": 0.6, "invert": True, "feather": 0.04}),
    ("pincel: conservar", (96, 54), "disc", {"edits": [_stroke("keep", [(0.1, 0.2), (0.4, 0.8)])]}),
    ("pincel: borrar", (96, 54), "disc", {"edits": [_stroke("erase", [(0.5, 0.1), (0.6, 0.9), (0.9, 0.5)], 0.12)]}),
    ("pincel: un punto", (96, 54), "disc", {"edits": [_stroke("keep", [(0.8, 0.3)], 0.2)]}),
    ("pincel: dos trazos con «m»", (96, 54), "disc",
     {"edits": [_stroke("erase", [(0.2, 0.2), (0.3, 0.3), (0.7, 0.2, 1), (0.8, 0.8)])]}),
    ("pincel: conservar y borrar, con pluma", (160, 90), "disc",
     {"feather": 0.02, "edits": [_stroke("keep", [(0.05, 0.5), (0.3, 0.5)], 0.1),
                                 _stroke("erase", [(0.4, 0.4), (0.5, 0.6)], 0.05)]}),
    ("pincel muy fino (grosor 1)", (96, 54), "disc", {"edits": [_stroke("erase", [(0.1, 0.5), (0.9, 0.5)], 0.002)]}),
    ("pincel fuera del lienzo", (96, 54), "disc", {"edits": [_stroke("keep", [(-0.2, 0.5), (1.3, 0.6)], 0.1)]}),
    ("SAM: los trazos no se pintan", (96, 54), "disc",
     {"provider": "sam21_tiny", "edits": [_stroke("keep", [(0.1, 0.2), (0.4, 0.8)], tool="smart")]}),
    ("todo a la vez", (160, 90), "disc",
     {"threshold": 0.45, "softness": 0.3, "feather": 0.06, "expansion": -0.2, "opacity": 0.7,
      "edits": [_stroke("keep", [(0.6, 0.3), (0.9, 0.7)], 0.07)]}),
]

# derive_matte sobre los mattes REALES de la caché de video-yt (--solo matte --real): los ajustes que se
# prueban con un fotograma de cada proveedor y tamaño.
DERIVE_REAL_AUTOS = [
    ("pluma 0,02", {"feather": 0.02}),
    ("pluma 0,08", {"feather": 0.08}),
    ("pluma 0,15", {"feather": 0.15}),
    ("expansión 0,3", {"expansion": 0.3}),
    ("contracción 0,3 y opacidad 0,5", {"expansion": -0.3, "opacity": 0.5}),
    ("umbral, suavidad y pluma", {"threshold": 0.35, "softness": 0.5, "feather": 0.04}),
    ("pincel y pluma", {"feather": 0.03, "edits": [_stroke("keep", [(0.2, 0.3), (0.45, 0.7)], 0.05),
                                                   _stroke("erase", [(0.6, 0.2), (0.8, 0.5), (0.7, 0.9)], 0.08)]}),
]

# La caché de las escenas de render (tests/fixtures/render/files/bgcache): el material es de 320×180.
RENDER_MATTES = {
    "escena_imagen": {"size": (320, 180), "frames": 1},
    "escena_pequeno": {"size": (80, 45), "frames": 1},            # ×4: el bicúbico se nota en el borde
    "escena_video": {"size": (320, 180), "frames": 31},           # 2 s a 15 fps
    "escena_corto": {"size": (320, 180), "frames": 8},            # solo 0..7: el resto repite el 7
    "escena_gif": {"size": (320, 180), "frames": 31, "source_duration": 2.0},
}
