"""Genera las fixtures de paridad de GenClipPro usando video-yt como referencia.

La app Python es la «verdad» durante la migración: este script le pasa cada
entrada y guarda lo que devuelve. Los tests de C++ (tests/parity) comprueban que
gc_core produce exactamente lo mismo.

Uso, con el Python de video-yt (tiene pydantic y el resto de dependencias):

    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\fixtures\\export_fixtures.py
    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\fixtures\\export_fixtures.py --real

Salida (al repo):
  tests/fixtures/model/synthetic.json      modelo: casos de model_cases.py
  tests/fixtures/edit/synthetic.json       edición: casos de ops_cases.py
  tests/fixtures/keyframes/synthetic.json  animación: casos de kf_cases.py
  tests/fixtures/motion/synthetic.json     envolvente de un sonido (clip_motion): motion_cases.py
  tests/fixtures/media/synthetic.json      cabeceras de imagen (images.probe_gif) y sondas de medios: media_cases.py
  tests/fixtures/export_input/synthetic.json  cómo lee el export el material (clip_kind.py): export_input_cases.py
  tests/fixtures/media/decode.json         decode como el export: media_cases.DECODE (MD5 por fotograma del ffmpeg del PATH)
  tests/fixtures/media/seek.json           seek exacto de frame_grab.extract_frame: media_cases.SEEK
  tests/fixtures/media/chroma.json         cadenas de FFmpeg del croma (clip_bg.py): media_cases.CHROMA
  tests/fixtures/render/synthetic.json     fotogramas del compuesto real (compose.render_frame): media_cases.RENDER
  tests/fixtures/layout/synthetic.json     encuadre: casos de layout_cases.py
  tests/fixtures/eval/synthetic.json       evaluación por clip: casos de eval_cases.py
  tests/fixtures/layers/synthetic.json     capas de un instante: layers_cases.py (layersAt con Node)
  tests/fixtures/frame/synthetic.json      fotograma: frame_cases.py (drawComposite con Node)
  tests/fixtures/history/synthetic.json    historial: pasos de history_cases.py
  tests/fixtures/settings/synthetic.json   ajustes: pasos de settings_cases.py
  tests/fixtures/library/synthetic.json    biblioteca: pasos de library_cases.py
  tests/fixtures/sfx/synthetic.json        biblioteca de SFX en disco: pasos de sfx_cases.py
  tests/fixtures/collections/synthetic.json  colecciones de material: pasos de collections_cases.py
  tests/fixtures/stick/synthetic.json      personajes stick: pasos de stick_cases.py
  tests/fixtures/projects/synthetic.json   almacén de proyectos: pasos de projects_cases.py
  tests/fixtures/json/synthetic.json       JSON como Python: json_cases.py (repr, dumps y loads)
Con --real, en tests/fixtures/local/<lo mismo>/real.json: TUS proyectos, timelines,
clips, historiales, ajustes (con las claves redactadas), biblioteca, SFX y material de video-yt.
tests/fixtures/local/ son datos personales: está en .gitignore y nunca se sube.

Las operaciones pasan por el `timeline_store.apply_op` REAL de video-yt (con el disco
sustituido por memoria) y con uuid.uuid4 sustituido por la misma secuencia que
`gc::core::SequentialIdSource`, para que los ids generados coincidan.
"""
from __future__ import annotations

import argparse
import copy
import json
import shutil
import subprocess
import sys
import tempfile
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
# Las funciones de sfx.py antes de sustituirlas (las fixtures de SFX en disco usan las reales).
_REAL_SFX: dict = {}


def _text_width_estimate(font, bold, em, text, ls) -> float:
    """Ancho de texto estimado (0,55 em por carácter + espaciado): el que usa gc_core hasta
    que F4 mida con FreeType. Sustituye a font_metrics.text_width (Pillow)."""
    return len(text) * em * 0.55 + ls * len(text)


def _install_fakes() -> None:
    """Lo que en video-yt depende del disco o de las fuentes instaladas, inyectado: medida de
    texto, biblioteca de SFX y duración de cada sonido (ffprobe)."""
    from app import sfx, sound_design, text_ass
    _REAL_SFX.setdefault("search", sfx.search)
    _REAL_SFX.setdefault("resolve", sfx.resolve)
    _REAL_SFX.setdefault("probe_duration", sound_design.probe_duration)  # la usan las sondas de medios
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


# --- Ajustes (settings.json) -----------------------------------------------------------------

def _run_settings_steps(steps: list) -> list[dict]:
    """Ejecuta los pasos con settings.py sobre un settings.json temporal."""
    from app import settings as st
    tmp_dir = Path(tempfile.mkdtemp(prefix="gc_settings_"))
    tmp = tmp_dir / "settings.json"
    saved = st._FILE
    st._FILE = tmp
    out = []
    try:
        for step in steps:
            kind, args = step[0], step[1:]
            entry: dict = {"step": list(step)}
            try:
                if kind == "write":
                    content = args[0]
                    tmp.write_text(content if isinstance(content, str) else json.dumps(content, ensure_ascii=False),
                                   encoding="utf-8")
                    result = None
                elif kind == "load":
                    result = st.load()
                elif kind == "public":
                    result = st.public()
                    # api_keys_counts sale de recorrer un set: orden arbitrario. gc_core las ordena.
                    result["api_keys_counts"] = dict(sorted(result["api_keys_counts"].items()))
                elif kind == "save":
                    result = st.save(args[0])
                else:   # api_key, keys_for, add_key, set_key, remove_key
                    result = getattr(st, kind)(*args)
                entry["result"] = result
            except Exception as e:  # noqa: BLE001
                entry["error"] = type(e).__name__
            if tmp.exists():
                try:
                    entry["file"] = json.loads(tmp.read_text(encoding="utf-8"))
                except ValueError:
                    entry["file_text"] = tmp.read_text(encoding="utf-8")
            else:
                entry["file"] = None
            out.append(entry)
    finally:
        st._FILE = saved
        for f in tmp_dir.iterdir():
            f.unlink()
        tmp_dir.rmdir()
    return _pure(out)


def synthetic_settings_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import settings_cases as sc
    return {"steps": _run_settings_steps(sc.STEPS)}


def _redact_keys(data):
    """Tus ajustes con cada API key cambiada por «REDACTED-n»: la fixture no lleva secretos."""
    data = copy.deepcopy(data)
    n = 0

    def red(v):
        nonlocal n
        if isinstance(v, str) and v.strip():
            n += 1
            return f"REDACTED-{n}"
        return v
    if isinstance(data, dict):
        if isinstance(data.get("api_keys"), dict):
            data["api_keys"] = {k: red(v) for k, v in data["api_keys"].items()}
        if isinstance(data.get("api_keys_extra"), dict):
            data["api_keys_extra"] = {k: [red(x) for x in v] if isinstance(v, list) else red(v)
                                      for k, v in data["api_keys_extra"].items()}
        if "gemini_api_key" in data:
            data["gemini_api_key"] = red(data["gemini_api_key"])
    return data


def real_settings_fixtures(ctx: Ctx) -> dict:
    src = ctx.root / "backend" / "data" / "settings.json"
    if not src.exists():
        return {"steps": []}
    data = _redact_keys(json.loads(src.read_text(encoding="utf-8")))
    providers = sorted(set((data.get("api_keys") or {}) | (data.get("api_keys_extra") or {})))
    steps = [("write", data), ("load",), ("public",), *[("keys_for", p) for p in providers]]
    return {"steps": _run_settings_steps(steps)}


# --- Biblioteca (data/library) ----------------------------------------------------------------

def _run_library_steps(steps: list, projects_data: dict) -> list[dict]:
    """Ejecuta los pasos con library.py sobre un data/ temporal y estos proyectos."""
    import shutil
    from app import config, library, projects
    tmp = Path(tempfile.mkdtemp(prefix="gc_library_"))
    saved = (config.DATA_DIR, projects._load)
    config.DATA_DIR = tmp
    projects._load = lambda: copy.deepcopy(projects_data)
    lib_file = tmp / "library" / "library.json"
    out = []
    try:
        for step in steps:
            kind, args = step[0], step[1:]
            entry: dict = {"step": list(step)}
            try:
                if kind == "write":
                    lib_file.parent.mkdir(parents=True, exist_ok=True)
                    content = args[0]
                    lib_file.write_text(content if isinstance(content, str) else json.dumps(content, ensure_ascii=False),
                                        encoding="utf-8")
                    result = None
                elif kind == "files":
                    for rel in args[0]:
                        f = tmp / "library" / rel
                        f.parent.mkdir(parents=True, exist_ok=True)
                        f.write_bytes(b"x")
                    result = None
                elif kind == "dirs":
                    for rel in args[0]:
                        (tmp / "library" / rel).mkdir(parents=True, exist_ok=True)
                    result = None
                elif kind == "exists":
                    result = (tmp / "library" / args[0]).exists()
                elif kind == "load":
                    result = library._load()
                elif kind == "list":
                    result = library.list_library()
                elif kind == "update":
                    result = library.update_item(*args)
                elif kind == "semantic":
                    result = library.set_semantic(*args)
                elif kind == "unsave":
                    result = library.unsave(*args)
                entry["result"] = result
            except Exception as e:  # noqa: BLE001
                entry["error"] = type(e).__name__
                if type(e) in (library.LibraryInUseError, LookupError):  # los mensajes que ve el usuario
                    entry["message"] = str(e)
            if lib_file.exists():
                try:
                    entry["file"] = json.loads(lib_file.read_text(encoding="utf-8"))
                except ValueError:
                    entry["file_text"] = lib_file.read_text(encoding="utf-8")
            else:
                entry["file"] = None
            out.append(entry)
    finally:
        config.DATA_DIR, projects._load = saved
        shutil.rmtree(tmp, ignore_errors=True)
    return _pure(out)


def _run_save_steps(ctx: Ctx, steps: list) -> list[dict]:
    """«Guardar en la biblioteca»: library.save_from_project y projects.py REALES sobre un data/ y
    unas carpetas de clips temporales, con uuid4 y _now fijados. «{TMP}» es la carpeta (con «/»)
    en los argumentos y vuelve a serlo en lo que se guarda: el resultado, library.json,
    projects.json y lo que hay en disco (fuera de esos dos archivos)."""
    from app import config, library
    projects = ctx.projects
    tmp = Path(tempfile.mkdtemp(prefix="gc_save_"))
    posix = tmp.as_posix()
    saved = (projects._FILE, config.OUTPUT_DIR, config.DATA_DIR, uuid.uuid4, library._now)
    projects._FILE = tmp / "data" / "projects.json"
    config.DATA_DIR = tmp / "data"
    config.OUTPUT_DIR = tmp / "clips"
    uuid.uuid4 = _SequentialUuid()
    library._now = _FixedNow()
    lib_file = tmp / "data" / "library" / "library.json"

    def tmp_of(value):
        return json.loads(json.dumps(value, ensure_ascii=False).replace(posix, "{TMP}"))

    def read(path: Path):
        if not path.exists():
            return None
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except ValueError:
            return {"texto": path.read_text(encoding="utf-8")}

    out = []
    try:
        for step in steps:
            kind = step[0]
            args = json.loads(json.dumps(list(step[1:]), ensure_ascii=False).replace("{TMP}", posix)
                              .replace("{DRIVE}", posix[:2]))
            entry: dict = {"step": list(step)}
            try:
                result = None
                if kind in ("projects", "library"):
                    f = projects._FILE if kind == "projects" else lib_file
                    f.parent.mkdir(parents=True, exist_ok=True)
                    f.write_text(args[0] if isinstance(args[0], str) else json.dumps(args[0], ensure_ascii=False),
                                 encoding="utf-8")
                elif kind == "disk":
                    for rel in args[0]:
                        f = tmp / rel
                        f.parent.mkdir(parents=True, exist_ok=True)
                        f.write_text(rel, encoding="utf-8")
                elif kind == "save":
                    result = library.save_from_project(*args)
                else:
                    raise ValueError(f"paso desconocido: {kind}")
                entry["result"] = tmp_of(result)
            except Exception as e:  # noqa: BLE001
                entry["error"] = type(e).__name__
                if isinstance(e, LookupError):  # los mensajes que ve el usuario
                    entry["message"] = str(e)
            entry["library"] = tmp_of(read(lib_file))
            entry["projects"] = tmp_of(read(projects._FILE))
            skip = {lib_file, projects._FILE}
            entry["disk"] = [[p.relative_to(tmp).as_posix(), p.read_text(encoding="utf-8") if p.is_file() else None]
                             for p in sorted(tmp.rglob("*"), key=lambda p: p.relative_to(tmp).as_posix())
                             if p not in skip]
            out.append(entry)
    finally:
        projects._FILE, config.OUTPUT_DIR, config.DATA_DIR, uuid.uuid4, library._now = saved
        shutil.rmtree(tmp, ignore_errors=True)
    return _pure(out)


def synthetic_library_fixtures(ctx: Ctx) -> dict:
    from app import library
    sys.path.insert(0, str(HERE))
    import library_cases as lc
    dto = []
    for item, scope in lc.DTO:
        try:
            dto.append({"item": item, "scope": scope, "result": library.material_dto(item, scope)})
        except Exception as e:  # noqa: BLE001
            dto.append({"item": item, "scope": scope, "error": type(e).__name__})
    return _pure({"projects": lc.PROJECTS, "steps": _run_library_steps(lc.STEPS, lc.PROJECTS), "dto": dto,
                  "save": _run_save_steps(ctx, lc.SAVE_STEPS)})


def _real_save_steps(data: dict) -> list:
    """«Guardar en la biblioteca» sobre una copia de tus proyectos: el primer clip, audio e imagen
    de proyectos sin carpeta propia, con un archivo diminuto en su sitio (en la carpeta temporal;
    tus archivos no se tocan)."""
    steps: list = [("projects", data)]
    disk, saves, done = [], [], set()
    for p in data.get("projects", []):
        if p.get("folder"):
            continue
        for kind, key, ident, disk_kind in (("clip", "clips", "index", "video"), ("audio", "audios", "id", "audio"),
                                            ("image", "images", "id", "image")):
            items = p.get(key) or []
            if kind in done or not items or not isinstance(items[0].get("filename"), str):
                continue
            done.add(kind)
            disk.append(f"clips/{p['id']}/{disk_kind}/{items[0]['filename']}")
            saves.append(("save", p["id"], kind, str(items[0].get(ident))))
    return steps + [("disk", disk)] + saves if saves else []


def real_library_fixtures(ctx: Ctx, data: dict) -> dict:
    save = _run_save_steps(ctx, _real_save_steps(data))
    src = ctx.root / "backend" / "data" / "library" / "library.json"
    if not src.exists():
        return {"projects": data, "steps": [], "dto": [], "save": save}
    lib = json.loads(src.read_text(encoding="utf-8"))
    return _pure({"projects": data, "steps": _run_library_steps([("write", lib), ("load",), ("list",)], data),
                  "dto": [], "save": save})


# --- Biblioteca de SFX en disco (sfx.py) -------------------------------------------------------

def _fake_probe(path: Path) -> float:
    """probe_duration con la sonda falsa: el número que contiene el archivo (si no, 0.0)."""
    try:
        return round(float(path.read_text(encoding="utf-8").strip()), 3)
    except Exception:  # noqa: BLE001
        return 0.0


def _run_sfx_steps(steps: list, root: Path | None) -> list[dict]:
    """Ejecuta los pasos con el sfx.py REAL sobre `root` (o una carpeta temporal) y un
    settings.json temporal. «{TMP}» es la carpeta (con «/») en los ajustes y las rutas, y la
    carpeta de los resultados vuelve a ser «{TMP}»."""
    from app import settings as st
    from app import sfx
    tmp = Path(tempfile.mkdtemp(prefix="gc_sfx_"))
    base_dir = root or tmp
    posix, native = base_dir.as_posix(), str(base_dir)
    saved = (st._FILE, sfx._DEFAULT_BASE, sfx.search, sfx.resolve)
    st._FILE = tmp / "__settings__.json"
    sfx.search, sfx.resolve = _REAL_SFX["search"], _REAL_SFX["resolve"]

    def with_base(result: dict) -> dict:
        if result.get("base"):
            result["base"] = result["base"].replace(native, "{TMP}")
        return result

    out = []
    try:
        for step in steps:
            kind, args = step[0], step[1:]
            entry: dict = {"step": list(step)}
            try:
                result = None
                if kind == "files":
                    for rel in args[0]:
                        f = base_dir / rel
                        f.parent.mkdir(parents=True, exist_ok=True)
                        f.write_bytes(b"x")
                elif kind in ("file", "bytes"):
                    f = base_dir / args[0]
                    f.parent.mkdir(parents=True, exist_ok=True)
                    content = args[1] if isinstance(args[1], str) else json.dumps(args[1], ensure_ascii=False)
                    f.write_bytes(content.encode("latin-1" if kind == "bytes" else "utf-8"))
                elif kind == "dirs":
                    for rel in args[0]:
                        (base_dir / rel).mkdir(parents=True, exist_ok=True)
                elif kind == "remove":
                    p = base_dir / args[0]
                    p.rmdir() if p.is_dir() else p.unlink()
                elif kind == "default":
                    sfx._DEFAULT_BASE = base_dir / args[0]
                elif kind == "settings":
                    content = args[0] if isinstance(args[0], str) else json.dumps(args[0], ensure_ascii=False)
                    st._FILE.write_text(content.replace("{TMP}", posix), encoding="utf-8")
                elif kind == "index":
                    result = with_base(sfx.load_index())
                elif kind == "scan":
                    result = sfx._scan(sfx.get_base())
                elif kind == "search":
                    result = with_base(sfx.search(args[0], args[1], args[2]))
                elif kind == "resolve":
                    target = sfx.resolve(args[0].replace("{TMP}", posix))
                    result = None if target is None else target.relative_to(sfx.get_base().resolve()).as_posix()
                elif kind == "library":   # sound_design.propose: search("", 100000) + resolve + probe
                    lib = sfx.search("", limit=100000)
                    durations = {}
                    for it in lib["items"]:
                        p = sfx.resolve(it["id"])
                        if p:
                            durations[it["id"]] = _fake_probe(p)
                    result = {"items": lib["items"], "durations": dict(sorted(durations.items()))}
                elif kind == "favorite_ids":
                    result = sorted(sfx._favorite_sfx_ids())
                elif kind == "entry":
                    result = sfx._entry(args[0], args[1], category=args[2], uso=args[3], name=args[4])
                elif kind == "filter":
                    result = sfx.filter_sfx_items(args[0], q=args[1], category=args[2], favorite_ids=args[3])
                elif kind == "favcat":
                    result = sfx.with_favorites_category(args[0], args[1], args[2])
                elif kind == "audio":
                    result = Path(args[0]).suffix.lower() in sfx._AUDIO_EXT
                else:
                    raise ValueError(f"paso desconocido: {kind}")
                entry["result"] = result
            except Exception as e:  # noqa: BLE001
                entry["error"] = type(e).__name__
            out.append(entry)
    finally:
        st._FILE, sfx._DEFAULT_BASE, sfx.search, sfx.resolve = saved
        shutil.rmtree(tmp, ignore_errors=True)
    return _pure(out)


def synthetic_sfx_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import sfx_cases as xc
    return {"root": None, "steps": _run_sfx_steps(xc.STEPS, None)}


def real_sfx_fixtures(ctx: Ctx) -> dict:
    """Tu carpeta de SFX (la de fábrica de video-yt) con tus favoritos: índice, escaneo,
    búsquedas por categoría y la biblioteca de sound_design con la ruta de cada sonido."""
    from app import config
    root = Path(config.SFX_DIR)
    if not root.is_dir():
        return {"root": None, "steps": []}
    settings_file = ctx.root / "backend" / "data" / "settings.json"
    favs = []
    if settings_file.exists():
        favs = ((json.loads(settings_file.read_text(encoding="utf-8")).get("favorites") or {}).get("sfx")) or []
    steps = [("default", ""), ("settings", {"favorites": {"sfx": favs}}), ("index",), ("scan",),
             ("search", "", "", 100000), ("search", "", "favoritos", 300), ("library",), ("favorite_ids",)]
    try:
        cats = list((json.loads((root / "sfx_library.json").read_text(encoding="utf-8")).get("categorias") or {}))
    except Exception:  # noqa: BLE001
        cats = []
    steps += [("search", "", c, 300) for c in cats]
    steps += [("search", q, "", 300) for q in ("golpe", "whoosh", "puerta", "risa", "click", "boom", "pop", "zz")]
    return {"root": root.as_posix(), "steps": _run_sfx_steps(steps, root)}


# --- JSON como Python (json.loads, json.dumps, repr de float) -----------------------------------

def synthetic_json_fixtures() -> dict:
    sys.path.insert(0, str(HERE))
    import json_cases as jc
    loads = []
    for text in jc.LOADS:
        try:
            loads.append({"text": text, "result": json.loads(text)})
        except ValueError as e:
            loads.append({"text": text, "error": type(e).__name__})
    return {"floats": jc.FLOATS,
            "dumps": [[value, n, json.dumps(value, ensure_ascii=False, indent=n)] for value, n in jc.DUMPS],
            "loads": loads}


# --- Almacén de proyectos (projects.py) --------------------------------------------------------

# Los mensajes de los errores que ve el usuario (ValueError de projects.py): se comparan.
_PROJECTS_MESSAGES = {"El nombre no puede estar vacío.", "La carpeta no existe."}
# Los pasos que pueden escribir projects.json: tras ellos se guarda su texto.
_PROJECTS_WRITES = {"create", "create_group", "rename_group", "delete_group", "move", "rename", "duplicate",
                    "set_folder", "delete", "add_clips", "add_transcript", "add_audio", "add_image", "update",
                    "remove_material", "manifest", "timeline", "motion", "delete_motion", "scenes", "blueprint",
                    "retarget"}


class _FixedNow:
    """_now() de projects.py fijado: la n-ésima llamada da 2026-01-01T00:00:00 + n segundos."""

    def __init__(self):
        self.n = 0

    def __call__(self):
        self.n += 1
        return f"2026-01-01T{self.n // 3600:02d}:{self.n // 60 % 60:02d}:{self.n % 60:02d}+00:00"


def _run_projects_steps(ctx: Ctx, steps: list, record_file: bool = True) -> list[dict]:
    """Ejecuta los pasos con el projects.py REAL sobre un projects.json temporal (y los clips por
    defecto en {TMP}/clips), con uuid4 y _now fijados. «{TMP}» es la carpeta temporal (con «/»)
    en los argumentos, y vuelve a serlo en los resultados y en el texto de projects.json."""
    from app import config
    projects, schemas = ctx.projects, ctx.schemas
    tmp = Path(tempfile.mkdtemp(prefix="gc_prj_"))
    posix = tmp.as_posix()
    saved = (projects._FILE, config.OUTPUT_DIR, uuid.uuid4, projects._now)
    projects._FILE = tmp / "data" / "projects.json"
    config.OUTPUT_DIR = tmp / "clips"
    uuid.uuid4 = _SequentialUuid()
    projects._now = _FixedNow()

    def dump(value):
        if isinstance(value, (schemas.Project, schemas.ProjectGroup)):
            return value.model_dump()
        if isinstance(value, list):
            return [dump(v) for v in value]
        return value

    def tmp_of(value):
        return json.loads(json.dumps(value, ensure_ascii=False).replace(posix, "{TMP}"))

    calls = {
        "load": lambda: projects._load(),
        "list": lambda: projects.list_projects(),
        "groups": lambda: projects.list_groups(),
        "get": projects.get_project, "create": projects.create_project, "create_group": projects.create_group,
        "rename_group": projects.rename_group, "delete_group": projects.delete_group,
        "move": projects.move_project, "rename": projects.rename_project,
        "duplicate": projects.duplicate_project, "set_folder": projects.set_folder,
        "delete": projects.delete_project, "update": projects.update_material,
        "remove_material": projects.remove_material, "manifest": projects.apply_manifest,
        "timeline": projects.save_timeline, "motion": projects.save_motion_composition,
        "delete_motion": projects.delete_motion_composition, "scenes": projects.save_scene_directions,
        "blueprint": projects.save_visual_blueprint, "retarget": projects.retarget_timeline_asset,
        "add_clips": lambda pid, clips: projects.add_clips(pid, [schemas.ClipInfo(**c) for c in clips]),
        "add_transcript": lambda pid, t: projects.add_transcript(pid, schemas.Transcript(**t)),
        "add_audio": lambda pid, a: projects.add_audio(pid, schemas.AudioInfo(**a)),
        "add_image": lambda pid, i: projects.add_image(pid, schemas.ImageInfo(**i)),
    }
    out = []
    try:
        for step in steps:
            kind = step[0]
            args = json.loads(json.dumps(list(step[1:]), ensure_ascii=False).replace("{TMP}", posix))
            entry: dict = {"step": list(step)}
            try:
                result = None
                if kind == "file":
                    projects._FILE.parent.mkdir(parents=True, exist_ok=True)
                    content = args[0] if isinstance(args[0], str) else json.dumps(args[0], ensure_ascii=False)
                    projects._FILE.write_text(content, encoding="utf-8")
                    if not isinstance(args[0], str):  # el texto exacto: el test escribe el mismo
                        entry["text"] = json.dumps(step[1], ensure_ascii=False)
                elif kind == "remove":
                    projects._FILE.unlink(missing_ok=True)
                elif kind == "disk":
                    for rel in args[0]:
                        f = tmp / rel
                        f.parent.mkdir(parents=True, exist_ok=True)
                        f.write_text(rel, encoding="utf-8")
                elif kind == "data":
                    result = projects._FILE.read_bytes().decode("utf-8") if projects._FILE.exists() else None
                elif kind == "tree":
                    top = tmp / "clips"
                    result = [[p.relative_to(top).as_posix(), p.read_text(encoding="utf-8") if p.is_file() else None]
                              for p in sorted(top.rglob("*"), key=lambda p: p.relative_to(top).as_posix())]
                elif kind in calls:
                    result = dump(calls[kind](*args))
                else:
                    raise ValueError(f"paso desconocido: {kind}")
                entry["result"] = tmp_of(result)
            except Exception as e:  # noqa: BLE001
                entry["error"] = type(e).__name__
                if str(e) in _PROJECTS_MESSAGES:
                    entry["message"] = str(e)
            if record_file and kind in _PROJECTS_WRITES:
                f = projects._FILE
                entry["file"] = f.read_bytes().decode("utf-8").replace(posix, "{TMP}") if f.exists() else None
            out.append(entry)
    finally:
        projects._FILE, config.OUTPUT_DIR, uuid.uuid4, projects._now = saved
        shutil.rmtree(tmp, ignore_errors=True)
    return _pure(out)


def synthetic_projects_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import projects_cases as pc
    return {"steps": _run_projects_steps(ctx, pc.STEPS)}


def real_projects_fixtures(ctx: Ctx) -> dict:
    """Una copia de tu projects.json en una carpeta temporal: lista, grupos, cada proyecto, y
    renombrar, mover, duplicar (sin carpeta propia, así que no se copia nada tuyo), guardar una
    timeline, editar un material y borrar, con el archivo al final. Tu projects.json no se toca."""
    src = ctx.root / "backend" / "data" / "projects.json"
    if not src.exists():
        return {"steps": []}
    text = src.read_text(encoding="utf-8")
    data = json.loads(text)
    ids = [p["id"] for p in data.get("projects", [])]
    groups = [g["id"] for g in data.get("groups", [])]
    steps: list = [("file", text), ("list",), ("groups",)] + [("get", pid) for pid in ids]
    if ids:
        first = ids[0]
        steps += [("rename", first, "Renombrado"), ("move", first, groups[0] if groups else None),
                  ("move", first, None)]
        no_folder = [p["id"] for p in data["projects"] if not p.get("folder")]
        if no_folder:
            steps += [("duplicate", no_folder[0]), ("tree",)]
        timeline = next((p["timeline"] for p in data["projects"] if p.get("timeline")), None)
        if timeline:
            steps += [("timeline", first, timeline)]
        steps += [("update", first, "clips", "0", {"label": "x"}), ("delete", ids[-1]), ("data",)]
    return {"steps": _run_projects_steps(ctx, steps, record_file=False)}


# --- Colecciones de material (collections.py) --------------------------------------------------

# Los mensajes de los errores que ve el usuario (ValueError y LookupError de collections.py):
# el test de C++ los compara literalmente.
_COLLECTION_MESSAGES = {"Colección no válida.", "No hay carpeta raíz de material.",
                        "Ponle un nombre a la carpeta.", "Carpeta no encontrada."}


def _run_collections_steps(steps: list, root: Path | None) -> list[dict]:
    """Ejecuta los pasos con el collections.py REAL sobre `root` (o una carpeta temporal) y un
    settings.json temporal. «{TMP}» es la carpeta (con «/») en los ajustes y las rutas, y la
    carpeta de los resultados vuelve a ser «{TMP}»."""
    import io

    from app import collections as col
    from app import config
    from app import settings as st
    tmp = Path(tempfile.mkdtemp(prefix="gc_col_"))
    base_dir = root or tmp
    posix, native = base_dir.as_posix(), str(base_dir)
    saved = (st._FILE, config.ASSETS_DIR)
    st._FILE = tmp / "__settings__.json"

    def tmp_of(value):
        return value.replace(native, "{TMP}") if isinstance(value, str) else value

    def with_root(result: dict) -> dict:
        result["root"] = tmp_of(result.get("root"))
        for c in result.get("collections") or []:
            c["path"] = tmp_of(c["path"])
        return result

    out = []
    try:
        for step in steps:
            kind, args = step[0], step[1:]
            entry: dict = {"step": list(step)}
            try:
                result = None
                if kind == "files":
                    for rel in args[0]:
                        f = base_dir / rel
                        f.parent.mkdir(parents=True, exist_ok=True)
                        f.write_bytes(b"x")
                elif kind == "file":
                    f = base_dir / args[0]
                    f.parent.mkdir(parents=True, exist_ok=True)
                    content = args[1] if isinstance(args[1], str) else json.dumps(args[1], ensure_ascii=False)
                    f.write_bytes(content.encode("utf-8"))
                elif kind == "dirs":
                    for rel in args[0]:
                        (base_dir / rel).mkdir(parents=True, exist_ok=True)
                elif kind == "assets":
                    config.ASSETS_DIR = base_dir / args[0]
                elif kind == "settings":
                    content = args[0] if isinstance(args[0], str) else json.dumps(args[0], ensure_ascii=False)
                    st._FILE.write_text(content.replace("{TMP}", posix), encoding="utf-8")
                elif kind == "save":
                    st.save(args[0])
                elif kind == "meta":
                    data = st.load()
                    result = {"material_root": tmp_of(data.get("material_root")),
                              "collections_meta": data.get("collections_meta")}
                elif kind == "root":
                    r = col.get_root()
                    result = None if r is None else tmp_of(str(r))
                elif kind == "set_root":
                    r = col.set_root(args[0].replace("{TMP}", posix))
                    result = None if r is None else tmp_of(str(r))
                elif kind == "prefs":
                    result = col.set_prefs(args[0], enabled=args[1], favorite=args[2], label=args[3])
                elif kind == "safe_id":
                    result = col._safe_collection_id(args[0])
                elif kind == "kind":
                    result = col.kind_for(Path(args[0]))
                elif kind == "entry":
                    result = col._entry(args[0], Path(args[1]))
                elif kind == "scan":
                    result = col._scan_dir(col.get_root(), args[0])
                elif kind == "list":
                    result = with_root(col.list_collections())
                elif kind == "match":
                    result = col._match(args[0], args[1])
                elif kind == "search":
                    result = with_root(col.search(args[0], args[1], args[2], args[3]))
                elif kind == "create":
                    result = col.create_collection(args[0])
                elif kind == "add":
                    result = col.add_files(args[0], [(n, io.BytesIO(c.encode("utf-8"))) for n, c in args[1]])
                elif kind == "resolve":
                    target = col.resolve(args[0].replace("{TMP}", posix))
                    result = None if target is None else target.relative_to(col.get_root().resolve()).as_posix()
                elif kind == "resolve_items":   # el `filename` de cada medio, resuelto
                    result = {}
                    for it in col.search("", limit=100000)["items"]:
                        target = col.resolve(it["filename"])
                        result[it["filename"]] = (None if target is None else
                                                  target.relative_to(col.get_root().resolve()).as_posix())
                elif kind == "tree":
                    top = base_dir / args[0]
                    result = [[p.relative_to(top).as_posix(), p.read_text(encoding="utf-8") if p.is_file() else None]
                              for p in sorted(top.rglob("*"), key=lambda p: p.relative_to(top).as_posix())]
                else:
                    raise ValueError(f"paso desconocido: {kind}")
                entry["result"] = result
            except Exception as e:  # noqa: BLE001
                entry["error"] = type(e).__name__
                if str(e) in _COLLECTION_MESSAGES:
                    entry["message"] = str(e)
            out.append(entry)
    finally:
        st._FILE, config.ASSETS_DIR = saved
        shutil.rmtree(tmp, ignore_errors=True)
    return _pure(out)


def synthetic_collections_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import collections_cases as cc
    return {"root": None, "steps": _run_collections_steps(cc.STEPS, None)}


def real_collections_fixtures(ctx: Ctx) -> dict:
    """Tu carpeta de material (la de fábrica de video-yt o la de `material_root`) con tus
    preferencias: solo lectura (raíz, lista, búsquedas, recorrido de cada colección y la ruta
    de cada medio). Nada que escriba en ella."""
    from app import collections as col
    from app import config
    assets = Path(config.ASSETS_DIR)
    settings_file = ctx.root / "backend" / "data" / "settings.json"
    mine = json.loads(settings_file.read_text(encoding="utf-8")) if settings_file.exists() else {}
    prefs = {k: mine[k] for k in ("material_root", "collections_meta") if mine.get(k) is not None}
    custom = prefs.get("material_root")
    root = Path(custom) if isinstance(custom, str) and Path(custom).is_dir() else assets / "material"
    if not root.is_dir():
        return {"root": None, "steps": []}
    # La carpeta del test es la de `assets`; con material_root, la raíz de tus ajustes tal cual.
    steps = [("assets", ""), ("settings", prefs), ("root",), ("list",), ("search", "", "", "", 100000)]
    steps += [("search", "", k, "", 100000) for k in ("video", "image", "gif", "audio")]
    steps += [("search", q, "", "", 500) for q in ("scientist", "pointing", "1080p", "…", "zz")]
    try:
        cids = [c["id"] for c in col.list_collections()["collections"]] if root == assets / "material" else []
    except Exception:  # noqa: BLE001
        cids = []
    steps += [("scan", c) for c in cids] + [("resolve_items",)]
    return {"root": assets.as_posix(), "steps": _run_collections_steps(steps, assets)}


# --- Personajes stick (stick_library.py) ------------------------------------------------------

_STICK_MESSAGES = {"Stick no encontrado.", "Recurso no encontrado en el stick.", "Colección no válida.",
                   "No hay carpeta raíz de material. Elige una en la Biblioteca.", "Ponle un nombre al stick.",
                   "Solo se categorizan vídeos o imágenes."}


def _run_stick_steps(steps: list, root: Path | None) -> list[dict]:
    """Ejecuta los pasos con el stick_library.py REAL sobre `root` (o una carpeta temporal),
    con un settings.json temporal vacío: la raíz de las colecciones es <assets>/material."""
    import io

    from app import config
    from app import settings as st
    from app import stick_library as sl
    tmp = Path(tempfile.mkdtemp(prefix="gc_stick_"))
    base_dir = root or tmp
    saved = (st._FILE, config.ASSETS_DIR)
    st._FILE = tmp / "__settings__.json"
    out = []
    try:
        for step in steps:
            kind, args = step[0], step[1:]
            entry: dict = {"step": list(step)}
            try:
                result = None
                if kind == "files":
                    for rel in args[0]:
                        f = base_dir / rel
                        f.parent.mkdir(parents=True, exist_ok=True)
                        f.write_bytes(b"x")
                elif kind == "file":
                    f = base_dir / args[0]
                    f.parent.mkdir(parents=True, exist_ok=True)
                    content = args[1] if isinstance(args[1], str) else json.dumps(args[1], ensure_ascii=False)
                    f.write_bytes(content.encode("utf-8"))
                elif kind == "dirs":
                    for rel in args[0]:
                        (base_dir / rel).mkdir(parents=True, exist_ok=True)
                elif kind == "rm":
                    (base_dir / args[0]).unlink()
                elif kind == "assets":
                    config.ASSETS_DIR = base_dir / args[0]
                elif kind == "list":
                    result = sl.list_sticks()
                elif kind == "detail":
                    result = sl.stick_detail(args[0])
                elif kind == "create":
                    result = sl.create_stick(args[0], args[1], args[2])
                elif kind == "add":
                    result = sl.add_files(args[0], [(n, io.BytesIO(c.encode("utf-8"))) for n, c in args[1]], args[2])
                elif kind == "set":
                    result = sl.set_expression(args[0], args[1], args[2])
                elif kind == "norm":
                    result = sl._norm(args[0])
                elif kind == "slug":
                    result = sl._slug(args[0], fallback=args[1])
                elif kind == "pretty":
                    result = sl._pretty_title(args[0])
                elif kind == "title":
                    result = args[0].title()
                elif kind == "raw":
                    f = base_dir / args[0]
                    result = f.read_bytes().decode("utf-8") if f.is_file() else None
                elif kind == "tree":
                    top = base_dir / args[0]
                    result = [[p.relative_to(top).as_posix(), p.read_text(encoding="utf-8") if p.is_file() else None]
                              for p in sorted(top.rglob("*"), key=lambda p: p.relative_to(top).as_posix())]
                else:
                    raise ValueError(f"paso desconocido: {kind}")
                entry["result"] = result
            except Exception as e:  # noqa: BLE001
                entry["error"] = type(e).__name__
                if str(e) in _STICK_MESSAGES or str(e).startswith("Expresión desconocida: "):
                    entry["message"] = str(e)
            out.append(entry)
    finally:
        st._FILE, config.ASSETS_DIR = saved
        shutil.rmtree(tmp, ignore_errors=True)
    return _pure(out)


def synthetic_stick_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import stick_cases as sc
    return {"root": None, "steps": _run_stick_steps(sc.STEPS, None)}


def real_stick_fixtures(ctx: Ctx) -> dict:
    """Tus personajes stick (en la raíz de fábrica de video-yt): la lista y el detalle de cada
    uno. Solo lectura."""
    from app import config
    assets = Path(config.ASSETS_DIR)
    if not (assets / "material").is_dir():
        return {"root": None, "steps": []}
    listed = _run_stick_steps([("assets", ""), ("list",)], assets)[-1].get("result") or {}
    steps = [("assets", ""), ("list",)] + [("detail", s["id"]) for s in listed.get("sticks", [])]
    return {"root": assets.as_posix(), "steps": _run_stick_steps(steps, assets)}


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


# --- Envolvente de un sonido (clip_motion: lo que usa animate_clip con follow_audio_id) -------

def _motion_pairs(ctx: Ctx, name: str, clips: list, times_of) -> dict:
    """Todos los pares (visual, audio) de una timeline: overlap_window y audio_src_at en los
    instantes que da times_of(visual, audio, ventana)."""
    from app import clip_motion as cm
    cs = [ctx.schemas.TimelineClip.model_validate(c) for c in clips]
    pairs = []
    for vi, v in enumerate(cs):
        for ai, a in enumerate(cs):
            if vi == ai or a.kind not in ("audio", "video"):
                continue
            ov = cm.overlap_window(v, a)
            pairs.append({"v": vi, "a": ai, "window": None if ov is None else list(ov),
                          "src": [[t, cm.audio_src_at(a, t)] for t in times_of(v, a, ov)]})
    return _pure(_nan_safe({"name": name, "clips": [c.model_dump() for c in cs], "pairs": pairs}))


def _rms_case(samples: list[float], duration, rate: int) -> dict:
    """El sample_rms_envelope REAL con un ffmpeg falso que devuelve estas muestras."""
    import struct
    from types import SimpleNamespace
    from unittest import mock
    from app import clip_motion as cm
    raw = struct.pack(f"<{len(samples)}f", *samples)
    with mock.patch.object(shutil, "which", lambda _name: "ffmpeg"), \
            mock.patch.object(subprocess, "run", lambda *_a, **_k: SimpleNamespace(returncode=0, stdout=raw, stderr=b"")):
        out = cm.sample_rms_envelope("audio.wav", 0.0, duration, rate)
    return _nan_safe({"samples": samples, "duration": duration, "rate": rate, "out": [list(p) for p in out]})


def synthetic_motion_fixtures(ctx: Ctx) -> dict:
    from app import clip_motion as cm
    sys.path.insert(0, str(HERE))
    import motion_cases as mc

    def downsample(points, max_n):
        try:
            return {"out": [list(p) for p in cm.downsample_envelope(points, max_n)]}
        except Exception as e:  # noqa: BLE001
            return {"error": type(e).__name__}

    def times_of(_v, a, ov):
        return [a.start + d for d in mc.SRC_OFFSETS] + mc.SRC_ABS + ([] if ov is None else [ov[0]])

    return _pure(_nan_safe({
        "downsample": [{"points": [list(p) for p in pts], "max_n": m, **downsample(pts, m)}
                       for pts, m in mc.DOWNSAMPLE],
        "normalize": [{"points": [list(p) for p in pts], "out": [list(p) for p in cm.normalize_amps(pts)]}
                      for pts in mc.NORMALIZE],
        "timelines": [_motion_pairs(ctx, name, clips, times_of) for name, clips in mc.TIMELINES],
        "rms": [_rms_case(s, d, r) for s, d, r in mc.RMS],
    }))


def real_motion_fixtures(ctx: Ctx, data: dict, max_rms: int = 60) -> dict:
    """Tus timelines: todos los pares (visual, audio) y, por cada audio o vídeo con archivo que
    se solapa con otro clip, el RMS de esa ventana con el ffmpeg REAL (se graban sus muestras)."""
    import struct
    from app import clip_motion as cm
    from app import compose
    timelines, rms = [], []
    real_run = subprocess.run

    def times_of(v, a, ov):
        return [v.start, a.start + 0.25] + ([] if ov is None else [ov[0]])

    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        clips = [c.model_dump() for c in proj.timeline.clips]
        timelines.append(_motion_pairs(ctx, proj.id, clips, times_of))
        for a in proj.timeline.clips:
            if len(rms) >= max_rms or a.kind not in ("audio", "video"):
                continue
            path = compose._clip_path(proj, a)
            visual = next((v for v in proj.timeline.clips if v.id != a.id and cm.overlap_window(v, a)), None)
            if path is None or not path.exists() or visual is None:
                continue
            lo, hi = cm.overlap_window(visual, a)
            if hi - lo > 30:
                continue
            got: list[bytes] = []

            def recording_run(*args, **kwargs):
                r = real_run(*args, **kwargs)
                got.append(r.stdout or b"")
                return r

            src = cm.audio_src_at(a, lo)
            subprocess.run = recording_run
            try:
                out = cm.sample_rms_envelope(path, src, hi - lo)
            except ValueError as e:
                print(f"  (RMS de {proj.id} · {a.id} omitido: {e})")
                continue
            finally:
                subprocess.run = real_run
            raw = got[-1]
            n = len(raw) // 4
            samples = list(struct.unpack(f"<{n}f", raw[: n * 4]))
            rms.append(_nan_safe({"name": f"{proj.id} · {a.id}", "samples": samples, "duration": hi - lo,
                                  "rate": 20, "out": [list(q) for q in out]}))
    return _pure({"downsample": [], "normalize": [], "timelines": timelines, "rms": rms})


# --- Cómo lee el export el material (clip_kind.py) --------------------------------------------

def _export_input_case(ctx: Ctx, name: str, raw_clip: dict, fps_list, paths) -> dict:
    from app import clip_kind as ck
    c = ctx.schemas.TimelineClip.model_validate(raw_clip)
    return _pure(_nan_safe({
        "name": name, "clip": c.model_dump(), "still": ck.is_still_clip(c),
        "trim": [{"fps": f, "window": list(ck.ffmpeg_trim_window(c, f)), "filter": ck.ffmpeg_trim_filter(c, f)}
                 for f in fps_list],
        "input": [{"path": p, "fps": f, "args": ck.ffmpeg_input_args(c, p, f)} for p in paths for f in fps_list[:2]],
    }))


def synthetic_export_input_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import export_input_cases as ec
    return {"clips": [_export_input_case(ctx, c["id"], c, ec.FPS, ec.PATHS) for c in ec.CLIPS]}


def real_export_input_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus clips, con el fps de su timeline y la ruta que les da compose._clip_path."""
    from app import compose
    cases = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        fps = int(proj.timeline.fps)
        for c in proj.timeline.clips:
            path = compose._clip_path(proj, c)
            cases.append(_export_input_case(ctx, f"{proj.id} · {c.id}", c.model_dump(), [fps],
                                            [] if path is None else [str(path)]))
    return {"clips": cases}


# --- Cabeceras de imagen (images.py: probe_gif y _ext_from_magic) -----------------------------

def _gif_case(name: str, data: bytes) -> dict:
    from app import images
    return {"name": name, "hex": data.hex(), "gif": images.probe_gif(data), "ext": images._ext_from_magic(data)}


def synthetic_media_fixtures() -> dict:
    from app import images
    sys.path.insert(0, str(HERE))
    import media_cases as mc
    return {"gifs": [_gif_case(n, d) for n, d in mc.gif_cases()],
            "magic": [{"hex": m.hex(), "ext": images._ext_from_magic(m)} for m in mc.MAGIC],
            "probe": synthetic_probe_fixtures()}


PROBE_DIR = REPO / "tests" / "fixtures" / "media" / "files"


def _ensure_probe_media() -> None:
    """Crea los medios de media_cases.PROBE_MEDIA que falten (una vez: van a git-lfs)."""
    import media_cases as mc
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        sys.exit("Hace falta ffmpeg en el PATH para generar tests/fixtures/media/files")
    PROBE_DIR.mkdir(parents=True, exist_ok=True)
    for name, args in mc.PROBE_MEDIA + mc.PROBE_MEDIA_VFR + mc.PROBE_MEDIA_2S + mc.PROBE_MEDIA_ALPHA:
        out = PROBE_DIR / name
        if not out.exists():
            subprocess.run([ffmpeg, "-y", "-hide_banner", "-loglevel", "error", *args,
                            "-map_metadata", "-1", "-fflags", "+bitexact", str(out)], check=True)
    for name, data in mc.PROBE_RAW:
        if not (PROBE_DIR / name).exists():
            (PROBE_DIR / name).write_bytes(data)
    for name, src, pre, post in mc.PROBE_DERIVED:
        out = PROBE_DIR / name
        if not out.exists():
            subprocess.run([ffmpeg, "-y", "-hide_banner", "-loglevel", "error", *pre, "-i", str(PROBE_DIR / src), *post,
                            str(out)], check=True)
    name, src, n = mc.CORRUPT
    if not (PROBE_DIR / name).exists():
        data = bytearray((PROBE_DIR / src).read_bytes())
        mid = len(data) // 2
        data[mid:mid + n] = bytes(b ^ 0xFF for b in data[mid:mid + n])
        (PROBE_DIR / name).write_bytes(bytes(data))
    trunc = PROBE_DIR / "va_trunc.mp4"  # el MP4 con audio, cortado por la mitad
    if not trunc.exists():
        whole = (PROBE_DIR / "va_bt709.mp4").read_bytes()
        trunc.write_bytes(whole[: len(whole) // 2])


def _decode_chain(c, path: Path, fps: int) -> str:
    """La cadena que el export aplica a la entrada de un clip antes del encuadre (compose.py:
    `[k:v]{color_pre}{trim},setpts=PTS-STARTPTS`), con las funciones REALES."""
    from app import clip_kind, compose
    color_pre = f"{compose.ASSUME_BT709_FILTER}," if (not clip_kind.is_still_clip(c) and compose._color_untagged(path)) else ""
    return color_pre + clip_kind.ffmpeg_trim_filter(c, fps) + ",setpts=PTS-STARTPTS"


def synthetic_decode_fixtures(ctx: Ctx) -> list[dict]:
    """Fotogramas que da el ffmpeg del PATH con la entrada y la cadena del export de cada caso de
    media_cases.DECODE, en RGBA: dimensiones y un MD5 por fotograma."""
    import hashlib
    import media_cases as mc
    from app import clip_kind
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    cases = []
    for name, file, raw, fps in mc.DECODE:
        path = PROBE_DIR / file
        still = raw.get("asset_kind") == "images" or raw["kind"] in ("image", "shape")
        clip = {"id": "c", "track_id": "V1", "asset_kind": "clips", "asset_id": "c", "filename": file, "start": 0.0,
                "source_duration": 0.0 if still else _REAL_SFX["probe_duration"](path), **raw}
        c = ctx.schemas.TimelineClip.model_validate(clip)
        args = clip_kind.ffmpeg_input_args(c, path, fps)
        chain = _decode_chain(c, path, fps)
        dims = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                               "-of", "csv=p=0", str(path)], capture_output=True, text=True, check=True).stdout
        w, h = (int(x) for x in dims.strip().split(",")[:2])
        out = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", *args, "-vf", chain + ",format=rgba",
                              "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
                             capture_output=True, check=True).stdout
        size = w * h * 4
        assert len(out) % size == 0, (name, len(out), size)
        cases.append({"name": name, "file": file, "fps": fps, "clip": c.model_dump(),
                      "input_args": ["{PATH}" if a == str(path) else a for a in args], "chain": chain,
                      "width": w, "height": h,
                      "frames": [hashlib.md5(out[i:i + size]).hexdigest() for i in range(0, len(out), size)]})
    return cases


def _extract_frame_ss(path: Path, t: float) -> str:
    """El `-ss` que pone el frame_grab.extract_frame REAL: se intercepta su subprocess.run y se
    comprueba que el instante va antes de `-i` y con `-frames:v 1`."""
    from types import SimpleNamespace
    from unittest import mock
    from app import frame_grab
    seen: list[list[str]] = []

    def fake_run(cmd, *_a, **_k):
        seen.append(list(cmd))
        return SimpleNamespace(returncode=1, stdout="", stderr="interceptado")

    with mock.patch.object(subprocess, "run", fake_run):
        try:
            frame_grab.extract_frame(path, t)
        except ValueError:
            pass
    cmd = seen[0]
    i, ss = cmd.index("-ss"), cmd[cmd.index("-ss") + 1]
    assert cmd[i + 2] == "-i" and cmd[i + 3] == str(path) and cmd[cmd.index("-frames:v") + 1] == "1", cmd
    return ss


def synthetic_seek_fixtures() -> list[dict]:
    """El fotograma de un instante como frame_grab.extract_frame: los argumentos que arma
    (`-ss {max(0, t):.3f}` antes de `-i`, `-frames:v 1`) con el ffmpeg del PATH, sin su escalado
    ni el JPEG, en RGBA. MD5 del fotograma o null si no sale ninguno."""
    import hashlib
    import media_cases as mc
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    cases = []
    for file, times in mc.SEEK:
        path = PROBE_DIR / file
        dims = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                               "-of", "csv=p=0", str(path)], capture_output=True, text=True, check=True).stdout
        w, h = (int(x) for x in dims.strip().split(",")[:2])
        for t in times:
            ss = _extract_frame_ss(path, t)
            out = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-ss", ss, "-i", str(path),
                                  "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
                                 capture_output=True, check=True).stdout
            assert len(out) in (0, w * h * 4), (file, t, len(out))
            cases.append(_nan_safe({"file": file, "t": t, "ss": ss, "width": w, "height": h,
                                    "frame": hashlib.md5(out).hexdigest() if out else None}))
    return cases


RENDER_DIR = REPO / "tests" / "fixtures" / "render"


def synthetic_render_fixtures(ctx: Ctx) -> list[dict]:
    """Fotogramas del COMPUESTO real del export (compose.render_frame: la misma tubería que el
    export, pero con un solo PNG) para media_cases.RENDER. Solo se sustituye dónde está el
    material (_clip_path). Los PNG van a tests/fixtures/render (git-lfs)."""
    import media_cases as mc
    from types import SimpleNamespace
    from app import compose
    ffmpeg = shutil.which("ffmpeg")
    files = RENDER_DIR / "files"
    files.mkdir(parents=True, exist_ok=True)
    for name, args in mc.RENDER_MEDIA + mc.RENDER_VIDEO_MEDIA:
        if not (files / name).exists():
            subprocess.run([ffmpeg, "-y", "-hide_banner", "-loglevel", "error", *args, "-map_metadata", "-1",
                            "-fflags", "+bitexact", str(files / name)], check=True)
    real_clip_path = compose._clip_path
    compose._clip_path = lambda project, clip, shape_files=None: files / clip.filename
    try:
        cases = []
        for i, (name, file, fields, times) in enumerate(mc.RENDER):
            raw = {"version": 2, "width": 360, "height": 640, "fps": 30,
                   "tracks": [{"id": "V1", "kind": "video", "name": "V1"}],
                   "clips": [{"id": "c1", "track_id": "V1", "kind": "image", "asset_kind": "images", "asset_id": "a1",
                              "filename": file, "start": 0.0, "in_point": 0.0, "out_point": 1.0,
                              "source_duration": 0.0, "layout": "overlay", **fields}]}
            timeline = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
            for t in times:
                png = f"r{i:02d}_{int(round(t * 1000)):05d}.png"
                compose.render_frame(SimpleNamespace(id="render-fixture"), timeline, RENDER_DIR / png, t)
                cases.append({"name": name, "t": t, "file": file, "frame": png,
                              "timeline": _pure(timeline.model_dump())})
        for i, (name, fps, tracks, clips, times) in enumerate(mc.RENDER_SCENES):
            raw_clips = []
            for j, (file, track, fields) in enumerate(clips):
                still = fields["kind"] == "image"
                raw_clips.append({"id": f"c{j + 1}", "track_id": track, "asset_id": f"a{j + 1}", "filename": file,
                                  "source_duration": 0.0 if still else _REAL_SFX["probe_duration"](files / file),
                                  **fields})
            raw = {"version": 2, "width": 360, "height": 640, "fps": fps,
                   "tracks": [{"id": tid, "kind": "video", "name": tid} for tid in tracks], "clips": raw_clips}
            timeline = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
            for t in times:
                png = f"s{i:02d}_{int(round(t * 1000)):05d}.png"
                compose.render_frame(SimpleNamespace(id="render-fixture"), timeline, RENDER_DIR / png, t)
                cases.append({"name": name, "t": t, "frame": png, "timeline": _pure(timeline.model_dump())})
        return cases
    finally:
        compose._clip_path = real_clip_path


def synthetic_text_render_fixtures(ctx: Ctx) -> list[dict]:
    """Fotogramas del export real (compose.render_frame, con el .ass de text_ass y libass) para
    media_cases.RENDER_TEXT. El reparto en líneas usa la medida REAL de Pillow
    (font_metrics.text_width), no la estimación de _install_fakes. Los PNG: x<escena>_<ms>.png."""
    import media_cases as mc
    from types import SimpleNamespace
    from app import compose, font_metrics, text_ass
    files = RENDER_DIR / "files"
    real_clip_path, fake_width = compose._clip_path, text_ass.text_width
    compose._clip_path = lambda project, clip, shape_files=None: files / clip.filename
    text_ass.text_width = font_metrics.text_width
    try:
        cases = []
        for i, (name, background, texts, times) in enumerate(mc.RENDER_TEXT):
            tracks = [{"id": "T1", "kind": "text", "name": "T1"}]
            clips = []
            if background:
                tracks.insert(0, {"id": "V1", "kind": "video", "name": "V1"})  # debajo del texto
                still = not background.endswith(".mp4")
                clips.append({"id": "bg", "track_id": "V1", "asset_id": "bg", "filename": background,
                              "source_duration": 0.0 if still else _REAL_SFX["probe_duration"](files / background),
                              "start": 0.0, "in_point": 0.0, "out_point": 2.0, **(mc._I if still else mc._V),
                              "transform": {"x": 0.5, "y": 0.5, "scale": 1.0}})
            for j, (text, style, fields) in enumerate(texts):
                clips.append({"id": f"t{j + 1}", "track_id": "T1", "kind": "text", "asset_kind": "text",
                              "asset_id": f"t{j + 1}", "filename": "", "text": text, "style": style,
                              "start": 0.0, "in_point": 0.0, "out_point": 2.0, **fields})
            raw = {"version": 2, "width": 540, "height": 960, "fps": 30, "tracks": tracks, "clips": clips}
            timeline = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
            for t in times:
                png = f"x{i:02d}_{int(round(t * 1000)):05d}.png"
                compose.render_frame(SimpleNamespace(id="render-fixture"), timeline, RENDER_DIR / png, t)
                (RENDER_DIR / png).with_suffix(".ass").unlink(missing_ok=True)  # el .ass que deja render_frame
                case = {"name": name, "t": t, "frame": png, "timeline": _pure(timeline.model_dump())}
                if name in mc.RENDER_TEXT_DIVERGENCES:
                    case["divergence"] = mc.RENDER_TEXT_DIVERGENCES[name]
                if name in mc.RENDER_TEXT_PENDING:
                    case["pending"] = True
                cases.append(case)
        return cases
    finally:
        compose._clip_path, text_ass.text_width = real_clip_path, fake_width


def synthetic_chroma_fixtures() -> list[dict]:
    """Las cadenas de FFmpeg del croma con las funciones REALES de clip_bg.py y la parte del
    croma de compose._bg_source_chain (sin matte), para media_cases.CHROMA."""
    import media_cases as mc
    from app import clip_bg
    cases = []
    for raw in mc.CHROMA:
        chroma = clip_bg.normalize_chroma(raw)
        by_height = []
        for h in mc.CHROMA_HEIGHTS:
            amorph = clip_bg.chroma_alpha_ffmpeg(chroma, h if h > 0 else 512)
            by_height.append({"height": h, "alpha": amorph})
        cases.append({"raw": raw, "chroma": chroma, "filters": clip_bg.chroma_filters(chroma), "heights": by_height})
    return cases


def _probe(path: Path) -> dict:
    """Las tres sondas REALES de video-yt (ffprobe del PATH; probe_duration, la original y no
    la falsa de _install_fakes)."""
    from app import compose
    return {"duration": _REAL_SFX["probe_duration"](path), "color_untagged": compose._color_untagged(path),
            "has_audio": compose._has_audio(path)}


def synthetic_probe_fixtures() -> list[dict]:
    sys.path.insert(0, str(HERE))
    import media_cases as mc
    _ensure_probe_media()
    names = sorted(p.name for p in PROBE_DIR.iterdir()) + mc.PROBE_MISSING
    return [{"file": n, **_probe(PROBE_DIR / n)} for n in names]


def real_probe_fixtures(ctx: Ctx, data: dict, limit: int = 120) -> list[dict]:
    """Una muestra de tus medios: los archivos de tus proyectos (clips, audios e imágenes) y de
    tu carpeta de SFX, con la ruta absoluta (el test los sondea en su sitio)."""
    from app import compose
    paths: list[Path] = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        for c in proj.timeline.clips:
            if c.kind in ("video", "audio", "image"):
                path = compose._clip_path(proj, c)
                if path is not None and path.exists() and path not in paths:
                    paths.append(path)
    try:
        from app import sfx
        folder = sfx.get_base()
        if folder is not None:
            paths += [p for p in sorted(folder.rglob("*")) if p.suffix.lower() in (".mp3", ".wav", ".m4a", ".ogg")][:40]
    except Exception:  # noqa: BLE001
        pass
    step = max(1, len(paths) // limit)
    return [{"file": str(p), **_probe(p)} for p in paths[::step][:limit]]


def real_media_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus imágenes (biblioteca, proyectos y la carpeta de material): firma de todas y, de los
    GIF, sus metadatos. Solo se guardan las cabeceras: los primeros 64 KB."""
    roots = [ctx.root / "backend" / "data", ctx.root / "backend" / "clips"]
    try:
        from app import collections as col
        if col.get_root() is not None:
            roots.append(Path(col.get_root()))
    except Exception:  # noqa: BLE001
        pass
    seen, cases = set(), []
    for root in roots:
        if not root.is_dir():
            continue
        for p in sorted(root.rglob("*")):
            if not p.is_file() or p.suffix.lower() not in (".gif", ".png", ".jpg", ".jpeg", ".webp"):
                continue
            key = p.resolve()
            if key in seen:
                continue
            seen.add(key)
            blob = p.read_bytes()
            if p.suffix.lower() != ".gif":
                blob = blob[:64]  # solo la firma
            elif len(blob) > 64 * 1024:
                continue  # GIF grandes: no se suben a las fixtures locales
            cases.append(_gif_case(str(p.relative_to(root)), blob))
    return {"gifs": cases, "magic": [], "probe": real_probe_fixtures(ctx, data)}


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


# --- Evaluación por clip (fx_anims, clip_fx, clip_blend, clip_adjust) ------------------------

def _nan_safe(value):
    """NaN e infinito como texto ("nan", "inf", "-inf"): JSON no los admite."""
    if isinstance(value, float) and value - value != 0:
        return repr(value)
    if isinstance(value, dict):
        return {k: _nan_safe(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_nan_safe(v) for v in value]
    return value


def _matrix(m) -> list | None:
    return None if m is None else [[float(v) for v in row] for row in m]


def eval_case(ctx: Ctx, name: str, raw_clip: dict, times: list[float]) -> dict:
    from app import clip_adjust as ca
    from app import clip_anim
    from app import clip_blend
    from app import clip_fx
    from app import fx_anims as fa
    c = ctx.schemas.TimelineClip.model_validate(raw_clip)
    dur = ctx.clip_speed.clip_timeline_duration(c)
    samples = [{"t": t, "delta": fa.animation_delta(c, t, dur), "pose": clip_anim.render_pose(c, t),
                "anim_opacity": clip_anim.anim_opacity(c, t), "fx": clip_fx.clip_fx_at(c, t, dur)}
               for t in times]
    return _pure(_nan_safe({
        "name": name, "clip": c.model_dump(), "duration": dur,
        "animations": [{"slot": a["slot"], "id": a["anim"]["id"], "dur": a["dur"]}
                       for a in fa.clip_animations(c, dur)],
        "has_animations": fa.has_animations(c), "sample_times": fa.animation_sample_times(c, dur),
        "blend": clip_blend.clip_blend(c), "color_matrix": _matrix(ca.color_matrix(c)),
        "has_color_adjust": ca.has_color_adjust(c),
        "passes": [{"ghost": g, "clip": p.model_dump()} for p, g in ca.adjust_passes(c)],
        "samples": samples,
    }))


def filter_case(ctx: Ctx, name: str, raw_clip: dict) -> dict:
    """Filtros de color de un clip (clip_filters.py): la pila, su matriz, la de capa de ajuste
    y el colorchannelmixer del export."""
    from app import clip_filters as cf
    c = ctx.schemas.TimelineClip.model_validate(raw_clip)
    return _pure(_nan_safe({
        "name": name, "clip": c.model_dump(), "filters": cf.clip_filters(c),
        "stack": _matrix(cf.stack_matrix(cf.clip_filters(c))), "adjustment": _matrix(cf.adjustment_matrix(c)),
        "ffmpeg": cf.filters_ffmpeg(c),
    }))


def synthetic_eval_fixtures(ctx: Ctx) -> dict:
    from app import clip_adjust as ca
    from app import clip_blend
    from app import clip_filters as cf
    from app import clip_fx
    from app import fx_anims as fa
    sys.path.insert(0, str(HERE))
    import eval_cases as ec
    return _pure(_nan_safe({
        "filters": [filter_case(ctx, c["name"], c["clip"]) for c in ec.FILTER_CLIPS],
        "filter_matrix": [{"id": fid, "k": float(k), "matrix": _matrix(cf.filter_matrix(fid, float(k))),
                           "ffmpeg": cf.matrix_ffmpeg(cf.filter_matrix(fid, float(k)))}
                          for fid in ec.FILTER_MATRIX_IDS for k in ec.FILTER_K],
        "filter_catalog": [{"id": f["id"], "label": f["label"], "group": f["group"], "matrix": _matrix(f["matrix"])}
                           for f in cf.FILTERS],
        "clips": [eval_case(ctx, c["name"], c["clip"], ec.TIMES) for c in ec.CLIPS],
        # x siempre es real en fx_anims (una fracción): con un int, las curvas devolverían int.
        "ease": [{"name": n, "x": float(x), "y": fa.EASES.get(n, fa.EASES["linear"])(float(x))}
                 for n in ec.EASE_NAMES for x in ec.EASE_X],
        "tracks": [{"keys": k, "u": u, "v": fa.eval_track(k, u)} for k in ec.TRACKS for u in ec.TRACK_U],
        "fx_windows": [{"duration": d, "windows": list(clip_fx.fx_windows(d))} for d in ec.FX_WINDOWS],
        "effects": [{"effects": e, "color_matrix": _matrix(ca.color_matrix(e if isinstance(e, dict) else {})),
                     "has_color_adjust": ca.has_color_adjust(e if isinstance(e, dict) else {}),
                     "values": {k: ca.adjust_value(e if isinstance(e, dict) else {}, k) for k in ca.ADJUST_KEYS},
                     "strip_color": ca.strip_color(e if isinstance(e, dict) else None)}
                    for e in ec.EFFECTS],
        "blend": [{"mode": m, "normalized": clip_blend.normalize_blend(m)} for m in ec.BLENDS],
    }))


def real_eval_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus clips que se dibujan (todo menos el audio), en el inicio, las ventanas de
    aparición y salida, la mitad y el final."""
    cases, filters = [], []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        for c in proj.timeline.clips:
            if c.kind == "audio":
                continue
            dur = ctx.clip_speed.clip_timeline_duration(c)
            times = sorted({0.0, 0.1, 0.2, 0.4, dur / 2, max(0.0, dur - 0.4), max(0.0, dur - 0.1), dur})
            cases.append(eval_case(ctx, f"{proj.id} · {c.id}", c.model_dump(), times))
            filters.append(filter_case(ctx, f"{proj.id} · {c.id}", c.model_dump()))
    return {"clips": cases, "ease": [], "tracks": [], "fx_windows": [], "effects": [], "blend": [],
            "filters": filters, "filter_matrix": [], "filter_catalog": []}


# --- Capas de un instante (layersAt del preview con Node; pila de track_stack.py) ------------

def _node_layers(ctx: Ctx, cases: list[dict]) -> list[dict]:
    """Ejecuta el layersAt/videosAt/stackLayers REAL de video-yt (tools/fixtures/layers_js.mjs)."""
    node = shutil.which("node")
    if node is None:
        sys.exit("Falta Node (≥ 22) en el PATH: las fixtures de capas ejecutan el JS de video-yt.")
    r = subprocess.run([node, str(HERE / "layers_js.mjs"), str(ctx.root / "frontend" / "src")],
                       input=json.dumps(cases), capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        sys.exit(f"layers_js.mjs falló:\n{r.stderr}")
    return json.loads(r.stdout)


def _js_twin(c: dict, ctx: Ctx) -> dict:
    """El clip con in_point 0, out_point = su duración de Python y velocidad 1: así el clipEnd
    de editorModel.js (sin redondeo y con las figuras a velocidad 1) da, bit a bit, el
    start + clip_timeline_duration de Python, que es el que usa el C++. layersAt no mira nada
    más del clip que dependa de eso."""
    return {**c, "in_point": 0.0, "out_point": ctx.clip_speed.clip_timeline_duration(c), "speed": 1.0,
            "reverse": False}


def layers_fixtures(ctx: Ctx, cases: list[dict]) -> list[dict]:
    """cases: [{name, timeline (dict), heads}] → casos con la pila, lo enterrado y las capas del
    preview en cada instante."""
    from app import track_stack as ts
    out, node_in = [], []
    for case in cases:
        tl = ctx.schemas.Timeline.model_validate(case["timeline"])
        dump = _pure(tl.model_dump())
        out.append({"name": case["name"], "timeline": dump, "heads": case["heads"],
                    "stack_py": [[k, v] for k, v in ts.stack_layers(tl.tracks).items()],
                    "buried": sorted(ts.buried_text_track_ids(tl.tracks))})
        node_in.append({"tracks": dump["tracks"], "clips": [_js_twin(c, ctx) for c in dump["clips"]],
                        "heads": case["heads"]})
    for case, js in zip(out, _node_layers(ctx, node_in)):
        case.update({"stack_js": js["stack"], "layers": js["layers"], "videos": js["videos"]})
    return out


def synthetic_layers_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import layers_cases as lc
    return {"cases": layers_fixtures(ctx, [{"name": c["name"], "timeline": {"tracks": c["tracks"],
                                                                         "clips": c["clips"]},
                                            "heads": c["heads"]} for c in lc.CASES])}


def real_layers_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus timelines, en los bordes de cada clip (±0,01–0,03 s), su mitad y su final."""
    cases = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        heads = set()
        for c in proj.timeline.clips:
            s, e = c.start, c.start + ctx.clip_speed.clip_timeline_duration(c)
            heads |= {s - 0.03, s - 0.02, s - 0.01, s, s + 0.01, (s + e) / 2, e - 0.01, e, e + 0.01}
        cases.append({"name": f"{proj.id} · {proj.name}", "timeline": proj.timeline.model_dump(),
                      "heads": sorted(heads)})
    return {"cases": layers_fixtures(ctx, cases)}


# --- Fotograma de un instante (drawComposite del preview con Node) ------------------------------

def _node(ctx: Ctx, script: str, cases: list[dict]) -> list[dict]:
    node = shutil.which("node")
    if node is None:
        sys.exit("Falta Node (≥ 22) en el PATH: estas fixtures ejecutan el JS de video-yt.")
    r = subprocess.run([node, str(HERE / script), str(ctx.root / "frontend" / "src")],
                       input=json.dumps(cases), capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        sys.exit(f"{script} falló:\n{r.stderr}")
    return json.loads(r.stdout)


def frame_fixtures(ctx: Ctx, cases: list[dict]) -> list[dict]:
    """cases: [{name, timeline (dict), out: [w, h], media: {clip_id: [w, h]}, heads}] → casos con lo
    que dibuja drawComposite en cada instante. `js_dur` lista los clips cuyo clipDur de JS no es la
    duración de Python Y en los que eso cambia el dibujo: con salida (la ventana se mide desde el
    final), con aparición en un clip de menos de 0,8 s (la ventana es la mitad del clip) o con
    animaciones de catálogo."""
    out, node_in = [], []
    for case in cases:
        tl = ctx.schemas.Timeline.model_validate(case["timeline"])
        dump = _pure(tl.model_dump())
        out.append({"name": case["name"], "timeline": dump, "out": case["out"], "media": case["media"],
                    "heads": case["heads"]})
        node_in.append({"tracks": dump["tracks"], "clips": dump["clips"], "out": case["out"],
                        "media": case["media"], "heads": case["heads"]})
    for case, js in zip(out, _node(ctx, "frame_js.mjs", node_in)):
        def sensitive(c: dict, js_dur: float) -> bool:
            d = min(js_dur, ctx.clip_speed.clip_timeline_duration(c))
            return ((c.get("exit") or "none") != "none" or ((c.get("appear") or "none") != "none" and d < 0.8)
                    or bool(c.get("animations")))
        clips = {c["id"]: c for c in case["timeline"]["clips"]}
        case["js_dur"] = sorted(cid for cid, d in js["durs"].items()
                                if d != ctx.clip_speed.clip_timeline_duration(clips[cid]) and sensitive(clips[cid], d))
        case["frames"] = js["frames"]
    return out


def synthetic_frame_fixtures(ctx: Ctx) -> dict:
    sys.path.insert(0, str(HERE))
    import frame_cases as fc
    cases = frame_fixtures(ctx, [{"name": c["name"], "timeline": {"tracks": c["tracks"], "clips": c["clips"]},
                                  "out": c["out"], "media": c["media"], "heads": c["heads"]} for c in fc.CASES])
    bad = [c["name"] for c in cases if c["js_dur"]]
    if bad:
        sys.exit(f"frame_cases.py: clipDur de JS ≠ Python en {bad}; usa duraciones en ms")
    return {"cases": cases}


def real_frame_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus timelines: cerca del inicio (ventana de aparición), en la mitad y cerca del final de cada
    clip de vídeo o imagen, lejos (≥ 0,03 s) de los bordes de cualquier clip que se dibuja: ahí el
    margen de 0,02 s y el clipEnd de JS deciden otra cosa."""
    cases = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        tl = proj.timeline
        dims = {a.id: [a.width, a.height] for a in (*proj.clips, *proj.images)
                if getattr(a, "width", None) and getattr(a, "height", None)}
        media = {c.id: dims.get(c.asset_id, [1920, 1080]) for c in tl.clips if c.kind in ("video", "image")}
        edges = []
        for c in tl.clips:
            if c.kind != "audio":
                edges += [c.start, c.start + ctx.clip_speed.clip_timeline_duration(c)]
        heads = set()
        for c in tl.clips:
            if c.kind not in ("video", "image"):
                continue
            s, d = c.start, ctx.clip_speed.clip_timeline_duration(c)
            heads |= {s + min(0.1, d / 4), s + min(0.25, d / 3), s + d / 2, s + d - min(0.25, d / 3)}
        heads = sorted(h for h in heads if all(abs(h - e) >= 0.03 for e in edges))
        cases.append({"name": f"{proj.id} · {proj.name}", "timeline": tl.model_dump(), "out": [tl.width, tl.height],
                      "media": media, "heads": heads})
    return {"cases": frame_fixtures(ctx, cases)}


def _fits(clip_kind: str, track_kind: str) -> bool:
    family = {"video": "video", "image": "video", "shape": "video", "motion": "video", "adjustment": "video",
              "audio": "audio", "text": "text"}
    return family.get(clip_kind) == track_kind


# --- Texto (text_ass, font_metrics) -----------------------------------------------------------

def _safe(fn, *args) -> dict:
    """{"ok": valor} o {"error": tipo}, con NaN e infinito como texto."""
    try:
        return {"ok": _nan_safe(fn(*args))}
    except Exception as e:  # noqa: BLE001 — basta con saber que Python falla
        return {"error": type(e).__name__}


def _fake_size_factor(name, bold=True) -> float:
    """ass_size_factor falso para las cajas de Fondo: no depende de las fuentes instaladas."""
    return 1.2 + 0.01 * len(name or "") + (0.05 if bold else 0.0)


def _text_style_case(st: dict) -> dict:
    """Los cálculos de text_ass.py sobre un estilo efectivo, en 1080×1920 (y _xy también en 360×640).
    Las cajas de Fondo usan _fake_size_factor (y el ancho de texto estimado de _install_fakes)."""
    import text_cases as tc
    from app import text_ass as ta
    font_px = ta._font_px(st, 1920)
    real_factor = ta.ass_size_factor
    ta.ass_size_factor = _fake_size_factor
    try:
        rows = ta._rows(st, 1080, 1920, tc.ROWS_TEXT)
        geometry = {
            "rows": rows,
            "box_rects": _safe(lambda: [list(r) for r in ta.box_rects(st, 1080, 1920, rows)]),
            "curve_glyphs": _safe(lambda: [list(g) for g in ta.curve_glyphs(st, 1080, 1920, rows)]),
        }
    finally:
        ta.ass_size_factor = real_factor
    return _pure({
        **geometry,
        "style": st,
        "fx": _safe(lambda: sorted(ta.word_fx_set(st))),
        "opacity": _safe(ta.style_opacity, st),
        "word_opacity": _safe(lambda: [ta.word_opacity(st, True), ta.word_opacity(st, False)]),
        "alpha": _safe(lambda: [ta.alpha_hex(ta.word_opacity(st, a)) for a in (True, False)]),
        "stretch": _safe(lambda: list(ta.stretch_of(st))),
        "box_color": _safe(ta._box_color, st),
        "box_drawn": _safe(ta.box_drawn, st),
        "xy": _safe(lambda: [list(ta._xy(st, 1080, 1920)), list(ta._xy(st, 360, 640))]),
        "font_px": _safe(ta._font_px, st, 1920),
        "letter_spacing": _safe(ta._letter_spacing_px, st, font_px),
        "line_height": _safe(ta._line_height, st),
        "shadow": _safe(ta.text_shadow, st, font_px),
        "curve": _safe(ta.curve_of, st),
    })


def _font_layout_case(layout: dict, root: Path) -> dict:
    """resolve_font_path y ass_size_factor REALES con las carpetas de fuentes en `root`: cada
    archivo de la disposición es un TTF mínimo con su propio alto de línea."""
    import text_cases as tc
    from app import font_metrics as fm
    files = {}
    for sub in ("system", "bundled"):
        (root / sub).mkdir(parents=True)
        for i, name in enumerate(layout[sub]):
            asc = 1000 + 37 * i + (500 if sub == "bundled" else 0)
            data = tc.font_bytes({b"head": tc._head(1000), b"OS/2": tc._os2(asc, 211)})
            (root / sub / name).write_bytes(data)
            files[f"{sub}/{name}"] = data.hex()
    saved = fm.FONT_DIR, fm.BUNDLED_FONT_DIR
    fm.FONT_DIR, fm.BUNDLED_FONT_DIR = root / "system", root / "bundled"
    fm._line_height_em.cache_clear()
    try:
        out = []
        for name in tc.FONT_NAMES:
            for bold in (True, False):
                p = fm.resolve_font_path(name, bold)
                out.append({"name": name, "bold": bold, "path": p.relative_to(root).as_posix(),
                            "factor": fm.ass_size_factor(name, bold)})
        return {"files": files, "fonts": out}
    finally:
        fm.FONT_DIR, fm.BUNDLED_FONT_DIR = saved
        fm._line_height_em.cache_clear()


def _caption_events(c, st: dict) -> list[dict]:
    """Los eventos REALES de caption_dialogues (1080×1920) de un texto quieto: inicio y fin como
    los escribe el .ass y, en vez del cuerpo, «F»/«N» (primer evento o no), las líneas y «A»/«I»
    por palabra. Para eso se sustituyen las piezas que arman el cuerpo, no las que deciden."""
    from app import text_ass as ta
    saved = ta._row_bodies, ta._curve_bodies, ta._layout_prefix, ta._glyph_prefix, ta._active_override, ta._idle_override

    def bodies(rows, override_for, prefix_for):
        n = sum(len(r) for r in rows)
        if not n:
            return []
        return [prefix_for(0) + "|" + "\n".join(" ".join(r) for r in rows) + "|"
                + "".join(override_for(i) for i in range(n))]

    ta._row_bodies = bodies
    ta._curve_bodies = lambda st_, W, H, rows, override_for, prefix_for: bodies(
        rows, override_for, lambda k: prefix_for(0.0, 0.0, 0.0))
    ta._layout_prefix = lambda st_, W, H, is_first, *a, **k: "F" if is_first else "N"
    ta._glyph_prefix = lambda st_, W, H, is_first, *a, **k: "F" if is_first else "N"
    ta._active_override, ta._idle_override = (lambda s: "A"), (lambda s: "I")
    try:
        lines = ta.caption_dialogues(c, 1080, 1920, st)
    finally:
        ta._row_bodies, ta._curve_bodies, ta._layout_prefix, ta._glyph_prefix, ta._active_override, \
            ta._idle_override = saved
    out = []
    for line in lines:
        parts = line.split(",", 9)
        first, rows, flags = parts[9].split("|")
        out.append({"start": parts[1], "end": parts[2], "first": first == "F", "rows": rows, "flags": flags})
    return out


def _text_clip_case(ctx: Ctx, name: str, raw: dict, times, track_style=None) -> dict:
    """Un clip de texto con las funciones REALES: si se anima (a 30 y 24 fps), las ventanas de sus
    palabras y, en cada instante local, la pose, el estilo con la pose, la palabra activa y lo que
    se ve (texto y, por palabra, «A» activa o «I» inactiva)."""
    from app import text_ass as ta
    c = ctx.schemas.TimelineClip.model_validate(raw)
    st = ta.effective_text_style(track_style, c.style)
    dur = ta._clip_dur(c)
    n = len(ta.split_words(c.text or ""))
    # Eventos del .ass: con el texto en May./min. y el estilo con la pose en 0, como build_ass.
    cased = c.model_copy(update={"text": ta.apply_text_case(c.text, st.get("text_case"))}) if st.get("text_case") else c
    out = {"name": name, "clip": c.model_dump(), "style": st, "dur": dur,
           "animates": [ta.text_animates(c, f) for f in (30, 24)],
           "windows": [list(w) for w in ta.word_windows(c)],
           "in_ass": ta._clip_dur(c) > 0.02 and bool(ta.split_words(c.text or "")) and not c.disabled,
           "events": _caption_events(cased, ta._posed_style(cased, st)), "times": []}
    real = ta._active_override, ta._idle_override
    for t in times:
        posed = ta._posed_style(c, st, t)
        ta._active_override, ta._idle_override = (lambda s: "A"), (lambda s: "I")
        try:
            seen = ta._content_at(c, posed, t, dur)
        finally:
            ta._active_override, ta._idle_override = real
        content = None
        if seen is not None:
            shown, override_for = seen
            content = {"text": shown, "flags": "".join(override_for(i) for i in range(len(ta.split_words(shown))))}
        out["times"].append({"t": t, "pose": ta._pose_at(c, t), "posed": posed,
                             "active": ta._active_word_at(c, n, t, dur), "content": content})
    return _pure(_nan_safe(out))


def synthetic_text_fixtures(ctx: Ctx) -> dict:
    """Los cálculos puros de text_ass.py (estilo, opacidad, alfa, colores, sombra, brillo, curva,
    May./min., palabra activa, pose y palabras de un clip) y las métricas de font_metrics.py, con
    las funciones REALES."""
    import tempfile
    import text_cases as tc
    from app import font_metrics as fm
    from app import text_ass as ta
    alphas = [0.0, 1.0, 0.5, 1 / 3, float("nan"), float("inf"), float("-inf"), -0.5, 1.5, 1 - 0.5 / 255,
              1 - 1.5 / 255, 0.25, 0.75, 0.998]
    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for i, (name, data) in enumerate(tc.FONT_FILES):
            p = Path(tmp) / f"f{i}.ttf"
            p.write_bytes(data)
            files.append({"name": name, "hex": data.hex(), "line_height_em": fm._line_height_em.__wrapped__(str(p))})
        files.append({"name": "no existe", "hex": None,
                      "line_height_em": fm._line_height_em.__wrapped__(str(Path(tmp) / "no-existe.ttf"))})
        layouts = [_font_layout_case(lay, Path(tmp) / f"l{i}") for i, lay in enumerate(tc.FONT_LAYOUTS)]
    return _pure({
        "rows_text": tc.ROWS_TEXT,
        "styles": [_text_style_case(st) for st in tc.STYLES],
        "clips": [_text_clip_case(ctx, c["name"], c["clip"], tc.TEXT_TIMES) for c in tc.TEXT_CLIPS],
        "effective": [{"track": t, "clip": c, "out": ta.effective_text_style(t, c)} for t, c in tc.EFFECTIVE],
        "alpha": [{"op": _nan_safe(op), "out": ta.alpha_hex(op)} for op in alphas],
        "case": [{"text": t, "mode": m, "out": ta.apply_text_case(t, m)} for t in tc.CASE_TEXTS for m in tc.CASE_MODES],
        "active_word": [{"n": n, "t": t, "d": d, "out": _safe(ta.active_word_index, n, t, d)}
                        for n, t, d in tc.ACTIVE_WORD],
        "font_files": files,
        "font_layouts": layouts,
        # Anton va con la app (resources/fonts, copiada de backend/app/fonts).
        "anton": fm._line_height_em.__wrapped__(str(fm.BUNDLED_FONT_DIR / "Anton-Regular.ttf")),
    })


def real_text_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus textos: el estilo efectivo (pista + clip) de cada clip de texto, sin repetidos, con
    sus cálculos; su texto en May./min.; y el factor de cada fuente instalada en C:/Windows/Fonts."""
    from app import font_metrics as fm
    from app import text_ass as ta
    seen, styles, cases = set(), [], []
    for p in data.get("projects", []):
        tl = p.get("timeline") or {}
        track_style = {t.get("id"): t.get("style") for t in tl.get("tracks", [])}
        for c in tl.get("clips", []):
            if c.get("kind") != "text":
                continue
            st = ta.effective_text_style(track_style.get(c.get("track_id")), c.get("style"))
            key = json.dumps(st, sort_keys=True)
            if key not in seen:
                seen.add(key)
                styles.append(_text_style_case(st))
            mode = st.get("text_case")
            for m in dict.fromkeys([mode, "upper", "title"]):
                cases.append({"text": c.get("text") or "", "mode": m, "out": ta.apply_text_case(c.get("text") or "", m)})
    clips = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        track_style = {t.id: t.style for t in proj.timeline.tracks}
        for c in proj.timeline.clips:
            if c.kind == "text":
                dur = ta._clip_dur(c)
                times = [dur * k / 6 for k in range(7)] + [w.start + 0.001 for w in c.words[:6]]
                clips.append(_text_clip_case(ctx, f"{proj.id} · {c.id}", c.model_dump(), times,
                                             track_style.get(c.track_id)))
    fm._line_height_em.cache_clear()
    fonts = [{"name": n, "bold": b, "factor": fm.ass_size_factor(n, b)}
             for n in sorted({*fm.FONTS, *fm.FONTS_BOLD, *fm.BUNDLED_FONTS}) for b in (True, False)]
    import text_cases as tc
    return _pure({"rows_text": tc.ROWS_TEXT, "styles": styles, "case": cases, "clips": clips, "system_dir": fm.FONT_DIR.as_posix(),
                  "fonts": fonts})


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
    exi = synthetic_export_input_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "export_input" / "synthetic.json", exi, len(exi["clips"]), 0)
    med = synthetic_media_fixtures()
    dec = synthetic_decode_fixtures(ctx)
    seek = synthetic_seek_fixtures()
    write(REPO / "tests" / "fixtures" / "media" / "seek.json", seek, len(seek), 0)
    chroma = synthetic_chroma_fixtures()
    write(REPO / "tests" / "fixtures" / "media" / "chroma.json", chroma, len(chroma), 0)
    txt = synthetic_text_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "text" / "synthetic.json", txt,
          len(txt["styles"]) + len(txt["clips"]) + len(txt["effective"]) + len(txt["alpha"]) + len(txt["case"]) + len(txt["active_word"])
          + len(txt["font_files"]) + sum(len(lay["fonts"]) for lay in txt["font_layouts"]), 0)
    ren = synthetic_render_fixtures(ctx)
    write(RENDER_DIR / "synthetic.json", ren, len(ren), 0)
    ren_txt = synthetic_text_render_fixtures(ctx)
    write(RENDER_DIR / "text.json", ren_txt, len(ren_txt), 0)
    write(REPO / "tests" / "fixtures" / "media" / "decode.json", dec, len(dec), 0)
    write(REPO / "tests" / "fixtures" / "media" / "synthetic.json", med,
          len(med["gifs"]) + len(med["magic"]) + len(med["probe"]), 0)
    mot = synthetic_motion_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "motion" / "synthetic.json", mot,
          len(mot["downsample"]) + len(mot["normalize"]) + sum(len(t["pairs"]) for t in mot["timelines"])
          + len(mot["rms"]), sum("error" in d for d in mot["downsample"]))
    libr = synthetic_library_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "library" / "synthetic.json", libr,
          len(libr["steps"]) + len(libr["dto"]) + len(libr["save"]),
          sum("error" in s for s in libr["steps"] + libr["dto"] + libr["save"]))
    sett = synthetic_settings_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "settings" / "synthetic.json", sett, len(sett["steps"]),
          sum("error" in s for s in sett["steps"]))
    sfx_syn = synthetic_sfx_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "sfx" / "synthetic.json", sfx_syn, len(sfx_syn["steps"]),
          sum("error" in s for s in sfx_syn["steps"]))
    js = synthetic_json_fixtures()
    write(REPO / "tests" / "fixtures" / "json" / "synthetic.json", js,
          len(js["floats"]) + len(js["dumps"]) + len(js["loads"]), sum("error" in c for c in js["loads"]))
    prj_syn = synthetic_projects_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "projects" / "synthetic.json", prj_syn, len(prj_syn["steps"]),
          sum("error" in s for s in prj_syn["steps"]))
    col_syn = synthetic_collections_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "collections" / "synthetic.json", col_syn, len(col_syn["steps"]),
          sum("error" in s for s in col_syn["steps"]))
    stk_syn = synthetic_stick_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "stick" / "synthetic.json", stk_syn, len(stk_syn["steps"]),
          sum("error" in s for s in stk_syn["steps"]))
    hist = synthetic_history_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "history" / "synthetic.json", hist, len(hist["steps"]), 0)
    lay = synthetic_layout_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "layout" / "synthetic.json", lay,
          len(lay["clips"]) + len(lay["dest"]) + len(lay["crop_px"]),
          sum("error" in d["rect"] or "error" in d["even"] for d in lay["dest"]))
    lyr = synthetic_layers_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "layers" / "synthetic.json", lyr,
          sum(len(c["heads"]) for c in lyr["cases"]), 0)
    frm = synthetic_frame_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "frame" / "synthetic.json", frm,
          sum(len(c["heads"]) for c in frm["cases"]), 0)
    ev = synthetic_eval_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "eval" / "synthetic.json", ev,
          len(ev["clips"]) + len(ev["ease"]) + len(ev["tracks"]) + len(ev["fx_windows"]) + len(ev["effects"])
          + len(ev["blend"]) + len(ev["filters"]) + len(ev["filter_matrix"]) + len(ev["filter_catalog"]), 0)

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
        exi_real = real_export_input_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "export_input" / "real.json", exi_real, len(exi_real["clips"]), 0)
        med_real = real_media_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "media" / "real.json", med_real,
              len(med_real["gifs"]) + len(med_real["probe"]), 0)
        mot_real = real_motion_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "motion" / "real.json", mot_real,
              sum(len(t["pairs"]) for t in mot_real["timelines"]) + len(mot_real["rms"]), 0)
        libr_real = real_library_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "library" / "real.json", libr_real, len(libr_real["steps"]), 0)
        sett_real = real_settings_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "local" / "settings" / "real.json", sett_real, len(sett_real["steps"]), 0)
        sfx_real = real_sfx_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "local" / "sfx" / "real.json", sfx_real, len(sfx_real["steps"]),
              sum("error" in s for s in sfx_real["steps"]))
        prj_real = real_projects_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "local" / "projects" / "real.json", prj_real, len(prj_real["steps"]),
              sum("error" in s for s in prj_real["steps"]))
        col_real = real_collections_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "local" / "collections" / "real.json", col_real, len(col_real["steps"]),
              sum("error" in s for s in col_real["steps"]))
        stk_real = real_stick_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "local" / "stick" / "real.json", stk_real, len(stk_real["steps"]),
              sum("error" in s for s in stk_real["steps"]))
        hist_real = real_history_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "history" / "real.json", hist_real, len(hist_real["cases"]), 0)
        lay_real = real_layout_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "layout" / "real.json", lay_real,
              len(lay_real["clips"]) + len(lay_real["dest"]),
              sum("error" in d["rect"] or "error" in d["even"] for d in lay_real["dest"]))
        lyr_real = real_layers_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "layers" / "real.json", lyr_real,
              sum(len(c["heads"]) for c in lyr_real["cases"]), 0)
        frm_real = real_frame_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "frame" / "real.json", frm_real,
              sum(len(c["heads"]) for c in frm_real["cases"]), 0)
        print(f"  fotograma: {sum(len(c['js_dur']) for c in frm_real['cases'])} clips con clipDur de JS ≠ Python")
        txt_real = real_text_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "text" / "real.json", txt_real,
              len(txt_real["styles"]) + len(txt_real["case"]) + len(txt_real["clips"]) + len(txt_real["fonts"]), 0)
        ev_real = real_eval_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "eval" / "real.json", ev_real,
              len(ev_real["clips"]) + len(ev_real["filters"]), 0)
    print(f"listo en {time.perf_counter() - t0:.1f} s")


if __name__ == "__main__":
    main()
