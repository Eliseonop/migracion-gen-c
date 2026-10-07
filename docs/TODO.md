# TODO de la migración: video-yt → GenClipPro

> Lista de tareas con checks de **todas las fases** del [plan](PLAN_MIGRACION_CPP.md).
> El detalle de cada tanda (qué se hizo, cómo se verificó, cifras) está en el §14 del plan.
> Arquitectura: [ARQUITECTURA.md](ARQUITECTURA.md). Actualizado: **2026-10-07**.

Leyenda: `[x]` hecho · `[ ]` pendiente · *(parcial)* empezado pero no cerrado.

## Estado de un vistazo

| Fase | Contenido | Estado | Hito |
|---|---|---|---|
| **F0** Cimientos | toolchain, CMake, CI, tests | ✅ hecha salvo MSVC y CI en marcha | |
| **F1** Modelo y formato | modelo, JSON, migraciones, fixtures | ✅ modelo hecho · falta `data/` (biblioteca, ajustes) | |
| **F2** Lógica de edición | ops, sin solapes, keyframes, undo | 🟡 en curso: 37 de las 42 ops de Python | |
| **F3** Media + export | gc_media, gc_render v1, `genclip render` | ⬜ | **M1** |
| **F4** Render completo | texto, fx, transiciones, chroma | ⬜ | |
| **F5** Editor de escritorio | app Qt Quick | ⬜ | **M2** |
| **F6** IA local | fondo, SAM, Whisper, TTS, tracking | ⬜ | |
| **F7** Nube, agente, MCP | providers, chat, MCP :8000, YouTube | ⬜ | |
| **F8** Motion, Stick, Paper | Skia + sidecar Chromium | ⬜ | |
| **F9** Empaquetado escritorio | instaladores, `.exe` | ⬜ | **M3** |
| **F10** Móvil | Android + iOS | ⬜ | **M4** |

**Cifras actuales:** 38 tests y 31 158 aserciones en verde (Debug y Release, 0 warnings).
Paridad exacta con video-yt en modelo (18 proyectos), edición (228 casos sintéticos + 1452 ops reales)
y animación (236 casos + 733 clips reales).

---

## Pendiente por tu parte (Edu)

- [ ] Instalar el **Windows SDK**: *Visual Studio Installer → Modificar → «Desarrollo para el escritorio con C++»* y marcar un Windows 11 SDK. Sin esto MSVC no enlaza, y F3 lo necesita.
- [ ] Decidir si se hace el **primer commit** (todavía no hay ninguno).
- [ ] Crear el **remoto en GitHub** para que el CI se ejecute.
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
- [ ] Biblioteca y colecciones de `data/` (`library`, `collections`)
- [ ] Ajustes y API keys (`settings.json`)

## F2 — Lógica de edición *(en curso)*

### Arquitectura del motor
- [x] Documento inmutable con estructura compartida (`Cow<T>`)
- [x] Comandos como datos (`std::variant`) → `apply_command` (op pura → *settle* → validación de errores nuevos)
- [x] `EditSession`: snapshots, deshacer/rehacer (límite 50), checkpoints, `Group` (un paso de deshacer), observadores y revisión
- [x] Ids inyectables (`IdSource`; `SequentialIdSource` = secuencia de las fixtures)
- [x] Resultado de cada op con `changed` y `warnings` (API de ops con diff)
- [x] Semántica de Python donde la paridad lo exige (`round`, `repr`, `strip`, cortes, `float()`/`int()`, `min`/`max` que conservan el tipo)

### Reglas y matemática
- [x] `timeline_rules`: tipos de clip ↔ pista, velocidad y duración, pila de capas (`display_order`, `reorder_tracks`) y validación
- [x] `no_overlap`: empujar si crece, subir de pista si cae encima, pista magnética
- [x] `keyframes`: curvas (ease, cúbicas, rebote, bézier, *hold*), interpolación por propiedad, colores, estilo de texto animado, `upsert_keyframe_at` y fundidos de volumen
- [x] `masks`: normalizar y evaluar máscaras animadas
- [x] `fx_catalog` (recurso `resources/fx/catalog.json`), `pose` (`clip_pose`, `frame_at`), `shapes`, `bg` y `captions`
- [ ] `clip_layout` (`destRectOnFrame`, `cropWindow`) con fixtures de layout *(parcial: ya están pose y `frame_at`; F3 lo necesita para evaluar)*
- [ ] `clip_motion`, para `animate_clip`
- [ ] `clip_attrs`, para `paste_clip_attributes`

### Comandos: 40 hechos (37 de las 42 ops de Python, más 3 setters propios)
- [x] **Pistas:** `add_track`, `reorder_track`, `rename_track`, `remove_track`, `link_tracks` y `unlink_track`
- [x] **Clips:** `add_clip`, `move_clip`, `remove_clip`, `split_clip` y `duplicate_clip`
- [x] **Proyecto:** `set_project_format` y `set_markers`
- [x] **Propiedades:** `update_clip`, `set_clip_opacity`, `set_clip_speed` (incluye `reverse`), `set_clip_transition`, `set_text_role`, `set_clip_blend`, `set_clip_flip`, `set_clip_layout` y `set_clip_note` (desactivar va por `update_clip`)
- [x] **Encuadre:** `reframe_clip` y `crop_clip`
- [x] **Animación y efectos:** `set_clip_keyframes`, `set_clip_beats`, `set_clip_masks`, `set_clip_effects`, `apply_effect` y `set_clip_animation`
- [x] **Audio:** `set_clip_volume`, `set_track_audio` y `set_clip_audio_fx`
- [x] **Material nuevo:** `replace_media`, `freeze_frame`, `add_subtitles`, `add_shape`, `add_cinema_bars`, `add_adjustment_layer` y `set_clip_bg_removal`
- [ ] `animate_clip` (zoom, giro, deslizar… generados; `clip_motion.py`)
- [ ] `paste_clip_attributes` (`clip_attrs.py`)
- [ ] `follow_object` (`object_track.follow_keys`)
- [ ] `add_sound_design` (`timeline_ops` + sound design)
- [ ] `apply_recipe` (`recipes.py`, `recipe_layout.py`)

### Notas para la 4ª tanda (análisis hecho el 2026-10-07, sin código aún)
- **Orden:** `clip_layout` (`crop_window`, `source_point_to_output`, `dest_rect[_even]`) → `clip_attrs` → `follow_object` (usa `source_point_to_output`) → `add_sound_design` → `clip_motion` (`animate_clip`) → recetas.
- **`interp_items`:** hay que hacerla pública en `keyframes.hpp` (la usan `clip_attrs` y `crop_window`).
- **`clip_attrs._constant_props`:** recorre un `set` de Python, así que el orden de las claves en `props` es aleatorio. En la paridad de `paste_clip_attributes` hay que comparar `keyframes.items[*].props` sin orden.
- **Orden de los uuid en `paste_attributes`:** primero los grupos en el orden de `ATTR_GROUP_IDS` (crop: ids de keyframes; mask: `m…`; vfx: `fx` + 10 hex), después los ids de los items de `merge_keyframes`.
- **Recetas:**
  - `text_reflection` mide texto (`text_ass._rows` → Pillow). Se inyecta como dependencia: el exportador parchea `text_ass.text_width` con la estimación `len·em·0,55 + ls·len`, que el C++ usa por defecto. La medición real llega en F4 (FreeType).
  - `subject_pop` lee la biblioteca de SFX y hace `ffprobe`. También se inyecta: biblioteca falsa y duraciones fijas guardadas en la propia fixture (se parchean `sfx.search`, `sfx.resolve` y `sound_design.probe_duration`).
- **`sound_design.norm`:** usa NFKD + quitar lo no ASCII. Basta con una tabla para Latin-1 y Latin Extended-A.
- **`_SFX_TRACK_RE` (`^sfx\b`, sin distinguir mayúsculas):** «SFX_1» y «sfx1» no casan; «Sfx-x» sí.
- **`Path(id).stem` (Python 3.14):** «x.» → «x»; «.hidden» → «.hidden»; «x..y» → «x.»; «C:foo.wav» → «foo».
- **Matemática:** Python redondea con `round()` al par (`_even`, `_even_pos`), su `%` de reales es de suelo (rotación en `follow_keys`) y `degrees = x·(180/π)`.

### Persistencia
- [ ] Guardar y cargar el historial (`data/history/<pid>.json`, mismo formato que `timeline_history.py`)
- [ ] Autoguardado

### Verificación
- [x] Fixtures de edición generadas desde el `apply_op` REAL de video-yt (`uuid4` determinista)
- [x] 228 casos sintéticos (100 de error) y 1452 operaciones sobre tus 18 timelines reales
- [x] Animación: 236 casos sintéticos y 733 clips reales (bit a bit salvo 1 `pow()` → tolerancia 1e-12)
- [x] Pruebas de mutación: detectan 1e-9, colores, `1` ↔ `1.0` y el orden de claves
- [x] Benchmark: ≈ 90 µs por edición en C++ frente a ≈ 49 ms en Python (timeline de 167 clips)
- [ ] **Salida de F2:** 100 % de paridad con las 42 ops y con las fixtures de layout

## F3 — Media + export C++ → **M1**

- [ ] Prerrequisito: MSVC + vcpkg con FFmpeg, Skia y ONNX Runtime
- [ ] **Evaluación:** timeline en el instante *t* → lista de capas (usa keyframes, pose, máscaras y layout)
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

## F4 — Render completo

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
