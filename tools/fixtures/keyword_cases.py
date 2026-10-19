"""Casos de las palabras clave locales de «Explorar» (explore_keywords.py) para export_fixtures.py."""

TEXTS = [
    "",
    "   ",
    "que el la de y por favor entonces quiero",
    "El tren atraviesa la nieve. El tren no se detiene: la nieve cubre todo el planeta.",
    "Snowpiercer: un tren que da vueltas al mundo congelado. En el tren viajan los últimos humanos; "
    "el tren es su mundo y la rebelión empieza en el último vagón del tren.",
    "La CIENCIA del cine épico: películas de ciencia ficción, ciencia ficción clásica y cine de culto.",
    "ÁRBOL árbol Árbol ñandú Ñandú pingüino PINGÜINO camión",
    "a1 b22 c333 4444 55555 año 2049 año 2049 blade runner 2049",
    "Ab Cd Ef ab cd ef abc def ghi abc def ghi abc def",
    "rocket rocket rocket launch launch space space space space the of and",
    "uno, dos; tres. cuatro! cinco? seis (siete) ocho-nueve diez_once",
    "İstanbul ıspanak ſtraße Kelvin día después",
    "música 🎵 emoji 😀 café ☕ mañana",
    "x" * 30 + " " + "y" * 30,
    "palabra " * 20,
    "gato gato",
    "perro perro perro gato gato",
    "ñú ño ñú ño casa",
    "luz luz sombra sombra luz",
]

RAWS = [
    "",
    '["Tren", "nieve", "planeta helado"]',
    '```json\n["Tren", "nieve"]\n```',
    '```JSON\n["a", "bb", "ccc"]\n```',
    '```\n["sin", "lenguaje", "fence"]```',
    'Claro, aquí tienes: ["uno", "dos", "tres"] espero que sirva',
    '["dup", "DUP", "Dup ", "  dup  "]',
    '["que", "por favor", "the", "Árbol", "árbol"]',
    '[1, 2.5, true, null, {"a": 1}, ["x"], "ok"]',
    '["  muchos    espacios\\n y saltos  "]',
    '["' + "z" * 60 + '"]',
    '["k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "k9", "k10", "k11", "k12"]',
    '{"keywords": ["no es lista"]}',
    "nada de json",
    "[roto, sin comillas]",
    "texto [\"a\"] y luego [\"b\"]",
    "```",
    '["ñandú", "Ñ", "ab"]',
]

PROJECT = {
    "id": "p1", "name": "Uno", "created_at": "2026-10-19T10:00:00+00:00",
    "audios": [{"id": "a1", "filename": "a.mp3", "url": "/u", "duration": 3.0, "description": "  Música épica  ",
                "text": "Narración del tren"}, {"id": "a2", "filename": "b.mp3", "url": "/u", "duration": 1.0}],
    "clips": [{"index": 1, "filename": "c.mp4", "url": "/c", "start": 0, "end": 5, "description": "Plano del tren",
               "transcript": {"id": "t", "model": "m", "segments": [{"start": 0, "end": 1, "text": " hola "},
                                                                    {"start": 1, "end": 2, "text": ""}]}}],
    "transcripts": [{"id": "t2", "model": "m", "segments": [{"start": 0, "end": 2, "text": "El mundo congelado"}]}],
    "timeline": {"schema_version": 5, "tracks": [{"id": "T1", "kind": "text", "name": "T1"}],
                 "clips": [{"id": "x", "track_id": "T1", "kind": "text", "asset_kind": "text", "asset_id": "x",
                            "filename": "", "text": "Subtítulo uno", "description": "nota"},
                           {"id": "v", "track_id": "T1", "kind": "video", "asset_kind": "clips", "asset_id": "1",
                            "filename": "c.mp4", "description": "no cuenta"},
                           {"id": "s", "track_id": "T1", "kind": "audio", "asset_kind": "audios", "asset_id": "a1",
                            "filename": "a.mp3", "text": "   "}]},
}

PROJECTS = [
    PROJECT,
    {"id": "p2", "name": "Vacío", "created_at": "2026-10-19T10:00:00+00:00"},
]
