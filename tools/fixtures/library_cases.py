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
