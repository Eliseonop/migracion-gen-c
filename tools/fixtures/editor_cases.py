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
        clips.append(clip(f"c{n}", tid, kind, start=start, inp=inp, out=out, **kw))
    return {"tracks": TRACKS, "clips": clips}


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


def cases() -> list[dict]:
    rng = random.Random(20261019)
    out = [{"name": "toFixed", "timeline": {"tracks": TRACKS, "clips": []},
            "calls": [["toFixed", x] for x in FIXED] + [["toFixed", rng.uniform(-100, 100)] for _ in range(300)]}]
    out.append({"name": "vista", "timeline": {"tracks": TRACKS, "clips": []}, "calls": _view_calls(rng)})
    shuffled = [track("A2", "audio"), track("V1", "video"), track("A1", "audio"), track("T1", "text"),
                track("V2", "video"), track("T2", "text")]
    out.append({"name": "orden de las pistas", "timeline": {"tracks": shuffled, "clips": []}, "calls": [["displayTracks"]]})
    for n in range(40):
        tl = _random_timeline(rng)
        out.append({"name": f"aleatoria {n}", "timeline": tl, "calls": _calls(rng, tl)})
    return out
