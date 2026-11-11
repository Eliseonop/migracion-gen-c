"""Casos sintéticos de paridad de la timeline del EDITOR (gc_core timeline_ui ↔ editorModel.js).

Timelines aleatorias (semilla fija) con clips solapados, pegados y con huecos, de todos los tipos
y velocidades (también las que JS sustituye por 1), y llamadas a las funciones que usa la timeline
al arrastrar, recortar y seleccionar. Node las ejecuta sobre el JS real (editor_ui_js.mjs), con los
clips tal como los deja pydantic.
"""
from __future__ import annotations

import math
import random

from ops_cases import clip, track

TRACKS = [track("V1", "video"), track("V2", "video"), track("T1", "text"), track("A1", "audio")]
KINDS = {"V1": ["video", "video", "image", "shape"], "V2": ["video", "image", "adjustment"], "T1": ["text"],
         "A1": ["audio"]}
SPEEDS = [1.0, 1.0, 1.0, 0.5, 2.0, 0.0, -1.0, 20.0, 0.3, 0.05]
MODES = ["trim-left", "trim-right"]
MODS = [{}, {"additive": True}, {"range": True}, {"keepGroup": True}, {"additive": True, "range": True}]

# +x.toFixed(3): empates exactos (0.0625 → 0.063 en JS, 0.062 con round de Python) y casi empates.
FIXED = [0.0625, -0.0625, 0.1875, 2.0625, 1.0005, 0.0005, 2.675, 1e-4, -1e-4, 1234.5675, 0.1 + 0.2,
         59.99950000000001, 3.0, -0.0, 1e21 / 1e18, 7.0004999999999995, 0.30000000000000004 * 3]


EXTRA_TRACKS = [track("V3", "video", locked=True), track("A2", "audio", magnetic=True), track("T2", "text", hidden=True),
                track("V4", "video", magnetic=True)]
LANE = {"video": "video", "image": "video", "shape": "video", "adjustment": "video", "text": "text", "audio": "audio"}


def geo(tracks: list[dict]) -> list[dict]:
    """Filas en pantalla como las dibuja la timeline: la pila al revés y luego el audio; el texto
    mide round(52 · 0,55) y lo demás 52, debajo de la regla de 22 px."""
    stack = [t for t in tracks if t["kind"] in ("video", "text")]
    rows, y = [], 22
    for t in stack[::-1] + [t for t in tracks if t["kind"] not in ("video", "text")]:
        h = 29 if t["kind"] == "text" else 52
        rows.append({"id": t["id"], "top": y, "bottom": y + h})
        y += h
    return rows


def _random_timeline(rng: random.Random) -> dict:
    clips = []
    for n in range(rng.randint(5, 14)):
        tid = rng.choice(["V1", "V1", "V2", "T1", "A1"])
        kind = rng.choice(KINDS[tid])
        start = rng.choice([round(rng.uniform(0, 20), 3), rng.randint(0, 16) * 0.5, 0.0])
        inp = rng.choice([0.0, 0.0, round(rng.uniform(0, 5), 3)])
        out = round(inp + rng.choice([0.1, 0.15, 1.0, 2.5, rng.uniform(0.05, 6)]), 3)
        kw = {"speed": rng.choice(SPEEDS)}
        if kind not in ("text", "image", "shape", "adjustment"):
            kw["source_duration"] = rng.choice([0.0, out, round(out + rng.uniform(0, 10), 3)])
        else:
            kw["source_duration"] = rng.choice([0.0, out, round(out + 2, 3)])
        if kind == "adjustment":
            kw["asset_kind"] = "shape"
        if kind in ("video", "audio") and rng.random() < 0.3:
            kw["beats"] = {"times": sorted(round(rng.uniform(0, 12), 3) for _ in range(rng.randint(1, 12))),
                           "bpm": 120, "every": rng.choice([1, 2, 4, 3, None, "2"])}
        if rng.random() < 0.1:
            kw["disabled"] = True
        clips.append(clip(f"c{n}", tid, kind, start=start, inp=inp, out=out, **kw))
    tracks = TRACKS + rng.sample(EXTRA_TRACKS, k=rng.randint(0, len(EXTRA_TRACKS)))
    rng.shuffle(tracks)
    for _ in range(rng.randint(0, 3)):
        extra = rng.choice([t for t in tracks if t["id"] in ("V3", "A2", "T2", "V4")] or [None])
        if extra:
            kind = {"video": "video", "audio": "audio", "text": "text"}[extra["kind"]]
            clips.append(clip(f"x{len(clips)}", extra["id"], kind, start=round(rng.uniform(0, 10), 3), out=round(rng.uniform(0.5, 4), 3)))
    markers = [{"id": f"m{i}", "t": rng.choice([round(rng.uniform(0, 20), 3), -1, "3.5", None])} for i in range(rng.randint(0, 4))]
    return {"tracks": tracks, "clips": clips, "markers": markers}


def _calls(rng: random.Random, tl: dict) -> list:
    ids = [c["id"] for c in tl["clips"]]
    by_track = {t["id"]: [c["id"] for c in tl["clips"] if c["track_id"] == t["id"]] for t in TRACKS}
    calls: list = [["displayTracks"]] + [["clipDur", i] for i in ids]
    for tid, row in by_track.items():
        calls.append(["clipsOnTrackSorted", tid])
        for _ in range(6):
            desired = rng.choice([-1.0, 0.0, round(rng.uniform(0, 25), 3), rng.randint(0, 40) * 0.5])
            dur = rng.choice([0.0, 0.5, 1.0, round(rng.uniform(0.05, 8), 3), 50.0])
            exclude = rng.sample(row, k=min(len(row), rng.randint(0, 2)))
            calls.append(["freeStartOnTrack", tid, desired, dur, exclude])
            calls.append(["clampStartNoOverlap", tid, desired, dur, exclude])
        for _ in range(3):
            pick = row + ["nada"]
            calls.append(["rangeSelectOnTrack", tid, rng.choice(pick), rng.choice(pick)])
    for i in ids:
        calls.append(["trimBounds", i])
        for mode in MODES:
            for d in (-10.0, -1.0, -0.07, 0.0, 0.5, 3.0, 100.0):
                calls.append(["trimClipPatch", i, mode, d])
            for d in (-5.0, -0.5, 0.3, 4.0):
                calls.append(["clampTrimDelta", i, mode, d])
    for _ in range(40):
        sel = rng.sample(ids + ["fantasma"], k=rng.randint(0, 3))
        anchor = rng.choice(ids + [None, "fantasma"])
        calls.append(["nextClipSelection", sel, anchor, rng.choice(ids + ["fantasma"]), rng.choice(MODS)])
    rows = geo(tl["tracks"])
    bottom = rows[-1]["bottom"] if rows else 22
    track_ids = [t["id"] for t in tl["tracks"]]
    for pps in (0, -3, 0.5, 40, 300):
        calls.append(["alignThresholdSec", pps])
    calls.append(["snapTargets", []])
    calls.append(["snapTargets", rng.sample(ids, k=min(3, len(ids)))])
    for i in ids:
        calls.append(["clipBeatTimes", i])
        thr = rng.choice([0.0, 0.1, 0.2, 0.5, 2.0])
        calls.append(["timelineAlignHits", [i], thr, rng.choice(["both", "start", "end"])])
        for d in (-3.0, -0.4, 0.0, 0.13, 1.7, round(rng.uniform(-8, 8), 3)):
            calls.append(["snapClipMove", i, d, thr, rng.choice([None, rng.choice(track_ids)])])
            for mode in MODES:
                calls.append(["snapClipTrim", i, mode, d, thr])
    for _ in range(15):
        calls.append(["nearestAlignTime", round(rng.uniform(-1, 22), 3), rng.choice(track_ids), rng.choice([0.05, 0.2, 1.0])])
        group = rng.sample(ids, k=rng.randint(1, min(4, len(ids))))
        calls.append(["snapClipGroup", group, round(rng.uniform(-6, 6), 3), rng.choice([0.1, 0.3])])
        calls.append(["clampGroupDelta", group, round(rng.uniform(-10, 10), 3)])
        tid = rng.choice(track_ids)
        ex = rng.sample(ids, k=rng.randint(0, 2))
        calls.append(["clipAtTime", tid, round(rng.uniform(0, 20), 3), ex])
        calls.append(["clipCollidingWith", tid, round(rng.uniform(0, 20), 3), rng.choice([0, 0.5, 3.0]), ex])
        calls.append(["placeOnTrack", tid, round(rng.uniform(-1, 20), 3), rng.choice([0, 0.5, 2.0, 6.0]), ex,
                      rng.choice([0, 0.4, 1.2, 100]), rng.choice([0, 0.2])])
        calls.append(["liftTarget", rng.choice(track_ids + ["nada"]), round(rng.uniform(0, 20), 3), rng.choice([0.5, 3.0]), ex])
        calls.append(["magneticCut", tid, round(rng.uniform(-1, 25), 3), ex])
    starts = [c["start"] for c in tl["clips"]]
    for _ in range(80):
        kind = rng.choice(["video", "text", "audio"])
        y = rng.choice([0, 10, 22, 25, 30, round(rng.uniform(0, bottom + 40), 1)] + [r["top"] + 3 for r in rows] + [r["bottom"] - 2 for r in rows])
        origin = rng.choice(track_ids + [None])
        calls.append(["zoneAt", kind, y, origin, rng.random() < 0.4])
        mover = rng.choice(ids + [None])
        near = rng.choice(starts) + rng.choice([-0.9, -0.3, -0.05, 0.1, 0.6, 1.5]) if starts else 0.0
        q = {"kind": kind, "y": y, "time": round(rng.choice([rng.uniform(-2, 20), near]), 3), "dur": rng.choice([0, 0.5, 2.0, 5.0]),
             "excludeIds": [mover] if mover else [], "originId": origin, "originAlone": rng.random() < 0.3,
             "replace": rng.random() < 0.2, "pps": rng.choice([0, 10, 60, 300]), "snapSec": rng.choice([0, 0.15])}
        if rng.random() < 0.5:
            q["pointerTime"] = round(rng.uniform(0, 20), 3)
        calls.append(["dropTarget", q])
    for _ in range(6):
        calls.append(["toggleMarkerAt", rng.choice([round(rng.uniform(0, 20), 3), 3.5, 3.51, 3.52]), rng.choice([30, 24, 60, 0])])
        calls.append(["nextSnapTime", round(rng.uniform(-1, 20), 3), rng.choice([1, -1])])
    calls.append(["normalizeMarkers", [{"id": "a", "t": "2", "label": "x" * 70, "color": "#fff"}, {"t": -4, "label": "ñ😀" * 40},
                                       {"id": 7, "t": 1e-4, "label": 0, "color": ""}, None, 5, {"id": "z", "t": None, "label": True}]])
    for _ in range(4):
        calls.append(["emptiedTracks", rng.sample(track_ids + ["nada"], k=rng.randint(0, len(track_ids)))])
    for kind in ("video", "text", "audio"):
        for side in ("above", "below"):
            calls.append(["newTrackIndex", kind, side])
    for tid in track_ids + ["nada"]:
        for place in ("above", "below"):
            calls.append(["slotBoundary", {"targetId": tid, "place": place}])
            calls.append(["slotIndex", {"targetId": tid, "place": place}])
    for _ in range(5):
        origs = [{"id": i, "start": next(c["start"] for c in tl["clips"] if c["id"] == i)}
                 for i in rng.sample(ids, k=rng.randint(1, min(4, len(ids))))]
        calls.append(["groupMoveFromOrig", origs, rng.choice([-30.0, -1.25, 0.0, 0.7, round(rng.uniform(-5, 5), 3)])])
    for i in ids + ["zz"]:  # capas en la pista (clipLayerInfo, moveClipLayer)
        calls.append(["canLayerClip", i])
        calls.append(["clipLayerInfo", i])
        for action in ("back", "backward", "forward", "front", "otra"):
            calls.append(["moveClipLayer", i, action])
    return calls


def _view_calls(rng: random.Random) -> list:
    fpss = [24, 25, 30, 50, 60, 29.97, 0, 120]
    durs = [0.0, 5.0, 12.0, 61.5, 600.0, 7200.0, -3.0]
    calls: list = []
    for fps in fpss:
        calls.append(["tickSteps", fps])
        for d in durs:
            calls.append(["maxPps", fps, d])
            for w in (0, 50, 900, 1600):
                calls.append(["minPps", d, w])
                for pps in (0, 0.01, 5, 80, 1e6):
                    calls.append(["clampPps", pps, d, w, fps])
            calls.append(["zoomByDrag", 40.0, rng.uniform(-300, 300), d, 1200, fps])
        for pps in (0, 0.001, 0.5, 3, 12, 75, 400, 2400, 1e5):
            calls.append(["tickStep", pps, fps, False])
            calls.append(["tickStep", pps, fps, True])
    for _ in range(300):
        t = rng.choice([0.0, rng.uniform(0, 10), rng.uniform(0, 8000), rng.randint(0, 600) / 30, -5.0, 3599.999])
        step = rng.choice([None, 0.04, 0.5, 0.999, 1.0, 60.0])
        opts = {"fps": rng.choice(fpss), "long": rng.random() < 0.2}
        if step is not None:
            opts["step"] = step
        calls.append(["fmtRuler", t, opts])
    for _ in range(80):
        opts = {"fps": rng.choice(fpss), "dense": rng.random() < 0.3, "scrollX": rng.choice([0, rng.uniform(0, 5000)]),
                "viewW": rng.choice([0, 300, 1200, 1900])}
        calls.append(["buildTicks", rng.choice([0, 3.5, 30, 95.4, 3700]), rng.choice([0.2, 2, 18, 75, 300, 2400]), opts])
    for raw in (None, "", 0, 20, 33.5, 34, 52, 77.5, 120, 500, "abc", "60"):
        calls.append(["clampRowHeight", raw])
        for kind in ("video", "text", "audio"):
            calls.append(["trackRowHeight", kind, raw])
        calls.append(["stepRowHeight", raw, -1])
        calls.append(["stepRowHeight", raw, 1])
    return calls


POSE_CLIPS = [
    clip("v", "V1", start=1, out=4, layout="overlay", transform={"x": 0.3}),
    clip("vk", "V1", start=6, out=4, keyframes={"enabled": True, "items": [
        {"id": "k1", "t": 0, "props": {"x": 0.2}},
        {"id": "k2", "t": 1.5, "props": {"opacity": 0.5}, "interpolation": "ease_in", "bezier": [0.1, 0.2, 0.3, 1.4]},
        {"id": "k3", "t": 3.0, "props": {"font_size": 40}, "ease": "step"}]}),
    clip("img", "V2", "image", start=0, out=5, reframe={"zoom": 0.8, "keyframes": [{"t": 0.5, "cx": 0.3, "cy": 0.6}]}),
    clip("t", "T1", "text", start=0, out=3, text="hola", style={"x": 0.4, "font_size": 50},
         keyframes={"enabled": True, "items": [{"id": "s1", "t": 0.5, "props": {"font_size": 40}}]}),
    clip("tk", "T1", "text", start=4, out=3, text="adiós", style={"y": 0.7},
         keyframes={"enabled": True, "items": [{"id": "p1", "t": 1, "props": {"y": 0.2}}, {"id": "p2", "t": None, "props": {"x": 1}},
                                               {"id": "p3", "t": "2", "props": {"scale": 2}, "interpolation": ""}]}),
    clip("sh", "V2", "shape", start=6, out=2, shape={"type": "rect", "x": 0.5, "draw": 0.5},
         keyframes={"enabled": False, "items": [{"t": 0.2, "props": {"x": 0.1}}]}),
    clip("au", "A1", "audio", start=0, out=8, volume=1.5, audio_fx={"eq": True, "reverb": 0.3}),
    clip("adj", "V2", "adjustment", start=9, out=2, asset_kind="shape"),
    clip("rf", "V1", start=11, inp=2, out=8, reframe={"zoom": 0.9, "keyframes": [{"id": "r1", "t": 3.0, "cx": 0.2, "cy": 0.5},
                                                                               {"id": "r2", "t": 5, "cx": 0.8, "cy": 0.5}]}),
    clip("ts", "T1", "text", start=8, out=4, text="estilo", style={"color": "#ffffff"},
         keyframes={"enabled": True, "items": [{"id": "s1", "t": 0.5, "props": {"color": "#ff0000", "y": 0.3}},
                                               {"id": "s2", "t": 2, "props": {"color": "#00ff00"}}]}),
]
POSE_PATCHES = [{"opacity": 0.4}, {"volume": 1.8}, {"x": 0.1, "y": 0.9}, {"scale": "2"}, {"cx": 0.3}, {"zoom": 0.5},
                {"draw": 1.4}, {"eq": 0.5, "reverb": True}, {"rot_x": 10, "rotation": 45}, {"opacity": None}, {}]


def _pose_calls(rng: random.Random) -> list:
    calls: list = []
    for c in POSE_CLIPS:
        calls.append(["textStyleKfKeys", c["id"]])
        for t in (0, 0.5, 0.51, 1, 1.5, 2, 3, 3.02, -1, 9):
            fps = rng.choice([30, 24, 25])
            calls.append(["kfState", c["id"], t, fps])
            calls.append(["keyframeIdAt", c["id"], t, fps])
            calls.append(["toggleKeyframe", c["id"], t, fps])
        for side in ("in", "out"):
            calls.append(["applyVolumeFade", c["id"], side])
    for c in POSE_CLIPS:
        calls.append(["poseKeyframed", c["id"]])
        for patch in POSE_PATCHES:
            calls.append(["shouldKeyframe", c["id"], patch])
            calls.append(["applyStaticPose", c["id"], patch])
            for t in (0, 0.4, 0.51, 1.5, 1.49, 3.3333, -1, round(rng.uniform(0, 4), 4)):
                fps = rng.choice([30, 24, 60, 0])
                calls.append(["commitPose", c["id"], patch, t, fps])
                calls.append(["upsertKeyframeAt", c["id"], t, patch, rng.choice([None, "hold", "ease_out", "nada"]), fps])
    return calls


MATERIALS = [
    ("clips", {"index": 0, "filename": "c0.mp4", "start": 3.5, "end": 9.25, "label": "Entrevista", "created_at": "2026-01-01"}),
    ("clips", {"index": 2, "filename": "c2.mp4", "duration": 12, "title": "sin etiqueta", "text": "dice algo"}),
    ("clips", {"index": 5, "filename": "orig.mp4", "in_point": 10, "out_point": 14.5, "face_track": True,
               "reframe": {"zoom": 0.7, "keyframes": [{"t": 10.5, "cx": 0.4, "cy": 0.5}, {"id": "k9", "t": 12, "cx": 0.6, "cy": 0.5}]}}),
    ("clips", {"index": 6, "filename": "m.mp4", "start": 0, "end": 4, "reframe": {"master": True, "zoom": 0.5}}),
    ("clips", {"index": 7, "filename": "s.mp4", "in_point": "3", "out_point": "", "start": None, "end": 3}),
    ("clips", {"id": "lib_abc", "filename": "lib.mp4", "duration": 5, "scope": "library"}),
    ("clips", {"id": "col_xyz", "filename": "col.mp4", "start": 1, "end": 2}),
    ("images", {"id": "i1", "filename": "logo.png", "name": "Logo"}),
    ("images", {"id": "i2", "filename": "loop.gif", "animated": True, "duration": 2.4}),
    ("images", {"id": "i3", "filename": "x.gif", "animated": True, "duration": 0, "loop": False}),
    ("audios", {"id": "a1", "filename": "musica.mp3", "duration": 92.5, "label": "Música", "description": "tema"}),
    ("audios", {"id": "a2", "filename": "voz.wav", "duration": "7"}),
    ("audios", {"id": "a3", "filename": "corto.wav", "duration": 0.1}),
    ("sfx", {"id": "Whoosh/fast.mp3", "name": "fast", "duration": 1.2}),
    ("sfx", {"id": "x.ogg", "chromaColor": "#00ff00"}),
]


def _material_calls(rng: random.Random) -> list:
    calls: list = []
    for kind, item in MATERIALS:
        calls.append(["segmentRange", item])
        calls.append(["materialDuration", item])
        calls.append(["dragPayload", kind, item])
        for start in (0, -2, 3.14159):
            calls.append(["makeClip", kind, item, "V1", start, rng.choice([None, 0, 2, "3", 0.1, 4.5])])
    for kind, item in MATERIALS:
        if kind == "sfx" and "chromaColor" in item:
            continue
        payload = {"asset_kind": kind, "asset_id": str(item.get("index", item.get("id"))), "filename": item.get("filename", item.get("id")),
                   "name": item.get("label") or item.get("name") or item.get("filename"), "duration": item.get("duration", 3),
                   "scope": item.get("scope", "project"), "reframe": item.get("reframe"), "description": None,
                   "media_version": None, "animated": item.get("animated"), "loop": item.get("loop")}
        if "in_point" in item:
            payload["in_point"], payload["out_point"] = item["in_point"], item["out_point"]
        payload = {k: v for k, v in payload.items() if v is not None or k in ("reframe", "description", "media_version")}
        for fit in (None, 0, 1.5, 100):
            calls.append(["placeAsset", payload, "V2", rng.choice([0, 2.5, 7.0001]), fit])
    for shape_type, label in (("star", "Estrella"), ("arrow_curve", "Flecha curva"), ("speech", "Burbuja")):
        payload = {"asset_kind": "shape", "asset_id": shape_type, "filename": "", "name": label, "duration": 5,
                   "kind": "shape", "shape": {"type": shape_type, "fill": "#e53935", "sides": 5}, "type": shape_type,
                   "label": label}
        for fit, dur in ((None, 5), (2.5, 5), (None, 0), (0, None), (None, 0.1)):
            calls.append(["placeAsset", {**payload, "duration": dur}, "V1", rng.choice([0, 3.25]), fit])
    calls.append(["placeAsset", {"kind": "shape", "asset_kind": "clips", "type": "heart", "duration": 2}, "V1", 1, None])
    for theme in (None, "neon", "box", "nada"):
        payload = {"asset_kind": "text", "kind": "text", "asset_id": theme or "text", "filename": "",
                   "name": theme or "Texto predeterminado", "text": "hola mundo" if theme else "Texto predeterminado",
                   "duration": 3, "theme_id": theme}
        for track_style in (None, {}, {"color": "#ff0000", "size": 0.07, "x": 0.3}):
            for fit, dur, text in ((None, 3, None), (2.25, 3, ""), (None, 0, "  "), (-1, "4", "otro")):
                p = {**payload, "duration": dur, **({"text": text} if text is not None else {})}
                calls.append(["placeText", p, "T1", rng.choice([0, 1.5]), fit, track_style])
    return calls


def _crop(rng: random.Random) -> dict:
    wf, hf = rng.choice([1, 0.5, 0.05, round(rng.uniform(0.05, 1), 4)]), rng.choice([1, 0.3, round(rng.uniform(0.05, 1), 4)])
    return {"cx": round(rng.uniform(wf / 2, 1 - wf / 2), 4), "cy": round(rng.uniform(hf / 2, 1 - hf / 2), 4), "wf": wf, "hf": hf}


def _layout_calls(rng: random.Random) -> list:
    """Geometría de la vista previa (clipLayout.js): recuadro Main, Recortar y tiradores."""
    calls: list = []
    for c in POSE_CLIPS:
        calls.append(["frameOf", c["id"]])
        calls.append(["slotAspectOf", c["id"], rng.choice([9 / 16, 16 / 9, 1])])
        for t in (0, 0.5, 1.5, 3, round(rng.uniform(0, 4), 4)):
            if c["id"] == "tk":  # keyframe con t null: JS lo pone en 0 y la pose de Python (la del export) lo ignora
                continue
            for src, out in (((1920, 1080), (1080, 1920)), ((1080, 1920), (1080, 1920)), ((640, 480), (1920, 1080))):
                calls.append(["freeFrameAt", c["id"], t, *src, *out])
    for x, y in ((1.75, -0.75), (-0.75, 1.75), (1.25, 0.25), (0.5, 0.5)):  # empates de Math.round
        calls.append(["toCapcutPos", x, y, 1, 1])
    for _ in range(60):
        w, h = rng.choice([(1080, 1920), (1920, 1080), (1080, 1080)])
        x, y = round(rng.uniform(-2, 3), 4), round(rng.uniform(-2, 3), 4)
        calls.append(["toCapcutPos", x, y, w, h])
        calls.append(["fromCapcutPos", rng.randint(-50000, 50000), rng.randint(-5000, 5000), w, h])
        calls.append(["frameRectOf", rng.choice([800, 1234.5]), rng.choice([450, 900]), rng.choice([None, 0, 0.5, 1, 2.5, -1]),
                      rng.choice([None, 0, 9 / 16, 16 / 9, 1])])
    for _ in range(150):
        crop = _crop(rng)
        tr = {"x": round(rng.uniform(-0.5, 1.5), 3), "y": round(rng.uniform(-0.5, 1.5), 3), "scale": rng.choice([1, 0.5, 2.25]),
              "rotation": rng.choice([0, 30, -90, 181])}
        px = {"sx": rng.uniform(0, 500), "sy": rng.uniform(0, 500), "sw": rng.uniform(10, 1920), "sh": rng.uniform(10, 1080)}
        frame = {"x": rng.uniform(0, 100), "y": rng.uniform(0, 50), "w": rng.uniform(200, 600), "h": rng.uniform(300, 900)}
        calls.append(["destRectOnFrame", tr, px, 1080, 1920, frame])
        calls.append(["sourceCropPx", crop, rng.choice([1920, 640]), rng.choice([1080, 480])])
        calls.append(["clampCrop", rng.uniform(-0.2, 1.2), rng.uniform(-0.2, 1.2), rng.uniform(0, 1.3), rng.uniform(0, 1.3)])
        nx, ny = round(rng.uniform(-0.1, 1.1), 4), round(rng.uniform(-0.1, 1.1), 4)
        calls.append(["cropSizeFromCorner", nx, ny, crop["cx"], crop["cy"]])
        calls.append(["cropHandleAt", nx, ny, crop, rng.choice([{"width": 400, "height": 700}, {}, {"width": 0, "height": 50}]),
                      rng.choice([12, 40, 200])])
        h0 = rng.choice(crop_handle_norms_py(crop))
        calls.append(["cropHandleAt", h0[2], h0[3], crop, {"width": 400, "height": 700}, 12])
        calls.append(["insideCrop", nx, ny, crop])
        hx, hy = rng.choice([-1, 0, 1]), rng.choice([-1, 0, 1])
        calls.append(["resizeCropFree", crop, hx, hy, nx, ny])
        calls.append(["resizeCropLocked", crop, hx, hy, nx, ny, rng.choice([1, 0.5625, 16 / 9 / (9 / 16), 3.2])])
        calls.append(["fitCropRatio", crop, rng.choice([1, 0.5625, 1.7777777777777777, 0.2, 5])])
        calls.append(["cropCursor", hx, hy])
        dest = {"dx": rng.uniform(0, 300), "dy": rng.uniform(0, 300), "dw": rng.uniform(20, 400), "dh": rng.uniform(20, 400),
                "rotation": rng.choice([0, 45, -30, 90])}
        for _ in range(3):
            calls.append(["hitTransformHandle", rng.uniform(-50, 750), rng.uniform(-50, 750), dest])
        calls.append(["hitTransformHandle", dest["dx"] + dest["dw"] / 2, dest["dy"] + dest["dh"] / 2, dest])
        calls.append(["hitTransformHandle", dest["dx"] + 1, dest["dy"] + 1, dest])
        if dest["rotation"] == 0:  # justo dentro del margen del tirador de giro
            calls.append(["hitTransformHandle", dest["dx"] + dest["dw"] / 2, dest["dy"] + dest["dh"] + 26 + 11.5, dest])
    for rid in ("free", "original", "output", "16:9", "9:16", "1:1", "4:3", "3:4", "4:5", "2.35:1", "nada"):
        for src in (16 / 9, 9 / 16, 1, 0, -1):
            calls.append(["cropRatioNorm", rid, src, rng.choice([9 / 16, 16 / 9, 0])])
    return calls


TEXT_PATCHES = [{"color": "#ff0000"}, {"color": "nada"}, {"font": "Impact", "size": 0.06}, {"x": 0.2, "y": "0.3"},
                {"border_width": 4, "shadow_color": "#00ff00"}, {"opacity": 0.5, "bold": False}, {"curve": 0.4, "scale": 2},
                {"cx": 0.3}, {}]


TEXT_CLIPS = POSE_CLIPS + [
    clip("sk1", "T1", "text", start=21, out=3, text="uno", keyframes={"enabled": True, "items": [
        {"id": "u1", "t": 0.5, "props": {"color": "#ff0000"}}]}),
    clip("sk", "T1", "text", start=17, out=3, text="solo color", style={"color": "#ffffff", "border_width": 2},
         keyframes={"enabled": True, "items": [{"id": "e1", "t": 1.6, "props": {"color": "#ff0000"}},
                                               {"id": "e2", "t": 2, "props": {"color": "#00ff00", "border_width": 4}}]}),
    clip("shk", "V2", "shape", start=1, out=3, shape={"type": "star"},
         keyframes={"enabled": True, "items": [{"id": "q1", "t": 0.5, "props": {"x": 0.3}}]}),
    clip("crv", "V1", start=20, out=4, keyframes={"enabled": True, "items": [
        {"id": "c0", "t": 0, "props": {"x": 0.1}, "interpolation": "cubic-out"},
        {"id": "c1", "t": 1.5, "props": {"x": 0.5}, "interpolation": "bezier", "bezier": [0.1, 0.9, 0.2, 1]},
        {"id": "c2", "t": 2, "props": {"x": 0.7}, "interpolation": "bezier"},
        {"id": "c3", "t": 3, "props": {"x": 0.9}, "interpolation": "hold"}]}),
    clip("ti", "T1", "text", start=13, out=4, text="curvas", style={"color": "#ffffff"},
         keyframes={"enabled": True, "items": [{"id": "h0", "t": 0, "props": {"color": "#111111"}, "interpolation": "hold"},
                                               {"id": "h2", "t": 1.6, "props": {"border_width": 2}, "interpolation": "ease_out"}]})]


def _text_calls(rng: random.Random) -> list:
    """Textos del editor: crear (makeTextClip, newTextStyle) y cambiar su estilo (changeStyle)."""
    calls: list = [["defaultTextStyle"], ["subtitleStyle"], ["subtitleThemes"], ["textLooks"]]
    theme_ids = ["classic", "neon", "fire", "box", "shorts", "yellow", "gold-glow", "nada"]
    currents = [None, {}, {"x": 0.3, "y": None, "w": 0.5, "size": 0.07, "opacity": 0.4, "max_words": 4},
                {"theme": "neon", "word_fx": "glow"}, {"theme": "neon", "word_fx": "none"}, {"preset": "fire", "word_fx": ["pop"]},
                {"theme": "", "preset": "box", "word_fx": "highlight"}, {"preset": "outline"}]
    for cur in currents:
        calls.append(["clearTextTheme", cur])
        calls.append(["selectedSubtitleThemeId", cur])
        for tid in theme_ids[:-1]:
            calls.append(["applyThemeToStyle", cur or {}, tid])
        for tid in theme_ids + [None]:
            calls.append(["applyOrClearTheme", cur, tid])
    looks = ["white-black", "black-white", "white-shadow", "black-grey", "black-green-glow", "white-yellow-glow", "nada", None]
    for lk in looks:
        calls.append(["lookPatch", lk])
    sts = [{}, {"color": "#FFFFFF"}, {"color": "#ffe600", "border_width": "5"}, {"bg": "#9A9A9A", "bg_opacity": 0.3},
           {"color": "#000000", "glow": True, "shadow_color": "#33ff33"}, {"glow": True, "glow_color": "#FFD400"},
           {"shadow": True, "shadow_color": "#123456"}, {"border_width": 5, "border_color": "#000000", "bg": "none"},
           {"color": "#000000", "bg": "#ffffff", "bg_opacity": 1}, {"border_width": 4.0, "shadow": True}]
    for st in sts:
        calls.append(["activeLookId", st])
    for ts in (None, {}, {"font": "Anton", "y": 0.2, "color": "#123456"}, {"x": None}):
        calls.append(["newTextStyle", ts])
    for text in ("hola", "", "Una frase bastante larga para el nombre", "ñandú 😀😀 emojis largos fin"):
        for start, dur in ((0, 3), (-1, 0.2), (2.0004, 7.12345)):
            calls.append(["makeTextClip", "T1", start, dur, text, rng.choice([None, {"size": 0.1}])])
    types = ["square", "circle", "rect", "line", "arrow", "arrow_curve", "arrow_double", "triangle", "star", "polygon",
             "speech", "check", "x", "marker", "heart", "path", "letterbox", "ellipse", None, "nada"]
    for t in types:
        calls.append(["defaultShape", t])
    for preset in [{"type": "star", "label": "Estrella"}, {"shape": {"type": "circle", "fill": "#00ff00"}}, {"shape_type": "x"},
                   {"name": "sin tipo"}, {"type": "path", "label": "Trazado", "shape": {"points": [[0, 0], [1, 1]]}}, None]:
        for start, dur in ((0, None), (-2, 0.1), (3.0006, 7)):
            calls.append(["makeShapeClip", "V2", start, dur, preset])
    for start, dur in ((0, None), (-2, 0.1), (3.0006, 7), (1, 0)):
        calls.append(["makeAdjustmentClip", "V2", start, dur])
    boards = [{"type": "keyframe", "props": {"x": 0.2, "opacity": 0.5, "volume": 1.5, "mx": 0.3, "color": "#ff0000", "zoom": 0.7},
               "interpolation": "ease_out", "bezier": [0.2, 0.1, 0.3, 1.2], "srcKind": "video"},
              {"type": "keyframe", "props": {"y": "0.8", "border_width": 3, "eq": 0.4}, "interpolation": "hold", "bezier": None},
              {"type": "otro", "props": {"x": 0.1}}]
    for c in [c for c in TEXT_CLIPS if c["id"] != "tk"]:  # «tk»: keyframe con t null (divergencia de la pose)
        for t in (0, 0.5, 1, 1.5, 2, 3, 0.51):
            fps = rng.choice([30, 24])
            calls.append(["targetInterpItem", c["id"], t, fps])
            calls.append(["copyKeyframeAt", c["id"], t, fps])
            for board in boards:
                calls.append(["pasteKeyframeAt", c["id"], t, board, rng.choice([None, [], ["transform"], ["audio", "text_style"], ["crop", "mask"]]), fps])
        items = (c.get("keyframes") or {}).get("items") or []
        for k in items:
            if k.get("id"):
                for target in (0, 0.7, 2.25, 9):
                    calls.append(["duplicateKeyframeAt", c["id"], k["id"], target, 30])
                for patch in ({"t": 1.234}, {"interpolation": "ease_in"}, {"interpolation": ""}, {"bezier": [0.1, 0.2, 0.3, 0.4]},
                              {"props": {"volume": 1.7, "color": "#fff", "x": "0.3"}}, {"t": -1, "props": {"opacity": None}}):
                    calls.append(["patchKeyframe", c["id"], k["id"], patch, rng.choice([30, 24])])
                calls.append(["moveKeyframe", c["id"], k["id"], 2.5, 30])
        for k in ((c.get("reframe") or {}).get("keyframes") or []):
            if k.get("id"):
                calls.append(["moveKeyframe", c["id"], k["id"], 4.1, 30])
    sections = [["color"], ["border_color", "border_width"], ["bg", "bg_opacity", "bg_radius"], ["shadow_color", "shadow_opacity", "shadow_blur"],
                ["glow_color", "glow_intensity"], ["curve"], ["stretch_x", "stretch_y"]]
    for c in [c for c in TEXT_CLIPS if c["id"] != "tk"]:
        for keys in sections:
            for t in (0, 0.5, 1.6, 2, 9):
                fps = rng.choice([30, 24])
                calls.append(["styleKfState", c["id"], t, keys, fps])
                calls.append(["toggleStyleKf", c["id"], rng.choice([None, {"color": "#123456", "bg": "#000000", "shadow_color": "#00ff00"}]), keys, t, fps])
    shape_patches = [{"fill": "#00ff00"}, {"x": 0.2, "rotation": 30}, {"w": 0.5, "h": "0.2"}, {"opacity": 0.3},
                     {"sides": 7, "dash": "dot"}, {"cx": 0.1}, {}]
    for c in TEXT_CLIPS:
        for patch in shape_patches:
            for t in (0, 0.5, 1.25, -1):
                calls.append(["changeShape", c["id"], patch, t, rng.choice([30, 24])])
    for ph in (0, 2.5, 30, 11.9996):
        calls.append(["newTextDuration", ph])
    for c in TEXT_CLIPS:
        if c["kind"] != "text":
            continue
        for patch in TEXT_PATCHES:
            for t in (0, 0.5, 0.51, 1.7, -1):
                fps = rng.choice([30, 24, 60])
                calls.append(["upsertStyleKf", c["id"], t, patch, fps])
                calls.append(["changeStyle", c["id"], patch, t, fps, rng.random() < 0.3])
    return calls


def _linked_case(rng: random.Random) -> dict:
    """Subtítulos enlazados a un audio (applyAudioSpeedToLinkedText)."""
    tracks = [track("V1", "video"), track("T1", "text"), track("T2", "text"),
              track("A1", "audio", linked_track_id="T1"), track("A2", "audio"), track("A3", "audio", linked_track_id="")]
    words = [{"text": "uno", "start": 0.0, "end": 0.4}, {"text": "dos", "start": 0.4555, "end": 1.0005}]
    clips = [clip("a", "A1", "audio", start=2, out=8, speed=1.0), clip("a2", "A1", "audio", start=12, out=4, speed=2.0),
             clip("b", "A2", "audio", start=0, out=5), clip("c", "A3", "audio", start=0, out=5),
             clip("v", "V1", start=0, out=4)]
    for n, (start, dur) in enumerate([(2, 1.5), (3.6, 2), (7.9995, 1), (9.9995, 1), (1, 1.0005), (0, 1), (13, 1),
                                      (14.5, 3), (5.25, 0.33)]):
        clips.append(clip(f"t{n}", rng.choice(["T1", "T1", "T2"]), "text", start=start, out=dur, text=f"t{n}", words=words))
    calls = []
    for aid in ("a", "a2", "b", "c", "v"):
        for sp in (1, 2, 0.5, 0.1, 10, 50, 0.01, 0, -1, "2", None, 1.0000001, 3.3):
            calls.append(["applyAudioSpeedToLinkedText", aid, sp])
    return {"name": "subtítulos enlazados", "timeline": {"tracks": tracks, "clips": clips}, "calls": calls}


def _vfx_case(rng: random.Random) -> dict:
    """Pila de efectos animados (normalizeVfxEntry y makeVfxEntry de fx/core/runtime.js)."""
    import json
    from pathlib import Path
    catalog = json.loads((Path(__file__).resolve().parents[2] / "resources" / "fx" / "catalog.json").read_text("utf-8"))
    calls: list = []
    for e in catalog["effects"]:
        calls.append(["makeVfxEntry", e["id"]])
        raw = {"effect": e["id"], "from": rng.choice([None, -1, 0.5, "2"]), "to": rng.choice([None, 0.2, 3, "x"]),
               "v": rng.choice([None, 0, 2.6, "3"]), "enabled": rng.choice([None, False, True, 0]), "params": {}}
        for p in e.get("params", []):
            raw["params"][p["id"]] = rng.choice([None, "basura", -1e9, 1e9, p.get("def"), True, "#00ff00",
                                                 (p.get("options") or [{"id": None}])[-1].get("id")])
        calls.append(["normalizeVfxEntry", raw])
    calls += [["makeVfxEntry", "nada"], ["normalizeVfxEntry", {"effect": "nada"}], ["normalizeVfxEntry", None],
              ["keepsUid", {"effect": catalog["effects"][0]["id"], "uid": "fx1"}]]
    return {"name": "efectos animados", "timeline": {"tracks": TRACKS, "clips": []}, "calls": calls}


def _mask_case(rng: random.Random) -> dict:
    """Máscaras desde el editor (addMask, removeMask, duplicateMask, changeMask y commitMask)."""
    m0 = {"id": "m1", "type": "rectangle", "x": 0.3, "y": 0.6, "w": 0.4, "h": 0.2, "feather": 0.05, "target": "clip"}
    clips = [clip("v0", "V1", start=0, out=6),
             clip("v1", "V1", start=7, out=6, masks=[m0, {"id": "m2", "type": "text", "text": {"content": "hola"}}]),
             clip("v2", "V2", start=0, out=5, masks=[dict(m0, id="m3")], keyframes={"enabled": True, "items": [
                 {"id": "a", "t": 0, "props": {"x": 0.5, "mx": 0.3, "mw": 0.4}}, {"id": "b", "t": 2, "props": {"mx": 0.7, "msx": 2}}]}),
             clip("t0", "T1", "text", start=0, out=3, text="hola", masks=[{"id": "m4", "type": "brush", "brush": {"points": [{"x": 0.1, "y": 0.2, "m": 1}, {"x": "a"}]}}])]
    calls: list = []
    for c in clips:
        for aspect in (9 / 16, 16 / 9, 0, -1):
            for typ in ("circle", "linear", "film", "rectangle", "star", "heart", "text", "brush", "nada"):
                calls.append(["maskOp", c["id"], "addMask", typ, aspect, 0, 30])
            calls.append(["maskOp", c["id"], "addAdjust", "circle", aspect, 0, 30])
        for op in ("removeMask", "duplicateMask"):
            calls.append(["maskOp", c["id"], op, None, 9 / 16, 0, 30])
        for patch in ({"type": "star"}, {"type": "rectangle"}, {"type": ""}, {"invert": True, "opacity": 3},
                      {"x": "0.2", "radius": 0.9, "target": "adjust"}, {"text": {"content": "nuevo", "size": 9}}, {"enabled": False}):
            calls.append(["maskOp", c["id"], "changeMask", patch, rng.choice([9 / 16, 1]), 0, 30])
        for patch in ({"mx": 0.2, "my": "0.8"}, {"mw": 9, "mfeather": 1}, {"mrot": 45, "msx": 0.5}, {"x": 0.1}, {"mx": None}):
            for t in (0, 1.25, 3):
                calls.append(["maskOp", c["id"], "commitMask", patch, 9 / 16, t, rng.choice([30, 24])])
    return {"name": "máscaras", "timeline": {"tracks": TRACKS, "clips": clips}, "calls": calls}


def _fragment_case(rng: random.Random) -> dict:
    """Fragmentar subtítulos en cuadros (splitClipByMaxWords y splitTrackTextByMaxWords)."""
    words = [{"text": w, "start": round(i * 0.4, 3), "end": round(i * 0.4 + 0.35, 3), **({"prob": 0.9} if i % 2 else {})}
             for i, w in enumerate("uno dos tres cuatro cinco seis siete".split())]
    clips = [clip("w", "T1", "text", start=2, out=3, text="uno dos tres cuatro cinco seis siete", words=words,
                  origin={"transcript_id": "tr", "word_range": [10, 17]}, style={"max_words": 3}),
             clip("p", "T1", "text", start=6, out=2.5, text="  palabras   sin tiempos ni origen  ", style={"color": "#fff"}),
             clip("f", "T1", "text", start=9, out=1, text="texto libre de varias palabras", text_role="free"),
             clip("s", "T1", "text", start=11, out=1.3333, text="ñandú 😀 emoji largos textos para el nombre del clip"),
             clip("v", "V1", start=0, out=4)]
    calls: list = []
    for raw in (None, [], {"sfx": [1, "a", None, True], "audios": "x", "textStyles": [None, {"style": {}}, {"style": {"a": 1}, "id": "t"}, 3]}):
        calls.append(["normalizeFavorites", raw])
    for st in ({}, {"theme": "neon", "size": 0.048}, {"preset": "fire", "size": "0.1"}, {"theme": "", "preset": "", "size": None},
               {"size": 0.0001}, {"theme": 7, "size": 0.05}, {"size": 0.0003906}):
        for existing in ([], [{"name": "neon · 61px"}, {"name": "neon · 61px (2)"}], [{"name": "Estilo · 64px"}], None):
            calls.append(["nameTextStyleFavorite", st, existing])
    for cid in ("w", "p", "f", "s"):
        for n in (1, 2, 3, 4, 10, 0, -1, "2", None, 2.7):
            calls.append(["splitClipByMaxWords", cid, n])
    for n in (1, 2, 5, None):
        calls.append(["splitTrackText", "T1", n])
    return {"name": "fragmentar", "timeline": {"tracks": TRACKS, "clips": clips}, "calls": calls}


def _sfx_name_case() -> dict:
    """Los borradores del modal de SFX (lib/sfxName.js)."""
    cats = [{"id": "01_IMPACTOS", "label": "Impactos", "count": 3}, {"id": "favoritos", "label": "Favoritos"},
            {"id": "02_AIRE"}, {"id": "", "label": "sin id"}, None, {"id": "03_ÑU", "label": "Ñandú Σ", "count": 0},
            {"id": 7, "label": ""}]
    calls: list = []
    for name in ("golpe.mp3", "a/b/c.wav", "C:\\x\\y.ogg", ".hidden", "sin_ext", "  dos.puntos.mp3  ", "",
                 None, 5, "x/\nb/c.mp3", "dir/", "ñandú ü.flac", "a.", "/"):
        calls.append(["sfxNameFromFile", name])
    calls += [["realSfxCategories", cats], ["realSfxCategories", None], ["realSfxCategories", []]]
    for cat in ({"id": "02_AIRE", "label": "Aire"}, {"id": "nueva", "count": None}, {"id": "favoritos"}, {"label": "x"},
                None, {"id": "01_IMPACTOS", "label": "", "count": 9}, {"id": 7, "label": "siete"}):
        calls.append(["upsertSfxCategory", cats, cat])
    for text in ("impactos", "  IMPACTOS ", "01_impactos", "02_aire", "ñandú σ", "Ñandú Σ", "nueva cosa", "", "   ",
                 None, 7, "7", "favoritos"):
        calls.append(["matchSfxCategory", text, [c for c in cats if c is not None]])
    for sfx in ({"id": "01_IMPACTOS/golpe.mp3", "name": "Golpe", "folder": "01_IMPACTOS", "category": "Impactos",
                 "uso": "caídas", "url": "/api/sfx/file/x"}, {}, None, {"id": "", "name": None, "uso": 0}):
        calls.append(["sfxDraftFromSaved", sfx])
    for row in ({"name": "  Golpe ", "categoryId": " 01_IMPACTOS ", "uso": " x "}, {"name": None, "categoryId": "",
                 "categoryLabel": "impactos"}, {"categoryLabel": "Otra nueva"}, {}, None,
                {"name": 5, "categoryId": 0, "categoryLabel": "  ", "uso": None}):
        calls.append(["sfxSavePayload", row, [c for c in cats if c is not None]])
    return {"name": "sfx", "timeline": {"tracks": TRACKS, "clips": []}, "calls": calls}


def _waveform_case(rng: random.Random) -> dict:
    """Las barras del clip de audio en la timeline (pseudoWaveform de lib/panning.js): no es el audio,
    sino barras pseudoaleatorias con la semilla del asset_id."""
    seeds = ["a", "", None, 0, 7, 123456789, "sfx_0001", "clip-3f9c", "voz.mp3", "ñandú", "日本語", "🎵 emoji",
             "a" * 300, " ", "0", -5, 1.5, 2 ** 40, "x\ny"]
    calls: list = []
    for s in seeds:
        calls.append(["pseudoWaveform", s, None])
    for s in seeds[:6]:
        for bars in (0, 1, 16, 47, 200):
            calls.append(["pseudoWaveform", s, bars])
    for _ in range(40):
        calls.append(["pseudoWaveform", "".join(rng.choice("abcdefghijklmnopqrstuvwxyz0123456789_-") for _ in range(12)),
                      rng.randint(16, 400)])
    return {"name": "forma de onda", "timeline": {"tracks": TRACKS, "clips": []}, "calls": calls}


def _crop_case(rng: random.Random) -> dict:
    """Confirmar el modal Recortar (confirmCrop)."""
    clips = [clip("st", "V1", start=0, inp=1, out=6, layout="overlay"),
             clip("rk", "V1", start=7, inp=0, out=5, layout="overlay", transform={"x": 0.3, "rotation": 10},
                  reframe={"zoom": 0.8, "keyframes": [{"id": "r1", "t": 0.5, "cx": 0.4, "cy": 0.5, "pan_mode": "direct"}]}),
             clip("an", "V2", start=0, out=5, layout="overlay", reframe={"zoom": 0.9, "keyframes": [
                 {"id": "a1", "t": 0, "cx": 0.2, "cy": 0.5}, {"id": "a2", "t": 3, "cx": 0.8, "cy": 0.5}]}),
             clip("dc", "V2", start=6, inp=2, out=8, layout="overlay", reframe={"dual_crop": True, "zoom": 0.7, "keyframes": [
                 {"id": "d1", "t": 2, "cx": 0.3, "cy": 0.5}, {"id": "d2", "t": 5, "cx": 0.6, "cy": 0.5}]}),
             clip("kf", "V1", start=13, out=4, layout="overlay", keyframes={"enabled": True, "items": [
                 {"id": "p1", "t": 0, "props": {"x": 0.2}}, {"id": "p2", "t": 2, "props": {"x": 0.8}}]})]
    calls: list = []
    for c in clips:
        for _ in range(6):
            crop = {"cx": round(rng.uniform(0.2, 0.8), 6), "cy": round(rng.uniform(0.2, 0.8), 6),
                    "wf": round(rng.uniform(0.1, 1), 6), "hf": round(rng.uniform(0.1, 1), 6)}
            calls.append(["confirmCrop", c["id"], crop, rng.choice([0, 10, 10.004, -45.678, 180]), round(rng.uniform(0, 3), 3),
                          rng.choice([30, 24])])
    return {"name": "recortar", "timeline": {"tracks": TRACKS, "clips": clips}, "calls": calls}


def _free_layout_case(rng: random.Random) -> dict:
    """Pasar un clip a objeto libre al conocer su medio (applyFreeLayout). Los clips llevan `layout` y
    `frame` como los deja el modelo (en el JS, un `frame` que falta cuenta como «free»)."""
    face = [{"id": "r1", "t": 1.5, "cx": 0.3, "cy": 0.5, "pan_mode": "smooth"},
            {"id": "r2", "t": 4.0, "cx": 0.7, "cy": 0.45, "pan_mode": "smooth"}]
    clips = [clip("f1", "V1", start=0, inp=0, out=5, layout="fill", frame="full"),          # sin encuadre: altura completa
             clip("f2", "V1", start=6, inp=1, out=6, layout="fill", frame="full", reframe={"zoom": 0.8, "keyframes": face}),
             clip("f3", "V1", start=12, out=4, layout="fill", frame="full",
                  reframe={"zoom": 1, "crop_w": 0.5, "crop_h": 0.6, "keyframes": []}),
             clip("zq", "V1", start=17, out=3, layout="fill", frame="full", reframe={"zoom": 0.9995, "keyframes": []}),
             clip("zm", "V1", start=21, out=3, layout="fill", frame="full", reframe={"zoom": 0.995, "keyframes": []}),
             clip("fk", "V1", start=28, out=5, layout="fill", frame="full", reframe={"zoom": 1, "keyframes": face}),  # paneo sin zoom
             clip("ov", "V2", start=0, out=5, layout="overlay", frame="free", transform={"x": 0.3, "y": 0.4, "scale": 0.5}),
             clip("oc", "V2", start=6, out=5, layout="overlay", frame="full",
                  reframe={"zoom": 1, "crop_w": 0.4, "crop_h": 0.7, "keyframes": []}, transform={"x": 0.6, "scale": 0.8}),
             clip("on", "V2", start=12, out=4, layout="overlay", frame="top", transform={"x": 0.2, "scale": 2}),
             clip("dc", "V2", start=17, out=4, layout="fill", frame="full", reframe={"dual_crop": True, "zoom": 0.7}),
             clip("im", "V1", kind="image", start=25, out=3, layout="fill", frame="full"),
             clip("rv", "V2", start=22, inp=2, out=9, layout="fill", frame="full", speed=2, reverse=True,
                  reframe={"zoom": 0.6, "keyframes": face}),
             clip("tx", "T1", kind="text", start=0, out=3, text="Hola")]
    sizes = [(1920, 1080), (1080, 1920), (3840, 2160), (640, 480), (1000, 1000), (0, 0)]
    outs = [(720, 1280), (1920, 1080)]
    calls: list = []
    for c in clips:
        for sw, sh in sizes:
            for ow, oh in outs:
                for head in (c["start"], c["start"] + round(rng.uniform(0, 3), 3), c["start"] - 1):
                    calls.append(["freeLayout", c["id"], sw, sh, ow, oh, head])
    return {"name": "objeto libre", "timeline": {"tracks": TRACKS, "clips": clips}, "calls": calls}


def _mask_ui_case(rng: random.Random) -> dict:
    """La máscara editada sobre la vista previa (lib/clipMask.js y features/editor/interactions.js): su
    geometría en el lienzo, los tiradores, qué arrastra un punto (sobre todo cerca de los tiradores), el
    parche de cada arrastre, el trazo del pincel y los contornos de la estrella y el corazón."""
    frames = [{"x": 157.5, "y": 0, "w": 405, "h": 720}, {"x": 0, "y": 60, "w": 1920, "h": 1080},
              {"x": 33.25, "y": 12.5, "w": 300, "h": 300}]
    masks: list = []
    for kind in ("circle", "rectangle", "linear", "film", "star", "heart", "text", "brush"):
        for _ in range(2):
            m = {"id": "m1", "type": kind, "x": round(rng.uniform(0.1, 0.9), 4), "y": round(rng.uniform(0.1, 0.9), 4),
                 "w": round(rng.uniform(0.1, 1.2), 4), "h": round(rng.uniform(0.1, 1.2), 4),
                 "scale_x": round(rng.uniform(0.5, 2), 3), "scale_y": round(rng.uniform(0.5, 2), 3),
                 "rotation": round(rng.uniform(-200, 200), 2), "feather": round(rng.uniform(0, 0.2), 4),
                 "radius": round(rng.uniform(0, 0.5), 3)}
            if kind == "brush":
                m["brush"] = {"size": 0.1, "points": [{"x": 0.1, "y": -0.2, "m": 1}, {"x": 0.12, "y": -0.18}]}
            masks.append(m)
    masks.append({"id": "m0", "type": "circle", "x": 0.5, "y": 0.5, "w": 0.5, "h": 0.5})  # sin giro, escala ni pluma
    masks.append({"id": "mt", "type": "rectangle", "x": 0.3, "y": 0.6, "w": 0.01, "h": 0.01, "scale_x": 0.02,
                  "scale_y": 0.02})  # semiejes en su mínimo de 0,5 px

    def handles(m: dict, f: dict) -> list:  # aproximados: solo para tirar puntos cerca
        ref = f["h"]
        cx, cy = f["x"] + m["x"] * f["w"], f["y"] + m["y"] * f["h"]
        hw = max(0.5, m["w"] * m.get("scale_x", 1) * ref / 2)
        hh = max(0.5, m["h"] * m.get("scale_y", 1) * ref / 2)
        if m["type"] == "linear":
            hw, hh = f["w"] * 0.3, max(18, f["h"] * 0.035)
        elif m["type"] == "film":
            hw = f["w"] * 0.3
        rot = m.get("rotation", 0) * math.pi / 180
        c, s = math.cos(rot), math.sin(rot)
        pad = max(16, f["h"] * 0.022)
        return [(cx + lx * c - ly * s, cy + lx * s + ly * c)
                for lx, ly in ((0, 0), (hw, 0), (0, hh), (hw, hh), (0, -hh - pad), (-hw - pad, 0))]

    calls: list = []
    for i, m in enumerate(masks):
        for f in (frames[i % 3], frames[(i + 1) % 3]):
            calls += [["maskGeometry", m, f], ["maskHandleBox", m, f], ["maskHandles", m, f]]
            hs = handles(m, f)
            near = [(round(x + rng.uniform(-18, 18), 2), round(y + rng.uniform(-18, 18), 2)) for x, y in hs]
            far = [(round(rng.uniform(f["x"] - 50, f["x"] + f["w"] + 50), 2),
                    round(rng.uniform(f["y"] - 50, f["y"] + f["h"] + 50), 2)) for _ in range(3)]
            for px, py in near + far:
                calls.append(["maskHitMode", px, py, m, f, None])
                calls.append(["maskCursor", px, py, m, f, False])
            calls.append(["maskCursor", *near[0], m, f, True])  # con el pincel: solo en las de pincel
            calls.append(["maskHitMode", *near[1], m, f, 6])
            calls += [["toMaskLocal", px, py, m, f] for px, py in far[:2]]
            for mode in ("move", "rotate", "feather", "width", "height", "corner", "nada"):
                x0, y0 = hs[rng.randrange(len(hs))]
                p0 = {"x": round(x0, 2), "y": round(y0, 2)}
                p = {"x": round(x0 + rng.uniform(-220, 220), 2), "y": round(y0 + rng.uniform(-220, 220), 2)}
                calls.append(["maskDragPatch", mode, m, f, p0, p])
                if mode == "move":  # hasta salirse: la posición se acota a [-1, 2]
                    calls.append(["maskDragPatch", mode, m, f, p0, {"x": p0["x"] + 6000, "y": p0["y"] - 6000}])
            if m["type"] in ("brush", "circle"):  # una máscara sin `brush` también: el JS empieza de cero
                x, y = hs[0]
                stroke = []
                for _ in range(14):
                    step = 0.4 if rng.random() < 0.4 else rng.uniform(3, 25)  # los pasos cortos no dejan punto
                    ang = rng.uniform(0, 2 * math.pi)
                    x, y = x + step * math.cos(ang), y + step * math.sin(ang)
                    stroke.append({"x": round(x, 3), "y": round(y, 3)})
                calls.append(["maskBrushStroke", m, f, stroke])
    calls += [["resizeCursor", d] for d in (-400, -202.5, -180, -67.5, -22.5, 0, 22.5, 22.49, 44, 67.5, 90, 112.5,
                                             135, 157.5, 179.9, 180, 202.5, 359, 3601.5)]
    calls += [["starPoints", 40, 30, None, None], ["starPoints", 12.5, 80, 7, 0.3], ["starPoints", 5, 5, 2, 0.5],
              ["heartPoints", 50, 40, None], ["heartPoints", 10, 20, 12]]
    return {"name": "máscara en el preview", "timeline": {"tracks": TRACKS, "clips": []}, "calls": calls}


def _captions_case(rng: random.Random) -> dict:
    """Subtítulos del editor desde una transcripción (canCaptionClip, sourceRefOf y textClipsFromTranscript de
    editorModel.js): qué clips se pueden transcribir, la referencia al material y los clips de texto del tramo
    del clip fuente, con sus palabras reales y partidos en cuadros de max_words."""
    clips = [clip("au", "A1", kind="audio", start=2, inp=1, out=9, filename="voz.mp3", asset_id="a1"),
             clip("vi", "V1", start=0, inp=0.5, out=6, name="Mi vídeo"),
             clip("lb", "V1", start=7, out=3, asset_scope="library"),
             clip("co", "V1", start=11, out=3, asset_scope="collection"),
             clip("sf", "A1", kind="audio", asset_kind="sfx", start=12, out=2, filename="boom.wav"),
             clip("tx", "T1", kind="text", start=0, out=3, text="Hola"),
             clip("im", "V2", kind="image", start=0, out=3),
             clip("vn", "V2", start=20, out=2, asset_id="")]

    def words(text: str, t0: float, t1: float) -> list:
        ws = text.split()
        step = (t1 - t0) / max(1, len(ws))
        out = []
        for i, w in enumerate(ws):
            item = {"text": f" {w} ", "start": round(t0 + i * step, 3)}
            if rng.random() < 0.8:
                item["end"] = round(t0 + (i + 1) * step - 0.01, 3)
            if rng.random() < 0.5:
                item["prob"] = round(rng.uniform(0.3, 1), 3)
            out.append(item)
        return out

    long_text = "una frase larga con muchas palabras para partir en varios cuadros seguidos"
    segments = [{"index": 0, "start": 0.0, "end": 0.8, "text": "antes"},                       # antes del tramo
                {"index": 1, "start": 0.5, "end": 2.0, "text": "empieza antes del tramo",
                 "words": words("empieza antes del tramo", 0.5, 2.0)},
                {"index": 2, "start": 3.0, "end": 5.2, "text": long_text, "words": words(long_text, 3.0, 5.2)},
                {"start": 6.0, "end": 6.1, "text": "corto"},                                   # sin índice: el suyo
                {"index": None, "start": 7.0, "end": 8.0, "text": "   "},                         # vacío
                {"index": 5, "start": 8.5, "end": 9.5, "text": "se sale del final",
                 "words": words("se sale del final", 8.5, 9.5)},
                {"index": 6, "start": 9.0, "end": 10.0, "text": "después"},                    # tras el tramo
                {"index": None, "start": 4.0, "end": 4.5, "text": "índice nulo y cuatro"}]      # null: el suyo
    calls: list = [["canCaptionClip", c["id"]] for c in clips] + [["sourceRefOf", c["id"]] for c in clips]
    styles = [{"max_words": 3}, {}, None, {"max_words": None, "size": 0.05}]
    refs = [None, {"kind": "audio", "asset_id": "a1", "filename": "voz.mp3"}]
    for cid in ("au", "vi"):
        for st in styles:
            for tr in (None, {"id": "tr1"}):
                for ref in refs:
                    calls.append(["textClipsFromTranscript", cid, segments, "T1", st, tr, ref])
    return {"name": "subtítulos del editor", "timeline": {"tracks": TRACKS, "clips": clips}, "calls": calls}


def _face_follow_case(rng: random.Random) -> dict:
    """«Seguir cara» de una máscara: el seguimiento de caras del material pasado a keyframes de su posición
    (followTrackMaskKeys de lib/clipLayout.js), enableKeyframes y el setClips de followMaskFace."""
    face = [{"id": "r1", "t": 1.0, "cx": 0.3, "cy": 0.5, "pan_mode": "smooth"},
            {"id": "r2", "t": 4.0, "cx": 0.7, "cy": 0.45, "pan_mode": "smooth"}]
    on = {"enabled": True, "items": [{"id": "k1", "t": 0.5, "interpolation": "linear",
                                      "props": {"x": 0.5, "y": 0.5, "scale": 1, "rotation": 0, "opacity": 1}}]}
    clips = [clip("f1", "V1", start=2, inp=1, out=6),
             clip("f2", "V1", start=8, inp=0, out=8, speed=2),
             clip("f3", "V1", start=13, inp=2, out=7, reverse=True),
             clip("f4", "V2", start=0, inp=0, out=5, reframe={"zoom": 0.7, "pan_mode": "smooth", "keyframes": face}),
             clip("f5", "V2", start=6, inp=0, out=5, layout="overlay", frame="free",
                  transform={"x": 0.4, "y": 0.6, "scale": 0.8, "rotation": 15},
                  reframe={"zoom": 1, "crop_w": 0.6, "crop_h": 0.8, "keyframes": []}),
             clip("f6", "V1", start=20, inp=0, out=4, keyframes=on),
             clip("f7", "V1", start=25, inp=0, out=3,
                  masks=[{"id": "m1", "type": "circle", "x": 0.5, "y": 0.5, "w": 0.4, "h": 0.4}])]
    track = []
    t = 0.0
    while t < 9:
        p = {"t": round(t, 3), "cx": round(rng.uniform(0.2, 0.8), 4)}
        if rng.random() < 0.9:
            p["cy"] = round(rng.uniform(0.3, 0.7), 4)
        track.append(p)
        t += rng.uniform(0.08, 0.3)
    rng.shuffle(track)  # en desorden: se ordena por t
    track += [{"t": None, "cx": 0.5, "cy": 0.5}, {"cx": 0.5, "cy": 0.5}]  # sin un t válido: fuera
    dims = [{"srcW": 1920, "srcH": 1080, "outW": 720, "outH": 1280}, {"srcW": 1080, "srcH": 1920, "outW": 1920, "outH": 1080}]
    calls: list = []
    for c in clips:
        for d in dims:
            for gap in (None, 0.5):
                calls.append(["followTrackMaskKeys", c["id"], track, d, gap])
            calls.append(["followMaskFace", c["id"], track, d, c["in_point"] + 0.3, 30])
        calls.append(["followMaskFace", c["id"], track, dims[0], c["in_point"], 25])
        # el primer keyframe con el encuadre del cabezal (srcTOf), entre dos del encuadre animado
        calls.append(["followMaskFace", c["id"], track, dims[0], c["in_point"] + 2.5, 30])
        for lt in (0.0, 0.5):
            calls.append(["enableKeyframes", c["id"], lt, c["in_point"] + lt])
    return {"name": "seguir cara", "timeline": {"tracks": TRACKS, "clips": clips}, "calls": calls}


def _script_prompt_case() -> dict:
    """«Prompt para el guion» (buildScriptPrompt de features/audio/scriptPrompt.js)."""
    both = {"index": 3, "filename": "c.mp4", "start": 1.0, "end": 3.25}
    clips = [
        {**both, "description": "  La escena del puente  ", "label": "x"},  # la descripción, sin espacios
        {**both, "index": 4, "description": "", "label": "Etiqueta"},
        {**both, "index": 5, "description": None, "label": None},  # el archivo
        {"index": 6, "description": "   ", "label": "no se mira", "filename": "f.mp4"},  # solo espacios: fuera
        {"index": 7, "filename": "", "label": ""},  # sin nada: fuera
        {"index": None, "description": "sin índice", "start": 2, "end": 2, "duration": 4.05},  # el puesto y la duración
        {"description": "sin duración", "start": 5, "end": 1},
        {"index": 9, "description": "duración negativa", "duration": -2},
        {"index": 10, "description": "con null", "start": None, "end": 2.5},  # null es 0
        {"index": 11, "description": "falta start", "end": 2.5, "duration": 1.25},
        {"index": 12, "description": "textos", "start": "1", "end": "2.5"},
        {"index": 13, "description": "no número", "start": "a", "end": "2.5", "duration": "b"},
        {"index": 14, "description": "　﻿ ancho \n", "start": 0, "end": 0.05},  # espacios de JS
        {"index": 15, "description": 42, "start": 0, "end": 1},  # String(42)
        {"index": 16, "description": 0, "label": True, "start": 0, "end": 1},  # 0 no; String(true)
        {"index": 17, "description": False, "filename": "z.mp4", "start": 0, "end": 1},
        {"index": 18.5, "description": "índice real", "start": 0, "end": 0.15},
        {"index": "ab", "description": "índice de texto", "start": 0, "end": 1},
        None,  # sin clip: fuera, y no cuenta para el puesto
        7,
        {"description": "después del hueco", "start": 0, "end": 1234.56},
    ]
    calls: list = []
    for topic in ("", "   ", "cómo el silicio de la arena termina en una GPU", "  con espacios\n ", "﻿«ñandú» 😀 "):
        calls.append(["buildScriptPrompt", topic, []])
        calls.append(["buildScriptPrompt", topic, clips])
    calls.append(["buildScriptPrompt", "t", None])
    calls.append(["buildScriptPrompt", "t", [{"index": 1}, {"index": 2, "description": " "}]])  # sin ninguna: sin bloques
    for c in clips:
        calls.append(["buildScriptPrompt", "t", [c]])
    return {"name": "prompt del guion", "timeline": {"tracks": TRACKS, "clips": []}, "calls": calls}


def _bg_case() -> dict:
    """«Eliminar fondo» del editor: toggleBgAuto, changeBgAuto, startBgAutoFor (el cuerpo del POST y el estado
    «running») y el final del job (el efecto de bgJob.status) de VideoEditor.jsx, sobre clip.bg_removal."""
    sam = {"enabled": True, "auto": {"enabled": True, "provider": "sam21_tiny", "base_key": "k-sam", "status": "ready",
                                     "edits": [{"op": "keep", "size": 0.05, "points": [{"x": 0.5, "y": 0.5}], "t": 1.2,
                                                "tool": "smart"}]}}
    ready = {"enabled": True, "mode": "auto",
             "auto": {"enabled": True, "provider": "rvm_mobilenetv3", "base_key": "abc123", "status": "ready",
                      "mask_height": 720, "threshold": 0.4, "feather": 0.05, "model_version": "v2"},
             "chroma": {"enabled": True, "color": "#123456", "similarity": 0.6, "spill": 0.3}, "outline": {"enabled": True, "width": 0.5}}
    clips = [clip("b1", "V1", start=0, inp=1, out=6),  # nunca tuvo bg_removal
             clip("b2", "V1", start=7, inp=0, out=3, kind="image", filename="foto.png", source_duration=0.0),
             clip("b3", "V1", start=11, inp=0, out=4, kind="image", filename="anim.GIF?v=2"),
             clip("b4", "V2", start=0, inp=0, out=5, bg_removal=sam),
             clip("b5", "V2", start=6, inp=2, out=8, bg_removal=ready, asset_scope="library", asset_id="lib-7"),
             clip("b6", "V2", start=15, inp=0, out=2, bg_removal={"auto": {"provider": "u2net", "status": "error",
                                                                            "error": "sin GPU"}}),
             clip("b7", "V1", start=20, inp=0, out=3, bg_removal={"enabled": False, "auto": {"enabled": True}}),
             clip("b8", "A1", start=0, inp=0, out=3, kind="audio"),
             clip("b9", "V1", start=25, inp=0, out=3, bg_removal={"auto": {"provider": "u2net", "base_key": "x"}}),
             clip("b10", "V1", start=30, inp=0, out=2, asset_scope=""),  # sin ámbito: el del proyecto
             clip("b11", "V1", start=33, inp=1, out=4, name="Mi recorte", bg_removal={"chroma": {"enabled": True, "color": "#00f"}}),
             clip("b12", "V1", start=38, inp=0, out=2, name="", bg_removal=ready),
             clip("b13", "V1", start=41, inp=0, out=2, bg_removal={"enabled": False, "chroma": {"enabled": True}}),
             clip("b14", "V1", start=44, inp=0, out=2, bg_removal={"enabled": False, "auto": ready["auto"]})]
    patches = [{"provider": "birefnet_lite"}, {"provider": "sam21_large"}, {"provider": "u2net", "mask_height": 1080},
               {"provider": "nada"}, {"provider": None}, {"mask_height": 720}, {"stabilize": 0.3},
               {"threshold": 0.62}, {"softness": 0.1}, {"feather": 0.2}, {"expansion": -0.35}, {"opacity": 0.5},
               {"invert": True}, {"invert": False, "threshold": 1.5}, {}, {"base_key": "manual"}]
    chroma = [{"color": "#0000ff"}, {"color": "0xABC"}, {"color": "verde"}, {"similarity": 0.55}, {"similarity": 0},
              {"blend": 0.3}, {"spill": 0.7}, {"edge": 0.4}, {"shrink": -0.6}, {"shrink": 2}, {"color": "#FF00FF", "enabled": True}]
    outline = [{"enabled": True}, {"enabled": False}, {"color": "#123"}, {"color": ""}, {"width": 0.8}, {"width": 3},
               {"soft": 0.5}, {"opacity": 0.25}, {"opacity": -1}]
    jobs = [{"status": "done", "bg_removal": {"base_key": "nuevo", "provider": "birefnet_lite", "model_version": "1.4",
                                              "mask_fps": 12, "mask_height": 1080, "warning": "Va lento"}},
            {"status": "done", "bg_removal": {"base_key": "solo-clave"}},
            {"status": "done", "bg_removal": {}},
            {"status": "done"},
            {"status": "error", "error": "Cancelado"},
            {"status": "error", "error": ""},
            {"status": "error"},
            {"status": "running", "progress": 0.4}]
    calls: list = []
    for c in clips:
        for on in (True, False):
            calls.append(["bgToggleAuto", c["id"], on])
        for p in patches:
            calls.append(["bgChangeAuto", c["id"], p])
        calls.append(["bgStart", c["id"]])
        calls.append(["bgCutout", c["id"]])
        for on in (True, False):
            calls.append(["bgToggleChroma", c["id"], on])
        for p in chroma:
            calls.append(["bgChangeChroma", c["id"], p])
        calls.append(["bgResetChroma", c["id"]])
        for p in outline:
            calls.append(["bgChangeOutline", c["id"], p])
        for j in jobs:
            calls.append(["bgFinished", c["id"], j])
    return {"name": "eliminar fondo", "timeline": {"tracks": TRACKS, "clips": clips}, "calls": calls}


def crop_handle_norms_py(c: dict) -> list:
    xs = [c["cx"] - c["wf"] / 2, c["cx"], c["cx"] + c["wf"] / 2]
    ys = [c["cy"] - c["hf"] / 2, c["cy"], c["cy"] + c["hf"] / 2]
    return [(ix - 1, iy - 1, xs[ix], ys[iy]) for iy in range(3) for ix in range(3) if (ix, iy) != (1, 1)]


def cases() -> list[dict]:
    rng = random.Random(20261019)
    out = [{"name": "toFixed", "timeline": {"tracks": TRACKS, "clips": []},
            "calls": [["toFixed", x] for x in FIXED] + [["toFixed", rng.uniform(-100, 100)] for _ in range(300)]}]
    out.append({"name": "pose", "timeline": {"tracks": TRACKS, "clips": POSE_CLIPS}, "calls": _pose_calls(rng)})
    out.append({"name": "material", "timeline": {"tracks": TRACKS, "clips": []}, "calls": _material_calls(rng)})
    out.append({"name": "vista", "timeline": {"tracks": TRACKS, "clips": []}, "calls": _view_calls(rng)})
    shuffled = [track("A2", "audio"), track("V1", "video"), track("A1", "audio"), track("T1", "text"),
                track("V2", "video"), track("T2", "text")]
    out.append({"name": "orden de las pistas", "timeline": {"tracks": shuffled, "clips": []}, "calls": [["displayTracks"]]})
    for n in range(40):
        tl = _random_timeline(rng)
        out.append({"name": f"aleatoria {n}", "timeline": tl, "calls": _calls(rng, tl)})
    out.append({"name": "encuadre", "timeline": {"tracks": TRACKS, "clips": POSE_CLIPS}, "calls": _layout_calls(rng)})
    out.append(_linked_case(rng))
    out.append(_vfx_case(rng))
    out.append(_mask_case(rng))
    out.append(_fragment_case(rng))
    out.append(_crop_case(rng))
    out.append(_free_layout_case(random.Random(20261106)))  # su propio rng: no mueve los casos de después
    out.append(_sfx_name_case())
    out.append({"name": "textos", "timeline": {"tracks": TRACKS, "clips": TEXT_CLIPS}, "calls": _text_calls(rng)})
    out.append(_waveform_case(rng))
    out.append(_mask_ui_case(random.Random(20261107)))  # su propio rng, el último: no mueve nada
    out.append(_captions_case(random.Random(20261108)))
    out.append(_face_follow_case(random.Random(20261109)))
    out.append(_script_prompt_case())
    out.append(_bg_case())
    return out
