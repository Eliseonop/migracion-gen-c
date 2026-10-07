"""Casos sintéticos de paridad del modelo (gc_core ↔ backend/app/schemas.py).

Cada caso es la ENTRADA cruda; la salida esperada la calcula video-yt al
exportar (export_fixtures.py), así que aquí no se escribe ningún resultado a
mano: si Python cambia, se regeneran y el test de C++ dice qué diverge.

kind:
  project   projects._project_from_dict(input).model_dump()
  timeline  Timeline.model_validate(migrate_timeline(input)).model_dump()
  group     ProjectGroup.model_validate(input).model_dump()
  migrate   migrations.migrate_timeline(input)  (JSON crudo, sin validar)
"""
from __future__ import annotations

T0 = "2026-10-06T00:00:00+00:00"


def project(pid: str = "p1", **kw) -> dict:
    p = {"id": pid, "name": "Proyecto", "created_at": T0}
    p.update(kw)
    return p


def clip(cid: str = "c1", **kw) -> dict:
    c = {"id": cid, "track_id": "V1", "kind": "video", "asset_kind": "clips",
         "asset_id": "0", "filename": "a.mp4"}
    c.update(kw)
    return c


def text_clip(cid: str, **kw) -> dict:
    return clip(cid, track_id="T1", kind="text", asset_kind="text", asset_id=cid,
                filename="", text="Hola", **kw)


def material(index: int, **kw) -> dict:
    m = {"index": index, "filename": f"clip_{index}.mp4", "url": f"/clips/p1/clip_{index}.mp4",
         "start": 0, "end": 10}
    m.update(kw)
    return m


TRACKS_V4 = [
    {"id": "T1", "kind": "text", "name": "Texto 1"},
    {"id": "V1", "kind": "video", "name": "Vídeo 1"},
    {"id": "A1", "kind": "audio", "name": "Audio 1"},
    {"id": "V2", "kind": "video", "name": "Vídeo 2"},
    {"id": "T2", "kind": "text", "name": "Texto 2"},
]

WORDS = [{"text": "Hola", "start": 0.0, "end": 0.4, "prob": 0.98},
         {"text": "mundo", "start": 0.4, "end": 0.9}]

CASES: list[dict] = [
    # --- Proyectos válidos -----------------------------------------------------------
    {"name": "proyecto mínimo", "kind": "project", "input": project()},
    {"name": "unicode en textos", "kind": "project",
     "input": project(name="Pañuelo 🎬 «ñandú» — prueba", folder="E:/vídeos/año")},
    {"name": "carpeta del inicio (group_id) y carpeta en disco", "kind": "project",
     "input": project(group_id="g1", folder=None)},
    {"name": "material sin id toma el index", "kind": "project",
     "input": project(clips=[material(3), material(4, id=""), material(5, id="propio"),
                             material(6, id=None)])},
    {"name": "clips null equivale a lista vacía", "kind": "project", "input": project(clips=None)},
    {"name": "timeline vacía {} → timeline por defecto", "kind": "project", "input": project(timeline={})},
    {"name": "timeline null", "kind": "project", "input": project(timeline=None)},
    {"name": "timeline v1: roles de texto, keep_pitch y pila de pistas", "kind": "project",
     "input": project(timeline={
         "tracks": TRACKS_V4,
         "clips": [
             text_clip("t_words", words=WORDS),
             text_clip("t_origin", origin={"transcript_id": "tr1", "segment_index": 0}),
             text_clip("t_libre"),
             text_clip("t_rol_fijado", text_role="free", words=WORDS),
             text_clip("t_rol_raro", text_role="otro"),
             clip("v_pitch", keep_pitch=False),
             clip("a_pitch_cero", track_id="A1", kind="audio", asset_kind="audios", keep_pitch=0),
         ],
     })},
    {"name": "timeline v4 se migra a v5", "kind": "project",
     "input": project(timeline={"schema_version": 4, "tracks": TRACKS_V4,
                                "clips": [clip("v", keep_pitch=False)]})},
    {"name": "timeline v5 no se toca", "kind": "project",
     "input": project(timeline={"schema_version": 5, "tracks": TRACKS_V4,
                                "clips": [clip("v", keep_pitch=False)]})},
    {"name": "schema_version real (5.0) cuenta como v1", "kind": "project",
     "input": project(timeline={"schema_version": 5.0, "clips": [text_clip("t")]})},
    {"name": "coerciones lax de Pydantic", "kind": "project",
     "input": project(timeline={
         "fps": 30.0, "width": "720", "height": " 1_280 ", "audio_target_db": -14,
         "clips": [clip("c", start=5, in_point="1.5", out_point="1_000.5", volume=True,
                        muted=1, flip_h="yes", flip_v="OFF", reverse=0.0, speed="2",
                        opacity=1, ref_segment="t", loop="n")],
     })},
    {"name": "claves desconocidas se ignoran", "kind": "project",
     "input": project(desconocida=1, timeline={
         "schema_version": 5, "otra": {"x": 1},
         "clips": [clip("c", extra_clip=True, reframe={
             "zoom": 0.5, "nuevo": "x",
             "keyframes": [{"t": 0, "cx": 0.5, "rara": [1]}]})],
     })},
    {"name": "material completo (reframe, transcript, face_track)", "kind": "project",
     "input": project(
         clips=[material(0, id="m0", label="Entrevista", semantic={"tema": "física"},
                         reframe={"zoom": 0.8, "keyframes": [{"t": 0, "cx": 0.4}, {"t": 2, "cx": 0.6, "cy": 0.4,
                                                                                    "zoom": 0.9, "fit": "contain"}],
                                  "dual_crop": True, "zoom2": 0.5, "keyframes2": [{"t": 0, "cx": 0.2}],
                                  "crop_w": 0.5, "crop_h": 1, "master": True},
                         transcript={"id": "tr", "model": "small", "segments": [
                             {"start": 0, "end": 1, "text": "Hola mundo", "words": WORDS}]},
                         in_point=12.5, out_point=17, parent_id="m_padre",
                         face_track={"start": 0, "end": 10, "width": 1920, "height": 1080,
                                     "samples": 20, "track": [{"t": 0, "cx": 0.5, "w": 0.1, "h": 0.2}],
                                     "keyframes": [{"t": 0, "cx": 0.5}]})],
         transcripts=[{"id": "tr2", "model": "large-v3", "language": "es", "duration": 61.5,
                       "segments": [{"start": 0, "end": 2, "text": "Texto"}]}],
         audios=[{"id": "au1", "filename": "voz.wav", "url": "/a/voz.wav", "engine": "kokoro",
                  "voice": "ef_dora", "speed": 1}],
         images=[{"id": "im1", "filename": "x.png", "url": "/i/x.png", "width": 1080, "height": 1920},
                 {"id": "im2", "filename": "y.gif", "url": "/i/y.gif", "origin": None, "animated": True,
                  "frames": 24, "fps": 12, "duration": 2, "loop": True, "analysis": {"tags": ["gato"]}}],
         motion_compositions=[{"id": "mc1", "scenes": [{"t": 0}]}],
         scene_directions={"tramos": [{"desde": 0, "hasta": 4.5}]},
         visual_blueprint={"paleta": ["#fff", "#000"]},
     )},
    {"name": "valores sin tipar se copian tal cual (1 ≠ 1.0)", "kind": "project",
     "input": project(timeline={"schema_version": 5, "markers": [{"id": "m1", "t": 3, "label": "beat"}],
                                "tracks": [{"id": "V1", "kind": "video", "name": "V1",
                                            "style": {"tema": "x", "tam": 64}}],
                                "clips": [clip("c", style={"font": "Inter", "size": 64, "scale": 1.0,
                                                           "nested": {"a": [1, 2.5, None, True, "ñ"]}},
                                               transform={"x": 0, "y": 0.5, "scale": 1, "rotation": -12.25},
                                               effects={}, filters=[{"id": "teal", "amount": 0.7}],
                                               masks=[{"type": "circle", "feather": 0}],
                                               vfx=[{"uid": "u1", "effect": "glitch", "params": {}}],
                                               keyframes={"enabled": True, "items": []},
                                               bg_removal=None, beats={"times": [0.5, 1], "bpm": 120})]})},
    {"name": "reales con muchos decimales y notación científica", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [
         clip("c", start=0.1 + 0.2, in_point=1e-05, out_point=123456.789012345, source_duration=1e16,
              volume=2.5e-3)]})},

    # --- Proyectos inválidos: Python lanza ValidationError ------------------------
    {"name": "ERROR cx fuera de rango", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [
         clip("c", reframe={"keyframes": [{"t": 0, "cx": 1.5}]})]})},
    {"name": "ERROR zoom de reframe por debajo de 0.1", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [clip("c", reframe={"zoom": 0.05})]})},
    {"name": "ERROR crop_w por debajo de 0.05", "kind": "project",
     "input": project(clips=[material(0, reframe={"crop_w": 0.01})])},
    {"name": "ERROR asset_id numérico (str estricto)", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [clip("c", asset_id=3)]})},
    {"name": "ERROR words null", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [clip("c", words=None)]})},
    {"name": "ERROR falta track_id", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [
         {k: v for k, v in clip("c").items() if k != "track_id"}]})},
    {"name": "ERROR fps con decimales", "kind": "project", "input": project(timeline={"fps": 30.5})},
    {"name": "ERROR muted = 2", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [clip("c", muted=2)]})},
    {"name": "ERROR style es una lista", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [clip("c", style=[1])]})},
    {"name": "ERROR máscara que no es dict", "kind": "project",
     "input": project(timeline={"schema_version": 5, "clips": [clip("c", masks=[{"ok": 1}, "no"])]})},
    {"name": "ERROR falta created_at", "kind": "project", "input": {"id": "p", "name": "x"}},
    {"name": "ERROR scene_directions null", "kind": "project", "input": project(scene_directions=None)},

    # --- Timeline suelta (p. ej. ExportRequest) -------------------------------------
    {"name": "timeline v4 con marcadores", "kind": "timeline",
     "input": {"schema_version": 4, "fps": 25, "tracks": TRACKS_V4[:3],
               "clips": [text_clip("t", words=WORDS)], "markers": [{"id": "m", "t": 1.5}]}},

    # --- Carpetas del inicio ------------------------------------------------------------
    {"name": "carpeta del inicio", "kind": "group", "input": {"id": "g1", "name": "Shorts", "created_at": T0}},
    {"name": "ERROR carpeta sin nombre", "kind": "group", "input": {"id": "g1", "created_at": T0}},

    # --- Migración cruda (salida sin validar, orden de claves incluido) -------------------
    {"name": "migrar v4 → v5 reordena vídeo/texto en sus huecos", "kind": "migrate",
     "input": {"schema_version": 4, "tracks": TRACKS_V4 + ["no-dict"], "clips": []}},
    {"name": "migrar v2 → v5 sin clips añade clips = []", "kind": "migrate",
     "input": {"schema_version": 2, "fps": 30}},
    {"name": "migrar v3: keep_pitch false → true, 0 se queda", "kind": "migrate",
     "input": {"schema_version": 3, "clips": [{"keep_pitch": False}, {"keep_pitch": 0}, "raro"]}},
    {"name": "migrar: schema_version true cuenta como v1", "kind": "migrate",
     "input": {"schema_version": True, "clips": [{"kind": "text", "text_role": "caption"}]}},
    {"name": "migrar: schema_version negativa cuenta como v1", "kind": "migrate",
     "input": {"schema_version": -3, "tracks": "no-lista"}},
    {"name": "migrar: objeto vacío no cambia", "kind": "migrate", "input": {}},
    {"name": "migrar: lo que no es objeto no cambia", "kind": "migrate", "input": [1, 2]},
    {"name": "migrar: versión futura no se degrada", "kind": "migrate",
     "input": {"schema_version": 9, "clips": [{"keep_pitch": False}]}},
]
