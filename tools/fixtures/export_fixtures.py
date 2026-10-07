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
  tests/fixtures/local/model/real.json  con --real: TUS proyectos de data/projects.json
  tests/fixtures/local/edit/real.json   con --real: operaciones sobre TUS timelines
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


class Ctx:
    """Módulos de video-yt que hacen de referencia."""

    def __init__(self, root: Path):
        backend = root / "backend"
        if not (backend / "app" / "schemas.py").exists():
            sys.exit(f"No encuentro video-yt en {root} (falta backend/app/schemas.py). Usa --video-yt.")
        sys.path.insert(0, str(backend))
        from pydantic import ValidationError
        from app import clip_speed, migrations, projects, schemas, timeline_history, timeline_store
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


def run_edit_case(ctx: Ctx, base_id: str, timeline: dict, name: str, op: str, params, compact: bool) -> dict:
    out = {"name": name, "timeline": base_id, "op": op, "params": params}
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
        cases.append(run_edit_case(ctx, bid, bases[bid], c["name"], c["op"], c["params"], compact=False))
    return {"timelines": bases, "cases": cases}


def real_edit_fixtures(ctx: Ctx, data: dict) -> dict:
    """Operaciones típicas del editor sobre cada timeline real (muestreo de clips)."""
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
        for name, op, params in ops:
            cases.append(run_edit_case(ctx, pid, tl, f"{pid} · {name}", op, params, compact=True))
    return {"timelines": timelines, "cases": cases}


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
    print(f"listo en {time.perf_counter() - t0:.1f} s")


if __name__ == "__main__":
    main()
