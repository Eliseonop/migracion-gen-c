"""Casos sintéticos de cómo lee el export el material de un clip (clip_kind.py:
is_still_clip, ffmpeg_trim_window, ffmpeg_trim_filter y ffmpeg_input_args).

Los usa export_fixtures.py → tests/fixtures/export_input/synthetic.json.
"""
from __future__ import annotations

NAN = float("nan")
INF = float("inf")


def _clip(id_: str, kind: str = "video", asset_kind: str | None = None, inp=0.0, out=5.0, src=10.0, **extra) -> dict:
    asset = asset_kind or {"video": "clips", "audio": "audios", "image": "images", "text": "text",
                           "shape": "shape", "motion": "motion"}.get(kind, "clips")
    c = {"id": id_, "track_id": "V1", "kind": kind, "asset_kind": asset, "asset_id": id_,
         "filename": f"{id_}.mp4", "start": 0.0, "in_point": inp, "out_point": out, "source_duration": src}
    c.update(extra)
    return c


CLIPS = [
    _clip("normal", inp=1.0, out=4.0),
    _clip("hasta_el_final", inp=2.0, out=10.0),
    _clip("casi_el_final", inp=2.0, out=10.0 - 1e-4),
    _clip("antes_del_margen", inp=2.0, out=10.0 - 1.1e-4),
    _clip("pasado_el_final", inp=2.0, out=12.5),
    _clip("sin_duracion_fuente", inp=2.0, out=10.0, src=0.0),
    _clip("final_y_entrada_tardia", inp=9.99, out=10.0),
    _clip("entrada_despues_del_final", inp=11.0, out=12.0),
    _clip("decimales", inp=0.0005, out=3.3335, src=3.3335),
    _clip("redondeo_al_par", inp=0.0625, out=1.0625),
    _clip("nan", inp=NAN, out=4.0),
    _clip("imagen", "image", inp=0.0, out=5.0, src=0.0),
    _clip("imagen_corta", "image", inp=2.0, out=2.05, src=0.0),
    _clip("imagen_al_reves", "image", inp=3.0, out=1.0, src=0.0),
    _clip("imagen_infinita", "image", inp=INF, out=INF, src=0.0),  # inf − inf: NaN negativo en x86
    _clip("figura", "shape", inp=0.0, out=2.5, src=0.0),
    _clip("video_de_images", "video", "images", inp=1.0, out=4.0),
    _clip("gif_loop", "image", inp=0.0, out=3.0, src=0.0, loop=True),
    _clip("gif_sin_loop", "image", inp=0.0, out=3.0, src=0.0, loop=False),
    _clip("motion", "motion", inp=0.0, out=2.0, src=2.0),
    _clip("audio", "audio", inp=0.5, out=8.0, src=8.0),
]

FPS = [30, 24, 25, 60, 1, 0, -5]

PATHS = [
    r"C:\media\clip.mp4", "clip.WEBM", "anim.webm", "anim.gif", "ANIM.GIF", "frames\\seq_%04d.png",
    "img.png", "carpeta.webm\\x.mp4", "Ünïcødé.WebM", "100%.mp4", "",
]
