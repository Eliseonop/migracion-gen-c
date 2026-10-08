"""Pasos de las colecciones de material (collections.py) para export_fixtures.py.

Cada paso es una tupla (tipo, *args) que se ejecuta con collections.py sobre una carpeta
temporal («{TMP}» en los ajustes, las rutas y los resultados es esa carpeta, con «/»):

  files [rutas]               crea archivos («x») bajo la carpeta
  file ruta contenido         escribe un archivo (texto o JSON)
  dirs [rutas]                crea carpetas
  assets ruta                 config.ASSETS_DIR: la raíz de fábrica es <ruta>/material
  settings contenido          settings.json (texto o JSON); settings.load() lo lee
  save parche                 settings.save(parche)
  meta                        {material_root, collections_meta} de settings.load()
  root                        get_root()
  set_root ruta               set_root(ruta)
  prefs id activa fav etiq    set_prefs(id, enabled=, favorite=, label=)
  safe_id texto               _safe_collection_id(texto)
  kind nombre                 kind_for(Path(nombre))
  entry colección ruta        _entry(colección, Path(ruta))
  scan colección              _scan_dir(get_root(), colección)
  list                        list_collections()
  match item q                _match(item, q)
  search q tipo colección lím search(q, tipo, colección, lím)
  create nombre               create_collection(nombre)
  add colección [[n, c]]      add_files(colección, [(n, BytesIO(c en UTF-8))])
  resolve ref                 resolve(ref), como ruta relativa a la raíz (o None)
  tree ruta                   [ruta relativa, contenido o None si es carpeta] de lo que hay
                              bajo <ruta>, ordenado
"""

SIGMA = "ΟΔΟΣ"            # ΟΔΟΣ: lower() con sigma final → οδος
FINAL = "οδος"            # οδος (ς)
MEDIAL = "οδοσ"           # οδοσ (σ)

M = "assets/material"

FILES = [
    "fondos/cielo.PNG", "fondos/sub/b.jpg", "fondos/Árbol.webp", "fondos/z.GIF", "fondos/a.gif",
    "fondos/notas.txt", "fondos/sin_ext", "fondos/.hidden.mp4", "fondos/x.mp4.bak", "fondos/clip.MOV",
    "fondos/sub/deep/v.mkv", "fondos/İx.png", "fondos/iz.png", "fondos/ß.png", "fondos/SS.jpeg",
    "fondos/sub/A.wav", "sonidos/a.mp3", "sonidos/b.WMA", "sonidos/c.flac", "sonidos/d.aiff",
    "Zeta/one.webm", "alfa/one.bmp", f"{SIGMA}/x.mpeg", "stick_char/stick.json", "stick_char/happy.png",
    "g r#ú%/x y.mp4", "solo_audio/s.ogg", "solo_audio/t.m4a", "solo_audio/u.aac",
]

STEPS = [
    # Sin raíz: ni la de los ajustes ni la de fábrica existen.
    ("assets", "no_existe"),
    ("root",), ("list",), ("search", "", "", "", 500), ("scan", "fondos"), ("resolve", "fondos/a.gif"),
    ("create", "nueva"), ("add", "fondos", [["a.png", "1"]]), ("add", "..", [["a.png", "1"]]),

    # La raíz de fábrica, con colecciones, archivos sueltos y carpetas que no son colección.
    ("files", [f"{M}/{f}" for f in FILES] + [f"{M}/suelto.mp4", "assets/fuera.mp4"]),
    ("dirs", [f"{M}/vacía", f"{M}/fondos/carpeta.mp4"]),
    ("assets", "assets"),
    ("root",), ("list",),
    ("scan", "fondos"), ("scan", "FONDOS"), ("scan", "sonidos"), ("scan", "vacía"), ("scan", "no_existe"),
    ("scan", "suelto.mp4"), ("scan", SIGMA), ("scan", "g r#ú%"),
    ("search", "", "", "", 500), ("search", "", "video", "", 500), ("search", "", " IMAGE ", "", 500),
    ("search", "", "gif", "", 500), ("search", "", "audio", "", 500), ("search", "", "otro", "", 500),
    ("search", "one", "", "", 500), ("search", "ONE alfa", "", "", 500), ("search", "árbol", "", "", 500),
    ("search", "ÁRBOL", "", "", 500), ("search", FINAL, "", "", 500), ("search", MEDIAL, "", "", 500),
    ("search", "x y", "", "", 500), ("search", "  ", "", "", 500), ("search", "nada", "", "", 500),
    ("search", "", "", "fondos", 500), ("search", "", "", " fondos ", 500), ("search", "", "", "Fondos", 500),
    ("search", "", "image", "fondos", 500), ("search", "sub", "", "", 500),
    ("search", "", "", "", 0), ("search", "", "", "", 3), ("search", "", "", "", -2), ("search", "", "", "", -100),
    ("resolve", "fondos/cielo.PNG"), ("resolve", "FONDOS/CIELO.png"), ("resolve", "fondos\\sub\\b.jpg"),
    ("resolve", "fondos/../sonidos/a.mp3"), ("resolve", "../fuera.mp4"), ("resolve", "fondos/../../fuera.mp4"),
    ("resolve", "{TMP}/assets/material/fondos/z.GIF"), ("resolve", "{TMP}/assets/fuera.mp4"),
    ("resolve", "fondos"), ("resolve", ""), ("resolve", "fondos/sub"), ("resolve", "fondos/no.png"),
    ("resolve", "fondos/notas.txt"), ("resolve", "suelto.mp4"), ("resolve", "."), ("resolve", "/"),
    ("resolve", "../MATERIAL/fondos/a.gif"),

    # Preferencias: activa, favorita y etiqueta (en collections_meta de los ajustes).
    ("meta",),
    ("prefs", "fondos", False, None, None), ("prefs", "alfa", None, True, "  Mi Alfa  "),
    ("prefs", "Zeta", None, True, None), ("prefs", " sonidos ", True, None, None), ("meta",),
    ("list",), ("search", "", "", "", 500), ("search", "", "", "fondos", 500), ("search", "one", "", "", 500),
    ("prefs", "alfa", None, None, ""), ("prefs", "alfa", True, False, None), ("prefs", "alfa", None, None, " B "),
    ("prefs", "no_existe", True, None, None), ("meta",), ("list",),
    ("prefs", "..", True, None, None), ("prefs", ".", True, None, None), ("prefs", "a/b", True, None, None),
    ("prefs", "a\\b", True, None, None), ("prefs", "", True, None, None), ("prefs", "C:x", True, None, None),
    ("prefs", "x/", True, None, None),
    ("save", {"tts": {"voice": "otra"}}), ("meta",),   # guardar otra cosa no las pierde

    # collections_meta raros (a mano en settings.json).
    ("settings", {"collections_meta": [1]}), ("list",), ("prefs", "alfa", None, True, None), ("meta",),
    ("settings", {"collections_meta": {"alfa": 5, "Zeta": None}}), ("list",),
    ("prefs", "alfa", True, None, None), ("prefs", "Zeta", True, None, None), ("meta",),
    ("settings", {"collections_meta": {"alfa": {"label": 5}}}), ("list",), ("search", "", "", "", 500),
    ("settings", {"collections_meta": {"alfa": {"label": True}}}), ("list",),
    ("settings", {"collections_meta": {"alfa": {"label": 0, "enabled": 0, "favorite": "sí"}}}), ("list",),
    ("settings", {"collections_meta": {"alfa": {"label": [], "enabled": [], "favorite": [0]}}}), ("list",),
    ("settings", {"collections_meta": {"alfa": {"label": "zz"}, "Zeta": {"label": "ÁRBOL"},
                                       "fondos": {"label": "árbol", "favorite": True},
                                       "sonidos": {"label": "Árbol", "favorite": True}}}), ("list",),
    ("settings", "{ roto"), ("list",), ("meta",),
    # Empate de (favorita, nombre): manda el orden de sorted(iterdir()), en minúsculas («a_» < «ab»),
    # y no el del disco (NTFS ordena en mayúsculas: «AB» < «A_»).
    ("files", [f"{M}/ab/x.png", f"{M}/a_/y.png", f"{M}/Ab_/z.png"]),
    ("settings", {"collections_meta": {"ab": {"label": "igual"}, "a_": {"label": "igual"},
                                       "Ab_": {"label": "Igual"}}}), ("list",),
    ("settings", {"collections_meta": {"ab": {"label": "igual", "favorite": True},
                                       "a_": {"label": "igual", "favorite": True}}}), ("list",),
    ("settings", {}),

    # La raíz de los ajustes (material_root).
    ("files", ["custom/col1/a.png", "custom/col1/b/c.MP4", "custom/col2/x.txt"]),
    ("settings", {"material_root": "{TMP}/custom"}), ("root",), ("list",), ("search", "", "", "", 500),
    ("resolve", "col1/b/c.MP4"), ("resolve", "../assets/material/fondos/a.gif"),
    ("settings", {"material_root": "{TMP}//custom/./"}), ("root",), ("list",),
    ("settings", {"material_root": "{TMP}\\custom\\"}), ("root",),
    ("settings", {"material_root": "{TMP}/no_existe"}), ("root",),              # no existe: la de fábrica
    ("settings", {"material_root": "{TMP}/custom/col1/a.png"}), ("root",),      # no es carpeta
    ("settings", {"material_root": ""}), ("root",),
    ("settings", {"material_root": None}), ("root",),
    ("settings", {}),
    ("set_root", "  {TMP}/custom  "), ("meta",), ("root",), ("list",),
    ("set_root", "{TMP}/no_existe"), ("set_root", "{TMP}/custom/col1/a.png"), ("meta",),
    ("set_root", "{TMP}\\custom\\col1\\"), ("meta",), ("root",),
    ("set_root", "  "), ("meta",),                                              # Path("") es «.»
    ("settings", {}),

    # Crear colecciones (en la de fábrica).
    ("create", "Nueva"), ("create", "Nueva"), ("create", "nueva"), ("create", 'a<b>:c"d/e\\f|g?h*i'),
    ("create", "  ..  "), ("create", "..."), ("create", " . x . "), ("create", "\x01tab\t"), ("create", ""),
    ("create", SIGMA), ("create", "alfa"), ("create", "dos  espacios"), ("create", "punto.final."),
    ("list",), ("tree", M),

    # Añadir archivos a una colección.
    ("add", "alfa", [["foto.PNG", "1"], ["foto.png", "2"], ["C:\\x\\y\\clip.mp4", "3"], ["../../evil.mp4", "4"],
                     ["a:b.mp3", "5"], ["notas.txt", "6"], [None, "7"], ["", "8"], ["  .mp4", "9"],
                     ["x.tar.gz", "10"], ["dos.puntos.gif", "11"], ["dos.puntos.gif", "12"], [5, "13"],
                     [" con espacio .webm ", "14"], ["x.mp4.", "15"], ["one.BMP", "16"], ["ok\x01.wav", "17"],
                     ["Ñandú.MP3", "ñ"], ["foto.png", "18"]]),
    ("tree", f"{M}/alfa"), ("scan", "alfa"),
    ("add", " alfa ", [["otro.gif", "x"]]), ("add", "no_existe", [["a.png", "1"]]), ("add", "..", []),
    ("add", "", []), ("add", "alfa/sub", [["a.png", "1"]]), ("add", "suelto.mp4", [["a.png", "1"]]),
    ("add", "alfa", []),
    # Un NUL en el id: el id vale, pero is_dir() da False (Windows cortaría la ruta en «alfa»).
    ("add", "alfa\x00x", [["nul.png", "1"]]), ("scan", "alfa\x00x"), ("search", "", "", "alfa\x00x", 500),
    ("resolve", "alfa/one.bmp\x00x"), ("resolve", "alfa\x00/one.bmp"), ("prefs", "alfa\x00x", True, None, None),
    ("tree", f"{M}/alfa"), ("settings", {}),

    # Sueltos.
    *[("kind", n) for n in ["x.mp4", "x.MP4", "x.gif", "x.GIF", "x.png", "x.jpeg", "x.webp", "x.bmp", "x.mp3",
                            "x.wma", "x.aiff", "x", "x.", ".mp4", "..mp4", "a/b.mov", "a.mp4/b", "x.ＭＰ４",
                            "x.mkv", "x.m4v", "x.mpg", "x.mpeg", "x.wmv", "x.flv", "x.avi", "x.webm", "x.jpg",
                            "x.m4a", "x.aac", "x.ogg", "x.flac", "x.wav", "C:x.gif", "x.tar.gz"]],
    *[("safe_id", t) for t in ["alfa", " alfa ", "", "  ", ".", "..", "a/b", "a\\b", "C:", "C:x", "x/", "/x",
                               "Ñandú", "a.b", " . ", "\\\\server\\share", "a\x00b"]],
    ("entry", "fondos", "sub/x.PNG"), ("entry", "c", "a\\b.mp4"), ("entry", "c", "x"), ("entry", "c", "./y.gif"),
    ("entry", "c", "a//b.MP3"), ("entry", "g r#ú%", "x y.mp4"), ("entry", "c", ".mp4"), ("entry", "c", ".."),
    ("entry", "", "x.png"), ("entry", "c", "Ñandú ü.mp3"), ("entry", "c", "a/./b/x.jpg"),
    ("match", {"name": "Alfa Beta", "collection": "Fondos"}, ""),
    ("match", {"name": "Alfa Beta", "collection": "Fondos"}, "alfa fondos"),
    ("match", {"name": "Alfa Beta", "collection": "Fondos"}, "beta  ALFA"),
    ("match", {"name": "Alfa Beta", "collection": "Fondos"}, "gamma"),
    ("match", {"name": "Alfa Beta", "collection": "Fondos"}, "a f"),
    ("match", {"name": "Alfa Beta", "collection": "Fondos"}, "  "),
    ("match", {"name": None, "collection": 5}, "none 5"),
    ("match", {}, " "), ("match", {"name": SIGMA}, FINAL), ("match", {"name": SIGMA}, MEDIAL),
]
