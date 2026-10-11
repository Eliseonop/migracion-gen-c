# TODO de la migración: video-yt → GenClipPro

> Lista de tareas con checks de **todas las fases** del [plan](PLAN_MIGRACION_CPP.md).
> El detalle de cada tanda (qué se hizo, cómo se verificó, cifras) está en el §14 del plan.
> Arquitectura: [ARQUITECTURA.md](ARQUITECTURA.md). Actualizado: **2026-10-10**.

Leyenda: `[x]` hecho · `[ ]` pendiente · *(parcial)* empezado pero no cerrado.

## Estado de un vistazo

| Fase | Contenido | Estado | Hito |
|---|---|---|---|
| **F0** Cimientos | toolchain, CMake, CI, tests | ✅ hecha salvo el CI en marcha (MSVC ya compila y pasa los tests) | |
| **F1** Modelo y formato | modelo, JSON, migraciones, fixtures | ✅ hecha: modelo, ajustes, biblioteca, SFX de disco, colecciones, personajes stick, almacén de proyectos y JSON como Python · dar de alta SFX llega con F5 | |
| **F2** Lógica de edición | ops, sin solapes, keyframes, undo | ✅ hecha: las 42 ops, el historial en disco, el autoguardado y la envolvente de un sonido (`clip_motion`) | |
| **F3** Media + export | gc_media, gc_render v1, `genclip render` | 🟡 evaluación hecha (por clip, capas y fotograma), cabeceras de imagen y sondas de medios · MSVC, vcpkg y FFmpeg listos; sigue el decode de `gc_media` | **M1** |
| **F4** Render completo | texto, fx, transiciones, chroma | ⬜ | |
| **F5** Editor de escritorio | app Qt Quick | ⬜ | **M2** |
| **F6** IA local | fondo, SAM, Whisper, TTS, tracking | ⬜ | |
| **F7** Nube, agente, MCP | providers, chat, MCP :8000, YouTube | ⬜ | |
| ~~**F8** Motion, Stick, Paper~~ | fuera de alcance por ahora (plan §6): Paper ya es el efecto `paper_cutout` (F4) | — | |
| **F9** Empaquetado escritorio | instaladores, `.exe` | ⬜ | **M3** |
| **F10** Móvil | Android + iOS | ⬜ | **M4** |

**Cifras actuales:** 112 tests (110 de Catch2 y 2 de la CLI) y 176 065 aserciones en verde con MinGW (Debug y Release) y con MSVC (Debug); con MSVC + vcpkg (FFmpeg), también los de `gc_media`: 114 tests y 176 481 aserciones. 0 warnings y 0 avisos de clang-tidy.
Paridad con video-yt en modelo (18 proyectos), edición (378 casos sintéticos + 1919 ops reales),
animación (236 casos + 733 clips reales), encuadre (2908 muestras sintéticas + 5875 de 235 clips reales)
historial (69 pasos sintéticos + el de tus 14 proyectos) ajustes (67 pasos + los tuyos, con las claves redactadas) biblioteca (55 pasos + 23 materiales + la tuya; guardar desde un proyecto, 47 pasos + 3 de tus materiales) SFX en disco (207 pasos + tu carpeta de 626 sonidos), colecciones de material (256 pasos + tu carpeta, 46 medios), personajes stick (245 pasos + tu `scientist_stick`), almacén de proyectos (317 pasos + una copia de tu `projects.json`, con el mismo texto), JSON como Python (5175 `repr`, `dumps` y `loads`), envolvente de un sonido (244 casos sintéticos + 16 018 pares de tus timelines y 60 RMS de tus audios), cabeceras de imagen (311 casos sintéticos + 1606 de tus imágenes), sondas de medios (23 sintéticas + 120 de tus medios) y evaluación por clip (41 clips con 984 muestras + 434 casos sueltos; 914 clips reales con 7122 muestras) capas de un instante (81 instantes sintéticos + 6675 de tus timelines) y fotograma (95 instantes sintéticos con 203 dibujos + 831 de tus timelines con 1692 dibujos), estos dos contra el JS real con Node.
Todo exacto bit a bit salvo lo que pasa por `pow()`, `sin()` o `cos()` de la libm: tolerancia de 1e-12 (4e-15 en la evaluación).

---

## Siguiente (para el autopiloto)

F0, F1 y F2 están hechas: lo que queda de F1 llega con la interfaz (F5). La evaluación de F3 también está completa (por clip, capas y fotograma). El Windows SDK (10.0.26100) ya está instalado y el remoto de GitHub existe.

1. **F3:** MSVC, vcpkg, FFmpeg y las sondas de `gc_media` están hechos (notas de F3). Sigue `gc_media`: demux y decode con seek exacto (primero por software; hwaccel después), con fixtures de fotogramas decodificados frente a los de ffmpeg.
2. **El CI:** subir los commits al remoto y ver que pasa en Windows, Linux y macOS (con el push que haga Edu).

## Pendiente por tu parte (Edu)

- [x] Instalar el **Windows SDK** (10.0.26100.0, con `um` y `ucrt`).
- [x] Primer commit hecho: un commit local por tanda verificada, sin push.
- [x] Crear el **remoto en GitHub** (`Eliseonop/migracion-gen-c`). Falta el primer push para que el CI se ejecute.
- [ ] Confirmar la **semántica de visibilidad y orden de capas** del render C++ (notas de F3): intervalo [inicio, fin) sin el margen de 0,02 s del preview, inicio acotado a 0, desempate por `start` dentro de una capa, solo clips que encajan en su pista y de más de 0,02 s, y fin con la duración redondeada de Python. Los textos se ordenan por la pila (el export los ordena por la lista en un .ass). Cambia algún fotograma de borde respecto al export actual. Mientras tanto se implementa así, con el modo del preview disponible.
- [ ] Elegir la **semántica del relleno** (notas de F3): `fit: contain` decidido en cada instante (preview) o solo si todos los keyframes lo son (export), y el doble encuadre. Tus clips no lo usan (todos son overlay).
- [ ] Elegir el **desplazamiento de `slide`** en overlay (notas de F3): el preview mueve el clip una fracción de SU tamaño (`fx.tx·ancho del destino`) y el export el cuadro entero (`overlay_xy_for_fx`). En un clip pequeño cambia mucho: ~335 px frente a 1280 px. Tres de tus clips lo usan. El C++ sigue al preview mientras tanto.
- [ ] *(Sugerencia)* Mover GenClipPro fuera de `C:`, porque ese SSD tiene bloques defectuosos.

---

## F0 — Cimientos

- [x] Repositorio git (`main`) con `.gitignore`, `.gitattributes` (git-lfs), `.clang-format` y `.editorconfig`
- [x] CMake ≥ 3.28 con presets: `mingw-*`, `msvc-*`, `msvc-vcpkg-*`, `linux-debug`, `macos-debug` y `ci`
- [x] Dependencias con `find_package` y, si no están, FetchContent con hash verificado (nlohmann/json 3.12.0, spdlog 1.15.3, Catch2 3.9.1)
- [x] `vcpkg.json` preparado para F3
- [x] Warnings estrictos (`gc_set_warnings`): 0 warnings con GCC 15
- [x] `-ffp-contract=off` para que los cálculos den los mismos bits en todas las plataformas
- [x] `genclip --version` con argumentos y salida en UTF-8 en Windows
- [x] CI de GitHub Actions escrito (Windows MSVC, Linux GCC, macOS Clang)
- [ ] CI ejecutándose (el remoto ya existe: falta el primer push). Ojo: las fixtures de disco (SFX y colecciones) llevan la semántica de `WindowsPath` (sin distinguir mayúsculas, «\» como separador, «C:x»), y las de proyectos guardan el texto de `projects.json` con los CRLF de Windows (`write_json_file` escribe LF fuera de Windows, como Python); en Linux y macOS habrá que saltarlas o adaptarlas
- [x] Toolchain MSVC operativo (`msvc-debug`): MSVC 14.50 de VS 2026 compila con 0 warnings y pasa todos los tests; `msvc-vcpkg-debug` configura con el vcpkg de `E:\vcpkg` (lo que hizo falta, en las notas de F3)
- [ ] `GC_WARNINGS_AS_ERRORS` activado en el CI de Windows
- [x] `.clang-tidy` y spdlog. `python tools/dev/tidy.py [--fix] [filtro]` pasa el clang-tidy de CLion por `src`, `apps` y `tests` y tiene que quedar en 0 avisos (lo que se desactiva y por qué está en el propio `.clang-tidy`). El registro es `gc_log` (`src/log`, spdlog 1.15.3 con hash verificado o el de vcpkg): `init`, el nivel de `GENCLIP_LOG` y salida a stderr. `genclip` lo usa para sus errores; `gc_core` no registra nada
- [ ] clang-tidy en el CI (`tidy.py` usa el clang-tidy y la MinGW de CLion; en el CI, el `run-clang-tidy` de cada plataforma)
- [ ] CI con ASan/UBSan (clang)

## F1 — Modelo y formato

- [x] `model.hpp`, espejo de `schemas.py`: Project, ClipInfo, Transcript, AudioInfo, ImageInfo, Timeline, TimelineTrack, TimelineClip, Reframe, Keyframe, TrackPoint, FaceTrack, Word y ProjectGroup
- [x] Valores por defecto idénticos a los de Pydantic
- [x] Coerciones *lax* de Pydantic v2, con errores que llevan la ruta (`timeline.clips[3].reframe.zoom`)
- [x] Migraciones v1→v5 y `resolve_text_role`
- [x] Escritura `Model` (= `model_dump()`) y `Preserve` (conserva claves desconocidas), con orden de claves estable
- [x] `tools/fixtures/export_fixtures.py`: 39 casos sintéticos + tus 18 proyectos reales y 2 carpetas
- [x] Round-trip sin pérdida del 100 % de los proyectos reales
- [x] `genclip projects` y `genclip normalize [--preserve]`; Python relee lo que escribe C++
- [x] Biblioteca de `data/library` (`library.hpp`): carga tolerante, tipo, origen y fuente deducidos, `material_dto` (con la URL codificada como `urllib.parse.quote`), lista ordenada por fecha, editar título y descripción, metadata semántica, proyectos que la usan y quitar (borra el archivo). Paridad: 55 pasos sobre un `data/` temporal (con los mensajes de error que ve el usuario), 23 materiales sueltos y tu `library.json`; 8/8 mutaciones
- [x] JSON como Python en `data/` (`io.hpp`): los dos lectores leen `NaN`, `Infinity`, `-Infinity` y los números que se desbordan (`1e999` → inf) como `json.loads` (antes, un archivo con ellos se leía como ilegible y la primera escritura lo vaciaba), y `write_json_file` escribe el texto de `json.dumps(ensure_ascii=False)` (`py_json_dumps`: reales con `repr`, `NaN`/`Infinity`, separadores de Python y CRLF en Windows, como `write_text`). `py_repr` es ahora `repr()` exacto. Paridad: `json_cases.py` con el CPython de video-yt (5175 `repr`, 10 `dumps` con todas las sangrías y 25 `loads`, 16 de error), sustitutos sueltos y anidamiento y pasos con NaN en proyectos, ajustes y SFX
- [x] «Guardar en la biblioteca» desde un proyecto (`LibraryFile::save_from_project`, espejo de `library.save_from_project`, con `storage.safe_name`, `storage.resolve_media` y `segments.is_shared_file`): copia el archivo a `data/library/<tipo>/<título>_<id><ext>`, lo da de alta, apunta la timeline al ítem nuevo, lo quita del proyecto y borra el original (no si es un segmento que comparte archivo); si algo falla tras copiar, deshace el alta y la copia como Python. Paridad: 47 pasos (13 de error, 11 con el mensaje que ve el usuario) comparando el resultado, `library.json`, `projects.json` y el disco tras cada paso, y una copia de tus proyectos (un clip, un audio y una imagen); 25/25 mutaciones (`tools/dev/mutantes.py library`)
- [x] Almacén de proyectos (`projects_store.hpp`, espejo de `projects.py` y de lo que usa de `storage.py`): `projects.json` en crudo (se conservan las claves desconocidas y su orden, y el texto escrito es el de Python), proyectos y carpetas del inicio (crear, renombrar, mover, borrar), duplicar con sus archivos (sin `exports/`, con las URLs al id nuevo), carpeta en disco, materiales (`add_clips` con upsert por index, editar, quitar, manifest), timeline, composiciones de Motion, guiones y `retarget_timeline_asset`. Paridad: 317 pasos sobre un `projects.json` temporal (77 de error, con los mensajes que ve el usuario) y una copia de tu `projects.json`; 41/41 mutaciones (`tools/dev/mutantes.py projects`)
- [x] Colecciones de material (`collections.hpp`, espejo de `collections.py`): raíz (`material_root` o la de fábrica), preferencias en `collections_meta`, lista (favoritas primero, portada, `stick.json`), búsqueda por palabras, tipo y colección con cuentas y `items[:limit]`, crear y añadir archivos con los nombres saneados de Python, y `resolve` sin salir de la raíz. Paridad: 256 pasos sobre una carpeta temporal (36 de error, con los mensajes que ve el usuario) y tu carpeta (`scientist_stick`, 46 medios); 41/42 mutaciones (`tools/dev/mutantes.py collections`; la otra es equivalente, y las dos que quitan la protección del NUL cuelgan los tests: cuentan como detectadas)
- [x] Biblioteca de SFX desde disco (`sfx_disk.hpp`, la parte de lectura de `sfx.py`): carpeta activa (`sfx_folder` o la de fábrica), `sfx_library.json` con sus fallos (los sonidos salen del escaneo y las categorías leídas se quedan), escaneo en el orden de `sorted()` de `WindowsPath`, búsqueda por categoría, favoritos y palabras, `resolve` sin salir de la carpeta y `load_sfx_library` (lo que usa `sound_design`, con la sonda de duración inyectada) → `set_sfx_library`. Paridad: 207 pasos sobre una carpeta temporal y tu carpeta (626 sonidos, 15 categorías, 21 favoritos); 34/35 mutaciones (`tools/dev/mutantes.py sfx`; la otra es equivalente)
- [ ] Dar de alta, editar y crear categorías de SFX (`add_sound`, `update_sound`, `create_category`): escriben archivos y el índice, y llegan con la pantalla de la biblioteca de SFX (F5)
- [x] Personajes stick (`stick_library.hpp`, espejo de `stick_library.py`): lista (por `order` y nombre), detalle agrupado por expresión (del manifiesto, de la subcarpeta o «otros»; las categorías propias por subcarpeta), crear (carpeta con slug, subcarpetas por expresión y `stick.json`), añadir archivos a una expresión y cambiar la expresión sin mover el archivo. Paridad: 245 pasos sobre una carpeta temporal (54 de error, con los mensajes que ve el usuario), con el texto exacto del `stick.json` escrito, `str.title()` y el plegado NFKD sobre rangos enteros de Unicode, y tu `scientist_stick`; 57/58 mutaciones (`tools/dev/mutantes.py stick`; la otra es equivalente)
- [x] Ajustes y API keys (`settings.hpp`: `settings.py`, `export_settings.py` y `transcribe_settings.py`): carga con defectos, `save` con la fusión de claves y máscaras, varias claves por proveedor y la vista pública sin secretos. Paridad: 67 pasos sintéticos con claves falsas y tus ajustes con las claves redactadas; 10/10 mutaciones

### Notas de F1

- **«Guardar en la biblioteca» (`LibraryFile::save_from_project`, hecho):**
  - **Orden de Python:** tipo, proyecto, material (clip por `index`, audio e imagen por `id`), archivo (`resolve_media`: estrictamente dentro de la carpeta del proyecto, que puede ser la propia o `<clips>/<id>`; la propia carpeta no vale) y, ya con el archivo, el id (`lib_` + 12 hex), la copia y el alta. Cada «no encontrado» lleva su mensaje.
  - **`base / kind / filename` de pathlib** (`pathlib_join`, en `disk_scan`): una ruta absoluta o con otra unidad sustituye a la base, una raíz sin unidad (`/c.mp4`) se queda la unidad de la base y la misma unidad sin raíz (`C:c.mp4`) se añade. La primera versión unía el texto «kind/filename» y con `/c9.mp4` daba por buena `video/c9.mp4` y la BORRABA. Un nombre de dispositivo de Windows (`NUL`, `CON`, `COM1`…, también con espacios o puntos detrás) se rechaza: Python falla con OSError y C++ lo daba por un archivo que existe.
  - **Errores al deshacer:** Python sale con el AttributeError o el TypeError del propio `except` (un 500 en la API) y C++ con NotFound o Invalid; el estado que queda es el mismo. Importa cuando F7 sirva la API.
  - **Nombre:** `safe_name(label or stem)`: quita `<>:"/\|?*`, saltos y tabuladores antes de juntar los espacios (`muy\nlargo` → `muylargo`), corta a 60 caracteres y, si no queda nada, «video». La extensión es la del archivo o `.mp4`, `.m4a` y `.png`. Windows quita el punto final de un nombre («…_id.» queda «…_id» en disco, igual en los dos).
  - **`library_root()` crea `audio/`, `video/` e `image/`** de la biblioteca aunque solo se use una.
  - **Duración:** la del material o, si no tiene y hay `start` y `end`, `float(end) - float(start)` (NaN e infinitos incluidos).
  - **Segmento compartido** (`is_shared_file`: otro clip con el mismo archivo, sin contar el propio por `index` ni por `id`): el archivo no se borra y la timeline se empareja solo por id.
  - **Deshacer:** si falla el alta, el `retarget` o `remove_material` (que con un audio con una clave `index` no lo encuentra), se quita el alta y se borra la copia; lo que ya cambió en la timeline se queda, como en Python. Y como Python, si `items` no se puede recorrer al deshacer (`"x"`), el error sale de ahí y la copia se queda; con `{}` se guarda `[]`.
- **Almacén de proyectos (`projects_store.hpp`, hecho):**
  - **En crudo, como Python:** cada operación lee `projects.json`, cambia el JSON y lo escribe entero; lo que devuelve se valida después. Si la validación falla, el archivo ya está escrito (pasa con `save_timeline` y una timeline mala). `list`, `get`, `move`, `rename`, `duplicate` y `save_motion_composition` validan con la preparación de `_project_from_dict` (ids de clip desde `index`, timeline migrada); `set_folder`, `apply_manifest` y `save_timeline` hacen `Project(**p)`, sin preparar (`project_from_json_as_is`).
  - **El texto escrito es el de Python** (`json.dumps(indent=2, ensure_ascii=False)` sin salto de línea final). `write_json_file` añadía un «\n» que no pone ningún archivo de video-yt; ya no lo pone (lo usan también los ajustes y la biblioteca).
  - **El texto escrito, igual que el de Python** (tanda «JSON como Python»): `py_json_dumps` reproduce `json.dumps(ensure_ascii=False)` (reales con `repr`, que nlohmann no hacía: con Grisu2, 375 de 300 000 dobles salían con otra última cifra y de 1e15 a 1e16 salía `1e+15` en vez de `1000000000000000.0`; `NaN`/`Infinity`; separadores) y `write_json_file` pone CRLF en Windows, como `write_text`. `duplicate_project` también pasa por `py_json_dumps`/`py_json_loads` (con el `dump` de nlohmann los NaN de la copia salían `null`).
  - **Divergencias que se quedan al leer:** un entero de más de 64 bits pasa a real (y uno de más de 308 cifras, a infinito); un sustituto suelto de UTF-16 (`\ud800`) pasa a U+FFFD (Python lo lee, pero no podría volver a escribirlo en UTF-8; sin el cambio, nlohmann daría el archivo entero por ilegible). Al escribir: más de 1500 niveles de anidamiento son un error (Python los escribe; la recursión agotaría la pila). En la biblioteca, un `saved_at` numérico NaN ordena distinto (el orden de Python sale de su timsort con comparaciones incoherentes); aquí los NaN van al final.
  - **Se reescribe aunque no cambie nada:** `delete_project`, `add_clips`, `add_transcript`, `add_audio` y `add_image` guardan siempre, aunque el proyecto no exista, como Python.
  - **Orden de ids y horas:** `create_group` pide el id antes de mirar el nombre (con un nombre vacío el id se gasta); `add_clips` toma la hora antes de cargar; `create_project` toma id y hora antes de cargar.
  - **Comparaciones de Python (`py_equal`):** `e.get("index") == d["index"]`, el `file` del manifest y los `id` usan `==` de Python (`True == 1 == 1.0`, exacto entre int y float); el manifest empareja por `file` como un dict (gana el último material con el mismo `file`; un `file` que es lista u objeto es un error, unhashable).
  - **Duplicar:** los archivos van a `<clips>/<id nuevo>` aunque el original tenga carpeta propia; `exports` se salta en cualquier nivel y sin distinguir mayúsculas (`ignore_patterns` usa `fnmatch`, que en Windows pasa a minúsculas); `motion/` sale siempre de la carpeta por defecto. Las URLs `/api/media/<pid>/` y `/api/projects/<pid>/` se cambian en el texto JSON (también en claves) y la copia va justo delante del original.
  - **Forma rara del archivo:** sin «projects», «projects» que no es lista (salvo vacía), proyectos sin «id», «groups» null o listas de materiales null fallan donde Python lanza KeyError, TypeError o AttributeError. Divergencia que se queda: un proyecto escrito como lista de pares (`dict(p)` lo acepta en Python) es un error.
- **Colecciones (`collections.hpp`, hecho):**
  - **Compartido con las SFX (`disk_scan.hpp`):** la carpeta de los ajustes (`dir_from_settings`), el recorrido de `sorted(base.rglob("*"))` (`sorted_files`) y el `resolve` que no deja salir de la carpeta (`resolve_inside`). `sfx_disk` usa lo mismo, sin cambiar nada.
  - **Orden:** las carpetas de la raíz, por `py_lower` del nombre (`sorted(iterdir())`); la lista, por `(not favorite, name.lower())`; la búsqueda recorre `sorted(enabled)`, que es orden de puntos de código, el mismo de los bytes UTF-8 de un `std::set<std::string>`.
  - **Nombres saneados** (`create_collection` y `add_files`): `[<>:"/\|?*\x00-\x1f]` → «_» y `strip(" .")`. Son ASCII, así que se cambian byte a byte. Un nombre que ya existe (sin distinguir mayúsculas, como el disco de Windows) lleva « 2», « 3»… en las colecciones y «_2», «_3»… antes de la extensión en los archivos.
  - **`items[:limit]`** de Python: con un límite negativo quita los últimos (`-2` deja todos menos dos).
  - **NUL en el id:** `_safe_collection_id("a\0b")` lo acepta, pero `is_dir()` da False, así que `add_files` responde «Carpeta no encontrada.» y `_scan_dir` no da nada. En C++ hay que comprobarlo antes de tocar el disco: Windows cortaría la ruta en el NUL y escribiría en `a`.
  - **Divergencias que se quedan:** un `material_root` que no es texto se ignora (Python falla en `Path(x)`, como con `sfx_folder`); unas preferencias de `collections_meta[id]` que no son un objeto son un error en `set_prefs` (Python hace `dict(x)`, que acepta una lista de pares); los nombres de carpeta que no son UTF-16 válido se saltan.
  - **`resolve` con nombres que Windows normaliza** (también en `sfx_resolve`, por `resolve_inside`): con «fondos/NUL» Python da None (el dispositivo queda fuera de la raíz) y C++ devuelve la ruta; con «a.png » o «a.png.» los dos encuentran el archivo, pero C++ devuelve la ruta como se pidió y Python la normalizada. CON, AUX, COM1 y LPT1 dan None en los dos. Hoy no importa; cuando F7 sirva `/api/collections/file/{ref}`, que rechace esos nombres antes.
  - **`add_files` en C++** recibe, por cada archivo, el nombre y quien escribe el contenido (`IncomingFile::write`): la API lo copiará de una subida y el escritorio, de un archivo arrastrado.
- **Personajes stick (`stick_library.hpp`, hecho):**
  - **Texto de Python nuevo en `py.hpp`** (tablas de `tools/gen/gen_py_text.py`, generadas con el Python de video-yt): `py_title` (`str.title()`: título tras un carácter sin caja y minúsculas tras uno con caja, con la sigma final), `py_isdecimal` (el `\d` de las regex de texto) y `py_nfkd_fold` (NFKD sin marcas, en minúsculas y lo que no es `[a-z0-9]` a «_»: la base de `_norm` y `_slug`; basta con cada carácter por separado porque NFKD solo reordena marcas, que se quitan).
  - **`_pretty_title`:** la regex `_(?:\d{3,4}p|\d{8,})(?=_|$)` a mano, con `\d` de Unicode y el `$` que también vale antes de un «\n» final.
  - **Ids de expresión que no son texto** (un manifiesto a mano): como claves de Python (`True == 1 == 1.0`, se queda la primera; una lista o un dict es un error, unhashable; los NaN de `json.loads` son el mismo objeto). Al ordenar, un empate de posición entre texto y números falla como `<` de Python, y un número sin etiqueta falla en `.replace` (AttributeError). Divergencia: un id NaN es un error (Python lo deja donde caiga su timsort) y un `order` de más de 64 bits también.
  - **El orden de Python en los fallos:** `create_stick` crea la carpeta y sus subcarpetas antes de mirar los demás sticks (si uno tiene un `order` que no es entero, falla y la carpeta se queda); `add_files` crea la carpeta de la expresión aunque luego no guarde nada; `set_expression` reescribe `items` solo con los dicts y, si la expresión es una categoría propia, `expressions` con las que valen (o las de fábrica) más la nueva.
  - **Compartido con las colecciones (`disk_scan`):** `sorted_subdirs` (el `sorted(iterdir())` de carpetas), `clean_name` (los nombres saneados) y `resolved_inside` (el `in path.parents` de `set_expression`).

- **JSON como Python:** `json.loads(read_text("utf-8"))` falla con un BOM de UTF-8. Ajustes, biblioteca, historial, SFX, colecciones y el almacén de proyectos usan `read_json_file_py`, que también falla; `read_json_file` (el CLI y las fixtures) lo sigue saltando.
- **Texto de Python en `py.hpp`:** `py_lower` (`str.lower()` de Unicode 16, con la sigma final; tabla generada con `tools/gen/gen_py_lower.py`), `py_path_str` (`str(Path(x))`), `py_path_name`, `py_path_suffix` y `py_path_stem` (Python 3.14 en Windows, con el `splitroot` de C: unidades de UTF-16, UNC y `\\?\UNC\`) y `py_quote`.
- **JSON roto y JSON de Python:** un NUL en el texto (nlohmann lo tomaba por el final) es un error de lectura, como en Python. `NaN`, `Infinity`, `-Infinity` y los números que se desbordan (`1e999`, que antes cerraba la app con `out_of_range` y después daba error) se leen como Python: nlohmann no los acepta, así que fuera de los textos pasan a un texto marcador (con NUL escapado) y, ya leído, al real; si salen más marcadores de los puestos, algún texto de verdad coincidía y se repite con otros. Como clave (`{NaN: 1}`) no se cambian y son un error, como en Python. Por abajo (`1e-999`) nlohmann ya da 0.0.
- **SFX en disco (`sfx_disk.hpp`):**
  - **Orden del escaneo:** `sorted(base.rglob("*"))` compara las partes de la ruta con `str.lower()`; `sfx_scan` ordena por `py_lower` de cada parte (la barra no es «cased» ni «case-ignorable», así que la sigma final da lo mismo por partes). Ejemplos medidos: `a/z.mp3` < `a.mp3` < `A.WAV`, `iz.mp3` < `İx.mp3`, `Z.flac` < `ß.mp3` < `é.m4a`.
  - **Fallos del índice:** cualquier excepción al leer `sfx_library.json` (BOM, UTF-8 inválido, no es un dict, una categoría o un sonido con otro tipo, un nombre de archivo que no es texto) deja las categorías leídas y saca los sonidos del escaneo. Con claves repetidas gana el último valor en la posición del primero, como en Python.
  - **`load_sfx_library`:** sondea la duración de todos los sonidos al cargar (Python solo la de los emparejados, al proponer). La app tiene que cargarla en segundo plano; la sonda (ffprobe en Python) se inyecta y llega con `gc_media`.
  - **Divergencias que se quedan:** un `sfx_folder` que no es texto (Python falla; aquí se ignora); `resolve` no sigue enlaces simbólicos en Windows (libstdc++), así que un enlace dentro de la carpeta que apunte fuera se acepta (Python lo rechaza) y devuelve la ruta con las mayúsculas pedidas (Python, las del disco); los nombres que no son UTF-16 válido se saltan (Python fallaría al codificar la URL).

## F2 — Lógica de edición *(hecha)*

### Arquitectura del motor
- [x] Documento inmutable con estructura compartida (`Cow<T>`)
- [x] Comandos como datos (`std::variant`) → `apply_command` (op pura → *settle* → validación de errores nuevos)
- [x] `EditSession`: snapshots, deshacer/rehacer (límite 50), checkpoints, `Group` (un paso de deshacer), observadores y revisión
- [x] Ids inyectables (`IdSource`; `SequentialIdSource` = secuencia de las fixtures)
- [x] Resultado de cada op con `changed` y `warnings` (API de ops con diff)
- [x] Semántica de Python donde la paridad lo exige (`round`, `repr`/`str` de cualquier valor, `strip`, cortes, `float()`/`int()`, `min`/`max` que conservan el tipo, `%` de reales, `Path.stem` y la clase de palabra de `re`)

### Reglas y matemática
- [x] `timeline_rules`: tipos de clip ↔ pista, velocidad y duración, pila de capas (`display_order`, `reorder_tracks`) y validación
- [x] `no_overlap`: empujar si crece, subir de pista si cae encima, pista magnética
- [x] `keyframes`: curvas (ease, cúbicas, rebote, bézier, *hold*), interpolación por propiedad, colores, estilo de texto animado, `upsert_keyframe_at` y fundidos de volumen
- [x] `masks`: normalizar y evaluar máscaras animadas
- [x] `fx_catalog` (recurso `resources/fx/catalog.json`), `pose` (`clip_pose`, `frame_at`), `shapes`, `bg` y `captions`
- [x] `clip_layout`: `crop_window`, `source_point_to_output`, `source_crop_px`, `dest_rect[_even]` y volteo, con fixtures de layout (las funciones que solo usa la vista previa de `clipLayout.js` van con F5)
- [x] `text_metrics` (tamaño de letra, interlineado y reparto en líneas como `text_ass._rows`; ancho de texto inyectable, estimado hasta F4), `sfx_library` (biblioteca inyectable, `norm`, `score_sfx` y `match_sounds`) y el motor de «Eliminar fondo» recomendado (`bg`)
- [x] `clip_motion`: los 13 presets (y sus alias), envolvente de audio (corte tras el pico) y fusión con los keyframes que había
- [x] Lo que faltaba de `clip_motion` (la envolvente de un sonido que usa `animate_clip` con `follow_audio_id`): `downsample_envelope`, `normalize_amps`, `overlap_window`, `audio_src_at` y `rms_envelope`, el núcleo de `sample_rms_envelope` con las muestras mono a 400 Hz inyectadas (decodificarlas es de `gc_media`, F3). Paridad (`tests/fixtures/motion`): 108 `downsample` (10 de error), 20 `normalize`, 85 pares visual/audio sintéticos con 1228 instantes, 31 RMS con un ffmpeg falso (11 553 muestras); y tus 18 timelines (16 018 pares, 2637 con solape, y 34 673 instantes) y 60 RMS de tus audios con las muestras del ffmpeg real (192 079 muestras). Exacto, bit a bit; 22/22 mutaciones (`tools/dev/mutantes.py motion`)
- [x] `follow`: `follow_keys` (del recorrido de un objeto a keyframes de pose del clip que lo acompaña); el seguimiento en sí es de F6
- [x] `clip_attrs`: grupos de atributos, `paste_attributes` y la fusión de keyframes (`merge_keyframes`: restringir, constantes y remuestreo)

### Comandos: 45 hechos (las 42 ops de Python, más 3 setters propios)
- [x] **Pistas:** `add_track`, `reorder_track`, `rename_track`, `remove_track`, `link_tracks` y `unlink_track`
- [x] **Clips:** `add_clip`, `move_clip`, `remove_clip`, `split_clip` y `duplicate_clip`
- [x] **Proyecto:** `set_project_format` y `set_markers`
- [x] **Propiedades:** `update_clip`, `set_clip_opacity`, `set_clip_speed` (incluye `reverse`), `set_clip_transition`, `set_text_role`, `set_clip_blend`, `set_clip_flip`, `set_clip_layout` y `set_clip_note` (desactivar va por `update_clip`)
- [x] **Encuadre:** `reframe_clip` y `crop_clip`
- [x] **Animación y efectos:** `set_clip_keyframes`, `set_clip_beats`, `set_clip_masks`, `set_clip_effects`, `apply_effect` y `set_clip_animation`
- [x] **Audio:** `set_clip_volume`, `set_track_audio` y `set_clip_audio_fx`
- [x] **Material nuevo:** `replace_media`, `freeze_frame`, `add_subtitles`, `add_shape`, `add_cinema_bars`, `add_adjustment_layer` y `set_clip_bg_removal`
- [x] **Compuestos:** `paste_clip_attributes`, `follow_object`, `add_sound_design`, `animate_clip` y `apply_recipe` (las 5 recetas)

### Notas de la 4ª tanda (hecha)
- **Orden:** ~~`clip_layout`~~ → ~~`clip_attrs`~~ → ~~`follow_object`~~ → ~~`add_sound_design`~~ → ~~`animate_clip`~~ → ~~recetas~~: las 42 ops, hechas. Queda la persistencia del historial.
- **`interp_items`:** ya es pública en `keyframes.hpp`, con el `_merge` de Python (un NaN del respaldo vale lo de un clip vacío). `clip_props_over` = `clip_props_at` con otra base (la usa `crop_window`).
- **`sin`/`cos`:** la libm de MinGW difiere de la de CPython en el último bit. Los puntos de `source_point_to_output` con giro (y lo que los use, como `follow_object`) se comparan con tolerancia de 1e-12; sin giro salen exactos. `follow_object` redondea después (t a 4 decimales; x, y y scale a 5; rotation a 3) y sale exacto en todas las fixtures.
- **Encuadre real:** los 235 clips de vídeo e imagen de tus timelines son `overlay`. El modo relleno solo lo cubren los casos sintéticos.
- **`clip_attrs._constant_props`** recorre un `set` de Python, así que el orden de esas claves cambia en cada ejecución. El exportador sustituye esa función por la misma en orden de aparición (que es lo que hace el C++), así que la fixture es estable y se compara con orden.
- **Orden de los uuid en `paste_attributes`:** los grupos van en el orden de `ATTR_GROUP_IDS`. Primero crop (un `k…` por keyframe de `keyframes` y de `keyframes2`; el id se descarta al validar, pero el uuid se gasta), luego vfx (`fx` + 10 hex), luego mask (`m…`) y al final un `k…` por item de `merge_keyframes`.
- **`follow_object`:** `track`, `dims`, `box_w` y `anchor_t` se validan al empezar (si el recorrido no coincide, Python falla igual), pero `min_gap` y los `cx`/`cy`/`s`/`rot` de cada punto solo cuando se usan, como en Python. Los items del clip que acompaña se quitan en [primera t − 0,06, última t + 0,06], ambos incluidos.
- **Envolvente de un sonido (`clip_motion`, hecho):** la usa la herramienta MCP `animate_clip` con `follow_audio_id` (`_follow_audio_envelope` de `tools_edit.py`, que va con F7); la op `animate_clip` recibe la envolvente ya hecha.
  - **`sum()` de Python ≥ 3.12 es compensado** (Neumaier): `sum(x * x for x in chunk)` NO es la suma ingenua. `rms_envelope` repite el algoritmo de CPython (`0 + x0`, compensación `c` y `f + c` solo si `c` es finita y distinta de 0); medido: 0 diferencias en 20 000 sumas aleatorias, y la suma ingenua cambia el último bit.
  - **`downsample_envelope`:** índice `round(i·(n−1)/(max_n−1))` al par; con `max_n` negativo, vacío; con `max_n = 1` y más de un punto, ZeroDivisionError (en C++, error).
  - **`normalize_amps`:** el `max()` de Python se queda el primero (un NaN delante es el pico y todo sale 0; uno detrás no gana); pico 0 o −0,0 → 1.
  - **`audio_src_at` y `overlap_window`:** `_num` deja un NaN en 0; con `t` NaN, `max(0.0, NaN)` es 0.
  - **RMS:** `hop = max(1, int(400 / max(1, rate)))`; con menos de 2 muestras, `[(0, 0), (round(dur, 3), 0)]` sin normalizar ni reducir; si el último punto queda antes de `dur − 0,02`, se repite su amplitud en `round(dur, 3)`.
- **Mutantes equivalentes de `clip_attrs`:** fusionar con el destino ya pegado (ningún grupo toca `keyframes`) y `keep_pitch` con la verdad de Python (Pydantic ya lo deja en `bool`).
- **Recetas (hechas):**
  - `text_reflection` mide texto (`text_ass._rows` → Pillow). Se inyecta: el exportador parchea `text_ass.text_width` con la estimación `len·em·0,55 + ls·len`, que es la que usa `gc_core` por defecto (`set_text_width`). La medida real llega en F4 (FreeType).
  - `subject_pop` lee la biblioteca de SFX y hace `ffprobe`. También se inyecta: cada caso puede llevar su `sfx_library` ({items, durations}), y el exportador parchea `sfx.search`, `sfx.resolve` y `sound_design.probe_duration`. El test carga la misma biblioteca con `set_sfx_library`.
  - El `_insert_track` de las recetas (justo encima de una pista dada o de la última de su tipo) no es el `insert_track` de la pila de capas.
  - `recipe_layout.py` no es de `apply_recipe`: es geometría del render (corte doble, `vstack`/`hstack`) y va con F3.
- **`sound_design.norm`** (hecho en `sfx_library`): NFKD + quitar lo no ASCII, con la tabla generada desde `unicodedata` (`tools/gen/gen_nfkd_ascii.py`, Unicode 16, 2166 caracteres). `add_sound_design` no lee disco ni la biblioteca: todo llega en `sounds`.
- **`_SFX_TRACK_RE` (`^sfx\b`, sin distinguir mayúsculas, hecho):** «SFX_1», «sfx1», «SFXé» y «SFX½» no casan; «Sfx-x», «SFX—…», «SFX» + NBSP y «ſfx» (la ſ vale por s con `re.I`) sí. Fuera de Latin-1, `py_word_char` aproxima por bloques.
- **`Path(id).stem` (Python 3.14 en Windows, hecho en `py_path_stem`):** «x.» → «x»; «.hidden» y «..» se quedan igual; «x..y» → «x.»; «C:foo.wav» → «foo»; «a:b:c.wav» → «b:c»; rutas UNC sin archivo → «».
- **Matemática:** Python redondea con `round()` al par (`_even`, `_even_pos`), su `%` de reales es de suelo (rotación en `follow_keys`) y `degrees = x·(180/π)`.

### Persistencia
- [x] Guardar y cargar el historial (`history_store.hpp`: `data/history/<pid>.json`, el mismo formato que `timeline_history.py`, con escritura atómica). Se lee tolerante: las timelines antiguas se migran y las entradas ilegibles se descartan
- [x] Autoguardado (`Autosave`): guarda la timeline tras cada cambio y el historial cada vez que cambia (también con un checkpoint o al cerrar un grupo). Escribe en el momento; el aplazamiento (*debounce*) y el hilo de fondo son cosa de F5
- [x] `EditSession` retoma una sesión con su historial guardado y avisa de los cambios del historial (`subscribe_history`)

### Verificación
- [x] Fixtures de edición generadas desde el `apply_op` REAL de video-yt (`uuid4` determinista)
- [x] 378 casos sintéticos (156 de error) y 1919 operaciones sobre tus 18 timelines reales
- [x] Animación: 236 casos sintéticos y 733 clips reales (bit a bit salvo 1 `pow()` → tolerancia 1e-12)
- [x] Pruebas de mutación: detectan 1e-9, colores, `1` ↔ `1.0` y el orden de claves
- [x] Encuadre: 2908 muestras sintéticas (48 de error) y 5875 de 235 clips reales. Exacto; solo los puntos con giro van con tolerancia de 1e-12
- [x] Pegar atributos: 31 casos sintéticos (9 de error, con remuestreo, constantes de los dos lados y estilo de figura) y 77 operaciones reales; las mutaciones no equivalentes se detectan
- [x] Seguir un objeto: 27 casos sintéticos (17 de error; el giro baja de −180) y 81 operaciones reales; se detectan las 10 mutaciones
- [x] Sonorizar: 19 casos sintéticos (10 de error) y 47 operaciones reales; se detectan las 13 mutaciones
- [x] Animar con un preset: 43 casos sintéticos (8 de error) y 188 operaciones reales; se detectan las 13 mutaciones
- [x] Recetas: 30 casos sintéticos (12 de error) y 74 operaciones reales; se detectan las 16 mutaciones
- [x] `json_diff` distingue 0.0 de −0.0 (como el JSON de Python)
- [x] Benchmark: ≈ 90 µs por edición en C++ frente a ≈ 49 ms en Python (timeline de 167 clips)
- [x] Historial: los mismos 69 pasos que `timeline_history` (pilas, límite de 50, checkpoints, restaurar), el JSON en disco idéntico al de Python, y el historial de tus 14 proyectos (1497 entradas en v4 se migran). Mutación: 8/8
- [x] **Salida de F2:** 100 % de paridad con las 42 ops, con las fixtures de layout y con el historial. Compilación limpia: 51 tests y 0 warnings en Debug y Release

## F3 — Media + export C++ → **M1**

- [ ] Prerrequisito: MSVC + vcpkg con FFmpeg, Skia y ONNX Runtime *(parcial)*
  - [x] MSVC 14.50 (`msvc-debug`): compila con 0 warnings y pasa todos los tests
  - [x] vcpkg en `E:\vcpkg` con `builtin-baseline` fijado; `msvc-vcpkg-debug` configura y compila
  - [x] FFmpeg 9.0.2 (feature `media` de `vcpkg.json`, LGPL con zlib, nvcodec, amf y qsv): construido por vcpkg (unos 10 minutos); `find_package(FFMPEG)` lo encuentra y se compila `gc_media`
  - [ ] Skia (para `gc_render`)
  - [ ] ONNX Runtime (llega con F6)
- [x] **Cabeceras de imagen** (`image_probe.hpp`, espejo de `images.probe_gif` y `_ext_from_magic`): fotogramas, retardos, dimensiones, transparencia y repetición de un GIF leyendo sus bloques, y la extensión por la firma. Paridad (`tests/fixtures/media`): 290 GIF sintéticos (a mano, de Pillow y todos los truncados de dos de ellos) y 21 firmas; y tus 1606 imágenes (la firma de todas; el único GIF, entero). 14/14 mutaciones (`tools/dev/mutantes.py media`)
- [x] **Evaluación:** timeline en el instante *t* → lista de capas (usa keyframes, pose, máscaras y layout)
  - [x] Por clip: animaciones de catálogo (`fx_anims`: `clip_animations`, `animation_delta`, `render_pose`, `anim_opacity` y `animation_sample_times`), aparición y salida (`clip_fx`: `clip_fx_at`, `fx_windows`), modo de fusión (`blend`) y ajustes de color (`clip_adjust`: matriz 3×3, `has_color_adjust`, `strip_color` y las pasadas base + fantasma de la máscara de ajuste). Paridad: 41 clips sintéticos en 24 instantes (uno es NaN), 240 puntos de curva, 150 de pista, 10 ventanas, 16 efectos y 18 modos de fusión sueltos, y tus 914 clips en 7122 instantes; 34/35 mutaciones con `tools/dev/mutantes.py eval` (la otra es equivalente)
  - [x] Capas de un instante: `stack_layers` y `buried_text_track_ids` (`timeline_rules`) y `layers_at`/`videos_at` (`layers.hpp`) con dos reglas, `kRenderLayers` (la del render C++) y `kPreviewLayers` (el `layersAt` literal del preview). Paridad contra el JS real ejecutado con Node: 81 instantes en 13 timelines sintéticas y 6675 en tus 18 timelines, sin descartes (a Node le llega un gemelo de cada clip con la duración de Python); las reglas del render, con 10 tests propios. 20/20 mutaciones (`tools/dev/mutantes.py layers`)
  - [x] Fotograma ensamblado (`frame.hpp`: `evaluate(timeline, t, FrameSpec, dimensiones del material)`): por capa y pasada, tiempo local y de fuente, pose, aparición/salida, opacidad, fusión, volteo y máscaras, y para vídeo, imagen y motion los «blits» (rectángulo de la fuente + transformación afín al cuadro, en el mismo orden de operaciones que el contexto 2D), con overlay, relleno (cover/contain) y doble encuadre, y la cortinilla. Paridad contra el `drawComposite` real ejecutado con Node sobre un contexto que graba: 10 timelines sintéticas (95 instantes, 203 dibujos) y tus 18 (831 instantes, 1692 dibujos; 22 fuera por el `clipDur` de JS); 7 tests de las reglas del render y los errores; 21/21 mutaciones (`tools/dev/mutantes.py frame`)
- [x] `recipe_layout.py`: `split_orientation_for` y `contain_dest` están en `frame.cpp` (doble encuadre y `contain` del relleno). Los filtros `vstack`/`hstack` son cadenas de FFmpeg: con Skia no hacen falta. `dual_slot_wh` da las ranuras en enteros (`W`, `H//2`) sobre dimensiones pares; `evaluate` usa las de salida tal cual, así que quien exporte las pasa pares (`FrameSpec`)
- [x] Comprobar las dimensiones antes de llamar a `clip_layout`: `evaluate` falla si las de salida no son finitas y mayores que 0 y, con las reglas del render, si las de un material tampoco (Python lanza ZeroDivisionError); con las del preview esa capa va sin blits, como `drawComposite`
- [x] **Sondas de medios** (`gc_media`, `probe.hpp`): `probe_media` con libavformat en vez de ffprobe (`sound_design.probe_duration`, `compose._color_untagged` y `compose._has_audio`). Paridad (`tests/fixtures/media`, clave `probe`): 21 medios sintéticos (MP4, MOV, MKV, AVI, WAV, MP3, M4A, PNG, GIF, uno cortado, uno vacío, basura y texto, y con varios streams: un MP3 con carátula, el audio antes que el vídeo y dos vídeos) y 2 rutas que no existen, contra las funciones de Python con su ffprobe; y 120 de tus medios en su sitio. Exacto. Solo con `msvc-vcpkg-*` (`tools\dev\verificar_msvc.bat`); `mutantes.py` usa MinGW y aún no lo cubre
- [ ] **gc_media:** demux y decode con hwaccel (D3D11VA/NVDEC), seek exacto
- [ ] Caché de frames con presupuesto de RAM, proxies, miniaturas y formas de onda
- [ ] Mezcla de audio y `loudnorm` a −14 LUFS
- [ ] Encode por hardware (NVENC/AMF/QSV) con alternativa por software y sonda real (como `gpu.py`)
- [ ] **gc_render v1:** vídeo, imagen y GIF; pose y transform; recorte y reframe
- [ ] Opacidad, modos de fusión, filtros y ajustes de color
- [ ] Máscaras básicas, figuras y texto básico
- [ ] `genclip render <proyecto> <timeline> out.mp4 --progress`
- [ ] `genclip frame … --at t`
- [ ] Fixtures de render (PSNR ≥ 35 dB, SSIM ≥ 0,98) y de audio (±0,5 LU)
- [ ] *Strangler:* ajuste «Motor de export: Python | C++» en video-yt, que llama a `genclip render`
- [ ] Regla de congelación: desde aquí video-yt solo recibe correcciones de bugs
- [ ] Benchmark: export igual de rápido o más que el de Python

### Notas de F3
- **Toolchain (2026-10-10):** MSVC 14.50 de Visual Studio 2026 (`D:\visual`; `vcvars64.bat` en `VC\Auxiliary\Build`). El vcpkg de VS no trae el árbol de puertos: hay un clon en `E:\vcpkg` (disco sano; `C:` tiene bloques defectuosos y poco sitio), con la caché binaria en `E:\vcpkg-cache` (`VCPKG_DEFAULT_BINARY_CACHE`) y `builtin-baseline` fijado en `vcpkg.json` a ese commit (`e456309`, 2026-10-10). Para compilar y pasar los tests: `tools\dev\verificar_msvc.bat` (por defecto `msvc-vcpkg-debug`, con FFmpeg y `gc_media`; o `msvc-debug`). `verificar.sh` sigue siendo MinGW (Debug y Release), que no tiene FFmpeg.
  - **Lo que MSVC destapó (GCC lo dejaba pasar):** el iterador de `std::array` no es un puntero (`const auto* it = std::ranges::find(...)` no compila; clang-tidy pide justo eso, así que se usa un bucle) y `Json == std::string_view` es ambiguo en C++20 con el operador reescrito (se compara con `Json(sv)`). `getenv` pide `_CRT_SECURE_NO_WARNINGS` y Catch2 da C4702 tras `FAIL()` (`/wd4702` en los tests).
  - **Sondas de medios (`gc_media`, `src/media/probe.hpp`; hecho):** `probe_media` reproduce las tres llamadas a ffprobe de video-yt con libavformat: abrir con `scan_all_pmts` y `avformat_find_stream_info` (el decodificador rellena el `color_space` de MJPEG y PNG); duración = «N/A» si `ctx->duration == AV_NOPTS_VALUE` (Python → 0.0; pasa con imágenes) y si no `ts · av_q2d(AV_TIME_BASE_Q)` impreso con `%f`, releído y `round(…, 3)`; `_color_untagged` = el primer stream de vídeo (también una carátula) sin nombre o «unknown»; `_has_audio` = algún stream de audio. Si no abre: 0.0, sin etiqueta y sin audio. `gc_media` solo se compila si `find_package(FFMPEG)` lo encuentra (vcpkg); el test es `tests/parity/test_probe_parity.cpp`. Los medios sintéticos se generan UNA vez (`media_cases.PROBE_MEDIA`, en `tests/fixtures/media/files` con git-lfs; regenerarlos cambiaría los bytes). En `export_fixtures.py`, `_install_fakes` sustituye `probe_duration`: las sondas usan la original (`_REAL_SFX["probe_duration"]`). ffprobe del PATH (Gyan 9.0) frente a la libavformat 9.0.2 de vcpkg: coinciden, pero el puerto de vcpkg desactiva por defecto todo lo externo (`--disable-autodetect`), también zlib, y sin zlib no hay decodificador de PNG (el `color_space` de un PNG sale «unknown» en vez de «gbr»): la feature `media` pide `zlib`. Si otra sonda no cuadra, buscar antes un decodificador que falte en el FFmpeg de vcpkg.
  - **Decode: qué fotograma toma el export (siguiente tanda; leído de `compose.py` y `clip_kind.py`):** el export NO hace seek. Abre cada material entero (`ffmpeg_input_args`) y en el grafo hace `fps=FPS,trim=in:out,setpts=PTS-STARTPTS` (`ffmpeg_trim_filter`): pasa a CFR al fps de la timeline ANTES del `trim`, porque las grabaciones de pantalla son VFR y un `trim` dentro de un hueco saldría vacío o adelantado; el preview repite el último fotograma, y `fps` también. Si `out` toca el final del archivo (`out ≥ source_duration − 1e-4`), lo adelanta `max(_LAST_FRAME_PULL, 1/fps)` para no sacar un fotograma negro (`ffmpeg_trim_window`). Los still (`is_still_clip`) van con `-loop 1 -framerate FPS -t dur` y `trim=0:dur`; los GIF con su demuxer y `-ignore_loop 0/1`; una secuencia PNG (`%0…`) con `-framerate FPS -start_number 0`. `frame_grab.extract_frame` (vista de la IA) sí usa `-ss t` antes de `-i` (seek exacto de ffmpeg: el primer fotograma con pts ≥ t). Así que «el fotograma del instante t» de `gc_media` debe reproducir el del filtro `fps` (redondeo al más cercano en la rejilla de 1/FPS) y no el de `-ss`. Las fixtures pueden compararlo con `ffmpeg -i x -vf fps=F,trim=… -f rawvideo` fotograma a fotograma.
  - **VP9 con alfa:** `ffmpeg_input_args` fuerza `-c:v libvpx-vp9` para `kind == "motion"` y todo `.webm`: el decodificador VP9 nativo no expone el alfa. El FFmpeg de vcpkg aún no trae libvpx (feature `vpx`, BSD): añadirla cuando el decode llegue a los WebM con alfa.
  - **Puertos previstos para `gc_media`/`gc_render`** (versiones del baseline): `ffmpeg` 9.0.2 con `avcodec`, `avformat`, `swresample`, `swscale`, `avfilter` y los encoders por hardware `nvcodec`, `amf` y `qsv`; sin `x264`/`x265`/`gpl` (pasarían todo a GPL, plan §10), con `openh264` como alternativa por software si hace falta. `skia` 148 (por defecto: `harfbuzz`, `icu`, `jpeg`, `png`, `webp`, `gl`). `onnxruntime` 1.23.2 llega con F6.
- **Evaluación, dos referencias:** el preview (`canvas.js` → `drawComposite`) evalúa por fotograma y el export (`compose.py`) arma expresiones de FFmpeg muestreando la pose (`_pose_prop_points`: `pose_sample_times` + `animation_sample_times` a 30 fps). Las piezas por clip son las mismas en los dos (`render_pose`, `clip_fx_at`, `clip_masks_at`, `crop_window`, `dest_rect`), y son las que ya están portadas.
- **Orden y visibilidad difieren** entre preview y export (la decisión, abajo):
  - preview (`layersAt`): visible si `start − 0,02 ≤ head < fin` (margen por el *seek* de `<video>`); orden por capa de la pila y después por índice en la lista; entran vídeo, imagen, figura, motion, ajuste y texto, salvo los desactivados o en pista oculta o inexistente;
  - export (`build_command`): solo clips de más de 0,02 s con archivo; dentro de una capa, orden por `start` y después por índice (para que la cadena de overlays sea monótona en el tiempo), y cada clip activo con `between(t, start, end)` (cerrado por los dos lados).
- **Decisión (recomendada por la revisión; falta que Edu la confirme):** una sola semántica, la del export donde decide el resultado, con duraciones de Python:
  - visible si `start ≤ t < start + clip_timeline_duration` (semiabierto, en el instante del fotograma), sin el margen de −0,02 s (compensaba el *seek* de `<video>`; con decodificación exacta adelantaría el clip entrante hasta 0,6 fotogramas) y sin la guarda de 0,02 s (un intervalo vacío ya no se ve);
  - orden por (capa de la pila, `max(0, start)`, índice): el clip que entra tapa al que sale aunque la lista esté al revés (`move_clip` no reordena la lista);
  - `between(t, start, end)` del export es cerrado y, con `eof_action=repeat`, en un corte que cae en la rejilla de 1/fps deja ver el último fotograma del saliente junto al entrante: el C++ no lo reproduce;
  - implementación: `layers_at(timeline, head, LayerRules{seek_margin, export_rules})`. `kPreviewLayers` = `{0.02, false}` es el `layersAt` literal (paridad con Node); `kRenderLayers` = `{0, true}` añade las reglas del export: inicio acotado a 0, desempate por inicio, encaje del clip en su pista (`compose.py:774`) y más de 0,02 s (`compose.py:769`). Las diferencias llevan tests propios (corte, margen, solape de 15 ms, inicio negativo, encaje, ids de pista repetidos de otro tipo y clips de 0,02 s);
  - **divergencia que se queda:** el export pone los textos normales en un único .ass con capa `2·i` por orden de la LISTA (`text_ass.py`, `compose.py:1300`), sin pila ni inicio; `layers_at` los ordena por la pila, como el preview. En tus datos no cambia nada (84 pares de textos solapados, 0 con otro orden). Revisarlo en F4 con el texto;
  - los PSNR de las fixtures de render deben excluir ±1 fotograma alrededor de los bordes de clip.
  - **Tus datos (medido por la revisión):** de 979 clips, 665 están pegados (hueco ≤ 20 ms) a otro de su pista, así que el borde es el caso normal; hay 17 solapes dentro de una pista (5 visuales en V1, de 0,2 a 46 ms) y en 2 el orden de la lista es el inverso al de `start`; 35 clips están fuera de orden de `start` en la lista. No hay clips de ≤ 0,02 s ni `start` negativos.
- **Capas (hecho):** `export_fixtures.py` pasa cada timeline (normalizada por el modelo de Python) a `tools/fixtures/layers_js.mjs`, que importa `clipLayout.js` y `trackStack.js` de video-yt con Node y devuelve, por instante, la POSICIÓN de cada clip en la lista (los ids pueden repetirse). A Node le llega un gemelo de cada clip (`_js_twin`: `in_point` 0, `out_point` = duración de Python, velocidad 1), así que el `clipEnd` de JS da bit a bit el fin de Python y no hay que descartar instantes (antes se descartaban 48: 14 por clips de audio y 34 por fines de JS 1–3 ulps por encima).
  - Rarezas del preview que el modo literal conserva: un clip de duración 0 se ve en [inicio − 0,02, inicio); con ids de clip repetidos, el índice de desempate es el del último; con ids de pista repetidos manda la última pista (su `hidden` y su capa); un clip visual en una pista de audio tiene capa −1 y va al fondo.
  - El render (`kRenderLayers`) toma `max(0, start)` para el intervalo y para el orden, como `compose.build_command` (`start = max(0.0, c.start)`, `end = start + dur`).
- **Fotograma ensamblado (hecho; esto es lo que hace el preview y lo que reproduce `evaluate`):** por capa, el preview (`canvas.js` → `drawComposite`) calcula `localT = max(0, head − start)`, `srcTime = clamp(timelineToSource(clip, head), in, out)` (vídeo: el `currentTime` del `<video>`, que va sincronizado), `clipRenderPose`, `clipFxAt` (opacidad final = fx × pose), `blendOp`, `clipFlip` y `clipMasksAt`; luego:
  - **overlay:** `cropWindow` → `sourceCropPx` → `destRect(posedRenderTransform)` (traslada, gira y escala por `fx.scale` alrededor del centro del destino, desplazado `fx.tx/ty` × destino);
  - **relleno:** `drawReframe` sobre todo el cuadro con el aspecto de SALIDA (no el del hueco `frame`, que es legado) y después la pose sobre el cuadro entero (`fillDestRect`). `fit: contain` se decide en cada instante (`frameAt` → `sourceDrawRect` con `containDest`); con `dual_crop`, dos mitades (`splitOrientationFor`, `targetAspectFor`). Con keyframes de pose y sin `dual_crop`, el recorte sale de la pose (`reframeForDraw`).
  - **El export difiere en el relleno:** `contain` solo si TODOS los keyframes del reframe lo son (`_kfs_all_contain`; si no, cover siempre) y el doble encuadre va por `recipe_layout` (vstack/hstack). Hay que elegir una semántica, como con las capas.
  - **Tus datos:** los 235 clips de vídeo e imagen son overlay (`frame: free`); no hay relleno, `dual_crop` ni `contain` (2 clips tienen keyframes de reframe, sin `contain`). La divergencia del relleno solo afecta a casos sintéticos o a proyectos antiguos.
  - **Oráculo:** `tools/fixtures/frame_js.mjs` importa `canvas.js` con Node (con `tools/fixtures/node_hook.mjs`, que añade la extensión a los imports relativos, como Vite) y ejecuta `drawComposite` sobre un contexto 2D que GRABA: lleva la matriz (translate/rotate/scale/setTransform), el último recorte y la opacidad, y por cada `drawImage` del material (un objeto con sus dimensiones y `currentTime` = el tiempo de fuente) guarda el rectángulo de la fuente y la afín fuente → cuadro. El C++ repite el mismo orden de operaciones, así que coincide bit a bit salvo en las capas giradas (sin/cos de V8 frente a la libm: tolerancia relativa de 1e-12).
  - **Lo que el oráculo no ve:** sin `document`, las máscaras y la fusión se dibujan sin capa aparte (se comparan por sus piezas, ya probadas), el filtro CSS (`look`, efectos) no se compara (F4), y textos, figuras y capas de ajuste no dibujan material.
  - **`clipDur` de JS:** se excluyen los dibujos de los clips cuyo `clipDur` de JS no es la duración de Python y en los que eso cambia el dibujo (con salida, aparición en un clip de menos de 0,8 s con cualquiera de las dos duraciones, o animaciones de catálogo): 22 de 1714 en tus datos. Ejemplo: una duración de 0,8 en Python es 0,7999999999999972 en JS y cambia la ventana de aparición.
  - **Más divergencias del export (revisión del fotograma; el C++ sigue al preview):**
    1. **`slide` en overlay** (decisión de Edu): el preview desplaza `fx.tx·ancho` y `fx.ty·alto` del DESTINO (`clipFx.js`, `canvas.js`); el export, el eje entero `W` o `H` (`clip_fx.py`, `overlay_xy_for_fx`). Afecta a 3 de tus clips (`cmui9xz1z2t`, `cmuc325tsd`, `cmu4ww5j17`): uno de 1920×1080 a escala 0,31 en 720×1280 se mueve ~335 px en el preview y 1280 px en el export.
    2. El overlay estático del export redondea a pares (`dest_rect_even`, recorte entero, giro solo por encima de 0,05°): hasta ~1 px frente a los blits en coma flotante (1968×1108 frente a 1969,15×1107,65).
    3. El export hace pares `W` y `H` y el doble encuadre usa `H//2`: `evaluate` usa `out_w`/`out_h` tal cual (lo dice `FrameSpec`).
    4. `clip_layout.dest_rect` del export lee `float(t.get("x") or 0.5)`: `x`, `y` o `scale` iguales a 0 valen 0,5 o 1; el preview respeta el 0 (un clip en el borde). Ninguno en tus datos.
    5. En relleno, el export ignora un `transform` estático (`pose_transform_animates` es falso sin keyframes ni animaciones); el preview y `evaluate` lo aplican. Tus clips son overlay.
  - **Rareza del preview que solo reproduce el modo preview:** en relleno, una escala que se aparta de 1 en 0,001 o menos (sin otra transformación) no se aplica; `kRenderLayers` la aplica.
  - **El relleno sigue al preview:** `contain` en cada instante y el doble encuadre por mitades. El export lo hace distinto (abajo) y está pendiente de que Edu elija.
- **Duraciones del preview ≠ Python:** `clipEnd` de `editorModel.js` usa `clipSpeed` de JS, que trata las **figuras** como velocidad 1 (Python solo exime texto, imagen y ajuste) y **no redondea**; `clip_timeline_duration` de Python redondea la fuente y el resultado a 3 decimales. El C++ usa siempre la duración de Python; las fixtures de `layersAt` lo resuelven con el gemelo de cada clip. El JS se importa bien desde Node 24 (`clipLayout.js` usa rutas con extensión; probado con `pathToFileURL`).
- **`animation_sample_times`:** tope de `kMaxAnimSamples` (10⁷ intervalos) sin test: solo se alcanza con duraciones irreales. Con una duración infinita Python lanza OverflowError y C++ construye 10⁷ muestras (~80 MB).
- **`fx_anims`:** el catálogo no usa `linear`, `inOutQuad`, `inOutCubic` ni `hold`; la rejilla de curvas los cubre. En `eval_track`, la rama de tramo de longitud 0 (`f = 1`) es inalcanzable: el tramo anterior ya cubre ese instante (también con claves desordenadas).
- **Tolerancia:** con tolerancia 0 fallan 3 muestras, todas de 1 ulp (curvas con `pow()`). La tolerancia es de 4e-15 relativa (unos 18 ulps, margen para otras libm) y solo donde hay `pow`/`sin`/`cos`: las curvas `outCubic`, `inOutQuad`, `inOutCubic`, `outBack`, `outElastic` e `inOutSine`, y la matriz de color con exposición o tono. Con 1e-12, o con tolerancia en cualquier clip animado, sobrevivían mutantes de 1e-14 en las constantes (`kC4`, `0.213`, `0.18`). NaN e infinito viajan como texto (`_nan_safe` en el exportador).
- **Tus datos** no usan animaciones de catálogo, modos de fusión ni máscaras de ajuste: eso solo lo cubren los casos sintéticos. Sí usan aparición y salida (fade, dissolve, zoom, wipe, slide).
- **Sin portar a propósito:** las cadenas de FFmpeg (`effects_ffmpeg`, `video_fx_chain`, `colorchannelmixer`, `blend_steps`): el render de Skia las sustituye. El `look` sale como texto; su filtro (`clipFx.js` → `cssFilter`) va con el render.

## F4 — Render completo

- [ ] Medida real del texto (FreeType + HarfBuzz) en `set_text_width`, comparada con la de Pillow de video-yt
- [ ] Texto completo: temas de subtítulos, karaoke por palabra, contorno, sombras, 3D y máscara en texto (SkParagraph + HarfBuzz)
- [ ] Revisión visual del texto Skia frente a libass
- [ ] Appear/exit, transiciones y looks
- [ ] Capa de ajuste, barras de cine y film
- [ ] Contorno del sujeto y chroma key
- [ ] Efectos animados del catálogo (fx → SkSL, efecto a efecto)
- [ ] `paper_cutout` («Papel recortado», `fx/effects/paper.js`): borde rasgado, arrugas, sombra, vibración stop-motion y pliegue de apertura y cierre. Sustituye al editor de Paper, que no se porta (plan §6.2)
- [ ] Alfa de «Eliminar fondo» leído desde la caché existente
- [ ] Marco de plataforma TikTok/Shorts (solo en preview)
- [ ] **Salida:** todo lo de `docs/` de video-yt se exporta desde C++, sin libass ni Chromium-fx

## F5 — Editor de escritorio Qt → **M2**

- [ ] App Qt Quick (QML + ViewModels C++) con preview como `QQuickItem`
- [ ] 1. Inicio: proyectos y carpetas
- [ ] 2. Editor: preview + timeline (arrastre sin solapes, pista magnética, zoom, beats y marcadores)
- [ ] 3. Inspector: transform, texto, filtros, máscaras y modal de recortar
- [ ] Pestaña **Efectos** del inspector (plan §6.3): aparece al añadir un efecto al clip; una subpestaña por efecto, en el orden de `vfx` y reordenable, con sus propiedades generadas desde los `params` del catálogo (`toggle`, `range`, `select`, `color`…), activar o desactivar, restablecer y quitar. Con muchos efectos, una lista de tarjetas plegables
- [ ] 4. Biblioteca y materiales
- [ ] 5. Export con progreso
- [ ] 6. Ajustes
- [ ] 7. Atajos de teclado
- [ ] Geometría de la vista previa de `clipLayout.js` (`frameOf`, `slotAspectOf`, `destRectOnFrame`, `freeFrameAt`, tiradores de recorte)
- [ ] Reproductor: reloj de audio maestro, preview a ½ o ¼, proxies al importar, repintado solo si algo cambia y 60 fps en el cabezal
- [ ] Puente REST temporal con el backend Python (`:8000`) para lo no portado
- [ ] **Salida:** editar y exportar un short completo sin abrir la UI React

## F6 — IA local

- [ ] ONNX Runtime con un *execution provider* por plataforma (CUDA, DirectML, CoreML…)
- [ ] Eliminar fondo: RVM, BiRefNet y U²-Net con caché en 2 niveles (alfa en espacio FUENTE)
- [ ] SAM 2.1: 4 herramientas, seguimiento (`sam_track`) y pincel con análisis en segundo plano
- [ ] whisper.cpp con timestamps por palabra
- [ ] TTS: Kokoro y Piper (espeak-ng)
- [ ] YuNet + reframe automático
- [ ] Tracking LK + RANSAC (`object_track`)
- [ ] Beats por FFT
- [ ] `model_manager`: descarga bajo demanda y presupuesto de RAM
- [ ] Fixtures de IA (IoU ≥ 0,95; Δ palabras ≤ 80 ms) y benchmarks iguales o mejores que Python

## F7 — Nube, agente y MCP

- [ ] gc_cloud con SSE: Gemini, OpenAI-compatible (openrouter, groq, cerebras, mistral, hf), Azure (TTS, STT, Vision, imagen; 3 regiones con alternativa), Foundry
- [ ] Stock: Pexels, Giphy, Pixabay y Unsplash
- [ ] Chat IA con streaming y confirmaciones
- [ ] Servidor MCP en `:8000/mcp` con los 50 tools, DTO semántico, errores estructurados y resources (`capabilities://`, `help://`, `config://`, `project://`)
- [ ] YouTube con el sidecar `yt-dlp` (solo escritorio)
- [ ] Dirección de escena, Generar escena y Generar recurso
- [ ] Prompts y plantillas movidos a `resources/prompts` y `resources/templates`
- [ ] Decidir si gc_cloud/gc_agent se hacen en Rust (`rmcp`) o en C++
- [ ] **Salida:** las fixtures MCP pasan y Claude Desktop conecta sin tocar su configuración

## F8 — Fuera de alcance por ahora

Decidido el 2026-10-08 (plan §6). No hay tareas:
- **Motion Studio** (con su stick, `motion/stick.js` y `stick.py`) no se porta todavía. Las composiciones de Motion de los proyectos se conservan al leer y escribir, pero no se editan.
- **Paper** ya es el efecto `paper_cutout` (va en F4) y su editor no se porta.
- Los personajes stick de la biblioteca (`stick_library`) siguen en F1.

## F9 — Empaquetado de escritorio → **M3**

- [ ] Decidir la licencia de distribución (GPL frente a evitar x264 y espeak-ng)
- [ ] Windows: `GenClipPro.exe` + `genclip.exe`, `windeployqt` e instalador NSIS/WiX/MSIX (o ZIP portátil)
- [ ] macOS: `.dmg` firmado y notarizado
- [ ] Linux: AppImage o Flatpak
- [ ] Modelos descargados en el primer uso (sustituye a `setup/nucleo.py`)
- [ ] Actualizaciones e informes de fallos (Crashpad)
- [ ] Firma de código
- [ ] **Se retira la app Python**

## F10 — Móvil → **M4**

- [ ] Qt for Android/iOS con UI táctil (layouts adaptativos y gestos en timeline y preview)
- [ ] *Scoped storage* y selector de Fotos
- [ ] Codecs por hardware (MediaCodec, VideoToolbox)
- [ ] ORT con NNAPI/XNNPACK o CoreML y modelos «lite»; límites de RAM
- [ ] Export en segundo plano (*foreground service* en Android; limitado en iOS)
- [ ] Sin YouTube
- [ ] CI de Android e iOS

## Opcionales

- [ ] T4: Linux headless (`genclip` + servidor MCP para renderizar en un servidor)
- [ ] T5: Web (WASM + CanvasKit)

---

## Decisiones (§13 del plan)

- [x] Orden de dispositivos: escritorio primero, móvil al final
- [x] UI: Qt Quick/QML
- [x] Motion Studio: **aplazado** (2026-10-08), fuera de alcance por ahora; el editor de Paper no se porta (es el efecto `paper_cutout`)
- [x] Congelar video-yt desde F3 (solo correcciones de bugs)
- [x] Rust: no se usa por rendimiento; solo se valorará para gc_cloud/gc_agent en F7
- [ ] Licencia de distribución: antes de F9
