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

Tras cada paso que puede escribir se guarda también el texto de projects.json.
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
