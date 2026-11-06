"""Pasos del almacén de proyectos (projects.py) para export_fixtures.py.

Cada paso es una tupla (tipo, *args) que se ejecuta con projects.py sobre un projects.json
temporal, con uuid4 en la secuencia de SequentialIdSource y _now fijado (la n-ésima llamada
da 2026-01-01T00:00:00+00:00 + n segundos). «{TMP}» es la carpeta temporal, con «/»: la de
los clips por defecto es «{TMP}/clips».

  file contenido               escribe projects.json (texto o JSON)
  remove                       borra projects.json
  disk [rutas]                 crea archivos bajo {TMP} (su contenido es su ruta)
  load                         _load()
  list / groups                list_projects() / list_groups()
  get pid                      get_project(pid)
  create nombre grupo          create_project(nombre, grupo)
  create_group nombre          create_group(nombre)
  rename_group gid nombre      rename_group(gid, nombre)
  delete_group gid             delete_group(gid)
  move pid grupo               move_project(pid, grupo)
  rename pid nombre            rename_project(pid, nombre)
  duplicate pid                duplicate_project(pid)
  set_folder pid ruta          set_folder(pid, ruta)
  delete pid                   delete_project(pid)
  add_clips pid [clips]        add_clips(pid, [ClipInfo(**c)])
  add_transcript pid t         add_transcript(pid, Transcript(**t))
  add_audio pid a              add_audio(pid, AudioInfo(**a))
  add_image pid i              add_image(pid, ImageInfo(**i))
  update pid lista id campos   update_material(...)
  remove_material pid lista id remove_material(...)
  manifest pid manifest        apply_manifest(pid, manifest)
  timeline pid tl              save_timeline(pid, tl)
  motion pid comp              save_motion_composition(pid, comp)
  delete_motion pid cid        delete_motion_composition(pid, cid)
  scenes pid doc               save_scene_directions(pid, doc)
  blueprint pid doc            save_visual_blueprint(pid, doc)
  retarget pid tipo id fn id2 fn2 por_nombre    retarget_timeline_asset(...)
  tree                         los archivos bajo {TMP}/clips (ruta relativa y contenido)
  segments pid id rangos dur   segments.create_segments(pid, id, rangos), con la sonda de duración
                               dando `dur` (None: falla, y vale 0)
  import_video pid nombre datos dur meta / import_audio pid nombre datos dur
                               videos.import_video / import_audio, con la sonda de duración
                               dando `dur` (None: falla, y vale 0)
  face_track pid id a b muestras forzar info
                               segments.face_track_material(...), con detect.face_track dando
                               `info` (None: falla)

Tras cada paso que puede escribir se guarda también el texto de projects.json tal cual (en
Windows, con los CRLF de write_text).
"""
from model_cases import T0, TRACKS_V4, clip, material, project

TL_V4 = {"schema_version": 4, "tracks": TRACKS_V4,
         "clips": [clip("c1", start=0, duration=2), clip("c2", start=2, duration=1.5, asset_id="1",
                                                           filename="clip_1.mp4")]}

AUDIO = {"id": "au1", "filename": "voz.wav", "url": "/api/media/aaaa0001/audio/voz.wav", "voice": "ana",
         "text": "Hola", "duration": 1.25}
IMAGE = {"id": "im1", "filename": "foto.png", "url": "/api/media/aaaa0001/image/foto.png", "width": 640,
         "height": 480}
TRANSCRIPT = {"id": "tr1", "model": "small", "language": "es",
              "segments": [{"start": 0, "end": 1.5, "text": "hola mundo"}]}

START = {
    "projects": [
        project("aaaa0001", name="Primero", created_at=T0, group_id="g0000001",
                clips=[material(0, url="/api/media/aaaa0001/video/clip_0.mp4", label="cero"),
                       material(1, id="", url="/api/media/aaaa0001/video/clip_1.mp4"),
                       material(2, id="propio")],
                audios=[AUDIO], images=[IMAGE], transcripts=[TRANSCRIPT],
                timeline=TL_V4, extra_key={"se": "conserva"},
                motion_compositions=[{"id": "m1", "name": "uno"}, {"id": "m2"}],
                notes="/api/projects/aaaa0001/x y /api/media/aaaa0001/ y /api/media/aaaa0001b/"),
        project("bbbb0002", name="Segundo", folder="{TMP}/propia", clips=[]),
        project("cccc0003", name="Tercero", group_id="g0000002"),
    ],
    "groups": [
        {"id": "g0000001", "name": "Carpeta 1", "created_at": T0},
        {"id": "g0000002", "name": "Carpeta 2", "created_at": T0, "color": "rojo"},
    ],
    "otra_clave": [1, 2],
}

STEPS = [
    # Sin projects.json y con archivos que no se pueden leer: {"projects": []}.
    ("load",), ("list",), ("groups",), ("get", "x"),
    ("create", "  Nuevo  ", None), ("create", "", "no_existe"), ("list",),
    ("file", "{ roto"), ("load",), ("list",),
    ("file", "﻿" + '{"projects": [], "con_bom": 1}'), ("load",),
    ("remove",), ("create_group", "  Mi carpeta "), ("create_group", "   "), ("groups",),

    # El estado de partida, con claves que no son del modelo.
    ("file", START),
    ("disk", ["clips/aaaa0001/video/clip_0.mp4", "clips/aaaa0001/exports/final.mp4",
              "clips/aaaa0001/video/Exports/x.mp4", "clips/aaaa0001/motion/m1.json",
              "propia/video/b.mp4", "propia/exports/b.mp4", "clips/bbbb0002/motion/m.json"]),
    ("list",), ("groups",), ("get", "aaaa0001"), ("get", "cccc0003"), ("get", "nada"),
    ("create", "Con carpeta", "g0000002"), ("create", "Sin carpeta", "nada"), ("create", "Vacía", ""),
    ("create_group", "Tercera"), ("rename_group", "g0000001", "  Renombrada "), ("rename_group", "nada", "x"),
    ("rename_group", "g0000001", " "), ("groups",),
    ("move", "cccc0003", "g0000001"), ("move", "cccc0003", None), ("move", "cccc0003", ""),
    ("move", "cccc0003", "nada"), ("move", "nada", "g0000001"), ("move", "nada", None),
    ("rename", "aaaa0001", "  Primero bis "), ("rename", "aaaa0001", "  "), ("rename", "nada", "x"),
    ("delete_group", "g0000002"), ("delete_group", "nada"), ("list",),

    # Duplicar: id nuevo, URLs al id nuevo, delante del original, archivos sin exports/.
    ("duplicate", "aaaa0001"), ("duplicate", "bbbb0002"), ("duplicate", "nada"), ("tree",), ("list",),

    # Carpeta en disco, timeline y documentos.
    ("set_folder", "cccc0003", "{TMP}/otra"), ("set_folder", "nada", "x"), ("get", "cccc0003"),
    ("set_folder", "aaaa0001", "{TMP}/propia2"),   # Project(**p): la timeline v4 y el clip sin id, sin preparar
    ("timeline", "cccc0003", TL_V4), ("timeline", "cccc0003", {}), ("timeline", "nada", {}),
    ("scenes", "aaaa0001", {"escenas": [1, 2]}), ("scenes", "nada", {}),
    ("blueprint", "aaaa0001", {"estilo": "noir"}), ("blueprint", "nada", {}),
    ("motion", "aaaa0001", {"id": "m1", "name": "uno bis"}), ("motion", "aaaa0001", {"id": "m3"}),
    ("motion", "aaaa0001", {"name": "sin id"}), ("motion", "aaaa0001", {"id": ""}),
    ("motion", "cccc0003", {"id": "m1"}), ("motion", "nada", {"id": "m1"}),
    ("delete_motion", "aaaa0001", "m2"), ("delete_motion", "aaaa0001", "m2"), ("delete_motion", "nada", "m1"),
    ("delete_motion", "bbbb0002", "m1"),

    # Materiales.
    ("add_clips", "aaaa0001", [material(1, url="/n/1.mp4"), material(2, url="/n/2.mp4"),
                                material(0, url="/n/0.mp4"), material(7, url="/n/7.mp4"),
                                material(8, id="mio", url="/n/8.mp4")]),
    ("add_clips", "bbbb0002", [material(0), material(0)]), ("add_clips", "nada", [material(5)]),
    ("add_clips", "aaaa0001", []),
    ("add_transcript", "aaaa0001", TRANSCRIPT), ("add_transcript", "nada", TRANSCRIPT),
    ("add_audio", "bbbb0002", AUDIO), ("add_image", "bbbb0002", IMAGE), ("add_audio", "nada", AUDIO),
    ("update", "aaaa0001", "clips", "1", {"label": "uno", "description": None, "nuevo": 5}),
    ("update", "aaaa0001", "clips", "propio", {"label": "x"}), ("update", "aaaa0001", "clips", "2", {"label": "dos"}),
    ("update", "aaaa0001", "audios", "au1", {"label": "voz"}), ("update", "aaaa0001", "audios", "nada", {}),
    ("update", "aaaa0001", "audios", "au1", {"voice": None, "label": None, "speed": 1.5}),  # null no se aplica
    ("update", "aaaa0001", "otra", "1", {}), ("update", "nada", "clips", "1", {}),
    ("remove_material", "aaaa0001", "clips", "7"), ("remove_material", "aaaa0001", "images", "im1"),
    ("remove_material", "aaaa0001", "clips", "99"), ("remove_material", "nada", "clips", "0"),
    ("manifest", "aaaa0001", {"project": {"name": "  Del manifest "},
                              "materials": [{"file": "clip_0.mp4", "label": "L0", "start": 1, "index": 99},
                                            {"file": "voz.wav", "label": "voz", "voice": "eva", "text": "no"},
                                            {"file": "", "label": "ignorado"}, {"label": "sin file"},
                                            {"file": "clip_0.mp4", "description": "gana el último"}]}),
    ("manifest", "aaaa0001", {}), ("manifest", "aaaa0001", {"project": None, "materials": None}),
    ("manifest", "nada", {"project": {"name": "x"}}),

    # Apuntar la timeline a la biblioteca (lo que hace «Guardar en la biblioteca»).
    ("timeline", "aaaa0001", {"tracks": TRACKS_V4,
                              "clips": [clip("c1", asset_id="0", filename="clip_0.mp4"),
                                        clip("c2", asset_id=1, filename="otro.mp4"),
                                        clip("c3", asset_id="9", filename="clip_0.mp4"),
                                        clip("c4", asset_kind="audios", asset_id="0", filename="clip_0.mp4"),
                                        clip("c5", asset_id="8", filename="x8.mp4")]}),
    ("retarget", "aaaa0001", "clips", "0", "clip_0.mp4", "lib1", "lib1.mp4", True),
    ("retarget", "aaaa0001", "clips", 1, "x8.mp4", "lib2", "lib2.mp4", False),   # sin nombre: c5 no cambia
    ("retarget", "nada", "clips", "0", "x", "y", "z", True),
    ("get", "aaaa0001"),

    ("delete", "bbbb0002"), ("delete", "nada"), ("list",),

    # projects.json con otra forma.
    ("file", []), ("list",), ("get", "x"), ("groups",), ("create", "x", None),
    ("file", {}), ("list",), ("groups",), ("create", "x", None), ("create_group", "x"), ("delete", "x"),
    ("file", {"projects": {}}), ("list",), ("get", "x"), ("delete", "x"), ("create", "x", None),
    ("file", {"projects": {"a": 1}}), ("list",), ("get", "x"),
    ("file", {"projects": "abc"}), ("list",),
    ("file", {"projects": None}), ("list",),
    ("file", {"projects": [1]}), ("list",), ("get", "x"),
    ("file", {"projects": [{"name": "sin id"}]}), ("get", "x"), ("delete", "x"),
    ("file", {"projects": [project("p1"), {"id": "p2"}]}), ("get", "p1"), ("get", "p2"), ("list",),
    ("file", {"projects": [project("p1", clips=None)], "groups": None}), ("groups",),
    ("add_clips", "p1", [material(0)]), ("create", "x", "g"),
    ("file", {"projects": [project("p1")], "groups": [{"name": "sin id"}]}), ("create", "x", "g"),
    ("create", "x", None), ("move", "p1", "g"), ("delete_group", "g"),
    # == de Python: True == 1, así que el clip con index True se actualiza (y conserva su id).
    ("file", {"projects": [project("p1", clips=[{**material(0, id="b"), "index": True}, {**material(2), "index": 2.0}])]}),
    ("add_clips", "p1", [material(1, url="/v/1.mp4"), material(2, url="/v/2.mp4")]),
    ("file", {"projects": [project("p1", audios=None, motion_compositions=None)]}),
    ("add_audio", "p1", AUDIO), ("delete_motion", "p1", "m"), ("motion", "p1", {"id": "m"}),
    ("update", "p1", "audios", "x", {}),
]

# --- Casos adversariales de la revisión: ids y grupos numéricos, set_folder y timelines de otro
# tipo, manifest con `file` y `project` raros, materiales con index raros, motion y retarget con
# tipos raros, grupos de otra forma, EXPORTS y un archivo «exports» al duplicar, y Unicode en strip.

BASE = {"projects": [project("p1", name="Uno", clips=[material(0, url="/api/media/p1/video/clip_0.mp4")]),
                      project("5", name="Cinco", group_id=5)],
        "groups": [{"id": 5, "name": "num", "created_at": T0}, {"id": "g1", "name": "g", "created_at": T0}]}
G = {"id": "g", "name": "n", "created_at": T0}
STEPS_IDS = [
    ("file", BASE), ("get", "5"), ("list",), ("move", "5", "g1"), ("move", "p1", "5"), ("delete_group", "5"),
    ("rename_group", "5", "x"), ("create", "n", "5"), ("delete", "5"),
    ("file", BASE), ("set_folder", "p1", 5), ("set_folder", "p1", None), ("set_folder", "p1", ""), ("set_folder", "p1", []),
    ("set_folder", "p1", "  "), ("get", "p1"),
    ("timeline", "p1", []), ("timeline", "p1", None), ("timeline", "p1", "x"), ("timeline", "p1", {"schema_version": 99}),
    ("timeline", "p1", {"schema_version": 5, "tracks": TRACKS_V4, "clips": [clip("c1", start=1.5)]}),
]
STEPS_DUP = [
    ("file", {"projects": [project("p1", name="Uno", folder="{TMP}/mi/carpeta", notes={"/api/media/p1/x": "/api/projects/p1/y"}),
                            project("p2", name="Dos", folder="")]}),
    ("disk", ["mi/carpeta/exports", "mi/carpeta/video/EXPORTS", "mi/carpeta/audio/a.wav", "mi/carpeta/x/exports/deep.txt",
              "mi/carpeta/video/exports.txt", "mi/carpeta/image/Export/z.png", "clips/p1/motion/m.json", "clips/p2/video/v.mp4",
              "clips/p2/motion/q/r/s.json", "clips/p2/exports/e.mp4"]),
    ("duplicate", "p1"), ("duplicate", "p2"), ("tree",), ("list",), ("data",),
    ("duplicate", "p2"), ("tree",),
    ("set_folder", "p2", "{TMP}/noexiste"), ("duplicate", "p2"), ("tree",),
    ("disk", ["unarchivo"]), ("set_folder", "p2", "{TMP}/unarchivo"), ("duplicate", "p2"), ("tree",),
]
STEPS_MANIFEST = [
    ("file", {"projects": [project("p1", clips=[{**material(0), "filename": "a.mp4"}, {**material(1), "filename": 1}, {**material(2), "filename": True}, {**material(3), "filename": None}, {**material(4), "filename": ["x"]}])]}),
    ("manifest", "p1", {"materials": [{"file": 1, "label": "uno"}]}),
    ("manifest", "p1", {"materials": [{"file": True, "label": "true"}]}),
    ("manifest", "p1", {"materials": [{"file": 1.0, "label": "flt"}]}),
    ("manifest", "p1", {"materials": [{"file": ["x"], "label": "bad"}]}),
    ("manifest", "p1", {"materials": [{"file": {"a": 1}}]}),
    ("manifest", "p1", {"materials": {"a": 1}}), ("manifest", "p1", {"materials": 5}), ("manifest", "p1", {"materials": "x"}),
    ("manifest", "p1", {"project": {"name": 5}}), ("manifest", "p1", {"project": {"name": ["a"]}}), ("manifest", "p1", {"project": "abc"}),
    ("manifest", "p1", {"project": [1]}), ("manifest", "p1", {"project": {"name": "   "}}),
    ("manifest", "p1", []), ("manifest", "p1", None),
    ("manifest", "p1", {"materials": [{"file": "a.mp4", "label": None, "start": None, "end": 3.5}]}),
]
STEPS_MAT = [
    ("update", "p1", "clips", "0", {"label": {"a": [1]}, "start": 1.0}), ("update", "p1", "clips", "0", []), ("update", "p1", "clips", "0", None),
    ("update", "p1", "clips", "nomatch", []), ("remove_material", "p1", "clips", "None"), ("remove_material", "p1", "clips", "0"),
    ("file", {"projects": [project("p1", clips=[{"id": "a"}, {"index": None, "id": "b"}, {"index": 1.0}, {"index": True}])]}),
    ("update", "p1", "clips", "a", {"label": "A"}), ("update", "p1", "clips", "None", {"label": "N"}), ("update", "p1", "clips", "1.0", {"label": "F"}),
    ("update", "p1", "clips", "True", {"label": "T"}), ("update", "p1", "clips", "1", {"label": "U"}),
    ("remove_material", "p1", "clips", "1.0"),
    ("file", {"projects": [project("p1", clips=[{"index": 1.5, "id": "x"}, {"index": "1", "id": "y"}, {"index": 1, "id": ""}])]}),
    ("add_clips", "p1", [material(1)]),
    ("file", {"projects": [project("p1", clips=[{"id": "x"}, "str"])]}), ("add_clips", "p1", [material(1)]),
    ("file", {"projects": [project("p1", clips={})]}), ("add_clips", "p1", [material(1)]), ("add_clips", "p1", []),
    ("file", {"projects": [project("p1", clips="")]}), ("add_clips", "p1", [material(1)]), ("add_clips", "p1", []),
]
STEPS_MOTION = [
    ("file", {"projects": [project("p1", motion_compositions=[{"id": 5}, {"id": True}, "s"])]}),
    ("motion", "p1", {"id": 5, "n": 1}), ("motion", "p1", {"id": "5"}), ("motion", "p1", {"id": 1}), ("motion", "p1", {"id": "x"}),
    ("motion", "p1", []), ("motion", "p1", None), ("motion", "p1", {"id": []}),
    ("delete_motion", "p1", "5"), ("delete_motion", "p1", "x"),
    ("file", {"projects": [project("p1", motion_compositions=[{"id": 5}, {"id": "a"}, {"no": 1}])]}),
    ("delete_motion", "p1", "5"), ("delete_motion", "p1", "a"), ("delete_motion", "p1", "zz"),
    ("file", {"projects": [project("p1", motion_compositions=0)]}), ("delete_motion", "p1", "a"), ("motion", "p1", {"id": "a"}),
]
STEPS_RETARGET = [
    ("file", {"projects": [project("p1", timeline={"schema_version": 5, "tracks": TRACKS_V4, "clips": [
        clip("c1", asset_id=None, filename=None), clip("c2", asset_id=0, filename=0), clip("c3", asset_id=0.0, filename=False),
        clip("c4", asset_kind=None), clip("c5", asset_id="None")]})]}),
    ("retarget", "p1", "clips", None, None, "n", "f", True), ("retarget", "p1", "clips", 0, 0, "n2", "f2", True),
    ("retarget", "p1", "clips", "0.0", False, "n3", "f3", True), ("retarget", "p1", "clips", "x", {"a": 1}, "n", "f", True),
    ("get", "p1"),
    ("file", {"projects": [project("p1", timeline={"clips": 5})]}), ("retarget", "p1", "clips", "0", "x", "n", "f", True),
    ("file", {"projects": [project("p1", timeline={"clips": [5]})]}), ("retarget", "p1", "clips", "0", "x", "n", "f", True),
    ("file", {"projects": [project("p1", timeline=[])]}), ("retarget", "p1", "clips", "0", "x", "n", "f", True),
    ("file", {"projects": [project("p1", timeline={"clips": {}})]}), ("retarget", "p1", "clips", "0", "x", "n", "f", True),
]
STEPS_GROUPS = [
    ("file", {"projects": [project("p1", group_id="g")], "groups": []}), ("delete_group", "g"),
    ("file", {"projects": [project("p1", group_id="g")], "groups": [G]}), ("delete_group", "g"), ("data",),
    ("file", {"projects": 5, "groups": [G]}), ("delete_group", "g"),
    ("file", {"groups": [G]}), ("delete_group", "g"), ("delete_group", "z"),
    ("file", {"projects": [project("p1")], "groups": [{**G, "extra": 1}]}), ("groups",), ("rename_group", "g", "nn"), ("data",),
    ("file", {"projects": [project("p1")], "groups": "x"}), ("create", "a", "g"), ("create", "a", None), ("create_group", "q"),
    ("file", {"projects": [project("p1")], "groups": {}}), ("create", "a", "g"), ("create_group", "q"), ("groups",),
    ("file", {"projects": [project("p1")], "groups": [[]]}), ("create", "a", "g"),
]
STEPS_UNI = [
    ("file", {"projects": [project("p1")]}),
    ("create", "\u00a0\u2003 tab\t \u00f1and\u00fa \U0001f3ac \u200b ", None), ("create", "\u3000", None), ("create_group", "\ufeffx"),
    ("rename", "p1", "\x1c\x1d x \x85"),
    ("data",),
]

for _steps in (STEPS_IDS, STEPS_DUP, STEPS_MANIFEST, STEPS_MAT, STEPS_MOTION, STEPS_RETARGET, STEPS_GROUPS, STEPS_UNI):
    STEPS += [("remove",), *_steps]

# Sin «groups» ni «materials»: los defectos de .get(…, []) (en C++ fueron un puntero a un temporal).
STEPS += [("remove",), ("file", {"projects": [project("p1")]}), ("groups",), ("create", "x", "g"),
          ("move", "p1", "g"), ("manifest", "p1", {}), ("groups",)]

# Números y NaN como Python: los reales salen con repr(), y NaN, Infinity y 1e999 se leen (antes,
# C++ tomaba el archivo por ilegible y la primera escritura lo vaciaba).
STEPS += [
    ("remove",),
    ("file", '{"projects": [], "f15": 1e15, "f15b": 1234567890123456.0, "f16": 1e16, "fm5": 1e-5, '
             '"fm4": 0.0001, "u64": 18446744073709551615, "neg0": -0.0, "e": 1E5, "r": 279.03900000000567}'),
    ("create", "x", None), ("data",),
    ("remove",),
    ("file", '{"projects": [], "x": NaN, "y": Infinity, "z": -Infinity, "w": 1e999, "v": -1e999, "u": 1e-999}'),
    ("load",), ("create", "a", None), ("data",),
]

# Duplicar un proyecto con NaN e Infinity: json.dumps/json.loads los conservan (el dump de
# nlohmann los pasaba a null).
STEPS += [
    ("remove",),
    ("file", '{"projects": [{"id": "p1", "name": "uno", "created_at": "2026-01-01T00:00:00+00:00", "clips": '
             '[{"index": 0, "filename": "a.mp4", "url": "/api/media/p1/a.mp4", "start": 0, "end": NaN, '
             '"zoom": Infinity}]}]}'),
    ("duplicate", "p1"), ("data",),
]

# --- Segmentos por referencia y seguimiento de caras (segments.py) --------------------------
# «Crear clip» del Clip Editor: materiales que apuntan al archivo de otro con in/out_point, sin
# render. El índice nuevo pasa de 99999; la etiqueta por defecto lleva m:ss–m:ss.
_SEG_SRC = material(0, id="src0", url="/api/media/s1/video/clip_0.mp4", label="  Entrevista ",
                    description="desc origen", source="external", provider="pexels", author="Ana",
                    license_info="CC-BY", source_url="https://ejemplo/v")
_SEG_START = {"projects": [project("s1", name="Segmentos", clips=[
    _SEG_SRC,
    material(3, filename="sin_label.mp4"),
    material(100005, id="seg", filename="clip_0.mp4", in_point=2.0, out_point=5.5, parent_id="src0"),
    material(4, filename="falta.mp4"),
    material(8, id="largo", filename="largo.mp4", start=100, end=130),
    material(9, id="cache", filename="clip_0.mp4", in_point=1.0, out_point=4.0,
             face_track={"start": 1.0, "end": 4.0, "width": 640, "height": 360, "fps": 25,
                         "samples": 3, "track": [{"t": 1.0, "cx": 0.4, "cy": 0.5, "w": 0.1, "h": 0.2}],
                         "keyframes": [{"t": 1.0, "cx": 0.4, "cy": 0.5}], "created_at": T0}),
    ]), project("s2", name="Índices bajos", clips=[material(0), material(5, filename="clip_0.mp4")])]}


def _track(a: float, b: float, n: int, wobble: float = 0.08) -> list[dict]:
    """Detecciones deterministas entre a y b: un centro que se mueve despacio (y a veces nada)."""
    import math
    out = []
    for i in range(n):
        t = a + (b - a) * i / max(1, n - 1)
        out.append({"t": round(t, 3), "cx": round(0.5 + wobble * math.sin(t * 1.7), 4),
                    "cy": round(0.45 + 0.5 * wobble * math.cos(t * 0.9), 4), "w": 0.12, "h": 0.2})
    return out


def _info(track: list[dict]) -> dict:
    return {"width": 1920, "height": 1080, "fps": 29.97, "samples": len(track), "track": track}


STEPS_SEG = [
    ("file", _SEG_START),
    ("disk", ["clips/s1/video/clip_0.mp4", "clips/s1/video/sin_label.mp4", "clips/s1/video/largo.mp4",
              "clips/s2/video/clip_0.mp4"]),
    ("segments", "s2", "0", [{"start": 0, "end": 1}, {"start": 1, "end": 2}], 5.0),  # del 100000 en adelante
    # Varios rangos: al revés, con etiqueta y descripción propias, negativos y recortados al final.
    ("segments", "s1", "src0", [{"start": 1, "end": 3.4567}, {"start": 10, "end": 8, "label": " Mi corte ",
                                                              "description": "  d  ", "extra": 1},
                                {"start": -1, "end": 0.5}, {"start": 7.5, "end": 20}], 9.0),
    ("segments", "s1", "0", [{"start": " 2.5 ", "end": "4"}, {"start": True, "end": 3}], 30.0),
    ("segments", "s1", "3", [{"start": 59.5, "end": 125.2}], None),       # sin etiqueta: el nombre
    ("segments", "s1", "seg", [{"start": 2, "end": 3}], 0.0),             # de un segmento: su origen
    ("segments", "s1", "src0", [{"start": 1, "end": 1.2}], 9.0),
    ("segments", "s1", "src0", [{"start": 1, "end": 2}, {"start": 3, "end": 3.1}], 9.0),
    ("segments", "s1", "src0", [{"start": "x", "end": 2}], 9.0),
    ("segments", "s1", "src0", [{"end": 2}], 9.0),
    ("segments", "s1", "src0", [{"start": [1], "end": 2}], 9.0),
    ("segments", "s1", "src0", [], 9.0),
    ("segments", "s1", "src0", None, 9.0),
    ("segments", "s1", "src0", ["a"], 9.0),
    ("segments", "s1", "src0", [{"start": 1, "end": float("inf"), "label": "infinito"}], None),
    ("segments", "s1", "src0", [{"start": 1, "end": float("inf")}], None),  # m:ss de infinito: falla
    ("segments", "s1", "zz", [{"start": 1, "end": 2}], 9.0),
    ("segments", "nada", "0", [{"start": 1, "end": 2}], 9.0),
    ("segments", "s1", "4", [{"start": 1, "end": 2}], 9.0),               # sin archivo
    ("get", "s1"),
    # Seguimiento de caras: el del propio rango se guarda (y siembra el reframe); el mismo rango
    # sale de la caché salvo que se fuerce; otro rango no se guarda.
    ("face_track", "s1", "seg", None, None, 0, False, _info(_track(2.0, 5.5, 36))),
    ("face_track", "s1", "seg", None, None, 0, False, _info(_track(2.0, 5.5, 5))),
    ("face_track", "s1", "seg", 2.03, 5.47, 0, False, _info(_track(2.0, 5.5, 5))),
    ("face_track", "s1", "seg", None, None, 12, True, _info(_track(2.0, 5.5, 12, wobble=0.01))),
    ("face_track", "s1", "seg", 2.5, 4.0, 0, False, _info(_track(2.5, 4.0, 8))),
    ("face_track", "s1", "seg", -3, 4.0, 0, False, _info(_track(0.0, 4.0, 8))),
    ("face_track", "s1", "seg", 3.0, 3.2, 0, False, _info([])),
    ("face_track", "s1", "cache", None, None, 0, False, _info(_track(1.0, 4.0, 4))),
    ("face_track", "s1", "cache", 1.0, 4.2, 0, False, _info([])),
    ("face_track", "s1", "largo", None, None, 0, False, _info(_track(-1.0, 31.0, 90, wobble=0.2))),
    ("face_track", "s1", "3", None, None, 0, False, None),               # el detector falla
    ("face_track", "s1", "4", None, None, 0, False, _info([])),          # sin archivo
    ("face_track", "s1", "zz", None, None, 0, False, _info([])),
    ("face_track", "nada", "0", None, None, 0, False, _info([])),
    # Tiempos no finitos del detector: inf // ventana es NaN e int() falla; NaN cae en la ventana 0.
    ("face_track", "s1", "seg", 2.5, 4.0, 0, True,
     _info([{"t": float("inf"), "cx": 0.5, "cy": 0.5, "w": 0.1, "h": 0.1}])),
    ("face_track", "s1", "seg", 2.5, 4.0, 0, True,
     _info([{"t": float("nan"), "cx": 0.3, "cy": 0.6, "w": 0.1, "h": 0.1},
            {"t": 3.0, "cx": 0.7, "cy": 0.4, "w": 0.1, "h": 0.1}])),
    ("get", "s1"),
]
STEPS += [("remove",), *STEPS_SEG]

# --- Cambiar la versión ligera por la de más calidad (media_upgrade.py) ----------------------
# versions pid id dims / upgrade pid id viejos dims: detect.dims da dims[nombre] ([w, h]; si no
# está, falla y vale (0, 0)) y timeline_store.apply_op guarda el historial en {TMP}/data/history.
_FT = {"start": 10.0, "end": 20.0, "width": 854, "height": 480, "fps": 25, "samples": 2,
       "track": [{"t": 10.5, "cx": 0.4, "cy": 0.5, "w": 0.1, "h": 0.2}, {"t": 19.25, "cx": 0.6, "cy": 0.5}],
       "keyframes": [{"t": 10.0, "cx": 0.4, "cy": 0.5}, {"t": 20.0, "cx": 0.6, "cy": 0.5, "zoom": 0.9}],
       "created_at": T0}
_UPG_CLIPS = [
    material(0, id="hq", filename="hq.mp4", url="/api/media/u1/video/hq.mp4", start=0, end=600, label="HQ",
             provider="youtube", external_id="VID1", created_at="2026-02-02T00:00:00+00:00"),
    material(1, filename="lq480.mp4", url="/api/media/u1/video/lq480.mp4", start=0, end=600, label="480p",
             source_url="https://www.youtube.com/watch?v=VID1"),
    material(2, id="tramo", filename="tramo.mp4", url="/api/media/u1/video/tramo.mp4", start=120, end=180,
             source_url="youtu.be/VID1", label=""),
    material(3, id="segref", filename="lq480.mp4", url="/api/media/u1/video/lq480.mp4", start=10, end=20,
             in_point=10, out_point=20, parent_id="1", face_track=_FT),
    material(4, filename="crop.mp4", start=0, end=600, provider="youtube", external_id="VID1",
             reframe={"zoom": 0.8}),
    material(5, filename="vert.mp4", start=0, end=600, provider="youtube", external_id="VID1"),
    material(7, filename="long.mp4", start=590, end=700, provider="youtube", external_id="VID1"),
    material(8, filename="otro.mp4", start=0, end=60, provider="youtube", external_id="VID2"),
    material(9, id="local", filename="local.mp4", start=0, end=600, label="Local"),
    material(10, filename="hq2.mp4", start=0, end=600, provider="youtube", external_id="VID1"),
    material(11, filename="nodims.mp4", start=0, end=600, provider="youtube", external_id="VID1"),
    material(12, filename="kfzoom.mp4", start=0, end=600, provider="youtube", external_id="VID1",
             reframe={"keyframes": [{"t": 0, "cx": 0.5, "zoom": 0.5}]}),
    material(13, filename="master.mp4", start=0, end=600, provider="youtube", external_id="VID1",
             reframe={"zoom": 0.5, "master": True}),
    material(14, filename="segtramo.mp4", start=0, end=30, in_point=2, out_point=8),   # segmento sin dueño
    material(15, id="partial", filename="partial.mp4", start=100, end=400, provider="youtube",
             external_id="VID1"),
    material(16, filename="near.mp4", start=0, end=600, provider="youtube", external_id="VID1"),   # 2,8 %
    material(17, filename="edge.mp4", start=0, end=600.5, provider="youtube", external_id="VID1"),  # +0,5 s
    material(18, id="near0", filename="near0.mp4", start=99.97, end=300, provider="youtube",
             external_id="VID1"),                                                                  # −0,03 s
]
_UPG_TL = {"schema_version": 5, "tracks": TRACKS_V4, "clips": [
    clip("u1", start=0, duration=5, asset_id="1", filename="lq480.mp4", in_point=30, out_point=35,
         reframe={"keyframes": [{"t": 31, "cx": 0.3}]}, beats={"times": [30.5, 32, "x"]}),
    clip("u2", start=5, duration=4, asset_id="1", filename="lq480.mp4", in_point=590, out_point=594),
    clip("u3", start=9, duration=3, asset_id="2", filename="tramo.mp4", in_point=1, out_point=4),
    clip("u4", start=12, duration=2, asset_id="4", filename="crop.mp4", in_point=0, out_point=2),
    clip("u5", start=14, duration=2, asset_id="9", filename="local.mp4", in_point=0, out_point=2),
    clip("u6", track_id="V2", start=0, duration=2, asset_id="lib1", filename="lq480.mp4", asset_scope="library",
         in_point=0, out_point=2),
    clip("u7", track_id="V2", start=3, duration=2, asset_id="1", filename="lq480.mp4", in_point=1, out_point=3,
         bg_removal={"auto": {"enabled": True, "base_key": "k", "status": "ready", "edits": [{"t": 1.5}]}}),
]}
_UPG_DIMS = {"hq.mp4": [1920, 1080], "lq480.mp4": [854, 480], "tramo.mp4": [1280, 720], "crop.mp4": [854, 480],
             "vert.mp4": [1080, 1920], "long.mp4": [640, 360], "otro.mp4": [640, 360], "local.mp4": [1280, 720],
             "hq2.mp4": [3840, 2160], "kfzoom.mp4": [854, 480], "master.mp4": [854, 480],
             "partial.mp4": [854, 480], "near.mp4": [1900, 1100], "edge.mp4": [854, 480], "near0.mp4": [854, 480]}
_UPG_FILES = [f"clips/u1/video/{c['filename']}" for c in _UPG_CLIPS if c["filename"] != "nodims.mp4"]
STEPS_UPG = [
    ("file", {"projects": [project("u1", name="Mejorar", clips=_UPG_CLIPS, timeline=_UPG_TL),
                           project("u2", name="Sin timeline", clips=[_UPG_CLIPS[0], _UPG_CLIPS[1]])]}),
    ("disk", _UPG_FILES),
    ("yt_sources", "u1", None), ("yt_sources", "u1", ""), ("yt_sources", "u1", " https://youtu.be/ABC "),
    ("yt_sources", "u1", "https://vimeo.com/1"), ("yt_sources", "u1", "  "), ("yt_sources", "nada", None),
    ("yt_sources", "nada", "youtube.com/watch?v=Z"),
    ("best_full", "u1", "VID1", _UPG_DIMS), ("best_full", "u1", "VID1", {}), ("best_full", "u1", "VID2", _UPG_DIMS),
    ("best_full", "u1", "NADA", _UPG_DIMS), ("best_full", "nada", "VID1", _UPG_DIMS),
    # La parcial (offset 100) y la que no es de provider youtube no cuentan aunque sean mayores.
    ("best_full", "u1", "VID1", {"partial.mp4": [7680, 4320], "hq.mp4": [1920, 1080], "lq480.mp4": [3000, 2000]}),
    ("versions", "u1", "hq", _UPG_DIMS),
    ("versions", "u1", "0", _UPG_DIMS),
    ("versions", "u1", "hq", {}),                         # sin medidas: nada es «lower»
    ("versions", "u1", "10", _UPG_DIMS),                # 4K: el HQ también es «lower»
    ("versions", "u1", "hq2", _UPG_DIMS),                 # sin id propio: el suyo es «10»
    ("versions", "u1", "local", _UPG_DIMS),               # sin YouTube: sin versiones
    ("versions", "u1", "partial", _UPG_DIMS),             # empieza en 100: las de 0 «empiezan antes»
    ("versions", "u1", "segref", _UPG_DIMS),              # un segmento no vale
    ("versions", "u1", "99", _UPG_DIMS),
    ("versions", "nada", "hq", _UPG_DIMS),
    ("upgrade", "u1", "hq", ["segref"], _UPG_DIMS),       # el dueño del segmento es lq480
    ("upgrade", "u1", "hq", ["hq"], _UPG_DIMS),           # por sí mismo
    ("upgrade", "u1", "hq", ["4"], _UPG_DIMS),            # encuadre horneado
    ("upgrade", "u1", "hq", ["5"], _UPG_DIMS),            # otra proporción
    ("upgrade", "u1", "hq", ["7"], _UPG_DIMS),            # dura más
    ("upgrade", "u1", "hq", ["zz"], _UPG_DIMS),
    ("upgrade", "u1", "segref", [], _UPG_DIMS),
    ("upgrade", "u2", "hq", [], _UPG_DIMS),               # sin timeline: solo segmentos (no hay)
    ("upgrade", "u1", "hq", [], _UPG_DIMS),               # las de menos calidad compatibles
    ("get", "u1"),
    ("versions", "u1", "hq", _UPG_DIMS),
    ("upgrade", "u1", "hq", [], _UPG_DIMS),               # ya no queda nada
    ("upgrade", "u1", "partial", ["near0"], _UPG_DIMS),   # sin usos: solo la lista, con su offset
    ("upgrade", "u1", "10", ["local", 9, "hq"], _UPG_DIMS),  # a mano: un archivo local y el viejo HQ
    ("get", "u1"),
    ("yt_sources", "u1", None), ("yt_sources", "u1", ""), ("yt_sources", "u1", " https://youtu.be/ABC "),
    ("yt_sources", "u1", "https://vimeo.com/1"), ("yt_sources", "u1", "  "), ("yt_sources", "nada", None),
    ("yt_sources", "nada", "youtube.com/watch?v=Z"),
    ("best_full", "u1", "VID1", _UPG_DIMS), ("best_full", "u1", "VID1", {}), ("best_full", "u1", "VID2", _UPG_DIMS),
    ("best_full", "u1", "NADA", _UPG_DIMS), ("best_full", "nada", "VID1", _UPG_DIMS),
    # La parcial (offset 100) y la que no es de provider youtube no cuentan aunque sean mayores.
    ("best_full", "u1", "VID1", {"partial.mp4": [7680, 4320], "hq.mp4": [1920, 1080], "lq480.mp4": [3000, 2000]}),
    # El mismo dueño dos veces (por su segmento y por él) con un NaN: `owner not in olds` lo encuentra
    # por identidad aunque NaN != NaN.
    ("file", {"projects": [project("n1", name="NaN", clips=[
        _UPG_CLIPS[0],
        material(1, filename="nan.mp4", url="/api/media/n1/video/nan.mp4", start=float("nan"), end=600,
                 provider="youtube", external_id="VID1"),
        material(2, id="segnan", filename="nan.mp4", url="/api/media/n1/video/nan.mp4", start=1, end=2,
                 in_point=1, out_point=2)],
        timeline={"schema_version": 5, "tracks": TRACKS_V4, "clips": [
            clip("n1c", start=0, duration=1, asset_id="1", filename="nan.mp4", in_point=0, out_point=1)]})]}),
    ("disk", ["clips/n1/video/hq.mp4", "clips/n1/video/nan.mp4"]),
    ("upgrade", "n1", "hq", ["segnan", "1"], {"hq.mp4": [1920, 1080], "nan.mp4": [854, 480]}),
    ("get", "n1"),
]
STEPS += STEPS_UPG

# Importar un vídeo o un audio local (videos.import_video e import_audio):
#   import_video pid nombre datos dur meta   con los datos como texto (sus bytes en UTF-8), la sonda de
#                                            duración dando `dur` (None: falla, y vale 0) y `meta` los
#                                            argumentos con nombre (label, description, origin…)
#   import_audio pid nombre datos dur
STEPS_IMPORT = [
    ("file", START),
    ("import_video", "aaaa0001", "Mi vídeo.MP4", "datos del vídeo", 12.5, {}),
    ("import_video", "aaaa0001", "carpeta/otro.mov", "x", None,
     {"label": "Etiqueta", "description": "Una descripción", "origin": "pexels", "source": "stock",
      "provider": "pexels", "external_id": "123", "source_url": "https://www.pexels.com/video/123/",
      "author": "Ana", "license_info": "Pexels License"}),
    ("import_video", "aaaa0001", "etiqueta vacía.webm", "x", 2.0, {"label": "", "origin": "", "source": ""}),
    ("import_video", "aaaa0001", "a.b.mkv", "x", 3.25, {}),
    ("import_video", "aaaa0001", '<>raro?*|"..avi', "x", 1.0, {}),
    ("import_video", "aaaa0001", "  espacios   en medio .m4v", "x", 0.0, {}),
    ("import_video", "aaaa0001", "malo.txt", "x", 1.0, {}),
    ("import_video", "aaaa0001", ".mp4", "x", 1.0, {}),
    ("import_video", "aaaa0001", "sin_ext", "x", 1.0, {}),
    ("import_video", "aaaa0001", "vacio.mp4", "", 1.0, {}),
    ("import_video", "bbbb0002", "en su carpeta.mp4", "y", 4.0, {}),
    ("import_video", "cccc0003", "ñandú.FLV", "z", 7.5, {}),
    ("import_audio", "aaaa0001", "Voz.WAV", "audio", 4.25),
    ("import_audio", "aaaa0001", "música.flac", "y", None),
    ("import_audio", "aaaa0001", "malo.mp4", "x", 1.0),
    ("import_audio", "aaaa0001", "vacio.mp3", "", 1.0),
    ("import_audio", "cccc0003", "ñandú.m4a", "z", 0.5),
    ("tree",), ("get", "aaaa0001"), ("get", "bbbb0002"), ("get", "cccc0003"),
]
STEPS += [("remove",), *STEPS_IMPORT]

# Importar una imagen (images.import_image, con OpenCV sustituido; ver import_image en
# export_fixtures.py):
#   import_image pid nombre datos_hex size meta keep_gif   `size` lo que da probe_size (None: falla)
from pathlib import Path as _Path  # noqa: E402

_PNG = b"\x89PNG\r\n\x1a\n"
_ANIM_GIF = (_Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "media" / "files" / "anim.gif").read_bytes()


def _img(pid, name, data: bytes, size=None, meta=None, keep_gif=True):
    return ("import_image", pid, name, data.hex(), size, meta or {}, keep_gif)


STEPS_IMAGE = [
    ("file", START),
    _img("aaaa0001", "Foto.PNG", _PNG + b"resto del png", [640, 360]),
    _img("aaaa0001", "foto.jpg", b"\xff\xd8\xffOK jpeg", [4, 3]),
    _img("aaaa0001", "rota.jpg", b"\xff\xd8\xff roto", [4, 3]),
    _img("aaaa0001", "vacía.png", b"", [1, 1]),
    _img("aaaa0001", "nota.txt", b"hola", [1, 1]),
    _img("aaaa0001", "nota.txt", _PNG + b"es un png", None),
    _img("aaaa0001", "sin_ext", b"OK sin firma", [2, 2]),
    _img("aaaa0001", "sin_ext2", b"sin firma", [2, 2]),
    _img("aaaa0001", "x.dat", b"RIFF\x00\x00\x00\x00WEBPOK", [8, 8]),
    _img("aaaa0001", "carpeta%2Fmi%20foto.png?x=1#frag", _PNG + b"q", [3, 3]),
    _img("aaaa0001", r"C:\fotos\de viaje.jpg", b"OK", [5, 5]),
    _img("aaaa0001", "   ", _PNG, [1, 2]),
    _img("aaaa0001", ".png", _PNG, [1, 2]),
    _img("aaaa0001", "%3F", _PNG, None),
    _img("aaaa0001", "anim.gif", _ANIM_GIF, None),
    _img("aaaa0001", "anim2.gif", _ANIM_GIF, [7, 8]),
    _img("aaaa0001", "anim3.gif", _ANIM_GIF, [0, 0]),
    _img("aaaa0001", "aplanado.gif", b"GIF89a OK", [9, 9], keep_gif=False),
    _img("aaaa0001", "corto.gif", b"GIF89a", None),
    _img("aaaa0001", "gif.png", b"GIF87a" + b"\x00" * 20, None),
    _img("aaaa0001", "stock.webp", b"OK", [100, 50],
         {"label": "Etiqueta", "description": "Una descripción", "origin": "pexels", "source": "stock",
          "provider": "pexels", "external_id": "9", "source_url": "https://www.pexels.com/photo/9/",
          "author": "Ana", "license_info": "Pexels License"}),
    _img("aaaa0001", "vacíos.png", _PNG, [1, 1], {"label": "", "description": "", "origin": "", "source": ""}),
    _img("bbbb0002", "en su carpeta.png", _PNG, [2, 2]),
    _img("cccc0003", "ñandú.JPEG", b"OK", [3, 3]),
    ("tree",), ("get", "aaaa0001"), ("get", "bbbb0002"), ("get", "cccc0003"),
]
STEPS += [("remove",), *STEPS_IMAGE]
