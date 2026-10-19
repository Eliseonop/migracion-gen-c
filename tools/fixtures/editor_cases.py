"""Casos sintéticos de paridad de la timeline del EDITOR (gc_core timeline_ui ↔ editorModel.js).

Timelines aleatorias (semilla fija) con clips solapados, pegados y con huecos, de todos los tipos
y velocidades (también las que JS sustituye por 1), y llamadas a las funciones que usa la timeline
al arrastrar, recortar y seleccionar. Node las ejecuta sobre el JS real (editor_ui_js.mjs), con los
clips tal como los deja pydantic.
"""
from __future__ import annotations

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
    out.append({"name": "textos", "timeline": {"tracks": TRACKS, "clips": TEXT_CLIPS}, "calls": _text_calls(rng)})
    return out
