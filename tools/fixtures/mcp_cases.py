"""Casos del núcleo sin red del servidor MCP de video-yt (backend/app/mcp_server) para
tests/fixtures/mcp/*.json. export_fixtures.py --solo mcp los pasa por los módulos REALES
(dto.py, capabilities.py, help_content.py, registry.py, resources.py y audit.py) y guarda lo
que devuelven; los tests de C++ (tests/parity/test_mcp_*_parity.cpp) comprueban que gc_core
da lo mismo.

  PROJECTS        proyectos crudos (como en projects.json) + su historial crudo: el DTO
                  semántico (dto.py), project_summary (capabilities.py) y los resources project://
  ASPECTS         ancho y alto para dto.aspect_ratio
  ANALYZE         respuestas de analyze_youtube (schemas.AnalyzeResponse) para dto.analyze_dto
  JOBS            jobs (schemas.Job) para dto.job_dto
  ENVIRONMENTS    lo que capabilities.py pregunta a la máquina y a los ajustes (modelo de
                  transcripción, motores TTS, categorías de SFX, ffmpeg, GPU, proveedor de IA),
                  ya resuelto: la entrada de C++
  DOMAIN_QUERIES  dominios para capabilities.describe y help://<domain>
  MCP_ERRORS, VALUE_ERRORS, JOB_DTO_CHECKS, REGISTRATIONS   registry.py
  EXTRACT_META    audit.extract_meta: [tool, params, modelo de los ajustes]
  AUDIT_STEPS     audit.py paso a paso sobre un data/ temporal, con el reloj, los uuid y los jobs falsos:
    ["now", µs]                    el reloj (datetime.now) pasa a valer µs desde 1970 en UTC
    ["source", nombre | None]      el origen de las acciones (`with audit.source(...)`)
    ["begin", tool, access, project_id, meta]
    ["finish", token, {status?, ms?, error?, param_keys?, extra_meta?}]
    ["log", tool, access, {project_id?, param_keys?, status?, ms?, error?, meta?}]
    ["track_job", job_id, tool, access, {project_id?, meta?, source_name?}]
    ["jobs", {job_id: {status, progress?, message?} | None}]   lo que da jobs.get_job desde aquí
    ["active", project_id | None] · ["reset_runtime"] · ["read_all"] · ["read_recent", limit, project_id]
    ["write_raw", texto] · ["write_hex", bytes en hex]   el archivo de auditoría pasa a ser eso
    ["remove_file"]
"""
from __future__ import annotations

NAN = float("nan")
INF = float("inf")

_ASSET_KIND = {"video": "clips", "audio": "audios", "text": "text", "image": "images", "shape": "shape"}


def _clip(cid: str, track: str, kind: str, **kw) -> dict:
    out = {"id": cid, "track_id": track, "kind": kind, "asset_kind": _ASSET_KIND.get(kind, "clips"),
           "asset_id": "0", "filename": f"{cid}.mp4"}
    out.update(kw)
    return out


def _track(tid: str, kind: str, **kw) -> dict:
    return {"id": tid, "kind": kind, "name": kw.pop("name", tid), **kw}


_WORDS = [{"text": "hola", "start": 0.0, "end": 0.4, "prob": 0.9}, {"text": "mundo", "start": 0.4, "end": 0.9}]

_ALL_KINDS = {
    "id": "p-kinds", "name": "Todos los tipos", "created_at": "2026-02-01T10:00:00", "folder": "D:\\videos",
    "timeline": {
        "fps": 30, "width": 720, "height": 1280,
        "tracks": [
            _track("V1", "video", name="Principal"),
            _track("V2", "video", magnetic=True, hidden=True),
            _track("A1", "audio", linked_track_id="T1", muted=True),
            _track("T1", "text", linked_track_id="A1", locked=True),
        ],
        "clips": [
            _clip("v1", "V1", "video", name="Entrevista", start=0.0, in_point=2.0, out_point=12.5,
                  source_duration=60.0, speed=2.0, reframe={"zoom": 0.8, "face_track_mode": "smooth"},
                  frame="top", layout="overlay", look="cinematic", description="Plano general",
                  flip_h=True, blend_mode="screen", beats={"times": [0.5, 1.0, 1.5], "bpm": 120}),
            _clip("v2", "V2", "video", start=6.25, in_point=0.0, out_point=3.0, reframe={},
                  volume=0.123456, audio_fx={"eq": 0.5}, muted=True, disabled=True, flip_v=True,
                  dup_of="v1", beats={"times": [1, 2, 3, 4], "bpm": None, "every": 4}),
            _clip("a1", "A1", "audio", start=1.0, in_point=0.5, out_point=8.5, volume=1.5,
                  audio_fx={}, asset_scope="library", beats={"times": [], "bpm": 90}),
            _clip("t1", "T1", "text", start=0.0, in_point=0.0, out_point=2.0, text="hola mundo",
                  words=_WORDS, speed=4.0),
            _clip("t2", "T1", "text", start=2.0, in_point=0.0, out_point=3.0, text="Título",
                  text_role="free", origin={"transcript_id": "tr1"}),
            _clip("t3", "T1", "text", start=5.0, in_point=1.0, out_point=2.0, text=None,
                  origin={"transcript_id": "tr1", "segment_index": 0}),
            _clip("t4", "T1", "text", start=6.0, out_point=1.0, text="sin rol", origin={}),
            _clip("i1", "V2", "image", start=3.0, in_point=0.0, out_point=4.0, speed=0.5,
                  asset_kind="images", asset_id="img1", filename="logo.png"),
            _clip("s1", "V1", "shape", start=12.0, in_point=0.0, out_point=1.5, speed=3.0,
                  shape={"type": "rect"}, dup_of="v1"),
        ],
        "markers": [{"id": "m1", "t": 1.5, "label": "Golpe"}, {"id": "m2", "t": 3.0, "color": "#ff0000"}],
    },
}

_ODD_VALUES = {
    "id": "p-odd", "name": "Valores raros", "created_at": "2026-02-02T10:00:00",
    "timeline": {
        "fps": 24, "width": 854, "height": 480,
        "tracks": [
            _track("V1", "video"), _track("V1", "audio", name="mismo id"), _track("X", ""),
            _track("A1", "audio"),
        ],
        "clips": [
            _clip("z0", "V1", "video", speed=0.0, in_point=1.0, out_point=3.0),          # speed 0 → 1
            _clip("z1", "V1", "video", speed=-2.0, in_point=0.0, out_point=4.0),         # speed < 0 → el tramo
            _clip("z2", "A1", "audio", speed=NAN, in_point=0.0, out_point=2.0),          # NaN no es > 0
            _clip("z3", "A1", "audio", speed=INF, in_point=0.0, out_point=2.0),          # ∞ → 0
            _clip("z4", "V1", "video", in_point=5.0, out_point=2.0),                     # out < in → 0
            _clip("z5", "V1", "video", start=-0.0, in_point=0.0, out_point=INF),         # dura ∞
            _clip("z6", "nadie", "video", start=NAN, out_point=1.0),                     # pista inexistente
            _clip("z7", "X", "audio", start=100.0004, out_point=0.0005, volume=-0.00004),  # pista sin tipo
            _clip("z8", "V1", "", start=2.0, out_point=1.0),                             # tipo vacío
            _clip("z8", "V1", "weird", start=1.0, out_point=1.0, dup_of=""),            # id repetido
            _clip("z9", "V1", "video", dup_of="z8", beats={"times": "abc"}, blend_mode=""),
            _clip("z10", "V1", "video", dup_of="z8", beats={"times": {"a": 1, "b": 2}}),
            _clip("z11", "V1", "video", dup_of="z2", beats={}, start=1e-4),
            _clip("z12", "V1", "image", speed=0.0, out_point=2.5, beats={"times": [1], "every": None}),
            _clip("z13", "A1", "audio", start=-3.0, out_point=1.0, volume=2.0005, beats={"bpm": 100}),
            _clip("z14", "V1", "shape", start="7.5", in_point="1", out_point="2.25", speed="2"),
        ],
        "markers": [],
    },
}

_BEATS_NO_LEN = {
    "id": "p-beats", "name": "Beats sin len", "created_at": "2026-02-03T10:00:00",
    "timeline": {"tracks": [_track("A1", "audio")],
                 "clips": [_clip("b1", "A1", "audio", out_point=4.0, beats={"times": 5, "bpm": 100})]},
}

_BEATS_BOOL = {
    "id": "p-beats-bool", "name": "Beats con True", "created_at": "2026-02-03T11:00:00",
    "timeline": {"tracks": [_track("A1", "audio")],
                 "clips": [_clip("b1", "A1", "audio", out_point=4.0, beats={"times": True})]},
}

_BEATS_FLOAT = {
    "id": "p-beats-float", "name": "Beats con real", "created_at": "2026-02-03T12:00:00",
    "timeline": {"tracks": [_track("A1", "audio")],
                 "clips": [_clip("b1", "A1", "audio", out_point=4.0, beats={"times": 2.5})]},
}

_MATERIAL = {
    "id": "p-media", "name": "Material", "created_at": "2026-02-04T10:00:00", "group_id": "g1",
    "clips": [
        {"index": 0, "filename": "clip0.mp4", "url": "/c/0", "start": 10.0, "end": 25.5555,
         "label": "Intro", "description": "La intro", "origin": "youtube", "source": "external",
         "transcript": {"id": "tc0", "model": "base", "segments": [{"start": 0, "end": 1, "text": "hola"}]}},
        {"index": 1, "filename": "clip1.mp4", "url": "/c/1", "start": 0, "end": 3, "id": "", "label": ""},
        {"index": 7, "filename": "clip7.mp4", "url": "/c/7", "start": 5.0, "end": 2.0, "id": "custom-id",
         "origin": "compose", "source": "generated"},
    ],
    "audios": [
        {"id": "au1", "filename": "voz.wav", "url": "/a/1", "label": "Narrador", "duration": 12.5,
         "voice": "ef_dora", "engine": "kokoro", "origin": "tts", "source": "generated", "description": "Voz"},
        {"id": "au2", "filename": "musica.mp3", "url": "/a/2", "label": "", "duration": None},
    ],
    "images": [
        {"id": "img1", "filename": "logo.png", "url": "/i/1", "width": 512, "height": 256, "label": "Logo",
         "description": "El logo", "origin": "upload"},
        {"id": "img2", "filename": "foto.jpg", "url": "/i/2", "label": None, "source": "generated",
         "origin": "pexels"},
    ],
    "transcripts": [
        {"id": "tr1", "model": "small", "title": "Guion", "language": "es", "duration": 61.5,
         "segments": [{"start": 0, "end": 2, "text": "uno"}, {"start": 2, "end": 4, "text": "dos"}]},
        {"id": "tr2", "model": "base", "segments": []},
    ],
    "timeline": {"width": 1080, "height": 1080, "fps": 60,
                 "tracks": [_track("V1", "video")],
                 "clips": [_clip("m1", "V1", "video", asset_id="0", out_point=15.5555, start=0.0004)]},
}

# Cada proyecto: [nombre, proyecto crudo, historial crudo (timeline_store._load_history)].
PROJECTS = [
    ["sin timeline", {"id": "p-none", "name": "Sin timeline", "created_at": "2026-01-01T00:00:00"},
     {"undo": [], "redo": [], "checkpoints": {}}],
    ["timeline vacía ({} no se migra)", {"id": "p-empty", "name": "Vacía", "created_at": "2026-01-02T00:00:00",
                                         "timeline": {}},
     {"undo": [{}], "redo": [], "checkpoints": {"inicio": {}}}],
    ["timeline sin clips con marcadores",
     {"id": "p-marks", "name": "Marcadores", "created_at": "2026-01-03T00:00:00",
      "timeline": {"fps": 25, "width": 1080, "height": 1920, "tracks": [_track("T1", "text")],
                   "markers": [{"id": "m1", "t": 0.5}, {"t": 2, "label": "sin id", "extra": [1, 2]}]}},
     {"undo": [], "redo": [{}, {}], "checkpoints": {"v1": {}, "antes de cortar": {}, "ñandú": {}}}],
    ["todos los tipos de clip", _ALL_KINDS, {"undo": [{}], "redo": [{}], "checkpoints": {}}],
    ["valores raros", _ODD_VALUES, None],
    ["beats con un número (len falla)", _BEATS_NO_LEN, {}],
    ["beats con True (len falla)", _BEATS_BOOL, {}],
    ["beats con un real (len falla)", _BEATS_FLOAT, {}],
    ["material", _MATERIAL, {"undo": [{}, {}, {}], "redo": [], "checkpoints": {"a": {}}}],
    ["formato 4:5", {"id": "p-45", "name": "4:5", "created_at": "2026-01-05T00:00:00",
                     "timeline": {"width": 1080, "height": 1350}}, {}],
    ["formato sin nombre (1234×567)", {"id": "p-odd-size", "name": "Raro", "created_at": "2026-01-06T00:00:00",
                                       "timeline": {"width": 1234, "height": 567, "fps": 0}}, {}],
    ["formato con alto 0", {"id": "p-zero", "name": "Cero", "created_at": "2026-01-07T00:00:00",
                            "timeline": {"width": 720, "height": 0}}, {}],
]

# Los historiales resumidos que recibe dto.project_context directamente (None = sin historial).
CONTEXT_HISTORIES = [
    None,
    {"can_undo": True, "can_redo": False, "checkpoints": ["inicio", "v2"]},
    {"can_undo": False, "can_redo": True, "checkpoints": []},
]

ASPECTS = [
    [720, 1280], [1280, 720], [854, 480], [480, 854], [1080, 1080], [1000, 1001], [1080, 1350],
    [1440, 1080], [1080, 1440], [1920, 1081], [1234, 567], [100, 300], [7, 3], [1, 1],
    [3840, 2160], [2160, 3840], [0, 100], [100, 0], [-16, 9], [16, -9], [0, 0], [2, 1], [1, 2],
    [4096, 4095], [1000003, 999983], [2 ** 40, 3], [3, 2 ** 40],
]

_VIDEO = {"id": "dQw4w9WgXcQ", "title": "Vídeo «de prueba»", "duration": 212.5}

ANALYZE = [
    ["con heatmap", {
        "video": {**_VIDEO, "uploader": "Canal", "thumbnail": "https://i.ytimg.com/x.jpg",
                  "qualities": [{"quality": 2160, "fps": 60, "size": 123456789}, {"quality": 1080}, {"quality": 720}]},
        "has_heatmap": True,
        "segments": [
            {"index": 1, "start": 10.0, "end": 40.0, "score": 0.87654, "duration": 30.0, "label": "pico"},
            {"index": 2, "start": 90.5, "end": 120.25, "score": 0.0625, "duration": 29.75},
            {"index": 3, "start": 150, "end": 160, "score": 1, "duration": 10},
            {"index": 4, "start": 0, "end": 1, "score": 0.9995, "duration": 1},
            {"index": 5, "start": 0, "end": 1, "score": -0.0004, "duration": 1},
            {"index": 6, "start": 0, "end": 1, "score": 2.675, "duration": 1},
        ]}],
    ["sin heatmap ni calidades", {"video": _VIDEO, "has_heatmap": False, "segments": []}],
    ["valores en texto (Pydantic los convierte)", {
        "video": {"id": "x", "title": "t", "duration": "60", "uploader": None, "qualities": [{"quality": "480"}]},
        "has_heatmap": 1,
        "segments": [{"index": "7", "start": "1.5", "end": "2.5", "score": "0.12345", "duration": "1"}]}],
]

_CLIP_INFO = {"index": 0, "filename": "c.mp4", "url": "/c/0", "start": 0, "end": 5}

JOBS = [
    ["recién creado", {"id": "j1"}],
    ["en marcha", {"id": "j2", "status": "running", "progress": 0.5, "message": "Transcribiendo…"}],
    ["terminado con todo", {
        "id": "j3", "status": "done", "progress": 1.0, "message": "Listo",
        "clips": [_CLIP_INFO, {**_CLIP_INFO, "index": 1}], "export_url": "/exports/x.mp4",
        "transcript": {"id": "tr9", "model": "base"}, "audio": {"id": "au9", "filename": "a.wav", "url": "/a"},
        "reframe_prep": {"proxy_url": "/p.mp4", "duration": 5, "width": 1920, "height": 1080},
        "result": {"extra": True, "clips": 99, "summary": {"n": 2}}}],
    ["con error", {"id": "j4", "status": "error", "progress": 0.3, "message": "Falló", "error": "ffmpeg: boom"}],
    ["cancelación pedida en marcha", {"id": "j5", "status": "running", "cancel_requested": True}],
    ["cancelación pedida y ya terminado", {"id": "j6", "status": "done", "cancel_requested": True}],
    ["cancelación pedida con error", {"id": "j7", "status": "error", "cancel_requested": True, "error": "x"}],
    ["vacíos que no cuentan", {"id": "j8", "status": "pending", "clips": [], "export_url": "", "error": "",
                               "result": {}}],
    ["solo result", {"id": "j9", "result": {"a": 1}}],
]

ENVIRONMENTS = [
    ["todo disponible", {
        "transcribe_model": "small",
        "tts_engines": [
            {"id": "kokoro", "available": True, "voices": ["ef_dora", "em_alex"]},
            {"id": "piper", "available": False, "voices": []},
            {"id": "gemini", "available": True, "reason": None, "voices": ["Kore", "Puck"]},
            {"id": "azure", "available": False, "reason": "Falta la clave de Azure", "voices": ["es-ES-ElviraNeural"]},
        ],
        "sfx_categories": ["ambiente", "golpes", "whoosh"],
        "ffmpeg": True, "ffprobe": False,
        "data_dir": "C:\\datos\\video-yt\\data",
        "gpu": {"name": "RTX 4070", "cuda": True, "vram_mb": 12282},
        "ai_provider": {"provider": "foundry", "model": "gpt-5", "configured": True},
    }],
    ["nada disponible", {
        "transcribe_model": "base", "tts_engines": [], "sfx_categories": [],
        "ffmpeg": False, "ffprobe": True, "data_dir": "D:\\data",
        "gpu": None, "ai_provider": None,
    }],
    ["motor disponible con valor no booleano", {
        "transcribe_model": "large-v3",
        "tts_engines": [{"id": "kokoro", "available": 1, "voices": []}, {"id": "piper", "available": "",
                                                                         "voices": ["x"]}],
        "sfx_categories": ["única"], "ffmpeg": True, "ffprobe": True, "data_dir": "data",
        "gpu": {}, "ai_provider": [],
    }],
]

DOMAIN_QUERIES = [
    "discovery", "project", "text", "jobs", "media", "transcription", "timeline", "clips", "audio", "render",
    "workflow", "vision", "history", "motion", "scene",
    " Audio ", "CLIPS", "Clips\u00a0", "timeline\n", "nada", "", "   ", "other", "ÁUDIO", "Proyecto",
]

# [code, message, hint, retryable, param] de registry.MCPError.
MCP_ERRORS = [
    ["invalid_parameter", "Velocidad fuera de rango", None, True, "speed"],
    ["resource_not_found", "Clip no encontrado: c9", "Relee la timeline con get_timeline", False, None],
    ["job_running", "Hay un export en curso", "", False, ""],
    ["configuration_error", "", None, False, None],
    ["processing_error", "Falló «ffmpeg» con ñ y \"comillas\"\n\ty\u2028más", "barra \\ y \u0001", True, "p"],
    ["temporary_error", "red caída", "reintenta", True, None],
]

# ValueError «sueltos» (de timeline_ops…) para registry._classify_value_error y _to_tool_error.
VALUE_ERRORS = [
    "Clip no encontrado: c1", "Proyecto NO ENCONTRADO", "Track not found", "Pista inexistente",
    "El clip no existe", "No se encuentra el archivo", "Ningún proyecto coincide con: x", "NINGUNA pista",
    "No se encontró el audio", "Valor INVÁLIDO", "invalid value", "Formato no válido", "debe ser positivo",
    "Texto vacío", "Lista VACÍA", "Falta 'query'", "kind desconocido", "Desconocida", "No hay audio",
    "no encontrado e inválido", "inválido y no encontrado", "algo raro pasó", "", "ÍNDICE fuera de rango",
    "İnválido", "no encontrada", "NO HAY", "failed: timeout", "Nº de clips vacío",
]

JOB_DTO_CHECKS = [
    {"id": "j1", "status": "running", "progress": 0.5, "message": "x"},
    {"id": "", "status": None, "progress": None, "message": None},
    {"id": "j1", "status": "done", "progress": 1},
    {"id": 5, "status": "done", "progress": 1, "message": ""},
    {"status": "done", "progress": 1, "message": ""},
    {"id": "j1", "progress": 1, "message": "", "extra": 1},
    {"id": "j1", "status": "done", "message": ""},
    [1, 2], "j1", None, 5, {},
]

# [name, access] en orden: registry.tool(mcp, access=access, name=name)(fn).
REGISTRATIONS = [
    ["get_timeline", "read"], ["remove_clip", "destructive"], ["update_clip", "write"],
    ["herramienta_nueva", "write"], ["get_timeline", "read"], ["get_timeline", "nope"], ["x", "admin"],
    ["y", ""], ["z", "READ"], ["describe_capabilities", "read"], ["delete_media", "destructive"],
]

EXTRACT_META = [
    ["update_clip", {"project_id": "p1", "clip_id": "c1", "patch": {"opacity": 0.5}, "opacity": 0.123456,
                     "muted": False, "speed": 2, "name": "n" * 100, "text": "largo", "start": 0.0,
                     "at_time": None, "query": "", "count": True, "volume": 1e-5, "position": "top",
                     "segments": [1, 2], "kind": "video"}, "base"],
    ["move_clip", {"start": -0.00004, "track_id": "V1", "width": 2 ** 60, "height": -3, "fps": NAN,
                   "aspect": "ñ" * 90, "category": INF, "blend": -INF}, "base"],
    ["move_clip", None, "base"],
    ["transcribe", None, "medium"],
    ["transcribe", {"model": "small", "language": "es"}, "base"],
    ["transcribe", {"model": "giant"}, "base"],
    ["transcribe", {"model": ""}, "small"],
    ["transcribe", {"model": None, "clip_index": 3}, "tiny"],
    ["generate_subtitles", {"model": 5, "filename": "voz.wav", "asset_kind": "audios"}, "base"],
    ["generate_subtitles", {"model": 0}, "large-v3"],
    ["create_short_from_youtube", {"model": True}, "base"],
    ["make_short_from_library", {"model": 1.5, "asset_id": "0"}, "small"],
    ["make_short_from_library", {"model": "LARGE-V3"}, "medium"],
    ["make_short_from_library", {"model": "large-v3"}, "medium"],
    ["transcribe", {"model": "m" * 100}, "base"],
]

_T0 = 1767225600000000  # 2026-01-01T00:00:00Z en µs

AUDIT_STEPS = [
    ["read_all"],
    ["read_recent", 80, None],
    ["now", _T0 + 123456],
    ["begin", "get_timeline", "read", "p1", {"clip_id": "c1"}],
    ["active", None],
    ["active", "p1"],
    ["active", "p2"],
    ["now", _T0 + 1999],
    ["finish", "000001ab0000", {"status": "ok", "ms": 12.345, "param_keys": ["project_id", "clip_id"]}],
    ["active", None],
    ["source", "ai_chat"],
    ["now", 1830297599999999],  # 2027-12-31T23:59:59.999999Z
    ["begin", "update_clip", "write", None, {"speed": 2}],
    ["finish", "000002ab0000", {"status": "error", "ms": 0.05, "error": "ValueError: speed inválida",
                                "param_keys": [], "extra_meta": {"job_id": "j1", "speed": 3}}],
    ["source", None],
    ["finish", "nadie", {}],
    ["finish", "nadie", {"extra_meta": {"x": 1}, "ms": 0.25}],
    ["now", 1835395200000000],  # 2028-02-29T00:00:00Z
    ["log", "export_project", "write", {"project_id": "p1", "param_keys": ["z", "á", "a", "Z"], "ms": NAN,
                                        "error": "ñ" * 301, "meta": {"aspect": "9:16"}}],
    ["log", "t", "read", {"ms": 0.35, "error": "", "meta": {}, "status": "weird"}],
    ["log", "t2", "destructive", {"ms": INF, "param_keys": None}],
    ["source", "ui"],
    ["log", "t3", "read", {}],
    ["source", None],
    ["track_job", "j1", "transcribe", "write", {"project_id": "p1", "meta": {"model": "base"}}],
    ["track_job", "", "transcribe", "write", {}],
    ["track_job", "j2", "export_project", "write", {"project_id": "p2", "source_name": "ui"}],
    ["source", "ai_chat"],
    ["track_job", "j4", "sound_design", "write", {}],
    ["source", None],
    ["jobs", {"j1": {"status": "running", "progress": 0.0, "message": "Transcribiendo…"},
              "j2": {"status": "running", "progress": 0.5}, "j4": {"status": "pending"}}],
    ["begin", "wait_for_job", "read", "p1", {"job_id": "j1"}],
    ["begin", "get_job", "read", "p1", {"job_id": 5}],
    ["begin", "list_jobs", "read", "", {}],
    ["active", None],
    ["active", "p1"],
    ["active", "p2"],
    ["active", ""],
    ["jobs", {"j1": {"status": "done"}, "j2": None, "j4": {"status": "running", "progress": 1.0}}],
    ["active", "p1"],
    ["active", None],
    ["track_job", "j3", "transcribe", "write", {"project_id": "p1"}],
    ["track_job", "j5", "transcribe", "write", {"project_id": "p1"}],
    ["track_job", "j3", "generate_subtitles", "write", {"project_id": None, "meta": {"a": 1}}],
    ["jobs", {"j3": {"status": "pending", "progress": 0.25, "message": "En cola…"},
              "j5": {"status": "error"}, "j4": {"status": "cancelled"}}],
    ["active", "p1"],
    ["active", None],
    ["reset_runtime"],
    ["active", None],
    ["read_all"],
    ["read_recent", 2, None],
    ["read_recent", "1", "p1"],
    ["read_recent", 0, None],
    ["read_recent", 500, None],
    ["read_recent", "abc", None],
    ["read_recent", None, "p2"],
    ["read_recent", 7.9, None],
    ["read_recent", True, None],
    ["read_recent", -5, ""],
    ["read_recent", " 3 ", "p1"],
    ["write_raw", "\ufeff{\"tool\": \"x\"}\n"],
    ["read_all"],
    ["write_raw", "\n  {\"tool\": \"a\", \"project_id\": null}  \r\n\r\n{\"tool\": \"b\", \"project_id\": \"\"}\n"
                  "{\"tool\": \"c\", \"project_id\": \"p1\"}\n\t{\"tool\": \"d\"}\u3000\n{\"tool\": \"e\", "
                  "\"project_id\": \"p2\", \"x\": NaN}"],
    ["read_all"],
    ["read_recent", 80, "p1"],
    ["read_recent", 2, "p2"],
    ["write_raw", "{\"tool\": \"a\u2028b\"}\n"],
    ["read_all"],
    ["write_raw", "{\"tool\": \"a\u0085b\"}\n"],
    ["read_all"],
    ["write_raw", "{\"tool\": \"a\u0085\"}\r\n{\"b\": 1}"],
    ["read_all"],
    ["write_raw", "[1, 2]\n5\n\"texto\"\n"],
    ["read_all"],
    ["read_recent", 80, None],
    ["read_recent", 80, "p1"],
    ["write_raw", "{\"a\": 1}\r{\"b\": 2}\x0c{\"c\": 3}\x1c{\"d\": 4}\x1d{\"e\": 5}\x1e{\"f\": 6}\x0b{\"g\": 7}"],
    ["read_all"],
    ["write_raw", "{\"a\": 1}\u2029{\"b\": 2}"],
    ["read_all"],
    ["write_hex", "7b2261223a2022ff227d0a"],
    ["read_all"],
    ["write_raw", "{\"a\": 1} {\"b\": 2}\n"],
    ["read_all"],
    # Más de 200 entradas: read_recent no da más de 200 (las últimas).
    ["write_raw", "".join(f'{{"tool": "t{i}", "project_id": "p{i % 3}"}}\n' for i in range(250))],
    ["read_recent", 500, None],
    ["read_recent", 1000, "p1"],
    ["write_raw", ""],
    ["read_all"],
    ["remove_file"],
    ["read_all"],
    ["read_recent", 80, None],
    ["now", _T0],
    ["begin", "undo", "write", "p3", None],
    ["finish", "000006ab0000", {"param_keys": ["project_id"], "extra_meta": {"job_id": "j9", "job_status": "done"}}],
    ["read_all"],
]

# --- Tools de lectura (tools_capabilities.py, tools_context.py y tools_read.py) -----------------
#
# Se escriben en un data/ temporal: projects.json con PROJECTS + TOOL_PROJECTS (en ese orden) y
# data/history/<pid>.json con el historial de cada uno (None = sin archivo; un texto se escribe tal
# cual: JSON roto, BOM…). Después se llaman las tools REALES con TOOL_CALLS.

def _tp(pid: str, name: str, **kw) -> dict:
    return {"id": pid, "name": name, "created_at": "2026-03-01T00:00:00", **kw}


_SEARCH = _tp("p-busca", "Búsqueda", clips=[
    {"index": 3, "filename": "a.mp4", "url": "/c/3", "start": 0, "end": 9,
     "transcript": {"id": "tca", "model": "base", "segments": [
         {"start": 0, "end": 1.5, "text": "Hola MUNDO"}, {"start": 1.5, "end": 3, "text": "adiós"},
         {"start": 3, "end": 4, "text": ""}]}},
    {"index": 4, "filename": "b.mp4", "url": "/c/4", "start": 0, "end": 2},
], transcripts=[
    {"id": "t1", "model": "small", "segments": [
        {"start": 0, "end": 2, "text": "hola, ¿qué tal?"}, {"start": 2, "end": 4, "text": "ΟΔΟΣ y hola"},
        {"start": 4, "end": 6, "text": "İstanbul"}]},
    {"id": "t2", "model": "base", "segments": [{"start": 10, "end": 12.25, "text": "otra vez hola"}]},
])

# [proyecto crudo, historial crudo (dict/list/…, None = sin archivo, ["raw", texto] = el texto tal cual)]
TOOL_PROJECTS = [
    [_tp("abc12345", "Mi Proyecto"), {"undo": "ab", "redo": [], "checkpoints": [["x", 1], ["y", 2]]}],
    [_tp("abc99999", "  mi proyecto "), ["raw", "{roto"]],
    [_tp("zz-ÑANDÚ", "ÑANDÚ grande"), ["raw", "﻿{\"undo\": [{}]}"]],
    [_tp("İd-1", "Istanbul"), [1, 2]],
    [_tp("x-vacío", ""), {"undo": 5}],
    [_tp("x-sigma", "ΟΔΟΣ"), {"undo": [], "redo": {"a": 1}, "checkpoints": "ab"}],
    [_tp("dup", "Doble"), {"checkpoints": [[1, "a"], [1.0, "b"], [True, "c"], ["1", "d"], [None, "e"]]}],
    [_tp("dup", "Doble (segunda)"), None],
    [_tp("x-cp3", "Puntos de tres"), {"checkpoints": [["a", 1, 2]]}],
    [_tp("x-cpnum", "Puntos con número"), {"checkpoints": [7]}],
    [_tp("x-cplist", "Puntos con clave lista"), {"checkpoints": [[[1], 2]]}],
    [_tp("x-cpstr", "Puntos de texto"), {"checkpoints": ["ab", "cd", "ab"]}],
    [_tp("x-cpdict", "Puntos con dicts"), {"checkpoints": [{"k": 1, "v": 2}]}],
    [_tp("x-false", "Falso"), False],
    [_tp("x-cero", "Cero"), 0],
    [_tp("x-texto", "Texto"), "hola"],
    [_tp("x-true", "Verdadero"), True],
    [_tp("x-redo-true", "Redo verdadero"), {"redo": True}],
    [_tp("x-nan", "NaN"), {"checkpoints": [[float("nan"), 1], [float("nan"), 2]]}],
    [_SEARCH, {"undo": [{}], "redo": [], "checkpoints": {"z": {}, "a": {}}}],
    [_tp("p-a1", "Alfa uno"), None], [_tp("p-a2", "Alfa dos"), None], [_tp("p-a3", "Alfa tres"), None],
]

TOOL_CALLS = (
    [["describe_capabilities", [d]] for d in (None, "", "audio", " Clips ", "nada")]
    + [["list_projects", []]]
    + [["resolve_project", [q]] for q in (
        "p-none", "  p-none\t", "", "   ", None, "mi proyecto", "MI PROYECTO", "abc1", "abc", "ABC9",
        "ñandú", "zz-ñ", "ZZ-Ñ", "i̇d", "İd", "οδος", "ΟΔΟΣ", "nada que ver", "p-", "a", "doble",
        "Doble (segunda)", "dup", "du", "dobl", "alfa", "x-", "x-vacío", "busca")]
    + [["get_project_context", [pid]] for pid in (
        "p-none", "p-empty", "p-marks", "p-media", "abc12345", "abc99999", "zz-ÑANDÚ", "İd-1", "x-vacío",
        "x-sigma", "dup", "x-cp3", "x-cpnum", "x-cplist", "x-cpstr", "x-cpdict", "x-false", "x-cero",
        "x-texto", "x-true", "x-redo-true", "x-nan", "p-busca", "no-existe", "")]
    + [["get_timeline", [pid]] for pid in ("p-none", "p-empty", "p-marks", "p-45", "p-zero", "no-existe")]
    + [["inspect_clip", [pid, cid]] for pid, cid in (
        ("p-marks", "nada"), ("p-none", "c1"), ("no-existe", "c1"), ("p-45", ""))]
    + [["list_media", [pid]] for pid in ("p-media", "p-none", "p-busca", "no-existe")]
    + [["search_transcript", args] for args in (
        ["p-busca", "hola"], ["p-busca", "HOLA"], ["p-busca", "  hola  "], ["p-busca", "hola", 1],
        ["p-busca", "hola", 0], ["p-busca", "hola", -5], ["p-busca", "hola", "2"], ["p-busca", "hola", 2.9],
        ["p-busca", "hola", True], ["p-busca", "hola", "x"], ["p-busca", "hola", None],
        ["p-busca", "hola", float("nan")], ["p-busca", "hola", float("inf")], ["p-busca", "zzz", "x"],
        ["p-busca", "οδος"], ["p-busca", "i̇stanbul"], ["p-busca", "mundo"], ["p-busca", ""],
        ["p-busca", "   "], ["p-busca", None], ["p-media", "hola"], ["p-media", "o"], ["no-existe", "hola"],
        ["p-busca", "hola", " 3 "], ["p-busca", "hola", "+2"], ["p-busca", "hola", "1_0"],
        ["p-busca", "hola", "2.0"])]
)


# --- Las tools de edición (tools_edit.py), paso a paso sobre un data/ temporal --------------------
#
# Cada paso es [tool, argumentos, nombres]: `nombres` guarda los ids de `changed` por posición y
# «$nombre» los usa en los pasos siguientes. El proyecto es e-main si no se dice otro.

def _clip(index, start, end, **kw):
    return {"index": index, "filename": f"c{index}.mp4", "url": f"/m/c{index}.mp4", "start": start, "end": end, **kw}


# Un catálogo con lo que el de verdad no trae: generadores, sin tags, sin dur y sin categorías.
EDIT_CATALOG = {
    "effects": [
        {"id": "gen", "name": "Generador", "category": "basic", "description": "Hace cosas", "engine": "generator",
         "tags": ["cosas"], "params": []},
        {"id": "sin-tags", "name": "Ñandú", "category": "basic", "description": "Sin etiquetas",
         "params": [{"id": "a", "label": "A", "type": "range"}, {"id": "b", "type": "bool"}]},
        {"id": "con-tags", "name": "Otro", "category": "retro", "description": "", "tags": ["COSAS", "Más"], "params": []},
    ],
    "anims": [
        {"id": "a1", "name": "Uno", "slot": "in", "dur": 0.5, "tags": ["cosas"]},
        {"id": "a2", "name": "Dos", "slot": "in"},
        {"id": "a3", "name": "Ñu", "slot": "out", "dur": None},
    ],
}
EDIT_CATALOG_CALLS = [{}, {"query": "cosas"}, {"query": "ÑANDÚ"}, {"query": "ñu"}, {"category": "retro"},
                      {"category": "basic", "query": "etiquetas"}, {"query": "más cosas"}, {"query": "sin-tags ñandú"},
                      {"query": "generador"}, {"category": "basic"}]

EDIT_SETTINGS = {"export": {"fps": 24, "quality": "standard"}}
EDIT_SFX_FILES = ["golpes/pum.wav", "viento.mp3"]

EDIT_PROJECTS = [
    {"id": "e-main", "name": "Edición", "created_at": "2026-01-01T00:00:00+00:00",
     "clips": [
         _clip(0, 2.0, 12.5, label="Primer tramo"),
         _clip(1, 0.0, 100.0, id="ref-1", in_point=20.125, out_point=28.5, label="",
               face_track={"start": 20.125, "end": 28.5, "fps": 30.0}, reframe={"zoom": 0.5, "pan_mode": "direct"}),
         _clip(2, 3.0, 4.5, id="c-2", reframe={"zoom": 0.75}),
     ],
     "audios": [
         {"id": "a1", "filename": "voz.mp3", "url": "/m/voz.mp3", "duration": 4.25, "label": "Voz"},
         {"id": "a0", "filename": "sin.mp3", "url": "/m/sin.mp3"},
         {"id": "a-falta", "filename": "falta.mp3", "url": "/m/falta.mp3", "duration": 2.0, "label": ""},
     ],
     "images": [{"id": "i1", "filename": "foto.png", "url": "/m/foto.png"},
                {"id": "i2", "filename": "otra.png", "url": "/m/otra.png", "label": "Portada"}]},
    {"id": "e-fresh", "name": "Sin timeline", "created_at": "2026-01-01T00:00:00+00:00"},
    {"id": "e-solo", "name": "Para seguir sin timeline", "created_at": "2026-01-01T00:00:00+00:00"},
    {"id": "e-tl", "name": "Con timeline", "created_at": "2026-01-01T00:00:00+00:00",
     "timeline": {"fps": 25, "width": 1280, "height": 720,
                  "tracks": [{"id": "tv", "kind": "video", "name": "V1"}, {"id": "ta", "kind": "audio", "name": "A1"}],
                  "clips": [{"id": "free", "track_id": "tv", "kind": "video", "asset_kind": "clips", "asset_id": "0",
                             "filename": "c0.mp4", "layout": "overlay", "start": 0.0, "in_point": 0.0, "out_point": 4.0,
                             "source_duration": 4.0},
                            {"id": "free2", "track_id": "tv", "kind": "image", "asset_kind": "images", "asset_id": "i1",
                             "filename": "f.png", "layout": "overlay", "start": 5.0, "in_point": 0.0, "out_point": 3.0,
                             "source_duration": 5.0},
                            {"id": "tl-a", "track_id": "ta", "kind": "audio", "asset_kind": "audios", "asset_id": "a1",
                             "filename": "v.mp3", "start": 1.0, "in_point": 0.5, "out_point": 3.0,
                             "source_duration": 4.0},
                            {"id": "free3", "track_id": "tv", "kind": "image", "asset_kind": "images", "asset_id": "i1",
                             "filename": "f.png", "layout": "overlay", "start": 3.0, "in_point": 0.0, "out_point": 2.0,
                             "source_duration": 5.0}]}},
]


def _e(tool, names=None, pid="e-main", **kw):
    return [tool, {"project_id": pid, **kw}, names or []]


def _fx(tool, **kw):
    return [tool, kw, []]


_KF = {"enabled": True, "items": [
    {"id": "k1", "t": 0.0, "interpolation": "linear", "props": {"opacity": 0.0}},
    {"id": "k2", "t": 1.0, "interpolation": "linear", "props": {"opacity": 1.0}}]}
_SEGS = [{"start": 0.0, "end": 1.0, "text": "hola mundo",
          "words": [{"word": "hola", "start": 0.0, "end": 0.5}, {"word": "mundo", "start": 0.5, "end": 1.0}]},
         {"start": 1.0, "end": 2.0, "text": "adiós"}]

EDIT_STEPS = [
    # Sin nada que deshacer y sin timeline (e-fresh sigue sin ella hasta añadir una pista: fps de los ajustes).
    _e("undo", pid="e-fresh"), _e("redo", pid="e-fresh"), _e("checkpoint", ["cp0"], pid="e-fresh", name="vacío"),
    _e("restore_checkpoint", pid="e-fresh", name="nada"), _e("restore_checkpoint", pid="e-fresh", name="vacío"),
    _e("undo", pid="e-fresh"), _e("redo", pid="e-fresh"),
    _e("add_track", ["tf"], pid="e-fresh", kind="video"), _e("undo", pid="e-fresh"),
    # Proyecto inexistente: cada capa con su texto.
    _e("undo", pid="no-existe"), _e("redo", pid="no-existe"), _e("checkpoint", pid="no-existe", name="x"),
    _e("restore_checkpoint", pid="no-existe", name="x"), _e("move_clip", pid="no-existe", clip_id="x", start=1.0),
    _e("add_to_timeline", pid="no-existe", asset_kind="clips", asset_id="0"),
    _e("animate_clip", pid="no-existe", clip_id="x", motion="pop"),
    _e("animate_clip", pid="no-existe", clip_id="x", motion="pop", follow_audio_id="y"),
    _e("animate_clip", pid="e-solo", clip_id="x", motion="pop", follow_audio_id="y"),
    # Material → timeline.
    _e("add_to_timeline", ["tv1", "v1"], asset_kind="clips", asset_id="0"),
    _e("add_to_timeline", ["v2"], asset_kind="clips", asset_id="ref-1", start=20.0),
    _e("add_to_timeline", ["v3"], asset_kind="clips", asset_id="2", start=40.0, in_point=0.25, out_point=1.0),
    _e("add_to_timeline", ["v3b"], asset_kind="clips", asset_id="c-2", start=45.0, in_point=0.1234, out_point=1.2345),
    _e("add_to_timeline", ["ta1", "a1"], asset_kind="audios", asset_id="a1"),
    _e("add_to_timeline", ["a2"], asset_kind="audios", asset_id="a1", start=6.0, in_point=1.5, out_point=2.0),
    _e("add_to_timeline", ["a3"], asset_kind="audios", asset_id="a0", start=10.0, out_point=2.0),
    _e("add_to_timeline", ["af"], asset_kind="audios", asset_id="a-falta", start=20.0),
    _e("add_to_timeline", ["im1"], asset_kind="images", asset_id="i1", start=60.0),
    _e("add_to_timeline", ["im2"], asset_kind="images", asset_id="i2", start=70.0, track_id="$tv1"),
    _e("add_to_timeline", ["s1"], asset_kind="sfx", asset_id="golpes/pum.wav", start=30.0),
    _e("add_to_timeline", ["s2"], asset_kind="sfx", asset_id="viento.mp3", start=34.0, track_id="$ta1", out_point=1.0),
    _e("add_to_timeline", ["solape"], asset_kind="clips", asset_id="0", start=1.0),
    _e("add_to_timeline", asset_kind="clips", asset_id="99"),
    _e("add_to_timeline", asset_kind="audios", asset_id="no"),
    _e("add_to_timeline", asset_kind="images", asset_id="no"),
    _e("add_to_timeline", asset_kind="sfx", asset_id="nada.wav"),
    _e("add_to_timeline", asset_kind="sfx", asset_id="../fuera.wav"),
    _e("add_to_timeline", asset_kind="raro", asset_id="0"),
    _e("add_to_timeline", asset_kind="clips", asset_id="0", in_point=5.0, out_point=1.0),
    _e("add_to_timeline", asset_kind="audios", asset_id="a0"),
    _e("add_to_timeline", asset_kind="clips", asset_id="0", track_id="nada"),
    _e("add_to_timeline", asset_kind="audios", asset_id="a1", track_id="$tv1", start=90.0),
    # Pistas.
    _e("add_track", ["tt1"], kind="text"), _e("add_track", ["tv2"], kind="video", name="V extra"),
    _e("add_track", ["ta2"], kind="audio", name="SFX"), _e("add_track", kind="raro"),
    _e("rename_track", track_id="$ta2", name="Música"), _e("rename_track", track_id="nada", name="x"),
    _e("reorder_track", track_id="$tv2", target_track_id="$tv1"),
    _e("reorder_track", track_id="$tt1", target_track_id="$tv1", place="below"),
    _e("reorder_track", track_id="$tv2", target_track_id="$tv2"),
    _e("reorder_track", track_id="$tv2", target_track_id="nada"),
    _e("reorder_track", track_id="$tv2", target_track_id="$tv1", place="raro"),
    _e("link_tracks", track_id="$ta2", to_track_id="$tt1"), _e("link_tracks", track_id="$ta2", to_track_id="nada"),
    _e("unlink_track", track_id="$ta2"), _e("unlink_track", track_id="nada"),
    # Clips.
    _e("move_clip", clip_id="$v1", start=1.5), _e("move_clip", clip_id="$v3", track_id="$tv2"),
    _e("move_clip", clip_id="$v3", start=41.0, track_id="$tv1"), _e("move_clip", clip_id="nada", start=1.0),
    _e("move_clip", clip_id="$v1", start=-3.0), _e("move_clip", clip_id="$v1", track_id="$ta1"),
    _e("split_clip", ["v1a", "v1b"], clip_id="$v1", at_time=4.0), _e("split_clip", clip_id="$v1a", at_time=500.0),
    _e("split_clip", clip_id="nada", at_time=1.0),
    _e("duplicate_clip", ["d1"], clip_id="$v3"), _e("duplicate_clip", ["d2"], clip_id="$im1", start=80.0),
    _e("duplicate_clip", clip_id="nada"), _e("remove_clip", clip_id="$d1"), _e("remove_clip", clip_id="nada"),
    _e("update_clip", clip_id="$v2", patch={"opacity": 0.5, "note": "la escena"}),
    _e("update_clip", clip_id="$v2", patch={"speed": 2.0, "keep_pitch": True}),
    _e("update_clip", clip_id="$v2", patch={"flip_h": True, "blend_mode": "screen", "disabled": True}),
    _e("update_clip", clip_id="$v2", patch={"blend_mode": "raro"}), _e("update_clip", clip_id="nada", patch={}),
    _e("update_clip", clip_id="$v2", patch={}), _e("update_clip", clip_id="$v2", patch={"campo_raro": 1}),
    # Marcadores y atributos.
    _e("set_timeline_markers", markers=[{"t": 1.0, "label": "A"}, {"t": 3.5, "color": "#ff0000"}]),
    _e("set_timeline_markers", markers=[]), _e("set_timeline_markers", markers=[{"label": "sin t"}]),
    _e("paste_clip_attributes", source_clip_id="$v2", target_clip_ids=["$v3", "$im1"]),
    _e("paste_clip_attributes", source_clip_id="$v2", target_clip_ids=["$v3"], groups=["blend", "flip"]),
    _e("paste_clip_attributes", source_clip_id="$v2", target_clip_ids=None),
    _e("paste_clip_attributes", source_clip_id="nada", target_clip_ids=["$v3"]),
    _e("paste_clip_attributes", source_clip_id="$v2", target_clip_ids=["nada"]),
    # Capas y barras.
    _e("add_adjustment_layer", ["adj"]),
    _e("add_adjustment_layer", ["adj2"], start=50.0, duration=2.0, intensity=0.5,
       filters=[{"id": "cinematic", "amount": 0.5}], effects={"brightness": 0.2}),
    _e("add_adjustment_layer", start=1.0, duration=-1.0),
    _e("add_cinema_bars", ["bars"]), _e("add_cinema_bars", ["bars2"], ratio="16:9", start=1.0, duration=3.0, animate=True),
    _e("add_cinema_bars", ratio="raro"),
    # Encuadre y recorte.
    _e("reframe_clip", clip_id="$v3", zoom=1.5), _e("reframe_clip", clip_id="$v3", zoom=0.5),
    _e("reframe_clip", clip_id="$v3", mode="manual", zoom=0.75,
                                                     pan_from={"cx": 0.25, "cy": 0.5}, pan_to={"cx": 0.75, "cy": 0.5}),
    _e("reframe_clip", clip_id="$v3", mode="raro"), _e("reframe_clip", clip_id="nada"),
    _e("crop_clip", clip_ids=["$v3"], cx=0.5, cy=0.5, w=0.5, h=0.5), _e("crop_clip", clip_ids=["$v3"], reset=True),
    _e("crop_clip", track_id="$tv1", cx=0.25, cy=0.25, w=0.5, h=0.5), _e("crop_clip", clip_ids=["nada"], w=0.5, h=0.5),
    _e("crop_clip"),
    _e("crop_clip", pid="e-tl", clip_ids=["free"], cx=0.5, cy=0.5, w=0.5, h=0.5),
    _e("crop_clip", pid="e-tl", track_id="tv", cx=0.25, cy=0.25, w=0.5, h=0.25),
    _e("crop_clip", pid="e-tl", clip_ids=["free"]), _e("crop_clip", pid="e-tl", clip_ids=["tl-a"], w=0.5, h=0.5),
    _e("crop_clip", pid="e-tl", clip_ids=["free"], reset=True),
    _e("add_to_timeline", ["tl-v"], pid="e-tl", asset_kind="clips", asset_id="0"),
    _e("animate_clip", pid="e-tl", clip_id="free", motion="pulse", follow_audio_id="tl-a"),
    _e("animate_clip", pid="e-tl", clip_id="free2", motion="pulse", follow_audio_id="tl-a"),
    _e("animate_clip", pid="e-tl", clip_id="free3", motion="pulse", follow_audio_id="tl-a"),
    # Subtítulos y formato.
    _e("add_subtitles", ["sub1", "sub2"], source_clip_id="$v3", segments=_SEGS),
    _e("add_subtitles", source_clip_id="$v3", segments=_SEGS, track_id="$tt1", transcript_id="tr1",
       style={"fontSize": 40}),
    _e("add_subtitles", source_clip_id="nada", segments=_SEGS), _e("add_subtitles", source_clip_id="$v3", segments=[]),
    _e("set_project_format", aspect="16:9"), _e("set_project_format", resolution=1080), _e("set_project_format", fps=60),
    _e("set_project_format", width=640, height=480), _e("set_project_format", aspect="raro"), _e("set_project_format"),
    # Efectos y animaciones.
    _e("set_clip_effects", clip_id="$v3", effects={"blur": 0.5, "sepia": 0.25}),
    _e("set_clip_effects", clip_id="$v3", effects={"contrast": 1.2}),
    _e("set_clip_effects", clip_id="$v3", effects={"hue": 10}, replace=True),
    _e("set_clip_effects", clip_id="$v3", filters=[{"id": "cinematic", "amount": 0.5}]),
    _e("set_clip_effects", clip_id="$v3", filters=[]), _e("set_clip_effects", clip_id="$a1", effects={"blur": 1}),
    _e("set_clip_effects", clip_id="nada", effects={"blur": 1}),
    _e("apply_effect", ["fx1"], clip_ids=["$v3"], effect="glitch_rgb", params={"amount": 0.25}, start=0.0, end=0.5),
    _e("apply_effect", clip_ids=["$v3", "$im1"], effect="vhs"),
    _e("apply_effect", clip_ids=["$v3"], effect="raro"), _e("apply_effect", clip_ids=["nada"], effect="vhs"),
    _e("apply_effect", clip_ids=None, effect="vhs"), _e("apply_effect", clip_ids=["$v3"]),
    _e("apply_effect", clip_ids=["$v3"], stack=[]),
    _e("set_clip_animation", clip_ids=["$v3"], slot="in", animation="fade_in", duration=0.5),
    _e("set_clip_animation", clip_ids=["$v3"], slot="in"), _e("set_clip_animation", clip_ids=["$v3"], slot="raro"),
    _e("set_clip_animation", clip_ids=["$v3"], slot="in", animation="raro"),
    _e("set_clip_animation", clip_ids=None, slot="in", animation="fade_in"),
    _e("set_clip_masks", clip_id="$v3", masks=[{"type": "circle"}]), _e("set_clip_masks", clip_id="$v3"),
    _e("set_clip_masks", clip_id="$v3", masks=[{"type": "raro"}]), _e("set_clip_masks", clip_id="nada"),
    # Audio.
    _e("set_clip_audio_fx", clip_id="$a1", audio_fx={"reverb": 0.5}),
    _e("set_clip_audio_fx", clip_id="$a1", audio_fx={"echo": 0.25}, ramp={"start": 0.0, "end": 1.0}),
    _e("set_clip_audio_fx", clip_id="$a1", audio_fx={"eq": 0.5}, replace=True),
    _e("set_clip_audio_fx", clip_id="$a1", audio_fx={"raro": 0.5}), _e("set_clip_audio_fx", clip_id="$im1", audio_fx={"eq": 1}),
    _e("set_clip_audio_fx", clip_id="nada", audio_fx={}),
    _e("set_clip_volume", clip_id="$a1", volume=0.5), _e("set_clip_volume", clip_id="$a1", muted=True),
    _e("set_clip_volume", clip_id="$a1", fade="in"), _e("set_clip_volume", clip_id="$a1", fade="out", fade_dur=1.0),
    _e("set_clip_volume", clip_id="$a1", fade="raro"), _e("set_clip_volume", clip_id="$im1", volume=1.0),
    _e("set_clip_volume", clip_id="nada", volume=1.0), _e("set_clip_volume", clip_id="$a1", volume=5.0),
    _e("set_track_audio", track_id="$ta1", volume=0.75), _e("set_track_audio", track_id="$ta1", muted=False),
    _e("set_track_audio", track_id="$ta1", audio_fx={"reverb": 0.25}),
    _e("set_track_audio", track_id="$ta1", fade="out", fade_dur=0.25),
    _e("set_track_audio", track_id="$tv1", volume=0.5), _e("set_track_audio", track_id="nada", volume=0.5),
    # Keyframes y animación.
    _e("set_clip_keyframes", clip_id="$v3", keyframes=_KF), _e("set_clip_keyframes", clip_id="$v3", keyframes=None),
    _e("set_clip_keyframes", clip_id="nada", keyframes=None),
    _e("animate_clip", clip_id="$v3", motion="zoom_in"), _e("animate_clip", clip_id="$v3", motion="pop", duration=0.5),
    _e("animate_clip", clip_id="$v3", motion="spin", intensity=0.5, turns=2.0),
    _e("animate_clip", clip_id="$v3", motion="raro"), _e("animate_clip", clip_id="nada", motion="pop"),
    _e("animate_clip", clip_id="$v2", motion="pulse", follow_audio_id="$af"),
    _e("animate_clip", clip_id="$v2", motion="pulse", follow_audio_id="$a1"),
    _e("animate_clip", clip_id="$v2", motion="pulse", follow_audio_id="$s1"),
    _e("animate_clip", clip_id="$v3", motion="pulse", follow_audio_id="$v2"),
    _e("animate_clip", clip_id="$v2", motion="pulse", follow_audio_id="nada"),
    _e("animate_clip", clip_id="nada", motion="pulse", follow_audio_id="$a1"),
    _e("animate_clip", clip_id="$v2", motion="pulse", follow_audio_id="$im1"),
    _e("animate_clip", clip_id="$v2", motion="pulse", follow_audio_id=""),
    _e("animate_clip", clip_id="$v2", motion="pulse", follow_audio_id="$af2"),
    # Figuras.
    _e("add_shape", ["sh1"], shape={"type": "rect", "fill": "#ff0000"}),
    _e("add_shape", ["sh2"], shape={"type": "star"}, track_id="$tv2", start=2.0, duration=1.5),
    _e("add_shape", ["sh3"], points=[[0.0, 0.0], [1.0, 1.0]], start=5.0), _e("add_shape"),
    _e("add_shape", shape={"type": "raro"}), _e("add_shape", shape={"type": "rect"}, track_id="nada"),
    # Recetas.
    _e("apply_recipe", recipe="cinema_grade"), _e("apply_recipe", recipe="raro"),
    _e("apply_recipe", recipe="text_reflection", clip_ids=["$sub1"]),
    _e("apply_recipe", recipe="film_strips", clip_ids=["$v3", "$im1"], params={"frames": 2}),
    _e("apply_recipe", recipe="film_strips", clip_ids=None), _e("apply_recipe", recipe="subject_pop", clip_ids=["$v3"]),
    _e("apply_recipe", recipe="pass_through_text", clip_ids=["$sub1"], params=None),
    # Historial.
    _e("checkpoint", ["cp1"], name="antes"), _e("checkpoint", name="otro ñ"), _e("checkpoint", name="antes"),
    _e("remove_track", track_id="$tv2"), _e("remove_track", track_id="nada"),
    _e("undo"), _e("redo"), _e("undo"), _e("undo"), _e("redo"), _e("redo"), _e("redo"),
    _e("restore_checkpoint", name="antes"), _e("restore_checkpoint", name="nada"), _e("undo"), _e("redo"),
    # El catálogo.
    _fx("list_effects"), _fx("list_effects", category="glitch"), _fx("list_effects", category="raro"),
    _fx("list_effects", query="GLITCH"), _fx("list_effects", query="vhs retro"), _fx("list_effects", query="  "),
    _fx("list_effects", category="", query=None), _fx("list_effects", category="retro", query="film"),
    _fx("list_effects", query="zoom"), _fx("list_effects", query="nada-de-nada"), _fx("list_effects", query="FADE in"),
]


# --- El servidor por HTTP: lo que responde el servidor MCP real a cada petición -------------------
#
# WIRE_STEPS: [nombre, {method?, session? (cur|none|gone|otro texto), headers?, rpc? | raw?}]. `rpc` es
# el mensaje JSON-RPC; `raw`, el cuerpo tal cual. ARG_MATRIX: [tool, parámetro, valores] para la
# validación de los argumentos (la de Pydantic, antes de entrar en la tool).

WIRE_PROJECTS = [
    {"id": "w1", "name": "Uno ñ", "created_at": "2026-01-01T00:00:00+00:00",
     "timeline": {"tracks": [{"id": "tv", "kind": "video", "name": "V1"}],
                  "clips": [{"id": "c1", "track_id": "tv", "kind": "video", "asset_kind": "clips", "asset_id": "0",
                             "filename": "c0.mp4", "start": 0.0, "in_point": 0.0, "out_point": 4.0,
                             "source_duration": 4.0}]}},
    {"id": "w-crash", "name": "Historial raro", "created_at": "2026-01-01T00:00:00+00:00"},
]
WIRE_HISTORIES = {"w-crash": "[[1, 2, 3]]"}
WIRE_INIT = {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}}


def _rpc(method, params=None, id_=1):
    msg = {"jsonrpc": "2.0", "id": id_, "method": method}
    if params is not None:
        msg["params"] = params
    return msg


def _call(_tool, /, **arguments):
    return _rpc("tools/call", {"name": _tool, "arguments": arguments})


def _n(name, method, session="cur", **kw):
    return [name, {"rpc": _rpc(method, kw.pop("params", None), kw.pop("id_", 1)), "session": session, **kw}]


WIRE_STEPS = [
    ["ping sin sesión", {"rpc": _rpc("ping"), "session": "none"}],
    ["initialize", {"rpc": _rpc("initialize", WIRE_INIT), "session": "none"}],
    ["initialize versión vieja", {"rpc": _rpc("initialize", {**WIRE_INIT, "protocolVersion": "2025-03-26"}), "session": "none", "keep_session": True}],
    ["initialize versión rara", {"rpc": _rpc("initialize", {**WIRE_INIT, "protocolVersion": "1999-01-01"}), "session": "none", "keep_session": True}],
    ["initialize sin params", {"rpc": _rpc("initialize"), "session": "none", "keep_session": True}],
    ["initialize otra vez", {"rpc": _rpc("initialize", WIRE_INIT)}],
    ["sin initialized", {"rpc": _rpc("ping")}],
    ["initialized", {"rpc": {"jsonrpc": "2.0", "method": "notifications/initialized"}}],
    ["otra notificación", {"rpc": {"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 1}}}],
    ["notificación rara", {"rpc": {"jsonrpc": "2.0", "method": "notifications/nada"}}],
    ["ping", {"rpc": _rpc("ping", id_=3)}],
    ["ping id de texto", {"rpc": _rpc("ping", id_="abc")}],
    ["ping con params", {"rpc": _rpc("ping", {})}],
    # Cabeceras.
    ["sin Content-Type", {"rpc": _rpc("ping"), "headers": {"Content-Type": None}}],
    ["otro Content-Type", {"rpc": _rpc("ping"), "headers": {"Content-Type": "text/plain"}}],
    ["Content-Type con charset", {"rpc": _rpc("ping"), "headers": {"Content-Type": "application/json; charset=utf-8"}}],
    ["Content-Type en mayúsculas", {"rpc": _rpc("ping"), "headers": {"Content-Type": "APPLICATION/JSON"}}],
    ["Host raro", {"rpc": _rpc("ping"), "headers": {"Host": "evil.com:8000"}}],
    ["Host sin puerto", {"rpc": _rpc("ping"), "headers": {"Host": "127.0.0.1"}}],
    ["Host localhost", {"rpc": _rpc("ping"), "headers": {"Host": "localhost:8000"}}],
    ["Host ipv6", {"rpc": _rpc("ping"), "headers": {"Host": "[::1]:8000"}}],
    ["Origin raro", {"rpc": _rpc("ping"), "headers": {"Origin": "http://evil.com"}}],
    ["Origin raro con puerto", {"rpc": _rpc("ping"), "headers": {"Origin": "http://evil.com:8000"}}],
    ["Origin local", {"rpc": _rpc("ping"), "headers": {"Origin": "http://localhost:5173"}}],
    ["Origin local sin puerto", {"rpc": _rpc("ping"), "headers": {"Origin": "http://localhost"}}],
    ["Origin https local", {"rpc": _rpc("ping"), "headers": {"Origin": "https://127.0.0.1:5173"}}],
    ["Accept json", {"rpc": _rpc("ping"), "headers": {"Accept": "application/json"}}],
    ["Accept sse", {"rpc": _rpc("ping"), "headers": {"Accept": "text/event-stream"}}],
    ["Accept todo", {"rpc": _rpc("ping"), "headers": {"Accept": "*/*"}}],
    ["Accept application/*", {"rpc": _rpc("ping"), "headers": {"Accept": "application/*, text/*"}}],
    ["Accept con q", {"rpc": _rpc("ping"), "headers": {"Accept": "application/json;q=0.9, text/event-stream;q=0.8"}}],
    ["Accept otro", {"rpc": _rpc("ping"), "headers": {"Accept": "text/plain"}}],
    ["versión de protocolo rara", {"rpc": _rpc("ping"), "headers": {"MCP-Protocol-Version": "1999-01-01"}}],
    ["versión de protocolo vieja", {"rpc": _rpc("ping"), "headers": {"MCP-Protocol-Version": "2025-03-26"}}],
    ["sesión desconocida", {"rpc": _rpc("ping"), "session": "no-existe"}],
    # Cuerpos.
    ["JSON roto", {"raw": "{nope"}], ["cuerpo vacío", {"raw": ""}], ["no es un objeto", {"raw": "3"}],
    ["lista", {"raw": "[{\"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"ping\"}]"}], ["lista vacía", {"raw": "[]"}],
    ["sin jsonrpc", {"raw": "{\"id\": 1, \"method\": \"ping\"}"}], ["jsonrpc viejo", {"raw": "{\"jsonrpc\": \"1.0\", \"id\": 1, \"method\": \"ping\"}"}],
    ["sin method", {"raw": "{\"jsonrpc\": \"2.0\", \"id\": 1}"}], ["una respuesta", {"raw": "{\"jsonrpc\": \"2.0\", \"id\": 1, \"result\": {}}"}],
    ["id nulo", {"raw": "{\"jsonrpc\": \"2.0\", \"id\": null, \"method\": \"ping\"}"}],
    ["método desconocido", {"rpc": _rpc("nada/nada")}],
    # Tools.
    ["tools/list", {"rpc": _rpc("tools/list"), "tools_list": True}],
    ["tools/list con cursor", {"rpc": _rpc("tools/list", {"cursor": "x"}), "tools_list": True}],
    ["lectura", {"rpc": _call("list_projects")}], ["lectura con args", {"rpc": _call("get_timeline", project_id="w1")}],
    ["ValueError", {"rpc": _call("get_timeline", project_id="nada")}],
    ["MCPError con param", {"rpc": _call("add_to_timeline", project_id="w1", asset_kind="raro", asset_id="0")}],
    ["crash", {"rpc": _call("get_project_context", project_id="w-crash")}],
    ["faltan argumentos", {"rpc": _call("move_clip")}], ["tipos mal", {"rpc": _call("move_clip", project_id=3, clip_id="c1", start="abc")}],
    ["argumento de más", {"rpc": _call("get_timeline", project_id="w1", otro=1)}],
    ["tool desconocida", {"rpc": _call("nada")}],
    ["sin nombre", {"rpc": _rpc("tools/call", {"arguments": {}})}], ["sin params", {"rpc": _rpc("tools/call")}],
    ["sin arguments", {"rpc": _rpc("tools/call", {"name": "list_projects"})}],
    ["sin arguments con obligatorios", {"rpc": _rpc("tools/call", {"name": "get_timeline"})}],
    ["arguments no es objeto", {"rpc": _rpc("tools/call", {"name": "list_projects", "arguments": 3})}],
    ["con _meta", {"rpc": _rpc("tools/call", {"name": "list_projects", "arguments": {}, "_meta": {"progressToken": 1}})}],
    # Escrituras: llegan a la timeline, con los números convertidos.
    ["escribir", {"rpc": _call("add_track", project_id="w1", kind="audio", name="Ñ")}],
    ["escribir con texto numérico", {"rpc": _call("move_clip", project_id="w1", clip_id="c1", start="1.5")}],
    ["escribir con entero", {"rpc": _call("move_clip", project_id="w1", clip_id="c1", start=2)}],
    ["JSON en un texto", {"rpc": _call("update_clip", project_id="w1", clip_id="c1", patch="{\"opacity\": 0.5}")}],
    ["JSON de una lista", {"rpc": _call("set_timeline_markers", project_id="w1", markers="[{\"t\": 1}]")}],
    ["deshacer", {"rpc": _call("undo", project_id="w1")}], ["rehacer", {"rpc": _call("redo", project_id="w1")}],
    # Resources y prompts.
    ["resources/list", {"rpc": _rpc("resources/list")}], ["plantillas", {"rpc": _rpc("resources/templates/list")}],
    ["leer capacidades", {"rpc": _rpc("resources/read", {"uri": "capabilities://index"})}],
    ["leer runtime", {"rpc": _rpc("resources/read", {"uri": "config://runtime"})}],
    ["leer guía", {"rpc": _rpc("resources/read", {"uri": "help://clips"})}],
    ["leer guía rara", {"rpc": _rpc("resources/read", {"uri": "help://nada"})}],
    ["leer proyecto", {"rpc": _rpc("resources/read", {"uri": "project://w1"})}],
    ["leer timeline", {"rpc": _rpc("resources/read", {"uri": "project://w1/timeline"})}],
    ["leer clip", {"rpc": _rpc("resources/read", {"uri": "project://w1/clip/c1"})}],
    ["leer clip raro", {"rpc": _rpc("resources/read", {"uri": "project://w1/clip/nada"})}],
    ["leer material", {"rpc": _rpc("resources/read", {"uri": "project://w1/media"})}],
    ["leer proyecto raro", {"rpc": _rpc("resources/read", {"uri": "project://nada"})}],
    ["leer historial raro", {"rpc": _rpc("resources/read", {"uri": "project://w-crash"})}],
    ["leer desconocido", {"rpc": _rpc("resources/read", {"uri": "nada://x"})}],
    ["leer URI mala", {"rpc": _rpc("resources/read", {"uri": "sin esquema"})}],
    ["leer sin uri", {"rpc": _rpc("resources/read", {})}], ["leer sin params", {"rpc": _rpc("resources/read")}],
    ["prompts/list", {"rpc": _rpc("prompts/list")}], ["prompts/get", {"rpc": _rpc("prompts/get", {"name": "x"})}],
    ["subscribe", {"rpc": _rpc("resources/subscribe", {"uri": "x://y"})}], ["logging", {"rpc": _rpc("logging/setLevel", {"level": "info"})}],
    ["completion", {"rpc": _rpc("completion/complete", {"ref": {"type": "ref/prompt", "name": "x"}, "argument": {"name": "a", "value": "b"}})}],
    # Las tools de material: un MCPError con su pista y con su param, y un ValueError suelto.
    ["material: versiones sin el vídeo", {"rpc": _call("replace_media_version", project_id="w1", clip_index="zzz", dry_run=True)}],
    ["material: kind raro", {"rpc": _call("delete_media", project_id="w1", kind="x", ident="0")}],
    ["material: sin el clip", {"rpc": _call("set_clip_ai_description", project_id="w1", clip_index="0", description=" x ")}],
    ["material: proyecto raro", {"rpc": _call("replace_media_version", project_id="nada", clip_index="0")}],
    # Otros métodos HTTP.
    ["GET Accept raro", {"method": "GET", "headers": {"Accept": "text/plain"}}],
    ["PUT", {"method": "PUT"}], ["DELETE sin sesión", {"method": "DELETE", "session": "none"}],
    ["DELETE sesión rara", {"method": "DELETE", "session": "no-existe"}],
    ["DELETE", {"method": "DELETE", "forget": True}], ["ping tras DELETE", {"rpc": _rpc("ping"), "session": "gone"}],
    ["DELETE otra vez", {"method": "DELETE", "session": "gone"}],
]

_LARGO = "una-cadena-bastante-larga-con-ñ-para-ver-como-corta-el-valor-que-muestra-el-error"
_NUMS = [1, 1.5, 0, -2, "-3", _LARGO, list(range(40)), {"clave": _LARGO}, "1.5", " 2 ", "abc", "", "1_000", "1e3", "inf", "nan", True, False, None, [], {}, [1], {"a": 1}, "[1]", "{\"a\": 1}", "3", "null", "true", "\"x\""]
ARG_MATRIX = [
    ["move_clip", "clip_id", ["x", "", 3, 1.5, True, None, [], {}, "[1]", "{\"a\": 1}", "3", "null"]],
    ["move_clip", "start", _NUMS],
    ["add_cinema_bars", "start", _NUMS],
    ["move_clip", "track_id", ["x", "", 3, 1.5, True, None, [], {}, "[1]", "{\"a\": 1}", "3", "null", "true"]],
    ["set_project_format", "fps", _NUMS + [2.0, 2.5, -1.0, "2.0", "2.5", "+4", "1e2"]],
    ["add_cinema_bars", "animate", [True, False, 0, 1, 2, 0.0, 1.0, 0.5, "true", "True", "TRUE", "false", "t", "f", "y", "n", "yes", "no", "on", "off", "1", "0", "2", "abc", "", None, [], {}, "[1]", "{\"a\": 1}", "null"]],
    ["set_clip_volume", "muted", [True, False, 0, 1, 2, "true", "off", "abc", None, [], "[1]", "null"]],
    ["update_clip", "patch", [{}, {"a": 1}, [], [1], 3, 1.5, True, None, "x", "", "{\"a\": 1}", "[1]", "3", "null", "{nope"]],
    ["set_clip_effects", "effects", [{}, {"a": 1}, [], 3, True, None, "x", "{\"a\": 1}", "[1]", "null"]],
    ["set_timeline_markers", "markers", [[], [1], ["a", {"b": 1}], {}, 3, True, None, "x", "[1]", "{\"a\": 1}", "null", "3"]],
    ["crop_clip", "clip_ids", [[], ["a"], ["a", "b"], [1], ["a", 2, None], [True], {}, 3, True, None, "x", "[\"a\"]", "[1]", "null"]],
    ["apply_effect", "clip_ids", [[], ["a"], [1], {}, 3, None, "x", "[\"a\"]", "[1]"]],
    ["paste_clip_attributes", "groups", [[], ["a"], [1], {}, 3, None, "x", "[\"a\"]", "[1]", "null"]],
]


# --- Tools de material sin IA: replace_media_version y delete_media (tools_media), search_sfx
# (tools_audio) y set_clip_ai_description (tools_vision), sobre un data/ temporal -----------------
#   MEDIA_PROJECTS   proyectos crudos: «u1» es el de media_upgrade (projects_cases: versiones de YouTube
#                    con medidas, segmentos y una timeline que usa las ligeras); «m2», un audio y una imagen
#   MEDIA_FILES      archivos que existen bajo la carpeta temporal (los de u1 y el audio de m2)
#   MEDIA_DIMS       detect.dims por nombre de archivo (sin él, falla y cuenta como sin medidas)
#   MEDIA_SFX_FILES  la biblioteca de SFX (sfx.get_base) para search_sfx
#   MEDIA_STEPS      [tool, argumentos] en orden: cada paso da su resultado o su error y el proyecto y los
#                    archivos tras él
from projects_cases import _UPG_CLIPS, _UPG_DIMS, _UPG_FILES, _UPG_TL  # noqa: E402
from projects_cases import project as _project  # noqa: E402

MEDIA_PROJECTS = [
    _project("u1", name="Mejorar", clips=_UPG_CLIPS, timeline=_UPG_TL),
    _project("m2", name="Borrar", audios=[{"id": "a1", "filename": "voz.mp3", "url": "/a1", "duration": 3.0, "label": "Voz"},
                                          {"id": "a2", "filename": "musica.mp3", "url": "/a2", "duration": 9.0}],
             images=[{"id": "i1", "filename": "logo.png", "url": "/i1", "label": "Logo"},
                     {"id": "i2", "filename": "foto.png", "url": "/i2"}]),
]
MEDIA_FILES = [*_UPG_FILES, "clips/m2/audio/voz.mp3", "clips/m2/audio/musica.mp3", "clips/m2/image/foto.png"]
MEDIA_DIMS = _UPG_DIMS
MEDIA_SFX_FILES = ["Impactos/golpe_seco.wav", "Impactos/golpe metálico.mp3", "Whoosh/aire_rapido.mp3",
                   "Whoosh/aire_lento.ogg", "Ambiente/lluvia.wav", "suelto.wav"]
MEDIA_STEPS = [
    # search_sfx: sin proyecto; el tope de limit entre 1 y 300.
    ["search_sfx", {}],
    ["search_sfx", {"query": "golpe"}],
    ["search_sfx", {"query": "aire", "limit": 1}],
    ["search_sfx", {"category": "Whoosh"}],
    ["search_sfx", {"category": "nada"}],
    ["search_sfx", {"limit": 0}],
    ["search_sfx", {"limit": -5}],
    ["search_sfx", {"limit": 1000}],
    ["search_sfx", {"query": "GOLPE metálico"}],
    # set_clip_ai_description: un campo aparte; strip() y el índice como número si son dígitos.
    ["set_clip_ai_description", {"project_id": "nada", "clip_index": "0", "description": "x"}],
    ["set_clip_ai_description", {"project_id": "u1", "clip_index": "999", "description": "x"}],
    ["set_clip_ai_description", {"project_id": "u1", "clip_index": "0", "description": "  Una charla\n"}],
    ["set_clip_ai_description", {"project_id": "u1", "clip_index": "local", "description": "Mi vídeo"}],
    ["set_clip_ai_description", {"project_id": "u1", "clip_index": "0", "description": ""}],
    ["set_clip_ai_description", {"project_id": "u1", "clip_index": "05", "description": "cero cinco"}],
    ["set_clip_ai_description", {"project_id": "u1", "clip_index": "4", "description": "　ancho　"}],
    # replace_media_version: dry_run solo busca; los LookupError son resource_not_found con su pista.
    ["replace_media_version", {"project_id": "nada", "clip_index": "0"}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "zzz", "dry_run": True}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "14", "dry_run": True}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "zzz"}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "0", "dry_run": True}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "10", "dry_run": True}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "local", "dry_run": True}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "0", "old_indexes": ["999"]}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "0", "old_indexes": ["0"]}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "0", "old_indexes": ["4"]}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "0", "old_indexes": ["5"]}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "0", "old_indexes": []}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "0"}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "0", "dry_run": True}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "10", "old_indexes": [0, "local"]}],
    ["replace_media_version", {"project_id": "u1", "clip_index": "10", "old_indexes": [9]}],
    # delete_media: el material y su archivo (si está); kind se comprueba antes que el proyecto.
    ["delete_media", {"project_id": "nada", "kind": "audio", "ident": "a1"}],
    ["delete_media", {"project_id": "nada", "kind": "audios", "ident": "a1"}],
    ["delete_media", {"project_id": "m2", "kind": "audios", "ident": "zz"}],
    ["delete_media", {"project_id": "m2", "kind": "audios", "ident": "a1"}],
    ["delete_media", {"project_id": "m2", "kind": "audios", "ident": "a1"}],
    ["delete_media", {"project_id": "m2", "kind": "images", "ident": "i1"}],
    ["delete_media", {"project_id": "m2", "kind": "images", "ident": "i2"}],
    ["delete_media", {"project_id": "m2", "kind": "clips", "ident": "0"}],
    ["delete_media", {"project_id": "u1", "kind": "clips", "ident": "5"}],
    ["delete_media", {"project_id": "u1", "kind": "clips", "ident": "local"}],
    ["delete_media", {"project_id": "u1", "kind": "clips", "ident": "11"}],
    ["delete_media", {"project_id": "u1", "kind": "clips", "ident": "14"}],
]
