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
