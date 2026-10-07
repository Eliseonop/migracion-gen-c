"""Casos sintéticos de paridad del MOTOR DE EDICIÓN (gc_core edit ↔ timeline_store.apply_op).

Cada caso: una timeline de partida, una operación y sus parámetros. El resultado
esperado (timeline + changed + warnings, o el error) lo calcula video-yt al
exportar, con uuid4 sustituido por la secuencia determinista de SequentialIdSource.
"""
from __future__ import annotations

import copy


def clip(cid, track, kind="video", start=0.0, inp=0.0, out=5.0, **kw):
    asset_kind = {"video": "clips", "audio": "audios", "image": "images", "text": "text",
                  "shape": "shape"}.get(kind, "clips")
    c = {"id": cid, "track_id": track, "kind": kind, "asset_kind": asset_kind, "asset_id": cid,
         "filename": "" if kind == "text" else f"{cid}.mp4", "start": start, "in_point": inp,
         "out_point": out, "source_duration": 0.0 if kind in ("text", "image") else 30.0}
    c.update(kw)
    return c


def track(tid, kind, name=None, **kw):
    t = {"id": tid, "kind": kind, "name": name or tid}
    t.update(kw)
    return t


WORDS = [{"text": "uno", "start": 0.0, "end": 0.4}, {"text": "dos", "start": 0.5, "end": 0.9},
         {"text": "tres", "start": 1.0, "end": 1.6, "prob": 0.9}]

BASIC = {
    "schema_version": 5, "fps": 30, "width": 720, "height": 1280,
    "tracks": [track("V1", "video"), track("V2", "video"), track("T1", "text", style={"tema": "pop"}),
               track("A1", "audio")],
    "clips": [
        clip("v1", "V1", start=0, inp=0, out=5),
        clip("v2", "V1", start=5, inp=10, out=14, speed=2.0),
        clip("img", "V2", kind="image", start=1, inp=0, out=3),
        clip("t1", "T1", kind="text", start=0, inp=0, out=2, text="uno dos tres", words=WORDS,
             keyframes={"enabled": True, "items": [{"id": "k1", "t": 0.0, "props": {"x": 0}},
                                                     {"id": "k2", "t": 1.5, "props": {"x": 1}}, "raro"]}),
        clip("t2", "T1", kind="text", start=3, inp=0, out=2, text="cuatro"),
        clip("a1", "A1", kind="audio", start=0, inp=2, out=10, reverse=True),
        clip("rev", "V2", start=10, inp=4, out=12, reverse=True, speed=0.5),
    ],
    "markers": [{"id": "m0", "t": 1.0}],
}

MAGNETIC = {
    "schema_version": 5,
    "tracks": [track("V1", "video", magnetic=True), track("V2", "video")],
    "clips": [clip("m1", "V1", start=0, out=4), clip("m2", "V1", start=4, out=3), clip("m3", "V1", start=7, out=2),
              clip("x", "V2", start=0, out=10)],
}

LEGACY_OVERLAP = {
    "schema_version": 5,
    "tracks": [track("V1", "video"), track("V2", "video")],
    "clips": [clip("o1", "V1", start=0, out=6), clip("o2", "V1", start=4, out=5), clip("o3", "V2", start=0, out=2)],
}

EMPTY = {"schema_version": 5}

LONG_NAME = "Pista de diálogo principal — versión ñandú 🎬 final"
LONG_LABEL = "Marcador con un texto larguísimo y caracteres raros ñ á é 🎵 " * 2


def op(name, base, op_name, params):
    return {"name": name, "base": base, "op": op_name, "params": params}


CASES = [
    # --- Pistas ---------------------------------------------------------------------------
    op("add_track vídeo con nombre e id por defecto", BASIC, "add_track", {"kind": "video"}),
    op("add_track texto con nombre e id", BASIC, "add_track", {"kind": "text", "name": "Títulos", "track_id": "Tx"}),
    op("add_track audio con track_id vacío", BASIC, "add_track", {"kind": "audio", "track_id": ""}),
    op("add_track en timeline vacía", EMPTY, "add_track", {"kind": "video"}),
    op("ERROR add_track kind inválido", BASIC, "add_track", {"kind": "foo"}),
    op("ERROR add_track id repetido", BASIC, "add_track", {"kind": "video", "track_id": "V1"}),
    op("ERROR add_track nombre numérico", BASIC, "add_track", {"kind": "video", "name": 5}),
    op("ERROR add_track parámetro desconocido", BASIC, "add_track", {"kind": "video", "color": "red"}),
    op("ERROR add_track sin kind", BASIC, "add_track", {}),

    op("reorder_track V1 encima de T1", BASIC, "reorder_track",
       {"track_id": "V1", "target_track_id": "T1", "place": "above"}),
    op("reorder_track T1 debajo de V2", BASIC, "reorder_track",
       {"track_id": "T1", "target_track_id": "V2", "place": "below"}),
    op("reorder_track sobre sí misma", BASIC, "reorder_track", {"track_id": "V2", "target_track_id": "V2"}),
    op("ERROR reorder_track audio dentro de la pila", BASIC, "reorder_track",
       {"track_id": "A1", "target_track_id": "V1"}),
    op("ERROR reorder_track place inválido", BASIC, "reorder_track",
       {"track_id": "V1", "target_track_id": "T1", "place": "middle"}),
    op("ERROR reorder_track place null", BASIC, "reorder_track",
       {"track_id": "V1", "target_track_id": "T1", "place": None}),
    op("ERROR reorder_track pista desconocida", BASIC, "reorder_track", {"track_id": "V9", "target_track_id": "T1"}),

    op("rename_track con espacios", BASIC, "rename_track", {"track_id": "V1", "name": "  Principal  "}),
    op("rename_track recorta a 32 caracteres", BASIC, "rename_track", {"track_id": "T1", "name": LONG_NAME}),
    op("ERROR rename_track nombre en blanco", BASIC, "rename_track", {"track_id": "V1", "name": " \t "}),
    op("ERROR rename_track nombre null", BASIC, "rename_track", {"track_id": "V1", "name": None}),
    op("ERROR rename_track pista desconocida", BASIC, "rename_track", {"track_id": "V9", "name": "x"}),

    op("remove_track con clips", BASIC, "remove_track", {"track_id": "V2"}),
    op("remove_track desliga las pistas ligadas",
       {**BASIC, "tracks": [*BASIC["tracks"][:3], track("A1", "audio", linked_track_id="T1")]},
       "remove_track", {"track_id": "T1"}),
    op("ERROR remove_track desconocida", BASIC, "remove_track", {"track_id": "V9"}),

    op("link_tracks audio → texto", BASIC, "link_tracks", {"track_id": "A1", "to_track_id": "T1"}),
    op("link_tracks a sí misma desliga", BASIC, "link_tracks", {"track_id": "A1", "to_track_id": "A1"}),
    op("link_tracks vacío desliga", BASIC, "link_tracks", {"track_id": "A1", "to_track_id": ""}),
    op("ERROR link_tracks destino inexistente", BASIC, "link_tracks", {"track_id": "A1", "to_track_id": "T9"}),
    op("ERROR link_tracks sin to_track_id", BASIC, "link_tracks", {"track_id": "A1"}),
    op("unlink_track", {**BASIC, "tracks": [*BASIC["tracks"][:3], track("A1", "audio", linked_track_id="T1")]},
       "unlink_track", {"track_id": "A1"}),

    # --- Clips ----------------------------------------------------------------------------
    op("add_clip en hueco libre", BASIC, "add_clip", {"clip": clip("nuevo", "V1", start=20, out=3)}),
    op("add_clip sin id genera uno", BASIC, "add_clip",
       {"clip": {k: v for k, v in clip("x", "V2", start=30).items() if k != "id"}}),
    op("add_clip encima de otro sube a la pista libre de encima", BASIC, "add_clip",
       {"clip": clip("pisa", "V1", start=1, out=2)}),
    op("add_clip que no cabe crea pista nueva", {**BASIC, "clips": [*BASIC["clips"], clip("ocupa", "V2", start=0, out=9)]},
       "add_clip", {"clip": clip("pisa", "V1", start=1, out=2)}),
    op("add_clip de texto pisa y hereda estilo en pista nueva", BASIC, "add_clip",
       {"clip": clip("tx", "T1", kind="text", start=0.5, out=1, text="hola")}),
    op("add_clip de audio pisa y baja de pista", BASIC, "add_clip",
       {"clip": clip("a2", "A1", kind="audio", start=1, out=3)}),
    op("add_clip imagen con coerciones", BASIC, "add_clip",
       {"clip": clip("im2", "V2", kind="image", start="40", inp=0, out=2, opacity=1)}),
    op("ERROR add_clip de audio en pista de vídeo", BASIC, "add_clip", {"clip": clip("a3", "V1", kind="audio")}),
    op("ERROR add_clip con kind desconocido", BASIC, "add_clip", {"clip": clip("f", "V1", kind="foo")}),
    op("ERROR add_clip id repetido", BASIC, "add_clip", {"clip": clip("v1", "V2", start=50)}),
    op("ERROR add_clip rango invertido (validación)", BASIC, "add_clip",
       {"clip": clip("inv", "V1", start=40, inp=5, out=2)}),
    op("ERROR add_clip reframe fuera de rango", BASIC, "add_clip",
       {"clip": clip("rf", "V1", start=40, reframe={"zoom": 5})}),
    op("ERROR add_clip pista inexistente", BASIC, "add_clip", {"clip": clip("np", "V9")}),
    op("ERROR add_clip no es objeto", BASIC, "add_clip", {"clip": [1, 2]}),

    op("move_clip dentro de la pista sin pisar", BASIC, "move_clip", {"clip_id": "v2", "start": 8}),
    op("move_clip que pisa sube de pista", BASIC, "move_clip", {"clip_id": "v2", "start": 0}),
    op("move_clip a otra pista y otro instante", BASIC, "move_clip", {"clip_id": "v1", "track_id": "V2", "start": 20}),
    op("move_clip redondea a milisegundos", BASIC, "move_clip", {"clip_id": "t2", "start": 3.14159}),
    op("move_clip start bool", BASIC, "move_clip", {"clip_id": "t2", "start": True}),
    op("ERROR move_clip a pista de otro tipo", BASIC, "move_clip", {"clip_id": "t2", "track_id": "A1"}),
    op("ERROR move_clip start negativo", BASIC, "move_clip", {"clip_id": "v1", "start": -1}),
    op("ERROR move_clip start texto", BASIC, "move_clip", {"clip_id": "v1", "start": "5"}),
    op("ERROR move_clip clip inexistente", BASIC, "move_clip", {"clip_id": "zz", "start": 1}),

    op("remove_clip", BASIC, "remove_clip", {"clip_id": "img"}),
    op("ERROR remove_clip inexistente", BASIC, "remove_clip", {"clip_id": "zz"}),

    op("split_clip vídeo", BASIC, "split_clip", {"clip_id": "v1", "at_time": 2.5}),
    op("split_clip con velocidad 2x", BASIC, "split_clip", {"clip_id": "v2", "at_time": 6}),
    op("split_clip invertido y lento", BASIC, "split_clip", {"clip_id": "rev", "at_time": 13.3333}),
    op("split_clip texto: palabras y keyframes", BASIC, "split_clip", {"clip_id": "t1", "at_time": 0.5}),
    op("split_clip audio invertido", BASIC, "split_clip", {"clip_id": "a1", "at_time": 3}),
    op("ERROR split_clip en el borde", BASIC, "split_clip", {"clip_id": "v1", "at_time": 5}),
    op("ERROR split_clip fuera del clip", BASIC, "split_clip", {"clip_id": "v1", "at_time": 50}),
    op("ERROR split_clip sin at_time", BASIC, "split_clip", {"clip_id": "v1"}),

    op("duplicate_clip detrás del original (pisa: sube)", BASIC, "duplicate_clip", {"clip_id": "v1"}),
    op("duplicate_clip en un instante", BASIC, "duplicate_clip", {"clip_id": "img", "start": 30}),
    op("duplicate_clip de una copia apunta a la raíz",
       {**BASIC, "clips": [*BASIC["clips"], clip("copia", "V2", start=40, dup_of="img")]},
       "duplicate_clip", {"clip_id": "copia", "start": 50}),
    op("ERROR duplicate_clip start negativo", BASIC, "duplicate_clip", {"clip_id": "v1", "start": -3}),

    # --- Pista magnética ---------------------------------------------------------------------
    op("magnética: mover al final se re-empaqueta", MAGNETIC, "move_clip", {"clip_id": "m1", "start": 20}),
    op("magnética: soltar en medio inserta y empuja", MAGNETIC, "add_clip",
       {"clip": clip("m4", "V1", start=4, out=1)}),
    op("magnética: quitar cierra el hueco", MAGNETIC, "remove_clip", {"clip_id": "m2"}),
    op("magnética: renombrar una pista también empaqueta", MAGNETIC, "rename_track",
       {"track_id": "V2", "name": "B-roll"}),

    # --- Solapes que ya traía el proyecto ---------------------------------------------------
    op("solape heredado: renombrar no lo toca", LEGACY_OVERLAP, "rename_track", {"track_id": "V1", "name": "A"}),
    op("solape heredado: mover otro clip a esa pista lo avisa", LEGACY_OVERLAP, "move_clip",
       {"clip_id": "o3", "track_id": "V1", "start": 20}),

    # --- Formato y marcadores ------------------------------------------------------------------
    op("set_project_format 16:9", BASIC, "set_project_format", {"aspect": "16:9"}),
    op("set_project_format 1080", BASIC, "set_project_format", {"resolution": 1080}),
    op("set_project_format 4:5 en 2160 (lado largo acotado)", BASIC, "set_project_format",
       {"aspect": "4:5", "resolution": 2160}),
    op("set_project_format ancho y alto sueltos", BASIC, "set_project_format", {"width": 1001, "height": "700.4"}),
    op("set_project_format resolution en texto", BASIC, "set_project_format", {"resolution": "720"}),
    op("set_project_format fps con decimales", BASIC, "set_project_format", {"fps": 29.97}),
    op("ERROR set_project_format fps 0", BASIC, "set_project_format", {"fps": 0}),
    op("ERROR set_project_format fps texto", BASIC, "set_project_format", {"fps": "30"}),
    op("ERROR set_project_format resolución rara", BASIC, "set_project_format", {"resolution": 999}),
    op("ERROR set_project_format aspect raro", BASIC, "set_project_format", {"aspect": "2:1"}),

    op("set_markers normaliza y ordena", BASIC, "set_markers", {"markers": [
        {"t": 3.14159, "label": "Beat"}, {"t": -2}, {"t": "1.5", "id": 7, "color": "#f00", "label": LONG_LABEL},
        {"t": 0.5, "id": "", "label": ""}]}),
    op("set_markers null vacía", BASIC, "set_markers", {"markers": None}),
    op("ERROR set_markers sin t", BASIC, "set_markers", {"markers": [{"label": "x"}]}),
    op("ERROR set_markers sin parámetro", BASIC, "set_markers", {}),

    op("ERROR operación desconocida", BASIC, "volar", {}),
]

# --- Propiedades de clip, encuadre, audio y material (2ª tanda de F2) --------------------------

PROPS = copy.deepcopy(BASIC)
PROPS["clips"] += [
    clip("v3", "V1", start=7, inp=0, out=2),   # justo detrás de v2: v2 al crecer lo empuja
    clip("ov", "V2", start=30, out=6, layout="overlay",
         reframe={"zoom": 0.8, "keyframes": [{"t": 0, "cx": 0.3, "zoom": 0.9, "pan_mode": "direct"}]},
         keyframes={"enabled": True, "items": [{"id": "k1", "t": 0, "props": {"cx": 0.2, "x": 0.1}},
                                               {"id": "k2", "t": 2, "props": {"scale": 2}}]}),
    clip("aud", "A1", kind="audio", start=20, out=6, volume=0.8, audio_fx={"eq": 0.5}),
    clip("adj", "V2", kind="adjustment", start=50, out=4),
    clip("shp", "V2", kind="shape", start=60, out=3, shape={"type": "rect", "x": 0.3, "draw": 0.5}),
    clip("yt1", "V1", start=40, inp=12, out=17, filename="video_720.mp4",
         beats={"times": [12.5, 13, True, "x"], "bpm": 120},
         bg_removal={"auto": {"enabled": True, "base_key": "abc", "status": "done",
                              "edits": [{"t": 13, "x": 1}, {"t": "no"}]}},
         reframe={"keyframes": [{"t": 12, "cx": 0.4}], "keyframes2": [{"t": 13, "cx": 0.6}]}),
    clip("yt2", "V2", start=70, inp=0, out=5, filename="video_720.mp4", asset_scope="library"),
]

LONG_NOTE = ("El científico escribe la ecuación en la pizarra mientras habla. " * 10).strip()

MASKS = [{"type": "circle", "x": 0.3}, "ignorada",
         {"type": "text", "text": {"content": "HOLA", "weight": 950.0, "align": "left"}},
         {"type": "brush", "invert": 1, "brush": {"points": [{"x": 0.1, "y": 0.2, "m": 1}, {"x": "nan", "y": 1},
                                                             {"x": "0.5", "y": 0.5}]}},
         {"type": "raro", "target": "adjust", "feather": 3, "w": 100, "id": 7}]

CASES += [
    # update_clip: varios grupos en un solo paso
    op("update_clip opacidad + voltear + fusión + desactivar", PROPS, "update_clip",
       {"clip_id": "v1", "patch": {"opacity": 0.5, "flip_h": 1, "blend_mode": "screen", "disabled": True}}),
    op("update_clip velocidad 0.5: crece y empuja a los de detrás", PROPS, "update_clip",
       {"clip_id": "v2", "patch": {"speed": 0.5}}),
    op("update_clip velocidad 4: encoge", PROPS, "update_clip", {"clip_id": "v2", "patch": {"speed": "4"}}),
    op("update_clip tono e invertido", PROPS, "update_clip",
       {"clip_id": "a1", "patch": {"keep_pitch": False, "reverse": 0}}),
    op("update_clip transiciones", PROPS, "update_clip", {"clip_id": "t2", "patch": {"appear": "fade", "exit": "pop"}}),
    op("update_clip posición, inicio y duración (generada)", PROPS, "update_clip",
       {"clip_id": "img", "patch": {"position": "top", "start": 3.5, "duration": 2}}),
    op("update_clip duración acotada a la fuente", PROPS, "update_clip", {"clip_id": "v1", "patch": {"duration": 50}}),
    op("update_clip rol de texto", PROPS, "update_clip", {"clip_id": "t1", "patch": {"role": "free"}}),
    op("update_clip nota con espacios", PROPS, "update_clip",
       {"clip_id": "v1", "patch": {"note": "  El científico escribe  "}}),
    op("update_clip nota larga se recorta", PROPS, "update_clip", {"clip_id": "v1", "patch": {"note": LONG_NOTE}}),
    op("update_clip nota vacía la borra", PROPS, "update_clip", {"clip_id": "v1", "patch": {"note": ""}}),
    op("update_clip fusión normal la quita", PROPS, "update_clip", {"clip_id": "v1", "patch": {"blend_mode": "normal"}}),
    op("update_clip opacidad escrita como texto", PROPS, "update_clip", {"clip_id": "v1", "patch": {"opacity": "0.25"}}),
    op("ERROR update_clip velocidad fuera de rango", PROPS, "update_clip", {"clip_id": "v2", "patch": {"speed": 20}}),
    op("ERROR update_clip transición desconocida", PROPS, "update_clip", {"clip_id": "t2", "patch": {"appear": "boom"}}),
    op("ERROR update_clip rol en vídeo", PROPS, "update_clip", {"clip_id": "v1", "patch": {"role": "free"}}),
    op("ERROR update_clip fusión desconocida", PROPS, "update_clip", {"clip_id": "v1", "patch": {"blend_mode": "glow"}}),
    op("ERROR update_clip patch vacío", PROPS, "update_clip", {"clip_id": "v1", "patch": {}}),
    op("ERROR update_clip sin patch", PROPS, "update_clip", {"clip_id": "v1"}),
    op("ERROR update_clip clave desconocida", PROPS, "update_clip", {"clip_id": "v1", "patch": {"color": 1}}),
    op("ERROR update_clip opacidad > 1", PROPS, "update_clip", {"clip_id": "v1", "patch": {"opacity": 2}}),
    op("ERROR update_clip start negativo", PROPS, "update_clip", {"clip_id": "v1", "patch": {"start": -1}}),
    op("ERROR update_clip voltear audio", PROPS, "update_clip", {"clip_id": "a1", "patch": {"flip_v": True}}),
    op("ERROR update_clip velocidad en imagen", PROPS, "update_clip", {"clip_id": "img", "patch": {"speed": 2}}),

    op("set_clip_layout libre", PROPS, "set_clip_layout", {"clip_id": "v1", "position": "free"}),
    op("set_clip_opacity", PROPS, "set_clip_opacity", {"clip_id": "img", "opacity": 0}),
    op("set_clip_speed", PROPS, "set_clip_speed", {"clip_id": "rev", "speed": 1.5, "keep_pitch": None}),
    op("set_clip_transition", PROPS, "set_clip_transition", {"clip_id": "v1", "appear": "zoom"}),
    op("set_text_role", PROPS, "set_text_role", {"clip_id": "t2", "role": "caption"}),
    op("ERROR set_clip_opacity sin valor", PROPS, "set_clip_opacity", {"clip_id": "img", "opacity": None}),

    # Encuadre
    op("reframe_clip centrado con zoom", PROPS, "reframe_clip", {"clip_id": "v1", "mode": "center", "zoom": 0.5}),
    op("reframe_clip manual con paneo", PROPS, "reframe_clip",
       {"clip_id": "img", "mode": "manual", "zoom": "0.7", "pan_from": {"cx": 0.2, "cy": 2},
        "pan_to": {"cx": "abc"}}),
    op("reframe_clip manual estático por defecto", PROPS, "reframe_clip", {"clip_id": "yt1", "mode": "manual"}),
    op("reframe_clip con keyframes", PROPS, "reframe_clip",
       {"clip_id": "yt1", "mode": "keyframes", "keyframes": [{"t": 12, "cx": 0.1}, {"t": 15, "cx": 0.9, "zoom": 0.5,
                                                                                  "extra": 1}]}),
    op("ERROR reframe_clip keyframes vacíos", PROPS, "reframe_clip", {"clip_id": "v1", "mode": "keyframes"}),
    op("ERROR reframe_clip zoom > 1", PROPS, "reframe_clip", {"clip_id": "v1", "zoom": 2}),
    op("ERROR reframe_clip modo raro", PROPS, "reframe_clip", {"clip_id": "v1", "mode": "auto"}),
    op("ERROR reframe_clip en texto", PROPS, "reframe_clip", {"clip_id": "t1"}),
    op("ERROR reframe_clip keyframe fuera de rango", PROPS, "reframe_clip",
       {"clip_id": "v1", "mode": "keyframes", "keyframes": [{"t": 0, "cx": 1.5}]}),
    op("ERROR reframe_clip pan_to no es objeto", PROPS, "reframe_clip", {"clip_id": "v1", "mode": "manual",
                                                                          "pan_to": []}),

    op("crop_clip objeto libre (acotado a la fuente)", PROPS, "crop_clip",
       {"clip_ids": ["ov", "ov"], "w": 0.5, "h": 0.4, "cx": 0.1, "cy": 0.95}),
    op("crop_clip restablecer", PROPS, "crop_clip", {"clip_ids": ["ov"], "reset": True}),
    op("ERROR crop_clip pista con clips que no son libres", PROPS, "crop_clip",
       {"track_id": "V2", "w": 0.5, "h": 0.5}),
    op("ERROR crop_clip sin w", PROPS, "crop_clip", {"clip_ids": ["ov"], "h": 0.5}),
    op("ERROR crop_clip clip que no es libre", PROPS, "crop_clip", {"clip_ids": ["v1"], "w": 0.5, "h": 0.5}),
    op("ERROR crop_clip sin objetivo", PROPS, "crop_clip", {"w": 0.5, "h": 0.5}),

    # Animación, ritmo y efectos
    op("set_clip_keyframes", PROPS, "set_clip_keyframes",
       {"clip_id": "v1", "keyframes": {"items": [{"t": 0, "props": {"x": 0.2}}], "extra": 1}}),
    op("set_clip_keyframes desactivados", PROPS, "set_clip_keyframes",
       {"clip_id": "ov", "keyframes": {"enabled": 0}}),
    op("set_clip_keyframes null los borra", PROPS, "set_clip_keyframes", {"clip_id": "ov", "keyframes": None}),
    op("ERROR set_clip_keyframes items no es lista", PROPS, "set_clip_keyframes",
       {"clip_id": "v1", "keyframes": {"items": 5}}),

    op("set_clip_beats normaliza y ordena", PROPS, "set_clip_beats",
       {"clip_id": "aud", "beats": {"times": [3, 1.23456, -1, "2"], "bpm": "128.26", "every": "2"}}),
    op("set_clip_beats null los quita", PROPS, "set_clip_beats", {"clip_id": "yt1", "beats": None}),
    op("ERROR set_clip_beats every 3", PROPS, "set_clip_beats", {"clip_id": "aud", "beats": {"every": 3}}),
    op("ERROR set_clip_beats en texto", PROPS, "set_clip_beats", {"clip_id": "t1", "beats": {}}),

    op("set_clip_masks normaliza", PROPS, "set_clip_masks", {"clip_id": "v1", "masks": MASKS}),
    op("set_clip_masks vacías", PROPS, "set_clip_masks", {"clip_id": "t1", "masks": None}),
    op("ERROR set_clip_masks en audio", PROPS, "set_clip_masks", {"clip_id": "a1", "masks": []}),
    op("ERROR set_clip_masks no es lista", PROPS, "set_clip_masks", {"clip_id": "v1", "masks": {}}),

    op("set_clip_effects efectos y filtros", PROPS, "set_clip_effects",
       {"clip_id": "v1", "effects": {"blur": 3}, "filters": [{"id": "teal_orange", "amount": 1.5}, "bw"]}),
    op("set_clip_effects reemplazar", {**PROPS, "clips": [*PROPS["clips"][:-1], {**PROPS["clips"][-1],
                                                                              "effects": {"sepia": 1}}]},
       "set_clip_effects", {"clip_id": "yt2", "effects": {"blur": 1}, "replace": True}),
    op("set_clip_effects capa de ajuste sin filtros", PROPS, "set_clip_effects", {"clip_id": "adj", "filters": []}),
    op("ERROR set_clip_effects filtro desconocido", PROPS, "set_clip_effects", {"clip_id": "v1", "filters": ["x"]}),
    op("ERROR set_clip_effects en texto", PROPS, "set_clip_effects", {"clip_id": "t1", "effects": {}}),
    op("ERROR set_clip_effects sin nada", PROPS, "set_clip_effects", {"clip_id": "v1"}),

    # Audio
    op("set_clip_volume", PROPS, "set_clip_volume", {"clip_id": "a1", "volume": 1.5}),
    op("set_clip_volume fundido de entrada", PROPS, "set_clip_volume", {"clip_id": "aud", "fade": "in", "fade_dur": 1}),
    op("set_clip_volume fundido de salida", PROPS, "set_clip_volume", {"clip_id": "aud", "fade": "out"}),
    op("set_clip_volume fundido sobre keyframes existentes", PROPS, "set_clip_volume",
       {"clip_id": "ov", "fade": "in", "volume": 0}),
    op("set_clip_volume silenciar vídeo", PROPS, "set_clip_volume", {"clip_id": "v1", "muted": True}),
    op("set_clip_volume valor raro = 1", PROPS, "set_clip_volume", {"clip_id": "v1", "volume": "abc"}),
    op("ERROR set_clip_volume fundido raro", PROPS, "set_clip_volume", {"clip_id": "aud", "fade": "sideways"}),
    op("ERROR set_clip_volume sin nada", PROPS, "set_clip_volume", {"clip_id": "aud"}),
    op("ERROR set_clip_volume en texto", PROPS, "set_clip_volume", {"clip_id": "t1", "volume": 1}),

    op("set_track_audio volumen y fundido", PROPS, "set_track_audio",
       {"track_id": "A1", "volume": 0.5, "fade": "out", "fade_dur": 0.3}),
    op("set_track_audio efectos reemplazando", PROPS, "set_track_audio",
       {"track_id": "A1", "audio_fx": {"reverb": 0.3}, "replace_fx": True}),
    op("ERROR set_track_audio pista sin audio", PROPS, "set_track_audio", {"track_id": "T1", "volume": 1}),
    op("ERROR set_track_audio efecto desconocido", PROPS, "set_track_audio", {"track_id": "A1", "audio_fx": {"x": 1}}),

    op("set_clip_audio_fx fusiona", PROPS, "set_clip_audio_fx", {"clip_id": "aud", "audio_fx": {"echo": 0.4, "eq": True}}),
    op("set_clip_audio_fx con rampa", PROPS, "set_clip_audio_fx",
       {"clip_id": "aud", "audio_fx": {"reverb": 0.8, "eq": True}, "ramp": {"start": 0.5, "end": 2, "from": 0.1}}),
    op("set_clip_audio_fx rampa sobre un keyframe existente", PROPS, "set_clip_audio_fx",
       {"clip_id": "ov", "audio_fx": {"radio": 1}, "ramp": {"end": 2.02}}),
    op("ERROR set_clip_audio_fx rampa fuera del clip", PROPS, "set_clip_audio_fx",
       {"clip_id": "aud", "audio_fx": {"echo": 1}, "ramp": {"start": 1, "end": 60}}),
    op("ERROR set_clip_audio_fx rampa sin end", PROPS, "set_clip_audio_fx",
       {"clip_id": "aud", "audio_fx": {"echo": 1}, "ramp": {"start": 1}}),
    op("ERROR set_clip_audio_fx efecto desconocido", PROPS, "set_clip_audio_fx", {"clip_id": "aud", "audio_fx": {"x": 1}}),

    # Material
    op("replace_media desplaza tiempos e invalida el recorte", PROPS, "replace_media",
       {"sources": [{"filename": "video_720.mp4", "delta": 12}],
        "target": {"asset_id": 9, "filename": "video_1080.mp4", "duration": 20, "media_version": "v2"}}),
    op("replace_media recorta lo que se sale", PROPS, "replace_media",
       {"sources": [{"filename": "video_720.mp4", "delta": -10}],
        "target": {"asset_id": "n", "filename": "corto.mp4", "duration": 6}}),
    op("replace_media sin coincidencias avisa", PROPS, "replace_media",
       {"sources": [{"filename": "otro.mp4"}], "target": {"asset_id": 1, "filename": "nuevo.mp4"}}),
    op("ERROR replace_media destino igual al origen", PROPS, "replace_media",
       {"sources": [{"filename": "video_720.mp4"}], "target": {"asset_id": 1, "filename": "video_720.mp4"}}),
    op("ERROR replace_media sin sources", PROPS, "replace_media", {"sources": [], "target": {"filename": "x"}}),
]

# --- 3ª tanda: catálogo, congelar, subtítulos, figuras, barras, ajuste, fondo -----------------

MEDIA = copy.deepcopy(PROPS)
MEDIA["clips"] += [
    clip("fz", "V1", start=100, inp=2, out=8, source_duration=20, layout="overlay",
         transform={"x": 0.3, "y": 0.6, "scale": 1.2, "rotation": 5}, opacity=0.8, look="warm",
         effects={"blur": 2}, flip_h=True, blend_mode="screen",
         masks=[{"type": "circle", "x": 0.4}, {"type": "rectangle", "target": "adjust"}],
         reframe={"zoom": 0.7, "keyframes": [{"t": 2, "cx": 0.2, "zoom": 0.9}, {"t": 8, "cx": 0.8, "fit": "contain"}],
                  "dual_crop": True, "keyframes2": [{"t": 2, "cx": 0.5}, {"t": 8, "cx": 0.1, "pan_mode": "direct"}]},
         bg_removal={"enabled": True, "auto": {"enabled": True, "base_key": "k"}, "chroma": {"enabled": True}},
         vfx=[{"uid": "fxA", "effect": "glitch_rgb", "v": 1, "enabled": False, "params": {"amount": 0.2},
               "from": 0, "to": None}, "raro"],
         keyframes={"enabled": True, "items": [{"id": "a", "t": 0, "props": {"x": 0.2, "opacity": 0.5}},
                                               {"id": "b", "t": 4, "props": {"x": 0.9, "cx": 0.3}}]}),
    clip("fz2", "V1", start=110, inp=0, out=4, reverse=True, speed=2,
         anim={"x": [{"t": 0, "v": 0.1}, {"t": 2, "v": 0.9, "ease": "direct"}, "x"], "opacity": [{"t": 1, "v": 0.4}]}),
    clip("tail", "V1", start=106.5, inp=0, out=2),
]
MEDIA["clips"] = [c for c in MEDIA["clips"] if c["id"] != "fz2"] + [c for c in MEDIA["clips"] if c["id"] == "fz2"]

IMAGE = {"id": 42, "filename": "frame_42.png", "label": "Fotograma congelado"}

SEGMENTS = [
    {"start": 0.0, "end": 2.5, "text": " Hola a todos y bienvenidos al canal ",
     "words": [{"text": "Hola", "start": 0.0, "end": 0.3, "prob": 0.9}, {"text": "a", "start": 0.3, "end": 0.4},
               {"text": "todos", "start": 0.4, "end": 0.8}, {"text": "y", "start": 0.8, "end": 0.9},
               {"text": "bienvenidos", "start": 0.9, "end": 1.6}, {"text": "al", "start": 1.6, "end": 1.7},
               {"text": "canal", "start": 1.7, "end": 2.4}]},
    {"start": 2.5, "end": 4.0, "text": "sin palabras pero con texto largo de verdad", "index": 7},
    {"start": 4.0, "end": 4.2, "text": "   "},
    {"start": 20, "end": 25, "text": "fuera del clip"},
    {"start": -3, "end": 0.5, "text": "empieza antes"},
    "no es dict",
]

CASES += [
    # Efectos y animaciones del catálogo
    op("apply_effect añade con parámetros saneados", MEDIA, "apply_effect",
       {"clip_ids": ["v1", "img"], "effect": "glitch_rgb", "params": {"amount": 5, "speed": "x", "seed": 2.5},
        "start": -1, "end": 3}),
    op("apply_effect cambia uno de la pila por uid", MEDIA, "apply_effect",
       {"clip_ids": ["fz"], "uid": "fxA", "params": {"speed": 2}, "end": 1.5}),
    op("apply_effect quita por uid", MEDIA, "apply_effect", {"clip_ids": ["fz"], "uid": "fxA", "remove": True}),
    op("apply_effect quita por efecto", MEDIA, "apply_effect",
       {"clip_ids": ["fz"], "effect": "glitch_rgb", "remove": True}),
    op("apply_effect sustituye la pila", MEDIA, "apply_effect",
       {"clip_ids": ["fz"], "stack": [{"effect": "glitch_rgb", "uid": "u1"}, "x",
                                      {"effect": "glitch_rgb", "from": 2, "to": 1}]}),
    op("apply_effect pila vacía", MEDIA, "apply_effect", {"clip_ids": ["fz"], "stack": []}),
    op("ERROR apply_effect efecto desconocido", MEDIA, "apply_effect", {"clip_ids": ["v1"], "effect": "nope"}),
    op("ERROR apply_effect en texto", MEDIA, "apply_effect", {"clip_ids": ["t1"], "effect": "glitch_rgb"}),
    op("ERROR apply_effect sin nada", MEDIA, "apply_effect", {"clip_ids": ["v1"]}),
    op("ERROR apply_effect uid inexistente", MEDIA, "apply_effect", {"clip_ids": ["v1"], "uid": "zz"}),
    op("ERROR apply_effect sin clips", MEDIA, "apply_effect", {"clip_ids": [], "effect": "glitch_rgb"}),

    op("set_clip_animation entrada con duración", MEDIA, "set_clip_animation",
       {"clip_ids": ["v1", "t1"], "slot": "in", "animation": "fade_in", "duration": 20}),
    op("set_clip_animation duración por defecto", MEDIA, "set_clip_animation",
       {"clip_ids": ["shp"], "slot": "in", "animation": "fade_in"}),
    op("set_clip_animation quitar", {**MEDIA, "clips": [{**c, "animations": {"in": {"id": "fade_in", "dur": 1}}}
                                                        if c["id"] == "v1" else c for c in MEDIA["clips"]]},
       "set_clip_animation", {"clip_ids": ["v1"], "slot": "in"}),
    op("ERROR set_clip_animation ranura equivocada", MEDIA, "set_clip_animation",
       {"clip_ids": ["v1"], "slot": "out", "animation": "fade_in"}),
    op("ERROR set_clip_animation ranura rara", MEDIA, "set_clip_animation", {"clip_ids": ["v1"], "slot": "x"}),
    op("ERROR set_clip_animation en audio", MEDIA, "set_clip_animation",
       {"clip_ids": ["a1"], "slot": "in", "animation": "fade_in"}),

    # Congelar fotograma
    op("freeze_frame en medio: parte y empuja", MEDIA, "freeze_frame",
       {"clip_id": "fz", "at_time": 103, "image": IMAGE, "duration": 2}),
    op("freeze_frame al principio", MEDIA, "freeze_frame",
       {"clip_id": "fz", "at_time": "100.05", "image": {"id": "i", "filename": "f.png"}}),
    op("freeze_frame al final", MEDIA, "freeze_frame", {"clip_id": "fz", "at_time": 105.95, "image": IMAGE}),
    op("freeze_frame sin keyframes: encuadre y pose legada", MEDIA, "freeze_frame",
       {"clip_id": "fz2", "at_time": 111, "image": IMAGE, "duration": 0.05}),
    op("freeze_frame invertido", MEDIA, "freeze_frame", {"clip_id": "rev", "at_time": 15, "image": IMAGE}),
    op("ERROR freeze_frame fuera del clip", MEDIA, "freeze_frame", {"clip_id": "fz", "at_time": 50, "image": IMAGE}),
    op("ERROR freeze_frame en imagen", MEDIA, "freeze_frame", {"clip_id": "img", "at_time": 2, "image": IMAGE}),
    op("ERROR freeze_frame sin filename", MEDIA, "freeze_frame", {"clip_id": "fz", "at_time": 103, "image": {"id": 1}}),

    # Subtítulos
    op("add_subtitles crea la pista si no hay", {**MEDIA, "tracks": [t for t in MEDIA["tracks"] if t["kind"] != "text"],
                                                 "clips": [c for c in MEDIA["clips"] if c["kind"] != "text"]},
       "add_subtitles", {"source_clip_id": "v1", "segments": SEGMENTS, "transcript_id": "tr1"}),
    op("add_subtitles parte por max_words", MEDIA, "add_subtitles",
       {"source_clip_id": "v1", "segments": SEGMENTS, "style": {"max_words": 3, "font": "Inter"},
        "track_id": "T1"}),
    op("add_subtitles hereda el estilo de la pista", MEDIA, "add_subtitles",
       {"source_clip_id": "yt1", "segments": [{"start": 12, "end": 15, "text": "uno dos tres cuatro cinco",
                                               "words": [{"text": "uno", "start": 12, "end": 12.5}]}]}),
    op("add_subtitles max_words raro = un solo clip", MEDIA, "add_subtitles",
       {"source_clip_id": "v1", "segments": SEGMENTS[:2], "style": {"max_words": "muchas"}}),
    op("ERROR add_subtitles en pista de vídeo", MEDIA, "add_subtitles",
       {"source_clip_id": "v1", "segments": SEGMENTS, "track_id": "V1"}),
    op("ERROR add_subtitles texto que no es texto", MEDIA, "add_subtitles",
       {"source_clip_id": "v1", "segments": [{"start": 0, "end": 1, "text": 5}]}),

    # Figuras
    op("add_shape por defecto", MEDIA, "add_shape", {}),
    op("add_shape estrella con valores raros", MEDIA, "add_shape",
       {"shape": {"type": "star", "x": 1.5, "y": "0.2", "w": 0.01, "opacity": 1.0, "fill": "#12abZZ",
                  "stroke": " #ABCDEF ", "sides": 20.5, "dash": "dot", "draw": 0, "rotation": None, "extra": 1},
        "track_id": "V2", "start": 3, "duration": "2.5"}),
    op("add_shape trazado libre", MEDIA, "add_shape",
       {"points": [[0.1, 0.2], [0.5, "0.9"], ["x", 1], [0.8, 0.3]], "shape": {"type": "x", "stroke": "#000000",
                                                                              "x": 9}}),
    op("add_shape crea pista de vídeo", {**MEDIA, "tracks": [t for t in MEDIA["tracks"] if t["kind"] != "video"],
                                         "clips": [c for c in MEDIA["clips"] if c["track_id"] not in ("V1", "V2")]},
       "add_shape", {"shape": {"type": "line"}}),
    op("ERROR add_shape trazado de un punto", MEDIA, "add_shape", {"points": [[0.1, 0.2]]}),
    op("ERROR add_shape número inválido", MEDIA, "add_shape", {"shape": {"x": "abc"}}),
    op("ERROR add_shape en pista de audio", MEDIA, "add_shape", {"track_id": "A1"}),

    # Barras de cine y capa de ajuste
    op("add_cinema_bars hasta el final", MEDIA, "add_cinema_bars", {}),
    op("add_cinema_bars 16:9 animadas", MEDIA, "add_cinema_bars", {"ratio": "16:9", "animate": True, "start": 2,
                                                                   "duration": 0.5}),
    op("add_cinema_bars número", MEDIA, "add_cinema_bars", {"ratio": 3, "start": "1", "duration": 4}),
    op("ERROR add_cinema_bars proporción que no deja barras", {**MEDIA, "width": 1920, "height": 1080},
       "add_cinema_bars", {"ratio": 1.5}),
    op("ERROR add_cinema_bars proporción rara", MEDIA, "add_cinema_bars", {"ratio": "ancha"}),

    op("add_adjustment_layer con filtros", MEDIA, "add_adjustment_layer",
       {"start": 1, "duration": 3, "filters": [{"id": "bw", "amount": 0.5}], "intensity": 0.6}),
    op("add_adjustment_layer en pista dada con efectos", MEDIA, "add_adjustment_layer",
       {"track_id": "V2", "effects": {"blur": 1}, "start": 200}),
    op("ERROR add_adjustment_layer intensidad", MEDIA, "add_adjustment_layer", {"intensity": 2}),
    op("ERROR add_adjustment_layer sin duración", MEDIA, "add_adjustment_layer", {"duration": None}),

    # Eliminar fondo
    op("set_clip_bg_removal fusiona por secciones", MEDIA, "set_clip_bg_removal",
       {"clip_id": "fz", "bg_removal": {"chroma": {"similarity": 0.5, "color": "0x0f0"}, "mode": "chroma",
                                        "auto": {"edits": [{"op": "keep", "points": [{"x": 0.1, "y": 0.2}], "t": 1.23456,
                                                            "tool": "smart"}, {"points": []}]}}}),
    op("set_clip_bg_removal reemplaza", MEDIA, "set_clip_bg_removal",
       {"clip_id": "fz", "bg_removal": {"outline": {"enabled": 1, "color": "red", "width": 3}}, "replace": True}),
    op("set_clip_bg_removal null lo quita", MEDIA, "set_clip_bg_removal", {"clip_id": "fz", "bg_removal": None}),
    op("set_clip_bg_removal vacío también", MEDIA, "set_clip_bg_removal",
       {"clip_id": "v1", "bg_removal": {}, "replace": True}),
    op("ERROR set_clip_bg_removal en texto", MEDIA, "set_clip_bg_removal", {"clip_id": "t1", "bg_removal": {}}),
]


def materialize() -> list[dict]:
    """Copia profunda de cada caso (las bases se comparten entre casos)."""
    return [copy.deepcopy(c) for c in CASES]
