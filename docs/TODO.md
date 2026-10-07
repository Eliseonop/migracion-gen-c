# TODO de la migración: video-yt → GenClipPro

> Lista de tareas con checks de **todas las fases** del [plan](PLAN_MIGRACION_CPP.md).
> El detalle de cada tanda (qué se hizo, cómo se verificó, cifras) está en el §14 del plan.
> Arquitectura: [ARQUITECTURA.md](ARQUITECTURA.md). Actualizado: **2026-10-07**.

Leyenda: `[x]` hecho · `[ ]` pendiente · *(parcial)* empezado pero no cerrado.

## Estado de un vistazo

| Fase | Contenido | Estado | Hito |
|---|---|---|---|
| **F0** Cimientos | toolchain, CMake, CI, tests | ✅ hecha salvo MSVC y CI en marcha | |
| **F1** Modelo y formato | modelo, JSON, migraciones, fixtures | ✅ modelo, ajustes y biblioteca hechos · faltan las colecciones y la biblioteca de SFX de disco | |
| **F2** Lógica de edición | ops, sin solapes, keyframes, undo | ✅ hecha: las 42 ops, el historial en disco y el autoguardado | |
| **F3** Media + export | gc_media, gc_render v1, `genclip render` | 🟡 evaluación hecha (por clip, capas y fotograma) · lo demás espera al Windows SDK | **M1** |
| **F4** Render completo | texto, fx, transiciones, chroma | ⬜ | |
| **F5** Editor de escritorio | app Qt Quick | ⬜ | **M2** |
| **F6** IA local | fondo, SAM, Whisper, TTS, tracking | ⬜ | |
| **F7** Nube, agente, MCP | providers, chat, MCP :8000, YouTube | ⬜ | |
| **F8** Motion, Stick, Paper | Skia + sidecar Chromium | ⬜ | |
| **F9** Empaquetado escritorio | instaladores, `.exe` | ⬜ | **M3** |
| **F10** Móvil | Android + iOS | ⬜ | **M4** |

**Cifras actuales:** 80 tests y 110 740 aserciones en verde (Debug y Release, 0 warnings).
Paridad con video-yt en modelo (18 proyectos), edición (378 casos sintéticos + 1919 ops reales),
animación (236 casos + 733 clips reales), encuadre (2908 muestras sintéticas + 5875 de 235 clips reales)
historial (69 pasos sintéticos + el de tus 14 proyectos) ajustes (59 pasos + los tuyos, con las claves redactadas) biblioteca (52 pasos + 23 materiales + la tuya) y evaluación por clip (41 clips con 984 muestras + 434 casos sueltos; 914 clips reales con 7122 muestras) capas de un instante (81 instantes sintéticos + 6675 de tus timelines) y fotograma (95 instantes sintéticos con 203 dibujos + 831 de tus timelines con 1692 dibujos), estos dos contra el JS real con Node.
Todo exacto bit a bit salvo lo que pasa por `pow()`, `sin()` o `cos()` de la libm: tolerancia de 1e-12 (4e-15 en la evaluación).

---

## Siguiente (para el autopiloto)

F2 está cerrada y F1 casi: solo faltan dos piezas de entrada y salida. F3 necesita el Windows SDK, así que el orden es:

1. **Con el Windows SDK instalado:** F3, empezando por MSVC + vcpkg (FFmpeg, Skia, ONNX Runtime) y `gc_media`.
2. **Sin él, en este orden:**
   - F1, biblioteca de SFX de disco (`sfx.py`) y colecciones (`collections.py`). Las colecciones son de la interfaz y pueden esperar a F5: escanean carpetas y ordenan como `WindowsPath`, con el `lower()` de Unicode;
   - F0, `.clang-tidy` y spdlog.
3. La evaluación de F3 está completa (por clip, capas y fotograma): lo siguiente de F3 ya es `gc_media` y `gc_render`, que necesitan el SDK.

## Pendiente por tu parte (Edu)

- [ ] Instalar el **Windows SDK**: *Visual Studio Installer → Modificar → «Desarrollo para el escritorio con C++»* y marcar un Windows 11 SDK. Sin esto MSVC no enlaza, y F3 lo necesita.
- [x] Primer commit hecho: un commit local por tanda verificada, sin push.
- [ ] Crear el **remoto en GitHub** para que el CI se ejecute.
- [ ] Confirmar la **semántica de visibilidad y orden de capas** del render C++ (notas de F3): intervalo [inicio, fin) sin el margen de 0,02 s del preview, inicio acotado a 0, desempate por `start` dentro de una capa, solo clips que encajan en su pista y de más de 0,02 s, y fin con la duración redondeada de Python. Los textos se ordenan por la pila (el export los ordena por la lista en un .ass). Cambia algún fotograma de borde respecto al export actual. Mientras tanto se implementa así, con el modo del preview disponible.
- [ ] Elegir la **semántica del relleno** (notas de F3): `fit: contain` decidido en cada instante (preview) o solo si todos los keyframes lo son (export), y el doble encuadre. Tus clips no lo usan (todos son overlay).
- [ ] *(Sugerencia)* Mover GenClipPro fuera de `C:`, porque ese SSD tiene bloques defectuosos.

---

## F0 — Cimientos

- [x] Repositorio git (`main`) con `.gitignore`, `.gitattributes` (git-lfs), `.clang-format` y `.editorconfig`
- [x] CMake ≥ 3.28 con presets: `mingw-*`, `msvc-*`, `msvc-vcpkg-*`, `linux-debug`, `macos-debug` y `ci`
- [x] Dependencias con `find_package` y, si no están, FetchContent con hash verificado (nlohmann/json 3.12.0, Catch2 3.9.1)
- [x] `vcpkg.json` preparado para F3
- [x] Warnings estrictos (`gc_set_warnings`): 0 warnings con GCC 15
- [x] `-ffp-contract=off` para que los cálculos den los mismos bits en todas las plataformas
- [x] `genclip --version` con argumentos y salida en UTF-8 en Windows
- [x] CI de GitHub Actions escrito (Windows MSVC, Linux GCC, macOS Clang)
- [ ] CI ejecutándose (falta el remoto)
- [ ] Toolchain MSVC operativo (`msvc-debug`): depende del Windows SDK
- [ ] `GC_WARNINGS_AS_ERRORS` activado en el CI de Windows
- [ ] `.clang-tidy` y spdlog
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
- [x] Biblioteca de `data/library` (`library.hpp`): carga tolerante, tipo, origen y fuente deducidos, `material_dto` (con la URL codificada como `urllib.parse.quote`), lista ordenada por fecha, editar título y descripción, metadata semántica, proyectos que la usan y quitar (borra el archivo). Paridad: 52 pasos sobre un `data/` temporal (con los mensajes de error que ve el usuario), 23 materiales sueltos y tu `library.json`; 8/8 mutaciones
- [ ] «Guardar en la biblioteca» desde un proyecto (`library.save_from_project`): cambia el proyecto (`retarget_timeline_asset`, `remove_material`) y copia archivos, así que llega con el almacén de proyectos de la app (F5)
- [ ] Colecciones de `data/` (`collections`: carpetas de material bajo `material_root`)
- [ ] Biblioteca de SFX desde disco (`sfx.load_index`, `_scan`, `_entry` y `resolve`, con la duración de `ffprobe`) → `set_sfx_library`. Sin ella, `subject_pop` avisa siempre de que faltan los sonidos
- [x] Ajustes y API keys (`settings.hpp`: `settings.py`, `export_settings.py` y `transcribe_settings.py`): carga con defectos, `save` con la fusión de claves y máscaras, varias claves por proveedor y la vista pública sin secretos. Paridad: 59 pasos sintéticos con claves falsas y tus ajustes con las claves redactadas; 10/10 mutaciones

### Notas de F1

- **JSON como Python:** `json.loads(read_text("utf-8"))` falla con un BOM de UTF-8. Ajustes, biblioteca e historial usan `read_json_file_py`, que también falla; `read_json_file` (proyectos y timelines) lo sigue saltando.
- **Texto de Python en `py.hpp`:** `py_lower` (`str.lower()` de Unicode 16, con la sigma final; tabla generada con `tools/gen/gen_py_lower.py`), `py_path_name`, `py_path_suffix` y `py_path_stem` (Python 3.14 en Windows) y `py_quote`.

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
- **`clip_motion` sin portar todavía:** `sample_rms_envelope` (lee el audio con ffmpeg: F3), `normalize_amps`, `downsample_envelope`, `overlap_window` y `audio_src_at`. Los usa la herramienta MCP que calcula la envolvente de un sonido (F7); `animate_clip` recibe la envolvente ya hecha.
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

- [ ] Prerrequisito: MSVC + vcpkg con FFmpeg, Skia y ONNX Runtime
- [x] **Evaluación:** timeline en el instante *t* → lista de capas (usa keyframes, pose, máscaras y layout)
  - [x] Por clip: animaciones de catálogo (`fx_anims`: `clip_animations`, `animation_delta`, `render_pose`, `anim_opacity` y `animation_sample_times`), aparición y salida (`clip_fx`: `clip_fx_at`, `fx_windows`), modo de fusión (`blend`) y ajustes de color (`clip_adjust`: matriz 3×3, `has_color_adjust`, `strip_color` y las pasadas base + fantasma de la máscara de ajuste). Paridad: 41 clips sintéticos en 24 instantes (uno es NaN), 240 puntos de curva, 150 de pista, 10 ventanas, 16 efectos y 18 modos de fusión sueltos, y tus 914 clips en 7122 instantes; 34/35 mutaciones con `tools/dev/mutantes.py eval` (la otra es equivalente)
  - [x] Capas de un instante: `stack_layers` y `buried_text_track_ids` (`timeline_rules`) y `layers_at`/`videos_at` (`layers.hpp`) con dos reglas, `kRenderLayers` (la del render C++) y `kPreviewLayers` (el `layersAt` literal del preview). Paridad contra el JS real ejecutado con Node: 81 instantes en 13 timelines sintéticas y 6675 en tus 18 timelines, sin descartes (a Node le llega un gemelo de cada clip con la duración de Python); las reglas del render, con 10 tests propios. 20/20 mutaciones (`tools/dev/mutantes.py layers`)
  - [x] Fotograma ensamblado (`frame.hpp`: `evaluate(timeline, t, FrameSpec, dimensiones del material)`): por capa y pasada, tiempo local y de fuente, pose, aparición/salida, opacidad, fusión, volteo y máscaras, y para vídeo, imagen y motion los «blits» (rectángulo de la fuente + transformación afín al cuadro, en el mismo orden de operaciones que el contexto 2D), con overlay, relleno (cover/contain) y doble encuadre, y la cortinilla. Paridad contra el `drawComposite` real ejecutado con Node sobre un contexto que graba: 10 timelines sintéticas (95 instantes, 203 dibujos) y tus 18 (831 instantes, 1692 dibujos; 22 fuera por el `clipDur` de JS); 7 tests de las reglas del render y los errores; 21/21 mutaciones (`tools/dev/mutantes.py frame`)
- [x] `recipe_layout.py`: `split_orientation_for` y `contain_dest` están en `frame.cpp` (doble encuadre y `contain` del relleno). Los filtros `vstack`/`hstack` y `dual_slot_wh` son cadenas de FFmpeg: con Skia no hacen falta
- [x] Comprobar las dimensiones antes de llamar a `clip_layout`: `evaluate` falla si las de salida no son finitas y mayores que 0 y, con las reglas del render, si las de un material tampoco (Python lanza ZeroDivisionError); con las del preview esa capa va sin blits, como `drawComposite`
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
- [ ] Alfa de «Eliminar fondo» leído desde la caché existente
- [ ] Marco de plataforma TikTok/Shorts (solo en preview)
- [ ] **Salida:** todo lo de `docs/` de video-yt se exporta desde C++, sin libass ni Chromium-fx

## F5 — Editor de escritorio Qt → **M2**

- [ ] App Qt Quick (QML + ViewModels C++) con preview como `QQuickItem`
- [ ] 1. Inicio: proyectos y carpetas
- [ ] 2. Editor: preview + timeline (arrastre sin solapes, pista magnética, zoom, beats y marcadores)
- [ ] 3. Inspector: transform, texto, filtros, máscaras y modal de recortar
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

## F8 — Motion, Stick, Paper

- [ ] Stick nativo en Skia (port de `stick.js`)
- [ ] Paper nativo en Skia (+ UI)
- [ ] Motion Studio con sidecar Chromium por CDP (opción A)
- [ ] Spike de 2 semanas del formato nativo (opción B: JSON declarativo o Lottie/Skottie)

## F9 — Empaquetado de escritorio → **M3**

- [ ] Decidir la licencia de distribución (GPL frente a evitar x264 y espeak-ng)
- [ ] Windows: `GenClipPro.exe` + `genclip.exe`, `windeployqt` e instalador NSIS/WiX/MSIX (o ZIP portátil)
- [ ] macOS: `.dmg` firmado y notarizado
- [ ] Linux: AppImage o Flatpak
- [ ] Modelos y Chromium descargados en el primer uso (sustituye a `setup/nucleo.py`)
- [ ] Actualizaciones e informes de fallos (Crashpad)
- [ ] Firma de código
- [ ] **Se retira la app Python**

## F10 — Móvil → **M4**

- [ ] Qt for Android/iOS con UI táctil (layouts adaptativos y gestos en timeline y preview)
- [ ] *Scoped storage* y selector de Fotos
- [ ] Codecs por hardware (MediaCodec, VideoToolbox)
- [ ] ORT con NNAPI/XNNPACK o CoreML y modelos «lite»; límites de RAM
- [ ] Export en segundo plano (*foreground service* en Android; limitado en iOS)
- [ ] Sin YouTube; Motion en remoto o desactivado
- [ ] CI de Android e iOS

## Opcionales

- [ ] T4: Linux headless (`genclip` + servidor MCP para renderizar en un servidor)
- [ ] T5: Web (WASM + CanvasKit)

---

## Decisiones (§13 del plan)

- [x] Orden de dispositivos: escritorio primero, móvil al final
- [x] UI: Qt Quick/QML
- [x] Motion Studio: sidecar Chromium (A) + spike de formato nativo (B)
- [x] Congelar video-yt desde F3 (solo correcciones de bugs)
- [x] Rust: no se usa por rendimiento; solo se valorará para gc_cloud/gc_agent en F7
- [ ] Licencia de distribución: antes de F9
