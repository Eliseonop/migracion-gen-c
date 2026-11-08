"""Genera las fixtures de paridad de GenClipPro usando video-yt como referencia.

La app Python es la «verdad» durante la migración: este script le pasa cada
entrada y guarda lo que devuelve. Los tests de C++ (tests/parity) comprueban que
gc_core produce exactamente lo mismo.

Uso, con el Python de video-yt (tiene pydantic y el resto de dependencias):

    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\fixtures\\export_fixtures.py
    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\fixtures\\export_fixtures.py --real
    E:\\proyectos\\video-yt\\.venv\\Scripts\\python.exe tools\\fixtures\\export_fixtures.py --solo export

Salida (al repo):
  tests/fixtures/model/synthetic.json      modelo: casos de model_cases.py
  tests/fixtures/edit/synthetic.json       edición: casos de ops_cases.py
  tests/fixtures/keyframes/synthetic.json  animación: casos de kf_cases.py
  tests/fixtures/motion/synthetic.json     envolvente de un sonido (clip_motion): motion_cases.py
  tests/fixtures/media/synthetic.json      cabeceras de imagen (images.probe_gif) y sondas de medios: media_cases.py
  tests/fixtures/export_input/synthetic.json  cómo lee el export el material (clip_kind.py): export_input_cases.py
  tests/fixtures/media/decode.json         decode como el export: media_cases.DECODE (MD5 por fotograma del ffmpeg del PATH;
                                           con el autogiro de los vídeos con giro en el contenedor; --solo decode)
  tests/fixtures/media/seek.json           seek exacto de frame_grab.extract_frame: media_cases.SEEK (--solo decode)
  tests/fixtures/media/chroma.json         cadenas de FFmpeg del croma (clip_bg.py): media_cases.CHROMA
  tests/fixtures/media/outline.json        contorno del sujeto (clip_bg.outline_ffmpeg_steps): media_cases.OUTLINE
  tests/fixtures/render/synthetic.json     fotogramas del compuesto real (compose.render_frame): media_cases.RENDER
  tests/fixtures/export/synthetic.json     el export entero (compose.render, MP4 con audio): media_cases.EXPORT
  tests/fixtures/shapes/geometry.json      geometría de las figuras (shapes.py): shape_cases.py
  tests/fixtures/layout/synthetic.json     encuadre: casos de layout_cases.py
  tests/fixtures/eval/synthetic.json       evaluación por clip: casos de eval_cases.py
  tests/fixtures/layers/synthetic.json     capas de un instante: layers_cases.py (layersAt con Node)
  tests/fixtures/editor/timeline_ui.json   timeline del editor: editor_cases.py (editorModel.js con Node)
  tests/fixtures/ai/model_manager.json     modelos de IA en RAM (model_manager.py): model_manager_cases.py (--solo modelos)
  tests/fixtures/mcp/*.json                núcleo sin red del servidor MCP (mcp_server: dto, capabilities, help_content,
                                           registry, resources y audit): mcp_cases.py (--solo mcp)
  tests/fixtures/editor/lru_bytes.json     caché LRU con tope en bytes: lru_cases.py (lib/lruBytes.js con Node; --solo lru)
  tests/fixtures/render/platform.json      marco de TikTok/Shorts del preview (platformChrome.js en Chromium; --solo plataforma)
  tests/fixtures/media/image_import.json   el PNG de trabajo de «Importar imagen» (images.to_working_png con OpenCV;
                                           originales y PNG en media/import/; --solo imagenes)
  tests/fixtures/frame/synthetic.json      fotograma: frame_cases.py (drawComposite con Node)
  tests/fixtures/history/synthetic.json    historial: pasos de history_cases.py
  tests/fixtures/settings/synthetic.json   ajustes: pasos de settings_cases.py
  tests/fixtures/library/synthetic.json    biblioteca: pasos de library_cases.py
  tests/fixtures/sfx/synthetic.json        biblioteca de SFX en disco: pasos de sfx_cases.py
  tests/fixtures/sfx/write.json            alta, edición y categorías de SFX (sfx_cases.WRITE_STEPS)
  tests/fixtures/beats/synthetic.json      beats automáticos (beats.py) sobre las señales de beats_cases.py
  tests/fixtures/keywords/synthetic.json   palabras clave locales de Explorar (explore_keywords.py)
  tests/fixtures/audio/mono.json           audio mono de beats.py y sample_rms_envelope: media_cases.MONO_* (--solo mono)
  tests/fixtures/text/widths.json          ancho del texto (font_metrics.text_width, Pillow): text_cases.WIDTH_* (--solo anchos)
  tests/fixtures/media/bg_matte.json       el matte de «Eliminar fondo» en la caché (bg/service.py): bg_cases.py (--solo matte)
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
import re
import shutil
import subprocess
import sys
import tempfile
import types
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
    from app import yt_history as yh
    tmp_dir = Path(tempfile.mkdtemp(prefix="gc_settings_"))
    tmp = tmp_dir / "settings.json"
    saved = st._FILE
    saved_time = yh.time
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
                elif kind == "yt_record":   # url, video, limit, int(time.time())
                    yh.time = types.SimpleNamespace(time=lambda: args[3])
                    result = yh.record(args[0], args[1], args[2])
                elif kind == "yt_remove":
                    result = yh.remove(args[0])
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
        yh.time = saved_time
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
                elif kind == "create":
                    result = sfx.create_category(args[0])
                elif kind == "add":
                    result = sfx.add_sound(args[0], args[1].encode("utf-8"), name=args[2], category_id=args[3],
                                           new_category=args[4], uso=args[5])
                elif kind == "update":
                    result = sfx.update_sound(args[0], name=args[1], category_id=args[2], new_category=args[3],
                                              uso=args[4])
                elif kind == "lib":           # sfx_library.json sin la fecha
                    jf = sfx.get_base() / "sfx_library.json"
                    result = json.loads(jf.read_text(encoding="utf-8")) if jf.is_file() else None
                    if isinstance(result, dict):
                        result.pop("generado", None)
                elif kind == "tree":          # los archivos de la biblioteca
                    base = sfx.get_base()
                    result = sorted(p.relative_to(base).as_posix() for p in base.rglob("*") if p.is_file())
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


def synthetic_sfx_write_fixtures(ctx: Ctx) -> dict:
    """Dar de alta, editar y crear categorías (sfx_cases.WRITE_STEPS). Sin rutas absolutas en los
    resultados: valen en Windows y en el resto."""
    sys.path.insert(0, str(HERE))
    import sfx_cases as xc
    return {"root": None, "steps": _run_sfx_steps(xc.WRITE_STEPS, None)}


def synthetic_beats_fixtures(ctx: Ctx) -> list:
    """beats.py sobre las señales de beats_cases: la curva de ataque (cada 7 fotogramas), el tempo
    sin redondear, los fotogramas de los beats y lo que devuelve detect_beats."""
    sys.path.insert(0, str(HERE))
    import beats_cases as bc
    from app import beats
    out = []
    for case in bc.CASES:
        y = bc.synth(case)
        entry: dict = {"case": case}
        try:
            if y.size < beats.N_FFT:   # decode_mono
                raise ValueError("El clip no tiene audio suficiente para detectar beats.")
            env = beats.onset_envelope(y)
            bpm = beats.estimate_bpm(env)
            frames = beats.track_beats(env, bpm)
            entry.update({
                "env_len": len(env), "env": [float(v) for v in env[::7]], "bpm_raw": float(bpm),
                "frames": [int(f) for f in frames],
                "times": [round(float(f * beats.HOP / beats.SR + beats.ONSET_LAG), 3) for f in frames],
                "bpm": round(bpm, 1),
            })
        except Exception as e:  # noqa: BLE001
            entry["error"] = type(e).__name__
        out.append(entry)
    return out


def synthetic_keyword_fixtures(ctx: Ctx) -> dict:
    """explore_keywords.py sin IA: extract_keywords, parse_keyword_list, gather_theme_text y
    suggest_keywords con la IA anulada (sin claves)."""
    sys.path.insert(0, str(HERE))
    import keyword_cases as kc
    from app import explore_keywords as ek
    from app.schemas import Project
    saved = ek._llm_keywords
    ek._llm_keywords = lambda text: []
    try:
        return {
            "extract": [[t, n, ek.extract_keywords(t, n)] for t in kc.TEXTS for n in (8, 3, 1)],
            "parse": [[r, ek.parse_keyword_list(r)] for r in kc.RAWS],
            "gather": [[p, ek.gather_theme_text(Project.model_validate(p))] for p in kc.PROJECTS],
            "suggest": [[t, ek.suggest_keywords(t)] for t in kc.TEXTS],
        }
    finally:
        ek._llm_keywords = saved


def synthetic_url_fixtures(ctx: Ctx) -> dict:
    """urllib.parse (urlparse, hostname, parse_qsl, unquote) y los enlaces de YouTube de
    youtube_audio.py, con el Python de video-yt."""
    sys.path.insert(0, str(HERE))
    import unicodedata
    import urllib.parse as up
    import url_cases as uc
    from app import asset_import as ai
    from app import youtube_audio as ya

    def download(u):
        try:
            return ai.allowed_download_url(u)
        except ValueError:
            return None

    def parsed(u):
        try:
            r = up.urlparse(u)
        except ValueError:
            return None
        return [r.scheme, r.netloc, r.path, r.params, r.query, r.fragment, r.hostname]

    raws = list(uc.URLS) + [("https://" + u if "://" not in u else u) for u in uc.URLS if u.strip()]
    return {
        "nfkc": [c for c in range(0x80, 0x110000) if not 0xD800 <= c < 0xE000
                 and any(x in unicodedata.normalize("NFKC", chr(c)) for x in "/?#@:")],
        "parse": [[u, parsed(u)] for u in raws],
        "youtube": [[u, ya.is_youtube_url(u), ya.youtube_id_from_url(u)] for u in uc.URLS],
        "unquote": [[s, up.unquote(s), up.unquote_plus(s)] for s in uc.UNQUOTE],
        "qsl": [[q, [list(p) for p in up.parse_qsl(q)]] for q in uc.QUERIES],
        "download": [[u, download(u)] for u in uc.URLS + uc.DOWNLOADS],
    }


def synthetic_youtube_video_fixtures(ctx: Ctx) -> dict:
    """La parte sin red de youtube_video.py, con las funciones reales: quality_of de cada formato,
    available_qualities, resolve_quality y format_spec de cada info, y _mb. null = lanza."""
    sys.path.insert(0, str(HERE))
    import youtube_video_cases as yc
    from app import youtube_video as yv

    def attempt(fn, *a):
        try:
            return {"ok": fn(*a)}
        except Exception as e:  # noqa: BLE001
            return {"error": type(e).__name__, "message": str(e)}

    formats = [f for info in yc.INFOS if isinstance(info.get("formats"), list) for f in info["formats"]
               if isinstance(f, dict)]
    infos = []
    for info in yc.INFOS:
        qs = attempt(yv.available_qualities, info)
        entry = {"info": info, "qualities": qs}
        if "ok" in qs:
            entry["resolve"] = [[w, yv.resolve_quality(qs["ok"], w)] for w in yc.WANTED]
            wanted = sorted({x["quality"] for x in qs["ok"]} | {144, 180, 300, 1081})
            entry["spec"] = [[q, attempt(yv.format_spec, info, q)] for q in wanted]
        else:
            entry["spec"] = [[720, attempt(yv.format_spec, info, 720)]]
        infos.append(entry)
    return {
        "quality": [[f, attempt(yv.quality_of, f)] for f in formats],
        "infos": infos,
        "mb": [[n, yv._mb(n)] for n in yc.MB],
    }


def synthetic_heatmap_fixtures(ctx: Ctx) -> list:
    """heatmap._pick_segments real: [heatmap, duration, min_score, max_clips, max_duration, padding, tramos]."""
    sys.path.insert(0, str(HERE))
    import heatmap_cases as hc
    from app import heatmap as hm
    return [[*c, [s.model_dump(exclude={"label", "description"}) for s in hm._pick_segments(*c)]]
            for c in hc.CASES]


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
_PROJECTS_MESSAGES = {"El nombre no puede estar vacío.", "La carpeta no existe.",
                      # segments.py
                      "Proyecto no encontrado.", "Material de vídeo no encontrado.",
                      "El archivo del vídeo no existe en el proyecto.", "No hay segmentos.",
                      "El rango es demasiado corto para analizar caras.",
                      # media_upgrade.py
                      "Material de vídeo no encontrado (usa el vídeo completo, no un segmento).",
                      "No se puede reemplazar un material por sí mismo.", "URL de YouTube no válida.",
                      # videos.py
                      "Formato de vídeo no válido. Usa MP4, MOV, MKV, WEBM…",
                      "Formato de audio no válido. Usa MP3, WAV, M4A…", "Archivo vacío.",
                      "No se pudo guardar el vídeo.", "No se pudo guardar el audio.",
                      # images.py
                      "Formato no válido. Usa PNG, JPG, WebP o GIF.",
                      "No se pudo leer la imagen. Usa PNG, JPG, WebP o GIF.", "No se pudo guardar la imagen."}
# Los pasos que pueden escribir projects.json: tras ellos se guarda su texto.
_PROJECTS_WRITES = {"create", "create_group", "rename_group", "delete_group", "move", "rename", "duplicate",
                    "set_folder", "delete", "add_clips", "add_transcript", "add_audio", "add_image", "update",
                    "remove_material", "manifest", "timeline", "motion", "delete_motion", "scenes", "blueprint",
                    "retarget", "segments", "face_track", "upgrade", "import_video", "import_audio",
                    "import_image"}


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
    from app import config, detect, images, media_upgrade, segments, timeline_store, videos, youtube_audio
    projects, schemas = ctx.projects, ctx.schemas
    tmp = Path(tempfile.mkdtemp(prefix="gc_prj_"))
    posix = tmp.as_posix()
    saved = (projects._FILE, config.OUTPUT_DIR, uuid.uuid4, projects._now)
    saved_seg = (segments._now, detect.face_track, youtube_audio.probe_duration, detect.dims,
                 timeline_store.HISTORY_DIR, videos.probe_duration, images.to_working_png, images.probe_size)
    timeline_store.HISTORY_DIR = tmp / "data" / "history"
    projects._FILE = tmp / "data" / "projects.json"
    config.OUTPUT_DIR = tmp / "clips"
    uuid.uuid4 = _SequentialUuid()
    projects._now = _FixedNow()
    segments._now = projects._now  # la misma hora que el almacén (una sola secuencia)

    def dump(value):
        if hasattr(value, "model_dump"):
            return value.model_dump()
        if isinstance(value, (list, tuple)):
            return [dump(v) for v in value]
        return value

    # La sonda de duración de segments._file_duration y el detector de caras, sustituidos por lo
    # que dice cada paso (None: fallan).
    def create_segments(pid, ident, raw, dur):
        def probe(_path):
            if dur is None:
                raise RuntimeError("sin sonda")
            return dur
        youtube_audio.probe_duration = probe
        return segments.create_segments(pid, ident, raw)

    def face_track(pid, ident, start, end, samples, force, info):
        def fake(_path, samples=0, on_progress=None, start=None, end=None, progress_base=0.0):
            if info is None:
                raise RuntimeError("el detector falla")
            return copy.deepcopy(info)
        detect.face_track = fake
        return segments.face_track_material(pid, ident, lambda *_a: None, start, end, samples, force)

    # detect.dims de media_upgrade: las medidas de cada archivo por su nombre (sin él, falla).
    def with_dims(dims):
        def fake(path):
            name = Path(path).name
            if name not in dims:
                raise RuntimeError("sin medidas")
            return tuple(dims[name])
        detect.dims = fake

    def versions(pid, ident, dims):
        with_dims(dims)
        return media_upgrade.find_versions(pid, ident)

    def upgrade(pid, ident, olds, dims):
        with_dims(dims)
        return media_upgrade.replace_with(pid, ident, olds)

    def best_full(pid, vid, dims):
        with_dims(dims)
        return media_upgrade.best_full_version(pid, vid)

    # videos.import_video / import_audio sobre el proyecto tal como está, con la sonda de duración
    # de videos.py dando `dur` (None: falla, y vale 0).
    def with_probe(dur):
        def probe(_path):
            if dur is None:
                raise RuntimeError("sin sonda")
            return dur
        videos.probe_duration = probe

    def import_video(pid, filename, data, dur, meta):
        with_probe(dur)
        return videos.import_video(projects.get_project(pid), filename, data.encode("utf-8"), **meta)

    def import_audio(pid, filename, data, dur):
        with_probe(dur)
        return videos.import_audio(projects.get_project(pid), filename, data.encode("utf-8"))

    # images.import_image con OpenCV sustituido (cv2 no se puede fijar con datos de mentira): un PNG
    # pasa tal cual (lo del to_working_png real); lo demás «se lee» si lleva «OK» (y da la firma de
    # PNG + «conv:» + los datos) y si no, el error de to_working_png. probe_size da `size` (None:
    # falla). Los datos van en hexadecimal.
    def import_image(pid, filename, data_hex, size, meta, keep_gif):
        def to_png(_name, data):
            if data.startswith(images.PNG_MAGIC):
                return data
            if b"OK" in data:
                return images.PNG_MAGIC + b"conv:" + data
            raise ValueError("No se pudo leer la imagen. Usa PNG, JPG, WebP o GIF.")
        images.to_working_png = to_png
        images.probe_size = lambda _path: tuple(size) if size is not None else (None, None)
        return images.import_image(projects.get_project(pid), filename, bytes.fromhex(data_hex), keep_gif=keep_gif,
                                   **meta)

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
        "segments": create_segments, "face_track": face_track, "versions": versions, "upgrade": upgrade,
        "yt_sources": media_upgrade.youtube_sources, "best_full": best_full,
        "import_video": import_video, "import_audio": import_audio, "import_image": import_image,
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
                    # El texto de cada archivo; el que no es UTF-8 (un PNG), «hex:» y sus bytes.
                    def content(f):
                        try:
                            return f.read_text(encoding="utf-8")
                        except UnicodeDecodeError:
                            return "hex:" + f.read_bytes().hex()
                    result = [[p.relative_to(top).as_posix(), content(p) if p.is_file() else None]
                              for p in sorted(top.rglob("*"), key=lambda p: p.relative_to(top).as_posix())]
                elif kind in calls:
                    result = dump(calls[kind](*args))
                else:
                    raise ValueError(f"paso desconocido: {kind}")
                entry["result"] = tmp_of(result)
            except Exception as e:  # noqa: BLE001
                entry["error"] = type(e).__name__
                if str(e) in _PROJECTS_MESSAGES or re.match(
                        r"Segmento \d+: |Material no encontrado: |«.*» no se puede cambiar: ", str(e)):
                    entry["message"] = str(e)
            if record_file and kind in _PROJECTS_WRITES:
                f = projects._FILE
                entry["file"] = f.read_bytes().decode("utf-8").replace(posix, "{TMP}") if f.exists() else None
            out.append(entry)
    finally:
        projects._FILE, config.OUTPUT_DIR, uuid.uuid4, projects._now = saved
        (segments._now, detect.face_track, youtube_audio.probe_duration, detect.dims,
         timeline_store.HISTORY_DIR, videos.probe_duration, images.to_working_png, images.probe_size) = saved_seg
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
    for name, args in (mc.PROBE_MEDIA + mc.PROBE_MEDIA_VFR + mc.PROBE_MEDIA_2S + mc.PROBE_MEDIA_ALPHA
                       + mc.PROBE_MEDIA_CODECS + mc.AUDIO_MEDIA + mc.MONO_MEDIA):
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


def _cli_dims(ffmpeg: str, ffprobe: str, path: Path, args: list[str]) -> tuple[int, int]:
    """Ancho y alto de lo que SALE del CLI con `args` (entrada y filtros): los de la cabecera del
    primer fotograma en PNG. No los de ffprobe, que son los codificados: un vídeo con giro en el
    contenedor sale girado (autogiro). Si no sale nada, los de ffprobe."""
    import struct
    png = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", *args, "-frames:v", "1",
                          "-c:v", "png", "-f", "image2pipe", "-"], capture_output=True).stdout
    if len(png) >= 24 and png[12:16] == b"IHDR":
        w, h = struct.unpack(">II", png[16:24])
        return w, h
    dims = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                           "-of", "csv=p=0", str(path)], capture_output=True, text=True, check=True).stdout
    w, h = (int(x) for x in dims.strip().split(",")[:2])
    return w, h


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
        vf = chain + (",scale=iw:ih" if name in mc.DECODE_SCALE_AFTER else "") + ",format=rgba"
        cpu = ["-cpuflags", mc.DECODE_CPUFLAGS[name]] if name in mc.DECODE_CPUFLAGS else []
        w, h = _cli_dims(ffmpeg, ffprobe, path, [*cpu, *args, "-vf", vf])
        out = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", *cpu, *args, "-vf", vf,
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
        w, h = _cli_dims(ffmpeg, ffprobe, path, ["-i", str(path)])
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


def synthetic_render_fixtures(ctx: Ctx, keep: bool = False) -> list[dict]:
    """Fotogramas del COMPUESTO real del export (compose.render_frame: la misma tubería que el
    export, pero con un solo PNG) para media_cases.RENDER. Solo se sustituye dónde está el
    material (_clip_path). Los PNG van a tests/fixtures/render (git-lfs). Con `keep` (--conservar),
    los PNG que ya existen no se vuelven a componer: solo los de las escenas nuevas."""
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
    for name, src, pre, post in mc.RENDER_DERIVED:
        if not (files / name).exists():
            subprocess.run([ffmpeg, "-y", "-hide_banner", "-loglevel", "error", *pre, "-i", str(files / src), *post,
                            str(files / name)], check=True)

    # Con `keep`, un PNG se conserva solo si su caso (instante y timeline) es el mismo que el del
    # synthetic.json que ya hay: una escena cambiada o un índice movido se vuelven a componer.
    old: dict[str, str] = {}
    if keep and (RENDER_DIR / "synthetic.json").exists():
        for c in json.loads((RENDER_DIR / "synthetic.json").read_text(encoding="utf-8")):
            old[c["frame"]] = json.dumps([c["t"], c["timeline"]], sort_keys=True)

    def render_frame(timeline, png: str, t: float) -> None:
        same = old.get(png) == json.dumps([t, _pure(timeline.model_dump())], sort_keys=True)
        if not (keep and same and (RENDER_DIR / png).exists()):
            compose.render_frame(SimpleNamespace(id="render-fixture"), timeline, RENDER_DIR / png, t)
    real_clip_path = compose._clip_path
    # Los horneados de los efectos de vídeo (fx_bake), a una carpeta temporal: no en data/ de video-yt.
    from app import fx_bake
    real_cache = fx_bake.CACHE_DIR
    fx_bake.CACHE_DIR = Path(tempfile.mkdtemp(prefix="gc_fxcache_"))
    # «Eliminar fondo»: el nivel 1 de las escenas, en render/files/bgcache (al repo; el C++ lo lee de
    # ahí); el nivel 2 que deriva ensure_derived, a una carpeta temporal nueva para cada fotograma.
    import bg_cases as bc
    from app.bg import service as bg_service
    bc.make_matte_cache(files / "bgcache", bc.RENDER_MATTES)
    real_bg = bg_service.MATTE_ROOT, bg_service.MASK_ROOT
    mask_tmp = Path(tempfile.mkdtemp(prefix="gc_bgmask_"))
    bg_service.MATTE_ROOT, bg_service.MASK_ROOT = files / "bgcache" / "matte", mask_tmp
    # El material de las fixtures; las figuras, su PNG rasterizado (shape_files), como el real.
    compose._clip_path = lambda project, clip, shape_files=None: (
        (shape_files or {}).get(clip.id) if clip.kind == "shape" else files / clip.filename)
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
                render_frame(timeline, png, t)
                cases.append({"name": name, "t": t, "file": file, "frame": png,
                              "timeline": _pure(timeline.model_dump())})
        for i, (name, fps, tracks, clips, times) in enumerate(mc.RENDER_SCENES):
            raw_clips = []
            for j, (file, track, fields) in enumerate(clips):
                still = fields["kind"] != "video"  # imágenes y capas de ajuste (sin archivo)
                raw_clips.append({"id": f"c{j + 1}", "track_id": track, "asset_id": f"a{j + 1}", "filename": file,
                                  "source_duration": 0.0 if still else _REAL_SFX["probe_duration"](files / file),
                                  **fields})
            raw = {"version": 2, "width": 360, "height": 640, "fps": fps,
                   "tracks": [{"id": tid, "kind": "video", "name": tid} for tid in tracks], "clips": raw_clips}
            timeline = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
            for t in times:
                png = f"s{i:02d}_{int(round(t * 1000)):05d}.png"
                bg_service.MASK_ROOT = mask_tmp / Path(png).stem
                render_frame(timeline, png, t)
                case = {"name": name, "t": t, "frame": png, "timeline": _pure(timeline.model_dump())}
                if name in mc.RENDER_NOISY:
                    case["noise"] = True
                cases.append(case)
        return cases
    finally:
        compose._clip_path = real_clip_path
        shutil.rmtree(fx_bake.CACHE_DIR, ignore_errors=True)
        fx_bake.CACHE_DIR = real_cache
        shutil.rmtree(mask_tmp, ignore_errors=True)
        bg_service.MATTE_ROOT, bg_service.MASK_ROOT = real_bg


def synthetic_text_mask_fixtures() -> dict:
    """Máscaras de tipo texto (clip_mask._paint_text, con Pillow): el alfa REAL de combined_alpha
    de cada media_cases.TEXT_MASKS en un PNG gris (tm<i>.png), y las medidas de Pillow que usa
    (getmetrics, getlength y la caja con cada ancla) para TEXT_MASK_METRICS."""
    import media_cases as mc
    from PIL import ImageFont
    from app import clip_mask
    from app.compose import resolve_font_path
    masks = []
    for i, (name, raw, w, h) in enumerate(mc.TEXT_MASKS):
        m = clip_mask.normalize_mask(raw)
        png = f"tm{i:02d}.png"
        clip_mask.write_mask_png([m], w, h, RENDER_DIR / png)
        font = resolve_font_path(m["text"]["font"], m["text"]["weight"] >= 600)
        masks.append({"name": name, "mask": m, "w": w, "h": h, "png": png, "font_file": Path(font).name})
    metrics = []
    for f, b, s, t in mc.TEXT_MASK_METRICS:
        font = ImageFont.truetype(str(resolve_font_path(f, b)), s)
        metrics.append({"font": f, "bold": b, "size": s, "text": t, "metrics": list(font.getmetrics()),
                        "length": font.getlength(t),
                        "bbox": {a: list(font.getbbox(t, anchor=a)) for a in ("ls", "mm", "lm", "rm")}})
    return {"masks": masks, "metrics": metrics}


_VFX_JS = """async ([url, w, h, entries, t, clipDur, pad]) => {
  const img = new Image();
  img.src = url;
  await img.decode();
  const c = document.createElement('canvas');
  const r = window.__fx.renderVfx({ source: img, w, h, entries, t, clipDur, padAllowed: pad,
                                    key: 'fixture', maxSide: Infinity, target: c });
  return { url: c.toDataURL('image/png'), width: c.width, height: c.height, pad: r ? r.layout.P : null };
}"""


def synthetic_fx_bake_fixtures() -> list[dict]:
    """fx_bake.py sin hornear: active_vfx, has_vfx, pad_frac, pad_allowed, _remap_reframe y
    baked_clip con las funciones reales, sobre los clips de vfx_cases.BAKE_CLIPS."""
    sys.path.insert(0, str(HERE))
    import vfx_cases as vc
    from app import fx_bake
    from app.schemas import TimelineClip
    out = []
    for raw in vc.BAKE_CLIPS:
        clip = TimelineClip(**raw)
        try:
            entries = fx_bake.active_vfx(clip)
        except (TypeError, ValueError, OverflowError) as e:
            out.append({"clip": raw, "error": type(e).__name__})
            continue
        rf = fx_bake._remap_reframe(clip)
        out.append({
            "clip": raw,
            "active": entries,
            "has": fx_bake.has_vfx(clip),
            "pad_frac": fx_bake.pad_frac(entries),
            "pad_allowed": [[*d, fx_bake.pad_allowed(clip, *d)] for d in vc.PAD_DIMS],
            "reframe": rf.model_dump() if rf is not None else None,
            "baked": fx_bake.baked_clip(clip, Path("horneado.webm")).model_dump(),
        })
    return out


def synthetic_vfx_fixtures() -> list[dict]:
    """Los efectos de vídeo de motor «shader» con el runtime REAL de video-yt en Chromium (el de
    fx_bake.FxBaker, el que hornea el export): `window.__fx.renderVfx` sobre la imagen de prueba
    de vfx_cases.py. Los PNG van a tests/fixtures/render/vfx/."""
    import base64
    from PIL import Image
    sys.path.insert(0, str(HERE))
    import vfx_cases as vc
    from app import fx_bake, fx_catalog
    files = RENDER_DIR / "files"
    src = files / "vfx_in.png"
    if not src.exists():
        vc.make_input(src)
    url = "data:image/png;base64," + base64.b64encode(src.read_bytes()).decode()
    out_dir = RENDER_DIR / "vfx"
    out_dir.mkdir(parents=True, exist_ok=True)
    effects = [e for e in fx_catalog.effects() if e["engine"] == "shader"]
    # Las texturas de los efectos (paper_cutout), recurso de la app como el catálogo: de la raíz
    # pública del frontend de video-yt a resources/fx, con la misma ruta relativa.
    for e in fx_catalog.effects():
        for rel in e.get("assets") or []:
            src_asset, dst_asset = fx_bake.PUBLIC_DIR / rel, REPO / "resources" / "fx" / rel
            if not dst_asset.exists() or dst_asset.read_bytes() != src_asset.read_bytes():
                dst_asset.parent.mkdir(parents=True, exist_ok=True)
                dst_asset.write_bytes(src_asset.read_bytes())
                print(f"{dst_asset.relative_to(REPO)}: actualizado desde video-yt")
    cases = []
    with fx_bake.FxBaker() as baker:
        baker._ensure()
        page = baker._page
        for e in effects:
            defaults = {p["id"]: p["def"] for p in e["params"]}
            runs = [(defaults, t, False) for t in vc.TIMES] + [(vc.variant_params(e["params"]), vc.VARIANT_T, False)]
            if e["pad"]:
                runs.append((defaults, vc.TIMES[0], True))
            for n, (params, t, pad) in enumerate(runs):
                entries = [{"effect": e["id"], "v": e["v"], "params": params, "from": 0, "to": None}]
                r = page.evaluate(_VFX_JS, [url, 128, 96, entries, t, vc.CLIP_DUR, pad])
                name = f"{e['id']}_{n}.png"
                (out_dir / name).write_bytes(base64.b64decode(r["url"].split(",", 1)[1]))
                cases.append({"effect": e["id"], "v": e["v"], "params": params, "t": t, "clip_dur": vc.CLIP_DUR,
                              "pad_allowed": pad, "input": "vfx_in.png", "frame": f"vfx/{name}",
                              "width": r["width"], "height": r["height"], "pad": r["pad"]})
        # paper_cutout (Canvas 2D): con sus texturas cargadas, como hace el horneado (preloadFxAssets).
        paper = next(e for e in fx_catalog.effects() if e["id"] == "paper_cutout")
        page.evaluate("async (assets) => { window.__fx.setAssetBase(location.origin + '/assets/');"
                      " await window.__fx.preload(assets); }", paper["assets"])
        for n, (file, params, t, dur, pad) in enumerate(vc.PAPER):
            img = files / file
            w, h = Image.open(img).size
            purl = "data:image/png;base64," + base64.b64encode(img.read_bytes()).decode()
            entries = [{"effect": "paper_cutout", "v": paper["v"], "params": params, "from": 0, "to": None}]
            r = page.evaluate(_VFX_JS, [purl, w, h, entries, t, dur, pad])
            name = f"paper_cutout_{n}.png"
            (out_dir / name).write_bytes(base64.b64decode(r["url"].split(",", 1)[1]))
            cases.append({"effect": "paper_cutout", "v": paper["v"], "params": params, "t": t, "clip_dur": dur,
                          "pad_allowed": pad, "input": file, "frame": f"vfx/{name}",
                          "width": r["width"], "height": r["height"], "pad": r["pad"]})
    return cases


# Escenas del marco de plataforma: (nombre, ancho, alto, recuadro [x, y, w, h], marco, fondo o None).
PLATFORM_CASES = [
    ("tiktok 360x640", 360, 640, [0, 0, 360, 640], "tiktok", "#3c6e91"),
    ("shorts 360x640", 360, 640, [0, 0, 360, 640], "shorts", "#3c6e91"),
    ("tiktok 720x1280", 720, 1280, [0, 0, 720, 1280], "tiktok", "#7a4b2a"),
    ("shorts 720x1280", 720, 1280, [0, 0, 720, 1280], "shorts", "#7a4b2a"),
    ("tiktok en un recuadro dentro del lienzo", 520, 720, [80, 40, 360, 640], "tiktok", "#204030"),
    ("shorts en un recuadro dentro del lienzo", 520, 720, [80, 40, 360, 640], "shorts", "#204030"),
    ("tiktok sin fondo (el disco recorta)", 360, 640, [0, 0, 360, 640], "tiktok", None),
    ("shorts sin fondo", 360, 640, [0, 0, 360, 640], "shorts", None),
    ("tiktok pequeño", 180, 320, [0, 0, 180, 320], "tiktok", "#3c6e91"),
    ("sin marco", 180, 320, [0, 0, 180, 320], "none", "#3c6e91"),
    ("marco desconocido", 180, 320, [0, 0, 180, 320], "instagram", "#3c6e91"),
    ("recuadro sin ancho", 180, 320, [0, 0, 0, 320], "tiktok", "#3c6e91"),
]


def synthetic_platform_chrome_fixtures(ctx: Ctx) -> list[dict]:
    """El marco de TikTok/Shorts del preview (features/editor/render/platformChrome.js) dibujado por
    el JS REAL en el Canvas 2D de Chromium (por software: sin GPU, para que sea estable). Los PNG
    van a tests/fixtures/render/platform/."""
    import base64
    from playwright.sync_api import sync_playwright
    src = (ctx.root / "frontend" / "src" / "features" / "editor" / "render" / "platformChrome.js").read_text(
        encoding="utf-8")
    body = src.replace("export const ", "const ").replace("export function ", "function ")
    js = "\n".join([
        "([W, H, frame, kind, bg]) => {",
        body,
        "const c = document.createElement('canvas'); c.width = W; c.height = H",
        "const g = c.getContext('2d')",
        "if (bg) { g.fillStyle = bg; g.fillRect(0, 0, W, H) }",
        "drawPlatformChrome(g, { x: frame[0], y: frame[1], w: frame[2], h: frame[3] }, kind)",
        "return { url: c.toDataURL('image/png'), overlays: PLATFORM_OVERLAYS,"
        " labels: ['none', 'tiktok', 'shorts', 'x', ''].map((id) => [id, overlayLabel(id)]) }",
        "}",
    ])
    out_dir = RENDER_DIR / "platform"
    out_dir.mkdir(parents=True, exist_ok=True)
    cases = []
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True, args=["--disable-gpu"])
        page = browser.new_page()
        for n, (name, w, h, frame, kind, bg) in enumerate(PLATFORM_CASES):
            r = page.evaluate(js, [w, h, frame, kind, bg])
            file = f"platform_{n}.png"
            (out_dir / file).write_bytes(base64.b64decode(r["url"].split(",", 1)[1]))
            cases.append({"name": name, "width": w, "height": h, "frame": frame, "kind": kind, "background": bg,
                          "png": f"platform/{file}", "overlays": r["overlays"], "labels": r["labels"]})
        browser.close()
    return cases


def synthetic_effect_chains() -> list[dict]:
    """La cadena de FFmpeg de effects_ffmpeg (clip_fx.py) para media_cases.EFFECT_CHAINS, en un
    cuadro de 360×640: el C++ comprueba que lee de cada `effects` lo mismo que FFmpeg."""
    import media_cases as mc
    from app.clip_fx import effects_ffmpeg
    return [{"effects": e, "chain": effects_ffmpeg({"effects": e}, 360, 640)} for e in mc.EFFECT_CHAINS]


def synthetic_text_render_fixtures(ctx: Ctx) -> list[dict]:
    """Fotogramas del export real (compose.render_frame, con el .ass de text_ass y libass) para
    media_cases.RENDER_TEXT. El reparto en líneas usa la medida REAL de Pillow
    (font_metrics.text_width), no la estimación de _install_fakes. Los PNG: x<escena>_<ms>.png."""
    import media_cases as mc
    from types import SimpleNamespace
    from app import compose, font_metrics, text_ass
    files = RENDER_DIR / "files"
    real_clip_path, fake_width = compose._clip_path, text_ass.text_width
    real_build = compose.build_command
    compose._clip_path = lambda project, clip, shape_files=None: files / clip.filename
    text_ass.text_width = font_metrics.text_width

    # Un texto en capa propia (su .ass es <salida>.mt<i>.ass) lo dibuja el filtro `ass` sobre una
    # capa RGBA, y sobre RGB FFmpeg 9 escribe los colores en rango de TV: el blanco sale a 235
    # (sobre yuva444p, a 255). Es la divergencia de «sobre una imagen» (decisión de Edu; el C++
    # pinta el color pedido): para comparar lo demás (3D, máscara, fusión y orden), aquí esa capa se
    # dibuja en yuva444p y vuelve a RGBA. Detrás de una capa propia, el overlay deja el cuadro en
    # RGB y al .ass global le pasa lo mismo: también se dibuja en yuv444p. Sin capas propias el
    # grafo no cambia.
    def build_full_range(*args, **kwargs):
        cmd = real_build(*args, **kwargs)
        i = cmd.index("-filter_complex")
        graph = cmd[i + 1]
        if ".mt0.ass" in graph or ".tt0.ass" in graph:  # capa propia o de una pista con vídeo encima
            graph = re.sub(r"format=rgba,(?P<ass>ass='[^']*\.(?:mt|tt)\d+\.ass'(?::fontsdir='[^']*')?:alpha=1)",
                           r"format=yuva444p,\g<ass>,format=rgba", graph)
            graph = re.sub(r"\[(?P<lab>[^\]]+)\](?P<ass>ass='[^']*'(?::fontsdir='[^']*')?)\[txass\]",
                           r"[\g<lab>]format=yuv444p,\g<ass>[txass]", graph)
        cmd[i + 1] = graph
        return cmd

    compose.build_command = build_full_range
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
                for extra in RENDER_DIR.glob(f"{Path(png).stem}.*.ass"):  # y los de capa propia (.mt0.ass…)
                    extra.unlink()
                case = {"name": name, "t": t, "frame": png, "timeline": _pure(timeline.model_dump())}
                if name in mc.RENDER_TEXT_DIVERGENCES:
                    case["divergence"] = mc.RENDER_TEXT_DIVERGENCES[name]
                if name in mc.RENDER_TEXT_FLOORS:
                    case["min_psnr"] = mc.RENDER_TEXT_FLOORS[name]
                if name in mc.RENDER_TEXT_LOOSE:
                    case["loose"] = mc.RENDER_TEXT_LOOSE[name]
                if name in mc.RENDER_TEXT_PENDING:
                    case["pending"] = True
                cases.append(case)
        # Pistas de texto con vídeo encima: cada texto en la pila de su escena (PNG y<escena>_<ms>.png).
        for i, (name, tracks, scene_clips, times) in enumerate(mc.RENDER_TEXT_STACKS):
            clips = [c if c["kind"] == "text" else
                     {**c, "source_duration": _REAL_SFX["probe_duration"](files / c["filename"])
                      if c["filename"].endswith(".mp4") else 0.0} for c in scene_clips]
            # En el esquema actual: migrar desde uno viejo pondría las pistas de texto arriba del todo.
            raw = {"version": 2, "schema_version": ctx.migrations.CURRENT_SCHEMA_VERSION, "width": 540, "height": 960,
                   "fps": 30, "tracks": tracks, "clips": clips}
            timeline = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
            assert [t.id for t in timeline.tracks] == [t["id"] for t in tracks], name
            for t in times:
                png = f"y{i:02d}_{int(round(t * 1000)):05d}.png"
                compose.render_frame(SimpleNamespace(id="render-fixture"), timeline, RENDER_DIR / png, t)
                (RENDER_DIR / png).with_suffix(".ass").unlink(missing_ok=True)
                for extra in RENDER_DIR.glob(f"{Path(png).stem}.*.ass"):  # capa propia y de pista (.tt0.ass…)
                    extra.unlink()
                case = {"name": name, "t": t, "frame": png, "timeline": _pure(timeline.model_dump())}
                for key, table in (("divergence", mc.RENDER_TEXT_DIVERGENCES), ("min_psnr", mc.RENDER_TEXT_FLOORS),
                                   ("loose", mc.RENDER_TEXT_LOOSE)):
                    if name in table:
                        case[key] = table[name]
                cases.append(case)
        return cases
    finally:
        compose._clip_path, text_ass.text_width = real_clip_path, fake_width
        compose.build_command = real_build


def synthetic_shape_fixtures(ctx: Ctx) -> dict:
    """Geometría de las figuras con las funciones REALES de shapes.py: shape_geometry (todos los
    tipos y sus variantes), path_points, trim_polyline, dash_runs, dash_pattern, flip_geometry y
    shape_draw_at (con y sin keyframes)."""
    import shape_cases as sc
    from app import shapes

    def pts(seq):
        return [[float(x), float(y)] for x, y in seq]

    def geo_json(g):
        return {"fills": [pts(r) for r in g["fills"]], "strokes": [pts(r) for r in g["strokes"]]}

    geometry = [{"type": t, "opts": o, "out": geo_json(shapes.shape_geometry(t, o))} for t, o in sc.GEOMETRY]
    paths = [{"points": p, "closed": c, "smooth": sm, "out": pts(shapes.path_points(p, c, sm))}
             for p, c, sm in sc.PATHS]
    trims = [{"pts": l, "frac": f, "out": pts(shapes.trim_polyline([tuple(x) for x in l], f))} for l, f in sc.TRIMS]
    dashes = []
    for l, pat in sc.DASHES:
        runs, dots = shapes.dash_runs([tuple(x) for x in l], tuple(pat))
        dashes.append({"pts": l, "pattern": pat, "runs": [pts(r) for r in runs], "dots": pts(dots)})
    patterns = [{"dash": d, "lw": lw, "out": list(shapes.dash_pattern(d, lw)) if shapes.dash_pattern(d, lw) else None}
                for d, lw in sc.PATTERNS]
    flips = [{"type": t, "flip": f, "out": geo_json(shapes.flip_geometry(shapes.shape_geometry(t, {}), tuple(f)))}
             for t, f in sc.FLIPS]
    draws = []
    for clip_fields, times in sc.DRAWS:
        raw = {"version": 2, "width": 360, "height": 640, "fps": 30,
               "tracks": [{"id": "V1", "kind": "video", "name": "V1"}],
               "clips": [{"id": "c1", "track_id": "V1", "kind": "shape", "asset_kind": "shape", "asset_id": "rect",
                          "filename": "", "start": 0.0, "in_point": 0.0, "out_point": 2.0, **clip_fields}]}
        timeline = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
        draws.append({"timeline": _pure(timeline.model_dump()),
                      "samples": [[t, shapes.shape_draw_at(timeline.clips[0], t)] for t in times]})
    return {"geometry": geometry, "paths": paths, "trims": trims, "dashes": dashes, "patterns": patterns,
            "flips": flips, "draws": draws}


EXPORT_DIR = REPO / "tests" / "fixtures" / "export"


def synthetic_export_fixtures(ctx: Ctx) -> list[dict]:
    """El export ENTERO (compose.render: vídeo y audio en un MP4, con el codificador que elija
    gpu.py) de media_cases.EXPORT. El material se lee en su sitio: clips de colección con la raíz
    de colecciones (collections.get_root) en tests/fixtures. Los MP4 van a tests/fixtures/export
    (git-lfs) y solo se rehacen si cambia su timeline (la huella va en la fixture): el codificador
    no da los mismos bytes en cada ejecución."""
    import hashlib
    import media_cases as mc
    from types import SimpleNamespace
    from app import collections, compose
    fixtures = REPO / "tests" / "fixtures"
    old = {}
    if (EXPORT_DIR / "synthetic.json").exists():
        old = {c["file"]: c.get("hash") for c in json.loads((EXPORT_DIR / "synthetic.json").read_text(encoding="utf-8"))}
    EXPORT_DIR.mkdir(parents=True, exist_ok=True)
    real_root = collections.get_root
    collections.get_root = lambda: fixtures
    try:
        cases = []
        for i, (name, fps, tracks, clips) in enumerate(mc.EXPORT):
            raw_clips = []
            for j, (file, track, fields) in enumerate(clips):
                still = fields["kind"] == "image"
                raw_clips.append({"id": f"c{j + 1}", "track_id": track, "asset_id": f"a{j + 1}",
                                  "asset_scope": "collection", "filename": file,
                                  "source_duration": 0.0 if still else _REAL_SFX["probe_duration"](fixtures / file),
                                  **fields})
            raw = {"version": 2, "width": 360, "height": 640, "fps": fps,
                   "tracks": [{"id": tid, "kind": "audio" if tid.startswith("A") else "video", "name": tid}
                              for tid in tracks],
                   "clips": raw_clips}
            timeline = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
            dump = _pure(timeline.model_dump())
            digest = hashlib.sha256(json.dumps(dump, sort_keys=True).encode()).hexdigest()[:16]
            mp4 = f"e{i:02d}.mp4"
            if old.get(mp4) != digest or not (EXPORT_DIR / mp4).exists():
                compose.render(SimpleNamespace(id="export-fixture"), timeline, EXPORT_DIR / mp4, lambda f, m: None)
            cases.append({"name": name, "file": mp4, "hash": digest, "timeline": dump})
        return cases
    finally:
        collections.get_root = real_root


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


def synthetic_outline_fixtures() -> list[dict]:
    """El contorno del sujeto con las funciones REALES de clip_bg.py (normalize_outline,
    outline_params y outline_ffmpeg_steps) para media_cases.OUTLINE. Los pasos van como la
    continuación de la cadena del clip que arma el C++ (outline_source_chain): unidos con «;», sin
    la etiqueta de entrada del primero ni la de salida del último y sin el número de clip."""
    import media_cases as mc
    from app import clip_bg
    cases = []
    for raw in mc.OUTLINE:
        outline = clip_bg.normalize_outline(raw)
        by_height = []
        for h in mc.OUTLINE_HEIGHTS:
            params = clip_bg.outline_params(outline, h)
            steps, out = clip_bg.outline_ffmpeg_steps(outline, h, "src", 0)
            chain = ";".join(steps).removeprefix("[src]").removesuffix(f"[{out}]") if steps else ""
            by_height.append({"height": h, "params": params, "chain": re.sub(r"\[(ol[a-z])0\]", r"[\1]", chain)})
        cases.append({"raw": _nan_safe(raw), "outline": outline, "heights": by_height})
    return _pure(cases)


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


# --- Audio del export (audio_fx, volume_filter, audio_speed_filters, build_command) ----------

def audio_clip_case(ctx: Ctx, name: str, raw_clip: dict) -> dict:
    """Envolventes, volumen, efectos (desde «ain0» con «ax0») y velocidad de un clip."""
    from app import audio_fx, clip_speed
    from app import clip_keyframes as ck
    c = ctx.schemas.TimelineClip.model_validate(raw_clip)
    steps, out = audio_fx.audio_fx_graph(c, "ain0", "ax0")
    return _pure(_nan_safe({
        "name": name, "clip": c.model_dump(),
        "envelopes": {k: ck.ffmpeg_envelope(c, k, 1.0 if k == "volume" else 0.25)
                      for k in ("volume", *audio_fx.AUDIO_FX_IDS)},
        "volume_filter": ck.volume_filter(c), "fx": {"steps": steps, "out": out},
        "speed": clip_speed.audio_speed_filters(c),
    }))


def audio_graph(project, timeline, exists: dict, has_audio: dict) -> list[str]:
    """La parte de audio (lo que va tras «[vout]») del filter_complex REAL de build_command, con
    el material simulado: un archivo por id de clip que existe y tiene audio según los dicts."""
    from app import compose, detect
    saved = (compose._clip_path, compose.media_exists, compose._has_audio, compose._color_untagged, detect.dims)
    compose._clip_path = lambda project, clip, shape_files=None: Path("C:/fake") / clip.id
    compose.media_exists = lambda p: p is not None and exists.get(Path(p).name, True)
    compose._has_audio = lambda p: has_audio.get(Path(p).name, True)
    compose._color_untagged = lambda p: False
    detect.dims = lambda p: (1920, 1080)
    try:
        cmd = compose.build_command(project, timeline, Path("out.mp4"))
    finally:
        compose._clip_path, compose.media_exists, compose._has_audio, compose._color_untagged, detect.dims = saved
    graph = cmd[cmd.index("-filter_complex") + 1].split(";")
    vout = next(i for i, step in enumerate(graph) if step.endswith("[vout]"))
    return graph[vout + 1:]


def synthetic_audio_fixtures(ctx: Ctx) -> dict:
    from types import SimpleNamespace
    from app import audio_fx, clip_speed
    sys.path.insert(0, str(HERE))
    import audio_cases as ac
    timelines = []
    for name, tracks, clips, missing, silent in ac.TIMELINES:
        for target in ac.TARGETS.get(name, [None]):
            raw = {"version": 2, "width": 1080, "height": 1920, "fps": 30, "tracks": tracks,
                   "clips": [{"asset_id": c["id"], **c} for c in clips]}
            if target is not None:
                raw["audio_target_db"] = target
            tl = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
            exists = {c["id"]: c["id"] not in missing for c in clips}
            has_audio = {c["id"]: c["id"] not in silent for c in clips}
            timelines.append({"name": name, "timeline": _pure(tl.model_dump()), "exists": exists,
                              "has_audio": has_audio,
                              "graph": audio_graph(SimpleNamespace(id="audio-fixture"), tl, exists, has_audio)})
    nodes = [audio_fx.ffmpeg_node(nd) for fx in audio_fx.AUDIO_FX for stage in fx["stages"] for nd in stage]
    return _pure(_nan_safe({
        "clips": [audio_clip_case(ctx, c["name"], c["clip"]) for c in ac.CLIPS],
        "catalog": [{"id": f["id"], "label": f["label"], "group": f["group"]} for f in audio_fx.AUDIO_FX],
        "nodes": nodes,
        "atempo": [{"speed": s, "chain": clip_speed.atempo_chain(s)} for s in ac.ATEMPO],
        "intensity": [{"value": v, "k": audio_fx.fx_intensity(v)} for v in ac.INTENSITY],
        "weights": [{"k": float(k), "n": n, "w": audio_fx.stage_weights(float(k), n)} for k in ac.WEIGHTS_K
                    for n in (1, 3)],
        "timelines": timelines,
    }))


def real_audio_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus clips (los que no son texto) y el grafo de audio de tus timelines, con lo que el export
    REAL ve de cada material: si existe (_clip_path + media_exists) y si tiene audio (_has_audio)."""
    from app import compose
    clips, timelines = [], []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        exists, has_audio = {}, {}
        for c in proj.timeline.clips:
            path = compose._clip_path(proj, c)
            exists[c.id] = bool(compose.media_exists(path))
            has_audio[c.id] = bool(exists[c.id] and c.kind != "text" and compose._has_audio(path))
            if c.kind != "text":
                clips.append(audio_clip_case(ctx, f"{proj.id} · {c.id}", c.model_dump()))
        try:
            graph = audio_graph(proj, proj.timeline, exists, has_audio)
        except RuntimeError:  # «La timeline está vacía»: el export no la acepta
            continue
        timelines.append({"name": f"{proj.id} · {proj.name}", "timeline": _pure(proj.timeline.model_dump()),
                          "exists": exists, "has_audio": has_audio, "graph": graph})
    return {"clips": clips, "catalog": [], "nodes": [], "atempo": [], "intensity": [], "weights": [],
            "timelines": timelines}


def _command_inputs(cmd: list[str]) -> list[list[str]]:
    """Los argumentos de cada entrada de un comando de build_command («ffmpeg -y <entradas>
    -filter_complex …»): cada grupo acaba en «-i ruta»."""
    toks = cmd[2:cmd.index("-filter_complex")]
    groups, cur, j = [], [], 0
    while j < len(toks):
        if toks[j] == "-i":
            groups.append([*cur, "-i", toks[j + 1]])
            cur, j = [], j + 2
            continue
        cur.append(toks[j])
        j += 1
    return groups


def _audio_reference(inputs: list[list[str]], graph: list[str], total: str, block: int) -> dict:
    """El audio que da el CLI con esas entradas y ese grafo, como lo saca el export (`-map [aout]
    -t total -ar 48000`), en float32: muestras por canal, MD5 y RMS de cada canal por bloques."""
    import array
    import hashlib
    import math
    import tempfile
    ffmpeg = shutil.which("ffmpeg")
    with tempfile.TemporaryDirectory() as td:
        raw = Path(td) / "mix.f32"
        subprocess.run([ffmpeg, "-y", "-hide_banner", "-loglevel", "error", *[t for g in inputs for t in g],
                        "-filter_complex", ";".join(graph), "-map", "[aout]", "-t", total, "-ar", "48000",
                        "-c:a", "pcm_f32le", "-f", "f32le", str(raw)], check=True)
        data = raw.read_bytes()
    pcm = array.array("f")
    pcm.frombytes(data)
    n = len(pcm) // 2
    rms = []
    for b in range(0, n, block):
        e = min(n, b + block)
        rms.append([math.sqrt(sum(pcm[2 * i + c] ** 2 for i in range(b, e)) / (e - b)) for c in (0, 1)])
    return {"samples": n, "md5": hashlib.md5(data).hexdigest(), "block": block, "rms": rms}


def _recorded_mono(call, path: Path):
    """Ejecuta `call()` (beats.detect_beats o clip_motion.sample_rms_envelope REALES) grabando lo
    que devuelve el ffmpeg del PATH: el comando (con la ruta como el nombre del archivo), el número
    de muestras, su MD5 y el RMS por décimas de segundo. Si falla, el tipo de la excepción."""
    import array
    import hashlib
    import math
    real_run = subprocess.run
    runs: list = []

    def recording_run(cmd, *args, **kwargs):
        r = real_run(cmd, *args, **kwargs)
        runs.append((cmd, r))
        return r

    subprocess.run = recording_run
    try:
        try:
            out = {"out": call()}
        except Exception as e:  # noqa: BLE001
            out = {"error": type(e).__name__}
    finally:
        subprocess.run = real_run
    if not runs:
        return out
    cmd, r = runs[-1]
    rate = int(cmd[cmd.index("-ar") + 1])
    rec = {"cmd": [path.name if a == str(path) else a for a in cmd[1:]], "returncode": r.returncode}
    if r.returncode == 0:
        data = r.stdout or b""
        pcm = array.array("f")
        pcm.frombytes(data[: len(data) // 4 * 4])
        block = rate // 10
        rec.update({"samples": len(pcm), "md5": hashlib.md5(data).hexdigest(), "block": block,
                    "rms": [math.sqrt(sum(v * v for v in pcm[b:b + block]) / len(pcm[b:b + block]))
                            for b in range(0, len(pcm), block)]})
    return {**rec, **out}


def synthetic_audio_mono_fixtures(ctx: Ctx) -> dict:
    """media_cases.MONO_BEATS y MONO_RMS: beats.detect_beats y clip_motion.sample_rms_envelope
    REALES sobre tests/fixtures/media/files, con el audio mono que les da el ffmpeg del PATH."""
    import media_cases as mc
    from app import beats
    from app import clip_motion as cm
    _ensure_probe_media()
    beats._cache.clear()
    out = {"beats": [], "rms": []}
    for name in mc.MONO_BEATS:
        path = PROBE_DIR / name
        out["beats"].append({"file": name, **_recorded_mono(lambda p=path: beats.detect_beats(p), path)})
    for name, src, dur, rate in mc.MONO_RMS:
        path = PROBE_DIR / name
        rec = _recorded_mono(lambda p=path, s=src, d=dur, r=rate: [list(q) for q in cm.sample_rms_envelope(p, s, d, r)],
                             path)
        out["rms"].append({"file": name, "src_start": src, "duration": dur, "rate": rate, **rec})
    return _pure(out)


def real_audio_mono_fixtures(ctx: Ctx, data: dict, max_files: int = 40) -> dict:
    """Tus audios: los beats de cada archivo de audio o vídeo con audio de tus timelines (como el
    «Detectar beats» del clip) y la envolvente de la ventana de cada clip que se solapa con otro
    (como animate_clip con follow_audio_id), con el ffmpeg REAL. Rutas absolutas: es local."""
    from app import beats
    from app import clip_motion as cm
    from app import compose
    beats._cache.clear()
    out = {"beats": [], "rms": []}
    seen: set = set()
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        for a in proj.timeline.clips:
            if a.kind not in ("audio", "video"):
                continue
            path = compose._clip_path(proj, a)
            if path is None or not path.exists():
                continue
            if str(path) not in seen and len(out["beats"]) < max_files:
                seen.add(str(path))
                rec = _recorded_mono(lambda q=path: beats.detect_beats(q), path)
                out["beats"].append({"name": f"{proj.id} · {a.id}", "path": str(path), **rec})
            visual = next((v for v in proj.timeline.clips if v.id != a.id and cm.overlap_window(v, a)), None)
            if visual is None or len(out["rms"]) >= max_files:
                continue
            lo, hi = cm.overlap_window(visual, a)
            if hi - lo > 30:
                continue
            src = cm.audio_src_at(a, lo)
            rec = _recorded_mono(lambda q=path, s=src, d=hi - lo: [list(x) for x in cm.sample_rms_envelope(q, s, d)], path)
            out["rms"].append({"name": f"{proj.id} · {a.id}", "path": str(path), "src_start": src, "duration": hi - lo,
                               "rate": 20, **rec})
    return _pure(out)


def synthetic_audio_mix_fixtures(ctx: Ctx) -> list[dict]:
    """media_cases.AUDIO_MIX: las entradas y el grafo de audio del build_command REAL (con el
    material de tests/fixtures/media/files) y el audio que da el CLI con ellos."""
    import media_cases as mc
    from types import SimpleNamespace
    from app import compose
    _ensure_probe_media()
    real_clip_path = compose._clip_path
    compose._clip_path = lambda project, clip, shape_files=None: PROBE_DIR / clip.filename
    try:
        cases = []
        for name, clips, target in mc.AUDIO_MIX:
            raw = {"version": 2, "width": 360, "height": 640, "fps": 30, "tracks": mc._AT,
                   "clips": [{"id": f"c{i + 1}", "asset_id": f"a{i + 1}",
                              "source_duration": _REAL_SFX["probe_duration"](PROBE_DIR / c["filename"]), **c}
                             for i, c in enumerate(clips)]}
            if target is not None:
                raw["audio_target_db"] = target
            tl = ctx.schemas.Timeline.model_validate(ctx.migrations.migrate_timeline(raw))
            cmd = compose.build_command(SimpleNamespace(id="audio-mix"), tl, Path("out.mp4"))
            inputs = _command_inputs(cmd)
            graph = cmd[cmd.index("-filter_complex") + 1].split(";")
            graph = graph[next(i for i, st in enumerate(graph) if st.endswith("[vout]")) + 1:]
            total = cmd[len(cmd) - 1 - cmd[::-1].index("-t") + 1]
            ref = _audio_reference(inputs, graph, total, 4800)
            # Las rutas, relativas a tests/fixtures/media/files.
            portable = [[Path(t).name if k > 0 and g[k - 1] == "-i" else t for k, t in enumerate(g)] for g in inputs]
            cases.append({"name": name, "timeline": _pure(tl.model_dump()), "inputs": portable, "graph": graph,
                          "total": float(total), **ref})
        return cases
    finally:
        compose._clip_path = real_clip_path


def real_audio_mix_fixtures(ctx: Ctx, data: dict) -> list[dict]:
    """Tus timelines con audio: las entradas y el grafo del build_command REAL y el audio del CLI
    (RMS por segundo)."""
    from app import compose
    cases = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        try:
            cmd = compose.build_command(proj, proj.timeline, Path("out.mp4"))
        except RuntimeError:  # «La timeline está vacía»
            continue
        graph = cmd[cmd.index("-filter_complex") + 1].split(";")
        graph = graph[next(i for i, st in enumerate(graph) if st.endswith("[vout]")) + 1:]
        if not graph:
            continue
        inputs = _command_inputs(cmd)
        total = cmd[len(cmd) - 1 - cmd[::-1].index("-t") + 1]
        cases.append({"name": f"{proj.id} · {proj.name}", "inputs": inputs, "graph": graph, "total": float(total),
                      **_audio_reference(inputs, graph, total, 48000)})
    return cases


# --- Evaluación por clip (fx_anims, clip_fx, clip_blend, clip_adjust) ------------------------

def synthetic_model_manager_fixtures() -> dict:
    """model_manager.py REAL (modelos de IA en RAM): cada caso de model_manager_cases.CASES paso a
    paso, con su reloj falso (`_set_clock`), sin el hilo que descarga y con `settings.load`
    sustituido; tras cada paso, lo que devuelve y las descargas que hubo. Y idle_minutes() con cada
    uno de model_manager_cases.SETTINGS."""
    import logging
    sys.path.insert(0, str(HERE))
    import model_manager_cases as mc
    from app import model_manager as mm
    from app import settings as st
    real_load, real_reaper, real_now = st.load, mm._ensure_reaper, mm._now
    logger = logging.getLogger(mm.__name__)
    real_level = logger.level
    logger.setLevel(logging.CRITICAL)  # el log.exception de la descarga que falla, fuera
    mm._ensure_reaper = lambda: None
    cases, settings_out = [], []
    try:
        for name, ops in mc.CASES:
            mm._models.clear()
            clock = [0.0]
            mm._set_clock(lambda: clock[0])
            current: list = [{}]
            st.load = lambda: current[0]
            events: list[str] = []
            stack = []

            def unloader(n: str, boom: bool):
                def f():
                    events.append(n)
                    if boom:
                        raise RuntimeError("la descarga falla")
                return f

            results = []
            for op in ops:
                events.clear()
                kind, ret = op[0], None
                if kind == "clock":
                    clock[0] = float(op[1])
                elif kind == "settings":
                    current[0] = op[1]
                elif kind in ("register", "register_boom"):
                    mm.register(op[1], unloader(op[1], kind == "register_boom"), heavy=op[2])
                elif kind == "touch":
                    mm.touch(op[1])
                elif kind == "forget":
                    mm.forget(op[1])
                elif kind == "unload":
                    ret = mm.unload(op[1])
                elif kind == "unload_all":
                    ret = mm.unload_all()
                elif kind == "unload_idle":
                    ret = mm.unload_idle(None if op[1] is None else float(op[1]))
                elif kind == "enter":
                    cm = mm.in_use(op[1])
                    cm.__enter__()
                    stack.append(cm)
                elif kind == "exit":
                    stack.pop().__exit__(None, None, None)
                elif kind == "loaded":
                    ret = mm.loaded()
                elif kind == "idle_minutes":
                    ret = mm.idle_minutes()
                elif kind == "report":
                    r = mm.memory_report()
                    ret = {"idle_minutes": r["idle_minutes"], "models": r["models"]}
                else:
                    raise ValueError(f"paso desconocido: {op}")
                results.append({"ret": ret, "events": list(events)})
            while stack:
                stack.pop().__exit__(None, None, None)
            cases.append({"name": name, "ops": ops, "results": results})
        for name, s in mc.SETTINGS:
            st.load = lambda s=s: s
            settings_out.append({"name": name, "settings": s, "idle_minutes": mm.idle_minutes()})
    finally:
        st.load, mm._ensure_reaper = real_load, real_reaper
        mm._set_clock(real_now)
        mm._models.clear()
        logger.setLevel(real_level)
    return _nan_safe({"cases": cases, "settings": settings_out})


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


def synthetic_editor_ui_fixtures(ctx: Ctx) -> dict:
    """La timeline del editor (editorModel.js, timelineScale.js, trackRows.js y trackStack.js con Node,
    editor_cases.py), con los clips tal como los deja pydantic."""
    sys.path.insert(0, str(HERE))
    import editor_cases as ec
    cases = [{**c, "timeline": ctx.schemas.Timeline(**c["timeline"]).model_dump()} for c in ec.cases()]
    cases = [{**c, "geo": ec.geo(c["timeline"]["tracks"])} for c in cases]
    results = _node(ctx, "editor_ui_js.mjs", [{"tracks": c["timeline"]["tracks"], "clips": c["timeline"]["clips"],
                                             "markers": c["timeline"]["markers"], "geo": c["geo"], "calls": c["calls"]}
                                            for c in cases])
    return {"cases": [{**c, "results": r} for c, r in zip(cases, results)]}


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


def _text_warp_case(ctx: Ctx, name: str, raw: dict, frames, track_style=None) -> dict:
    """Texto 3D de un clip con las funciones REALES, en cada cuadro (ancho, alto, fps): si va en
    capa propia (_prepare_texts), si es 3D, sus fotogramas y la deformación de text_warp_spec, más
    las piezas sueltas de text3d.py en su primer fotograma."""
    from app import text3d
    from app import text_ass as ta
    from app.clip_blend import clip_blend
    from app.clip_mask import has_mask
    c = ctx.schemas.TimelineClip.model_validate(raw)
    st = ta.effective_text_style(track_style, c.style)
    out = {"name": name, "clip": c.model_dump(), "style": st, "frames": []}
    for w, h, fps in frames:
        warp = ta.text_warp_spec(c, st, w, h, fps)
        posed = ta._posed_style(c, st, 0.0)
        font_px = ta._font_px(posed, h)
        shown = ta.apply_text_case(c.text or "", st.get("text_case"))
        out["frames"].append({
            "w": w, "h": h, "fps": fps,
            "own_layer": has_mask(c) or ta.text_is_3d(c, fps) or clip_blend(c) != "normal",
            "is_3d": ta.text_is_3d(c, fps),
            "text_frames": [list(f) for f in ta._text_frames(c, fps)],
            "focal": text3d.focal_of(h, st.get("perspective")),
            "angles": text3d.angles(posed),
            "half": list(text3d.text_half_extents(posed, shown, w, h, font_px, ta.text_shadow(posed, font_px))),
            "warp": None if warp is None else {
                "region": list(warp["region"]), "frames": warp["frames"],
                "corners": [[[float(clipper_fnum(v)) for v in pt] for pt in cs] for cs in warp["corners"]]},
        })
    return _pure(_nan_safe(out))


def clipper_fnum(v: float) -> str:
    from app import clipper
    return clipper._fnum(v)


def synthetic_text_warp_fixtures(ctx: Ctx) -> dict:
    """Texto 3D (text3d.py y text_warp_spec): los clips de text_cases (TEXT_CLIPS y WARP_CLIPS) en
    los cuadros de WARP_FRAMES, y text3d.plane_project suelto."""
    import text_cases as tc
    from app import text3d
    clips = [_text_warp_case(ctx, c["name"], c["clip"], tc.WARP_FRAMES) for c in tc.TEXT_CLIPS + tc.WARP_CLIPS]
    pts = []
    for rx, ry in [(0, 0), (30, 0), (0, -45), (60, 70), (-75, 75), (90, 0)]:
        for f in (100.0, 640.0, 1e-3):
            for x, y in [(0, 0), (150, -80), (-300, 400), (1e6, 1e6)]:
                pts.append({"cx": 180.0, "cy": 320.0, "rx": rx, "ry": ry, "f": f, "x": x, "y": y,
                            "out": list(text3d.plane_project(180.0, 320.0, rx, ry, f, x, y))})
    return _pure(_nan_safe({"clips": clips, "project": pts}))


def real_text_warp_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus textos: la deformación 3D (y si van en capa propia) con el tamaño y los fps de su
    timeline."""
    clips = []
    for p in data.get("projects", []):
        proj = ctx.projects._project_from_dict(p)
        if proj.timeline is None:
            continue
        tl = proj.timeline
        w, h = int(tl.width or 1080), int(tl.height or 1920)
        frames = [(w - w % 2, h - h % 2, tl.fps or 30)]
        track_style = {t.id: t.style for t in tl.tracks}
        for c in tl.clips:
            if c.kind == "text":
                clips.append(_text_warp_case(ctx, f"{proj.id} · {c.id}", c.model_dump(), frames,
                                             track_style.get(c.track_id)))
    return {"clips": clips}


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


def _width_case(fm, font, bold, em, ti, text, ls) -> list:
    """[em, índice del texto, espaciado, text_width REAL] o, si Python lanza, {"error": tipo} en vez
    del ancho (el round() de un em no finito está fuera del try de text_width)."""
    try:
        return [em, ti, ls, fm.text_width(font, bold, em, text, ls)]
    except Exception as e:  # noqa: BLE001 — basta con saber que Python falla
        return [em, ti, ls, {"error": type(e).__name__}]


def synthetic_text_width_fixtures() -> dict:
    """font_metrics.text_width REAL (Pillow con la maquetación básica de su FreeType) para
    text_cases.WIDTH_*: cada archivo de fuente de FONTS, FONTS_BOLD y BUNDLED_FONTS con la rejilla
    de tamaños, textos y espaciados; los nombres de FONT_NAMES (alias, desconocidos, vacío) con unos
    pocos; tamaños y espaciados raros con Arial; más de MAX_STRING_LENGTH caracteres; y carpetas de
    fuentes de mentira (sin fuentes, o con archivos vacíos, rotos o cortados: lo que Pillow no abre
    lo busca por su nombre en WINDIR\\fonts y, si tampoco, text_width estima). Cada archivo de
    fuente del sistema o de la app va con su tamaño y su CRC-32 (y lo que encuentra esa búsqueda,
    en "fallback"): si en otro equipo no es lo mismo, el test lo dice."""
    import tempfile
    import zlib
    import text_cases as tc
    from PIL import Image, features
    from app import font_metrics as fm

    anton = (REPO / "resources" / "fonts" / "Anton-Regular.ttf").read_bytes()
    assert anton == (fm.BUNDLED_FONT_DIR / "Anton-Regular.ttf").read_bytes(), "Anton de resources/fonts ≠ video-yt"
    long_text = tc.WIDTH_LONG["repeat"] * tc.WIDTH_LONG["times"]
    plain = list(dict.fromkeys([*tc.WIDTH_TEXTS, *tc.WIDTH_ODD_TEXTS, *tc.WIDTH_DIR_TEXTS]))
    texts = [*plain, tc.WIDTH_LONG]
    index = {t: i for i, t in enumerate(plain)}
    files: dict[str, dict] = {}

    def rel(p: Path) -> str:
        """La ruta de resolve_font_path relativa a su carpeta: system/… o bundled/…."""
        for prefix, base in (("bundled", fm.BUNDLED_FONT_DIR), ("system", fm.FONT_DIR)):
            if p.parent == base:
                if p.exists() and f"{prefix}/{p.name}" not in files:
                    data = p.read_bytes()
                    files[f"{prefix}/{p.name}"] = {"size": len(data), "crc32": zlib.crc32(data)}
                return f"{prefix}/{p.name}"
        raise AssertionError(f"fuente fuera de las carpetas: {p}")

    def group(name, bold, widths) -> dict:
        return {"font": name, "bold": bold, "file": rel(fm.resolve_font_path(name or "Arial", bold)), "widths": widths}

    fm._pil_font.cache_clear()
    out_files, aliases, odd = [], [], []
    seen: set[Path] = set()
    for name in sorted({*fm.FONTS, *fm.FONTS_BOLD, *fm.BUNDLED_FONTS}):
        for bold in (False, True):
            p = fm.resolve_font_path(name, bold)
            if p in seen:
                continue
            seen.add(p)
            widths = [_width_case(fm, name, bold, em, index[t], t, 0.0) for em in tc.WIDTH_EMS for t in tc.WIDTH_GRID_TEXTS]
            widths += [_width_case(fm, name, bold, em, index[t], t, ls) for em in tc.WIDTH_SPOT_EMS
                       for t in tc.WIDTH_TEXTS for ls in tc.WIDTH_SPACINGS
                       if not (ls == 0.0 and t in tc.WIDTH_GRID_TEXTS)]
            out_files.append(group(name, bold, widths))
    for name in tc.FONT_NAMES:
        for bold in (False, True):
            aliases.append(group(name, bold, [_width_case(fm, name, bold, 46.375, index[t], t, 0.0)
                                              for t in ("Hola mundo", "AVAVAVAV WAWAWA")]))
    for bold in (False, True):
        widths = [_width_case(fm, "Arial", bold, em, index[t], t, 0.0) for em in tc.WIDTH_ODD_EMS for t in tc.WIDTH_ODD_TEXTS]
        widths += [_width_case(fm, "Arial", bold, 46.0, index[t], t, ls) for ls in tc.WIDTH_ODD_SPACINGS
                   for t in tc.WIDTH_ODD_TEXTS]
        widths += [_width_case(fm, "Arial", bold, 46.0, len(texts) - 1, long_text, ls) for ls in (0.0, 1.5)]
        odd.append(group("Arial", bold, widths))

    # Carpetas de mentira: los archivos de cada una, con FONT_DIR y BUNDLED_FONT_DIR cambiadas.
    ttf = dict(tc.FONT_FILES)
    dirs = []
    saved = fm.FONT_DIR, fm.BUNDLED_FONT_DIR
    with tempfile.TemporaryDirectory() as tmp:
        try:
            for i, d in enumerate(tc.WIDTH_DIRS):
                root = Path(tmp) / f"d{i}"
                spec = {}
                for sub in ("system", "bundled"):
                    (root / sub).mkdir(parents=True)
                    spec[sub] = {}
                    for fname, recipe in d[sub].items():
                        if "anton" in recipe:
                            data = anton if recipe["anton"] is None else anton[:recipe["anton"]]
                            spec[sub][fname] = {"anton": recipe["anton"]}
                        else:
                            data = ttf[recipe["ttf"]] if "ttf" in recipe else bytes.fromhex(recipe["hex"])
                            spec[sub][fname] = {"hex": data.hex()}
                        (root / sub / fname).write_bytes(data)
                fm.FONT_DIR, fm.BUNDLED_FONT_DIR = root / "system", root / "bundled"
                fm._pil_font.cache_clear()
                cases = []
                for name, bold in tc.WIDTH_DIR_FONTS:
                    p = fm.resolve_font_path(name, bold)
                    widths = [_width_case(fm, name, bold, em, index[t], t, 0.0) for em in tc.WIDTH_DIR_EMS
                              for t in tc.WIDTH_DIR_TEXTS]
                    cases.append({"font": name, "bold": bold, "file": f"{p.parent.name}/{p.name}", "widths": widths})
                dirs.append({"name": d["name"], **spec, "cases": cases})
        finally:
            fm.FONT_DIR, fm.BUNDLED_FONT_DIR = saved
            fm._pil_font.cache_clear()
    # Lo que no abre, ImageFont.truetype lo busca por su nombre en WINDIR\fonts (con os.walk) y mide
    # con eso: lo que encuentra aquí para los archivos de las carpetas de mentira.
    import os
    windir = os.environ.get("WINDIR")
    fallback = {}
    for d in dirs:
        for c in d["cases"]:
            name = c["file"].split("/", 1)[1]
            if name in fallback:
                continue
            fallback[name] = None
            for walkroot, _walkdirs, walkfiles in (os.walk(os.path.join(windir, "fonts")) if windir else ()):
                if name in walkfiles:
                    data = (Path(walkroot) / name).read_bytes()
                    rel_dir = os.path.relpath(walkroot, os.path.join(windir, "fonts"))
                    fallback[name] = {"path": Path(rel_dir, name).as_posix(), "size": len(data), "crc32": zlib.crc32(data)}
                    break
    return _nan_safe({
        "pillow": Image.__version__, "freetype": features.version("freetype2"), "raqm": features.check("raqm"),
        "system_dir": fm.FONT_DIR.as_posix(), "files": files, "texts": texts,
        "fonts": out_files, "aliases": aliases, "odd": odd, "dirs": dirs, "fallback": fallback,
    })


def real_text_width_fixtures(ctx: Ctx, data: dict) -> dict:
    """Tus textos con la medida REAL de Pillow: el reparto en líneas (_rows) y las cajas de Fondo
    (box_rects, con el ass_size_factor real) de cada clip de texto con su estilo efectivo y su
    texto en May./min., en el cuadro de su timeline, con cada text_width que hacen. Sin repetidos."""
    from app import font_metrics as fm
    from app import text_ass as ta
    calls: list = []

    def recording(font, bold, em, text, ls=0.0):
        out = fm.text_width(font, bold, em, text, ls)
        calls.append([font, bold, em, text, ls, out])
        return out

    saved = ta.text_width
    ta.text_width = recording
    seen, cases = set(), []
    try:
        for p in data.get("projects", []):
            tl = p.get("timeline") or {}
            w, h = int(tl.get("width") or 1080), int(tl.get("height") or 1920)
            track_style = {t.get("id"): t.get("style") for t in tl.get("tracks", [])}
            for c in tl.get("clips", []):
                if c.get("kind") != "text":
                    continue
                st = ta.effective_text_style(track_style.get(c.get("track_id")), c.get("style"))
                text = ta.apply_text_case(c.get("text") or "", st.get("text_case"))
                key = json.dumps([st, w, h, text], sort_keys=True)
                if key in seen:
                    continue
                seen.add(key)
                calls.clear()
                rows = ta._rows(st, w, h, text)
                boxes = [list(r) for r in ta.box_rects(st, w, h, rows)]
                cases.append({"style": st, "w": w, "h": h, "text": text, "rows": rows, "box_rects": boxes,
                              "calls": list(calls)})
    finally:
        ta.text_width = saved
    return _pure(_nan_safe({"system_dir": fm.FONT_DIR.as_posix(), "cases": cases}))


def synthetic_bg_matte_fixtures(ctx: Ctx) -> dict:
    """«Eliminar fondo» con el matte de la caché: build_clip_bg_mask y ensure_derived REALES
    (bg/service.py) sobre las cachés sintéticas de bg_cases.MATTES, en una carpeta temporal. Para
    cada clip de SPEC_CLIPS, lo que da (o None, o el error) y, de cada fotograma derivado del tramo,
    de qué fotograma crudo sale (None si falta: la secuencia PNG del export se corta ahí), con el
    nivel 2 vacío. Y la LUT de clip_bg.matte_lut para LUT_CASES. El C++ rehace la caché con los
    nombres de archivo y el meta."""
    import cv2
    import numpy as np
    import bg_cases as bc
    from app import clip_bg
    from app.bg import service as bg_service
    saved = bg_service.MATTE_ROOT, bg_service.MASK_ROOT
    clips, mattes = [], {}
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        bc.make_matte_cache(root, bc.MATTES)
        for key in bc.MATTES:
            folder = root / "matte" / key
            meta = folder / "meta.json"
            mattes[key] = {"files": sorted(int(f.stem) - 1 for f in folder.glob("*.png")),
                           "meta": meta.read_text(encoding="utf-8") if meta.exists() else None}
        bg_service.MATTE_ROOT = root / "matte"
        try:
            for i, (name, raw) in enumerate(bc.SPEC_CLIPS):
                # Cada clip con su nivel 2 vacío: ensure_derived reutiliza lo que ya hubiera en
                # mask/<derive_key>, y así cada caso es lo que pasa la primera vez (lo que hace el C++).
                bg_service.MASK_ROOT = root / f"mask{i}"
                clip = ctx.schemas.TimelineClip.model_validate(raw)
                case = {"name": name, "clip": _pure(clip.model_dump())}
                try:
                    spec = bg_service.build_clip_bg_mask(clip, 30)
                except Exception as e:  # noqa: BLE001 — basta con saber que Python falla
                    clips.append({**case, "error": type(e).__name__})
                    continue
                if spec is None:
                    clips.append({**case, "spec": None})
                    continue
                bg = clip_bg.clip_bg(clip)
                auto = bg["auto"]
                # El atajo de derive_matte: sin pluma, expansión, opacidad ni pincel, el derivado es la LUT.
                lut_only = (float(auto["feather"]) <= 0.0 and abs(float(auto.get("expansion", 0.0))) < 1e-4
                            and float(auto.get("opacity", 1.0)) >= 1.0
                            and (clip_bg.is_interactive_provider(auto.get("provider")) or not auto["edits"]))
                case["spec"] = {k: v for k, v in spec.items() if k != "path"}
                case["lut_only"] = lut_only
                if not lut_only:
                    clips.append(case)
                    continue
                lut = clip_bg.matte_lut(auto)
                raws = {int(f.stem) - 1: cv2.LUT(cv2.imread(str(f), cv2.IMREAD_GRAYSCALE), lut)
                        for f in (root / "matte" / auto["base_key"]).glob("*.png")}
                folder = Path(spec["path"]).parent
                sources = []
                for idx in range(spec["start_number"] - 1, spec["start_number"] - 1 + spec["frames"]):
                    f = folder / f"{idx + 1:06d}.png"
                    if not f.exists():
                        sources.append(None)
                        continue
                    d = cv2.imread(str(f), cv2.IMREAD_GRAYSCALE)
                    hits = [j for j, r in raws.items() if r.shape == d.shape and np.array_equal(r, d)]
                    assert len(hits) == 1, (name, idx, hits)
                    sources.append(hits[0])
                clips.append({**case, "sources": sources})
        finally:
            bg_service.MATTE_ROOT, bg_service.MASK_ROOT = saved
    luts = []
    for raw in bc.LUT_CASES:
        auto = clip_bg.normalize_auto(raw)
        luts.append({"auto": raw, "lut": [int(v) for v in clip_bg.matte_lut(auto)]})
    # derive_matte REAL más allá de la LUT: el crudo y el derivado, en gris de 8 bits (base64).
    import base64
    derive = []
    for name, (w, h), shape, raw in bc.DERIVE_CASES:
        auto = clip_bg.normalize_auto({"enabled": True, "base_key": "k", "status": "ready", **raw})
        src = bc.matte_frame(w, h, 3, 8, shape)
        out = clip_bg.derive_matte(src, auto)
        assert out.dtype == np.uint8 and out.shape == src.shape, name
        derive.append({"name": name, "auto": raw, "width": w, "height": h,
                       "raw": base64.b64encode(src.tobytes()).decode("ascii"),
                       "derived": base64.b64encode(out.tobytes()).decode("ascii")})
    return _pure(_nan_safe({"mattes": mattes, "clips": clips, "lut": luts, "derive": derive}))


def real_bg_derive_fixtures(matte_root: Path) -> dict:
    """derive_matte REAL (clip_bg.py) sobre tus mattes de la caché de video-yt (solo se leen): el
    fotograma del medio de un conjunto por cada proveedor y tamaño, con los ajustes de
    bg_cases.DERIVE_REAL_AUTOS. El crudo y cada derivado van en gris de 8 bits, sin cabecera, a
    tests/fixtures/local/bg/derive/ (no se suben)."""
    import cv2
    import bg_cases as bc
    from app import clip_bg
    out_dir = REPO / "tests" / "fixtures" / "local" / "bg" / "derive"
    out_dir.mkdir(parents=True, exist_ok=True)
    picked: dict[tuple, Path] = {}
    for folder in sorted(p for p in matte_root.iterdir() if p.is_dir()):
        meta = folder / "meta.json"
        frames = sorted(folder.glob("*.png"))
        if not meta.exists() or not frames:
            continue
        m = json.loads(meta.read_text(encoding="utf-8"))
        key = (m.get("provider"), m.get("width"), m.get("height"))
        picked.setdefault(key, frames[len(frames) // 2])
    cases = []
    for (provider, _w, _h), frame in sorted(picked.items(), key=lambda kv: str(kv[0])):
        raw = cv2.imread(str(frame), cv2.IMREAD_GRAYSCALE)
        h, w = raw.shape
        stem = f"{frame.parent.name[:10]}_{frame.stem}"
        (out_dir / f"{stem}.gray").write_bytes(raw.tobytes())
        for name, extra in bc.DERIVE_REAL_AUTOS:
            auto = clip_bg.normalize_auto({"enabled": True, "base_key": "k", "status": "ready", "provider": provider,
                                           **extra})
            der = clip_bg.derive_matte(raw, auto)
            dname = f"{stem}_{len(cases)}.gray"
            (out_dir / dname).write_bytes(der.tobytes())
            cases.append({"name": f"{provider} {w}×{h}: {name}", "width": w, "height": h, "auto": auto,
                          "raw": f"{stem}.gray", "derived": dname})
    return _pure(_nan_safe({"cases": cases}))


# --- Servidor MCP sin red (mcp_server: dto, capabilities, help_content, registry, resources, audit)

class _FakeMcp:
    """Lo que registry.tool y resources.register piden al MCPServer: guarda lo que se registra."""

    def __init__(self):
        self.tools: list[dict] = []
        self.resources: list[tuple[str, dict, object]] = []

    def tool(self, name=None, meta=None, annotations=None, **kwargs):
        def deco(fn):
            self.tools.append({"name": name, "meta": meta, "kwargs": sorted(kwargs),
                               "annotations": annotations.model_dump(by_alias=True, exclude_none=True)})
            return fn
        return deco

    def resource(self, uri, **kwargs):
        def deco(fn):
            self.resources.append((uri, kwargs, fn))
            return fn
        return deco


class _Patches:
    """Atributos sustituidos durante un bloque y restaurados al salir (aunque falle)."""

    def __init__(self):
        self.saved: list[tuple[object, str, object]] = []

    def set(self, obj, name: str, value) -> None:
        self.saved.append((obj, name, getattr(obj, name)))
        setattr(obj, name, value)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        for obj, name, value in reversed(self.saved):
            setattr(obj, name, value)
        self.saved.clear()


def _mcp_env(p: _Patches, env: dict) -> None:
    """Lo que capabilities.py pregunta a la máquina y a los ajustes, con los valores de `env` (la
    entrada que recibe gc_core ya resuelta): modelo de transcripción, motores TTS, categorías de
    SFX, ffmpeg/ffprobe en el PATH, data/, GPU y proveedor de IA."""
    import shutil as shutil_mod
    from app import config, gpu, transcribe_settings
    from app.ai import providers
    from app.mcp_server import capabilities
    p.set(capabilities, "tts_engines", lambda: copy.deepcopy(env["tts_engines"]))
    p.set(capabilities, "sfx_categories", lambda: list(env["sfx_categories"]))
    p.set(transcribe_settings, "load", lambda: {"model": env["transcribe_model"]})
    p.set(shutil_mod, "which", lambda name, *a, **k: f"C:\\bin\\{name}.exe" if env.get(name) else None)
    p.set(config, "DATA_DIR", Path(env["data_dir"]))
    p.set(gpu, "summary", lambda: copy.deepcopy(env["gpu"]))
    p.set(providers, "ai_config", lambda: copy.deepcopy(env["ai_provider"]))


def _exc_text(e: Exception) -> str:
    return f"{type(e).__name__}: {e}"


def _mcp_dto_fixtures(ctx: Ctx, mc) -> dict:
    """dto.py: el DTO semántico de cada proyecto (con cada historial resumido), su timeline en
    detalle, el material, cada clip completo; aspect_ratio, analyze_dto y job_dto."""
    from app.mcp_server import dto
    projects = []
    for name, raw, _hist in mc.PROJECTS:
        proj = ctx.projects._project_from_dict(copy.deepcopy(raw))
        tl = proj.timeline
        case = {"name": name, "input": raw,
                "project_context": [_pure(dto.project_context(proj, history=copy.deepcopy(h)))
                                    for h in mc.CONTEXT_HISTORIES],
                "media_list": _pure(dto.media_list(proj)),
                "timeline_dto": _pure(dto._timeline_dto(tl))}
        try:
            case["timeline_detail"] = _pure(dto.timeline_detail(tl))
        except Exception as e:  # noqa: BLE001 — Python también falla aquí: C++ debe fallar
            case["timeline_detail"] = {"error": _exc_text(e)}
        case["clips"] = [{"id": c.id, "timeline_duration": dto._clip_timeline_duration(c),
                          "detail": _pure(dto.clip_detail(c))} for c in (tl.clips if tl else [])]
        projects.append(case)
    analyze = []
    for name, raw in mc.ANALYZE:
        resp = ctx.schemas.AnalyzeResponse.model_validate(copy.deepcopy(raw))
        analyze.append({"name": name, "input": raw, "dumped": resp.model_dump(mode="json"),
                        "expected": _pure(dto.analyze_dto(resp))})
    jobs = []
    for name, raw in mc.JOBS:
        job = ctx.schemas.Job.model_validate(copy.deepcopy(raw))
        jobs.append({"name": name, "input": raw, "dumped": job.model_dump(mode="json"),
                     "expected": _pure(dto.job_dto(job))})
    return {"capabilities": dto.capabilities(),
            "aspect_ratio": [[w, h, dto.aspect_ratio(w, h)] for w, h in mc.ASPECTS],
            "context_histories": mc.CONTEXT_HISTORIES,
            "projects": projects, "analyze": analyze, "jobs": jobs}


def _mcp_capabilities_fixtures(ctx: Ctx, mc) -> dict:
    """capabilities.py: los enums de valores válidos, verbos por dominio y, con cada entorno,
    overview, describe(dominio), transcribe_models y runtime_info; project_summary de cada proyecto."""
    from app.mcp_server import capabilities as cap
    enums = {"ASPECTS": cap.ASPECTS, "MOTIONS": cap.MOTIONS, "DASH_STYLES": cap.DASH_STYLES,
             "TRANSITIONS": cap.TRANSITIONS, "VISUAL_FX": cap.VISUAL_FX, "MASK_TYPES": cap.MASK_TYPES,
             "AUDIO_FX": cap.AUDIO_FX, "LOOKS": cap.LOOKS, "POSITIONS": cap.POSITIONS,
             "REFRAME_MODES": cap.REFRAME_MODES, "TEXT_ROLES": cap.TEXT_ROLES, "TRACK_KINDS": cap.TRACK_KINDS,
             "crop_modes": cap.crop_modes()}
    envs = []
    for name, env in mc.ENVIRONMENTS:
        with _Patches() as p:
            _mcp_env(p, env)
            describe = []
            for q in mc.DOMAIN_QUERIES:
                try:
                    describe.append([q, _pure(cap.describe(q))])
                except ValueError as e:
                    describe.append([q, {"error": str(e)}])
            envs.append({"name": name, "env": env, "overview": _pure(cap.overview()),
                         "transcribe_models": _pure(cap.transcribe_models()),
                         "runtime_info": _pure(cap.runtime_info()), "describe": describe})
    summaries = [{"name": name, "input": raw,
                  "expected": _pure(cap.project_summary(ctx.projects._project_from_dict(copy.deepcopy(raw))))}
                 for name, raw, _hist in mc.PROJECTS]
    return {"enums": enums, "verbs_by_domain": cap.verbs_by_domain(), "domains": cap.domains(),
            "environments": envs, "project_summaries": summaries}


def _mcp_help_fixtures(mc) -> dict:
    """help_content.py: TOOL_DOMAINS y HELP en su orden (textos byte a byte), domains() y guide()."""
    from app.mcp_server import help_content as hc
    return {"tool_domains": [[k, v] for k, v in hc.TOOL_DOMAINS.items()],
            "help": [[k, v] for k, v in hc.HELP.items()],
            "domains": hc.domains(),
            "guide": [[q, hc.guide(q)] for q in mc.DOMAIN_QUERIES]}


def _mcp_registry_fixtures(mc) -> dict:
    """registry.py: niveles de acceso y códigos de error, el payload de MCPError, la clasificación
    de los ValueError sueltos y su ToolError (el JSON que llega al agente), _is_job_dto, el
    registro de tools (meta, anotaciones y errores) y las tools REALES del servidor (server.py)."""
    from app.mcp_server import registry, server  # noqa: F401 — server registra las tools reales
    builtin = [[n, s.access] for n, s in registry.registered().items()]
    saved = registry.registered()
    mcp_errors = []
    for code, message, hint, retryable, param in mc.MCP_ERRORS:
        err = registry.MCPError(code, message, hint=hint, retryable=retryable, param=param)
        mcp_errors.append({"input": [code, message, hint, retryable, param], "payload": err.payload(),
                           "str": str(err), "tool_error": str(registry._to_tool_error(err))})
    value_errors = [{"message": m, "classified": list(registry._classify_value_error(m)),
                     "tool_error": str(registry._to_tool_error(ValueError(m)))} for m in mc.VALUE_ERRORS]
    assert registry._to_tool_error(TypeError("x")) is None  # un crash no es un error de tool
    fake = _FakeMcp()
    registrations = []
    registry.reset()
    try:
        for name, access in mc.REGISTRATIONS:
            try:
                registry.tool(fake, access=access, name=name)(lambda: None)
                registrations.append({"name": name, "access": access, "tool": fake.tools[-1]})
            except ValueError as e:
                registrations.append({"name": name, "access": access, "error": str(e)})
        registered = [[n, s.access] for n, s in registry.registered().items()]
    finally:
        registry.reset()
        registry._registry.update(saved)
    return {"access_levels": list(registry.ACCESS_LEVELS), "error_codes": list(registry.ERROR_CODES),
            "mcp_errors": mcp_errors, "value_errors": value_errors,
            "job_dto_checks": [[v, registry._is_job_dto(copy.deepcopy(v))] for v in mc.JOB_DTO_CHECKS],
            "registrations": registrations, "registered": registered, "builtin_tools": builtin}


def _mcp_resources_fixtures(ctx: Ctx, mc) -> dict:
    """resources.py: el catálogo (uri, mime_type, description) y lo que devuelve cada resource,
    con projects.get_project y timeline_store._load_history sobre los proyectos de los casos."""
    from app import projects as projects_mod, timeline_store
    from app.mcp_server import help_content, resources
    fake = _FakeMcp()
    resources.register(fake)
    handlers = {uri: fn for uri, _kw, fn in fake.resources}
    catalog = [{"uri": uri, "kwargs": kw} for uri, kw, _fn in fake.resources]
    raw_by_id = {raw["id"]: (raw, hist) for _n, raw, hist in mc.PROJECTS}

    def call(uri: str, **params) -> dict:
        try:
            return {"uri": uri, "params": params, "text": handlers[uri](**params)}
        except ValueError as e:
            return {"uri": uri, "params": params, "error": str(e)}
        except Exception as e:  # noqa: BLE001 — un fallo de Python (TypeError…): C++ también debe fallar
            return {"uri": uri, "params": params, "error": _exc_text(e), "crash": True}

    calls, histories = [], []
    for name, env in mc.ENVIRONMENTS:
        with _Patches() as p:
            _mcp_env(p, env)
            calls.append({"env": name, **call("capabilities://index")})
            calls.append({"env": name, **call("config://runtime")})
    for q in [*help_content.domains(), *mc.DOMAIN_QUERIES]:
        calls.append(call("help://{domain}", domain=q))
    with _Patches() as p:
        p.set(projects_mod, "get_project", lambda pid: ctx.projects._project_from_dict(
            copy.deepcopy(raw_by_id[pid][0])) if pid in raw_by_id else None)
        p.set(timeline_store, "_load_history", lambda pid: copy.deepcopy(raw_by_id[pid][1]))
        for pid, (raw, hist) in raw_by_id.items():
            histories.append({"pid": pid, "raw": hist, "summary": resources._history(pid)})
            calls.append(call("project://{pid}", pid=pid))
            calls.append(call("project://{pid}/timeline", pid=pid))
            calls.append(call("project://{pid}/media", pid=pid))
            ids = list(dict.fromkeys(c["id"] for c in (raw.get("timeline") or {}).get("clips", [])))
            for cid in [*ids, "nadie"]:
                calls.append(call("project://{pid}/clip/{cid}", pid=pid, cid=cid))
        for uri in ("project://{pid}", "project://{pid}/timeline", "project://{pid}/media"):
            calls.append(call(uri, pid="no-existe"))
        calls.append(call("project://{pid}/clip/{cid}", pid="no-existe", cid="v1"))
    return {"catalog": catalog, "environments": mc.ENVIRONMENTS,
            "projects": [{"pid": pid, "input": raw} for pid, (raw, _h) in raw_by_id.items()],
            "histories": histories, "calls": calls}


class _FakeAuditClock:
    """datetime de audit.py: now() da el instante fijado (µs desde 1970, en UTC)."""
    us = 0

    @classmethod
    def now(cls, tz=None):
        from datetime import datetime, timedelta, timezone
        return datetime(1970, 1, 1, tzinfo=timezone.utc) + timedelta(microseconds=cls.us)


def _mcp_audit_fixtures(mc) -> dict:
    """audit.py: extract_meta con cada modelo de los ajustes, y los pasos de AUDIT_STEPS sobre un
    data/ temporal (que log crea), con el reloj, uuid4 y jobs.get_job falsos; tras los pasos que
    escriben, el archivo de auditoría tal cual."""
    from app import config, jobs as jobs_mod, transcribe_settings
    from app.mcp_server import audit
    extract = []
    for tool, params, model in mc.EXTRACT_META:
        with _Patches() as p:
            p.set(transcribe_settings, "load", lambda m=model: {"model": m})
            extract.append({"tool": tool, "params": params, "settings_model": model,
                            "meta": _pure(audit.extract_meta(tool, copy.deepcopy(params)))})
    tmp = Path(tempfile.mkdtemp(prefix="gc_mcp_audit_"))
    data_dir = tmp / "nuevo" / "data"
    path = data_dir / "mcp_audit.jsonl"
    fake_jobs: dict = {}

    def get_job(jid):
        spec = fake_jobs.get(jid)
        if spec is None:
            return None
        if spec["status"] in {s.value for s in schemas_mod.JobStatus}:
            return schemas_mod.Job(id=jid, **spec)
        # Un estado que Job no admite (p. ej. «cancelled»): un objeto cualquiera con esos atributos.
        return types.SimpleNamespace(id=jid, progress=spec.get("progress", 0.0),
                                     message=spec.get("message", "En cola…"), status=spec["status"])

    from app import schemas as schemas_mod
    steps = []
    audit.reset_runtime()
    try:
        with _Patches() as p:
            p.set(config, "DATA_DIR", data_dir)
            p.set(uuid, "uuid4", _SequentialUuid())
            p.set(audit, "datetime", _FakeAuditClock)
            p.set(jobs_mod, "get_job", get_job)
            for op in mc.AUDIT_STEPS:
                kind, ret, out = op[0], None, {"op": op}
                try:
                    if kind == "now":
                        _FakeAuditClock.us = op[1]
                    elif kind == "source":
                        audit._source_var.set(op[1])
                    elif kind == "begin":
                        ret = audit.begin(op[1], op[2], project_id=op[3], meta=copy.deepcopy(op[4]))
                    elif kind == "finish":
                        ret = audit.finish(op[1], **copy.deepcopy(op[2]))
                    elif kind == "log":
                        ret = audit.log(op[1], op[2], **copy.deepcopy(op[3]))
                    elif kind == "track_job":
                        audit.track_job(op[1], op[2], op[3], **copy.deepcopy(op[4]))
                    elif kind == "jobs":
                        fake_jobs.clear()
                        fake_jobs.update({k: v for k, v in copy.deepcopy(op[1]).items() if v is not None})
                    elif kind == "active":
                        ret = audit.active(op[1])
                    elif kind == "reset_runtime":
                        audit.reset_runtime()
                    elif kind == "read_all":
                        ret = audit.read_all()
                    elif kind == "read_recent":
                        ret = audit.read_recent(limit=op[1], project_id=op[2])
                    elif kind == "write_raw":
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(op[1].encode("utf-8"))
                    elif kind == "write_hex":
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(bytes.fromhex(op[1]))
                    elif kind == "remove_file":
                        path.unlink()
                    else:
                        raise SystemExit(f"paso desconocido: {op}")
                    out["ret"] = _pure(ret)
                except (ValueError, AttributeError, TypeError, OSError) as e:
                    out["error"] = _exc_text(e)
                if kind in ("finish", "log", "write_raw", "write_hex", "remove_file"):
                    if not path.exists():
                        out["file"] = None
                    else:
                        data = path.read_bytes()
                        try:
                            out["file"] = data.decode("utf-8")
                        except UnicodeDecodeError:
                            out["file_hex"] = data.hex()
                steps.append(out)
            audit._source_var.set(None)
    finally:
        audit.reset_runtime()
        shutil.rmtree(tmp, ignore_errors=True)
    return {"extract_meta": extract, "steps": steps}


def _mcp_tools_read_fixtures(mc) -> dict:
    """Las tools de lectura REALES (tools_capabilities, tools_context y tools_read) sobre un data/
    temporal: projects.json con PROJECTS + TOOL_PROJECTS y el historial de cada uno en
    data/history/<pid>.json. Cada llamada da su resultado o el error (`value_error`: el que llega
    al agente; si no, el servidor lo oculta). describe_capabilities, con el primer entorno."""
    import tempfile

    from app import projects, timeline_store
    from app.mcp_server import tools_capabilities, tools_context, tools_read
    fns = {"describe_capabilities": tools_capabilities.describe_capabilities,
           "list_projects": tools_capabilities.list_projects,
           "resolve_project": tools_capabilities.resolve_project,
           "get_project_context": tools_context.get_project_context,
           "get_timeline": tools_read.get_timeline, "inspect_clip": tools_read.inspect_clip,
           "list_media": tools_read.list_media, "search_transcript": tools_read.search_transcript}
    entries = [[raw, hist] for _n, raw, hist in mc.PROJECTS] + [list(e) for e in mc.TOOL_PROJECTS]
    calls = [list(c) for c in mc.TOOL_CALLS]
    # La timeline y cada clip de los proyectos de dto (todos los tipos, valores raros, beats…).
    for _n, raw, _h in mc.PROJECTS:
        clips = ((raw.get("timeline") or {}).get("clips")) or []
        if clips:
            calls.append(["get_timeline", [raw["id"]]])
            calls += [["inspect_clip", [raw["id"], c["id"]]] for c in clips]
    with tempfile.TemporaryDirectory() as td, _Patches() as p:
        data = Path(td) / "data"
        (data / "history").mkdir(parents=True)
        p.set(projects, "_FILE", data / "projects.json")
        p.set(timeline_store, "HISTORY_DIR", data / "history")
        _mcp_env(p, mc.ENVIRONMENTS[0][1])
        projects._FILE.write_text(json.dumps({"projects": [raw for raw, _h in entries]}, ensure_ascii=False),
                                  encoding="utf-8")
        files: dict[str, object] = {}
        for raw, hist in entries:
            if hist is None or raw["id"] in files:  # un id repetido: vale el historial del primero
                continue
            text = hist[1] if isinstance(hist, list) and hist[:1] == ["raw"] else json.dumps(hist, ensure_ascii=False)
            files[raw["id"]] = text
            (data / "history" / f"{raw['id']}.json").write_text(text, encoding="utf-8")
        out = []
        for name, args in calls:
            case = {"tool": name, "args": args}
            try:
                case["result"] = _pure(fns[name](*args))
            except Exception as e:  # noqa: BLE001 — el C++ debe fallar igual
                case["error"] = str(e)
                case["error_type"] = type(e).__name__
                case["value_error"] = isinstance(e, ValueError)
            out.append(case)
    return {"environment": mc.ENVIRONMENTS[0][1], "projects": [raw for raw, _h in entries],
            "history_files": files, "calls": out}


def _mcp_tools_edit_fixtures(mc) -> dict:
    """Las tools REALES de tools_edit.py paso a paso sobre un data/ temporal, con uuid4 en secuencia.
    Lo que lee el material va sustituido igual que en C++: _probe_duration (2,5 s) y, para seguir un
    audio, compose._clip_path y clip_motion.sample_rms_envelope. Cada paso da su resultado o su error y
    el estado del archivo: la timeline (si cambió) y el tamaño del historial."""
    import tempfile

    from app import clip_motion, compose, fx_catalog, projects, settings, sfx, timeline_store
    from app.mcp_server import tools_edit

    registered: list[list[str]] = []

    def fake_tool(_mcp, access="read", **_kw):
        return lambda fn: registered.append([fn.__name__, access]) or fn

    def envelope(_path, src, dur):
        return [(0.0, round(src / 100, 4)), (round(dur / 2, 3), 1.0), (round(dur, 3), 0.5)]

    def clip_path(_proj, clip, *_a):
        return None if clip.filename == "falta.mp3" else Path(__file__)

    def fill(v, names):
        if isinstance(v, str) and v.startswith("$"):
            return names.get(v[1:], v[1:])
        if isinstance(v, list):
            return [fill(x, names) for x in v]
        if isinstance(v, dict):
            return {k: fill(x, names) for k, x in v.items()}
        return v

    with tempfile.TemporaryDirectory() as td, _Patches() as p:
        data = Path(td) / "data"
        (data / "history").mkdir(parents=True)
        base = Path(td) / "sfx"
        for rel in mc.EDIT_SFX_FILES:
            (base / rel).parent.mkdir(parents=True, exist_ok=True)
            (base / rel).write_bytes(b"x")
        p.set(projects, "_FILE", data / "projects.json")
        p.set(timeline_store, "HISTORY_DIR", data / "history")
        p.set(settings, "_FILE", data / "settings.json")
        p.set(sfx, "get_base", lambda: base)
        p.set(sfx, "resolve", _REAL_SFX.get("resolve", sfx.resolve))  # otras fixtures lo sustituyen
        p.set(tools_edit, "_probe_duration", lambda _path: 2.5)
        p.set(tools_edit, "tool", fake_tool)
        p.set(compose, "_clip_path", clip_path)
        p.set(clip_motion, "sample_rms_envelope", envelope)
        p.set(uuid, "uuid4", _SequentialUuid())
        tools_edit.register(None)
        settings._FILE.write_text(json.dumps(mc.EDIT_SETTINGS), encoding="utf-8")
        projects._FILE.write_text(json.dumps({"projects": mc.EDIT_PROJECTS}, ensure_ascii=False), encoding="utf-8")

        def state(pid: str, last: dict) -> dict:
            raw = next((x for x in json.loads(projects._FILE.read_text(encoding="utf-8"))["projects"]
                        if x["id"] == pid), None)
            out: dict = {}
            tl = raw.get("timeline") if raw else None
            if tl != last.get(pid):
                out["timeline"] = tl
                last[pid] = tl
            hist = data / "history" / f"{pid}.json"
            if hist.exists():
                h = json.loads(hist.read_text(encoding="utf-8"))
                out["history"] = {"undo": len(h["undo"]), "redo": len(h["redo"]),
                                  "checkpoints": sorted(h["checkpoints"])}
            return out

        names: dict[str, str] = {}
        last: dict = {}
        steps = []
        for tool, kwargs, save in mc.EDIT_STEPS:
            args = fill(kwargs, names)
            case: dict = {"tool": tool, "args": args}
            try:
                res = getattr(tools_edit, tool)(**args)
                case["result"] = _pure(res)
                for name, value in zip(save, res.get("changed") or []):
                    names[name] = value
            except Exception as e:  # noqa: BLE001 — el C++ debe fallar igual
                case["error"] = str(e)
                case["error_type"] = type(e).__name__
                case["value_error"] = isinstance(e, ValueError)
                case["param"] = getattr(e, "param", None)
            if "project_id" in args:
                case.update(state(args["project_id"], last))
            steps.append(case)
        p.set(fx_catalog, "catalog", lambda: mc.EDIT_CATALOG)
        catalog_calls = [{"args": a, "result": _pure(tools_edit.list_effects(**a))} for a in mc.EDIT_CATALOG_CALLS]
    return {"settings": mc.EDIT_SETTINGS, "sfx_files": mc.EDIT_SFX_FILES, "projects": mc.EDIT_PROJECTS,
            "registered": registered, "steps": steps, "catalog": mc.EDIT_CATALOG, "catalog_calls": catalog_calls}


def synthetic_mcp_fixtures(ctx: Ctx) -> dict[str, dict]:
    """El núcleo sin red del servidor MCP con los módulos REALES (mcp_cases.py), un archivo por
    módulo de app/mcp_server: dto, capabilities, help, registry, resources y audit."""
    sys.path.insert(0, str(HERE))
    import mcp_cases as mc
    return {"dto": _mcp_dto_fixtures(ctx, mc), "capabilities": _mcp_capabilities_fixtures(ctx, mc),
            "help": _mcp_help_fixtures(mc), "registry": _mcp_registry_fixtures(mc),
            "resources": _mcp_resources_fixtures(ctx, mc), "audit": _mcp_audit_fixtures(mc),
            "tools_read": _mcp_tools_read_fixtures(mc), "tools_edit": _mcp_tools_edit_fixtures(mc)}


# --- Salida -----------------------------------------------------------------------------------

# --- «Importar imagen»: to_working_png con OpenCV (images.py) ------------------------------------

IMAGE_IMPORT_DIR = REPO / "tests" / "fixtures" / "media" / "import"


def synthetic_image_import_fixtures(ctx: Ctx) -> list[dict]:
    """Imágenes de prueba en cada formato (con Pillow y OpenCV) y lo que hace con ellas el
    to_working_png REAL de images.py (cv2.imdecode + imencode): el PNG de trabajo o el error. Los
    originales y los PNG van a tests/fixtures/media/import/. `lossless`: los píxeles deben salir
    exactos."""
    import io

    import cv2
    import numpy as np
    from PIL import Image as PilImage
    from app import images

    rng = np.random.default_rng(7)

    def pattern(w, h, alpha=False):
        y, x = np.mgrid[0:h, 0:w]
        r = (x * 255 // max(1, w - 1)).astype(np.uint8)
        g = (y * 255 // max(1, h - 1)).astype(np.uint8)
        b = ((x + y) * 3 % 256).astype(np.uint8)
        img = np.dstack([r, g, b])
        img[h // 4:h // 2, w // 4:w // 2] = (250, 30, 60)          # un bloque de color puro (bordes de croma)
        img = np.clip(img.astype(np.int16) + rng.integers(-12, 13, img.shape), 0, 255).astype(np.uint8)
        if alpha:
            a = (x * 255 // max(1, w - 1)).astype(np.uint8)
            a[: h // 3] = 255
            a[-h // 5:] = 0
            img = np.dstack([img, a])
        return img                                                    # RGB(A)

    def pil(img):
        return PilImage.fromarray(img, "RGBA" if img.shape[2] == 4 else "RGB")

    def pil_bytes(img, fmt, **kw):
        buf = io.BytesIO()
        img.save(buf, fmt, **kw)
        return buf.getvalue()

    def cv_bytes(img, ext, params=()):
        bgr = cv2.cvtColor(img, cv2.COLOR_RGBA2BGRA if img.shape[2] == 4 else cv2.COLOR_RGB2BGR)
        ok, buf = cv2.imencode(ext, bgr, list(params))
        assert ok, ext
        return bytes(buf)

    photo = pattern(160, 96)
    exif = PilImage.Exif()
    exif[0x0112] = 6                                                  # Orientation: girar 90°
    gray = pil(photo).convert("L")
    cases = [
        ("jpeg_420", ".jpg", cv_bytes(photo, ".jpg", (cv2.IMWRITE_JPEG_QUALITY, 90)), False),
        ("jpeg_444", ".jpg", pil_bytes(pil(photo), "JPEG", quality=92, subsampling=0), False),
        ("jpeg_impar", ".jpg", pil_bytes(pil(pattern(33, 17)), "JPEG", quality=90), False),
        ("jpeg_34x18", ".jpg", pil_bytes(pil(pattern(34, 18)), "JPEG", quality=90), False),
        ("jpeg_33x16", ".jpg", pil_bytes(pil(pattern(33, 16)), "JPEG", quality=90), False),
        ("jpeg_32x17", ".jpg", pil_bytes(pil(pattern(32, 17)), "JPEG", quality=90), False),
        ("jpeg_161x97", ".jpg", pil_bytes(pil(pattern(161, 97)), "JPEG", quality=90), False),
        ("jpeg_gris", ".jpg", pil_bytes(gray, "JPEG", quality=90), False),
        ("jpeg_progresivo", ".jpg", pil_bytes(pil(photo), "JPEG", quality=85, progressive=True), False),
        ("jpeg_exif_girado", ".jpg", pil_bytes(pil(photo), "JPEG", quality=90, exif=exif), False),
        ("webp_con_perdida", ".webp", cv_bytes(photo, ".webp", (cv2.IMWRITE_WEBP_QUALITY, 80)), False),
        ("webp_impar", ".webp", pil_bytes(pil(pattern(33, 17)), "WEBP", quality=80), False),
        ("webp_sin_perdida_alfa", ".webp", pil_bytes(pil(pattern(64, 40, True)), "WEBP", lossless=True), True),
        ("webp_alfa_con_perdida", ".webp", pil_bytes(pil(pattern(64, 40, True)), "WEBP", quality=80), False),
        ("bmp", ".bmp", cv_bytes(photo, ".bmp"), True),
        ("tiff", ".tiff", cv_bytes(photo, ".tiff"), True),
        ("tiff_alfa", ".tiff", pil_bytes(pil(pattern(48, 30, True)), "TIFF", compression="tiff_lzw"), True),
        ("avif", ".avif", pil_bytes(pil(photo), "AVIF", quality=80), False),
        ("jpeg_cortado", ".jpg", cv_bytes(photo, ".jpg")[:400], False),
        ("no_es_imagen", ".jpg", b"esto no es una imagen", False),
        ("video_con_nombre_de_imagen", ".png", (PROBE_DIR / "v_2s.mp4").read_bytes(), False),
    ]
    IMAGE_IMPORT_DIR.mkdir(parents=True, exist_ok=True)
    out = []
    for name, ext, data, lossless in cases:
        (IMAGE_IMPORT_DIR / f"{name}{ext}").write_bytes(data)
        entry = {"name": name, "src": f"media/import/{name}{ext}", "lossless": lossless}
        try:
            png = images.to_working_png(f"{name}{ext}", data)
            (IMAGE_IMPORT_DIR / f"{name}.png").write_bytes(png)
            entry["want"] = f"media/import/{name}.png"
            entry["size"] = list(images.probe_size(IMAGE_IMPORT_DIR / f"{name}.png"))
        except ValueError as e:
            entry["error"] = str(e)
        out.append(entry)
    return out


def write(path: Path, payload, count: int, errors: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    size = path.stat().st_size / 1024
    print(f"{path.relative_to(REPO)}: {count} casos ({errors} de error), {size:,.0f} KB")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--video-yt", type=Path, default=DEFAULT_VIDEO_YT, help="raíz del repo video-yt")
    ap.add_argument("--real", action="store_true", help="exportar también tus proyectos reales (local)")
    ap.add_argument("--solo", choices=["export", "render", "shapes", "texto", "texto3d", "mascaras", "editor",
                                       "sfx_escritura", "beats", "keywords", "mono", "contorno", "proyectos", "ajustes", "heatmap", "url", "vfx", "youtube_video", "fx_bake",
                                       "anchos", "matte", "lru", "plataforma", "imagenes", "decode", "modelos", "mcp"],
                    help="solo ese grupo de fixtures (export: tests/fixtures/export; render: render/synthetic.json; "
                         "decode: media/decode.json y media/seek.json)")
    ap.add_argument("--conservar", action="store_true",
                    help="render: no volver a componer los PNG que ya existen (solo los de escenas nuevas)")
    args = ap.parse_args()

    ctx = Ctx(args.video_yt)
    sys.path.insert(0, str(HERE))
    from model_cases import CASES

    def export_group() -> None:
        exp = synthetic_export_fixtures(ctx)
        write(EXPORT_DIR / "synthetic.json", exp, len(exp), 0)

    if args.solo == "export":
        export_group()
        return

    def decode_group() -> None:
        # Las sondas recorren todo tests/fixtures/media/files: con medios nuevos, también cambian.
        med = synthetic_media_fixtures()
        write(REPO / "tests" / "fixtures" / "media" / "synthetic.json", med,
              len(med["gifs"]) + len(med["magic"]) + len(med["probe"]), 0)
        dec = synthetic_decode_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "media" / "decode.json", dec, len(dec), 0)
        seek = synthetic_seek_fixtures()
        write(REPO / "tests" / "fixtures" / "media" / "seek.json", seek, len(seek), 0)

    if args.solo == "decode":
        decode_group()
        return

    def editor_group() -> None:
        ed = synthetic_editor_ui_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "editor" / "timeline_ui.json", ed,
              sum(len(c["calls"]) for c in ed["cases"]), 0)

    if args.solo == "editor":
        editor_group()
        return

    def lru_group() -> None:
        import lru_cases
        cases = lru_cases.cases()
        results = _node(ctx, "lru_js.mjs", cases)
        out = [{**c, "results": r} for c, r in zip(cases, results)]
        write(REPO / "tests" / "fixtures" / "editor" / "lru_bytes.json", out, sum(len(c["ops"]) for c in cases), 0)

    if args.solo == "lru":
        lru_group()
        return

    def models_group() -> None:
        mmf = synthetic_model_manager_fixtures()
        write(REPO / "tests" / "fixtures" / "ai" / "model_manager.json", mmf,
              sum(len(c["ops"]) for c in mmf["cases"]) + len(mmf["settings"]), 0)

    if args.solo == "modelos":
        models_group()
        return

    def mcp_group() -> None:
        mcp = synthetic_mcp_fixtures(ctx)
        counts = {
            "dto": lambda d: len(d["projects"]) + len(d["analyze"]) + len(d["jobs"]) + len(d["aspect_ratio"]),
            "capabilities": lambda d: len(d["environments"]) + len(d["project_summaries"]),
            "help": lambda d: len(d["help"]) + len(d["guide"]),
            "registry": lambda d: len(d["mcp_errors"]) + len(d["value_errors"]) + len(d["registrations"]),
            "resources": lambda d: len(d["calls"]),
            "audit": lambda d: len(d["extract_meta"]) + len(d["steps"]),
            "tools_read": lambda d: len(d["calls"]),
            "tools_edit": lambda d: len(d["steps"]),
        }
        errors = {
            "dto": lambda d: sum("error" in (p["timeline_detail"] or {}) for p in d["projects"]),
            "capabilities": lambda d: sum("error" in r for e in d["environments"] for _q, r in e["describe"]),
            "help": lambda d: 0,
            "registry": lambda d: sum("error" in r for r in d["registrations"]),
            "resources": lambda d: sum("error" in c for c in d["calls"]),
            "audit": lambda d: sum("error" in s for s in d["steps"]),
            "tools_read": lambda d: sum("error" in c for c in d["calls"]),
            "tools_edit": lambda d: sum("error" in c for c in d["steps"]),
        }
        for name, payload in mcp.items():
            write(REPO / "tests" / "fixtures" / "mcp" / f"{name}.json", payload, counts[name](payload),
                  errors[name](payload))

    if args.solo == "mcp":
        mcp_group()
        return

    def platform_group() -> None:
        plat = synthetic_platform_chrome_fixtures(ctx)
        write(RENDER_DIR / "platform.json", plat, len(plat), 0)

    if args.solo == "plataforma":
        platform_group()
        return

    def image_import_group() -> None:
        imp = synthetic_image_import_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "media" / "image_import.json", imp, len(imp),
              sum("error" in c for c in imp))

    if args.solo == "imagenes":
        image_import_group()
        return

    def sfx_write_group() -> None:
        sfx_w = synthetic_sfx_write_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "sfx" / "write.json", sfx_w, len(sfx_w["steps"]),
              sum("error" in s for s in sfx_w["steps"]))

    if args.solo == "sfx_escritura":
        sfx_write_group()
        return

    def beats_group() -> None:
        bts = synthetic_beats_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "beats" / "synthetic.json", bts, len(bts), sum("error" in b for b in bts))

    if args.solo == "beats":
        beats_group()
        return

    def keywords_group() -> None:
        kw = synthetic_keyword_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "keywords" / "synthetic.json", kw, sum(len(v) for v in kw.values()), 0)

    if args.solo == "keywords":
        keywords_group()
        return

    def mono_group() -> None:
        mono = synthetic_audio_mono_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "audio" / "mono.json", mono, len(mono["beats"]) + len(mono["rms"]),
              sum("error" in c for c in mono["beats"] + mono["rms"]))
        if args.real:
            data = json.loads((args.video_yt / "backend" / "data" / "projects.json").read_text(encoding="utf-8"))
            mono_real = real_audio_mono_fixtures(ctx, data)
            write(REPO / "tests" / "fixtures" / "local" / "audio" / "mono.json", mono_real,
                  len(mono_real["beats"]) + len(mono_real["rms"]),
                  sum("error" in c for c in mono_real["beats"] + mono_real["rms"]))

    if args.solo == "mono":
        mono_group()
        return

    def outline_group() -> None:
        ol = synthetic_outline_fixtures()
        write(REPO / "tests" / "fixtures" / "media" / "outline.json", ol, sum(len(c["heights"]) for c in ol), 0)

    if args.solo == "contorno":
        outline_group()
        return

    def projects_group() -> None:
        prj_syn = synthetic_projects_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "projects" / "synthetic.json", prj_syn, len(prj_syn["steps"]),
              sum("error" in s for s in prj_syn["steps"]))

    if args.solo == "proyectos":
        projects_group()
        return

    def settings_group() -> None:
        sett = synthetic_settings_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "settings" / "synthetic.json", sett, len(sett["steps"]),
              sum("error" in s for s in sett["steps"]))

    if args.solo == "ajustes":
        settings_group()
        return

    def heatmap_group() -> None:
        hmp = synthetic_heatmap_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "heatmap" / "synthetic.json", hmp, len(hmp), 0)

    if args.solo == "heatmap":
        heatmap_group()
        return

    def youtube_video_group() -> None:
        ytv = synthetic_youtube_video_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "youtube" / "video.json", ytv,
              len(ytv["quality"]) + len(ytv["infos"]) + len(ytv["mb"]),
              sum("error" in q[1] for q in ytv["quality"]) + sum("error" in i["qualities"] for i in ytv["infos"]))

    if args.solo == "youtube_video":
        youtube_video_group()
        return

    def url_group() -> None:
        urls = synthetic_url_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "url" / "synthetic.json", urls,
              len(urls["parse"]) + len(urls["youtube"]) + len(urls["unquote"]) + len(urls["qsl"]),
              sum(p[1] is None for p in urls["parse"]))

    if args.solo == "url":
        url_group()
        return

    if args.solo == "vfx":
        vfx = synthetic_vfx_fixtures()
        write(RENDER_DIR / "vfx.json", vfx, len(vfx), 0)
        return

    def fx_bake_group() -> None:
        bake = synthetic_fx_bake_fixtures()
        write(REPO / "tests" / "fixtures" / "fx" / "bake.json", bake, len(bake), 0)

    if args.solo == "fx_bake":
        fx_bake_group()
        return

    def widths_group() -> None:
        wid = synthetic_text_width_fixtures()
        groups = wid["fonts"] + wid["aliases"] + wid["odd"] + [c for d in wid["dirs"] for c in d["cases"]]
        write(REPO / "tests" / "fixtures" / "text" / "widths.json", wid, sum(len(g["widths"]) for g in groups),
              sum(isinstance(w[3], dict) for g in groups for w in g["widths"]))
        if args.real:
            data = json.loads((args.video_yt / "backend" / "data" / "projects.json").read_text(encoding="utf-8"))
            wid_real = real_text_width_fixtures(ctx, data)
            write(REPO / "tests" / "fixtures" / "local" / "text" / "widths.json", wid_real,
                  sum(len(c["calls"]) for c in wid_real["cases"]), 0)

    if args.solo == "anchos":
        widths_group()
        return

    def matte_group() -> None:
        mat = synthetic_bg_matte_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "media" / "bg_matte.json", mat, len(mat["clips"]) + len(mat["lut"]),
              sum("error" in c for c in mat["clips"]))
        if args.real:
            real = real_bg_derive_fixtures(args.video_yt / "backend" / "data" / "bgcache" / "matte")
            write(REPO / "tests" / "fixtures" / "local" / "bg" / "derive.json", real, len(real["cases"]), 0)

    if args.solo == "matte":
        matte_group()
        return

    def shapes_group() -> None:
        shp = synthetic_shape_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "shapes" / "geometry.json", shp,
              sum(len(v) for v in shp.values()), 0)

    def warp_group() -> None:
        warp = synthetic_text_warp_fixtures(ctx)
        write(REPO / "tests" / "fixtures" / "text" / "warp.json", warp, len(warp["clips"]) + len(warp["project"]), 0)

    if args.solo == "mascaras":
        tmask = synthetic_text_mask_fixtures()
        write(RENDER_DIR / "text_masks.json", tmask, len(tmask["masks"]) + len(tmask["metrics"]), 0)
        return
    if args.solo == "texto3d":
        warp_group()
        if args.real:
            data = json.loads((args.video_yt / "backend" / "data" / "projects.json").read_text(encoding="utf-8"))
            warp_real = real_text_warp_fixtures(ctx, data)
            write(REPO / "tests" / "fixtures" / "local" / "text" / "warp.json", warp_real, len(warp_real["clips"]), 0)
        return
    if args.solo == "texto":
        ren_txt = synthetic_text_render_fixtures(ctx)
        write(RENDER_DIR / "text.json", ren_txt, len(ren_txt), 0)
        return
    if args.solo == "shapes":
        shapes_group()
        return
    def render_group() -> None:
        ren = synthetic_render_fixtures(ctx, keep=args.conservar)
        write(RENDER_DIR / "synthetic.json", ren, len(ren), 0)
        chains = synthetic_effect_chains()
        write(RENDER_DIR / "effects.json", chains, len(chains), 0)
        tmask = synthetic_text_mask_fixtures()
        write(RENDER_DIR / "text_masks.json", tmask, len(tmask["masks"]) + len(tmask["metrics"]), 0)

    if args.solo == "render":
        render_group()
        return

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
    outline_group()
    matte_group()
    txt = synthetic_text_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "text" / "synthetic.json", txt,
          len(txt["styles"]) + len(txt["clips"]) + len(txt["effective"]) + len(txt["alpha"]) + len(txt["case"]) + len(txt["active_word"])
          + len(txt["font_files"]) + sum(len(lay["fonts"]) for lay in txt["font_layouts"]), 0)
    warp_group()
    widths_group()
    render_group()
    ren_txt = synthetic_text_render_fixtures(ctx)
    write(RENDER_DIR / "text.json", ren_txt, len(ren_txt), 0)
    export_group()
    shapes_group()
    editor_group()
    lru_group()
    platform_group()
    models_group()
    mcp_group()
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
    settings_group()
    sfx_syn = synthetic_sfx_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "sfx" / "synthetic.json", sfx_syn, len(sfx_syn["steps"]),
          sum("error" in s for s in sfx_syn["steps"]))
    sfx_write_group()
    beats_group()
    keywords_group()
    heatmap_group()
    youtube_video_group()
    fx_bake_group()
    url_group()
    mono_group()
    js = synthetic_json_fixtures()
    write(REPO / "tests" / "fixtures" / "json" / "synthetic.json", js,
          len(js["floats"]) + len(js["dumps"]) + len(js["loads"]), sum("error" in c for c in js["loads"]))
    projects_group()
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
    mix = synthetic_audio_mix_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "audio" / "mix.json", mix, len(mix), 0)
    aud = synthetic_audio_fixtures(ctx)
    write(REPO / "tests" / "fixtures" / "audio" / "synthetic.json", aud,
          len(aud["clips"]) + len(aud["nodes"]) + len(aud["atempo"]) + len(aud["intensity"]) + len(aud["weights"])
          + len(aud["timelines"]), 0)
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
        warp_real = real_text_warp_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "text" / "warp.json", warp_real, len(warp_real["clips"]), 0)
        mix_real = real_audio_mix_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "audio" / "mix.json", mix_real, len(mix_real), 0)
        aud_real = real_audio_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "audio" / "real.json", aud_real,
              len(aud_real["clips"]) + len(aud_real["timelines"]), 0)
        ev_real = real_eval_fixtures(ctx, data)
        write(REPO / "tests" / "fixtures" / "local" / "eval" / "real.json", ev_real,
              len(ev_real["clips"]) + len(ev_real["filters"]), 0)
    print(f"listo en {time.perf_counter() - t0:.1f} s")


if __name__ == "__main__":
    main()
