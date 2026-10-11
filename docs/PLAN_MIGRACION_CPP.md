# Plan de migración: video-yt → GenClipPro (C++)

> Estado: **en marcha** desde 2026-10-06 · F0 hecha (salvo el CI en marcha) · F1 hecha (salvo lo que escribe desde la interfaz) · F2 cerrada · F3 en curso (evaluación hecha; MSVC y vcpkg en marcha para `gc_media`). Ver §14, [ARQUITECTURA.md](ARQUITECTURA.md) y la lista de tareas [TODO.md](TODO.md).
> Origen: `E:\proyectos\video-yt` (FastAPI + React). Destino: `C:\Users\Edu\CLionProjects\GenClipPro`.

## 0. Resumen

- **Qué se construye:** un **motor C++ único** (modelo, edición, decodificación, render, IA local) que se ejecuta igual en todos los dispositivos, con una **UI Qt Quick (QML)** encima. Ya no hay servidor HTTP entre la UI y el motor: la UI llama directamente a C++.
- **La mayor ventaja no es la velocidad, sino tener un solo renderizador.** Hoy el preview (`canvas.js` + `lib/*.js`) y el export (`compose.py` → FFmpeg `filter_complex` + libass + Chromium) son dos motores con la lógica duplicada en JS y en Python. En C++ el **mismo compositor (Skia)** dibuja el preview y el export, así que la paridad entre ambos ya no hay que mantenerla a mano.
- **Cómo se migra:** de forma **incremental (strangler)**. La app Python sigue en uso hasta que la C++ la iguale, y cada fase entrega algo que se usa de verdad:
  - **M1 (~4–5 meses):** la app Python exporta con el motor C++ (`genclip render`).
  - **M2 (~8–10 meses):** editor C++ de escritorio usable; lo que aún no está portado (IA) se le pide al backend Python por su API REST actual.
  - **M3 (~12–15 meses):** se retira Python; instaladores para Windows, macOS y Linux.
  - **M4 (~15–19 meses):** Android e iOS.
- **Fuera de alcance por ahora (§6):** Motion Studio y el editor de Paper. Paper ya es un efecto del catálogo (`paper_cutout`) y se porta con los demás efectos.
- **Coste honesto:** ~92 k líneas de lógica (42,5 k de Python + 50 k de JS/JSX; Motion y el editor de Paper no se portan), lo que supone **≈ 50–71 semanas** de trabajo dedicado. Lo que protege el plan es que la paridad se **mide con fixtures generadas desde Python**: los 20,6 k de líneas de tests no se reescriben a mano.

---

## 1. Punto de partida (medido el 2026-10-06)

| Área | Tamaño | Notas |
|---|---|---|
| Backend `backend/app` | 42 469 líneas Python | 164 rutas FastAPI y 50 tools MCP; ~30 módulos llaman a `ffmpeg`/`ffprobe` como subproceso |
| Tests backend | 20 564 líneas | Lógica pura en su mayoría (ops, layout, máscaras, filtros…) |
| Frontend `frontend/src` | 50 095 líneas JS/JSX/CSS | editor 24 k · `lib/` 6,3 k (**espejo** de la lógica Python) · paper 5 k · motion 3,6 k · fx 3 k |
| JS que el backend ejecuta para renderizar | `stick.js` (905), `motion/runtime.js` (494) + GSAP, `fx_assets/runtime.bundle.js` (17,8 k empaquetado) | Corre en Chromium headless mediante Playwright (`fx_bake.py`, `motion/renderer/hyperframes.py`) |
| Modelos locales | RVM (mobilenet/resnet50), BiRefNet, U²-Net(p), SAM 2.1 (enc/dec), YuNet, Kokoro v1.0, voces Piper, Whisper (faster-whisper/CTranslate2) | Todos ONNX salvo Whisper |
| Dependencias Python clave | numpy, opencv, Pillow, faster-whisper, kokoro-onnx, onnxruntime, playwright, yt-dlp, google-genai, openai, mcp, FastAPI | Todas tienen equivalente C/C++ **salvo yt-dlp y Playwright** |

**Problema estructural que la migración resuelve:** hay pares espejo que deben coincidir al píxel (`clipMask.js`↔`clip_mask.py`, `objectTrack.js`↔`object_track.py`, `clipFilters.js`↔`clip_filters.py`, `shared/bg_chroma_golden.json`…). A eso se suman tres caminos de render distintos (canvas 2D/WebGL, FFmpeg + libass y Chromium para fx/motion). Ver las invariantes en la memoria *overlay-export-invariantes*.

---

## 2. Principios

1. **Un motor y una implementación.** Cada regla (keyframes, layout, máscaras, filtros, texto) existe una sola vez, en C++.
2. **Mismo formato de proyecto.** GenClipPro abre los proyectos actuales (`schema_version` 5, la misma estructura de `data/`) sin convertirlos. Durante la transición las dos apps pueden abrir los mismos proyectos (con un fichero de bloqueo para que no escriban a la vez).
3. **Paridad medida.** Cada fase se cierra con fixtures doradas generadas desde la app Python (§7).
4. **Siempre hay algo usable.** Ninguna fase deja a Edu sin herramienta.
5. **Portabilidad desde el día 1.** El núcleo no depende de Qt, del SO ni de la GPU. Todo lo específico de cada plataforma vive en `platform/` y en los «providers» de IA y codecs.
6. **Rendimiento por diseño:** reloj de audio maestro, decodificación en hilos con lectura anticipada, caché de frames con presupuesto de RAM (como el `lruBytes` actual), proxies, preview a resolución reducida y repintado solo cuando algo cambia («dirty flag»).

---

## 3. Dispositivos objetivo

| Nivel | Plataformas | Alcance |
|---|---|---|
| **T1** | Windows x64 (tu máquina, NVIDIA) | Completo. Referencia de paridad. |
| **T2** | macOS (Apple Silicon), Linux x64 | Completo. CUDA → Metal/CoreML (mac) o Vulkan/CUDA (Linux). |
| **T3** | Android (arm64), iOS | Editor, export e IA «ligera» (RVM mobilenet, U²-Netp, Whisper tiny/base, Kokoro). **Sin descarga de YouTube** (las políticas de Play Store y App Store la prohíben). |
| T4 (opcional) | Linux headless | `genclip` CLI + servidor MCP para renderizar en un servidor. |
| T5 (opcional) | Web (WASM) | El núcleo y Skia (CanvasKit) compilan a WebAssembly. Puede estudiarse cuando todo lo anterior esté hecho. |

---

## 4. Arquitectura

```
┌───────────────────────────── apps ─────────────────────────────┐
│  studio (Qt Quick/QML + ViewModels C++)       cli (genclip)     │
└───────────────┬──────────────────────────────────────┬─────────┘
                │                                      │
┌───────────────▼──────────────┐   ┌───────────────────▼─────────┐
│ gc_agent  chat IA + MCP      │   │ gc_services  proyectos,     │
│ (JSON-RPC, :8000/mcp)        │   │ biblioteca, jobs, ajustes,  │
└───────┬──────────────────────┘   │ import, YouTube (sidecar)   │
        │                          └──────┬──────────────────────┘
┌───────▼───────┐ ┌────────────┐ ┌────────▼──────┐ ┌─────────────┐
│ gc_cloud      │ │ gc_ai      │ │ gc_render     │ │ gc_media    │
│ Gemini, OAI-  │ │ ORT, whis- │ │ compositor    │ │ FFmpeg libav│
│ compat, Azure,│ │ per.cpp,   │ │ Skia: preview │ │ decode/enc, │
│ Foundry, stock│ │ TTS, OpenCV│ │ = export      │ │ audio, caché│
└───────┬───────┘ └─────┬──────┘ └───────┬───────┘ └──────┬──────┘
        └───────────────┴────────┬───────┴────────────────┘
                        ┌────────▼────────┐   ┌──────────────┐
                        │ gc_core         │   │ gc_platform  │
                        │ modelo, JSON,   │   │ rutas, GPU,  │
                        │ ops, keyframes, │   │ permisos,    │
                        │ layout, undo    │   │ hilos        │
                        └─────────────────┘   └──────────────┘
```

### 4.1 Módulos

| Módulo | Responsabilidad | Dependencias |
|---|---|---|
| **gc_core** | `Timeline`/`Track`/`Clip`/`Project`, (de)serialización JSON + migraciones, todas las ops de `timeline_ops` (sin solapes, pista magnética, track stack), keyframes, curvas y easing, `clip_layout`/`reframe_math`, velocidad, congelar fotograma, undo/redo (patrón comando) | Solo STL + `nlohmann/json` (o `glaze`). Sin Qt. |
| **gc_media** | Demux y decode con hwaccel (D3D11VA/NVDEC, VideoToolbox, MediaCodec, VAAPI), seek exacto, caché de frames con presupuesto, proxies, miniaturas y formas de onda, mezcla de audio, `loudnorm` a −14 LUFS, filtros de sonido mediante libavfilter, encode (NVENC/AMF/QSV, VideoToolbox, MediaCodec, x264 u OpenH264), reproducción de audio con miniaudio | FFmpeg (libav* **dentro del proceso**, nunca CLI: iOS no permite lanzar procesos) |
| **gc_render** | Compositor de capas: vídeo, imagen, GIF, texto, figuras, máscaras, modos de fusión, filtros y ajustes de color (SkSL), chroma key, capa de ajuste, barras de cine, transiciones y efectos del catálogo (paper incluido: es el efecto `paper_cutout`). Dibuja sobre una superficie de cualquier tamaño: **el preview a ½ o ¼ de resolución y el export a resolución completa usan el mismo código.** | Skia (GPU: D3D/Metal/Vulkan/GL; CPU raster como alternativa), HarfBuzz + SkParagraph |
| **gc_ai** | Eliminar fondo (RVM, BiRefNet, U²-Net) con caché en 2 niveles, SAM 2.1 + seguimiento + pincel, Whisper (palabras con timestamps), TTS (Kokoro, Piper), YuNet + reframe, tracking LK + RANSAC, beats (FFT), `model_manager` (descarga en el primer uso y presupuesto de RAM) | ONNX Runtime C++ (CUDA, DirectML/WinML, CoreML, NNAPI/XNNPACK, CPU), whisper.cpp, OpenCV (core/imgproc/video/objdetect/dnn), espeak-ng |
| **gc_cloud** | Clientes REST y SSE: Gemini, OpenAI-compatible (openrouter/groq/cerebras/mistral/hf), Azure (TTS/STT/Vision/imagen), Foundry, Pexels/Giphy/Pixabay/Unsplash | libcurl (+ cpr), JSON |
| **gc_services** | Proyectos, carpetas del inicio, biblioteca y colecciones, ajustes y API keys, jobs (pool de hilos con progreso y cancelación), import, YouTube (sidecar `yt-dlp` solo en escritorio) | gc_core, gc_media |
| **gc_agent** | Agente de chat con tools en el mismo proceso y servidor MCP en `:8000/mcp` (Streamable HTTP) con el **mismo contrato** que hoy: los 50 tools, DTO semántico y errores estructurados. Un único registro de tools, como `registry.py`. | cpp-httplib o Boost.Beast, gc_cloud |
| **gc_platform** | Rutas de datos por SO, detección de GPU y sondas reales (como `gpu.py`), permisos móviles, almacenamiento | Código condicional por SO |
| **studio** | UI QML; la lógica vive en ViewModels `QObject`. El preview es un `QQuickItem` que muestra el frame de gc_render. | Qt 6 (Quick, QuickControls2) |
| **cli** | `genclip render|frame|probe|parity` | Todo lo anterior salvo Qt |

### 4.2 Decisiones técnicas y motivo

- **Skia como compositor.** El Canvas 2D de Chrome *es* Skia, así que `canvas.js`, `lib/*.js` y los efectos de `fx/` se portan casi 1:1:

  | Canvas 2D (JS) | Skia (C++) |
  |---|---|
  | `drawImage` | `drawImageRect` |
  | `globalCompositeOperation` | `SkBlendMode` |
  | `ctx.filter` | `SkColorFilter` / `SkImageFilter` |
  | `Path2D` | `SkPath` |

  El preview se verá como hoy. Además, Skia trae Skottie (Lottie) y un módulo SVG.
- **FFmpeg solo para lo que hace bien:** decodificar, codificar, audio y algún filtro puntual. Ya no compone la imagen: el `filter_complex` no sirve para un preview interactivo y es justo el origen de la duplicación.
- **Qt Quick para la UI.** Es el único toolkit C++ maduro que cubre Windows, macOS, Linux, Android e iOS con una sola base de código. QML es declarativo, mentalmente parecido a JSX, y CLion lo soporta.
- **Modelo de hilos:** el hilo de UI solo muestra; el hilo de render recibe **snapshots inmutables** de la timeline (con estructura compartida); hay hilos de decodificación por clip visible y el reloj maestro es el de audio. Las ediciones entran como **ops** (las mismas que usa el MCP), lo que da undo/redo y diffs en lugar de enviar la timeline entera, como pasa ahora con el `PUT` y las respuestas del MCP.
- **Errores:** `std::expected<T, gc::Error>` entre módulos; en el MCP se mapea al enum de 8 códigos actual.
- **Prompts y plantillas como datos.** Los prompts de `scene_direction.py`, «Generar escena» y «Generar recurso» se sacan a `resources/prompts/*.md` y `resources/templates/*.json`. Son contenido del producto, no código, y así ambas apps los comparten durante la transición.

---

## 5. Mapa de migración (Python/JS → C++)

Dificultad: S (días) · M (1–3 semanas) · L (1–2 meses) · XL (más de 2 meses).

| Hoy | C++ | Dif. | Notas |
|---|---|---|---|
| `schemas.py`, `migrations.py` | gc_core/model, json | M | Los campos `dict` sueltos (`style`, `effects`, `transform`, `shape`, `masks`, `bg_removal`, `vfx`, `animations`, `keyframes`) se guardan primero como `json` y se tipan poco a poco. Punto de partida: `model_json_schema()` de Pydantic. |
| `timeline_ops.py`, `no_overlap.py`, `track_stack.py`, `timeline_store.py`, `timeline_history.py` | gc_core/ops | M | Paridad con fixtures (entrada, op, salida). |
| `clip_keyframes`, `clip_anim`, `clip_layout`, `reframe_math`, `clip_speed`, `clip_attrs`, `freeze` **y sus espejos en `lib/*.js`** | gc_core/anim, layout | M | Desaparece el espejo JS↔Python. |
| `compose.py`, `compose_clip.py`, `compose_layout.py`, `clip_filters`, `clip_blend`, `clip_mask`, `clip_adjust`, `clip_fx`, `shapes`, `text3d` + `canvas.js`, `warp3d.js`, `vfxLayer.js` | gc_render | **XL** | El corazón de la migración. |
| `text_ass.py` (libass) + `textstyles.js` | gc_render/text | L | Un solo motor de texto: SkParagraph + HarfBuzz para estilos, contorno, karaoke por palabra, 3D y máscara. Puede haber pequeñas diferencias con libass (§9). |
| `fx_bake.py` + `frontend/src/fx` (Chromium) | gc_render/fx (SkSL) | L | Se porta efecto a efecto; el catálogo indica cuáles están listos. |
| `fx/effects/paper.js` (efecto `paper_cutout`) | gc_render/fx | M | Uno más de los efectos del catálogo (F4). Sustituye al editor de Paper (§6). |
| `features/paper` (editor de Paper) | — | — | No se porta (§6). |
| `motion/` (HTML/GSAP + Playwright, con `stick.js` y `stick.py`) | — | — | Fuera de alcance por ahora (§6). |
| `transcribe.py` (faster-whisper) | whisper.cpp | M | Los timestamps por palabra difieren algo: validar con fixtures. |
| `tts.py`, `piper_tts.py`, kokoro-onnx | ORT + espeak-ng | M | espeak-ng es **GPLv3** (§10). |
| `gemini_tts`, `azure_*`, `foundry*`, `media_search`, `material_ai` | gc_cloud | S cada uno | REST puro. |
| `bg/` (providers, sam, sam_track, cutout_export), `clip_bg.py` | gc_ai/matting | L | Caché de alfa en espacio FUENTE, como hoy. |
| `detect.py` (YuNet), `reframe.py`, `object_track.py` (LK + RANSAC) | OpenCV C++ | S/M | Misma API de OpenCV. |
| `beats.py`, `sound_design.py`, `audio_fx.py`, `sfx.py`, `clip_audio.py` | gc_media/audio | M | FFT con pocketfft o kissfft; ecualizador, compresor y reverb con libavfilter en tiempo real. |
| `heatmap.py`, `ytdlp.py`, `youtube_*.py`, `yt_history.py` | gc_services/youtube | S | Sidecar con el binario oficial `yt-dlp` (exe/macos/linux) descargado al usarlo. No existe en móvil. |
| `jobs.py` | gc_services/jobs | S | |
| `main.py` (164 rutas) | — | — | Desaparece como frontera interna. |
| `mcp_server/` (50 tools), `ai/` (agente) | gc_agent | M | JSON-RPC propio (los SDK MCP de C++ todavía están verdes). Mismo endpoint, para que Claude Desktop y `mcp-remote` no cambien de configuración. |
| `projects`, `library`, `collections`, `storage`, `settings`, `stick_library` | gc_services | M | Los diálogos tkinter de `storage.py` pasan a `FileDialog` de Qt. |
| `gpu.py`, `model_manager.py`, `diagnostics.py` | gc_platform, gc_ai | S | |
| UI React (editor 24 k, ai, audio, direction, projects) | studio (QML) | **XL** | Se porta por pantallas, en orden de uso (F5). Sin paper ni motion (§6). |

---

## 6. Fuera de alcance: Motion y el editor de Paper. Los efectos en el inspector

*Decidido el 2026-10-08.*

### 6.1 Motion Studio: fuera por ahora

Motion Studio no se porta todavía: ni el generador de HTML, CSS y GSAP, ni su render en Chromium (`motion/`, Playwright), ni su stick (`motion/stick.js`, `stick.py`). Las composiciones de Motion que ya hay en los proyectos se conservan tal cual al leer y escribir (el almacén de proyectos ya lo hace), pero GenClipPro no las edita ni las genera. El puente REST de F5 tampoco las pide a Python.

Se volverá a ver cuando el editor esté hecho. Las dos opciones que se estudiaron siguen valiendo:
- **A. Sidecar Chromium headless por CDP** (solo escritorio, ~100 MB). Mantiene el generador y los prompts tal cual.
- **B. Formato nativo** (JSON declarativo con tweens, o Lottie con Skottie). Un solo motor, que también funciona en móvil, pero obliga a rehacer el generador.

Los **personajes stick de la biblioteca** (`stick_library`, el menú «Agregar Stick») no son Motion: son clips normales de vídeo o imagen con croma, y siguen en F1.

### 6.2 Paper: ya es un efecto

El aspecto de Paper ya vive en el catálogo de efectos como **`paper_cutout`** («Papel recortado», `fx/effects/paper.js`, portado de `features/paper/paperRender.js`). Sus parámetros cubren lo que hacía el editor de Paper: borde rasgado (grosor, rugosidad, detalle), arrugas y su fusión, sombra, vibración stop-motion y el pliegue de apertura y cierre. Por eso:
- **El editor de Paper** (`features/paper`: lienzo, capas y panel de propiedades propios) **no se porta**.
- `paper_cutout` se porta en F4 como un efecto más del catálogo (fx → SkSL).
- Las opciones de cómo aparece y desaparece el papel (el pliegue, la vibración) son **parámetros del efecto**, no una sección aparte.
- Lo que el editor tenía y el efecto no (las letras recortadas de `assets/alfnum`, el borrador y el fondo propio) no se porta. Si hace falta, se valorará como efecto o como herramienta más adelante.

### 6.3 Los efectos en el inspector

En vez de una sección por cada tipo de efecto, el inspector trata todos los efectos del clip igual (F5):
- Al añadir un efecto a un clip, aparece la pestaña **Efectos** en las propiedades. Si el clip no tiene ninguno, la pestaña no se muestra.
- Dentro de **Efectos**, cada efecto añadido tiene su **subpestaña** (con su nombre). Al pulsarla se ven las propiedades de ese efecto.
- Esas propiedades salen de los `params` del catálogo (`toggle`, `range`, `select`, `color`…): la UI se genera desde el catálogo, sin pantallas hechas a mano por efecto. Así, un efecto nuevo del catálogo aparece en el inspector sin tocar la UI.
- Cada subpestaña también deja activar o desactivar el efecto, restablecer sus valores y quitarlo.
- **El orden importa:** los efectos se aplican uno tras otro, en el orden de la lista `vfx` del clip. Las subpestañas siguen ese orden y se pueden reordenar arrastrándolas.
- Si un clip acumula muchos efectos, las subpestañas pueden no caber. En ese caso se pasa a una lista vertical de tarjetas plegables, con el mismo contenido.

---

## 7. Estrategia de paridad y pruebas

**Fixtures doradas desde Python.** `GenClipPro/tools/fixtures/export_fixtures.py` se ejecuta con el Python de video-yt: importa su `app` y guarda lo que devuelve. Así video-yt no se toca. Genera en `GenClipPro/tests/fixtures/`, con git-lfs para los binarios:

| Tipo | Contenido | Criterio en C++ |
|---|---|---|
| modelo | todos los `project.json` y timelines reales | round-trip sin pérdida (diff JSON semántico = 0) |
| ops | `(timeline, op, args) → timeline'` grabados al pasar los tests actuales + `shared/fragmentation_cases.json` | igualdad exacta |
| anim/layout | `clipPropsAt`, `destRectOnFrame`, `cropWindow`… en N instantes | `|Δ| < 1e-6` |
| render | `compose.render_frame` en 5–10 instantes por proyecto (PNG) | PSNR ≥ 35 dB y SSIM ≥ 0,98; texto con revisión visual |
| audio | LUFS integrado, picos y hash de la forma de onda | ±0,5 LU |
| IA | máscaras (IoU) y palabras (Δ inicio/fin) | IoU ≥ 0,95; Δ ≤ 80 ms |
| MCP | `tools/list` + respuestas de cada tool sobre un proyecto de prueba | igualdad tras normalizar ids y fechas |

- **Tests unitarios:** Catch2 o GoogleTest. **Benchmarks:** Google Benchmark (export s/min, fps del preview, RAM máxima) comparados con la app Python.
- **CI:** GitHub Actions en Windows, macOS y Linux desde F0; Android e iOS desde F10. ASan/UBSan con clang.
- **Regla de congelación:** desde F3, video-yt **solo recibe correcciones de bugs**. Si se añade algo nuevo, se añade con su fixture y se apunta en el backlog de GenClipPro. Sin esta regla, la migración no converge.

---

## 8. Fases

Las estimaciones suponen 1 persona con Claude, a dedicación alta.

| Fase | Contenido | Semanas | Hito |
|---|---|---|---|
| **F0** Cimientos | toolchain, estructura, vcpkg, CI, tests | 1–2 | |
| **F1** Modelo y formato | gc_core/model + JSON + migraciones + fixtures | 3–4 | |
| **F2** Lógica de edición | ops, sin solapes, magnética, keyframes, layout, undo | 4–6 | |
| **F3** Media + export | gc_media + gc_render v1 + `genclip render` | 6–8 | **M1** |
| **F4** Render completo | texto, fx, transiciones, máscaras avanzadas, chroma | 6–8 | |
| **F5** Editor de escritorio | app Qt: proyectos, timeline, preview, inspector, export | 10–14 | **M2** |
| **F6** IA local | fondo, SAM, Whisper, TTS, tracking, beats | 6–8 | |
| **F7** Nube, agente, MCP | providers, chat, MCP :8000, YouTube, escena/recurso | 4–6 | |
| ~~**F8** Motion, Stick, Paper~~ | fuera de alcance por ahora (§6) | — | |
| **F9** Empaquetado escritorio | instaladores, modelos en el primer arranque, updates | 2–3 | **M3** |
| **F10** Móvil | Android + iOS | 8–12 | **M4** |
| | **Total** | **≈ 50–71** | |

### F0 — Cimientos (1–2 semanas)
- **Toolchain:** en Windows, cambiar de la MinGW incluida en CLion a **MSVC (Visual Studio 2022 Build Tools)**. ONNX Runtime, CUDA y los binarios precompilados de Skia y Qt están pensados para MSVC; con MinGW darán problemas. En macOS e iOS, Apple Clang; en Linux, Clang o GCC; en Android, Clang del NDK.
- **C++23** (como está en `CMakeLists.txt`), pero sin módulos ni nada que no exista en las tres STL (MSVC STL, libc++ de Apple/NDK, libstdc++). Se usan `std::expected`, `std::span`, ranges y `std::format`.
- **Build:** CMake + Ninja + `CMakePresets.json` (`win-msvc-{debug,release}`, `linux`, `macos`, y más adelante `android-arm64` e `ios`). Conviene bajar `cmake_minimum_required` a `3.28...4.3`, porque los runners de CI y el NDK no siempre traen CMake 4.3.
- **Dependencias:** manifiesto `vcpkg.json` con baseline fijado (CLion integra vcpkg). Qt se instala con su instalador o con aqtinstall, no con vcpkg.
- **Calidad:** clang-format, clang-tidy, spdlog, `git init` + `.gitignore` (`cmake-build-*`, `.idea/workspace.xml`) y git-lfs para las fixtures.
- **Salida:** `genclip --version` compila en los 3 SO en CI con un test verde.

### F1 — Modelo y formato (3–4 semanas)
- Structs de `Project`, `Timeline`, `TimelineTrack`, `TimelineClip`, `Reframe`, `Keyframe`, `Transcript`, `Word`…, con valores por defecto **idénticos** a los de Pydantic.
- Migraciones de `schema_version` hasta la 5. Escritura estable (mismo orden de claves) para que los diffs sean legibles.
- `tools/fixtures/export_fixtures.py`. Lectura de `data/` (`projects.json`, grupos, biblioteca).
- **Salida:** el 100 % de tus proyectos reales hacen round-trip sin pérdida.

### F2 — Lógica de edición (4–6 semanas)
- Todas las ops de `apply_op`, `no_overlap`, pista magnética, track stack, keyframes y curvas, `clip_layout`, `reframe_math`, velocidad y reverse, congelar, pegar atributos y desactivar.
- Undo/redo con comandos y API de ops con diff.
- **Salida:** 100 % de paridad con las fixtures de ops y de animación/layout.

### F3 — Media + export C++ (6–8 semanas) → **M1**
- **gc_media:** decode con hwaccel, seek exacto, caché, mezcla de audio y loudnorm, encode por hardware con alternativa por software (con el mismo tipo de sonda real que `gpu.py`).
- **gc_render v1:** vídeo, imagen y GIF; pose y transform; recorte y reframe; opacidad, fusión, filtros y ajustes; máscaras básicas; figuras; texto básico.
- **CLI:** `genclip render <proyecto> <timeline> out.mp4 --progress` y `genclip frame … --at t`.
- **Strangler:** en video-yt, ajuste «Motor de export: Python | C++» que llama a `genclip render` como subproceso (con progreso por stdout), para usar el motor nuevo en tus shorts reales.
- **Salida:** las fixtures de render pasan el umbral y el export es igual de rápido o más que el actual.

### F4 — Render completo (6–8 semanas)
- Texto completo (temas de subtítulos, karaoke, contorno, sombras, 3D, máscara en texto), appear/exit, looks, capa de ajuste, barras de cine, film, contorno del sujeto, chroma key, efectos animados (fx → SkSL; entre ellos `paper_cutout`, que sustituye al editor de Paper, §6.2), alfa de «Eliminar fondo» leído desde la caché existente y marco de plataforma (solo en preview).
- **Salida:** todo lo descrito en `docs/` de video-yt se exporta desde C++. libass y Chromium-fx dejan de usarse en el export.

### F5 — Editor de escritorio Qt (10–14 semanas) → **M2**
- **Orden de pantallas:**
  1. Inicio (proyectos y carpetas).
  2. Editor: preview + timeline (arrastre sin solapes, pista magnética, zoom, beats y marcadores).
  3. Inspector: transform, texto, filtros, máscaras, modal de recortar y la pestaña **Efectos**, con una subpestaña por efecto añadido y sus propiedades generadas desde el catálogo (§6.3).
  4. Biblioteca y materiales.
  5. Export con progreso.
  6. Ajustes.
  7. Atajos de teclado.
- **Reproductor:** reloj de audio, preview a ½ o ¼ de resolución, proxies generados al importar, repintado solo cuando cambia algo y 60 fps en el cabezal sin re-render del árbol de UI (justo lo que hoy cuesta en React).
- **Puente transitorio:** lo que aún no está portado (eliminar fondo, Whisper, TTS, chat, YouTube) se pide al backend Python **por su API REST actual** (`:8000`), como cliente HTTP. Se retira en F6–F7. Motion no pasa por el puente: queda fuera (§6.1).
- **Salida:** editas y exportas un short completo sin abrir la UI React.

### F6 — IA local (6–8 semanas)
- ORT con un EP por plataforma; RVM, BiRefNet y U²-Net con caché en 2 niveles; SAM 2.1 con 4 herramientas, seguimiento (`sam_track`) y pincel con análisis en segundo plano.
- whisper.cpp (CUDA, Metal o Vulkan) con palabras; Kokoro y Piper (espeak-ng); YuNet + reframe; tracking LK; beats.
- `model_manager` con descarga bajo demanda y presupuesto de RAM.
- **Salida:** fixtures de IA dentro del umbral y benchmarks iguales o mejores que Python.

### F7 — Nube, agente y MCP (4–6 semanas)
- gc_cloud (Gemini, OpenAI-compatible, Azure ×3 regiones con alternativa entre ellas, Foundry, stock) con SSE.
- Chat IA con streaming y confirmaciones; servidor MCP con los 50 tools y los resources (`capabilities://`, `help://`, `config://`, `project://`).
- YouTube mediante el sidecar `yt-dlp`; Dirección de escena, Generar escena y Generar recurso con los prompts movidos a `resources/`.
- **Salida:** las fixtures MCP pasan y Claude Desktop se conecta sin tocar su configuración.

### F8 — Fuera de alcance por ahora
- Motion Studio (con su stick) no se porta todavía (§6.1). Paper ya es el efecto `paper_cutout` y va en F4 (§6.2). Se mantiene el número de las fases siguientes.

### F9 — Empaquetado escritorio (2–3 semanas) → **M3**
- CPack: Windows con MSIX o NSIS/WiX; macOS `.dmg` firmado y notarizado; Linux AppImage o Flatpak.
- Los modelos se descargan en el primer uso (sustituye a `setup/nucleo.py`). Actualizaciones e informes de fallos (Crashpad).
- **Se retira la app Python.**

### F10 — Móvil (8–12 semanas) → **M4**
- Qt for Android/iOS; UI táctil (layouts QML adaptativos y gestos en timeline y preview).
- Almacenamiento con *scoped storage* y selector de Fotos; codecs por hardware (MediaCodec, VideoToolbox); ORT con NNAPI/XNNPACK o CoreML; modelos «lite»; límites de RAM.
- Export en segundo plano: *foreground service* en Android; en iOS es limitado.
- Sin YouTube.

---

## 9. Riesgos y mitigación

| Riesgo | Impacto | Mitigación |
|---|---|---|
| Alcance (~92 k líneas) y duración de más de un año | Alto | Hitos M1 y M2 usables pronto; congelación de video-yt; puente REST en F5. |
| Doble mantenimiento durante la transición | Alto | Regla de congelación (§7). |
| Texto de Skia ≠ libass (los proyectos viejos pueden verse distinto) | Medio | Revisión visual en F4; desde M1, el render C++ pasa a ser la referencia y se regeneran las doradas. |
| Preview en tiempo real con varias pistas 1080p (sobre todo en móvil) | Medio | Proxies, preview a resolución reducida, decode por hardware y caché con presupuesto. |
| Toolchain en 5 plataformas (Skia, Qt, ORT, FFmpeg) | Medio | Binarios precompilados, vcpkg con baseline y CI desde F0. |
| Whisper C++ ≠ faster-whisper en tiempos por palabra | Bajo-medio | Fixtures con tolerancia y opción Azure STT. |
| YouTube en tiendas | — | Solo en escritorio. |
| NVENC bloqueado por el driver en tu máquina | Bajo | Mismo patrón de sonda real + alternativa por software que hoy. |

---

## 10. Licencias (decidir antes de distribuir)

- **Qt:** LGPLv3. Enlazado dinámico en escritorio sin problema. **iOS obliga a enlazar estático**, lo que exige cumplir la LGPL (entregar los objetos para re-enlazar) o pagar licencia comercial.
- **FFmpeg:** LGPL. Pasa a **GPL** si se incluyen x264 o x265 (alternativas: OpenH264 y los encoders por hardware).
- **espeak-ng (Kokoro, Piper):** **GPLv3**, lo que arrastra a GPL toda la app distribuida. Alternativas: un G2P propio, misaki o voces en la nube.
- **Sin problema:** Skia (BSD), ONNX Runtime (MIT), whisper.cpp (MIT), OpenCV (Apache 2) y miniaudio (dominio público o MIT-0).
- Para uso personal da igual. Si algún día se publica, hay que elegir entre GPL y evitar esos componentes.

---

## 11. Estructura del repositorio GenClipPro

```
GenClipPro/
├─ CMakeLists.txt          # raíz: opciones, subdirectorios
├─ CMakePresets.json       # win-msvc / macos / linux / android / ios
├─ vcpkg.json              # dependencias con baseline fijado
├─ cmake/                  # warnings, sanitizers, helpers por plataforma
├─ src/
│  ├─ core/                # gc_core
│  ├─ media/               # gc_media
│  ├─ render/              # gc_render
│  ├─ ai/                  # gc_ai
│  ├─ cloud/               # gc_cloud
│  ├─ services/            # gc_services
│  ├─ agent/               # gc_agent (chat + MCP)
│  └─ platform/            # gc_platform
├─ apps/
│  ├─ cli/                 # genclip
│  └─ studio/              # app Qt Quick (QML + ViewModels)
├─ resources/              # fuentes, LUTs, prompts, plantillas, sfx
├─ tests/
│  ├─ unit/
│  ├─ parity/              # contra fixtures de Python
│  └─ fixtures/            # generadas desde video-yt (git-lfs)
└─ docs/
   └─ PLAN_MIGRACION_CPP.md
```

Espacio de nombres `gc::core`, `gc::media`… Cada módulo es una biblioteca CMake estática con su interfaz pública en `include/gc/<módulo>/`.

---

## 12. Primeros pasos (semana 1)

1. `git init` en GenClipPro con `.gitignore` y git-lfs.
2. En CLion: *Settings → Build → Toolchains* → añadir **Visual Studio** y ponerlo por defecto; activar la integración de vcpkg.
3. Crear la estructura de §11, `CMakePresets.json` y un `vcpkg.json` mínimo (`nlohmann-json`, `spdlog`, `catch2`).
4. Dar a `gc_core` su primer tipo (`Timeline`) con un test que carga un `project.json` real copiado a `tests/fixtures/`.
5. Crear `tools/fixtures/export_fixtures.py`, empezando por modelo y ops.
6. CI con GitHub Actions para Windows, macOS y Linux.

---

## 13. Decisiones abiertas (a confirmar)

| # | Decisión | Recomendación | Alternativas |
|---|---|---|---|
| 1 | Orden de dispositivos | Escritorio primero (T1→T2) y móvil al final (F10) | Móvil desde F5: obliga a diseñar la UI táctil desde el principio y retrasa M2 |
| 2 | UI | **Qt Quick/QML** | React en WebView con núcleo C++: más rápido de entregar, pero meter un preview nativo dentro del WebView es difícil. Flutter + C++ por FFI: buena en móvil, pero la UI no sería C++ |
| 3 | Motion Studio | **Aplazada (2026-10-08):** fuera de alcance por ahora (§6.1) | Más adelante: sidecar Chromium (A), formato nativo (B) o los dos |
| 4 | Licencia de distribución | Decidirla antes de F9 | GPL frente a evitar x264 y espeak-ng |
| 5 | Congelar video-yt | Desde F3, solo correcciones de bugs | Doble implementación con fixture obligatoria |

---

## 14. Registro de avance

### 2026-10-06 — Arranque (F0 + modelo de F1)

**Decisiones de §13 aplicadas** (por defecto, las recomendadas): escritorio primero, Qt Quick, Motion con sidecar Chromium + spike, congelar video-yt desde F3. La licencia queda para antes de F9.

**F0 — hecho:**
- Repositorio `git` (rama `main`) con `.gitignore`, `.gitattributes` (git-lfs para binarios de fixtures), `.clang-format` y `.editorconfig`.
- CMake ≥ 3.28 con presets (`mingw-*`, `msvc-*`, `msvc-vcpkg-*`, `linux-debug`, `macos-debug`, `ci`).
- Dependencias: `find_package` primero y, si no están, FetchContent con hash verificado (nlohmann/json 3.12.0, Catch2 3.9.1). `vcpkg.json` ya preparado para F3.
- Warnings estrictos en nuestro código (`gc_set_warnings`). Con GCC 15: 0 warnings.
- CI de GitHub Actions para Windows (MSVC), Linux (GCC) y macOS (Apple Clang). **Sin ejecutar todavía:** falta el remoto en GitHub.
- `genclip` (CLI) con argumentos y salida en UTF-8 en Windows.

**F0 — pendiente: MSVC.** Visual Studio Community 2026 está en `D:\visual` (MSVC 14.50), pero **falta `vcvarsall.bat` y el Windows SDK**, así que no puede enlazar. Arreglo: *Visual Studio Installer → Modificar → «Desarrollo para el escritorio con C++»*, marcando un Windows 11 SDK. Mientras tanto se compila con la MinGW de CLion, que basta para el núcleo puro (F1–F2). MSVC será imprescindible en F3.

**F1 — modelo y formato hechos:**
- `gc_core`: `model.hpp` (espejo de `schemas.py`: Project, ClipInfo, Transcript, AudioInfo, ImageInfo, Timeline, TimelineTrack, TimelineClip, Reframe, Keyframe, TrackPoint, FaceTrack, Word, ProjectGroup).
- Lectura con las coerciones lax de Pydantic v2, medidas en video-yt (2.13.4); errores con ruta (`timeline.clips[3].reframe.zoom`).
- Migraciones v1→v5 y `resolve_text_role`; `_project_from_dict` (id desde index).
- Escritura `Model` (= `model_dump()`) y `Preserve` (conserva las claves desconocidas que `save_timeline` deja en el archivo).
- Fixtures: 39 casos sintéticos (13 de error, comparando también la ruta del error con la de Pydantic) y tus 18 proyectos reales + 2 carpetas (979 clips en timelines).
- **Resultado: 21 tests y 272 aserciones en verde. Paridad exacta** (tipos, valores y orden de claves) con los 18 proyectos. Una prueba de mutación confirmó que el comparador detecta `1.0` → `1`.
- Compatibilidad en las dos direcciones: Python lee el `projects.json` que escribe `genclip normalize --preserve` y obtiene los mismos 18 proyectos.

**Siguiente (resto de F1 → F2):**
1. Instalar el Windows SDK y pasar a `msvc-debug` (y activar `GC_WARNINGS_AS_ERRORS` en el CI de Windows).
2. Biblioteca, colecciones y ajustes de `data/` (`library`, `settings.json`) en `gc_core`.
3. F2: fixtures de ops (`timeline_ops.apply_op`: entrada, op y salida) grabadas desde los tests de video-yt, y port de `timeline_ops`, `no_overlap`, `track_stack` y keyframes.

### 2026-10-07 — F2 en marcha: motor de edición

**Arquitectura** (detallada en [ARQUITECTURA.md](ARQUITECTURA.md)):
- documento inmutable con estructura compartida (`Cow<T>`);
- toda edición es un comando que entra por `apply_command` (operación pura → sin solapes → validación);
- `EditSession` con snapshots, deshacer/rehacer, checkpoints, grupos (un paso de deshacer por arrastre), observadores y revisión.

**Hecho:**
- **Reglas** (`timeline_rules`): tipos de clip ↔ pista, velocidad y duración, pila de capas (`display_order`, `reorder_tracks`) y validación.
- **`no_overlap`:** *settle* (empujar si crece, subir de pista si cae encima, pista magnética).
- **13 comandos:** `add_track`, `reorder_track`, `rename_track`, `remove_track`, `link_tracks`, `unlink_track`, `add_clip`, `move_clip`, `remove_clip`, `split_clip`, `duplicate_clip`, `set_project_format` y `set_markers`.
- **Semántica de Python** donde la paridad lo exige: `round()` exacto, `str.strip`/cortes por carácter, `float()`/`int()`, kwargs.
- **Ids inyectables** (`IdSource`); en las fixtures, la misma secuencia que en Python.

**Verificación:**
- **Fixtures:** las genera el `apply_op` REAL de video-yt (disco en memoria, `uuid4` determinista). Son 88 casos sintéticos (38 de error) y 706 operaciones sobre tus 18 timelines reales.
- **Resultado:** 36 tests y 2808 aserciones en verde, en Debug y en Release, con 0 warnings.
- **Prueba de mutación:** detecta los cambios de valor y de orden.
- **Velocidad:** una edición completa en la timeline real de 167 clips tarda **≈ 90 µs** en C++ frente a ≈ 49 ms en video-yt.

**Pendiente de F2:**
1. Comandos de propiedades de clip: `update_clip`, `set_clip_speed` (es el que ejercita el «crece → empuja», que aún no se prueba), opacidad, transición, fusión y volteo.
2. Comandos de efectos y animación: efectos, filtros, máscaras, keyframes, `animate_clip` y `freeze_frame`.
3. Comandos compuestos: subtítulos (`fragment.py`), recetas, *sound design*, barras de cine, capa de ajuste y `replace_media`.
4. Matemática de animación (`clip_keyframes`, `clip_anim`, `clip_layout`, `reframe_math`), que también usará la evaluación de F3.
5. Persistir el historial y el autosave.

### 2026-10-07 — F2, 2ª tanda: propiedades, encuadre, audio y animación

**Hecho:**
- **19 comandos más (32 en total):**
  - `update_clip` y sus setters: opacidad, velocidad, transición, rol de texto, fusión, volteo, posición/duración, nota y desactivar;
  - `reframe_clip` y `crop_clip`;
  - `set_clip_keyframes`, `set_clip_beats`, `set_clip_masks` y `set_clip_effects`;
  - `set_clip_volume` (fundidos), `set_track_audio` y `set_clip_audio_fx` (rampa animada);
  - `replace_media`.
- **Animación** (`keyframes.hpp`, `masks.hpp`): curvas (ease, cúbicas, rebote, bézier, *hold*), interpolación por propiedad, colores, estilo de texto animado, máscaras normalizadas y animadas, `upsert_keyframe_at` y fundidos de volumen. Es la base de la evaluación de F3.
- **Compilación:** `-ffp-contract=off` en GCC/Clang, para dar los mismos bits que Python también en ARM.

**Verificación:**
- **Edición:** 176 casos sintéticos (79 de error) y 1210 operaciones sobre las timelines reales. Por fin se prueba el «crece → empuja» (velocidad 0,5).
- **Animación:** 236 casos sintéticos y 733 clips reales muestreados. Sin tolerancia coinciden bit a bit 25 676 de 25 677 comprobaciones; la restante difiere en el último bit de un `pow()`.
- **Resultado:** 38 tests y 30 259 aserciones en verde. Las pruebas de mutación detectan un valor desplazado 1e-6, un color cambiado y un `700` ↔ `700.0`.

**Pendiente de F2:**
1. Efectos y animaciones de catálogo: `apply_effect`, `set_clip_animation` (necesitan `fx_catalog`) y `animate_clip` (`clip_motion`).
2. Material nuevo: `add_shape` (`shapes`), `add_subtitles` (`fragment.py`), `freeze_frame` (pose + `reframe_math`), `set_clip_bg_removal` (`clip_bg`).
3. Compuestos: `paste_clip_attributes` (`clip_attrs`), `follow_object`, `add_sound_design`, `add_cinema_bars`, `add_adjustment_layer` y `apply_recipe`.
4. Persistir el historial y el autosave.

### 2026-10-07 — F2, 3ª tanda: catálogo, congelar, subtítulos, figuras, fondo

**Hecho:**
- **8 comandos más (40 en total):** `apply_effect` y `set_clip_animation` (catálogo), `freeze_frame`, `add_subtitles`, `add_shape` (figuras y trazado libre), `add_cinema_bars`, `add_adjustment_layer` y `set_clip_bg_removal`.
- **Módulos nuevos de `gc_core`:**
  - `fx_catalog`: catálogo como recurso de la app en `resources/fx/catalog.json`, que el exportador sincroniza desde video-yt.
  - `pose`: `clip_pose` con pistas `anim` legadas y `frame_at` del encuadre.
  - `shapes`: `normalize_shape`, `path_shape` y barras de cine.
  - `bg`: `normalize_bg`, con auto/croma/contorno y trazos de SAM.
  - `captions`: fragmentación de subtítulos por número de palabras, con `words[]` y `origin`.

**Verificación:**
- **Casos:** 228 sintéticos (100 de error) y 1452 operaciones reales. Las reales incluyen subtítulos desde tus transcripciones, congelados, efectos, croma, figuras, barras y capas de ajuste.
- **Resultado:** 38 tests y 31 158 aserciones en verde, en Debug y en Release, con 0 warnings.
- **Mutación:** detecta la pose del congelado (1e-9), una palabra de subtítulo y un `1` ↔ `1.0` en una figura.
- **Fallo real corregido:** el compilador avisó de una referencia a un temporal en `captions.cpp`.

**Pendiente de F2:**
1. `animate_clip` (`clip_motion`), `paste_clip_attributes` (`clip_attrs`), `follow_object` (`object_track.follow_keys`), `add_sound_design` y `apply_recipe` (`recipes`, `recipe_layout`).
2. Persistir el historial y el autosave.

### 2026-10-07 — F2, 4ª tanda: encuadre y comandos compuestos *(hecha)*

**Autopiloto:** un hook `Stop` (`.claude/autopiloto/`) mantiene el trabajo en marcha entre turnos y pide una revisión al subagente `revisor-migracion` cada vez que se marcan tareas como hechas. Se enciende y se apaga con `.claude/autopiloto/activo`. Cada tanda verificada va en su propio commit local.

**Hecho:**
- **`clip_layout`** (espejo de `clip_layout.py`):
  - `crop_window` (reframe, keyframes cx/cy/zoom o recorte libre del overlay);
  - `source_point_to_output` (recorte → pose → volteo → giro), que es la base de `follow_object`;
  - `source_crop_px`, `dest_rect` y `dest_rect_even` (enteros pares con los empates al par de `round()`), además de `flip_filters`, que necesita el render de F3.
- `interp_items` pasa a ser pública, con el `_merge` de Python, y aparece `clip_props_over`.
- `tools/dev/verificar.sh` compila y pasa los tests en Debug y Release con el CMake de CLion.

**Verificación:**
- **Encuadre:** 2283 muestras sintéticas (48 de error) y 5875 de tus 235 clips reales (recorte, puntos y rectángulo de destino de cada transform).
- **Exactitud:** recortes y rectángulos coinciden bit a bit. Los puntos usan `sin`/`cos`, así que van con tolerancia de 1e-12: con tolerancia 0 fallan 115, porque la libm de MinGW no es la de CPython.
- **Mutación:** se detecta quitar el `fit=cover` de los keyframes, cambiar el redondeo al par por `round()`, quitar el volteo, tratar `x: 0` como valor y quitar el NaN → defecto de `interp_items`.
- **Resultado:** 41 tests y 44 238 aserciones en verde, en Debug y en Release, con 0 warnings.

**Revisión del subagente (APROBADO, solo hallazgos menores), ya aplicada:**
- `dest_rect_even` ya no puede desbordar `int64`: da error por encima de 4·10¹⁸ px.
- La precondición de dimensiones mayores que 0 queda documentada, con una tarea en F3.
- `json_diff` distingue 0.0 de −0.0.
- Cinco casos nuevos de encuadre: zoom fuera de 0,1–1, `pan_mode` vacío, recorte libre intermedio con keyframes y keyframes sin items. En total, 2908 muestras.
- Los puntos sin giro se comparan ahora exactos: los 115 que fallaban tienen giro, así que la causa es la libm.
- Las cifras de error del exportador están unificadas.

**Hecho (2): `clip_attrs` y `paste_clip_attributes`:**
- Los 14 grupos de «Pegar atributos», trabajando sobre el `model_dump()` y revalidando, como en Python.
- `merge_keyframes`: restringir a lo que cambia, constantes de un lado y remuestreo con `round(x, 6)`.
- El orden de uuid de Python: crop, vfx, mask y después los items.
- El orden aleatorio de `_constant_props` (un `set` de Python). Primero se ordenaban las props en el exportador y el test; tras la revisión, el exportador usa la misma función en orden de aparición y se compara con orden.

**Verificación (2):**
- **Casos:** 26 sintéticos (9 de error), entre ellos los dos lados constantes, el «crece → empuja» por velocidad, croma en todas sus combinaciones y estilo de texto animado. Además, 77 operaciones de pegar sobre tus timelines reales.
- **Mutación:** se detectan 10 de 13. Las otras 3 son equivalentes: el destino ya pegado no cambia `keyframes`, `keep_pitch` siempre llega como `bool` y una reordenación que no cambia la lógica.
- **Resultado:** 41 tests y 45 202 aserciones en verde, en Debug y en Release.

**Revisión del subagente (APROBADO, solo hallazgos menores), ya aplicada:**
- Ninguna fixture del repo pasaba por el remuestreo ni por la rama de constantes del origen: solo lo hacían tus datos locales. Hay casos nuevos, incluido uno con curva cúbica para que importe el `round(x, 6)`.
- También hay casos para el estilo de figura con relleno y trazo y para un item con t NaN.
- `_constant_props` en orden de aparición dentro del exportador. Ya no se ordenan las props en el test: se compara con orden.

**Hecho (3): `follow_object`:**
- `follow.hpp`: `follow_keys`. Lleva el centro del objeto y el extremo de su eje a la salida con `source_point_to_output`. A partir de ahí calcula el desplazamiento, la escala (`hypot`) y el giro (`atan2`, con el `%` de reales de Python, `py_mod`), todo con los redondeos de Python.
- El comando quita los keyframes del tramo seguido y escribe keyframes lineales con `upsert_keyframe_at`.
- Errores perezosos como en Python: un `min_gap` no numérico o un punto incompleto solo fallan si se llegan a usar.

**Verificación (3):**
- **Casos:** 25 sintéticos (16 de error), entre ellos un vídeo al revés a velocidad 2 con pose animada, giro que pasa de ±180, keyframes justo en el borde del tramo y clip con curva propia. Además, 81 operaciones sobre tus timelines reales.
- **Exactitud:** sale exacto también lo que pasa por `sin`/`cos`/`atan2`/`hypot`, porque se redondea a 3–5 decimales.
- **Mutación:** se detectan las 10.
- **Resultado:** 41 tests y 45 526 aserciones en verde, en Debug y en Release.

**Revisión del subagente (APROBADO, solo hallazgos menores), ya aplicada:**
- El caso «con giro» no bajaba de −180, así que la rama negativa de `py_mod` solo la cubrían tus datos locales. Ahora usa un recorrido que sí baja: cambiar `py_mod` por `fmod` lo detectan los sintéticos.
- Dos casos perezosos más: un punto malo usado que no es la referencia (error) y uno que se salta por `min_gap` (bien).
- El `_constant_props` del exportador comprueba en cada llamada que da lo mismo que el original de video-yt y usa su `_EPS`. Además, un caso con dos props constantes.

**Hecho (4): `add_sound_design`:**
- Coloca los sonidos de «Sonorizar con IA» en la primera pista «SFX» libre y no bloqueada (`^sfx\b` con `re.I`). Si no hay ninguna, crea «SFX n+1» al final. El ambiente de más de 1 s lleva fundidos de 0,4 s.
- No lee disco ni la biblioteca de SFX: eso es `propose`, que va con F7.
- `py.hpp` crece con la semántica de Python que necesita:
  - `py_repr` de cualquier valor JSON (comillas y escapes de Python); `py_str` lo usa para listas, dicts y reales, que antes salían como JSON;
  - `py_path_stem` (`Path.stem` de Python 3.14 en Windows);
  - `py_word_char` (la clase de palabra de `re`; aproximada por bloques fuera de Latin-1);
  - `utf8_next`.
  - Todo con tests unitarios hechos con valores medidos en Python.

**Verificación (4):**
- **Casos:** 19 sintéticos (10 de error): nombres de pista raros, pistas bloqueadas, sonidos que tocan un clip justo antes y justo después, ambiente de 1 s exacto, escena a velocidad 2, ids que no son texto y recortes al final de la escena. Además, 47 operaciones sobre tus timelines reales.
- **Mutación:** se detectan las 13.
- **Cambio de `py_str`:** no altera ninguna de las 1657 operaciones reales.
- **Resultado:** 44 tests y 45 792 aserciones en verde, en Debug y en Release.

**Hecho (5): `animate_clip`:**
- `clip_motion.hpp`: los 13 presets (zoom, giro, deslizar, fundido, pop, pulso y trazo) con sus alias.
- Envolvente de audio: el corte tras el pico en los movimientos de aparición y el remate al final del clip.
- Los items generados se fusionan con las propiedades que el clip tenía en cada instante (volumen, efectos…).
- En clips de relleno, el zoom se escribe también como keyframes del reframe, en tiempo de la fuente.
- Lo que lee el audio con ffmpeg (`sample_rms_envelope`) se deja para F3/F7: el comando recibe la envolvente ya calculada.

**Verificación (5):**
- **Casos:** 43 sintéticos (8 de error): todos los presets, alias con mayúsculas y guiones, valores extremos, envolventes con basura, un punto justo en el umbral tras el pico y clips muy cortos. Además, 188 operaciones sobre tus timelines reales.
- **Mutación:** se detectan las 13.
- **Resultado:** 44 tests y 46 513 aserciones en verde, en Debug y en Release.

**Revisión del subagente (sonorizar y animate_clip juntos; APROBADO), ya aplicada:**
- `py_path_stem` ya no toma por unidad una ruta que empieza en la raíz («/:a» → «:a») y mira caracteres, no bytes («é:foo.wav» → «foo»).
- El error de `motion` desconocido da el valor original y la lista de movimientos, como Python.
- Se quedan documentadas las aproximaciones de `py_word_char` y `py_printable` fuera de los bloques comunes. Las mide el revisor: 0 diferencias en latín, griego, cirílico, CJK y emoji.

**Hecho (6): `apply_recipe`, que completa las 42 ops de Python:**
- Las cinco recetas:
  - `cinema_grade`: capa de ajuste + barras;
  - `text_reflection`: copia volteada con máscara lineal;
  - `pass_through_text`: zoom ×60 al final;
  - `film_strips`: franjas al ritmo de los beats;
  - `subject_pop`: copia con «Eliminar fondo», destello y sonidos.
- Módulos nuevos:
  - `text_metrics.hpp`: `_font_px`, `_line_height` y `_rows`, con el ancho de texto inyectable (`set_text_width`; por defecto, la estimación de 0,55 em);
  - `sfx_library.hpp`: biblioteca inyectable (`set_sfx_library`) y `norm` con tabla NFKD, `score_sfx` y `match_sounds`;
  - en `bg.hpp`, el motor recomendado y su alto de matte.
- Fixtures: el exportador inyecta en video-yt el mismo ancho de texto y, por caso, una biblioteca de SFX con duraciones fijas (`sfx.search`, `sfx.resolve`, `probe_duration`).

**Verificación (6):**
- **Casos:** 30 sintéticos (12 de error) y 74 operaciones sobre tus timelines reales. Incluyen:
  - un formato sin sitio para las barras;
  - un `zoom_time` menor que el imán de keyframes;
  - beats con un valor raro que `every` se salta;
  - más de 6 franjas;
  - un sonido sin archivo;
  - una biblioteca vacía;
  - empates por la longitud del nombre;
  - un texto que solo cabe por la tolerancia del 2 %.
- **Mutación:** se detectan las 16. Para lograrlo hicieron falta casos más afinados para el ancho de texto, la tolerancia, el tope de franjas y la puntuación.
- **Resultado:** 45 tests y 46 853 aserciones en verde, en Debug y en Release.

### 2026-10-07 — F2 cerrada: historial en disco y autoguardado

**Hecho:**
- **`history_store.hpp`:**
  - `history_to_json` y `history_from_json` con el formato de `timeline_history.py`: `{undo, redo, checkpoints}` con cada timeline como su `model_dump()`;
  - `load_history`, tolerante: sin archivo o ilegible, un historial vacío; las timelines v4 se migran y las entradas que no son una timeline se descartan;
  - `save_history`, con escritura atómica.
- **`Autosave`:** guarda la timeline tras cada cambio (con la función que le pasa la app, porque la timeline vive en el proyecto) y el historial cada vez que cambia. Un fallo no para la edición y queda en `last_error()`.
- **`EditSession`:** se puede retomar con un historial guardado y avisa de los cambios del historial con `subscribe_history`, también cuando se guarda un checkpoint o se cierra un grupo.

**Verificación:**
- **Pasos:** 69 sintéticos repetidos con `timeline_history` (pilas tras cada paso, límite de 50, checkpoint con el mismo nombre en su sitio, restaurar y deshacer vacío).
- **Formato:** el JSON final es idéntico al que escribe Python, y leerlo y volverlo a escribir da lo mismo.
- **Tus datos:** el historial de tus 14 proyectos, con 1497 entradas en v4 que se migran.
- **Autoguardado:** 4 tests de autoguardado y de retomar una sesión.
- **Mutación:** 8/8.
- **Resultado:** compilación limpia, 51 tests y 47 246 aserciones en verde en Debug y en Release, con 0 warnings.

**F2, completa:**
- las 42 ops de video-yt (45 comandos con los 3 setters propios) con paridad exacta: 378 casos sintéticos y 1919 operaciones sobre tus timelines;
- encuadre, animación e historial.

**Siguiente: F3.**
- Necesita MSVC + vcpkg (FFmpeg, Skia, ONNX Runtime), que a su vez necesita el **Windows SDK** (pendiente por tu parte).
- Mientras tanto se puede avanzar sin él:
  - la evaluación de la timeline en un instante (lista de capas), que es lógica pura;
  - `recipe_layout`;
  - lo que queda de F1 (biblioteca, colecciones y ajustes de `data/`).

### 2026-10-07 — F1: ajustes y API keys (y revisión de las recetas)

**Revisión del subagente (recetas; APROBADO), ya aplicada:**
- «receta desconocida» lleva la lista de recetas, como en Python.
- `sfx_norm` usa la tabla NFKD → ASCII completa, generada desde `unicodedata` (`tools/gen/gen_nfkd_ascii.py`). Son 2166 caracteres frente a los ~200 tabulados a mano: vietnamita, ™, ①, ㎏…
- Falta `<cctype>` para MSVC.
- `Autosave` guarda el historial antes que la timeline, como `timeline_store`: un cierre entre los dos ya no pierde un paso de deshacer.

**Hecho: `settings.hpp`** (`settings.py`, `export_settings.py` y `transcribe_settings.py`):
- `load` con los defectos, tolerante: si el archivo falta o no se puede leer, los de fábrica;
- `save`, con la fusión de API keys: una máscara no cambia la clave y un texto vacío la borra; normaliza export, transcripción, «Eliminar fondo» y modelos;
- varias claves por proveedor (`keys_for`, `add_key`, `set_key` y `remove_key`);
- `public_view`, sin secretos. Python cuenta las claves recorriendo un `set`: aquí salen ordenadas y el exportador las ordena igual.

**Verificación:**
- **Casos:** 59 pasos sintéticos sobre un `settings.json` temporal, con claves falsas; tras cada paso se comparan lo que devuelve (o si falla) y el archivo.
- **Tus ajustes:** se exportan con cada clave cambiada por «REDACTED-n», así que la fixture no lleva secretos.
- **Mutación:** 10/10.
- **Resultado:** 53 tests y 47 552 aserciones en verde, en Debug y en Release.

**Siguiente:**
- la biblioteca y las colecciones de `data/` y la biblioteca de SFX desde disco, que cierran F1;
- después, la evaluación de capas de F3, que es lógica pura, mientras falte el Windows SDK.

### 2026-10-07 — F1: biblioteca de `data/library`

**Hecho: `library.hpp`** (espejo de `library.py` y `storage.resolve_library_media`):
- carga tolerante de `library.json`;
- tipo, origen y fuente deducidos;
- `material_dto`, con la URL codificada como `urllib.parse.quote`;
- lista ordenada por fecha (orden estable al revés);
- editar título y descripción y la metadata semántica;
- proyectos que usan un material;
- quitar un material: borra su archivo con la carpeta del tipo guardado, no del deducido, como Python.

«Guardar en la biblioteca» desde un proyecto cambia el proyecto y copia archivos, así que llega con el almacén de proyectos de F5.

**Verificación:**
- **Casos:** 32 pasos sobre un `data/` temporal, con `config.DATA_DIR` y `projects._load` apuntando a él en video-yt, y 21 materiales sueltos para `material_dto`. Tras cada paso se comparan el resultado, los errores y `library.json`.
- **Tus datos:** tu `library.json`.
- **Mutación:** 8/8. El de «tipo deducido al quitar» necesitó un caso propio.
- **Resultado:** 55 tests y 47 728 aserciones en verde, en Debug y en Release.

**Revisión del subagente (biblioteca; APROBADO), ya aplicada:**
- `list` ordena con la clave `saved_at or ""` y las comparaciones de Python: texto con texto y números (o booleanos) con números. Mezclar las dos familias, o un dict o una lista, falla con dos o más materiales, como el `TypeError` de Python.
- Quitar un material en uso da el error nuevo `in_use` (el 409 de la API) con el mensaje de `LibraryInUseError`: el nombre o, si está vacío, el id; si no hay ninguno, «otro proyecto». Un clip con `asset_scope` vacío cuenta como «project» y no bloquea.
- `Path.unlink()` no borra una carpeta: si el archivo resulta ser una carpeta, se deja, como Python.
- `std::tolower`, `std::toupper` e `std::isalnum` dependen del locale (Qt fija el del sistema en Linux y macOS): se cambian por versiones ASCII (`ascii_lower`, `ascii_upper`, `ascii_alnum`) en todo `gc_core`.
- El test compara también el mensaje de los errores que ve el usuario.
- **Casos:** 52 pasos (eran 31) y 23 materiales sueltos.
- **Resultado:** 55 tests y 47 813 aserciones en verde, en Debug y en Release.

### 2026-10-07 — F3, evaluación (1): lo que se dibuja de cada clip

Sin el Windows SDK, F3 empieza por la evaluación, que es lógica pura: qué se dibuja de cada clip en un instante.

**Hecho:**
- **`fx_anims`** (`fx_anims.py` y `clip_anim.render_pose`):
  - animaciones de catálogo de entrada, salida, bucle y combo: duraciones efectivas (la del manifiesto si la del clip no vale; entrada y salida repartidas si no caben; combo = el clip; mínimo 0,05 s);
  - las 13 curvas, `eval_anim_track`, el delta sobre la pose (x/y y giro suman; escala y opacidad multiplican) y `render_pose`;
  - `animation_sample_times`, los instantes a 30 fps con los que el export muestrea la pose.
- **`clip_fx`** (`clip_fx.clip_fx_at`): aparición y salida (fundido, disolver, pop, zoom, deslizar, cortinilla) con ventanas de 0,4 s o medio clip, y el `look`.
- **`blend`**: los 12 modos de fusión y su validación.
- **`clip_adjust`**: la matriz 3×3 de exposición, blancos, temperatura y tono; `has_color_adjust`, `strip_color` y las pasadas de la máscara de ajuste (la base sin ajustes y el fantasma «<id>__adj», mudo, con todas sus máscaras como máscaras de recorte).
- Las cadenas de FFmpeg (`effects_ffmpeg`, `video_fx_chain`, `colorchannelmixer`, `blend_steps`) no se portan: el render de Skia las sustituye.

**Verificación:**
- **Casos:** 38 clips sintéticos en 23 instantes (874 muestras): duraciones como texto, negativas, NaN, inf o bool; ids de otra ranura; entrada y salida que no caben; bucles más allá del clip; combo sobre keyframes; pistas `anim` legadas; clip de duración 0; velocidad 2 al revés; todas las apariciones y salidas; fusión válida y no válida; y máscaras de ajuste en vídeo, imagen, figura y texto. Además, 240 puntos de curva, 140 de pista, 8 ventanas y 16 efectos sueltos.
- **Tus datos:** tus 914 clips que se dibujan, en 7122 instantes (inicio, ventanas, mitad y final).
- **Exactitud:** con tolerancia 0 fallan 3 de 41 011 comprobaciones (`outBack` por `pow()`); con 1e-12 relativa pasan todas.
- **Mutación:** se detectan 23 de 24. La otra es equivalente: con blancos a 0, `k / (1 − 0)` y `k · (1 + 0)` son el mismo valor.
- **Resultado:** 57 tests y 88 824 aserciones en verde, en Debug y en Release, con 0 warnings.

**Siguiente (evaluación, 2ª parte):** `layers_at`/`videos_at` (filtro y orden de la pila; fixtures con node desde `clipLayout.js`) y el fotograma ensamblado. Antes hay que decidir cómo se resuelven las diferencias de orden y visibilidad entre el preview y el export (notas de F3 en el TODO).

**Revisión del subagente (evaluación 1; APROBADO, sin bloqueantes), ya aplicada:**
- **Tolerancia más estrecha:** 4e-15 relativa en lugar de 1e-12, y solo donde hay `pow`/`sin`/`cos` (6 curvas y la matriz con exposición o tono), no en cualquier clip animado. Con la de antes sobrevivían mutantes de 1e-14 en las constantes (`kC4`, `0.213`, `0.18`); ahora se detectan.
- **Huecos de las fixtures:** un caso por cada modo de fusión (y variantes no válidas), `dur: 0` exacto, salida y bucle sobre un clip de duración 0, máscara de ajuste en una capa de ajuste y en un motion (una sola pasada), e instantes, pistas y ventanas con NaN e infinito (van como texto en el JSON).
- **`animation_sample_times`:** tope de 10⁷ intervalos (≈ 92 h a 30 fps): convertir a entero un número que no cabe era UB. Por encima, Python tampoco termina.
- **Mutación reproducible:** `tools/dev/mutantes_eval.py` (hoy `tools/dev/mutantes.py eval`) aplica y deshace cada mutante. Se detectan 34 de 35; el otro es equivalente.
- Docs: la sección de evaluación de ARQUITECTURA ya no parte el bloque de rendimiento de F2, y el docstring del exportador lista todas las fixtures.
- **Casos:** 41 clips sintéticos en 24 instantes (984 muestras) y 434 casos sueltos. **Resultado:** 57 tests y 89 321 aserciones en verde, en Debug y en Release.
- **Decisión para la 2ª parte** (en las notas de F3 del TODO, pendiente de que Edu la confirme): el render C++ usa el intervalo [inicio, fin) sin margen, desempata por `start` dentro de una capa y toma el fin de `clip_timeline_duration`; el modo literal del preview queda disponible con `LayerRules`.

### 2026-10-07 — F3, evaluación (2): las capas de un instante

**Hecho:**
- **`timeline_rules`:** `stack_layers` (track_id → capa de la pila vídeo + texto) y `buried_text_track_ids` (pistas de texto con vídeo encima), de `track_stack.py`.
- **`layers.hpp`:** `layers_at`/`videos_at`, qué clips se dibujan en un instante y en qué orden. Con dos reglas:
  - `kPreviewLayers`: el `layersAt` literal de `clipLayout.js` (margen de 0,02 s antes del inicio y orden por capa e índice en la lista);
  - `kRenderLayers`: la del render C++, la recomendada por la revisión (pendiente de que Edu la confirme). Intervalo [inicio, fin) sin margen, inicio acotado a ≥ 0 y, dentro de una capa, delante el que empieza después, como el export.
  - El fin es siempre el de Python (`clip_timeline_duration`, redondeada a ms).
- **Fixtures con el JS real:** `tools/fixtures/layers_js.mjs` importa `clipLayout.js` y `trackStack.js` de video-yt con Node 24 y devuelve la posición de cada clip en la lista (los ids pueden repetirse). El exportador descarta los instantes en que el `clipEnd` de JS (sin redondeo, figuras a velocidad 1) no coincide con el de Python.
- **`tools/dev/mutantes.py <grupo>`:** el script de mutación, ahora por grupos (`eval`, `layers`).

**Verificación:**
- **Casos:** 12 timelines sintéticas en 71 instantes: todos los tipos de clip, pista oculta o inexistente, desactivados, el margen y el fin abierto, solapes, ids de pista y de clip repetidos, clip visual en pista de audio, velocidades fuera de rango, texto enterrado y duración 0. Además, tus 18 timelines en 6627 instantes alrededor de los bordes de cada clip (48 descartados).
- **Reglas del render:** 6 tests propios (corte, margen, solape de 15 ms con la lista al revés, pila frente a inicio, inicio negativo y duración 0).
- **Mutación:** se detectan las 16.
- **Resultado:** 65 tests y 102 861 aserciones en verde, en Debug y en Release, con 0 warnings.

**Siguiente:** el fotograma ensamblado (`evaluate`): por capa, tiempo local y de fuente, pose, fx, fusión, volteo, máscaras, recorte y destino con las dimensiones del material, y la comprobación de ancho o alto 0.

**Revisión del subagente (capas; APROBADO, sin bloqueantes), ya aplicada:**
- **Sin descartes en las fixtures:** a Node le llega un gemelo de cada clip con `in_point` 0, `out_point` = duración de Python y velocidad 1, así que el `clipEnd` de JS da bit a bit el fin de Python. Antes se descartaban 48 instantes: 14 por clips de audio (que `layersAt` no pinta) y 34 por fines de JS 1–3 ulps por encima. Ahora son 81 instantes sintéticos (un caso nuevo con una figura a velocidad 2 y duraciones que se redondean) y 6675 reales.
- **`kRenderLayers` sigue al export en dos cosas más:** el clip tiene que encajar en su pista (`clip_fits_track`; el export solo pinta vídeo en pistas de vídeo) y durar más de 0,02 s (el export descarta los demás). El campo pasa a llamarse `export_rules`. Se queda una divergencia, anotada: el export ordena los textos normales por la lista en un único .ass; `layers_at`, por la pila.
- **Tests de las reglas del render:** 4 más (inicio negativo que cambia el orden, encaje en la pista, ids de pista repetidos de otro tipo y clips de 0,02 s). Mutación: 20/20.
- Docs: firma de `layers_at` y campo de `LayerRules` en las notas, nota obsoleta de las duraciones, fila de F3, tope sin test de `animation_sample_times` y la decisión del relleno en «Pendiente por tu parte».
- **Resultado:** 69 tests y 102 988 aserciones en verde, en Debug y en Release.

### 2026-10-07 — F3, evaluación (3): el fotograma ensamblado

**Hecho:**
- **`frame.hpp`: `evaluate(timeline, t, FrameSpec, dimensiones del material)`.** Por cada capa visible (`layers_at`) y cada pasada (`adjust_passes`):
  - tiempo local y de fuente, pose (`render_pose`), aparición/salida (`clip_fx_at`), opacidad final, fusión, volteo y máscaras;
  - para el vídeo, la imagen y el motion, los «blits»: el rectángulo de la fuente y la transformación afín que lo lleva al cuadro, en el mismo orden de operaciones que el contexto 2D del preview. Hay overlay, relleno (cover y `contain`, con la pose sobre el cuadro entero) y doble encuadre (`split_orientation_for`, `contain_dest`), además del recorte de la cortinilla.
- **Errores:** las dimensiones de salida tienen que ser finitas y mayores que 0. Con las reglas del render, también las de cada material (Python lanzaría ZeroDivisionError); con las del preview, esa capa va sin dibujo.
- **Oráculo con el JS real:** `tools/fixtures/frame_js.mjs` ejecuta el `drawComposite` de `canvas.js` con Node (con un hook de resolución, `node_hook.mjs`, para sus imports sin extensión) sobre un contexto 2D que graba la matriz, el recorte y la opacidad de cada `drawImage` del material.
- `recipe_layout.py` queda cubierto: lo que no está en `frame.cpp` son cadenas de FFmpeg.

**Verificación:**
- **Casos:** 10 timelines sintéticas en 95 instantes (203 dibujos): overlay con recorte libre, volteos y giro; todas las apariciones y salidas; pose animada y animaciones de catálogo; relleno con reframe, keyframes de encuadre, `contain` (en un tramo, en todos, vertical en una salida horizontal) y pose; el atajo de escala del preview; doble encuadre vertical y horizontal; imagen, motion, velocidad 2 y al revés; máscara de ajuste (base + fantasma) con fusión; y un clip sin material.
- **Tus datos:** tus 18 timelines en 831 instantes, lejos de los bordes de los clips: 1692 dibujos comparados y 22 fuera, porque el `clipDur` de JS (sin redondeo) cambia la ventana de aparición o salida.
- **Exactitud:** bit a bit salvo en las capas giradas (sin/cos de V8 frente a la libm: tolerancia relativa de 1e-12).
- **Reglas del render y errores:** 7 tests propios.
- **Mutación:** 21/21. Hizo falta un caso de `contain` que centre en horizontal.
- **Resultado:** 78 tests y 110 605 aserciones en verde, en Debug y en Release, con 0 warnings.

**Con esto, la evaluación de F3 está completa.** Lo siguiente de F3 es `gc_media` y `gc_render`, que necesitan el Windows SDK (MSVC + vcpkg). Mientras tanto quedan los pendientes de F1 (biblioteca de SFX de disco y colecciones) y de F0 (`.clang-tidy`, spdlog).


### 2026-10-07 — F1: el JSON se lee como Python, `lower()` de Unicode y rutas de Python

Preparación de la biblioteca de SFX y de las colecciones, que ordenan y filtran texto como Python.

**Hecho:**
- **Un BOM de UTF-8 ya no se acepta donde Python lo rechaza.** `json.loads(path.read_text("utf-8"))` falla con un BOM; C++ lo saltaba sin decir nada. `read_json_file_py` falla igual, y la usan los ajustes, la biblioteca y el historial: con BOM, los ajustes de fábrica, la biblioteca vacía y el historial vacío, como en video-yt. `read_json_file` sigue saltando el BOM para el CLI y las fixtures; `projects.json` también es estricto en video-yt (`projects.py`, cae a `{"projects": []}`), así que el almacén de proyectos de F5 tiene que usar `read_json_file_py`.
- **`py_lower`: `str.lower()` de Python** con toda la tabla de Unicode 16 (incluidas las expansiones, como «İ» → «i̇») y la sigma final («ΟΔΟΣ» → «οδος»). La tabla y los rangos «cased» y «case-ignorable» se generan desde el Python de video-yt (`tools/gen/gen_py_lower.py` → `py_lower.inc`).
- **`py_path_name`, `py_path_suffix` y `py_path_stem`:** `Path(x).name`, `.suffix` y `.stem` de Python 3.14 en Windows (unidades, UNC, «x.» con sufijo «.»…). **`py_quote`** pasa de la biblioteca a `py.hpp`.

**Verificación:**
- **Casos:** 5 pasos nuevos con BOM (2 en ajustes y 3 en biblioteca) contra el Python real, y un test del historial con BOM.
- **Tests:** 26 comprobaciones de `py_lower` y 90 de rutas y `quote`, con los valores medidos en Python.
- **Resultado:** 80 tests y 110 740 aserciones en verde, en Debug y en Release, con 0 warnings.

**Revisión del subagente (fotograma y lectura de JSON; APROBADO, sin bloqueantes), ya aplicada:**
- El fotograma coincide línea a línea con `canvas.js`, `panning.js`, `recipeLayout.js` y `clipLayout.js`, y al volver a ejecutar el JS salen las mismas fixtures. `py_lower` y `py_quote` dan lo mismo que Python en todos los puntos de código (7,8 millones de casos de `lower`).
- **Rutas exóticas:** `\\?\UNC\servidor` y una unidad con un carácter fuera del plano básico daban otro `name` (591 de 209 862 cadenas raras). `py_path_name` usa ahora el mismo `splitroot` que Python (`_Py_skiproot`, en unidades de UTF-16), y hay `py_path_str` (`str(Path(x))`). 84 comprobaciones más con valores medidos.
- **JSON:** `1e999` cerraba la app (`out_of_range` sin capturar) y un NUL cortaba el texto («`{"a":1}\0basura`» cargaba). Los dos son ahora errores de lectura, con un test.
- **Divergencias del export sin anotar:** el `slide` en overlay (el preview desplaza una fracción del clip y el export el cuadro entero; 3 de tus clips, decisión de Edu), el redondeo a pares del overlay estático, las dimensiones pares del export (`FrameSpec` lo dice), `x`/`y`/`scale` a 0 leídos como 0,5 o 1 en el export, y el `transform` estático que el export ignora en relleno.
- Docs: cifras de pasos (ajustes 61, biblioteca 55, 5 pasos con BOM), `projects.json` también es estricto en Python (el almacén de F5 usará `read_json_file_py`), `dual_slot_wh` y dos comentarios fuera de sitio.
- **Resultado:** 82 tests en verde, en Debug y en Release, con 0 warnings (verificado junto con la tanda siguiente, la biblioteca de SFX).

### 2026-10-07 — F1: biblioteca de SFX en disco

**Hecho: `sfx_disk.hpp`** (la parte de lectura de `sfx.py` y lo que `sound_design.propose` hace con ella):
- **Carpeta activa (`sfx_base`):** la de `sfx_folder` en los ajustes si es una carpeta; si no, la de fábrica. La que se devuelve en `base` es `str(Path)` de Python (`py_path_str`).
- **Índice (`sfx_load_index`):** `sfx_library.json` con categorías y sonidos (`sfx_entry`: id, nombre o el stem, archivo, carpeta, categoría o la carpeta, uso y URL codificada). Si falla en cualquier punto, los sonidos salen del escaneo y las categorías ya leídas se quedan, como en Python.
- **Escaneo (`sfx_scan`):** los audios (.mp3, .wav, .ogg, .m4a, .aac y .flac, sin distinguir mayúsculas) de la carpeta y sus subcarpetas, en el orden de `sorted()` de `WindowsPath`.
- **Búsqueda (`sfx_search`, `filter_sfx_items`, `with_favorites_category`):** por categoría (en la carpeta o la categoría; «favoritos» son los marcados en los ajustes) y por palabras, que tienen que estar todas en el nombre, la categoría, el uso o la carpeta, con `str.lower()` de Python sobre el texto entero.
- **`sfx_resolve`:** la ruta del sonido si existe y no sale de la carpeta (sin distinguir mayúsculas, como `WindowsPath`).
- **`load_sfx_library`:** `search("", 100000)` y la duración de cada sonido que resuelve, con la sonda inyectada y redondeada a ms. Es lo que se pasa a `set_sfx_library` para que `subject_pop` encuentre sus sonidos.
- Dar de alta, editar y crear categorías escriben archivos y llegan con la pantalla de SFX (F5).

**Verificación:**
- **Casos:** 205 pasos con el `sfx.py` REAL sobre una carpeta temporal y un `settings.json` temporal (`sfx_cases.py`). Cubren:
  - el escaneo con subcarpetas, `.MP3`, `İ`, `ß`, `Σ`, archivos que no son audio y carpetas con nombre de audio;
  - la carpeta de los ajustes (con barras repetidas, `..`, que no existe o que es un archivo);
  - un índice válido con tipos raros y 22 variantes más: 15 que fallan (BOM, latin-1, no es un dict, tipos raros, es una carpeta…) y 7 vacías, falsas o con claves repetidas;
  - búsquedas con espacios Unicode y sigma final, favoritos, `resolve` dentro, fuera, absoluto y con otras mayúsculas, y la biblioteca con duraciones de una sonda falsa;
  - `_entry`, `filter_sfx_items` y `with_favorites_category` sueltos.
- **Tu carpeta:** la de fábrica de video-yt, con tus favoritos. Son 626 sonidos en 15 categorías (619 en disco), 21 favoritos, una búsqueda por categoría y 8 por palabras.
- **Mutación:** 34/35 (`tools/dev/mutantes.py sfx`); la otra es equivalente.
- **Resultado:** 84 tests y 111 305 aserciones en verde, en Debug y en Release, con 0 warnings.

**Siguiente:** F0 (`.clang-tidy` y spdlog). Las colecciones de F1 pueden esperar a F5, y F3 sigue esperando al Windows SDK.

### 2026-10-08 — F0: `.clang-tidy` y spdlog

**Hecho:**
- **`.clang-tidy`** con `bugprone`, `clang-analyzer`, `concurrency`, `misc`, `modernize`, `performance`, `portability` y una selección de `readability`. Lo que se desactiva y por qué está en el propio archivo: estilo, choques con cómo está escrito el motor (los `get_or` que devuelven el defecto por referencia, varios `Json` o `double` seguidos) y falsos positivos (`unchecked-optional-access` no ve las comprobaciones hechas en otra función). Las condiciones no se reescriben con De Morgan, porque copian las de Python.
- **`tools/dev/tidy.py [--fix] [filtro]`:** pasa el clang-tidy de CLion por `src`, `apps` y `tests` con una build propia (`build/tidy`, sin el escaneo de módulos de C++20, cuyas opciones de GCC clang no entiende) y sale con error si hay algún aviso. Ese clang-tidy no trae las cabeceras internas de clang; sirven las de GCC, salvo las de intrínsecos x86 que incluye `windows.h`, que se sustituyen por las vacías de `tools/dev/tidy_shim`.
- **Lo que arregló tidy** (sin cambiar ningún resultado; lo confirman las fixtures de paridad): `std::scoped_lock`, `contains`, `starts_with('#')`, `std::views::reverse`, `std::ranges::none_of`/`any_of`, `std::cmp_less`, `reserve` antes de llenar vectores, `const` donde faltaba, parámetros `Json` por referencia, máscaras de bits en `unsigned`, nombres de parámetros iguales en la declaración y la definición, y `ValidationFailure` deriva ahora de `std::exception` (solo la captura `parse_model`; ninguna otra captura de `std::exception` envuelve la validación).
- **`gc_log`** (`src/log`): registro sobre spdlog 1.15.3 (FetchContent con hash verificado, o el de vcpkg). `init(nivel, sinks extra)` crea el logger por defecto con salida a stderr («nivel: texto»), `GENCLIP_LOG` (trace, debug, info, warn, error, critical u off) manda sobre el nivel del programa y `parse_level` acepta mayúsculas y espacios. `gc_core` no lo usa: es lógica pura y devuelve los errores en `Result`.
- **`genclip`:** los errores van por el registro («error: …» en stderr), `GENCLIP_LOG=debug` muestra la traza y una excepción inesperada sale con un mensaje y código 1 en vez de cerrar el programa de golpe.

**Verificación:**
- **Tests:** 3 de `gc_log` (nombres de nivel, la variable de entorno y el filtrado por nivel con un sink que graba, también con «ñ»).
- **tidy:** 0 avisos en 64 archivos. La primera pasada en `main` daba 10: `build/tidy` era de antes de spdlog (`tidy.py` solo configuraba si faltaba `compile_commands.json`; ahora reconfigura siempre), la firma de `main` (NOLINT con el motivo) y 6 concatenaciones de mensajes dentro de bucles, que pasan a `std::format` con el mismo texto.
- **Resultado:** 87 tests y 111 318 aserciones en verde, en Debug y en Release, con 0 warnings.

**Revisión del subagente (APROBADO, sin bloqueantes), ya aplicada:**
- Los arreglos de tidy no cambian nada: revisados uno a uno (`is_free`, los `std::format`, `json_or`, `prepare`, `ValidationFailure`…), y los 111 patrones de `tools/dev/mutantes.py` siguen encontrando su texto.
- **Errores de `genclip`:** pasaban por el filtro del registro, así que con `GENCLIP_LOG=off` salía con código 1 y sin mensaje. Ahora se escriben siempre («error: …» en stderr) y el registro queda para la traza. Importa en F3, cuando video-yt lance `genclip render` y lea stderr. Hay 2 tests de la CLI (`tests/cli/errores.cmake`, con el nivel por defecto y con `off`): comprueban el código 1 y el mensaje.
- **Catálogo roto:** `param_value` hacía `*py_float(min)` sin comprobar. Con un `range` sin `min` o `max` numéricos, Python lanza KeyError o ValueError y C++ desreferenciaba un `nullopt`. Ahora `resolve_params` devuelve `Result` y `apply_effect` falla con un error Internal; un parámetro sin id de texto también es un error (antes, `type_error` sin capturar). El catálogo incluido no lo dispara. Hay un test con los dos casos y otro de defectos y rangos.
- **`unchecked-optional-access`** sigue desactivado: con el check activo quedan 31 avisos, revisados a mano y todos protegidos (el motivo está en `.clang-tidy`).
- **`tidy.py`:** un archivo en el que clang-tidy falla sin dar ningún aviso nuestro (por ejemplo, un error en una cabecera de terceros) cuenta como «sin analizar» y hace fallar la pasada, en vez de dar «0 avisos».
- Docs: el estado del plan (las SFX de disco ya estaban hechas), spdlog en la lista de dependencias con hash, el comentario de `log.hpp` (spdlog usa su fmt) y el estado de F0 en el README.
- **Resultado:** 91 tests (89 de Catch2 y 2 de la CLI) y 111 329 aserciones en verde, en Debug y en Release, con 0 warnings; tidy, 0 avisos en 65 archivos.

**Siguiente:** F1, colecciones (`collections.py`: el plan de la revisión está en el TODO). F3 sigue esperando al Windows SDK.

### 2026-10-08 — F1: colecciones de material

**Hecho: `collections.hpp`** (espejo de `collections.py`): la carpeta raíz de material (`material_root` o la de fábrica, `<assets>/material`) cuyas subcarpetas son colecciones.
- **Tipo de medio** (`collection_kind`): vídeo, imagen, GIF o audio por la extensión, sin distinguir mayúsculas.
- **Raíz** (`collections_root`, `set_collections_root`): la de los ajustes si existe y, si no, la de fábrica. Guardarla escribe `str(Path(x.strip()))`, y solo si es una carpeta.
- **Preferencias** (`set_collection_prefs`, con `safe_collection_id`): activa, favorita y etiqueta, mezcladas en `collections_meta`. Una etiqueta vacía se borra.
- **Lista** (`list_collections`): `{available, root, collections}`, con la cuenta, la portada (la primera imagen o GIF), si es un personaje stick (`stick.json`) y las favoritas primero.
- **Búsqueda** (`collections_search`, `collection_match`): en las activas o en una sola, por palabras (todas en «nombre colección», sin distinguir mayúsculas) y por tipo. Las cuentas por tipo se hacen antes de filtrar por tipo, y los resultados se cortan como `items[:limit]`.
- **Crear y añadir** (`create_collection`, `add_collection_files`): nombres saneados como en Python, con « N» o «_N» si ya existen. Los archivos que no son medio van a `errors`, y el contenido lo escribe quien llama (`IncomingFile`).
- **`collection_resolve`:** el archivo de un `filename` de colección, sin salir de la raíz y solo si es un archivo.
- **`disk_scan.hpp`:** la carpeta de los ajustes, el recorrido de `sorted(rglob)` y el `resolve` seguro salen de `sfx_disk.cpp` y los usan las dos bibliotecas.
- Los errores que ve el usuario llevan el texto de Python, y el test los compara.

**Verificación:**
- **Casos:** 256 pasos (36 de error, 31 con el mensaje que ve el usuario) con el `collections.py` REAL sobre una carpeta temporal y un `settings.json` temporal (`collections_cases.py`). Cubren:
  - sin raíz;
  - la raíz de fábrica con colecciones en Unicode (`ΟΔΟΣ`, `g r#ú%`), mayúsculas, `.GIF`, extensiones no soportadas, subcarpetas, una carpeta con nombre de vídeo y `stick.json`;
  - búsquedas por tipo, palabras, colección y límites 0, 3, −2 y −100;
  - `resolve` dentro, fuera, absoluto, con otras mayúsculas y de carpetas;
  - preferencias, también con `collections_meta` raros a mano y con un `save` de otra cosa (no las pierde);
  - la raíz de los ajustes (normalizada, inexistente, un archivo, vacía), `set_root` (también «» → «.»), crear (colisiones, caracteres prohibidos, puntos y controles) y añadir (rutas, «a:b», colisiones, no medios, nombres que no son texto), con el árbol de carpetas resultante;
  - un empate de (favorita, nombre), que ordena `sorted(iterdir())` en minúsculas y no el disco (NTFS ordena en mayúsculas), y un NUL en el id.
- **Tu carpeta:** la de fábrica de video-yt (`scientist_stick`: 27 imágenes, 19 vídeos y su `stick.json`), solo lectura: lista, búsquedas, el recorrido y la ruta de cada medio.
- **Mutación:** 41 de 42 detectados (`tools/dev/mutantes.py collections`); el otro es equivalente (el separador del recorrido: `collection_entry` vuelve a poner «/»). El de quitar la protección del NUL en `add` cuelga los tests: Windows corta la ruta en el NUL, así que `exists()` de «alfa\0x/nul_N.png» mira la carpeta `alfa`, que existe, y la búsqueda de un nombre libre no acaba. `mutantes.py` no tenía tiempo máximo y se quedó esperando; ahora lo tiene (600 s) y un mutante que cuelga cuenta como detectado. El de la protección del NUL en el recorrido también cuelga (un `recursive_directory_iterator` sobre la ruta cortada no acaba). Los de SFX que pasaron a `disk_scan.cpp` siguen igual: 34/35, y el otro es el equivalente de siempre.
- **Resultado:** 93 tests (91 de Catch2 y 2 de la CLI) y 111 874 aserciones en verde, en Debug y en Release, con 0 warnings; tidy, 0 avisos en 68 archivos.

**Revisión del subagente (APROBADO, sin bloqueantes), ya aplicada:**
- Paridad línea a línea con `collections.py` y ~250 pasos más contra el Python real (espacios Unicode, límites de ±2^40, `collections_meta` y `material_root` raros, CON y NBSP al crear, nombres que no son texto al añadir, sigma final, İ y ǅ): todo coincide salvo lo anotado. El refactor de `sfx_disk` es equivalente: los mutantes de SFX que pasaron a `disk_scan.cpp` dan 9/9.
- Cifra de mutación (41/42) y tiempo máximo de los mutantes a 120 s (los grupos tardan segundos).
- Divergencia anotada: `resolve` con «NUL» o con un punto o espacio final (Windows normaliza el nombre). Y otra para el CI: las fixtures de disco son de `WindowsPath`.
- `write_file` de los tests de disco devuelve si pudo escribir, y el escritor de `add` de las colecciones falla si no.

**Siguiente:** `gc_services`, el almacén de proyectos (`projects.py`). F3 sigue esperando al Windows SDK.

### 2026-10-08 — F1: el almacén de proyectos (projects.py)

En el plan va en `gc_services`. De momento vive en `gc_core`, con los ajustes, la biblioteca y las colecciones; se separará cuando haya servicios con E/S de verdad (F5-F7).

**Hecho: `projects_store.hpp`** (espejo de `projects.py` y de lo que usa de `storage.py`):
- **`ProjectsStore`:** `data/projects.json` en crudo, con un mutex como el `_lock` de Python y la hora inyectable (`_now`). Hace lo mismo que `projects.py`:
  - proyectos: listar, leer, crear («Sin título», con un grupo que puede no existir), renombrar, mover a una carpeta del inicio y borrar;
  - grupos: crear, renombrar y borrar (sus proyectos vuelven a la raíz);
  - duplicar con los archivos, la carpeta en disco y los materiales: `add_clips` con upsert por `index`, `add_transcript`, `add_audio`, `add_image`, `update_material`, `remove_material` y `apply_manifest`;
  - la timeline, las composiciones de Motion, la escaleta, la dirección visual y `retarget_timeline_asset`.
- **`storage.py`:** `project_default_base`, `project_base` y `ensure_project_dirs`. El resto, que son diálogos y resolución de URLs, llega con F5 y F7.
- **`serialize.hpp`:** `project_from_json_as_is` (`Project(**p)`, sin preparar) y lectura y escritura de `ClipInfo`, `AudioInfo`, `ImageInfo` y `Transcript`. **`py.hpp`:** `py_equal`, el `==` de Python entre valores JSON.
- **`write_json_file`** ya no añade un salto de línea final: ningún archivo de video-yt lo lleva.

**Verificación:**
- **Casos:** 151 pasos (31 de error, 4 con el mensaje que ve el usuario) con el `projects.py` REAL sobre un `projects.json` temporal, con `uuid4` en la secuencia de `SequentialIdSource` y `_now` fijado (`projects_cases.py`). Tras cada paso que escribe se compara el TEXTO de `projects.json`. Cubren:
  - sin archivo, roto y con BOM;
  - el estado de partida con claves desconocidas y URLs del proyecto;
  - grupos que existen y que no, nombres vacíos;
  - duplicar con `exports/`, `Exports/`, `motion/` y carpeta propia, con el árbol de archivos resultante;
  - upsert de clips (con y sin id, `index` 0), manifest con `file` repetido, vacío o ausente;
  - `retarget` por id, por nombre y sin nombre;
  - 13 formas raras del archivo.
- **Tu `projects.json`:** una copia (18 proyectos) en una carpeta temporal, con lista, grupos, cada proyecto, renombrar, mover, duplicar, guardar una timeline, editar un material y borrar. El archivo final sale con el mismo texto que el de Python. El tuyo no se toca.
- **Mutación:** 41/41 (`tools/dev/mutantes.py projects`). En la primera pasada se escaparon 5 por huecos de las fixtures, ya cubiertos: el BOM con el mismo contenido que el defecto, `set_folder` sobre un proyecto sin nada que preparar, un `null` sobre un campo que ya lo era, un `retarget` sin nadie con ese nombre y `True == 1`.
- **Resultado:** 95 tests (93 de Catch2 y 2 de la CLI) y 112 349 aserciones en verde, en Debug y en Release, con 0 warnings; tidy, 0 avisos en 70 archivos.

**Revisión del subagente (CORREGIR: un bloqueante), ya aplicada:**
- **Bloqueante:** `get_or` devolvía la dirección de su `fallback`, y tres llamadas le pasaban un temporal (`Json::array()`): `list_groups`, `create_project`/`move_project` con grupo y `apply_manifest` sin «materials». Al faltar la clave (un `projects.json` nuevo no tiene «groups») se leía memoria ya liberada; los tests pasaban de casualidad. Ahora el defecto es una constante y `get_or` con un temporal no compila. Pasos de regresión en las fixtures.
- 153 pasos adversariales de la revisión, pasados por el Python real: ids y grupos numéricos, `set_folder` y timelines de otro tipo, manifest con `file` y `project` raros, materiales con `index` raros, motion y retarget con tipos raros, grupos de otra forma, `EXPORTS` y un archivo «exports» al duplicar, y Unicode en `strip`. Destaparon un orden distinto: `add_clips` sobre unos `clips` que son `{}` o `""` pide el uuid antes de fallar en el `append`, como Python.
- Docs: «byte a byte» era demasiado. Python en Windows escribe CRLF, nlohmann escribe los reales con Grisu2 (y no con `repr`) y los enteros de más de 64 bits pasan a real. Queda anotado y va en la tanda siguiente.
- **Riesgo anotado como lo siguiente:** `NaN`, `Infinity` y `1e999` se leen como archivo ilegible, y la primera escritura vaciaría `projects.json`. Python los lee y los escribe.
- Tests directos de `py_equal` (enteros de más de 2^53 y de 64 bits, `True == 1`, listas y dicts, NaN) y de `utc_now_iso` y `project_base`, que solo cubrían las fixtures.
- **Resultado:** 304 pasos (77 de error, 6 con mensaje); 98 tests (96 de Catch2 y 2 de la CLI) y 112 774 aserciones en verde, en Debug y en Release, con 0 warnings; tidy, 0 avisos en 71 archivos.

**Siguiente:** JSON como Python (leer `NaN`, `Infinity` y `1e999`, y escribir como `json.dumps`); después, «Guardar en la biblioteca» (`library.save_from_project`).

### 2026-10-08 — F1: JSON como Python (leer NaN e Infinity, escribir como json.dumps)

Lo encontró la revisión del almacén de proyectos: con un `NaN` en `projects.json` (Python lo escribe con `allow_nan`), C++ tomaba el archivo por ilegible, lo leía como `{"projects": []}` y la primera escritura borraba todos los proyectos. Lo mismo pasaba con los ajustes y la biblioteca.

**Hecho (`io.hpp`, `py.hpp`):**
- **Leer como `json.loads`:** `read_json_file` y `read_json_file_py` aceptan `NaN`, `Infinity`, `-Infinity` y los números que se desbordan (`1e999` es inf; antes, un error). nlohmann no los admite, así que fuera de los textos se cambian por un texto marcador (un NUL escapado delante y detrás) y, ya leído, por el real. `1e-999` ya daba 0.0, como en Python. Sigue habiendo una divergencia: un entero de más de 64 bits pasa a real.
- **Escribir como `json.dumps(ensure_ascii=False)`:** `py_json_dumps` da el texto exacto de Python. Los reales salen con `repr`, `NaN`/`Infinity`/`-Infinity` también, con «, » y «: » sin sangría y «,» y «: » con ella, y solo se escapan las comillas, la barra y los caracteres de control (`\u00XX` en minúsculas). Un texto que no es UTF-8 válido es un error. `write_json_file` lo usa y, en Windows, escribe CRLF, como `path.write_text()` en modo texto. El historial (sin sangría) también sale ya como el de Python.
- **`py_repr` es `repr()` exacto:** las cifras más cortas de `std::to_chars` con los umbrales de `format_float_short`: científica si el punto queda a 4 ceros o más o más allá de 16 cifras. Antes usaba el Grisu2 de nlohmann, que en algunos dobles no da las cifras más cortas, y pasaba a científica desde 1e15.

**Verificación:**
- **`json_cases.py`** con el CPython de video-yt:
  - 5175 `repr`: bits aleatorios, decimales redondeados y alrededor de cada potencia de 10, incluidos 1e15, 1e16, 1e-05, 5e-324 y -0.0;
  - 10 `json.dumps` con sangría None, 0, 2 y 4, con textos con todos los escapes, Unicode, U+2028, DEL, vacíos y anidados;
  - 19 `json.loads`: `NaN`/`Infinity` sueltos y dentro de textos, desbordes por arriba y por abajo, claves repetidas y 11 errores (`-NaN`, `nan`, `[1NaN]`, `Infinityx`…).
- **Pasos con NaN en los almacenes:** en proyectos, `NaN`/`Infinity`/`1e999` y reales en los bordes, comparando el texto del archivo, ahora con sus CRLF; en ajustes, `idle_minutes` NaN (`min(240, max(0, nan))` es 0.0 en los dos) y `inf`; en SFX, un índice con NaN.
- **Mutación:** 17/17 (`tools/dev/mutantes.py json`): los umbrales y el formato de `repr`, los separadores, los escapes, `NaN` como `null`, el CRLF y cada pieza del lector de NaN e infinitos.
- **Resultado:** 101 tests (99 de Catch2 y 2 de la CLI) y 118 076 aserciones en verde, en Debug y en Release, con 0 warnings; tidy, 0 avisos en 72 archivos.

**Revisión del subagente (CORREGIR: un bloqueante), ya aplicada:**
- **Bloqueante:** `duplicate_project` pasaba el proyecto por el `dump()`/`parse()` de nlohmann, que escribe NaN e infinitos como `null`. La copia los perdía y, en un campo del modelo, fallaba después de escribirse. Ahora usa `py_json_dumps` y `py_json_loads` (nueva: `json.loads` de un texto). Hay un paso de fixture (duplicar con NaN e Infinity) y un mutante.
- **El lector, más fiel:**
  - `NaN`, `Infinity` o un número como clave son un error, como en Python. Antes, C++ los aceptaba con una clave marcador.
  - Un texto de verdad igual a un marcador ya no pasa a NaN: los marcadores llevan un número y, si salen más de los puestos, se repite con otro.
  - Un sustituto suelto de UTF-16 pasa a U+FFFD en vez de dar el archivo por ilegible.
  - `restore_marks` ya no es recursivo.
- **Escritura:** más de 1500 niveles de anidamiento son un error, no un desbordamiento de la pila.
- **Biblioteca:** con un `saved_at` NaN, el comparador ya no rompe el orden estricto. Los NaN van al final; Python da el orden de su timsort, que no se puede reproducir.
- **Docs:** las divergencias que quedan (enteros grandes, sustitutos sueltos, profundidad, `saved_at` NaN) y el CRLF de las fixtures para el CI de Linux y macOS.
- **Contraste de la revisión, fuera del repo:** 3 752 179 dobles con `repr` (0 diferencias); 81 973 textos de decimales largos; los escapes de los 128 caracteres de control y de todos los planos de Unicode; 1190 archivos de `data/` reescritos con el mismo texto que Python. Tu `projects.json`, de 4 586 120 bytes, sale con los mismos bytes, CRLF incluidos.
- **Mutación:** 23/23 (`tools/dev/mutantes.py json`; 6 nuevos: NaN como clave, marcador que choca, sustitutos altos y bajos, profundidad y duplicar con NaN).
- **Resultado:** 103 tests (101 de Catch2 y 2 de la CLI) y 118 111 aserciones en verde, en Debug y en Release, con 0 warnings; tidy, 0 avisos en 72 archivos.

**Siguiente:** «Guardar en la biblioteca» (`library.save_from_project`).

### 2026-10-08 — F1: «Guardar en la biblioteca» (library.save_from_project)

**Hecho:**
- **`LibraryFile::save_from_project`:** copia el archivo de un clip, audio o imagen del proyecto a `data/library/<video|audio|image>/<título>_<id><ext>`. Después da de alta el material, apunta la timeline al ítem nuevo (`ProjectsStore::retarget_timeline_asset`), lo quita del proyecto (`remove_material`) y borra el original. Devuelve el DTO.
  - **Segmento por referencia** que comparte archivo con otros: el archivo se queda y la timeline se empareja solo por id.
  - **Si algo falla tras copiar,** se deshacen el alta y la copia como en el `except` de Python, incluido el caso en que deshacer también falla.
- **`storage.py`:** `safe_file_name` (`safe_name`) y `resolve_project_media` (`resolve_media`). **`segments.is_shared_file`**, interna de la biblioteca.

**Verificación:**
- **Casos:** 42 pasos (12 de error, 10 con el mensaje que ve el usuario) con el `library.py` y el `projects.py` REALES sobre un `data/` y unas carpetas temporales, con `uuid4` y `_now` fijados (`library_cases.SAVE_STEPS`). Tras cada paso se comparan el resultado, `library.json`, `projects.json` y todo lo que hay en disco. Cubren:
  - los cuatro errores, un archivo fuera de la carpeta (`../../`) y la propia carpeta (`..`);
  - clips sin extensión, en subcarpeta, con punto final, con NaN e infinito en `start`/`end`, con etiqueta larga o que se queda vacía, de YouTube, y segmentos compartidos y con id propio;
  - audios (uno con una clave `index` que `remove_material` no encuentra, para forzar la vuelta atrás), imágenes y una carpeta de proyecto propia;
  - bibliotecas rotas: JSON roto, `items` como `{}`, `[1]` y `"x"`.
- **Tus proyectos:** una copia con un archivo diminuto para el primer clip, audio e imagen de un proyecto sin carpeta propia. Los tres guardados salen como en Python.
- **Mutación:** 21/21 (`tools/dev/mutantes.py library`). Con eso salieron tres casos que faltaban (un clip con id propio solo en su archivo, una etiqueta que se queda vacía y la propia carpeta del proyecto) y un detalle de la vuelta atrás: si no se puede guardar `library.json`, la copia se queda, como en Python.
- **Resultado:** 105 tests (103 de Catch2 y 2 de la CLI) y 118 381 aserciones en verde, en Debug y en Release, con 0 warnings; tidy, 0 avisos en 72 archivos.

**Revisión del subagente (CORREGIR: un bloqueante), ya aplicada:**
- **Bloqueante:** `resolve_project_media` unía el texto «kind/filename», y Python hace `base / kind / filename`, donde una raíz o una unidad en `filename` sustituye la ruta. Con `/c9.mp4`, Python da «Archivo no encontrado.»; C++ daba por buena `video/c9.mp4`, la copiaba, quitaba el clip y la BORRABA. Con una ruta absoluta dentro de la carpeta o con `C:c9.mp4`, Python guarda y C++ no. Ahora `pathlib_join` (`disk_scan`) une como pathlib y `resolve_inside` acepta varias partes y el modo estricto. Hay casos en las fixtures (`/c0.mp4`, la absoluta y `{DRIVE}c0c.mp4`) y mutantes.
- **Dispositivos de Windows:** `NUL`, `CON`, `COM1` y el resto, también con espacios o puntos detrás, se rechazan. Python falla con OSError, y C++ los daba por archivos que existen y se inventaba un material. Hay un test unitario: no van en las fixtures porque el OSError de Python cambiaría la secuencia de uuids.
- **Fixtures:** cubren `reframe`, `face_track`, `provider`, `external_id`, `author`, `license_info`, una descripción vacía con `text`, y un audio de YouTube con duración 0.0.
- `py_float` comprobado en la duración. Nota sobre los tipos de error al deshacer.
- **Contraste de la revisión, fuera del repo:** 235 escenarios contra `library.py`/`projects.py`, comparando resultado, `library.json`, `projects.json`, disco, orden de claves y 1 frente a 1.0. Todo coincide salvo lo de arriba.
- **Mutación:** 25/25 (`tools/dev/mutantes.py library`; 4 nuevos: unir como texto, NUL, y las dos reglas de `pathlib_join`).
- **Resultado:** 47 pasos (13 de error, 11 con mensaje); 106 tests (104 de Catch2 y 2 de la CLI) y 118 426 aserciones en verde, en Debug y en Release, con 0 warnings; tidy, 0 avisos en 72 archivos.

**Siguiente:** `stick_library.py` y lo que falta de `clip_motion`. F3 sigue esperando al Windows SDK.

### 2026-10-08 — Revisión de estado tras cuatro tandas

El autopiloto pidió una revisión de conjunto (colecciones, almacén de proyectos, JSON como Python y «Guardar en la biblioteca», cada una ya revisada por su cuenta). **APROBADO, sin bloqueantes.**
- **Compilación desde cero** (`verificar.sh --limpio`): 106 tests y 118 426 aserciones en Debug y Release, con 0 warnings; tidy, 0 avisos.
- **Fixtures:** las 14 sintéticas, regeneradas con el Python real en una copia aparte, salen idénticas byte a byte a las del repo.
- **Mutantes:** el de SFX «resolve: sale de la carpeta» buscaba un texto que cambió con `pathlib_join`. Ya lo encuentra y se detecta. Además, `mutantes.py` ahora termina con error si algún mutante no encuentra su texto; antes imprimía `??` y salía con 0.
- **Docs:**
  - las cifras de SFX (207 pasos) y ajustes (67);
  - el estado del plan («F1 casi cerrada (faltan las colecciones)»);
  - ARQUITECTURA, que aún daba por pendientes el autoguardado y el historial;
  - la nota de qué lector usa cada almacén;
  - casillas en el TODO para los personajes stick y lo que falta de `clip_motion`, para que el autopiloto las vea.

### 2026-10-08 — Cambio de alcance: sin Motion ni editor de Paper; efectos en el inspector

Decisión de Edu (§6):
- **Motion Studio fuera por ahora**, con su stick (`motion/stick.js`, `stick.py`). F8 queda vacía; las demás fases mantienen su número. La decisión 3 de §13 queda aplazada. Los personajes stick de la biblioteca (`stick_library`) siguen en F1: son clips normales.
- **Paper ya es un efecto** (`paper_cutout`, en el catálogo): el editor de Paper no se porta. Sus opciones de aparición (pliegue, vibración) son parámetros del efecto, y el efecto se porta en F4 con los demás.
- **Pestaña Efectos en el inspector (F5):** aparece al añadir un efecto al clip, con una subpestaña por efecto (en el orden de `vfx` y reordenable) y sus propiedades generadas desde los `params` del catálogo.
- Estimación total: de ≈ 56–81 a **≈ 50–71 semanas**.

### 2026-10-08 — F1: personajes stick (stick_library.py)

*(Entrada añadida el 2026-10-10: la tanda se cerró con su commit, `31eadb7`, sin entrada en este registro.)*
- **Hecho:** `stick_library.hpp` con la lista, el detalle agrupado por expresión, crear, añadir archivos a una expresión y cambiar la expresión sin mover el archivo. En `py.hpp`: `py_title`, `py_isdecimal` y `py_nfkd_fold`, con tablas generadas desde el Python de video-yt (`tools/gen/gen_py_text.py`). `disk_scan` comparte ahora `sorted_subdirs`, `clean_name` y `resolved_inside` con las colecciones.
- **Verificación:** 245 pasos sobre una carpeta temporal (54 de error) y tu `scientist_stick`; 57/58 mutaciones (la otra es equivalente). Detalles y divergencias en las notas de F1 del TODO.

### 2026-10-10 — F2: la envolvente de un sonido (lo que faltaba de `clip_motion`)

**Hecho:**
- **`clip_motion.hpp`:** `downsample_envelope`, `normalize_amps`, `overlap_window`, `audio_src_at` y `rms_envelope`. Esta última es el núcleo de `sample_rms_envelope`: recibe las muestras mono a 400 Hz que da ffmpeg (`-ac 1 -ar 400 -f f32le`), y decodificarlas es de `gc_media`. Es lo que necesita la herramienta MCP `animate_clip` con `follow_audio_id` (F7); la op `animate_clip` ya recibía la envolvente hecha.
- **El `sum()` de Python ≥ 3.12 es compensado** (Neumaier), no la suma ingenua: el RMS lo repite paso a paso, y así sale bit a bit.

**Verificación:**
- **Fixtures (`tests/fixtures/motion`, `motion_cases.py`):** 108 `downsample` (10 de error, `max_n` negativo, 0 y 1), 20 `normalize` (pico 0 y −0,0, NaN delante y detrás, infinitos), 85 pares visual/audio en 4 timelines sintéticas con 1228 instantes (velocidades, `in_point`, texto, imagen y figura, 0,02 s justos) y 31 RMS con el `sample_rms_envelope` REAL y un ffmpeg falso que devuelve las muestras del caso (11 553 muestras; rates 0, 1, 3, 7, 400 y 1000, NaN, infinitos y trozos que desbordan).
- **Tus datos:** los 16 018 pares (visual, audio) de tus 18 timelines (2637 con solape, 34 673 instantes) y 60 RMS de tus audios con las muestras que da el ffmpeg real (192 079 muestras).
- **Mutación:** 22/22 (`tools/dev/mutantes.py motion`). El de «compensación aunque no sea finita» se escapaba hasta que se añadió un caso con trozos finitos y luego uno infinito (si todos son infinitos, el resultado normalizado es el mismo).

### 2026-10-10 — F0/F3: MSVC y vcpkg

- **MSVC 14.50** (Visual Studio 2026, en `D:\visual`) compila con 0 warnings y pasa los 112 tests. Hizo falta: bucles en vez de `const auto* it = std::ranges::find(...)` sobre `std::array` (en MSVC el iterador no es un puntero); `Json(sv)` en las comparaciones con `string_view` (ambiguas en C++20 con el operador reescrito); `py_json_dumps` iterativo (recursivo, con 1 MB de pila, se caía en Debug antes de los 1500 niveles; el límite se queda); `_CRT_SECURE_NO_WARNINGS` y `/wd4702` en los tests.
- **vcpkg** en `E:\vcpkg` (el de VS no trae los puertos), con la caché binaria en `E:\vcpkg-cache` y `builtin-baseline` fijado. `msvc-vcpkg-debug` configura y compila con nlohmann-json, spdlog 1.17 (fmt 12) y Catch2 de vcpkg. FFmpeg va en la feature `media` (LGPL, sin x264/x265; nvcodec, amf y qsv), activada en esos presets; falta construirla.
- **Revisión del subagente** (tanda de stick): APROBADO, sin bloqueantes. Faltaba la entrada del §14 de stick (añadida arriba) y la cabecera del plan estaba atrasada.

### 2026-10-10 — F3: cabeceras de imagen (images.probe_gif y _ext_from_magic)

- **Hecho:** `image_probe.hpp` (en `gc_core`: lee bytes, sin dependencias). Cuenta fotogramas, suma retardos (0,1 s si no hay o son 0), lee dimensiones, transparencia y repetición (NETSCAPE2.0 con contador 0; sin él, un GIF animado repite) y saca la extensión por la firma. Un archivo cortado en mitad de un bloque devuelve solo las dimensiones, como el `except` de Python.
- **Verificación:** 290 GIF sintéticos (a mano, de Pillow y todos los truncados de dos de ellos) y 21 firmas, contra el `images.py` real; tus 1606 imágenes (la firma de todas; el único GIF, entero). 14/14 mutaciones (`tools/dev/mutantes.py media`).
- **Resultado:** 112 tests (110 de Catch2 y 2 de la CLI) y 176 065 aserciones en verde con MinGW (Debug y Release) y MSVC (Debug), 0 warnings; tidy, 0 avisos en lo tocado.

**Siguiente:** construir FFmpeg con vcpkg y portar las sondas de ffprobe (`probe_duration`, `_color_untagged`, `_has_audio`) a `gc_media` con libavformat.

### 2026-10-10 — F3: FFmpeg y las sondas de medios (`gc_media`)

- **Hecho:** FFmpeg 9.0.2 construido por vcpkg (feature `media`: LGPL con zlib, nvcodec, amf y qsv; unos 10 minutos). Nace `gc_media` (`src/media`), que solo se compila si `find_package(FFMPEG)` lo encuentra (presets `msvc-vcpkg-*`); la MinGW de CLion sigue sin él. `probe_media` sustituye a las tres llamadas a ffprobe de video-yt: `sound_design.probe_duration` (la duración del formato, por el mismo texto `%f` y el mismo `round`), `compose._color_untagged` y `compose._has_audio`.
- **Trampas:** ffprobe imprime «N/A» cuando la duración es `AV_NOPTS_VALUE` (no cuando es 0); el puerto de vcpkg desactiva zlib si no se pide, y sin zlib no hay decodificador de PNG (su `color_space` salía «unknown» en vez de «gbr»). Las dos salieron en las fixtures, con PNG sintéticos y tuyos.
- **Verificación:** 17 medios sintéticos generados una vez con ffmpeg (`tests/fixtures/media/files`, en git-lfs) y 2 rutas que no existen, contra las funciones de Python con su ffprobe; 120 de tus medios en su sitio (MP4, WAV, WebM, PNG, GIF y JPEG). Exacto. `tools\dev\verificar_msvc.bat`: 114 tests y 176 481 aserciones, 0 warnings. Los mutantes aún no cubren `gc_media` (`mutantes.py` compila con MinGW).

**Siguiente:** demux y decode con seek exacto en `gc_media`.

**Revisión del subagente:** APROBADO, sin bloqueantes. Se aplicaron sus menores: `color_untagged` compara también con `AVCOL_SPC_UNSPECIFIED` (no solo con el nombre «unknown», que podría cambiar en otra versión), y hay 4 medios más con varios streams (un MP3 con carátula etiquetada, un MKV con el audio antes que el vídeo y dos MKV con dos vídeos, el primero con y sin etiqueta), que es justo la semántica de `-select_streams v:0` y de `has_audio`. Queda sin reproducir, a propósito, el `timeout=10` de `probe_duration` (un archivo que cuelgue ffprobe da 0.0 en Python); se decidirá al integrarlo.

### 2026-10-10 — F3: la entrada del export (clip_kind.py)

- **Hecho:** `export_input.hpp` en `gc_core`: `is_still_clip`, `ffmpeg_trim_window` (still en bucle desde 0 con un mínimo de 0,1 s; vídeo con la salida adelantada `max(0,04, 1/fps)` si toca el final del archivo), `ffmpeg_trim_filter` y `ffmpeg_input_args`, con los mismos textos que Python. Es la especificación de lo que tendrá que hacer el decode de `gc_media` con cada material.
- **Trampa:** Python escribe «nan» sin signo y `std::format` escribiría «-nan» con el NaN negativo que da x86 en `inf − inf` (una imagen con entrada y salida infinitas); hay un caso para eso.
- **Verificación:** 21 clips sintéticos (la ventana y el filtro con 7 fps; la entrada con 11 rutas y 2 fps) y tus 979 clips (con el fps de su timeline y la ruta de `compose._clip_path`), contra el `clip_kind.py` real; exacto. 11/11 mutaciones. MinGW: 114 tests y 181 057 aserciones en Debug y Release, 0 warnings; tidy, 0 avisos.

**Siguiente:** el decode de `gc_media` que reproduce `ffmpeg_input_args` + `fps=F,trim,setpts`.

**Revisión del subagente:** APROBADO (11/11 mutaciones, 114/114). Solo ajustó cómo se cuentan los casos (commit aparte).

### 2026-10-11 — F3: el decode de `gc_media`, como el export

- **Hecho:** `decode.hpp`. `export_decode_spec(clip, ruta, fps)` arma la entrada (`ffmpeg_input_args`) y la cadena que el export aplica a cada material antes del encuadre (`{color_pre}{trim},setpts=PTS-STARTPTS`, con `color_pre` = `setparams` a BT.709 si no es un still y el material no declara matriz). `decode_frames` la ejecuta con libavformat, libavcodec y libavfilter como el CLI: opciones del demuxer, `-c:v`, `-t` como `trim=durationi`, `pts = best_effort_timestamp`, el grafo armado con el primer fotograma (formato, tamaño, SAR, matriz y rango), `flags=bicubic` y `format=rgba`. `md5_hex` (av_md5) para las fixtures.
- **Verificación:** 13 casos (MP4 sin etiqueta y BT.709, MOV, MKV VP8, AVI MJPEG, un VFR con un hueco de 1 s antes, dentro y cruzándolo, un PNG en bucle y un GIF en bucle y una vez), 214 fotogramas: la misma entrada, la misma cadena y el mismo MD5 por fotograma que el ffmpeg del PATH (`-vf "<cadena>,format=rgba" -f rawvideo`), a la primera. MSVC + vcpkg: 117 tests y 181 984 aserciones, 0 warnings; MinGW (sin FFmpeg): 114 y 181 057.
- **Pendiente:** el seek exacto (`frame_grab.extract_frame`, `-ss t` antes de `-i`), el autogiro de los vídeos del móvil, el WebM VP9 con alfa (libvpx) y el hwaccel.

**Revisión del subagente:** APROBADO, con dos diferencias de paridad con el CLI que ya están arregladas: el CLI desplaza las marcas de tiempo con `ts_offset = −start_time` del contenedor (con un MKV que empieza en 0,5 s, el CLI daba 24 fotogramas y el C++ 9) y salta los paquetes que no se pueden decodificar (con 300 bytes invertidos a mitad de un H.264, el CLI daba 49 fotogramas y el C++ abortaba). Hay 3 casos nuevos en la fixture (16 casos, 307 fotogramas) y se comprobó con dos mutantes a mano que, sin cada arreglo, el test falla. Menores: comprobación de memoria en `av_buffersrc_parameters_alloc` y de `-t` al leerlo. MSVC + vcpkg: 117 tests y 182 197 aserciones, 0 warnings.

### 2026-10-11 — F3: el seek exacto (frame_grab.extract_frame)

- **Hecho:** `frame_at(ruta, t)` en `gc_media`: los argumentos de `extract_frame` (`-ss {max(0, t):.3f}` antes de `-i`, `-frames:v 1`) y el decode con «-ss» como el CLI: `avformat_seek_file` al instante más el `start_time`, `ts_offset = −(ss + start_time)` y `trim=starti=0`, así que sale el primer fotograma con pts ≥ t; nada si `t` cae después del último. El escalado y el JPEG de `extract_frame` son presentación y no se comparan.
- **Medido antes de portar:** la sincronización CFR de la salida no cambia el fotograma; testsrc2 repite contenido entre fotogramas, así que el índice por MD5 engaña y hay que contar fotogramas.
- **Verificación:** 46 instantes en 8 medios (justo en un fotograma, entre dos, negativos, NaN, dentro del hueco del VFR, más allá del final, PNG y GIF), con el mismo MD5 que el ffmpeg del PATH. Sin el `trim=starti=0`, fallan 27 de 200 aserciones (mutante a mano). MSVC + vcpkg: 118 tests y 182 397 aserciones, 0 warnings; MinGW: 114/114.

**Siguiente:** WebM VP9 con alfa (feature `vpx` de vcpkg) y autogiro.

**Revisión del subagente:** APROBADO. Menores aplicados: un instante que no cabe en µs (`frame_at(…, 1e300)`) es un error y no un seek con basura, como el «Invalid duration» de ffmpeg (hay test); si hubo errores de decodificación y no salió ningún fotograma, el decode devuelve error en vez de «nada en ese instante» (`extract_frame` lanza ValueError en los dos casos); la fixture del seek saca el `-ss` de una llamada al `extract_frame` REAL (se intercepta su `subprocess.run` y se comprueba que va antes de `-i` con `-frames:v 1`), y sale idéntica. MSVC + vcpkg: 119 tests y 182 398 aserciones, 0 warnings.
