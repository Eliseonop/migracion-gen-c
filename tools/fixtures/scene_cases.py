"""Casos de la dirección de escena (app/scene_direction.py de video-yt) para export_fixtures.py --solo escena.

  SEGMENTS     normalize_segment de cada valor (con el reloj y los uuid desde el principio en cada uno).
  COMPONENTS   normalize_component de cada valor.
  DOCS         normalize_doc de cada valor.
  BLUEPRINTS   normalize_blueprint de cada valor.
  PROJECTS     proyectos con subtítulos, transcripción del material, texto del audio o nada: de cada uno
               timeline_duration, script_units, text_in_range, status_summary y auto_segments con cada
               escaleta de KEEPS.
  STORE        pasos sobre un projects.json temporal: load, save, update_segment, apply_plan_all,
               get_segment, load_blueprint y save_blueprint, con el reloj y los uuid seguidos.
  KEY_TEXTS    key_terms de motion/segment_context.py ([texto, limit]); STRIP_TEXTS, su _strip_accents.
  SEMANTICS    normalize_semantic de cada valor; BLOBS, semantic_blob.
  MATERIAL_PROJECTS y LIBRARY   material_catalog (sin biblioteca, con ella y con un library.json roto) y
               rank_materials con RANK_TEXTS y con los catálogos a mano de RANK_CATALOGS.
  META_STEPS   set_material_meta paso a paso sobre projects.json y library.json.
"""
from __future__ import annotations

NAN = float("nan")
INF = float("inf")

_ASSET_KIND = {"video": "clips", "audio": "audios", "text": "text", "image": "images"}


def _clip(cid: str, track: str, kind: str, **kw) -> dict:
    out = {"id": cid, "track_id": track, "kind": kind, "asset_kind": _ASSET_KIND.get(kind, "clips"),
           "asset_id": "0", "filename": f"{cid}.mp4"}
    out.update(kw)
    return out


def _track(tid: str, kind: str) -> dict:
    return {"id": tid, "kind": kind, "name": tid}


def _words(*items) -> list[dict]:
    return [{"text": t, "start": s, "end": e} for t, s, e in items]


_MAT = {"kind": "clips", "id": "3"}

SEGMENTS = [
    None, [], "tramo", 5, {}, {"start": 0}, {"end": 4},
    {"start": 0, "end": 0.1},
    {"start": 0, "end": 0.2},
    {"start": 1.0, "end": 1.19},
    {"start": "1.5", "end": " 4 "},
    {"start": True, "end": 3},
    {"start": -2, "end": 1.23456},
    {"start": 0.0004, "end": 2.0005},
    {"start": "x", "end": 3},
    {"start": [1], "end": 3},
    {"start": "nan", "end": 3},
    {"start": 0, "end": "inf"},
    {"start": 0, "end": 5, "id": "ab"},
    {"start": 0, "end": 5, "id": "abc"},
    {"start": 0, "end": 5, "id": "abc\n"},
    {"start": 0, "end": 5, "id": "abc\n\n"},
    {"start": 0, "end": 5, "id": "a b c"},
    {"start": 0, "end": 5, "id": "ñandú"},
    {"start": 0, "end": 5, "id": 12345},
    {"start": 0, "end": 5, "id": True},
    {"start": 0, "end": 5, "id": 0},
    {"start": 0, "end": 5, "id": 1.5},
    {"start": 0, "end": 5, "id": "x" * 40},
    {"start": 0, "end": 5, "id": "x" * 41},
    {"start": 0, "end": 5, "id": "sd_-_ok_"},
    {"start": 0, "end": 5, "mode": "explain"},
    {"start": 0, "end": 5, "mode": "Explain"},
    {"start": 0, "end": 5, "mode": 3},
    {"start": 0, "end": 5, "mode": None, "status": "placed"},
    {"start": 0, "end": 5, "mode": ["explain"]},
    {"start": 0, "end": 5, "mode": {"a": 1}},
    {"start": 0, "end": 5, "status": "bogus"},
    {"start": 0, "end": 5, "status": ["ready"]},
    {"start": 0, "end": 5, "status": "empty", "instruction": "hola"},
    {"start": 0, "end": 5, "instruction": "   \n\t "},
    {"start": 0, "end": 5, "instruction": 0},
    {"start": 0, "end": 5, "instruction": 7},
    {"start": 0, "end": 5, "composition_intent": "dos planos"},
    {"start": 0, "end": 5, "no_visual": 1},
    {"start": 0, "end": 5, "no_visual": "", "status": "generated"},
    {"start": 0, "end": 5, "mode": "material", "status": "empty"},
    {"start": 0, "end": 5, "materials": [
        _MAT, {"kind": "images", "id": 7, "scope": "library"}, {"kind": "clips", "id": None},
        {"kind": "audios", "id": "a"}, {"kind": "clips", "id": 0, "scope": "collection"}, "x", 4,
        {"kind": "clips"}, {"kind": "images", "id": True}, {"kind": "images", "id": 2.5}]},
    {"start": 0, "end": 5, "materials": [{"kind": "clips", "id": str(i)} for i in range(10)]},
    {"start": 0, "end": 5, "materials": "abc"},
    {"start": 0, "end": 5, "materials": {"kind": "clips", "id": 1}},
    {"start": 0, "end": 5, "materials": 5},
    {"start": 0, "end": 5, "materials": True},
    {"start": 0, "end": 5, "materials": 0},
    {"start": 0, "end": 5, "text": "  hola\t\tmundo \n y más  "},
    {"start": 0, "end": 5, "text": "palabra " * 300},
    {"start": 0, "end": 5, "text": "ñ" * 2001},
    {"start": 0, "end": 5, "text": "ñ" * 2000},
    {"start": 0, "end": 5, "text": 123},
    {"start": 0, "end": 5, "text": 1.5},
    {"start": 0, "end": 5, "text": {"a": 1}},
    {"start": 0, "end": 5, "text": [1, "dos"]},
    {"start": 0, "end": 5, "text": 0},
    {"start": 0, "end": 5, "text": False},
    {"start": 0, "end": 5, "text": True},
    {"start": 0, "end": 5, "text_source": None},
    {"start": 0, "end": 5, "text_source": "x" * 40},
    {"start": 0, "end": 5, "text_source": 5},
    {"start": 0, "end": 5, "text_source": "captions"},
    {"start": 0, "end": 5, "reference_id": 7},
    {"start": 0, "end": 5, "reference_id": ""},
    {"start": 0, "end": 5, "reference_id": "sd_1"},
    {"start": 0, "end": 5, "composition_id": {"x": 1}, "placed_clip_id": 0},
    {"start": 0, "end": 5, "composition_id": "c1", "placed_clip_id": "clip_9"},
    {"start": 0, "end": 5, "composition_id": [], "placed_clip_id": 3},
    {"start": 0, "end": 5, "complexity": 3},
    {"start": 0, "end": 5, "complexity": "4"},
    {"start": 0, "end": 5, "complexity": " 2 "},
    {"start": 0, "end": 5, "complexity": "4.0"},
    {"start": 0, "end": 5, "complexity": "1_0"},
    {"start": 0, "end": 5, "complexity": 5.9},
    {"start": 0, "end": 5, "complexity": 0.5},
    {"start": 0, "end": 5, "complexity": -0.5},
    {"start": 0, "end": 5, "complexity": True},
    {"start": 0, "end": 5, "complexity": False},
    {"start": 0, "end": 5, "complexity": 6},
    {"start": 0, "end": 5, "complexity": 1e300},
    {"start": 0, "end": 5, "complexity": NAN},
    {"start": 0, "end": 5, "complexity": INF},
    {"start": 0, "end": 5, "complexity": -INF},
    {"start": 0, "end": 5, "complexity": [3]},
    {"start": 0, "end": 5, "complexity": None},
    {"start": 0, "end": 5, "components": "abc"},
    {"start": 0, "end": 5, "components": 3},
    {"start": 0, "end": 5, "components": {"source": "motion"}},
    {"start": 0, "end": 5, "components": [{"source": "motion"}] * 9},
    {"start": 0, "end": 5, "updated_at": "ayer"},
    {"start": 0, "end": 5, "updated_at": 0},
    {"start": 0, "end": 5, "updated_at": 12.5},
    {"start": 0, "end": 5, "strict": "x"},
    {"start": 0, "end": 5, "strict": []},
    {"id": "sd_full", "start": 2.25, "end": 8.0, "text": "Todo junto", "text_source": "captions", "mode": "represent",
     "instruction": "Una metáfora", "strict": True, "materials": [_MAT], "reference_id": "sd_otro", "status": "generated",
     "composition_id": "comp1", "placed_clip_id": None, "composition_intent": "Plano + PiP", "complexity": 4,
     "no_visual": False, "components": [{"source": "material", "material": _MAT, "role": "pip"}],
     "updated_at": 1690000000.5, "extra": "se pierde"},
]

COMPONENTS = [
    None, "x", [], {},
    {"source": "motion"},
    {"source": "motion", "role": "pip", "note": "  una   nota  "},
    {"source": "keep", "role": "nope"},
    {"source": "material"},
    {"source": "material", "material": {"kind": "clips"}},
    {"material": _MAT},
    {"source": "bogus", "material": _MAT},
    {"source": "bogus"},
    {"source": ["motion"], "material": _MAT},
    {"source": "stickman", "material": _MAT, "source_range": {"in": 1, "out": 2}},
    {"source": "material", "material": _MAT, "source_range": {"in": 1.23456, "out": 4.5}},
    {"source": "material", "material": _MAT, "source_range": {"in": -3, "out": "2"}},
    {"source": "material", "material": _MAT, "source_range": {"in": 1}},
    {"source": "material", "material": _MAT, "source_range": {"out": 1}},
    {"source": "material", "material": _MAT, "source_range": {"in": "x", "out": 2}},
    {"source": "material", "material": _MAT, "source_range": {"in": None, "out": 2}},
    {"source": "material", "material": _MAT, "source_range": {"in": "nan", "out": "inf"}},
    {"source": "material", "material": _MAT, "source_range": [1, 2]},
    {"source": "material", "material": {"kind": "images", "id": 0, "scope": "library"}, "role": "side_panel",
     "note": "n" * 250},
    {"source": "text", "note": 0},
    {"source": "graphic", "note": ["a"]},
]

DOCS = [
    None, [], "x", 5, {}, {"segments": None}, {"segments": []}, {"segments": "ab"}, {"segments": {"a": 1}},
    {"segments": 5},
    {"version": 7, "segments": [
        {"id": "sd_c", "start": 10, "end": 12, "reference_id": "sd_a"},
        {"id": "sd_a", "start": 0, "end": 3, "reference_id": "sd_a"},
        {"id": "sd_b", "start": 0, "end": 2, "reference_id": "sd_zz"},
        {"id": "sd_a", "start": 20, "end": 25, "mode": "explain"},
        {"id": "x", "start": 5, "end": 6},
        {"id": "y", "start": 5, "end": 6},
        {"start": 7, "end": 7.1},
        "no", None,
        {"id": "sd_d", "start": 30, "end": 31, "reference_id": "sd_c"},
        {"id": "sd_e", "start": 0, "end": 2, "reference_id": 5},
    ]},
    {"segments": [{"id": "sd_1", "start": 0, "end": 3}, {"id": "sd_2", "start": 4, "end": 5, "mode": [1]}]},
    {"segments": [{"id": "sd_1", "start": 0, "end": 3, "complexity": INF}]},
]

BLUEPRINTS = [
    None, [], "x", {}, {"direction": "nope"}, {"direction": " swiss "}, {"direction": "SWISS"},
    {"direction": 5}, {"direction": ""}, {"direction": None, "source": "ai"},
    {"direction": "sketchbook", "source": "ai"},
    {"direction": "blueprint", "source": "AI", "updated_at": "hoy"},
    {"direction": "ink_wash", "source": None, "updated_at": 0},
    {"direction_overrides": {"accent": "  #fff ", "bg": 5, "notes": "n" * 300, "ink": "   ", "surface": "#000",
                             "secondary": "#123456", "extra": "x"}},
    {"direction_overrides": {"forbidden": ["a", "", None, 0, "  ", 5, "b" * 100, "  c  d  ", False, [], {"k": 1},
                                           "e", "f", "g", "h", "i", "j", "k"]}},
    {"direction_overrides": {"forbidden": "texto"}},
    {"direction_overrides": {"accent": "", "forbidden": []}},
    {"direction_overrides": "rojo"},
    {"identity": "  Un mundo   de papel  "},
    {"identity": "i" * 500},
    {"identity": "ab " * 200},
    {"vocabulary": ["ab " * 20]},
    {"identity": 0},
    {"vocabulary": "a, b;c,,  ;  d  "},
    {"vocabulary": ",;"},
    {"vocabulary": ["stickman", "", None, "  ", "x" * 50, 3, "diagram"]},
    {"vocabulary": [str(i) for i in range(15)]},
    {"vocabulary": 5},
    {"vocabulary": {"a": 1}},
    {"rules": "una regla"},
    {"rules": "  "},
    {"rules": ["r" * 200, "dos", ""] + [f"regla {i}" for i in range(12)]},
    {"rules": {"a": 1}},
    {"intensity_curve": " sube  y baja "},
    {"intensity_curve": "c" * 400},
    {"source": "ai"},
    {"source": "ai", "updated_at": 5},
    {"direction": "documentary", "direction_overrides": {"accent": "#ff0000", "forbidden": ["neón"]},
     "identity": "Documental sobrio", "vocabulary": ["movie_footage", "chart"], "rules": ["Una idea por plano"],
     "intensity_curve": "Sube al final", "source": "manual", "version": 9, "updated_at": 1700000001.25},
]

# --- Proyectos ---------------------------------------------------------------------------------

_SENTENCES = [
    "Hola a todos.", "Hoy veremos cómo funciona", "un motor de combustión interna.", "Primero, la admisión:",
    "el pistón baja", "y entra aire.", "¿Y después?", "Compresión…", "«Es la clave.»", "\"Sin ella no hay nada.\"",
    "(nota al margen)", "Explosión!", "Escape.", "Fin del ciclo.", "Ahora repetimos", "todo otra vez", "más rápido.",
]


def _caption_clips() -> list[dict]:
    out, t = [], 0.0
    for i, sentence in enumerate(_SENTENCES):
        words = sentence.split()
        items, rel = [], 0.0
        for w in words:
            items.append((w, round(rel, 3), round(rel + 0.3, 3)))
            rel += 0.35
        dur = round(rel + 0.1, 3)
        out.append(_clip(f"cap{i}", "T1", "text", start=round(t, 3), out_point=dur, text=sentence,
                         words=_words(*items), text_role="caption"))
        # pausas de varios tamaños: algunas cortan frase (> 0,6 s) y otras dejan huecos para auto_segments
        t += dur + (0.2, 0.7, 1.2, 0.05, 2.0)[i % 5]
    return out


_CAPTIONS = {
    "id": "p-cap", "name": "Subtítulos", "created_at": "2026-03-01T10:00:00",
    "timeline": {
        "tracks": [_track("V1", "video"), _track("T1", "text"), _track("A1", "audio")],
        "clips": [
            _clip("v1", "V1", "video", start=0.0, in_point=0, out_point=30.5),
            *_caption_clips(),
            _clip("tsin", "T1", "text", start=60.0, out_point=2.0, text="  Frase   sin palabras  ",
                  text_role="caption"),
            _clip("tvacio", "T1", "text", start=63.0, out_point=1.0, text="   ", text_role="caption"),
            _clip("tfree", "T1", "text", start=64.0, out_point=1.0, text="Texto libre.", text_role="free"),
            _clip("torig", "T1", "text", start=66.0, out_point=1.5, text="Con origen", origin={"audio_id": "a1"}),
            _clip("tplain", "T1", "text", start=68.0, out_point=1.0, text="Sin rol"),
            _clip("img", "V1", "image", asset_kind="images", asset_id="i1", start=70.0, out_point=3.0),
        ],
    },
    "scene_directions": {"version": 1, "segments": [
        {"id": "sd_dir1", "start": 6.0, "end": 9.5, "mode": "explain", "instruction": "Mostrar el pistón",
         "status": "ready", "updated_at": 1690000000.0},
        {"id": "sd_dir2", "start": 20.0, "end": 24.0, "composition_id": "comp7", "status": "generated",
         "updated_at": 1690000001.0, "complexity": 3, "materials": [_MAT]},
        {"id": "sd_nv", "start": 30.0, "end": 33.0, "no_visual": True, "updated_at": 1690000002.0},
        {"id": "sd_pl", "start": 40.0, "end": 41.0, "status": "placed", "placed_clip_id": "v1",
         "components": [{"source": "motion"}], "updated_at": 1690000003.0},
        {"id": "x", "start": 50.0, "end": 52.0},
    ]},
    "visual_blueprint": {"direction": "whiteboard", "identity": "Pizarra", "updated_at": 1690000004.0},
}

_TRANSCRIPT = {
    "id": "p-tr", "name": "Transcripción", "created_at": "2026-03-02T10:00:00",
    "clips": [
        {"index": 0, "filename": "c0.mp4", "url": "/c/0", "start": 0, "end": 20,
         "transcript": {"id": "t0", "model": "base", "segments": [
             {"start": 0.0, "end": 3.0, "text": "uno dos tres",
              "words": _words(("Uno", 0.0, 0.5), ("dos", 0.6, 1.0), ("tres.", 1.1, 1.6))},
             {"start": 3.0, "end": 6.0, "text": "Una frase entera sin palabras."},
             {"start": 6.0, "end": 9.0, "text": "otra",
              "words": _words(("Fuera", 6.0, 6.5), ("del", 6.6, 7.0), ("tramo", 7.1, 9.0))},
         ]}},
        {"index": 1, "filename": "c1.mp4", "url": "/c/1", "start": 0, "end": 10,
         "transcript": {"id": "t1", "model": "base", "segments": [{"start": 0, "end": 2, "text": "La primera versión."}]}},
        {"index": 1, "filename": "c1b.mp4", "url": "/c/1b", "start": 0, "end": 10,
         "transcript": {"id": "t1b", "model": "base", "segments": [
             {"start": 0.5, "end": 2.5, "text": "Gana la última"}, {"start": 2.6, "end": 4.0, "text": "con el mismo índice."}]}},
        {"index": 2, "filename": "c2.mp4", "url": "/c/2", "start": 0, "end": 10},
        {"index": 3, "filename": "c3.mp4", "url": "/c/3", "start": 0, "end": 10,
         "transcript": {"id": "t3", "model": "base", "segments": []}},
    ],
    "timeline": {
        "tracks": [_track("V1", "video"), _track("V2", "video")],
        "clips": [
            _clip("tv0", "V1", "video", asset_id="0", start=2.0, in_point=0.8, out_point=7.5, speed=2.0),
            _clip("tv1", "V1", "video", asset_id="1", start=10.0, in_point=0.0, out_point=5.0),
            _clip("tv2", "V2", "video", asset_id="2", start=0.0, in_point=0.0, out_point=3.0),
            _clip("tv3", "V2", "video", asset_id="3", start=4.0, in_point=0.0, out_point=3.0),
            _clip("tv9", "V2", "video", asset_id="9", start=8.0, in_point=0.0, out_point=3.0),
            _clip("tv0b", "V2", "video", asset_id="0", start=20.0, in_point=0.0, out_point=2.0, speed=0.5),
        ],
    },
}

_AUDIO_TEXT = "...Primera frase. Segunda frase con ñandú!  Tercera…\nCuarta sin punto\n\n¿Quinta? Sexta.  "

_AUDIO = {
    "id": "p-aud", "name": "Audio", "created_at": "2026-03-03T10:00:00",
    "audios": [
        {"id": "a0", "filename": "a0.wav", "url": "/a/0", "duration": 5.0},
        {"id": "a1", "filename": "a1.wav", "url": "/a/1", "text": "   ", "duration": 5.0},
        {"id": "a2", "filename": "a2.wav", "url": "/a/2", "text": _AUDIO_TEXT, "duration": 12.0},
    ],
    "timeline": {
        "tracks": [_track("A1", "audio")],
        "clips": [
            _clip("au0", "A1", "audio", asset_kind="audios", asset_id="a0", start=0.0, out_point=5.0),
            _clip("au1", "A1", "audio", asset_kind="audios", asset_id="a1", start=0.0, out_point=5.0),
            _clip("auX", "A1", "audio", asset_kind="audios", asset_id="zz", start=0.0, out_point=5.0, source_duration=5.0),
            _clip("au2", "A1", "audio", asset_kind="audios", asset_id="a2", start=1.0, in_point=2.0, out_point=10.0,
                  source_duration=0.0, speed=0.0),
        ],
    },
}

_AUDIO2 = {
    "id": "p-aud2", "name": "Audio 2", "created_at": "2026-03-04T10:00:00",
    "audios": [
        {"id": "b1", "filename": "b1.wav", "url": "/a/1", "text": "Antes. Mucho antes.", "duration": 10.0},
        {"id": "b2", "filename": "b2.wav", "url": "/a/2", "text": "Rápido uno. Rápido dos. Rápido tres.", "duration": 9.0},
    ],
    "timeline": {
        "tracks": [_track("A1", "audio")],
        "clips": [
            # todo su texto queda antes del in_point: no da frases y se pasa al siguiente
            _clip("bb1", "A1", "audio", asset_kind="audios", asset_id="b1", start=0.0, in_point=10.0, out_point=10.5,
                  source_duration=10.0),
            _clip("bb2", "A1", "audio", asset_kind="audios", asset_id="b2", start=3.0, in_point=1.0, out_point=9.0,
                  source_duration=9.0, speed=2.0),
        ],
    },
}

_NOTHING = {
    "id": "p-nada", "name": "Nada", "created_at": "2026-03-05T10:00:00",
    "timeline": {"tracks": [_track("V1", "video")],
                 "clips": [_clip("i1", "V1", "image", asset_kind="images", asset_id="i1", start=1.5, out_point=4.25),
                           _clip("i2", "V1", "image", asset_kind="images", asset_id="i1", start=0.0, out_point=5.75),
                           _clip("i3", "V1", "image", asset_kind="images", asset_id="i1", start=0.0, out_point=1.0)]},
    "scene_directions": {"segments": [{"start": 0, "end": 3, "status": "placed"}]},
}

# Una pausa de 0,65 s que corta frase, un tramo que pasaría de MAX_SEG = 8 (8,5 s) sin llegar a 10 y un
# primer tramo que empieza en 0,5 s (pasa a 0).
_CAPTIONS2 = {
    "id": "p-cap2", "name": "Subtítulos 2", "created_at": "2026-03-08T10:00:00",
    "timeline": {
        "tracks": [_track("T1", "text")],
        "clips": [_clip("k1", "T1", "text", start=0.0, out_point=12.0, text="uno dos tres. cuatro. cinco seis.",
                        text_role="caption",
                        words=_words(("uno", 0.5, 0.8), ("dos", 1.45, 1.7), ("tres.", 1.75, 2.0), ("cuatro.", 8.0, 9.0),
                                     ("cinco", 9.5, 10.0), ("seis.", 10.1, 12.0)))],
    },
}

_NO_TIMELINE = {"id": "p-sin", "name": "Sin timeline", "created_at": "2026-03-06T10:00:00"}
_EMPTY = {"id": "p-vacia", "name": "Vacía", "created_at": "2026-03-07T10:00:00",
          "timeline": {"tracks": [_track("V1", "video")], "clips": []}}

PROJECTS = [_CAPTIONS, _CAPTIONS2, _TRANSCRIPT, _AUDIO, _AUDIO2, _NOTHING, _NO_TIMELINE, _EMPTY]

# Rangos de text_in_range sobre las frases de cada proyecto.
RANGES = [[0, 5], [0, 1000], [5.0, 12.0], [-1, 0], [10, 10], [0, 0.5], [0, 0.65]]

# Escaletas a conservar en auto_segments («load» = la guardada en el proyecto). Se normalizan con
# normalize_doc antes de pasarlas.
KEEPS = [
    [],
    "load",
    [{"id": "sd_k1", "start": 3.0, "end": 8.0, "mode": "represent"},
     {"id": "sd_k2", "start": 12.0, "end": 14.0},
     {"id": "sd_k3", "start": 14.5, "end": 15.5, "composition_id": "c9"},
     {"id": "sd_k4", "start": 0.0, "end": 0.9, "instruction": "abre"}],
    [{"id": "sd_all", "start": 0.0, "end": 1000.0, "mode": "explain"}],
]

# --- Pasos sobre projects.json -----------------------------------------------------------------

STORE_PROJECTS = [
    {"id": "s-main", "name": "Principal", "created_at": "2026-04-01T10:00:00",
     "scene_directions": {"version": 1, "segments": [
         {"id": "sd_s1", "start": 0.0, "end": 4.0, "text": "uno", "updated_at": 1690000000.0},
         {"id": "sd_s2", "start": 4.0, "end": 9.0, "mode": "explain", "updated_at": 1690000001.0},
         {"id": "sd_s3", "start": 9.0, "end": 12.0, "reference_id": "sd_s2", "updated_at": 1690000002.0},
         {"id": "bad", "start": 12.0, "end": 14.0}]}},
    {"id": "s-vacio", "name": "Vacío", "created_at": "2026-04-02T10:00:00"},
]

STORE = [
    ["load", "s-main"],
    ["load", "s-vacio"],
    ["get_segment", "s-main", "sd_s2"],
    ["get_segment", "s-main", "bad"],
    ["get_segment", "s-main", "nope"],
    ["update_segment", "s-main", "sd_s1", {"mode": "explain", "instruction": "  ver  "}],
    ["update_segment", "s-main", "sd_s2", {"id": "otro", "status": "placed", "foo": 1, "updated_at": 5}],
    ["update_segment", "s-main", "nope", {"mode": "explain"}],
    ["update_segment", "nada", "sd_s1", {"mode": "explain"}],
    ["update_segment", "s-main", "sd_s3", ["x"]],
    ["update_segment", "s-main", "sd_s3", {}],
    ["update_segment", "s-main", "sd_s3", {"reference_id": "sd_s3"}],
    ["update_segment", "s-main", "sd_s3", {"end": 9.1}],
    ["update_segment", "s-main", "sd_s1", {"mode": ["x"]}],
    ["apply_plan_all", "s-main", [
        {"id": "sd_s1", "mode": "represent", "complexity": 9, "instruction": "no cuenta"},
        {"id": "zz", "mode": "explain"}, {"id": "sd_s2", "no_visual": True, "composition_intent": "PiP"},
        "x", {"mode": "explain"}, {"id": "sd_s2", "complexity": 2}, {"id": ""}]],
    ["apply_plan_all", "s-main", None],
    ["apply_plan_all", "s-main", [{"id": "sd_s1"}]],
    ["apply_plan_all", "s-main", "texto"],
    ["apply_plan_all", "s-main", 5],
    ["apply_plan_all", "nada", []],
    ["save", "s-vacio", {"segments": [{"start": 1, "end": 4, "mode": "propose"}, {"start": 0, "end": 1}]}],
    ["save", "s-vacio", {"segments": 5}],
    ["save", "nada", {"segments": []}],
    ["save", "s-vacio", []],
    ["load_blueprint", "s-main"],
    ["save_blueprint", "s-main", {"direction": "newspaper", "rules": "Titulares grandes", "source": "ai"}],
    ["load_blueprint", "s-main"],
    ["save_blueprint", "s-main", {}],
    ["save_blueprint", "s-main", {"direction": "nope"}],
    ["save_blueprint", "nada", {"direction": "swiss"}],
    ["save_blueprint", "s-vacio", "x"],
    ["status_summary", "s-main"],
    ["status_summary", "s-vacio"],
]


# --- El material (2.ª tanda): key_terms y _strip_accents de motion/segment_context.py, la metadata
# semántica, material_catalog, rank_materials y set_material_meta ------------------------------------

# key_terms(texto, limit): nombres propios a mitad de frase, palabras largas, cifras, muletillas con y sin
# tilde, guiones, y Unicode que no es Latin-1 (isupper, isdigit, \w, la sigma final y la NFD).
KEY_TEXTS = [
    ["", 6], [None, 6],
    ["La ecuación de Navier-Stokes describe fluidos turbulentos", 6],
    ["OpenAI lanzó GPT-4 en 2023; luego OpenAI volvió", 6],
    ["Pero también después, había mucho cuando sobre entre", 6],
    ["TAMBIÉN Después SÓLO quedó Quedo", 6],
    ["uno dos tres cuatro cinco seis siete ocho nueve diez once doce", 6],
    ["palabras larguísimas repetidas larguisimas LARGUÍSIMAS otra-vez", 6],
    ["--guion -medio- final-- a-b-c ---", 6],
    ["x2 3D Ab1 ²³ ①② ٣ ๓ Ⅷ", 10],
    ["Él dijo Ñandú y Ángela, Über Ölmühle", 6],
    ["texto ΟΔΟΣ Οδυσσέας ΣΟΦΙΑ Ἄλφα", 6],
    ["İstanbul İİİİ ǅungla ǈubica Ǳx", 6],
    ["ﬁnancial ﬂuffiness straße STRASSE", 6],
    ["한국어 텍스트 서울특별시 대한민국", 6],
    ["Ａｂｃｄｅｆ ＡＢＣＤ fullwidth ｗｉｄｅｓｔ", 6],
    ["éléctrique caféine naïvement", 6],
    ["_privado_ __dunder__ snake_case_name", 6],
    ["emoji 🚀cohete 🎬película misión🌍", 6],
    ["tabs\tand\nnewlines nbsp em", 6],
    ["Primera Segunda Tercera Cuarta Quinta Sexta Séptima Octava", 3],
    ["Primera Segunda Tercera Cuarta Quinta Sexta Séptima Octava", 10],
    ["Primera Segunda Tercera", 1],
    ["corta Larguísima", 0],
    ["Larguísima corta", 0],
    ["ὈΔΥΣΣΕΎΣ Ᾰᾱ ᾼ ᾈ", 6],
    ["ڪراچي पाकिस्तान தமிழ்நாடு ကမ္ဘာ", 6],
    ["dígitos१२३ ٤٥٦ ⁴⁵⁶ ₇₈₉", 6],
    ["Ⓐbcd ⒶⒷⒸⒹ Ⅻabc ℌilbert", 6],
    # Muletillas que solo lo son sin tilde, y nombres propios de 3 letras (no cuentan).
    ["el siémpre porqué duránte», «cuándo", 6],
    ["hola Ana Bob Eva", 6],
]

# _strip_accents directo: NFD sin Mn, con el orden canónico (marcas Mc con clase de combinación), el CGJ
# (Mn de clase 0) entre medias y los hangul.
STRIP_TEXTS = [
    "", "abc", "áéíóúüñÁÉÍÓÚÜÑ", "ệṩǖ", "é", "한국어", "가각힣", "ﬁ", "Å", "Ω",
    "a\U0001D16D\U0001D165", "a\U0001D16D͏\U0001D165", "\U0001D15F\U0001D16E", "ཱཱིུ",
    "à֮̕", "ḍ̇", "̈́", "가", "क़", "ǅ",
]

SEMANTICS = [
    None, "x", [], {}, 5,
    {"subjects": "perro, gato;  ;ratón,", "actions": ["corre", "  ", None, 0, "", "salta  alto", 7, ["x"]]},
    {"subjects": ["a", "b", "c", "d", "e", "f", "g", "h", "i", "j"], "actions": "uno"},
    {"subjects": ["x" * 70, "  espacios   en   medio  "], "actions": [True, False, 1.5, {"k": 1}]},
    {"subjects": 5, "actions": {"a": 1}, "visual_content": "  plano  general ", "environment": "x" * 250,
     "mood": None, "composition": 7, "suggested_usage": ["lista"], "visual_priority": "apoyo"},
    {"visual_priority": "Apoyo"}, {"visual_priority": ["fondo"]}, {"visual_priority": "fondo", "otro": 1},
    {"subjects": ("tupla",), "mood": "", "composition": "   "},
    {"subjects": "", "actions": ",;,"},
    {"subjects": ["ñandú", "Ελλάδα"], "mood": "triste y oscuro"},
    {"subjects": "\x1cuno\x85, \u2003dos\u3000;\xa0", "actions": ["\u2028tres\x1f", "\u200b"],
     "mood": "\x0b\x0cmuy\u2009\u2009tenso\x1d\x1e", "composition": "\x85"},
]

BLOBS = [
    None, {}, "x", {"subjects": ["a", "b"], "actions": ["c"], "mood": "m", "visual_priority": "apoyo"},
    {"subjects": "abc", "environment": "e"}, {"subjects": {"k1": 1, "k2": 2}}, {"actions": 5},
    {"actions": True}, {"subjects": [1, 2]}, {"mood": 5}, {"mood": 0, "composition": ""},
    {"subjects": [], "actions": None, "visual_content": "v", "environment": "e", "mood": "m", "composition": "c",
     "suggested_usage": "u"},
]


def _mat_clip(index: int, **kw) -> dict:
    out = {"index": index, "filename": f"c{index}.mp4", "url": f"/clips/c{index}.mp4", "start": 0.0, "end": 10.0}
    out.update(kw)
    return out


def _mat_image(iid: str, **kw) -> dict:
    out = {"id": iid, "filename": f"{iid}.png", "url": f"/images/{iid}.png"}
    out.update(kw)
    return out


MATERIAL_PROJECTS = [
    {"id": "m-main", "name": "Material", "created_at": "2026-05-01T10:00:00",
     "clips": [
         _mat_clip(1, label="Perro corriendo", description="Un perro corre por la playa", start=2.0, end=7.5),
         _mat_clip(2, label="", description_ai="Vista aérea de una ciudad de noche", in_point=1.25, out_point=4.0),
         _mat_clip(3, description="", semantic={"subjects": ["volcán"], "actions": ["erupción"]},
                   start=0.0, end=3.3333),
         _mat_clip(4, label="Sin nada", semantic={}),
         _mat_clip(5, label="Medio segmento", in_point=2.0, start=1.0, end=6.0),
     ],
     "images": [
         _mat_image("im1", label="Mapa de Europa", description="Mapa político", description_ai="no se usa"),
         _mat_image("im2", semantic={"subjects": ["Ecuación", "Navier-Stokes"], "mood": "técnico"}),
         _mat_image("im3", label="", description=""),
     ]},
    {"id": "m-vacio", "name": "Vacío", "created_at": "2026-05-02T10:00:00"},
]

# data/library/library.json de los pasos (material_dto los ordena por saved_at, del más reciente).
LIBRARY = {"version": 1, "items": [
    {"id": "lib_a", "resource_type": "clip", "filename": "playa.mp4", "label": "Playa tropical",
     "description": "Olas en la orilla", "duration": 12.5, "saved_at": "2026-01-02T00:00:00+00:00"},
    {"id": "lib_b", "resource_type": "clip", "filename": "volcan.mp4", "label": "",
     "semantic": {"subjects": ["volcán", "lava"]}, "saved_at": "2026-01-03T00:00:00+00:00"},
    {"id": "lib_c", "resource_type": "image", "filename": "cohete.png", "label": "Cohete",
     "description": "", "semantic": {}, "duration": 3.5, "saved_at": "2026-01-01T00:00:00+00:00"},
    {"id": "lib_d", "resource_type": "audio", "filename": "voz.mp3", "label": "Voz"},
    {"id": 7, "resource_type": "image", "filename": "num.png", "label": 5, "description": 3},
]}

# rank_materials sobre el catálogo de m-main (con la biblioteca): [texto, limit].
RANK_TEXTS = [
    ["Un perro corre por la playa tropical", 5],
    ["La erupción del volcán y la lava", 20],
    ["Vista aérea de la ciudad de noche con el mapa político de Europa", 3],
    ["La ecuación de Navier-Stokes en un mapa técnico", 5],
    ["nada coincide aquí", 5],
    ["", 5],
    ["Perro perro PERRO", 1],
    ["Cohete espacial despegando", 20],
    # El volcán es el 7.º término clave: con los 6 de key_terms por defecto no contaría.
    ["alfabeto basilisco caramelo diamante elefante farmacia volcán", 5],
]

# Catálogos a mano para rank_materials (lo que no sale de material_catalog): semántica que revienta el
# join, títulos que no son texto y descripciones verdaderas que no son texto.
RANK_CATALOGS = [
    [["volcán erupción", 5], [
        {"kind": "clips", "id": "1", "scope": "project", "title": None, "description": "", "semantic": None},
        {"kind": "clips", "id": "2", "scope": "project", "title": 5, "description": 0, "semantic": {}},
        {"kind": "clips", "id": "3", "scope": "project", "title": "volcán", "description": ["erupción"]},
        {"kind": "clips", "id": "4", "scope": "project", "title": "x", "description": "",
         "semantic": {"subjects": "volcán"}},
    ]],
    [["volcán", 5], [{"kind": "clips", "id": "1", "scope": "project", "title": "t", "description": "",
                      "semantic": {"actions": 5}}]],
    [["volcán", 5], [{"kind": "clips", "id": "1", "scope": "project", "title": "t", "description": "",
                      "semantic": {"mood": 3}}]],
    [["volcán", 5], [{"kind": "clips", "id": "1", "scope": "project", "title": "t", "description": "",
                      "match": 9, "semantic": {"subjects": ["volcán"]}}]],
]

# set_material_meta paso a paso sobre projects.json (MATERIAL_PROJECTS) y library.json (LIBRARY):
# [project_id, kind, ident, meta, scope]; después de cada paso, el catálogo de m-main.
META_STEPS = [
    ["m-main", "clips", "1", {"subjects": "perro, playa", "mood": "alegre"}, "project"],
    ["m-main", "images", "im3", {"visual_priority": "fondo"}, "project"],
    ["m-main", "clips", "2", {}, "project"],
    ["m-main", "clips", "3", "x", "project"],
    ["m-main", "clips", "99", {"mood": "x"}, "project"],
    ["m-main", "images", "nope", {"mood": "x"}, "project"],
    ["m-main", "audios", "1", {"mood": "x"}, "project"],
    ["m-main", ["clips"], "1", {"mood": "x"}, "project"],
    ["nada", "clips", "1", {"mood": "x"}, "project"],
    ["m-main", "clips", "lib_a", {"subjects": ["olas"], "environment": "playa"}, "library"],
    ["m-main", "images", "lib_c", None, "library"],
    ["m-main", "clips", "lib_zz", {"mood": "x"}, "library"],
    ["m-main", "clips", "7", {"actions": ["volar"]}, "library"],
    ["", "clips", "lib_b", {"mood": "oscuro"}, "otro"],
    ["m-main", "clips", "1", {"actions": ["corre"], "visual_priority": "protagonista"}, "project"],
]


# --- El pack (3.ª tanda): build_pack, pack_text, estimate_tokens, skeleton_beats y brief_defaults, con
# caption_lines, range_words, material_usage, recent_materials y timeline_context y style_context de
# motion/segment_context.py ---------------------------------------------------------------------------

def _cap(cid: str, start: float, text: str, track: str = "T1", **kw) -> dict:
    """Un subtítulo con una palabra cada 0,35 s (0,3 s cada una)."""
    items, rel = [], 0.0
    for w in text.split():
        items.append((w, round(rel, 3), round(rel + 0.3, 3)))
        rel += 0.35
    out = _clip(cid, track, "text", start=start, out_point=round(rel + 0.1, 3), text=text, words=_words(*items),
                text_role="caption")
    out.update(kw)
    return out


_PACK_MAIN = {
    "id": "k-main", "name": "Pack", "created_at": "2026-06-01T10:00:00",
    "clips": [
        _mat_clip(1, label="Perro corriendo", description="Un perro corre por la playa",
                  semantic={"subjects": ["perro", "playa"], "actions": ["corre"], "suggested_usage": "b-roll de apoyo",
                            "visual_priority": "apoyo"}),
        _mat_clip(2, label="", description_ai="Vista aérea de una ciudad", in_point=1.0, out_point=4.0),
        _mat_clip(3, label="Volcán activo", description="", semantic={"subjects": ["volcán"], "mood": "tenso"}),
        _mat_clip(4),
        _mat_clip(5, id="clip-5", label="Con id", description="Encontrado por su id"),
    ],
    "images": [
        _mat_image("im1", label="Mapa", description="Mapa político de Europa"),
        _mat_image("im2", label="Gráfico de barras", description="", semantic={"subjects": "datos", "actions": "subir"}),
        _mat_image("im3", label="Foto", description="Foto antigua", semantic={"visual_priority": "fondo"}),
    ],
    "timeline": {
        "tracks": [{"id": "V1", "kind": "video", "name": "Principal"}, {"id": "V2", "kind": "video", "name": "Encima"},
                   {"id": "V3", "kind": "video", "name": "Oculta", "hidden": True},
                   {"id": "T1", "kind": "text", "name": "Subtítulos", "style": {"font": "Anton", "color": "#fff", "y": 0.8}},
                   {"id": "T2", "kind": "text", "name": "Arriba", "style": {"y": 0.1, "highlight_color": "#f00"}},
                   {"id": "A1", "kind": "audio", "name": "Voz"}],
        "clips": [
            _clip("v1", "V1", "video", asset_id="1", start=0.0, in_point=0.0, out_point=6.0, name="Plano perro",
                  note="  El perro   entra  "),
            _clip("v2", "V2", "video", asset_id="2", start=4.0, in_point=1.0, out_point=4.0),
            _clip("v3", "V3", "video", asset_id="3", start=0.0, in_point=0.0, out_point=30.0),
            _clip("sh1", "V2", "shape", asset_kind="shape", asset_id="s", start=2.0, out_point=1.5,
                  description="Flecha roja"),
            _clip("img1", "V2", "image", asset_kind="images", asset_id="im1", start=8.0, out_point=2.5),
            _clip("mo1", "V2", "motion", asset_id="m", start=9.5, out_point=4.0, name="Gráfico animado",
                  composition_id="comp1"),
            _clip("mo2", "V2", "motion", asset_id="m", start=12.0, out_point=0.5),
            _clip("vlib", "V1", "video", asset_id="lib_a", asset_scope="library", start=14.0, out_point=3.0),
            _clip("im2u", "V2", "image", asset_kind="images", asset_id="im2", start=16.0, out_point=1.0),
            _clip("v1b", "V1", "video", asset_id="1", start=18.0, in_point=0.0, out_point=2.0),
            _clip("v5", "V2", "video", asset_id="clip-5", start=19.0, out_point=0.8),
            _clip("v4", "V2", "video", asset_id="4", start=21.0, out_point=1.0, description="Del clip"),
            _clip("vx", "V2", "video", asset_id="99", start=25.0, out_point=2.0, description="Material perdido"),
            # Una imagen en una pista de texto: para el clip activo, su pista no tiene orden (-1).
            _clip("imgT", "T2", "image", asset_kind="images", asset_id="im3", start=26.0, out_point=2.0),
            _clip("a1", "A1", "audio", asset_kind="audios", asset_id="a1", start=0.0, out_point=30.0),
            _clip("tf1", "T2", "text", start=3.0, out_point=2.0, text="Título  libre", text_role="free"),
            _cap("c1", 0.0, "Hoy hablamos del perro, el mejor amigo."),
            _cap("c2", 2.8, "Corre por la playa cada mañana."),
            # Antes que c3 en la lista y después en el tiempo: caption_lines ordena por inicio.
            _cap("c8", 6.0, "Arriba", track="T2", style={"y": 0.15}),
            _cap("c3", 5.2, "Después, mira el volcán: humo, lava, ceniza y miedo."),
            _cap("c4", 9.0, "El mapa de Europa cambia."),
            _clip("c5", "T1", "text", start=11.0, out_point=2.0, text="  Una línea   sin palabras  ",
                  text_role="caption"),
            _cap("c6", 13.5, "Fin."),
            _cap("c7", 20.0, "Otra vez el perro corre rápido sin parar nunca jamás hoy mismo aquí"),
        ],
    },
    "scene_directions": {"version": 1, "segments": [
        {"id": "sd_a", "start": 0.0, "end": 5.0, "mode": "explain", "instruction": "Mostrar al perro",
         "materials": [{"kind": "clips", "id": "1"}], "updated_at": 1690000000.0,
         "components": [{"source": "material", "material": {"kind": "clips", "id": "1"}, "role": "full",
                         "source_range": {"in": 0.5, "out": 4}},
                        {"source": "motion", "role": "overlay", "note": "Flecha"}]},
        {"id": "sd_b", "start": 5.0, "end": 9.0, "mode": "reinforce", "reference_id": "sd_a", "strict": True,
         "complexity": 3, "updated_at": 1690000001.0},
        {"id": "sd_c", "start": 9.0, "end": 14.0, "instruction": "El mapa cambia", "composition_intent": "Mapa entero",
         "materials": [{"kind": "images", "id": "im1"}, {"kind": "images", "id": "im3"},
                       {"kind": "clips", "id": "lib_a", "scope": "library"}], "updated_at": 1690000002.0},
        {"id": "sd_d", "start": 14.0, "end": 20.0, "mode": "material",
         "materials": [{"kind": "images", "id": 7, "scope": "library"}], "updated_at": 1690000003.0},
        {"id": "sd_e", "start": 20.0, "end": 30.0, "no_visual": True, "updated_at": 1690000004.0},
        {"id": "x", "start": 30.0, "end": 40.0},
    ]},
    # Sin updated_at: normalize_blueprint pide la hora al cargarla (en el pack, después de normalizar).
    "visual_blueprint": {"direction": "documentary", "identity": "Documental sobrio",
                         "vocabulary": ["movie_footage", "chart"], "rules": ["Una idea por plano", "Sin neón"],
                         "intensity_curve": "Sube al final", "direction_overrides": {"accent": "#ff0000"}},
}

_PACK_TRANSCRIPT = {
    "id": "k-tr", "name": "Pack transcripción", "created_at": "2026-06-02T10:00:00",
    "clips": [
        {"index": 0, "filename": "c0.mp4", "url": "/c/0", "start": 0, "end": 20, "label": "Entrevista",
         "transcript": {"id": "t0", "model": "base", "segments": [
             {"start": 0.0, "end": 3.0, "text": "uno dos tres",
              "words": _words(("Primero,", 0.0, 0.5), ("la", 0.6, 1.0), ("idea.", 1.1, 1.6), ("Luego", 1.7, 2.0),
                              ("viene", 2.1, 2.4), ("lo", 2.5, 2.6), ("demás,", 2.7, 3.0))},
             {"start": 3.0, "end": 6.0, "text": "Una frase entera sin palabras."},
         ]}},
    ],
    "timeline": {
        "tracks": [_track("V1", "video"), _track("V2", "video")],
        "clips": [_clip("tv0", "V1", "video", asset_id="0", start=2.0, in_point=0.0, out_point=6.0),
                  _clip("tv1", "V2", "video", asset_id="0", start=1.0, in_point=0.0, out_point=1.0, speed=2.0)],
    },
}

_PACK_AUDIO = {
    "id": "k-aud", "name": "Pack audio", "created_at": "2026-06-03T10:00:00",
    "audios": [{"id": "b2", "filename": "b2.wav", "url": "/a/2", "text": "Rápido uno. Rápido dos. Rápido tres.",
                "duration": 9.0}],
    "timeline": {"tracks": [_track("A1", "audio")],
                 "clips": [_clip("bb2", "A1", "audio", asset_kind="audios", asset_id="b2", start=0.0, in_point=0.0,
                                 out_point=9.0, source_duration=9.0)]},
}

# Subtítulos en pistas con estilos distintos: `y` booleano, entero, texto; el estilo del clip pisa el de la
# pista; empates de distancia (gana el primero de la timeline) y un formato que no es el de por defecto.
_PACK_STYLES = {
    "id": "k-sty", "name": "Pack estilos", "created_at": "2026-06-04T10:00:00",
    "timeline": {
        "fps": 60, "width": 1080, "height": 1920,
        "tracks": [{"id": "T1", "kind": "text", "name": "T1", "style": {"font": "Inter", "color": "#111", "y": True}},
                   {"id": "T2", "kind": "text", "name": "T2", "style": {"y": "0.5", "font": ""}},
                   {"id": "T3", "kind": "text", "name": "T3"},
                   {"id": "T1", "kind": "text", "name": "T1 repetida", "style": {"font": "Repetida"}}],
        "clips": [
            _cap("s1", 0.0, "Uno dos tres.", style={"color": "#abc", "y": 0.95}),
            _cap("s2", 10.0, "Cuatro cinco.", track="T2"),
            _cap("s3", 20.0, "Seis siete.", track="T3", style={"y": 0.03, "highlight_color": "#ff0"}),
            _cap("s4", 30.0, "Ocho.", style={"y": 1}),
            _cap("s5", 40.0, "Nueve.", track="T9", style={"y": -0.5, "font": "Sola"}),
            _cap("s6", 50.0, "Diez.", style={"color": "#0f0"}),
            # La coma de «seis,» llega a 2,05 s: corta con el ritmo medio (≥ 0,8 × 2,5) aunque no llegue a 2,5.
            _cap("s7", 60.0, "uno dos tres cuatro cinco seis, siete ocho."),
            # Un texto libre en la pista T1 repetida: su nombre es el de la última con ese id.
            _clip("libre", "T1", "text", start=0.0, out_point=2.0, text="Libre", text_role="free"),
        ],
    },
}

# Más de MAX_ELEMENTS elementos, un clip con nombre vacío, composition_id fuera de un motion, una descripción
# igual al nombre, una nota y una descripción largas, un vídeo de «None» (no es el str() del id que falta: al
# cargar, un clip sin id recibe su índice), uno de una colección, un subtítulo de 0,05 s justos y un material
# usado cuatro veces.
_PACK_FULL = {
    "id": "k-full", "name": "Pack lleno", "created_at": "2026-06-07T10:00:00",
    "clips": [_mat_clip(1, label="Uno", description="Clip muy usado"), _mat_clip(2, label="Dos", description="Otro")],
    "timeline": {
        "tracks": [_track("V1", "video"), _track("V2", "video"), _track("T1", "text")],
        "clips": [
            # Empiezan en -0.0: float(c.start or 0.0) da 0.0.
            _clip("cero", "V2", "shape", asset_kind="shape", asset_id="s", start=-0.0, out_point=0.3),
            _clip("vcero", "V1", "video", asset_id="2", start=-0.0, out_point=0.5),
            *[_clip(f"f{i}", "V2", "shape", asset_kind="shape", asset_id="s", start=i * 0.5, out_point=0.4)
              for i in range(22)],
            _clip("noname", "V1", "video", asset_id="1", start=1.0, out_point=1.0, name=""),
            _clip("vcomp", "V1", "video", asset_id="1", start=2.0, out_point=1.0, composition_id="c-v"),
            _clip("igual", "V2", "shape", asset_kind="shape", asset_id="s", start=3.0, out_point=1.0, name="Igual",
                  description="Igual"),
            _clip("nota", "V1", "video", asset_id="1", start=4.0, out_point=1.0, note="n" * 250),
            _clip("vnone", "V1", "video", asset_id="None", start=5.0, out_point=1.0, description="Desc del clip"),
            _clip("vcol", "V1", "video", asset_kind="collection", asset_id="2", start=6.0, out_point=1.0),
            _clip("dlarga", "V2", "shape", asset_kind="shape", asset_id="s", start=7.0, out_point=1.0,
                  description="d" * 150),
            _clip("v1x", "V1", "video", asset_id="1", start=8.0, out_point=1.0),
            _clip("mini", "T1", "text", start=0.0, out_point=0.05, text="Mini", text_role="caption"),
        ],
    },
}

_PACK_NONE = {"id": "k-sin", "name": "Pack sin timeline", "created_at": "2026-06-05T10:00:00"}

# Metadata semántica que _sem_note no puede escribir (o sí, por poco) y títulos para _material_name.
_PACK_WEIRD = {
    "id": "k-raro", "name": "Pack raro", "created_at": "2026-06-06T10:00:00",
    "clips": [
        _mat_clip(1, label="Texto más lista", semantic={"subjects": "abc", "actions": ["x"]}),
        _mat_clip(2, label="Suma cero", semantic={"subjects": 5, "actions": -5}),
        _mat_clip(3, label="Suma ocho", semantic={"subjects": 5, "actions": 3}),
        _mat_clip(4, label="Prioridad número", semantic={"visual_priority": 7}),
        _mat_clip(5, label="Lista con número", semantic={"subjects": [1, "a"]}),
        _mat_clip(6, label="Cinco etiquetas", semantic={"subjects": ["a", "b", "c", "d", 5]}),
        _mat_clip(7, label="Dos textos", semantic={"subjects": "ab", "actions": "cdef"}),
        _mat_clip(8, label="Uso en lista", semantic={"suggested_usage": ["lista"], "visual_priority": "fondo"}),
        _mat_clip(9, label="Dict", semantic={"subjects": {"k": 1}}),
        _mat_clip(10, label="Reales cero", semantic={"subjects": 1.5, "actions": -1.5}),
        _mat_clip(11, label="Bool cero", semantic={"subjects": True, "actions": -1}),
        _mat_clip(12, label="Perro grande", description="perro"),
        _mat_clip(13, label="Real y entero", semantic={"subjects": 1.5, "actions": -1}),
    ],
    "images": [
        _mat_image("w1", label="", description=""),
        _mat_image("w2", label="Gato", description="gato negro"),
        _mat_image("w3", label="Gato negro en la noche", description="NEGRO"),
        _mat_image("w4", label="İstanbul", description="i̇stanbul"),
        _mat_image("w5", label="Ciudad", description="  Una ciudad   " + "muy " * 40 + "grande"),
    ],
    "timeline": {"tracks": [_track("V1", "video")], "clips": []},
}

PACK_PROJECTS = [_PACK_MAIN, _PACK_TRANSCRIPT, _PACK_AUDIO, _PACK_STYLES, _PACK_NONE, _PACK_WEIRD, _PACK_FULL]

PACK_LIBRARY = {"version": 1, "items": [
    {"id": "lib_a", "resource_type": "clip", "filename": "playa.mp4", "label": "Playa tropical",
     "description": "Olas en la orilla", "duration": 12.5, "semantic": {"subjects": ["olas"]},
     "saved_at": "2026-01-02T00:00:00+00:00"},
    {"id": "lib_b", "resource_type": "clip", "filename": "volcan.mp4", "label": "",
     "semantic": {"subjects": ["volcán", "lava"], "suggested_usage": "clímax"}, "saved_at": "2026-01-03T00:00:00+00:00"},
    {"id": "lib_c", "resource_type": "image", "filename": "cohete.png", "label": "Cohete",
     "description": "Cohete despegando", "saved_at": "2026-01-01T00:00:00+00:00"},
    {"id": 7, "resource_type": "image", "filename": "num.png", "label": 5, "description": 3},
    {"id": "lib_n", "resource_type": "image"},
    {"id": "lib_m", "resource_type": "image", "description": "algo sin título"},
    {"id": "lib_z", "resource_type": "image", "label": 0, "filename": "", "description": "cero"},
    # La misma clave «clips:1» que el clip 1 del proyecto: en recent_materials gana el último del catálogo.
    {"id": "1", "resource_type": "clip", "filename": "uno.mp4", "label": "Uno de la biblioteca"},
]}

# timeline_context, style_context, caption_lines y range_words de cada proyecto en cada rango.
PACK_RANGES = [[0, 5], [2.0, 6.5], [5, 9], [9, 14], [14, 20], [20, 30], [30, 40], [0, 1000], [-1, 0.5],
               [5.0, 5.0], [10, 4], [25, 29], [40, 45], [0.5, 5], [0, 2.0], [12, 16], [9, 12], [50, 55]]

# timeline_context de k-main con el cursor o un clip: [start, end, playhead, clip_id].
PACK_TIMELINE_CALLS = [
    [0, 5, None, None], [0, 5, 4.5, None], [0, 5, 0.0, None], [0, 5, 6.0, None], [0, 5, 7.5, None],
    [0, 5, None, "img1"], [0, 5, None, "nope"], [0, 5, None, ""], [0, 5, 100.0, None], [8.5, 9.0, None, None],
    [9, 14, 9.6, None], [9, 14, 12.2, "mo2"], [0, 5, -0.0005, None], [0, 5, 5.999, None], [25, 29, 26.5, None],
]

# recent_materials del catálogo de k-main con su material_usage, en cada inicio.
PACK_RECENT_STARTS = [0.0, 6.0, 7.0, 20.0, 20.0005, 26.0, 30.0, 38.0, 40.0, 1000.0]

# Las llamadas al pack: [project_id, tramo crudo, escaleta cruda o None (la guardada)]. El tramo y la
# escaleta se normalizan como en scene_direction_pack (con el reloj y los uuid desde cero en cada una).
PACK_CALLS = [
    ["k-main", {"id": "sd_a", "start": 0, "end": 5, "mode": "explain", "instruction": "Mostrar al perro",
                "materials": [{"kind": "clips", "id": "1"}],
                "components": [{"source": "material", "material": {"kind": "clips", "id": "1"}, "role": "full",
                                "source_range": {"in": 0.5, "out": 4}},
                               {"source": "motion", "role": "overlay", "note": "Flecha"},
                               {"source": "material", "material": {"kind": "clips", "id": "nope"}},
                               {"source": "keep"}]}, None],
    ["k-main", {"id": "sd_b", "start": 5, "end": 9, "mode": "reinforce", "reference_id": "sd_a", "strict": True,
                "complexity": 3}, None],
    ["k-main", {"id": "sd_c", "start": 9, "end": 14, "instruction": "El mapa cambia",
                "composition_intent": "Mapa entero",
                "materials": [{"kind": "images", "id": "im1"}, {"kind": "images", "id": "im3"},
                              {"kind": "clips", "id": "lib_a", "scope": "library"}]}, None],
    ["k-main", {"id": "sd_d", "start": 14, "end": 20, "mode": "material",
                "materials": [{"kind": "images", "id": 7, "scope": "library"}]}, None],
    ["k-main", {"start": 20, "end": 30, "no_visual": True}, None],
    ["k-main", {"start": 2.0, "end": 6.5}, None],
    ["k-main", {"start": 2.0, "end": 6.5, "instruction": "Un volcán con lava y la playa tropical"}, None],
    ["k-main", {"id": "sd_a", "start": 0, "end": 5}, []],
    ["k-main", {"id": "sd_mid", "start": 9, "end": 14, "mode": "material",
                "materials": [{"kind": "clips", "id": "lib_a", "scope": "library"}, {"kind": "clips", "id": "1"},
                              {"kind": "clips", "id": "1"}, {"kind": "clips", "id": "4"}]},
     [{"id": "sd_prev", "start": 0, "end": 9, "mode": "explain", "instruction": "x" * 200, "text": "t " * 80},
      {"id": "sd_mid", "start": 9, "end": 14}, {"id": "sd_next", "start": 14, "end": 20, "instruction": "Cerrar"},
      {"id": "sd_last", "start": 20, "end": 30}]],
    ["k-main", {"id": "sd_last", "start": 20, "end": 30, "reference_id": "sd_last"},
     [{"id": "sd_mid", "start": 9, "end": 14, "mode": "represent"}, {"id": "sd_last", "start": 20, "end": 30,
                                                                     "instruction": "Yo mismo"}]],
    ["k-main", {"id": "sd_e", "start": 20, "end": 30, "reference_id": "sd_nope", "complexity": "5",
                "composition_intent": "  dos   planos  ", "no_visual": True, "strict": 1}, None],
    ["k-main", {"start": 30, "end": 40}, None],
    ["k-main", {"start": 30, "end": 40, "text": "Texto del propio tramo", "instruction": "Mucho " * 120,
                "composition_intent": "Plano " * 130}, None],
    ["k-main", {"start": 0, "end": 1000}, None],
    # Con inicio y fin infinitos, la duración es inf − inf: un NaN con el signo puesto («nan» en Python).
    ["k-main", {"start": "inf", "end": "inf"}, None],
    ["k-main", {"start": 0, "end": 2.2}, None],
    ["k-main", {"start": 13.4, "end": 13.9}, None],
    ["k-main", {"start": 5.2, "end": 8.5, "materials": [{"kind": "images", "id": "im2"},
                                                         {"kind": "images", "id": "lib_c", "scope": "library"}]}, None],
    ["k-main", {"start": 9, "end": 14, "materials": [{"kind": "images", "id": "lib_n", "scope": "library"},
                                                      {"kind": "images", "id": "lib_m", "scope": "library"},
                                                      {"kind": "images", "id": "lib_z", "scope": "library"}]}, None],
    ["k-main", {"start": "nan", "end": "nan"}, None],
    ["k-main", {"start": 50, "end": "nan"}, None],
    ["k-main", {"start": 0, "end": "inf"}, None],
    ["k-main", {"start": 40, "end": "inf"}, None],
    ["k-main", {"start": -2, "end": -1.5}, None],
    ["k-main", {"start": 0, "end": 0.25}, None],
    ["k-tr", {"start": 2, "end": 8}, None],
    ["k-tr", {"start": 2, "end": 3.5, "strict": True}, None],
    ["k-aud", {"start": 1, "end": 7, "mode": "propose"}, None],
    ["k-sty", {"start": 0, "end": 4}, None],
    ["k-sty", {"start": 5, "end": 9}, None],
    ["k-sty", {"start": 25, "end": 29}, None],
    ["k-sty", {"start": 30, "end": 34}, None],
    ["k-sty", {"start": 40, "end": 44}, None],
    ["k-sty", {"start": 60, "end": 64}, None],
    ["k-sin", {"start": 0, "end": 5, "text": "Un texto propio", "mode": "represent"}, None],
    ["k-sin", {"start": 0, "end": 5}, None],
    ["k-sin", {"start": 0, "end": 2.5}, None],
    *[["k-raro", {"start": 0, "end": 5, "materials": [{"kind": "clips", "id": str(i)}]}, None] for i in range(1, 14)],
    ["k-raro", {"start": 0, "end": 5, "materials": [{"kind": "images", "id": f"w{i}"} for i in (1, 2, 3, 4, 5, 1, 2)]},
     None],
    ["k-raro", {"start": 0, "end": 5, "materials": [{"kind": "clips", "id": "12"}, {"kind": "images", "id": "w2"}],
                "strict": True}, None],
    ["k-raro", {"start": 0, "end": 5, "materials": [{"kind": "clips", "id": "12"}],
                "components": [{"source": "material", "material": {"kind": "images", "id": "w5"}, "role": "pip",
                                "note": "Nota"}, {"source": "text"}]}, None],
    ["k-raro", {"start": 0, "end": 5}, None],
    ["k-full", {"start": 0, "end": 12, "materials": [{"kind": "clips", "id": "1"}]}, None],
]

# Ritmos de skeleton_beats en cada pack (los de PACES y dos más).
PACK_PACES = [3.4, 2.5, 1.7, 0.5, 10.0]
