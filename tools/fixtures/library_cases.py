"""Casos sintéticos de paridad de la BIBLIOTECA (gc_core LibraryFile ↔ library.py).

Pasos sobre un data/ temporal: escribir library.json y archivos, listar, editar, guardar
metadata semántica y quitar materiales (con proyectos que los usan o no).
"""
from __future__ import annotations

from ops_cases import clip, track

ITEMS = [
    {"id": "lib_clip", "index": 3, "filename": "Entrevista final.mp4", "label": "Entrevista",
     "saved_at": "2026-09-20T10:00:00+00:00", "source_url": "https://YouTube.com/watch?v=abc"},
    {"id": "lib_img_w", "width": 1080, "filename": "foto", "saved_at": "2026-09-21T10:00:00+00:00"},
    {"id": "lib_img_ext", "filename": "Logo Ñandú?.PNG", "saved_at": "2026-09-21T10:00:00+00:00"},
    {"id": "lib_img_end", "filename": "x.png", "end": 3, "saved_at": "2026-09-19T10:00:00+00:00"},
    {"id": "lib_tts", "resource_type": "audio", "filename": "voz 1.m4a", "voice": "ef_dora",
     "saved_at": "2026-09-22T10:00:00+00:00", "text": "hola"},
    {"id": "lib_yt", "resource_type": "audio", "filename": "musica.m4a", "youtube_url": "https://youtu.be/xyz"},
    {"id": "lib_ytid", "resource_type": "audio", "filename": "m2.m4a", "youtube_id": "q1",
     "saved_at": "2026-09-22T10:00:00+00:00"},
    {"id": "lib_origin", "resource_type": "clip", "filename": "o.mp4", "origin": "stock"},
    {"id": "lib_origin_yt", "resource_type": "image", "filename": "o.jpg", "origin": "youtube"},
    {"id": "lib_both", "resource_type": "clip", "filename": "b.mp4", "origin": "foundry", "source": "generated"},
    {"id": "lib_raro", "resource_type": "video", "filename": 5, "saved_at": ""},
]

PROJECTS = {
    "projects": [
        {"id": "p1", "name": "Uno", "created_at": "2026-09-01T00:00:00", "timeline": {
            "schema_version": 5, "tracks": [track("V1", "video")],
            "clips": [clip("c1", "V1", asset_id="lib_clip", asset_scope="library"),
                      clip("c2", "V1", start=6, asset_id="lib_clip", asset_scope="library")]}},
        {"id": "p2", "name": "Sin timeline", "created_at": "2026-09-02T00:00:00"},
        {"id": "p3", "name": "Proyecto", "created_at": "2026-09-03T00:00:00", "timeline": {
            "schema_version": 5, "tracks": [track("V1", "video")],
            "clips": [clip("c3", "V1", asset_id="lib_tts")]}},
        {"id": "p4", "name": "", "created_at": "2026-09-04T00:00:00", "timeline": {
            "schema_version": 5, "tracks": [track("V1", "video")],
            "clips": [clip("c4", "V1", asset_id="lib_dos", asset_scope="library")]}},
        {"id": "p5", "name": "Cinco", "created_at": "2026-09-05T00:00:00", "timeline": {
            "schema_version": 5, "tracks": [track("V1", "video")],
            "clips": [clip("c5", "V1", asset_id="lib_dos", asset_scope="library")]}},
        {"id": "", "name": "", "created_at": "2026-09-06T00:00:00", "timeline": {
            "schema_version": 5, "tracks": [track("V1", "video")],
            "clips": [clip("c6", "V1", asset_id="lib_anonimo", asset_scope="library")]}},
    ],
    "groups": [],
}

STEPS = [
    ("load",), ("list",),
    ("write", {"version": 1, "items": ITEMS[:-1]}),
    ("files", ["video/Entrevista final.mp4", "audio/voz 1.m4a", "image/x.png"]),
    ("list",),
    ("update", "lib_tts", {"label": "  Voz nueva  ", "description": 5, "otro": "x"}),
    ("update", "nope", {"label": "x"}),
    ("update", "lib_tts", "no es dict"),
    ("semantic", "lib_img_w", {"tema": "logo", "tono": ["serio"]}),
    ("semantic", "lib_img_w", None),
    ("unsave", "lib_clip"),                       # lo usa el proyecto «Uno»
    ("unsave", "lib_tts"),                        # asset_scope «project»: no cuenta
    ("exists", "audio/voz 1.m4a"),
    ("unsave", "lib_img_end"),
    ("exists", "image/x.png"),
    ("files", ["image/foto"]),
    ("unsave", "lib_img_w"),                      # sin resource_type guardado: busca en audio/, no en image/
    ("exists", "image/foto"),
    ("unsave", "nope"),
    ("list",),
    ("write", {"version": 1, "items": ITEMS}),
    ("list",),                                    # filename numérico: quote() falla
    ("write", {"items": [ITEMS[0], "no es un dict"]}),
    ("list",),
    ("update", "lib_clip", {"label": "x"}),       # se encuentra antes de llegar al que no es dict
    ("write", "{ roto"), ("load",),
    # BOM de UTF-8: json.loads(read_text("utf-8")) falla → biblioteca vacía
    ("write", "\ufeff{\"items\": [{\"id\": \"bom\", \"index\": 1}]}"), ("load",), ("list",),
    ("write", [1]), ("load",),
    ("write", {"items": None}), ("list",),
    # saved_at: `or ""` y comparaciones de Python (texto con texto, números con números)
    ("write", {"items": [{"id": "a", "index": 1, "saved_at": 5}]}), ("list",),
    ("write", {"items": [{"id": "a", "index": 1, "saved_at": {"x": 1}}]}), ("list",),
    ("write", {"items": [{"id": "a", "index": 1, "saved_at": 5}, {"id": "b", "index": 1, "saved_at": 7.5},
                         {"id": "c", "index": 1, "saved_at": True}, {"id": "d", "index": 1, "saved_at": 5.0},
                         {"id": "e", "filename": "x.png", "saved_at": "z"}]}), ("list",),
    ("write", {"items": [{"id": "a", "index": 1, "saved_at": 5}, {"id": "b", "index": 1, "saved_at": 0}]}),
    ("list",),
    ("write", {"items": [{"id": "a", "index": 1, "saved_at": {"x": 1}}, {"id": "b", "index": 1, "saved_at": "z"}]}),
    ("list",),
    ("write", {"items": [{"id": "a", "index": 1, "saved_at": "2026-01-02"}, {"id": "b", "index": 1, "saved_at": False},
                         {"id": "c", "index": 1, "saved_at": "2026-01-03"}, {"id": "d", "index": 1}]}), ("list",),
    # En uso: nombre vacío → id; varios proyectos
    ("write", {"items": [{"id": "lib_dos", "resource_type": "clip", "filename": "dos.mp4"}]}),
    ("unsave", "lib_dos"), ("unsave", "nope"),
    ("write", {"items": [{"id": "lib_anonimo", "resource_type": "clip", "filename": "a.mp4"}]}),
    ("unsave", "lib_anonimo"),                    # proyecto sin nombre ni id: «otro proyecto»
    # Path.unlink() no borra una carpeta: falla y Python la deja
    ("write", {"items": [{"id": "lib_dir", "resource_type": "clip", "filename": "sub"}]}),
    ("dirs", ["video/sub"]), ("unsave", "lib_dir"), ("exists", "video/sub"),
]

DTO = [(item, scope) for item in ITEMS for scope in ("library", "project")] + [("texto", "library")]

# --- «Guardar en la biblioteca» (save_from_project) ------------------------------------------
# Pasos con el library.py y el projects.py REALES sobre un data/ y unas carpetas de clips
# temporales («{TMP}»; los clips por defecto en {TMP}/clips; «{DRIVE}», su unidad), con uuid4 y
# _now fijados:
#   projects contenido    escribe data/projects.json
#   library contenido     escribe data/library/library.json
#   disk [rutas]          crea archivos bajo {TMP} (su contenido es su ruta)
#   save pid tipo ident   save_from_project(pid, tipo, ident)
# Tras cada paso se guardan library.json, projects.json y lo que hay en disco.

T0 = "2026-10-06T00:00:00+00:00"


def _clip(index, filename, **kw):
    c = {"index": index, "filename": filename, "url": f"/api/media/p1/video/{filename}", "start": 0, "end": 10}
    c.update(kw)
    return c


def _tl_clip(cid, asset_kind, asset_id, filename):
    return {"id": cid, "track_id": "V1" if asset_kind != "audios" else "A1",
            "kind": {"clips": "video", "audios": "audio", "images": "image"}[asset_kind],
            "asset_kind": asset_kind, "asset_id": asset_id, "filename": filename, "start": 0, "duration": 1}


SAVE_PROJECTS = {"projects": [
    {"id": "p1", "name": "Uno", "created_at": T0,
     "clips": [_clip(0, "c0.mp4", label="  Mi clip: <bueno>?\t ", start=1, end=3.5, created_at=T0),
               _clip(1, "c1"),
               _clip(2, "orig.mp4", in_point=1, out_point=2, id="seg2"),
               _clip(3, "orig.mp4", in_point=3, out_point=4),
               _clip(4, "../../fuera.mp4"),
               _clip(5, "falta.mp4"),
               _clip(6, "nan.mp4", start=float("nan"), end=float("inf")),
               _clip(7, "largo.mp4", label="Un título   muy\nlargo " + "x" * 70 + " fin"),
               _clip(8, "yt.mp4", origin="youtube", source_url="https://youtu.be/abc"),
               _clip(9, "sub/dentro.mp4"),
               _clip(10, "punto.", label="..."),
               _clip(11, "solo.mp4", id="propio"),       # id propio y solo en su archivo: no es compartido
               _clip(12, "v.mp4", label="<<>>"),        # la etiqueta se queda vacía: «video»
               _clip(13, ".."),                         # la propia carpeta del proyecto: no vale
               _clip(14, "/c0.mp4"),                    # raíz sin unidad: la de la base (fuera)
               _clip(15, "{TMP}/clips/p1/video/c0b.mp4"),  # absoluta, dentro: vale
               _clip(16, "{DRIVE}c0c.mp4"),             # la misma unidad sin raíz: se añade a video/
               _clip(17, "todo.mp4", reframe={"zoom": 0.5, "keyframes": [{"t": 0, "cx": 0.4}]},
                     face_track={"start": 0, "end": 2, "width": 640, "height": 360, "samples": 3},
                     provider="pexels", external_id="123", author="Ana", license_info="CC0",
                     description="", in_point=0.5, out_point=1.5)],
     "audios": [{"id": "au1", "filename": "voz.wav", "url": "/api/media/p1/audio/voz.wav", "voice": "ana",
                 "text": "Hola", "duration": 2.5, "engine": "kokoro"},
                {"id": "au2", "filename": "otra", "url": "u", "text": "sin duración", "description": ""},
                {"id": "au3", "filename": "au3.wav", "url": "u", "index": 99},    # remove_material no lo encuentra
                {"id": "au4", "filename": "yt.m4a", "url": "u", "origin": "youtube", "youtube_url": "https://y/1",
                 "youtube_id": "1", "duration": 0.0, "description": "", "text": "texto", "speed": 1.25}],
     "images": [{"id": "im1", "filename": "foto.png", "url": "u", "width": 640, "height": 480, "label": "Foto"},
                {"id": "im2", "filename": "anim", "url": "u", "animated": True, "duration": 1.5}],
     "timeline": {"schema_version": 5,
                  "tracks": [{"id": "V1", "kind": "video", "name": "V"}, {"id": "A1", "kind": "audio", "name": "A"}],
                  "clips": [_tl_clip("t0", "clips", "0", "c0.mp4"), _tl_clip("t2", "clips", "2", "orig.mp4"),
                            _tl_clip("t3", "clips", "3", "orig.mp4"), _tl_clip("t9", "clips", "77", "c0.mp4"),
                            _tl_clip("ta", "audios", "au1", "voz.wav")]}},
    {"id": "p2", "name": "Dos", "created_at": T0, "folder": "{TMP}/propia",
     "clips": [_clip(0, "x.mp4")]},
]}

SAVE_STEPS = [
    ("projects", SAVE_PROJECTS),
    ("disk", ["clips/p1/video/c0.mp4", "clips/p1/video/c1", "clips/p1/video/orig.mp4", "clips/fuera.mp4",
              "clips/p1/video/nan.mp4", "clips/p1/video/largo.mp4", "clips/p1/video/yt.mp4",
              "clips/p1/video/sub/dentro.mp4", "clips/p1/video/punto.", "clips/p1/video/solo.mp4", "clips/p1/video/v.mp4", "clips/p1/video/c0b.mp4",
              "clips/p1/video/c0c.mp4", "clips/p1/video/todo.mp4", "clips/p1/audio/yt.m4a", "clips/p1/audio/voz.wav",
              "clips/p1/audio/otra", "clips/p1/audio/au3.wav", "clips/p1/image/foto.png", "clips/p1/image/anim",
              "propia/video/x.mp4"]),
    ("save", "p1", "otro", "0"), ("save", "nada", "clip", "0"), ("save", "p1", "clip", "99"),
    ("save", "p1", "clip", "5"), ("save", "p1", "clip", "4"), ("save", "p1", "audio", "0"),
    ("save", "p1", "clip", "0"), ("save", "p1", "clip", "0"),
    ("save", "p1", "clip", "1"), ("save", "p1", "clip", "2"), ("save", "p1", "clip", "3"),
    ("save", "p1", "clip", "6"), ("save", "p1", "clip", "7"), ("save", "p1", "clip", "8"),
    ("save", "p1", "clip", "9"), ("save", "p1", "clip", "10"), ("save", "p1", "clip", "11"),
    ("save", "p1", "clip", "12"), ("save", "p1", "clip", "13"), ("save", "p1", "clip", "14"),
    ("save", "p1", "clip", "15"), ("save", "p1", "clip", "16"), ("save", "p1", "clip", "17"),
    ("save", "p1", "audio", "au4"),
    ("save", "p1", "audio", "au1"), ("save", "p1", "audio", "au2"), ("save", "p1", "audio", "au3"),
    ("save", "p1", "image", "im1"), ("save", "p1", "image", "im2"), ("save", "p1", "image", "nada"),
    ("save", "p2", "clip", "0"),
    # Una biblioteca rota se lee vacía; con «items» que no es lista falla y se deshace.
    ("projects", SAVE_PROJECTS), ("disk", ["clips/p1/video/c0.mp4"]), ("library", "{ roto"),
    ("save", "p1", "clip", "0"),
    ("projects", SAVE_PROJECTS), ("disk", ["clips/p1/video/c0.mp4"]), ("library", {"version": 1, "items": {}}),
    ("save", "p1", "clip", "0"),
    ("library", {"version": 1, "items": [1]}), ("save", "p1", "clip", "0"),
    ("projects", SAVE_PROJECTS), ("disk", ["clips/p1/video/c0.mp4"]), ("library", {"items": "x"}),
    ("save", "p1", "clip", "0"),
]
