"""Casos sintéticos de paridad del AUDIO del export (audio_fx.py, clip_keyframes.volume_filter y
ffmpeg_envelope, clip_speed.audio_speed_filters y el bloque de audio de compose.build_command).

Clips sueltos (envolventes, volumen, efectos, velocidad) y timelines enteras, de las que se compara
la parte de audio del filter_complex que arma build_command (todo lo que va tras «[vout]»).
"""
from __future__ import annotations

from kf_cases import clip, items

_A = {"kind": "audio", "asset_kind": "audio", "filename": "a.mp3"}

CLIPS = [
    {"name": "sin nada", "clip": clip("n")},
    {"name": "volumen fijo", "clip": clip("v", volume=0.35)},
    {"name": "volumen fuera de rango y como texto", "clip": clip("v2", volume=5)},
    {"name": "efectos fijos", "clip": clip("e", audio_fx={"eq": True, "reverb": 0.3, "echo": False, "denoise": "0.5",
                                                          "distortion": 2, "compressor": 0.00005})},
    {"name": "filtros con etapas", "clip": clip("f", audio_fx={"underwater": 0.5, "muffled": 0.9, "telephone": 1,
                                                               "radio": 0.2, "megaphone": 0.7})},
    {"name": "etapas justo en una (k·N entero)", "clip": clip("f2", audio_fx={"underwater": 1 / 3, "muffled": 2 / 3})},
    {"name": "efectos raros", "clip": clip("e2", audio_fx={"eq": "nan", "reverb": [1], "echo": None, "denoise": -1,
                                                           "radio": "abc", "megaphone": "inf"})},
    {"name": "fundido de volumen con keyframes", "clip": clip("k1", keyframes=items(
        {"t": 0, "props": {"volume": 0}}, {"t": 1, "props": {"volume": 1}}, {"t": 3, "props": {"volume": 1}},
        {"t": 4, "interpolation": "ease-out", "props": {"volume": 0}}))},
    {"name": "todas las curvas en el volumen", "clip": clip("k2", keyframes=items(
        {"t": 0, "props": {"volume": 0.2}},
        *[{"t": 0.3 * (i + 1), "interpolation": k, "props": {"volume": (0.4 + 0.5 * (i % 3)) % 2}}
          for i, k in enumerate(["linear", "ease-in", "ease-out", "ease-in-out", "cubic-in", "cubic-out",
                                 "cubic-in-out", "back-out", "bezier", "hold", "direct", "raro", None])]))},
    {"name": "bézier propia en un efecto", "clip": clip("k3", audio_fx={"reverb": 0.5}, keyframes=items(
        {"t": 0, "props": {"reverb": 0}},
        {"t": 2, "interpolation": "bezier", "bezier": [0.1, 0.9, 0.2, 1.0], "props": {"reverb": 1}}))},
    {"name": "efecto con etapas animado", "clip": clip("k4", audio_fx={"underwater": 0.2}, keyframes=items(
        {"t": 0.5, "props": {"underwater": 0.1}}, {"t": 2.5, "props": {"underwater": 0.9}}))},
    {"name": "keyframe de efecto negativo", "clip": clip("k11", keyframes=items(
        {"t": 0, "props": {"echo": -0.5}}, {"t": 1, "props": {"echo": 0.5}}, {"t": 2, "props": {"echo": "-2"}}))},
    {"name": "etapa con un peso de 6e-4", "clip": clip("f3", audio_fx={"underwater": 0.0002, "muffled": 0.99985})},
    {"name": "keyframes que no cambian nada", "clip": clip("k5", volume=0.8, keyframes=items(
        {"t": 0, "props": {"volume": 0.8}}, {"t": 2, "props": {"volume": 0.80001}}))},
    {"name": "un solo keyframe", "clip": clip("k6", keyframes=items({"t": 1, "props": {"volume": 0.3}}))},
    {"name": "keyframes desactivados", "clip": clip("k7", volume=0.5, keyframes={
        "enabled": False, "items": [{"t": 0, "props": {"volume": 0}}, {"t": 1, "props": {"volume": 1}}]})},
    {"name": "t raros y desordenados", "clip": clip("k8", keyframes=items(
        {"t": 2, "props": {"volume": 1.5}}, {"t": "0.5", "props": {"volume": 0.1}}, {"t": None, "props": {"volume": 0.7}},
        {"t": "nan", "props": {"volume": 0.2}}, "x", {"t": 3.25, "interpolation": "back-out", "props": {"volume": 0}}))},
    {"name": "tramo de curva muy largo (tope de 120)", "clip": clip("k9", out_point=10, keyframes=items(
        {"t": 0, "props": {"volume": 0}}, {"t": 9, "interpolation": "back-out", "props": {"volume": 1}}))},
    {"name": "tramo de curva cortísimo", "clip": clip("k10", keyframes=items(
        {"t": 1, "props": {"volume": 0}}, {"t": 1.001, "interpolation": "bezier", "props": {"volume": 1}}))},
    # Velocidad
    {"name": "velocidad 1,5", "clip": clip("s1", speed=1.5)},
    {"name": "velocidad 4 (atempo encadenado)", "clip": clip("s2", speed=4)},
    {"name": "velocidad 0,2 (atempo encadenado)", "clip": clip("s3", speed=0.2)},
    {"name": "velocidad 0,3 sin conservar el tono", "clip": clip("s4", speed=0.3, keep_pitch=False)},
    {"name": "al revés", "clip": clip("s5", reverse=True)},
    {"name": "al revés a 2,5", "clip": clip("s6", reverse=True, speed=2.5)},
    {"name": "velocidad casi 1", "clip": clip("s7", speed=1.0005)},
    {"name": "audio con todo", "clip": clip("t", **_A, start=1.2345, in_point=0.5, out_point=3.25, speed=0.75,
                                            volume=1.3, audio_fx={"echo": 0.6, "telephone": True})},
]

ATEMPO = [0.1, 0.25, 0.4999999999, 0.5, 0.75, 1.0, 1.5, 2.0, 2.0000000001, 3.3, 4.0, 7.77, 10.0]
INTENSITY = [True, False, None, 0, 1, 2, -1, 0.5, "0.25", " 0.7 ", "abc", "nan", "inf", "-inf", [1], {}, ""]
WEIGHTS_K = [float("nan"), -1, 0, 1e-5, 0.1, 1 / 3, 0.5, 2 / 3, 0.9, 1, 2]

# Timelines enteras: (nombre, pistas, clips, material sin archivo, material sin audio). El
# material se simula: build_command ve un archivo por id de clip.
_T = [{"id": "V1", "kind": "video", "name": "V1"}, {"id": "A1", "kind": "audio", "name": "A1"}]
_V = {"kind": "video", "asset_kind": "clips", "filename": "v.mp4", "source_duration": 10}
_I = {"kind": "image", "asset_kind": "images", "filename": "i.png"}
TIMELINES = [
    ("un vídeo con audio", _T, [{"id": "v1", "track_id": "V1", **_V, "start": 0, "in_point": 1, "out_point": 3}], [], []),
    ("vídeo y música", _T, [
        {"id": "v1", "track_id": "V1", **_V, "start": 0, "in_point": 0, "out_point": 4},
        {"id": "m1", "track_id": "A1", **_A, "start": 0.5, "in_point": 2, "out_point": 6, "volume": 0.4}], [], []),
    ("pista silenciada, clip silenciado, sin audio y sin archivo", [
        *_T, {"id": "A2", "kind": "audio", "name": "A2", "muted": True}], [
        {"id": "v1", "track_id": "V1", **_V, "start": 0, "in_point": 0, "out_point": 2},
        {"id": "v2", "track_id": "V1", **_V, "start": 2, "in_point": 0, "out_point": 2, "muted": True},
        {"id": "v3", "track_id": "V1", **_V, "start": 4, "in_point": 0, "out_point": 2},
        {"id": "m1", "track_id": "A2", **_A, "start": 0, "in_point": 0, "out_point": 3},
        {"id": "m2", "track_id": "A1", **_A, "start": 1, "in_point": 0, "out_point": 3},
        {"id": "m3", "track_id": "A1", **_A, "start": 5, "in_point": 0, "out_point": 3}], ["m3"], ["v3"]),
    ("imagen, desactivado, oculto, corto y sin pista", [
        *_T, {"id": "V2", "kind": "video", "name": "V2", "hidden": True}], [
        {"id": "i1", "track_id": "V1", **_I, "start": 0, "in_point": 0, "out_point": 3},
        {"id": "v1", "track_id": "V2", **_V, "start": 0, "in_point": 0, "out_point": 3},
        {"id": "v2", "track_id": "V1", **_V, "start": 3, "in_point": 0, "out_point": 2, "disabled": True},
        {"id": "v3", "track_id": "V1", **_V, "start": 5, "in_point": 0, "out_point": 0.015},
        {"id": "v4", "track_id": "V9", **_V, "start": 6, "in_point": 0, "out_point": 2},
        {"id": "m1", "track_id": "A1", **_A, "start": 0, "in_point": 0, "out_point": 2}], [], []),
    ("inicio negativo, fuera de la rejilla y velocidad", _T, [
        {"id": "v1", "track_id": "V1", **_V, "start": -0.3, "in_point": 0, "out_point": 2},
        {"id": "v2", "track_id": "V1", **_V, "start": 2.0005, "in_point": 0.25, "out_point": 2, "speed": 2,
         "reverse": True},
        {"id": "m1", "track_id": "A1", **_A, "start": 0.0004, "in_point": 0, "out_point": 3, "speed": 0.5,
         "keep_pitch": False}], [], []),
    ("efectos y fundidos con keyframes", _T, [
        {"id": "v1", "track_id": "V1", **_V, "start": 0, "in_point": 0, "out_point": 4,
         "audio_fx": {"radio": 0.8}, "keyframes": {"enabled": True, "items": [
             {"t": 0, "props": {"volume": 0}}, {"t": 1, "interpolation": "ease-in", "props": {"volume": 1}}]}},
        {"id": "m1", "track_id": "A1", **_A, "start": 1, "in_point": 0, "out_point": 5,
         "audio_fx": {"underwater": 0.4, "eq": True}}], [], []),
    ("objetivo de sonoridad alto y bajo", _T, [
        {"id": "m1", "track_id": "A1", **_A, "start": 0, "in_point": 0, "out_point": 1}], [], []),
    ("id de clip repetido", _T, [
        {"id": "v1", "track_id": "V1", **_V, "start": 0, "in_point": 0, "out_point": 2},
        {"id": "v1", "track_id": "V1", **_V, "start": 2, "in_point": 3, "out_point": 5},
        {"id": "m1", "track_id": "A1", **_A, "start": 0, "in_point": 0, "out_point": 2}], [], []),
    ("pista de vídeo repetida (manda la última)", [
        {"id": "V1", "kind": "video", "name": "V1"}, {"id": "V1", "kind": "video", "name": "V1b", "muted": True},
        {"id": "A1", "kind": "audio", "name": "A1"}], [
        {"id": "v1", "track_id": "V1", **_V, "start": 0, "in_point": 0, "out_point": 2},
        {"id": "m1", "track_id": "A1", **_A, "start": 0, "in_point": 0, "out_point": 2}], [], []),
    ("inicio enorme (adelay sin límite de 64 bits, como el int de Python)", _T, [
        {"id": "m1", "track_id": "A1", **_A, "start": 1e16, "in_point": 0, "out_point": 2}], [], []),
    ("sin nada que suene", _T, [{"id": "i1", "track_id": "V1", **_I, "start": 0, "in_point": 0, "out_point": 2}],
     [], []),
]
# Objetivo de loudness por timeline (audio_target_db), si no es el de por defecto.
TARGETS = {"objetivo de sonoridad alto y bajo": [-2.0, -60.0, -23.456]}
