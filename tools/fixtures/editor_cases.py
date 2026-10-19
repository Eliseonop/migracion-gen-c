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
