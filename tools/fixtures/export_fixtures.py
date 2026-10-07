"""Genera las fixtures de paridad de GenClipPro usando video-yt como referencia.

La app Python es la «verdad» durante la migración: este script le pasa cada
entrada y guarda lo que devuelve. Los tests de C++ (tests/parity) comprueban que
gc_core produce exactamente lo mismo.

Uso, con el Python de video-yt (tiene pydantic y el resto de dependencias):

    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\fixtures\\export_fixtures.py
    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\fixtures\\export_fixtures.py --real

Salida:
  tests/fixtures/model/synthetic.json   modelo: casos de model_cases.py        (al repo)
  tests/fixtures/edit/synthetic.json    edición: casos de ops_cases.py         (al repo)
  tests/fixtures/keyframes/synthetic.json  animación: casos de kf_cases.py     (al repo)
  tests/fixtures/layout/synthetic.json  encuadre: casos de layout_cases.py     (al repo)
  tests/fixtures/local/model/real.json  con --real: TUS proyectos de data/projects.json
  tests/fixtures/local/edit/real.json   con --real: operaciones sobre TUS timelines
  tests/fixtures/local/{keyframes,layout}/real.json  con --real: TUS clips
                                        tests/fixtures/local/ son datos personales: está
                                        en .gitignore y nunca se sube.

Las operaciones pasan por el `timeline_store.apply_op` REAL de video-yt (con el disco
sustituido por memoria) y con uuid.uuid4 sustituido por la misma secuencia que
`gc::core::SequentialIdSource`, para que los ids generados coincidan.
"""
from __future__ import annotations

import argparse
import copy
import json
import sys
import time
import uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
DEFAULT_VIDEO_YT = Path(r"E:\proyectos\video-yt")


_ORIGINAL_CONSTANT_PROPS = None


def _constant_props_in_order(items: list[dict]):
    """clip_attrs._constant_props con las claves en orden de aparición (y no el de un set).
    Comprueba en cada llamada que da lo mismo que la original (== de dict no mira el orden)."""
    from app import clip_attrs
    out: dict = {}
    for key in dict.fromkeys(key for k in items for key in k["props"]):
        v0 = items[0]["props"].get(key)
        if v0 is None or any(k["props"].get(key) is None or abs(k["props"][key] - v0) > clip_attrs._EPS
                             for k in items):
            out = None
            break
        out[key] = v0
    expected = _ORIGINAL_CONSTANT_PROPS(items)
    assert expected == out, f"_constant_props cambió en video-yt: {expected!r} != {out!r}"
    return out


# Biblioteca de SFX de la operación en curso (las recetas la leen con sfx.search/resolve).
_SFX: dict = {"items": [], "durations": {}}


def _text_width_estimate(font, bold, em, text, ls) -> float:
    """Ancho de texto estimado (0,55 em por carácter + espaciado): el que usa gc_core hasta
    que F4 mida con FreeType. Sustituye a font_metrics.text_width (Pillow)."""
    return len(text) * em * 0.55 + ls * len(text)


def _install_fakes() -> None:
    """Lo que en video-yt depende del disco o de las fuentes instaladas, inyectado: medida de
    texto, biblioteca de SFX y duración de cada sonido (ffprobe)."""
    from app import sfx, sound_design, text_ass
    text_ass.text_width = _text_width_estimate
    sfx.search = lambda *a, **k: {"items": copy.deepcopy(_SFX["items"]), "total": len(_SFX["items"])}
    sfx.resolve = lambda relpath: relpath if relpath in _SFX["durations"] else None
    sound_design.probe_duration = lambda path: _SFX["durations"][str(path)]


class Ctx:
    """Módulos de video-yt que hacen de referencia."""

    def __init__(self, root: Path):
        backend = root / "backend"
        if not (backend / "app" / "schemas.py").exists():
            sys.exit(f"No encuentro video-yt en {root} (falta backend/app/schemas.py). Usa --video-yt.")
        sys.path.insert(0, str(backend))
        from pydantic import ValidationError
        from app import clip_attrs, clip_speed, migrations, projects, schemas, timeline_history, timeline_store
        # _constant_props recorre un `set`: el orden de sus claves cambia en cada ejecución
        # (PYTHONHASHSEED). Se usa la misma función en orden de aparición, como el C++; el
        # resultado es el mismo salvo el orden, y así la fixture es estable y se compara con orden.
        _install_fakes()
        global _ORIGINAL_CONSTANT_PROPS
        if clip_attrs._constant_props is not _constant_props_in_order:
            _ORIGINAL_CONSTANT_PROPS = clip_attrs._constant_props
            clip_attrs._constant_props = _constant_props_in_order
        self.ValidationError = ValidationError
        self.clip_speed = clip_speed
        self.migrations = migrations
        self.projects = projects
        self.schemas = schemas
        self.timeline_history = timeline_history
        self.timeline_store = timeline_store
        self.root = root


def _loc_path(loc: tuple) -> str:
    """('timeline', 'clips', 3, 'reframe') → 'timeline.clips[3].reframe' (mismo formato que gc::Error::path)."""
    out = ""
    for part in loc:
        if isinstance(part, int):
            out += f"[{part}]"
        else:
            out += ("." if out else "") + str(part)
    return out


def _pure(value):
    """Lo que model_dump() devuelve, pasado por JSON (como lo guarda projects._save)."""
    return json.loads(json.dumps(value, ensure_ascii=False))


def _error(out: dict, e: Exception, ctx: Ctx) -> dict:
    if isinstance(e, ctx.ValidationError):
        out["error"] = str(e).splitlines()[0]
        out["error_paths"] = sorted({_loc_path(err["loc"]) for err in e.errors()})
    else:
        out["error"] = f"{type(e).__name__}: {e}"
        out["error_paths"] = []
    return out


# --- Modelo -----------------------------------------------------------------------------------

def run_model_case(case: dict, ctx: Ctx) -> dict:
    kind = case["kind"]
    raw = copy.deepcopy(case["input"])  # la entrada que se guarda no debe verse alterada
    out = {"name": case["name"], "kind": kind, "input": case["input"]}
    try:
        if kind == "project":
            result = ctx.projects._project_from_dict(raw).model_dump()
        elif kind == "timeline":
            result = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw)).model_dump()
        elif kind == "group":
            result = ctx.schemas.ProjectGroup.model_validate(raw).model_dump()
        elif kind == "migrate":
            result = ctx.migrations.migrate_timeline(raw)
        else:
            raise SystemExit(f"kind desconocido: {kind}")
        out["expected"] = _pure(result)
    except Exception as e:  # noqa: BLE001 — Python también falla aquí: C++ debe fallar
        _error(out, e, ctx)
    return out


# --- Edición ----------------------------------------------------------------------------------

class _SequentialUuid:
    """Igual que gc::core::SequentialIdSource: la n-ésima llamada → '{n:06x}ab' + 24 ceros."""

    def __init__(self):
        self.n = 0

    def __call__(self):
        self.n += 1
        return uuid.UUID(hex=f"{self.n:06x}ab" + "0" * 24)


def apply_op(ctx: Ctx, timeline: dict, op: str, params) -> dict:
    """timeline_store.apply_op de verdad, con el proyecto y el historial en memoria."""
    ts = ctx.timeline_store
    saved = (uuid.uuid4, ts._current, ts._load_history, ts._save_history, ctx.projects.save_timeline)
    uuid.uuid4 = _SequentialUuid()
    ts._current = lambda pid: copy.deepcopy(timeline)
    ts._load_history = lambda pid: ctx.timeline_history.empty_history()
    ts._save_history = lambda pid, hist: None
    ctx.projects.save_timeline = lambda pid, tl: None
    try:
        return ts.apply_op("fixture", op, copy.deepcopy(params))
    finally:
        uuid.uuid4, ts._current, ts._load_history, ts._save_history, ctx.projects.save_timeline = saved


def normalize_timeline(ctx: Ctx, raw: dict) -> dict:
    return _pure(ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw)).model_dump())


def timeline_delta(before: dict, after: dict) -> dict | None:
    """Solo lo que cambió: las pistas/metadatos completos y los clips nuevos o distintos."""
    ids_in = [c["id"] for c in before["clips"]]
    ids_out = [c["id"] for c in after["clips"]]
    if len(set(ids_in)) != len(ids_in) or len(set(ids_out)) != len(ids_out):
        return None  # ids repetidos: no se puede reconstruir por id
    same = {c["id"]: json.dumps(c, ensure_ascii=False) for c in before["clips"]}
    return {
        "keys": list(after.keys()),
        "meta": {k: v for k, v in after.items() if k != "clips"},
        "clip_ids": ids_out,
        "clips": {c["id"]: c for c in after["clips"] if same.get(c["id"]) != json.dumps(c, ensure_ascii=False)},
    }


def run_edit_case(ctx: Ctx, base_id: str, timeline: dict, name: str, op: str, params, compact: bool,
                  sfx_library: dict | None = None) -> dict:
    out = {"name": name, "timeline": base_id, "op": op, "params": params}
    if sfx_library is not None:
        out["sfx_library"] = sfx_library
    _SFX.clear()
    _SFX.update(copy.deepcopy(sfx_library or {"items": [], "durations": {}}))
    try:
        r = apply_op(ctx, timeline, op, params)
    except Exception as e:  # noqa: BLE001
        return _error(out, e, ctx)
    result = _pure(r["timeline"])
    expected = {"changed": r["changed"], "warnings": r["warnings"]}
    delta = timeline_delta(timeline, result) if compact else None
    if delta is not None:
        expected["timeline_delta"] = delta
    else:
        expected["timeline"] = result
    out["expected"] = expected
    return out


def synthetic_edit_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    from ops_cases import materialize
    bases: dict[str, dict] = {}
    keys: dict[str, str] = {}
    cases = []
    for c in materialize():
        key = json.dumps(c["base"], sort_keys=True)
        if key not in keys:
            keys[key] = f"base{len(keys)}"
            bases[keys[key]] = normalize_timeline(ctx, c["base"])
        bid = keys[key]
        cases.append(run_edit_case(ctx, bid, bases[bid], c["name"], c["op"], c["params"], compact=False,
                                   sfx_library=c.get("sfx_library")))
    return {"timelines": bases, "cases": cases}


def real_edit_fixtures(ctx: Ctx, data: dict) -> dict:
    """Operaciones típicas del editor sobre cada timeline real (muestreo de clips)."""
    sys.path.insert(0, str(HERE))
    from ops_cases import SFX_LIBRARY as library
    timelines: dict[str, dict] = {}
    cases = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        tl = _pure(proj.timeline.model_dump())
        pid = proj.id
        timelines[pid] = tl
        ops: list[tuple[str, str, dict]] = []
        clips = tl["clips"]
        step = max(1, len(clips) // 6)
        for c in clips[::step][:6]:
            dur = ctx.clip_speed.clip_timeline_duration(c)
            cid = c["id"]
            ops += [
                (f"mover {cid} +1 s", "move_clip", {"clip_id": cid, "start": c["start"] + 1.0}),
                (f"mover {cid} al inicio", "move_clip", {"clip_id": cid, "start": 0}),
                (f"duplicar {cid}", "duplicate_clip", {"clip_id": cid}),
                (f"quitar {cid}", "remove_clip", {"clip_id": cid}),
            ]
            if dur > 0.01:
                ops.append((f"cortar {cid} por la mitad", "split_clip",
                            {"clip_id": cid, "at_time": c["start"] + dur / 2}))
            others = [t["id"] for t in tl["tracks"]
                      if t["id"] != c["track_id"] and _fits(c["kind"], t["kind"])]
            if others:
                ops.append((f"mover {cid} a {others[0]}", "move_clip",
                            {"clip_id": cid, "track_id": others[0], "start": c["start"]}))
        tracks = tl["tracks"]
        stack = [t["id"] for t in tracks if t["kind"] in ("video", "text")]
        audio = [t["id"] for t in tracks if t["kind"] == "audio"]
        texts = [t["id"] for t in tracks if t["kind"] == "text"]
        if tracks:
            ops += [("renombrar la primera pista", "rename_track", {"track_id": tracks[0]["id"], "name": "Renombrada"}),
                    ("quitar la última pista", "remove_track", {"track_id": tracks[-1]["id"]})]
        if len(stack) >= 2:
            ops += [("pila: la primera encima de la última", "reorder_track",
                     {"track_id": stack[0], "target_track_id": stack[-1], "place": "above"}),
                    ("pila: la última debajo de la primera", "reorder_track",
                     {"track_id": stack[-1], "target_track_id": stack[0], "place": "below"})]
        if audio and texts:
            ops.append(("ligar audio y texto", "link_tracks", {"track_id": audio[0], "to_track_id": texts[0]}))
        ops += [
            ("añadir pista de vídeo", "add_track", {"kind": "video"}),
            ("añadir pista de texto", "add_track", {"kind": "text", "name": "Notas"}),
            ("formato 16:9", "set_project_format", {"aspect": "16:9"}),
            ("formato 1080", "set_project_format", {"resolution": 1080}),
            ("marcadores", "set_markers", {"markers": [{"t": 2.25, "label": "A"}, {"t": 0.5}]}),
        ]
        ops += extra_real_ops(tl, ctx)
        ops += media_real_ops(tl, proj, ctx)
        ops += compound_real_ops(tl)
        ops += follow_real_ops(tl, proj, ctx)
        ops += sound_real_ops(tl)
        ops += animate_real_ops(tl)
        ops += recipe_real_ops(tl)
        for name, op, params in ops:
            cases.append(run_edit_case(ctx, pid, tl, f"{pid} · {name}", op, params, compact=True,
                                       sfx_library=library))
    return {"timelines": timelines, "cases": cases, "sfx_library": library}


def extra_real_ops(tl: dict, ctx: Ctx) -> list[tuple[str, str, dict]]:
    """Operaciones de propiedades, encuadre y audio (2ª tanda de F2) sobre una timeline real."""
    ops: list[tuple[str, str, dict]] = []
    clips = tl["clips"]
    step = max(1, len(clips) // 6)
    for c in clips[::step][:6]:
        cid, kind = c["id"], c["kind"]
        dur = ctx.clip_speed.clip_timeline_duration(c)
        ops.append((f"opacidad de {cid}", "update_clip", {"clip_id": cid, "patch": {"opacity": 0.5}}))
        if kind != "audio":
            ops.append((f"voltear {cid}", "update_clip", {"clip_id": cid, "patch": {"flip_h": True}}))
        if kind in ("video", "audio"):
            ops += [(f"velocidad 0.5 en {cid} (crece)", "update_clip", {"clip_id": cid, "patch": {"speed": 0.5}}),
                    (f"fundido de entrada en {cid}", "set_clip_volume", {"clip_id": cid, "fade": "in"})]
            if dur > 0.1:
                ops.append((f"rampa de reverb en {cid}", "set_clip_audio_fx",
                            {"clip_id": cid, "audio_fx": {"reverb": 0.4}, "ramp": {"start": 0, "end": min(1, dur / 2)}}))
        if kind in ("video", "image"):
            ops += [(f"encuadre centrado en {cid}", "reframe_clip", {"clip_id": cid, "mode": "center", "zoom": 0.8}),
                    (f"filtro cálido en {cid}", "set_clip_effects",
                     {"clip_id": cid, "filters": [{"id": "warm", "amount": 0.6}]})]
            if c.get("layout") == "overlay":
                ops.append((f"recorte de {cid}", "crop_clip",
                            {"clip_ids": [cid], "w": 0.6, "h": 0.6, "cx": 0.4, "cy": 0.5}))
        if kind in ("video", "image", "shape", "text"):
            ops.append((f"máscara en {cid}", "set_clip_masks", {"clip_id": cid, "masks": [{"type": "circle"}]}))
    audio = [t["id"] for t in tl["tracks"] if t["kind"] == "audio"]
    if audio:
        ops.append(("volumen de la pista de audio", "set_track_audio", {"track_id": audio[0], "volume": 0.8}))
    media = next((c for c in clips if c["asset_kind"] == "clips" and c["filename"]), None)
    if media:
        ops.append(("sustituir el material", "replace_media",
                    {"sources": [{"filename": media["filename"], "delta": 1}],
                     "target": {"asset_id": "nuevo", "filename": "nuevo.mp4", "duration": 600}}))
    return ops


def media_real_ops(tl: dict, proj, ctx: Ctx) -> list[tuple[str, str, dict]]:
    """3ª tanda de F2 sobre una timeline real: catálogo, congelar, subtítulos, figuras…"""
    ops: list[tuple[str, str, dict]] = []
    videos = [c for c in tl["clips"] if c["kind"] == "video"]
    visuals = [c for c in tl["clips"] if c["kind"] in ("video", "image", "text", "shape")]
    for v in videos[:3]:
        dur = ctx.clip_speed.clip_timeline_duration(v)
        ops += [(f"glitch en {v['id']}", "apply_effect",
                 {"clip_ids": [v["id"]], "effect": "glitch_rgb", "params": {"amount": 0.4}}),
                (f"croma en {v['id']}", "set_clip_bg_removal",
                 {"clip_id": v["id"], "bg_removal": {"mode": "chroma", "chroma": {"enabled": True}}}),
                (f"congelar {v['id']}", "freeze_frame",
                 {"clip_id": v["id"], "at_time": v["start"] + dur / 2,
                  "image": {"id": "f1", "filename": "frame.png", "label": "Congelado"}})]
    for c in visuals[:3]:
        ops.append((f"animación de entrada en {c['id']}", "set_clip_animation",
                    {"clip_ids": [c["id"]], "slot": "in", "animation": "fade_in"}))
    segments = [s.model_dump() for t in proj.transcripts for s in t.segments][:40]
    if videos and segments:
        ops.append(("subtítulos desde la transcripción", "add_subtitles",
                    {"source_clip_id": videos[0]["id"], "segments": segments, "style": {"max_words": 4},
                     "transcript_id": proj.transcripts[0].id}))
    ops += [("figura", "add_shape", {"shape": {"type": "arrow"}, "start": 1}),
            ("barras de cine", "add_cinema_bars", {"animate": True}),
            ("capa de ajuste", "add_adjustment_layer", {"start": 0, "duration": 3, "filters": ["noir"]})]
    return ops


def compound_real_ops(tl: dict) -> list[tuple[str, str, dict]]:
    """4ª tanda de F2 sobre una timeline real: pegar atributos entre tus clips."""
    ops: list[tuple[str, str, dict]] = []
    by_kind: dict[str, list[str]] = {}
    for c in tl["clips"]:
        by_kind.setdefault(c["kind"], []).append(c["id"])
    videos, texts = by_kind.get("video", []), by_kind.get("text", [])
    visuals = videos + by_kind.get("image", []) + texts + by_kind.get("shape", [])
    animated = [c["id"] for c in tl["clips"] if isinstance(c.get("keyframes"), dict) and c["keyframes"].get("items")]
    if len(videos) >= 2:
        ops.append(("pegar todos los atributos entre vídeos", "paste_clip_attributes",
                    {"source_clip_id": videos[0], "target_clip_ids": videos[1:4]}))
    for src in animated[:3]:
        others = [v for v in visuals if v != src][:3]
        if others:
            ops.append((f"pegar la animación de {src}", "paste_clip_attributes",
                        {"source_clip_id": src, "target_clip_ids": others, "groups": ["animation", "crop", "mask"]}))
    if len(texts) >= 2:
        ops.append(("pegar el estilo entre textos", "paste_clip_attributes",
                    {"source_clip_id": texts[0], "target_clip_ids": texts[1:4],
                     "groups": ["style", "transform", "animation"]}))
    if videos and len(visuals) >= 2:
        ops.append(("pegar posición, mezcla y velocidad del primer vídeo", "paste_clip_attributes",
                    {"source_clip_id": videos[0], "target_clip_ids": visuals[:8],
                     "groups": ["transform", "blend", "flip", "speed"]}))
    return ops


def follow_real_ops(tl: dict, proj, ctx: Ctx) -> list[tuple[str, str, dict]]:
    """follow_object sobre una timeline real: un recorrido sintético sobre cada uno de tus
    primeros vídeos y, como acompañantes, los clips visuales que coinciden con él en el tiempo."""
    ops: list[tuple[str, str, dict]] = []
    sizes = {a.id: (a.width, a.height) for a in (*proj.clips, *proj.images)
             if getattr(a, "width", None) and getattr(a, "height", None)}
    dur = ctx.clip_speed.clip_timeline_duration
    for v in [c for c in tl["clips"] if c["kind"] == "video"][:3]:
        v0, v1 = v["start"], v["start"] + dur(v)
        followers = [c for c in tl["clips"] if c["id"] != v["id"] and c["kind"] in ("text", "shape", "image", "video")
                     and c["start"] < v1 and c["start"] + dur(c) > v0][:2]
        if not followers or v["out_point"] <= v["in_point"]:
            continue
        n = 16
        track = [{"t": v["in_point"] + (v["out_point"] - v["in_point"]) * i / (n - 1), "cx": 0.3 + 0.025 * i,
                  "cy": 0.6 - 0.015 * i, "s": 1 + 0.04 * i, "rot": 13 * i} for i in range(n)]
        src_w, src_h = sizes.get(v["asset_id"], (1920, 1080))
        dims = {"srcW": src_w, "srcH": src_h, "outW": tl["width"], "outH": tl["height"]}
        for f, mode in zip(followers, ("position_scale_rotation", "position_scale")):
            anchor = max(v0, f["start"]) + 0.25
            ops.append((f"{f['id']} sigue a un objeto de {v['id']} ({mode})", "follow_object",
                        {"follower_clip_id": f["id"], "video_clip_id": v["id"], "track": track, "box_w": 0.15,
                         "anchor_t": anchor, "dims": dims, "mode": mode, "min_gap": 0.2}))
    return ops


def sound_real_ops(tl: dict) -> list[tuple[str, str, dict]]:
    """add_sound_design sobre una timeline real: sonidos tipo «Sonorizar con IA» en tus escenas."""
    ops: list[tuple[str, str, dict]] = []
    for v in [c for c in tl["clips"] if c["kind"] == "video"][:3]:
        sounds = [
            {"sfx": {"id": "whoosh/rapido.wav", "name": "Whoosh rápido", "duration": 0.7}, "start": 0,
             "duration": 1, "volume": 0.7, "kind": "spot", "what": "entrada"},
            {"sfx": {"id": "ambiente/ciudad.ogg", "duration": 0}, "start": 0, "duration": 600, "volume": 0.25,
             "kind": "ambience", "what": "ciudad de fondo"},
            {"sfx": {"id": "impactos/golpe.mp3", "duration": 1.5}, "start": 1.25, "duration": 0.5, "kind": "spot"},
            {"sfx": {"id": "impactos/golpe.mp3", "duration": 1.5}, "start": 1.3, "duration": 0.5, "kind": "spot"},
        ]
        ops.append((f"sonorizar {v['id']}", "add_sound_design", {"clip_id": v["id"], "sounds": sounds}))
    return ops


def recipe_real_ops(tl: dict) -> list[tuple[str, str, dict]]:
    """apply_recipe sobre una timeline real: las cinco recetas con tus clips."""
    ops: list[tuple[str, str, dict]] = [("receta cinema_grade", "apply_recipe", {"recipe": "cinema_grade"})]
    texts = [c["id"] for c in tl["clips"] if c["kind"] == "text"]
    media = [c["id"] for c in tl["clips"] if c["kind"] in ("video", "image")]
    late = [c["id"] for c in tl["clips"] if c["kind"] == "video" and c["start"] > 0.3]
    if texts:
        ops += [("receta text_reflection", "apply_recipe", {"recipe": "text_reflection", "clip_ids": texts[:2]}),
                ("receta pass_through_text", "apply_recipe",
                 {"recipe": "pass_through_text", "clip_ids": texts[:2], "params": {"zoom_time": 0.8}})]
    if len(media) >= 2:
        ops.append(("receta film_strips", "apply_recipe", {"recipe": "film_strips", "clip_ids": media[:4]}))
    if late:
        ops.append(("receta subject_pop", "apply_recipe", {"recipe": "subject_pop", "clip_ids": late[:1]}))
    return ops


def animate_real_ops(tl: dict) -> list[tuple[str, str, dict]]:
    """animate_clip sobre una timeline real: un preset por tipo de clip, alguno con envolvente."""
    ops: list[tuple[str, str, dict]] = []
    presets = {"video": ("zoom_in", "slide_left", "fade_out"), "image": ("pop", "spin"), "text": ("pop", "pulse"),
               "shape": ("draw_in", "spin_in")}
    env = [[0, 0.05], [0.15, 0.9], [0.3, 0.8], [0.5, 0.4], [0.8, 0.2]]
    for kind, motions in presets.items():
        clips = [c for c in tl["clips"] if c["kind"] == kind][:2]
        for c in clips:
            for i, m in enumerate(motions):
                params = {"clip_id": c["id"], "motion": m}
                if i == 1:
                    params["envelope"] = env
                ops.append((f"{m} en {c['id']}", "animate_clip", params))
    return ops


# --- Historial (timeline_history) -----------------------------------------------------------

def _valid_timelines(ctx: Ctx, entries) -> list[dict]:
    """Las entradas que gc_core sabe leer (dicts que son una timeline válida), normalizadas."""
    out = []
    for e in entries:
        if not isinstance(e, dict):
            continue
        try:
            out.append(normalize_timeline(ctx, e))
        except Exception:  # noqa: BLE001 — una entrada ilegible se descarta
            pass
    return out


def _normalized_history(ctx: Ctx, hist: dict) -> dict:
    undo = hist.get("undo") if isinstance(hist.get("undo"), list) else []
    redo = hist.get("redo") if isinstance(hist.get("redo"), list) else []
    cps = hist.get("checkpoints") if isinstance(hist.get("checkpoints"), dict) else {}
    checkpoints = {}
    for name, tl in cps.items():
        norm = _valid_timelines(ctx, [tl])
        if norm:
            checkpoints[name] = norm[0]
    return {"undo": _valid_timelines(ctx, undo), "redo": _valid_timelines(ctx, redo), "checkpoints": checkpoints}


def synthetic_history_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import history_cases as hc
    th = ctx.timeline_history
    tls = {k: normalize_timeline(ctx, v) for k, v in hc.TIMELINES.items()}

    def name_of(tl):
        return None if tl is None else next(k for k, v in tls.items() if v == tl)

    hist = th.empty_history()
    steps = []
    for step in hc.STEPS:
        kind, restored = step[0], None
        if kind == "snapshot":
            hist = th.snapshot(hist, tls[step[1]])
        elif kind == "undo":
            hist, restored = th.undo(hist, tls[step[1]])
        elif kind == "redo":
            hist, restored = th.redo(hist, tls[step[1]])
        elif kind == "checkpoint":
            hist = th.checkpoint(hist, step[1], tls[step[2]])
        elif kind == "restore":   # timeline_store.restore_checkpoint
            restored = th.restore_checkpoint(hist, step[1])
            if restored is not None:
                hist = th.snapshot(hist, tls[step[2]])
        steps.append({"step": list(step), "restored": name_of(restored),
                      "undo": [name_of(t) for t in hist["undo"]], "redo": [name_of(t) for t in hist["redo"]],
                      "checkpoints": [[n, name_of(t)] for n, t in hist["checkpoints"].items()]})
    return _pure({"limit": th.DEFAULT_LIMIT, "timelines": tls, "steps": steps, "final": hist,
                  "raw": hc.RAW, "raw_expected": _normalized_history(ctx, hc.RAW)})


def real_history_fixtures(ctx: Ctx, data: dict) -> dict:
    """El historial de cada uno de tus proyectos: el archivo tal cual y, para comprobarlo, las
    primeras y últimas entradas y los checkpoints normalizados."""
    hist_dir = ctx.root / "backend" / "data" / "history"
    out_dir = REPO / "tests" / "fixtures" / "local" / "history"
    out_dir.mkdir(parents=True, exist_ok=True)
    cases = []
    for p in data.get("projects", []):
        pid = p.get("id")
        src = hist_dir / f"{pid}.json"
        if not src.exists():
            continue
        (out_dir / f"{pid}.json").write_bytes(src.read_bytes())
        norm = _normalized_history(ctx, json.loads(src.read_text(encoding="utf-8")))
        sample = {k: [norm[k][0], norm[k][-1]] if len(norm[k]) > 1 else norm[k] for k in ("undo", "redo")}
        cases.append({"project": pid, "file": f"{pid}.json", "undo_count": len(norm["undo"]),
                      "redo_count": len(norm["redo"]), "first_last": sample, "checkpoints": norm["checkpoints"]})
    return _pure({"cases": cases})


# --- Animación (keyframes y máscaras) ---------------------------------------------------------

def keyframe_case(ctx: Ctx, name: str, raw_clip: dict, times: list[float]) -> dict:
    from app import clip_keyframes as ck
    from app import clip_mask as cm
    c = ctx.schemas.TimelineClip.model_validate(raw_clip)
    dur = ctx.clip_speed.clip_timeline_duration(c)
    samples = [{"t": t, "props": ck.clip_props_at(c, t), "style": ck.text_style_at(c, t),
                "masks": cm.clip_masks_at(c, t, include_adjust=True), "volume": ck.clip_volume_at(c, t)}
               for t in times]
    return _pure({"name": name, "clip": c.model_dump(), "samples": samples,
                  "text_style_animates": ck.text_style_animates(c), "has_mask": cm.has_mask(c),
                  "mask_animates": cm.mask_animates(c, dur), "duration": dur})


def synthetic_keyframe_fixtures(ctx: Ctx) -> dict:
    from app import clip_keyframes as ck
    from app import clip_mask as cm
    sys.path.insert(0, str(HERE))
    import kf_cases as kc
    return _pure({
        "clips": [keyframe_case(ctx, c["name"], c["clip"], kc.TIMES) for c in kc.CLIPS],
        "ease": [{**e, "y": ck.ease_t(e["u"], e["kind"], e["bezier"])} for e in kc.EASE],
        "masks": [{"input": m, "expected": cm.normalize_mask(m)} for m in kc.MASKS],
        "styles": [{"style": s, "anim": a, "expected": ck.with_text_style_kf(s, a)} for s, a in kc.STYLES],
    })


def real_keyframe_fixtures(ctx: Ctx, data: dict) -> dict:
    """Clips reales con keyframes, máscaras o texto, muestreados en sus keyframes y entre medias."""
    cases = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        for c in proj.timeline.clips:
            kf = c.keyframes if isinstance(c.keyframes, dict) else {}
            if not (kf.get("items") or c.masks or c.kind == "text"):
                continue
            dur = ctx.clip_speed.clip_timeline_duration(c)
            times = {0.0, dur, *(dur * k / 6 for k in range(1, 6))}
            for it in kf.get("items") or []:
                if isinstance(it, dict) and isinstance(it.get("t"), (int, float)):
                    times |= {it["t"], max(0.0, it["t"] - 0.001), it["t"] + 0.001}
            cases.append(keyframe_case(ctx, f"{proj.id} · {c.id}", c.model_dump(), sorted(times)))
    return {"clips": cases, "ease": [], "masks": [], "styles": []}


# --- Encuadre (clip_layout) -------------------------------------------------------------------

def _floats(values) -> list[float]:
    """Las salidas de clip_layout son cálculos, no datos guardados: se comparan como reales."""
    return [float(v) for v in values]


def _outcome(fn, *args) -> dict:
    """{"ok": [...]} o {"error": tipo}. NaN e infinito van como texto ("nan", "inf", "-inf"):
    JSON no los admite y Python no falla con un transform {"scale": "nan"}."""
    try:
        values = fn(*args)
    except Exception as e:  # noqa: BLE001 — basta con saber que Python falla
        return {"error": type(e).__name__}
    return {"ok": [repr(v) if isinstance(v, float) and v - v != 0 else v for v in values]}


def dest_case(transform, crop_sw: float, crop_sh: float, out_w: float, out_h: float) -> dict:
    from app import clip_layout as cl
    return {"transform": transform, "crop_sw": crop_sw, "crop_sh": crop_sh, "out_w": out_w, "out_h": out_h,
            "rect": _outcome(cl.dest_rect, transform, crop_sw, crop_sh, out_w, out_h),
            "even": _outcome(cl.dest_rect_even, transform, crop_sw, crop_sh, out_w, out_h)}


def layout_case(ctx: Ctx, name: str, raw_clip: dict, crops: list[tuple], points: list[tuple]) -> dict:
    """crops: (src_aspect, out_aspect, src_t, local_t). points: (nx, ny, src_w, src_h, out_w, out_h, src_t, local_t)."""
    from app import clip_layout as cl
    c = ctx.schemas.TimelineClip.model_validate(raw_clip)
    out_crops = []
    for sa, oa, st, lt in crops:
        w = cl.crop_window(c, sa, oa, st, lt)
        out_crops.append({"src_aspect": sa, "out_aspect": oa, "src_t": st, "local_t": lt,
                          "crop": _floats([w["cx"], w["cy"], w["wf"], w["hf"]])})
    out_points = [{"args": list(p), "out": _floats(cl.source_point_to_output(c, *p))} for p in points]
    return _pure({"name": name, "clip": c.model_dump(), "is_overlay": cl.is_overlay(c),
                  "flip": list(cl.clip_flip(c)), "flip_filters": cl.flip_filters(c),
                  "crops": out_crops, "points": out_points})


def synthetic_layout_fixtures(ctx: Ctx) -> dict:
    from app import clip_layout as cl
    sys.path.insert(0, str(HERE))
    import layout_cases as lc
    crops = [(sa, oa, st, lt) for sa, oa in lc.ASPECTS for st, lt in lc.TIMES]
    points = [(nx, ny, sw, sh, ow, oh, st, lt) for sw, sh, ow, oh in lc.DIMS for st, lt in lc.POINT_TIMES
              for nx, ny in lc.POINTS]
    return _pure({
        "clips": [layout_case(ctx, c["name"], c["clip"], crops, points) for c in lc.CLIPS],
        "dest": [dest_case(t, *s) for t in lc.TRANSFORMS for s in lc.DEST_SIZES],
        "crop_px": [{"args": list(a), "out": _floats(cl.source_crop_px(*a))} for a in lc.CROP_PX],
    })


def real_layout_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus clips de vídeo e imagen: encuadre en 5 instantes, tres puntos llevados a la salida y,
    con el recorte de cada instante, el rectángulo de destino de su transform (como compose.py)."""
    from app import clip_layout as cl
    clips, dests = [], []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        out_w, out_h = proj.timeline.width, proj.timeline.height
        dims = {a.id: (a.width, a.height) for a in (*proj.clips, *proj.images)
                if getattr(a, "width", None) and getattr(a, "height", None)}
        for c in proj.timeline.clips:
            if c.kind not in ("video", "image"):
                continue
            src_w, src_h = dims.get(c.asset_id, (1920, 1080))
            dur = ctx.clip_speed.clip_timeline_duration(c)
            times = [(c.in_point + dur * k / 4 * (c.speed or 1.0), dur * k / 4) for k in range(5)]
            crops = [(src_w / src_h, out_w / out_h, st, lt) for st, lt in times]
            points = [(nx, ny, src_w, src_h, out_w, out_h, st, lt) for st, lt in times
                      for nx, ny in ((0.5, 0.5), (0.2, 0.3), (0.9, 0.8))]
            case = layout_case(ctx, f"{proj.id} · {c.id}", c.model_dump(), crops, points)
            clips.append(case)
            for cr in case["crops"]:
                cx, cy, wf, hf = cr["crop"]
                _, _, csw, csh = cl.source_crop_px(wf, hf, cx, cy, src_w, src_h)
                dests.append(dest_case(c.transform, csw, csh, out_w, out_h))
    return _pure({"clips": clips, "dest": dests, "crop_px": []})


def _fits(clip_kind: str, track_kind: str) -> bool:
    family = {"video": "video", "image": "video", "shape": "video", "motion": "video", "adjustment": "video",
              "audio": "audio", "text": "text"}
    return family.get(clip_kind) == track_kind


# --- Salida -----------------------------------------------------------------------------------

def write(path: Path, payload, count: int, errors: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    size = path.stat().st_size / 1024
    print(f"{path.relative_to(REPO)}: {count} casos ({errors} de error), {size:,.0f} KB")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video-yt", type=Path, default=DEFAULT_VIDEO_YT, help="raíz del repo video-yt")
    ap.add_argument("--real", action="store_true", help="exportar también tus proyectos reales (local)")
    args = ap.parse_args()

    ctx = Ctx(args.video_yt)
    sys.path.insert(0, str(HERE))
    from model_cases import CASES

    # Catálogo de efectos: recurso de la app, lo genera el frontend de video-yt.
    src_catalog = args.video_yt / "backend" / "app" / "fx_assets" / "catalog.json"
    dst_catalog = REPO / "resources" / "fx" / "catalog.json"
    data_catalog = src_catalog.read_bytes()
    if not dst_catalog.exists() or dst_catalog.read_bytes() != data_catalog:
        dst_catalog.parent.mkdir(parents=True, exist_ok=True)
        dst_catalog.write_bytes(data_catalog)
        print(f"{dst_catalog.relative_to(REPO)}: actualizado desde video-yt")

    t0 = time.perf_counter()
    model = [run_model_case(c, ctx) for c in CASES]
    write(REPO / "tests" / "fixtures" / "model" / "synthetic.json", model, len(model),
          sum("error" in c for c in model))
    edit = synthetic_edit_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "edit" / "synthetic.json", edit, len(edit["cases"]),
          sum("error" in c for c in edit["cases"]))
    kf = synthetic_keyframe_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "keyframes" / "synthetic.json", kf,
          len(kf["clips"]) + len(kf["ease"]) + len(kf["masks"]) + len(kf["styles"]), 0)
    hist = synthetic_history_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "history" / "synthetic.json", hist, len(hist["steps"]), 0)
    lay = synthetic_layout_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "layout" / "synthetic.json", lay,
          len(lay["clips"]) + len(lay["dest"]) + len(lay["crop_px"]),
          sum("error" in d["rect"] or "error" in d["even"] for d in lay["dest"]))

    if args.real:
        data_file = args.video_yt / "backend" / "data" / "projects.json"
        data = json.loads(data_file.read_text(encoding="utf-8"))
        cases = [{"name": f"{p.get('id')} · {p.get('name')}", "kind": "project", "input": p}
                 for p in data.get("projects", [])]
        cases += [{"name": f"carpeta {g.get('id')} · {g.get('name')}", "kind": "group", "input": g}
                  for g in data.get("groups", [])]
        real = [run_model_case(c, ctx) for c in cases]
        write(REPO / "tests" / "fixtures" / "local" / "model" / "real.json", real, len(real),
              sum("error" in c for c in real))
        edit_real = real_edit_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "edit" / "real.json", edit_real, len(edit_real["cases"]),
              sum("error" in c for c in edit_real["cases"]))
        kf_real = real_keyframe_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "keyframes" / "real.json", kf_real, len(kf_real["clips"]), 0)
        hist_real = real_history_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "history" / "real.json", hist_real, len(hist_real["cases"]), 0)
        lay_real = real_layout_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "layout" / "real.json", lay_real,
              len(lay_real["clips"]) + len(lay_real["dest"]),
              sum("error" in d["rect"] or "error" in d["even"] for d in lay_real["dest"]))
    print(f"listo en {time.perf_counter() - t0:.1f} s")


if __name__ == "__main__":
    main()
