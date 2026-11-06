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
- **Sin problema:** Skia (BSD), libvpx y dav1d (BSD, decodificadores de VP9 con alfa y de AV1 dentro de FFmpeg), ONNX Runtime (MIT), whisper.cpp (MIT), OpenCV (Apache 2) y miniaudio (dominio público o MIT-0).
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

### 2026-10-11 — F3: WebM VP9 con alfa (libvpx)

- **Hecho:** la feature `vpx` del FFmpeg de vcpkg (libvpx 1.16, BSD; la recompilación de FFmpeg con ella tarda unos 25 minutos). El export de video-yt fuerza `-c:v libvpx-vp9` para todo `.webm` y los clips `motion` porque el VP9 nativo no expone el alfa; ahora `decode_frames` también lo tiene y el alfa llega al RGBA.
- **Verificación:** un WebM VP9 `yuva420p` (un círculo semitransparente que se mueve; 1937 píxeles transparentes en el primer fotograma), decodificado como vídeo y como motion, con el mismo MD5 por fotograma que el CLI (decode: 18 casos, 334 fotogramas). MSVC + vcpkg: 119 tests y 182 465 aserciones, 0 warnings; MinGW: 114/114.

**Siguiente:** el autogiro (ninguno de tus vídeos lleva matriz de rotación) o pasar ya a la caché de fotogramas, el audio y el encode; y Skia para `gc_render`.

### 2026-10-11 — F0/F3: el primer CI y la revisión del seek y el VP9

- **CI de GitHub (primer push de Edu):** fallaba en los tres sistemas sin culpa de la lógica. Linux: GCC 13 daba un falso positivo de `-Wnull-dereference` al insertar al principio de un vector de `shared_ptr` (`log.cpp`; ahora un vector nuevo con `reserve`). macOS: `INFINITY` es `float` y Apple Clang con `-Wdouble-promotion` lo hacía error (`test_py.cpp`). Los dos comprobados con un `g++-13` en WSL sobre todo el proyecto. Windows: 49 tests con tildes en el nombre no se encuentran porque el runner usa cp1252 y no UTF-8 (pendiente: manifiesto `activeCodePage=UTF-8`). Por decisión de Edu, el CI solo se ejecuta a mano (`workflow_dispatch`).
- **Revisión de `abab9f8..HEAD`** (la anterior se paró a medias): APROBADO, sin bloqueantes. Menores aplicados: el seek ahora también se prueba en contenedores que no empiezan en 0 (`v_offset.mkv`, `va_offset.mp4`: el `-ss` cuenta desde el `start_time`), con paquetes rotos (`v_corrupt.mp4`) y con el WebM alfa (74 instantes en 12 medios), y el test del decode comprueba que el alfa del VP9 llega al RGBA (hay píxeles con alfa < 255), no solo que el MD5 coincida con el CLI.
- **Verificación:** MSVC + vcpkg: 119/119; MinGW: 114/114 en Debug y Release.
- **Queda anotado:** la regla «0 fotogramas con errores de decode ⇒ error» también se aplica al export (`decode_frames`), donde un `trim` vacío sobre un archivo con paquetes rotos daría error y el CLI, nada. Es un caso marginal; revisarlo con la caché de fotogramas.

**Siguiente:** Skia (compilándose con vcpkg en modo clásico) y el arranque de `gc_render`.

### 2026-10-11 — F3: Skia y el arranque de `gc_render`

- **Hecho:** Skia 148 con vcpkg (feature `render`, con sus features por defecto), compilado una vez en modo clásico (11 min) para dejarlo en la caché binaria; el build lo restaura en segundos. `gc_render` (`src/render`, solo si hay Skia) con su primera pieza: `Canvas`, un lienzo raster RGBA (premultiplicado por dentro y sin premultiplicar al leerlo, como `gc::media::RgbaFrame`), con `clear` y `fill_rect`; Skia queda detrás de un pimpl. También `tests/utf8.manifest`, para que el CI de Windows encuentre los tests con tildes.
- **Verificación:** 3 tests del lienzo (dimensiones no válidas, relleno exacto por píxel y src-over semitransparente leído sin premultiplicar). MSVC + vcpkg: 122/122, 0 warnings; MinGW: 114/114 en Debug y Release.

**Siguiente:** dibujar los blits de `frame.hpp` (imagen con transformación afín, recorte y opacidad) y la primera fixture de render contra `compose.py`/ffmpeg.

### 2026-10-11 — F3: `gc_render` compone el fotograma y la primera fixture de render

- **Hecho:** `Canvas::draw_image` dibuja un `Blit` de `evaluate` (la `Affine` del contexto 2D como `SkMatrix`, `drawImageRect` estricto sobre el recorte, bicúbico B = 0, C = 0,6 como el `flags=bicubic` de swscale, y la cortinilla como `clipPath`). `compose_frame` dibuja las capas de fondo a frente sobre el negro opaco del export; texto, figuras, ajuste, modos de fusión y máscaras son Unsupported por ahora. Fixture `tests/fixtures/render` con `compose.render_frame` real (6 fotogramas de una imagen en overlay: posición, escala, giro, opacidad, recorte parcial y una que se sale del cuadro).
- **Verificación:** luma BT.709 de 49,7 a 60,7 dB frente al export sin la banda de 2 px del borde del clip (de 30,1 a 54,1 dB con ella); el objetivo del plan es ≥ 35 dB. Mutante «ignorar la opacidad»: 20,7 dB, falla. 9 tests del lienzo y el render (8 del lienzo y 1 de paridad). MSVC + vcpkg: 128/128, 0 warnings; MinGW: 114/114.
- **Tolerancia justificada:** la banda de 2 px se excluye porque el export redondea el destino del overlay a píxeles pares (`dest_rect_even`) y el C++ lo deja en coma flotante, como el preview; contra el fondo negro, ese píxel de diferencia en el borde basta para bajar el fotograma entero a ~31 dB. Queda como decisión de Edu (redondear o no); si se decide redondear, la banda sobra.
- **Divergencia nueva entre preview y export:** un overlay sin `reframe.crop_w/crop_h` se recorta al aspecto de la salida en el preview y no en el export (17 de los 235 overlays de Edu). Decisión de Edu; el C++ sigue al preview.

**Siguiente:** vídeo (el fotograma de `decode_frames` en el instante del export), más de un clip por fotograma y `genclip frame … --at t`.

### 2026-10-11 — F3, revisión: la fixture de render, sensible a la posición

- **Revisión de `e7b7e31..HEAD`:** APROBADO, sin bloqueantes. Menores aplicados: la fixture apenas veía errores de posición en X (el degradado casi no varía en horizontal), así que hay un material nuevo, `waves.png`, con ondas de ~18 px en el verde en X y en Y. Con él, un mutante que desplaza 1 px en X baja a ~34 dB y falla. Los tres casos con ondas van en posiciones donde el redondeo del export (`dest_rect_even` y el `rotate`, que trunca medio píxel si el cuadro girado sale impar) no mueve nada; donde sí, la luma baja a 30–35 dB solo por eso (dato para la decisión de Edu). `compose_frame` distingue un material sin dimensiones (error de argumento) de un tipo de capa aún no soportado y comprueba que el fotograma no sea nulo; un alfa NaN dibuja opaco, como `globalAlpha` del contexto 2D (test).
- **Verificación:** render frente al export, 9 fotogramas: 48–61 dB de luma sin la banda de 2 px del borde. MSVC + vcpkg: 129/129, 0 warnings; MinGW: 114/114.
- **Anotado sin arreglar:** más redondeos del export que la fixture no ve (el recorte a enteros pares, la pose animada truncada a par), el `crop_w = 0` (1,0 en el export, 0,05 en el preview) y la reducción fuerte, donde el bicúbico de Skia no ensancha el soporte como swscale.

**Siguiente:** vídeo en el render (el fotograma del export en el instante), varios clips por fotograma, PNG con alfa, cortinilla y `genclip frame … --at t`.

### 2026-10-11 — F3: vídeo en el render

- **Hecho** (la fórmula de abajo era incompleta: corregida en «F3, revisión: el fotograma que elige el export»): `gc::media::export_frame_at(clip, ruta, fps, t)`, el fotograma de un clip que el export compone en el instante `t`: el n-ésimo de su cadena (`fps`, `trim`, `setpts`), con n = ⌊(t − max(0, start))·fps·velocidad⌋ (la velocidad como `setpts=PTS/{sp:.6f}` de `video_speed_filters`), contado desde el final si va al revés y el último si la cadena se acaba (`eof_action=repeat`). En un still no cuentan ni la velocidad ni el revés, como en `compose.py`. El test de render toma de ahí el material de cada capa, imágenes incluidas.
- **Verificación:** 13 fotogramas nuevos de `compose.render_frame` con vídeos de ondas que cambian ~1 rad por fotograma: overlay con `in_point` e inicio, 25 fps sobre 30, velocidad 1,5, al revés y una imagen debajo con un vídeo encima. Vídeos: 66,8–67,6 dB de luma (la elección del fotograma coincide con el export en todos); dos clips: 51,5 dB. Mutante «un fotograma de más»: ~24 dB, falla. MSVC + vcpkg: 129/129, 0 warnings; MinGW: 114/114.

**Siguiente:** PNG con alfa, cortinilla y fundidos (`fx.opacity`), y `genclip frame … --at t`.

### 2026-10-11 — F3: alfa, fundidos y cortinilla en el render

- **Hecho:** sin código nuevo en `gc_render`: la fixture de render cubre ahora un PNG con alfa sobre otra imagen (51,1 dB de luma), el fundido de entrada y de salida (52,2–66,9 dB) y la cortinilla (69,7 dB), todos frente a `compose.render_frame`.
- **Divergencia nueva:** el zoom (y el pop) de aparición o salida en un overlay estático crece desde la esquina superior izquierda en el export (`scale=eval=frame` con el overlay fijo en `x=ox:y=oy`) y desde el centro en el preview: 20 dB. Afecta a 7 clips de Edu; queda como decisión suya y el caso, fuera de la fixture.
- **Verificación:** 27 fotogramas de render. MSVC + vcpkg: 129/129, 0 warnings; MinGW: 114/114.

**Siguiente:** `genclip frame <proyecto> <timeline> --at t` (un PNG del fotograma compuesto).

### 2026-10-11 — F3: `genclip frame`

- **Hecho:** `genclip frame <raíz de video-yt> <proyecto> --at t -o salida.png [--omitir]` (solo con FFmpeg y Skia): carga el proyecto, resuelve el material (`gc_core/clip_media.hpp`, espejo de `compose._clip_path`, con la carpeta de colecciones de los ajustes), descarta los clips sin material como el export, toma el fotograma de la rejilla como el `-ss` de salida de `render_frame`, decodifica con `export_frame_at`, compone y escribe el PNG (`gc_render/png.hpp`, codificador de Skia). `compose_frame` ya no dibuja en silencio lo que no sabe: `unsupported_reason` lo rechaza (eliminar fondo, filtros, efectos, vfx, máscaras, fusión y lo que no es material); con `--omitir` se salta con aviso. Lo destapó el primer fotograma real, donde un personaje con croma salía con el fondo verde.
- **Verificación:** tests de `clip_media_path` (proyecto, biblioteca, colección, still sin tipo, rutas que salen de la carpeta; destapó un `string_view` colgando de un ternario con `std::string`) y de `unsupported_reason`; un fotograma real de «l filo del mañana» (2160×3840). MSVC + vcpkg: 131/131, 0 warnings; MinGW: 115/115.

**Siguiente:** lo que el render aún no dibuja, por orden de uso en las timelines de Edu (texto y croma primero).

### 2026-10-11 — F3, revisión: el fotograma que elige el export

- **Revisión de `dbafcda..HEAD`:** CORREGIR, con tres bloqueantes reproducidos contra el `compose.render_frame` real, ya arreglados:
  1. `export_frame_at` (n = ⌊(t − start)·fps·velocidad⌋) fallaba con un inicio fuera de la rejilla (163 de los 195 vídeos de Edu) y con velocidades fraccionarias: las dos `setpts` del export truncan a ticks enteros (`D2TS`). Ahora recibe el tick k, calcula el pts de cada fotograma como el export (⌊⌊m/sp⌋ + start/(1/F)⌋) y elige como el overlay (el primero con pts = k o el último anterior; el último pasado el final). El revés cuenta en una pasada y toma uno en otra, en vez de guardar todos los fotogramas en memoria.
  2. El instante de `genclip frame` y del test: el `-ss` de salida de `render_frame` da el fotograma más cercano, no el primero con pts ≥ T (`export_frame_tick`: k = ⌊at·F + 0,5⌋).
  3. A la cadena le faltaba el `fps=F` que el export pone tras el encuadre: un GIF animado avanzaba sus fotogramas crudos.
  Menores aplicados: los motion graphics solo dan error (o aviso con `--omitir`) si se ven en ese instante y las figuras ya no salen como «sin material»; `unsupported_reason` lee también los efectos escritos como texto.
- **Verificación:** 47 fotogramas de render frente al export (vídeos 66,7–67,7 dB, GIF 51,1 dB); mutante «redondear el inicio en vez de truncarlo»: ~24 dB, falla. MSVC + vcpkg: 131/131, 0 warnings; MinGW: 115/115.

**Siguiente:** el croma (eliminar fondo por color), que el export hace con FFmpeg antes del encuadre; después el texto básico y `genclip render` a MP4.

### 2026-10-11 — F3: croma en el render (adelantado de F4)

- **Hecho:** `gc_core/bg.hpp` con las cadenas de FFmpeg del croma de `clip_bg.py` (`chroma_filters`, `chroma_alpha_ffmpeg`) y la parte del croma de `compose._bg_source_chain` (`chroma_source_chain`: la cadena sola o el subgrafo que separa el alfa, lo limpia y lo vuelve a unir). `export_frame_at` la aplica a cada clip con el croma activo, en el espacio del material y antes del encuadre, como el export; el desenfoque del borde va en px del alto real del material. `unsupported_reason` deja pasar el croma y sigue rechazando el matte automático y el contorno.
- **Verificación:** paridad de las cadenas con las funciones reales (16 croma crudos × 5 altos) y 5 fotogramas con croma frente a `compose.render_frame` (imagen, derrame, limpieza del borde y vídeo): 50,7–51,1 dB. Mutante «alto fijo de 512 en la limpieza del borde»: 33,1 dB, falla. Fotograma real de «l filo del mañana» con el personaje recortado. MSVC + vcpkg: 133/133, 0 warnings; MinGW: 117/117.

**Siguiente:** el texto básico (679 clips de texto en las timelines de Edu), el siguiente en uso.

### 2026-10-11 — F3, revisión: croma y fotograma elegido, aprobados

- **Revisión de `0277318^..HEAD`:** APROBADO, sin bloqueantes. Confirmó la corrección del fotograma elegido con ~45 fotogramas más de su arnés (timelines a 24, 25 y 60 fps, inicio negativo, revés con inicio fuera de la rejilla, GIF con velocidad y revés) y el croma con velocidad, revés, GIF y relleno con recorte. Menores aplicados: `genclip frame` da error con la timeline vacía o un fotograma fuera de ella, como `render_frame` (`export_total_duration` en `gc_core`, con test), y con un `--at` enorme; `export_frame_at` rechaza un inicio cuyo pts no cabe en 64 bits; la fixture suma un vídeo con inicio negativo y croma en un vídeo al revés a velocidad 1,5; cifras del TODO al día.
- **Verificación:** 56 fotogramas de render. MSVC + vcpkg: 134/134, 0 warnings; MinGW: 118/118 (181 193 aserciones).

**Siguiente:** el texto: portar `text_ass.build_ass` a `gc_core` con paridad exacta del `.ass` y dibujarlo después con Skia.

### 2026-10-11 — F3: texto, etapa 1a: estilo y métricas de fuente

- **Hecho:**
  - `py_upper` (tabla generada) y `py_is_space` público.
  - `gc_core/font_metrics`: espejo de `font_metrics.py`, con `resolve_font_path`, `font_line_height_em` y `ass_size_factor`. Las carpetas van en `FontDirs` y Anton (OFL) está en `resources/fonts`.
  - `gc_core/text_style`: los cálculos puros de `text_ass.py`, es decir, estilo efectivo, efectos de palabra, opacidades, alfa y color ASS, escala X/Y, Fondo (color y si va dibujado), palabra activa, May./min., sombra y Brillo, posición y Curva.
  - Donde Python lanza una excepción (un color que no es texto, un ángulo de sombra infinito), el C++ toma la salida segura y lo dice en la cabecera.
- **Verificación:**
  - Paridad exacta con las funciones reales en 155 estilos sintéticos y tus 11 estilos distintos. Los cos/sin de la sombra se comparan con margen de 1e-12.
  - También coinciden 98 casos de May./min. y 128 de palabra activa, 20 TTF mínimos (rotos o no), 4 carpetas de fuentes falsas, Anton y las fuentes de `C:/Windows/Fonts`.
  - 10 mutantes: fallan todos salvo uno, que era código muerto y se quitó.
  - MSVC + vcpkg: 138/138; MinGW: 122/122. 0 warnings.

**Siguiente:** etapa 1b, con `_posed_style`, las ventanas de palabra con `words`, `box_rects` y `curve_glyphs`; después, dibujar el texto en `gc_render`.

### 2026-10-11 — F3: texto, etapa 1b: pose, palabras y geometría

- **Hecho:** en `gc_core/text_style` están ahora:
  - la pose del texto con el giro 3D, el estilo con la pose y si el texto se anima (muestreando a los fps de la timeline);
  - las ventanas y la palabra activa con `words`, lo que deja ver «escribir» y qué se ve en cada instante;
  - las cajas de Fondo y las letras de la Curva.
- **Verificación:**
  - Paridad con las funciones reales en 24 clips sintéticos × 12 instantes, más las filas, cajas y letras de los 155 estilos, con un margen de 1e-12 donde intervienen sin y cos.
  - Local: tus 679 clips de texto (48 940 aserciones).
  - 16 mutantes: fallan todos. Tres necesitaron un caso nuevo: el giro 3D del estilo, el umbral de movimiento y un pico que solo se ve a 24 fps.
  - MSVC + vcpkg: 139/139; MinGW: 123/123. 0 warnings.

**Siguiente:** etapa 2, dibujar el texto en `gc_render` y compararlo con `compose.render_frame` (libass).

### 2026-10-11 — F3: texto, etapa 2a: los textos quietos dibujados con Skia

- **Hecho:**
  - `gc_core/text_events`: los eventos del .ass de un texto quieto y el instante que FFmpeg pasa a libass (el pts en ms, truncado).
  - `gc_render/text`: `TextFonts` (TTF para Skia y HarfBuzz, y la medida de Pillow para partir las líneas) y `draw_timeline_texts`.
  - Qué dibuja: el texto plano con contorno, las alineaciones, el karaoke resaltado, las opacidades, el fundido de entrada, el espaciado entre letras y «escribir». Lo demás da Unsupported con el motivo.
  - `genclip frame` dibuja los textos encima del compuesto.
- **Lo que destapó la fixture:**
  - libass no hace kerning sin «Kerning: yes».
  - libass usa el hinting de FreeType (avances casi enteros, factor de 0,997 a 1,005 según la fuente).
  - Dos fallos del export, anotados como decisiones de Edu: el color con alfa ≥ 0x80 se satura a blanco, y el blanco sale a 235 si hay una imagen PNG debajo.
- **Verificación:**
  - 30 fotogramas frente a `compose.render_frame`: PSNR de 19–33 dB en la zona del texto y de 42–63 dB perdonando 1 px.
  - 13 mutantes del dibujo: fallan todos.
  - MSVC + vcpkg: 140/140; MinGW: 123/123. 0 warnings.
  - Paridad exacta de los eventos con `caption_dialogues` en los sintéticos y los 679 clips de Edu.

**Siguiente:** etapa 2b, con Fondo, sombra y Brillo, los efectos de palabra brillo y pop, y las apariciones pop y deslizar.

### 2026-10-11 — F3: texto, etapa 2b: Fondo, sombra, Brillo, efectos y apariciones

- **Hecho:** `draw_timeline_texts` dibuja ya todos los textos quietos salvo la Curva, el giro y el volteo, la cursiva o el subrayado y la negrita sintética. Eso incluye:
  - el Fondo por línea (BorderStyle 4) y el dibujado (`box_rects` con la forma de `_rrect_path`);
  - la sombra paralela y el halo del Brillo (`\blur`, con σ = 0,85 · N);
  - los efectos de palabra brillo y pop, la Escala X/Y y las apariciones deslizar y pop.
- **Lo que destapó la fixture:**
  - Con trazo, libass desenfoca solo el contorno.
  - La aparición pop no escala el texto en el export (el override de cada palabra repone `\fscx`), pero sí la forma del Fondo dibujado. Queda anotado como decisión de Edu; el C++ copia el export.
- **Verificación:**
  - 51 fotogramas frente a `compose.render_frame`; las 16 escenas nuevas dan 36–50 dB perdonando 1 px.
  - 14 mutantes: fallan todos.
  - MSVC + vcpkg: 140/140; MinGW: 123/123. 0 warnings.

**Siguiente:** etapa 2c, con el texto animado, el giro y el volteo, y la Curva.

### 2026-10-11 — F3: filtros de color (clip_filters.py) en el núcleo y en el render

- **Hecho:**
  - `gc_core/clip_filters`: el catálogo de 15 filtros, la pila del clip (con el `look` antiguo), `filter_matrix`, `stack_matrix`, `adjustment_matrix` (la de la capa de ajuste) y el `colorchannelmixer` del export.
  - `gc_render`: la matriz de la pila va en el `SkPaint` de cada blit, sin premultiplicar y acotada, como el `feColorMatrix` del preview. `unsupported_reason` ya no rechaza `filters` ni `look`.
- **Lo que destapó:** el export pone el desplazamiento del filtro en la columna del alfa (`ra = o`), así que en un píxel semitransparente o en un clip con opacidad fija < 1 el desplazamiento se queda en `o·alfa`. El C++ sigue al preview; tus 3 clips con filtro son opacos (notas de F3).
- **Verificación:**
  - Paridad exacta con el Python real: 34 clips sintéticos, 204 matrices sueltas, el catálogo y tus 914 clips que se dibujan.
  - Render frente a `compose.render_frame`: 9 fotogramas nuevos, 41–57 dB. Los mutantes «sin desplazamiento» y «sin matriz» fallan.
  - MSVC + vcpkg: 141/141; MinGW: 123/123. 0 warnings y 0 avisos de clang-tidy.

**Siguiente:** arreglar el hallazgo de la revisión del texto (la medida de Pillow que `draw_timeline_texts` deja puesta para todo el proceso) y después las máscaras (2 de tus vídeos) o la mezcla de audio.

### 2026-10-11 — F3, revisión: el texto (etapas 1a–2b)

- **Veredicto:** aprobado. Se arregla el hallazgo 1: la medida de Pillow que `draw_timeline_texts` dejaba puesta para todo el proceso ahora solo vale mientras dibuja (`core::ScopedTextWidth`), con test; antes, los tests del texto fallaban si corrían después del render en el mismo proceso. También salen los 2 avisos de clang-tidy de `test_text_parity.cpp`.
- **Los otros dos (menores), también:** `text.cpp` lee el estilo con el `float()`/`int()` de Python del núcleo (un `size` como « 0.06 » ya no da Unsupported; escena nueva en la fixture del texto, 52 fotogramas) y el margen del desenfoque queda acotado a 1024 px.
- **Verificación:** MSVC + vcpkg: 142/142; MinGW: 124/124. 0 warnings y 0 avisos de clang-tidy en lo tocado.

### 2026-10-11 — F3: máscaras de clip en el render

- **Hecho:** `gc_render/mask` rasteriza las máscaras como `clip_mask.py` (en coordenadas del cuadro: lineal, rollo, rectángulo redondeado, círculo, estrella, corazón y pincel, con difuminado, invertir, opacidad e intersección) y `compose_frame` dibuja el clip en una capa que recorta con ese alfa, como el `maskedmerge` del export. La máscara de texto sigue en Unsupported.
- **Lo que destapó:**
  - El antialias de OpenCV ensancha las formas ~0,7 px por lado (cubre del todo los píxeles cuyo centro cae en el borde). Skia las dibuja con ese margen: de 42 a 45 dB.
  - El difuminado reproduce `cv2.GaussianBlur` (borde `REFLECT_101`) y, con σ ≥ 32, la reducción de `_feather` (INTER_AREA + INTER_LINEAR).
- **Verificación:**
  - 10 fotogramas nuevos frente a `compose.render_frame`: 44,7–45,3 dB. 6 mutantes (sin máscara, sin invertir, sin difuminado, sin opacidad, sin giro en x y en y): fallan todos.
  - MSVC + vcpkg: 142/142; MinGW: 124/124. 0 warnings.
- **Pendiente:** tu clip con máscara (Snowpiercer) es el único vídeo AV1 de tus proyectos y el FFmpeg de vcpkg no trae decodificador de AV1: siguiente paso, la feature `dav1d`.

### 2026-10-11 — F3: AV1 en gc_media (dav1d)

- **Hecho:** la feature `dav1d` del FFmpeg de vcpkg (libdav1d, BSD-2). El FFmpeg de vcpkg no traía decodificador de AV1 (el nativo solo funciona con hwaccel) y tu único vídeo AV1, el de Snowpiercer con la máscara, no se podía decodificar.
- **Verificación:** un MP4 AV1 y uno HEVC nuevos en las fixtures de decode (20 casos) y de seek (80 instantes), con el mismo MD5 por fotograma que el ffmpeg del PATH. `genclip frame` de Snowpiercer en 0,5 s: 35,2 dB frente a `compose.render_frame`. MSVC + vcpkg: 142/142.

### 2026-10-11 — F3: las cadenas de audio del export

- **Hecho:** `gc_core/audio_export` arma el grafo de audio del export como `compose.build_command`: la cadena de cada clip que suena (tramo, formato, velocidad, efectos de sonido por etapas, volumen con su envolvente de keyframes y retraso) y la mezcla con `amix`, `apad` y `loudnorm`. `export_inputs` decide qué clips entran y en qué orden, así que los índices de entrada coinciden.
- **Verificación:**
  - Paridad exacta con el `build_command` real (la parte tras «[vout]», con el material simulado): 12 timelines sintéticas y tus 17; y las piezas sueltas en 27 clips sintéticos y tus 300.
  - 13/13 mutantes del audio y 7/7 de los filtros de color (`tools/dev/mutantes.py`).
  - El barrido de `genclip frame --omitir` por 65 fotogramas de tus 17 timelines no omite ni falla ninguna capa.
  - MSVC + vcpkg: 144/144; MinGW: 126/126. 0 warnings y 0 avisos de clang-tidy.

**Siguiente:** ejecutar ese grafo con libavfilter en gc_media (y medir la sonoridad contra el export, ±0,5 LU), y después el encode y `genclip render`.

### 2026-10-11 — F3: el audio del export, ejecutado con libavfilter

- **Hecho:** `gc_media/audio` (`mix_audio`) ejecuta el grafo de `export_audio_graph` como el CLI: una fuente por cada entrada de audio, con las marcas de tiempo tratadas como en `ffmpeg_dec.c`, y a la salida el formato del AAC del export y el `-t` de salida.
- **Lo que destapó:** una duración escrita como texto en un filtro sin unidad son segundos (`durationi=6050000` no recorta nada); el CLI la pone en µs por la API. Arreglado también en el `-t` de entrada del decode de vídeo, donde estaba latente.
- **Verificación:**
  - Frente al CLI con las entradas y el grafo del `build_command` real: el mismo PCM (mismo MD5) en tus 16 timelines con audio y en 2 de las 5 mezclas sintéticas; las otras 3 (con MP3) a menos de 1e-7 dB.
  - 4 mutantes: fallan todos.
  - MSVC + vcpkg: 146/146; MinGW: 126/126. 0 warnings.

**Siguiente:** el encode (vídeo H.264 por hardware o software y AAC) y `genclip render`.

### 2026-10-12 — F3: encode MP4, lector por clip y `genclip render` (empezado)

- **Hecho:** OpenH264 en el FFmpeg de vcpkg; `Mp4Writer` (H.264 con la elección y la sonda de `gpu.py`, BT.709 de rango TV, AAC y `+faststart`); el decode en modo «tirar» y `ClipFrames`, igual que `export_frame_at` tick a tick sin redecodificar; y `genclip render`, escrito y compilando.
- **Verificación:** tests nuevos del encode y de `ClipFrames`. MSVC + vcpkg: 148/148; MinGW: 126/126. 0 warnings.
- **Sin probar aún:** `genclip render` de punta a punta (en Debug, un proyecto de 15 s pasó de 10 min). Pausa pedida por Edu: lo pendiente está en las notas de F3.

### 2026-10-16 — F3: `genclip render` de punta a punta, y por qué el render va lento

- **Hecho:** primera prueba completa con «app launcher» (15 s, un vídeo en overlay): el MP4 tiene la misma duración, el mismo número de fotogramas y el mismo formato que el del export real. `compose_into` compone sobre un lienzo que se reutiliza y los textos van en el mismo, sin la segunda pasada bicúbica de todo el cuadro; el codificador lee los píxeles del lienzo sin copiarlos (`opaque_pixels`). `--tiempos` da el desglose por fase. De 354 s a 83 s (el export de Python, 5,1 s).
- **Lo que destapó:** Skia compilado con MSVC usa su *raster pipeline* escalar (`SKRP_CPU_SCALAR`: SSE/AVX solo con Clang o GCC). Es casi todo lo que queda del tiempo (171 ms por fotograma).
- **Verificación:** frente a `compose.render_frame`, 31 dB de luma por la geometría par del export (580 px en vez de 581,2); con geometría entera igual en los dos, 58,7 dB. MSVC + vcpkg: 148/148 (Debug y Release); MinGW: 126/126. 0 warnings.

**Siguiente:** Skia con clang-cl (overlay del puerto) y volver a medir; luego repartir el trabajo en hilos.

### 2026-10-16 — F3: Skia con clang-cl, `gc_export` en tubería y el export más rápido que el de Python

- **Hecho:**
  - Skia como overlay de vcpkg compilado con clang-cl (con MSVC su dibujo raster iba píxel a píxel): componer pasa de 171 a 21 ms por fotograma.
  - `gc_export`: la carga del proyecto y `render_project`, en tubería (un hilo evalúa y decodifica en orden, varios componen y pasan a YUV, otro codifica). `genclip render --hilos`.
  - `ClipFrames` salta a un fotograma clave antes del tramo (en vez de decodificar desde el principio del archivo, como el export) y decodifica por adelantado en su propio hilo. OpenH264 con hasta 4 hilos y *slices*.
  - La medida de texto de `ScopedTextWidth` es por hilo.
- **Verificación:**
  - Paridad del export entero frente al MP4 de `compose.render` (2 timelines sintéticas): cada fotograma ≥ 35 dB de luma (peor 40,5 y 45,0) y el audio por bloques de 100 ms (±0,5 dB). El salto previo, tick a tick frente a decodificar desde 0, con 4 vídeos nuevos (B-frames, GOP abierto, VP9, MKV que no empieza en 0). Mutantes: audio +1 dB, un fotograma de desfase y saltar después del tramo; fallan los tres.
  - «App launcher»: 2,6 s frente a 5,1 del export. Snowpiercer: 32–34 s frente a 100, con la misma sonoridad (−15,7 LUFS); lo que difiere en la imagen son divergencias del export ya apuntadas y una nueva (el tiempo de los efectos con un inicio fuera de la rejilla).
  - MSVC + vcpkg: 151/151 (Debug y Release); MinGW: 127/127. 0 warnings.

**Siguiente:** medir con el PC tranquilo y con los proyectos de 4K; ver por qué componer Snowpiercer cuesta 87 ms por fotograma. Pendiente de Edu: las decisiones de las divergencias (TODO).

### 2026-10-17 — F3: fusión, capa de ajuste, figuras, texto animado, giro y Curva en el render

- **Hecho:**
  - Los 12 modos de fusión (una capa de Skia con su `SkBlendMode`, fórmulas W3C como el export y el preview) y la capa de ajuste (su matriz sobre lo compuesto debajo).
  - Figuras: la geometría de `shapes.py` en `gc_core/shape_geometry` (exacta) y el dibujo con Skia, con el ensanchamiento del antialias de OpenCV medido.
  - Texto: giro y volteo (la matriz de `\org`/`\frz`/`\frx180`/`\fry180` de libass), texto animado (un evento por fotograma con su pose y la aparición horneada) y la Curva (una letra por evento).
  - `export_fixtures.py --solo render | shapes | texto` regenera solo ese grupo.
- **Verificación:** fixtures de render nuevas (fusión 40,7–46,3 dB; ajuste 44,1–46,3; figuras 37,9–46,5), geometría de figuras (92 casos, exacta), texto (35 escenas nuevas dentro de los umbrales). Mutantes en cada pieza: fallan todos. MSVC + vcpkg: 154/154 (Debug y Release); MinGW: 129/129. 0 warnings.
- **Divergencias nuevas para Edu (TODO):** opacidad y aparición de las figuras, relleno a medio aparecer oscurecido por el export, el blanco forzado de libass en las apariciones de texto animado y la caja del Fondo por línea sin girar.

**Siguiente:** los efectos (`effects`), que el preview y el export hacen distinto (decisión de Edu); la máscara de texto y el texto 3D.

### 2026-10-17 — F3: efectos de clip en el render (como el export)

- **Hecho:**
  - `gc_core/clip_effects`: lo que FFmpeg lee de cada `effects` en la cadena de `effects_ffmpeg` (límites y redondeos al escribir los números).
  - `gc_render/effects`: desenfoque (`gblur`), nitidez (`unsharp`), blanco y negro, sepia, VHS, brillo/contraste/saturación (`eq`), ajustes de color, grano (`noise`) y pixelado, con las cuentas de esos filtros, sobre el clip escalado a su destino y antes de girarlo (`Blit::dst` y `Blit::place`), también en las figuras. El pixelado se queda en el tamaño del clip (el export lo estira al cuadro en overlay).
  - `unsupported_reason` ya no rechaza los efectos; los de vídeo (`vfx`) siguen dando error.
- **Verificación:** paridad exacta de 59 `effects` frente a la cadena real; 17 escenas de render frente a `compose.render_frame` (42,6–46,4 dB de luma), con un suelo propio de luma y de crominancia por escena, que hace fallar los 7 mutantes probados (con solo los 35 dB de luma sobrevivían 3). MSVC + vcpkg: 161/161; MinGW: 130/130 (Debug y Release). 0 warnings y 0 avisos de clang-tidy.
- **Pendiente de Edu:** confirmar que los efectos siguen al export y no al preview (TODO).

**Siguiente:** la máscara de texto y el texto 3D; los efectos de vídeo (`vfx`); medir con los proyectos de 4K.

### 2026-10-17 — F3: texto 3D, con máscara o con fusión (capa propia) en el render

- **Hecho:**
  - `gc_core/text3d`: espejo de `text3d.py`, `text_warp_spec`, `text_is_3d` y la regla de capa propia de `_prepare_texts`.
  - `draw_timeline_texts`: los textos de capa propia (sin vídeo encima) van antes que el .ass global, por orden de inicio; cada uno en un lienzo transparente, deformado con la homografía de `perspective` (con el medio píxel entre índices y centros), recortado con su máscara y fundido con su modo.
- **Verificación:** paridad exacta del núcleo (52 clips × 3 cuadros, 72 proyecciones y tus 679 textos); 14 escenas de render nuevas frente a `compose.render_frame` (27–37 dB estrictos, 41–88 tolerantes), con un suelo por escena; 6/6 mutantes fallan. MSVC + vcpkg 164/164 (Debug y Release), MinGW 133/133. 0 warnings.
- **Divergencias nuevas para Edu (TODO):** lo semitransparente de un texto en capa propia sale como c·α³ en el export; el 3D animado va un fotograma adelantado; y el rango de TV del filtro `ass` sobre RGB también afecta a la capa propia y al .ass que va detrás (la fixture lo corrige para comparar lo demás).

**Siguiente:** la máscara de tipo texto (`_paint_text`, Pillow); después, los efectos de vídeo (`vfx`).

### 2026-10-17 — F3: máscara de tipo texto en el render (como Pillow)

- **Hecho:** `pillow_text` (las medidas de Pillow: métricas y anclas con la aritmética de 26.6 de FreeType, ancho con los avances redondeados) y `paint_text_mask`, que `mask_alpha` usa para las máscaras de tipo texto; las fuentes llegan como parámetro opcional a `mask_alpha`, `compose_frame`/`compose_into` y `unsupported_reason`, y `gc_export` y `genclip frame` las pasan.
- **Verificación:** 180 medidas de Pillow (métricas y ancla vertical exactas), 15 máscaras frente al alfa de `combined_alpha` (29,8–58,5 dB) y 2 escenas de render compuesto (43–44 dB); 5/6 mutantes fallan (el otro mueve 0,3 px). MSVC + vcpkg 166/166 (Debug y Release), MinGW 133/133. 0 warnings.

**Siguiente:** los efectos de vídeo (`vfx`), los textos de una pista con vídeo encima y medir con los proyectos de 4K.

### 2026-10-19 — F0 en Linux y F5: el editor Qt Quick (`genclip-studio`)

- **F0 en Linux:** GCC 13 y 14 y Clang 20 + libc++ con `-DGC_WARNINGS_AS_ERRORS=ON` y 0 warnings. Los tests que solo tienen sentido en Windows se saltan fuera de él. Con libc++ no se compila el studio, porque el Qt de las distribuciones usa libstdc++ y no enlaza.
- **F5:** `apps/studio` con ViewModels en C++ (`HomeModel`, `EditorModel` sobre `EditSession`, `ClipListModel`, `SettingsModel`) y QML compatible con Qt 6.4:
  - inicio con carpetas;
  - timeline: arrastrar con imán y `dropTarget`, recortar, pistas nuevas, grupo, Alt para reemplazar y autodesplazamiento;
  - cabecera de pista, marcadores, selección por área, portapapeles y atajos;
  - panel de material;
  - inspector: transformación, mezcla, velocidad, audio con fundidos, rombo de keyframe, aparición y salida, nota, textos y figuras;
  - ajustes.
- **Lógica en `gc_core`:** `timeline_ui`, `timeline_view`, `timeline_drop`, `clip_pose_edit`, `material_drop`, `clip_layout_ui` y `text_clip_ui`.
- **Verificación de la paridad:**
  - Con el JS real del editor de video-yt, ejecutado con Node: `export_fixtures.py --solo editor`, unas 44 000 llamadas.
  - Las funciones internas de `VideoEditor.jsx` (`applyStaticPose`, `placeAsset`, `newTextStyle`, `changeStyle`, `changeShape`) se extraen del código fuente.
  - Mutantes comprobados en cada tanda.
- **Interfaz entera sin pantalla:** QTest con `QT_QPA_PLATFORM=offscreen` (ratón y teclado). Así salieron varios fallos de la timeline: delegados recreados al seleccionar, la coordenada del gesto y un delegado destruido dentro de su propio manejador.
- **Linux:** 184/184 con GCC y 138/138 con Clang + libc++ (sin el studio).
- **Divergencias que se quedan:**
  - Un keyframe sin id es aleatorio en el JS, así que el rombo no lo puede quitar; en C++ sí se quita.
  - `t: null` en un keyframe de pose: el JS lo toma como t = 0 y la pose de Python (la del export) lo ignora. C++ sigue a la del export.

**Segunda tanda del mismo día:**

- **Inspector en pestañas** (Clip, Color, Audio, Máscara y Animar):
  - Transformación con texto 3D y voltear. Velocidad; un audio estira sus subtítulos enlazados.
  - Textos: temas de subtítulos y estilos «Aa», con tablas generadas desde el JS con `tools/gen/gen_text_themes.mjs`. Trazo, fondo, sombra, brillo, mayúsculas, espaciado y el estilo general de una pista.
  - Figuras. Filtros de color, ajustes de color y capa de ajuste.
  - Efectos sencillos y animados del catálogo (`vfx_stack.hpp`).
  - Efectos de sonido. Máscaras (`mask_edit.hpp`). Animaciones de clip por ranura.
  - Curva del keyframe.
- **Timeline:**
  - Keyframes que se arrastran.
  - Alt+C/V/D: copiar, pegar y duplicar keyframes.
  - Ctrl+Alt+C/V: copiar y pegar atributos.
  - Marcadores que se mueven, nombran y quitan. Beats en el clip.
  - Alt para reemplazar y autodesplazamiento.
- **Material:** proyecto, biblioteca, colecciones y SFX, con búsqueda.
- **Paridad:** la del editor pasa a unas 45 000 llamadas.
- **Sin fixture del JS** (lógica dentro de componentes de React): `transform_view` y la lógica del `EditorModel` (efectos, filtros, atributos, animaciones…). Tienen tests propios y los ha revisado el revisor de la migración contra el JS.

**Recortar (misma fecha):**

- `confirm_crop` (confirmCrop de VideoEditor.jsx) con paridad: clip animado → tamaño en el reframe y keyframe en el cabezal; estático → un único keyframe del reframe. El giro decide si anima con el clip ya recortado, como `setClips`.
- Modal en QML (`CropModal.qml`) sobre `CropEditor`: tiradores, mover, proporciones, rotación, restablecer, scrub y Confirmar. Tests del modelo, del recuadro y de la interfaz sin pantalla (mutante «sin confirmCrop» detectado).
- Solo imágenes, con su aspecto real. Un vídeo avisa hasta gc_media: con un aspecto inventado, el recorte guardado saldría mal (revisión). Un clic en un tirador de la curva sin arrastrar no la cambia, y Enter en el giro o la proporción no confirma.
- **Curva del keyframe:** el editor bézier de EdCurve.jsx (`CurveEditor.qml`, en Canvas porque el Qt de apt no trae QtQuick.Shapes), con `curvePoints` sobre `ease_t`; el arrastre es local y se aplica al soltar, en un solo paso de deshacer.
- **Carpetas de material:** gestionar (raíz, crear, favorita, visible) y subir archivos desde el panel. Lo destapó la prueba: `set_collections_root` usaba el `str(Path)` de Windows también en Linux (una ruta absoluta nunca valía). Ahora usa el del sistema (`py_native_path_str`: Windows igual que antes, `str(PurePosixPath)` en el resto), y lo mismo las rutas que enseñan colecciones y SFX.
- **SFX en disco, la parte que escribe** (F1, pendiente desde entonces): `sfx_create_category`, `sfx_add_sound` y `sfx_update_sound` como `sfx.py` (slug «NN_ETIQUETA», «_2» si el archivo ya está, mover al cambiar de categoría, cantidades y la estrella del favorito). Fixture nueva `sfx/write.json` de 75 pasos que no lleva rutas absolutas: es la primera de disco que corre también en Linux. Mutantes (slug a 30, sin llevar la estrella, cantidad negativa) detectados.
- **Modal de SFX** (SfxClassifyModal.jsx): alta de varios audios y edición de uno, con los borradores de `lib/sfxName.js` (`sfx_name.hpp`, paridad en la fixture del editor) y la escritura de arriba. Sin escuchar el audio hasta el reproductor.
- **Pestaña Efectos:** la pila del inspector pasa a su propia pestaña con desplegables (EdVfxStack) y la biblioteca a una pestaña del material (EdVfxLibrary, con la búsqueda de `listEffects`). El test destapó que el inspector no se refrescaba tras una edición (el modelo solo avisaba de `timelineChanged`); ahora cada cambio avisa también a la selección.
- **Figuras en el material:** pestaña con miniaturas (Canvas sobre `shape_geometry`, sin el plugin SVG de Qt) que se arrastran a la timeline. `place_asset_clip` no tenía la rama de figuras de `placeAsset`; ahora sí, con casos en la fixture del editor (mutante «sin rama» detectado).
- **Textos en el material:** pestaña «Texto» con el texto por defecto y los temas, que se arrastran (`place_text_clip`, la rama de texto de `placeAsset`, con el estilo de la pista; 48 casos en la fixture del editor, mutante «sin tema» detectado) o se aplican con un clic. Las fichas comparten `MaterialTile.qml` con las figuras.
- **Menú del clic derecho en un clip:** lo que no necesita red ni IA (favorito, plantilla global, capas, activar, beats, voltear, atributos, dividir, duplicar y eliminar). Las capas (`move_clip_layer`) con paridad: 3 100 llamadas más en la fixture del editor, mutante «al fondo ↔ al frente» detectado.
- **Beats automáticos (F6, adelantado):** `detect_beats` sobre el audio mono a 22 050 Hz, como `beats.py`, con su propia FFT. Señales sintéticas reproducibles en los dos lados (ruido LCG y golpes con caída exponencial): 120, 90, 174, 128 con silencios, 100 con acentos, 60 y 140 BPM, silencio, ruido y tres entradas cortas (dos fallan como en Python). Solo falta la decodificación, que es de gc_media.
- **Palabras clave de «Explorar» (F7, la parte local):** `extract_keywords`, `parse_keyword_list`, `gather_theme_text` y la sugerencia sin IA, con 96 casos del Python real (texto con tildes, İ/ı/ſ/K por el `re.I`, respuestas de IA con ``` o con texto alrededor, tipos raros en la lista). Un mutante es equivalente: la segunda condición del filtro de Python nunca se cumple.
- **Linux:** 203/203 con GCC y 142/142 con Clang + libc++.

**Siguiente:**
- El preview espera a `gc_render`.

### 2026-10-22 — F0 con MSVC de nuevo y F6/F2: el audio mono de los beats y la envolvente

- **MSVC:** lo escrito en Linux el 19 no compilaba con MSVC 14.50 (`Json == string_view` ambiguo, un iterador de `std::array` usado como puntero y `Json` → `optional<Json>` con dos conversiones, en `clip_layout_ui`, `clip_pose_edit`, `material_drop` y `text_clip_ui`). Arreglado sin cambiar nada de lo que hacen; `msvc-vcpkg-debug` vuelve a 0 warnings.
- **`gc::media::decode_mono`:** el `ffmpeg [-ss s -t d] -i x -ac 1 -ar N [-t m] -f f32le -` con el que leen el audio `beats.decode_mono` (22 050 Hz, hasta 20 min) y `clip_motion.sample_rms_envelope` (400 Hz), sobre la maquinaria de `mix_audio`. Encima, `gc::media::detect_beats(ruta)` y `gc::media::sample_rms_envelope(ruta, inicio, duración)`: con esto, beats y envolvente ya no esperan a nada.
- **Lo que hace el CLI y hubo que copiar** (lo destapó la fixture):
  - Con `-ss` de entrada: salta al instante, resta también el `-ss` de las marcas y pone `atrim=starti=0`; con `-t` de entrada, un `atrim` con `durationi` antes del grafo.
  - **El orden en que se crean los filtros decide dónde se inserta el `aresample` automático.** El CLI crea el grafo, luego cada entrada (abuffer y su recorte) y al final la salida (abuffersink, aformat y el recorte del `-t`). Creando la cola antes de las entradas, el remuestreo a 400 Hz caía delante del `atrim` de la entrada y el resultado era otro (hasta 60 dB en algún bloque). `mix_audio` usa ahora el mismo orden y sigue saliendo igual que el CLI.
  - **El `-t` de salida también corta en el codificador** (`check_recording_time`): un bloque que empieza en o después de `-t` no sale. Solo se nota si el audio no empieza en 0: en una grabación tuya de 28 min con el AAC a 0,023 s, el CLI da 368 muestras menos que el `atrim` solo.
- **Paridad** (`export_fixtures.py --solo mono [--real]`, `tests/fixtures/audio/mono.json`): `beats.detect_beats` y `sample_rms_envelope` REALES grabando lo que devuelve el ffmpeg del PATH. 22 casos sobre los medios de prueba (una base de batería nueva a 120 BPM en AAC, tonos, voz en MP3, vídeos con audio, un MP4 que empieza en 0,3 s, Opus, PCM en AVI; seeks pasado el final, negativos y de menos de 0,05 s; sin audio y sin archivo) y 80 de tus audios (40 archivos con sus beats y 40 ventanas de envolvente). El mismo PCM (MD5) en 90 de 96 decodificaciones y el resto a menos de 0,001 dB por décima de segundo (MP3 y AAC del FFmpeg de vcpkg frente al del PATH, como en la mezcla).
- **Divergencia que se queda:** numpy calcula la curva de ataque en float32 y el C++ en double; con el mismo audio, en tu voz de 2 min, 2 de 253 beats caen un fotograma de análisis (23,2 ms) al lado. El test lo admite en hasta el 1 % de los beats.
- **MSVC:** 177/177.

**Segunda tanda del mismo día: la pista de texto con vídeo encima (F3)**

- Lo último que faltaba del texto básico del render. El export mete esos textos en la cadena de vídeo, en su sitio de la pila: cada texto de capa propia por su clave y los demás de la pista en una capa, con la clave de su clip más temprano. La clave es (capa de la pila, inicio ≥ 0, índice en la lista).
- **`compose_with_buried_texts`** (`gc_render`, text.hpp) compone con `compose_into` y un gancho antes de cada capa (`BetweenFn` en compose.hpp) que dibuja los textos que van antes; `draw_timeline_texts` ya no los da por Unsupported, los deja para la cadena. `gc_export` y `genclip frame` lo usan, con los tiempos de compuesto y texto separados como antes.
- **Paridad:** 5 escenas nuevas (`RENDER_TEXT_STACKS`, 8 fotogramas) frente a `compose.render_frame`: 25–28 dB en la zona del texto, 44–60 perdonando 1 px. El mutante «encima de todo» falla 12 aserciones. Las escenas van en el esquema actual: con `version: 2` la migración subía las pistas de texto arriba del todo y no había nada enterrado.
- **Divergencia:** la capa de la pista va con `ass=…:alpha=1`, igual que la de capa propia: lo semitransparente sale c·α³ en el export (pendiente de Edu; el C++ pinta c·α). Ninguna de tus pistas con textos está enterrada (la única es de un proyecto de prueba y está vacía).
- **MSVC:** 177/177.

**Tercera tanda: el contorno del sujeto con el croma (F4)**

- `outline_params` y `outline_source_chain` (`gc_core/bg.hpp`): la cadena de `outline_ffmpeg_steps` (alfa difuminado y umbralizado en Φ(−1,5), del color del contorno, con su halo, y el sujeto encima) como continuación de la del clip, detrás del croma y antes del encuadre, como `_bg_source_chain`. El grosor va en px del alto del material (`detect.dims`; sin saberlo, 0 y no hay contorno), que el decode lee del primer fotograma. `compose` ya no lo da por Unsupported.
- **Paridad:** las cadenas frente a las funciones reales (`media/outline.json`: 11 contornos, con crudos raros, NaN e infinitos, × 6 altos, también los que rozan el mínimo de 0,5 px) y 5 fotogramas nuevos del render (imagen, rojo difuminado y semitransparente, con limpieza del borde y en vídeo): 50,5–51 dB de luma y 54–59 de crominancia. Como el contorno es fino, un render sin él aún pasaba de 35 dB en tres escenas: esas escenas llevan un suelo de 45 dB y el mutante falla en las cinco.
- Las escenas nuevas van al final de `RENDER_SCENES`: las PNG se nombran por su posición y en medio renumeraban las de detrás.
- **MSVC:** 178/178.

**Cuarta tanda: segmentos por referencia y seguimiento de caras (`segments.py`)**

- `segments.hpp`: «Crear clip» del Clip Editor (`create_segments`, materiales que apuntan al archivo del original con in/out_point) y el seguimiento de caras cacheado del material (`face_track_material`, `seed_face_keyframes`, `cached_face_track`), sobre `ProjectsStore`. La sonda de duración (gc_media) y el detector de caras (YuNet, F6) se inyectan; `py_floordiv` es el `//` de reales de CPython.
- **Paridad:** 38 pasos nuevos en la fixture del almacén de proyectos, con `segments.py` real y la sonda y `detect.face_track` sustituidos por lo que pide cada paso: rangos al revés, negativos, recortados al final del archivo, como texto o bool, demasiado cortos, sin lista, que no son objetos, con fin infinito (con y sin etiqueta: m:ss de infinito falla), desde un segmento (hereda el padre), sin archivo, y el seguimiento del propio rango, de la caché (±0,05 s), forzado, de otro rango, vacío, largo (ventanas de 2,5 s) y con el detector fallando. 14/14 mutaciones; la del índice desde 99999 pedía un proyecto con índices bajos.
- **MSVC:** 178/178; **MinGW:** 143/143.

### 2026-10-23 — F1, revisión, y F7: el historial de YouTube

- **Revisión de `segments`:** GCC daba un `-Wnull-dereference` en Release en `seed_face_keyframes` (`out.front()` sin ver que `out` nunca queda vacío): con la guarda, `verificar.sh` vuelve a 0 warnings. Y un tiempo infinito del detector era UB al pasar a entero; ahora es un error, como el `int(nan)` de Python (`seed_face_keyframes` devuelve `Result`). Dos pasos nuevos en la fixture, con inf y NaN. `export_fixtures.py --solo proyectos`.
- **`yt_history.hpp`** (espejo de `yt_history.py`): `yt_history_record` y `yt_history_remove` sobre `SettingsFile`. 48 pasos nuevos en la fixture de ajustes (`--solo ajustes`), con los `str()` e `int()` de Python sobre entradas raras; 9/9 mutaciones.
- **Trampa:** con un `video` de texto, `getattr("x", "title")` de Python es el método `str.title` y se guardaría su `repr` («<built-in method title…>», con una dirección de memoria). No es determinista: el caso va con un número, y C++ trata un texto como sin vídeo.
- **`heatmap.hpp`** (espejo de `heatmap._pick_segments`): los tramos de alto interés de «Analizar» a partir del heatmap de yt-dlp. 38 casos contra la función real (`--solo heatmap`), con los empates estables de `sorted` y `sort(reverse=True)` y el `[:max_clips]` de Python; 11/11 mutaciones. `analyze` sigue en Python hasta que llegue el sidecar de yt-dlp.
- **Enlaces de YouTube** (`youtube_url.hpp`): `is_youtube_url` y `youtube_id_from_url`, que hacen falta para `media_upgrade` y las descargas. Debajo, `py_url.hpp` con lo que usan de `urllib.parse`: `urlparse` (C0 y tabuladores fuera, esquema, netloc, params solo en los esquemas de `uses_params`), la validación de los corchetes (IPv6 como `ipaddress`, con zona y un IPv4 al final; IPvFuture; un IPv4 entre corchetes es un error) y el `_checknetloc` de NFKC (19 caracteres, tabla generada con el `unicodedata` 16.0 del Python de video-yt y comprobada por la fixture), `.hostname`, `parse_qsl` y `unquote` (cada trozo ASCII se descodifica por su lado, con el «replace» de CPython: un U+FFFD por trozo inválido más largo). 368 casos (`--solo url`); 22/22 mutaciones.
- **MinGW:** 145/145, 0 warnings en Debug y Release y 0 avisos de clang-tidy en lo nuevo; **MSVC:** 180/180.

- **`media_upgrade.hpp`** (espejo de `media_upgrade.py`): buscar las otras versiones del mismo vídeo de YouTube y cambiarlas en la timeline por la de más calidad (`replace_media` por un `TimelineOpApplier` inyectado: en el estudio, la `EditSession`; en el test, `apply_command` y `save_timeline`), con los segmentos por referencia al archivo nuevo. 26 pasos en la fixture del almacén; 16/16 mutaciones (dos más eran equivalentes: el id de un segmento no se usa en lo portado y el redondeo del delta lo absorbe el de `replace_media`).
- **También** `youtube_sources` y `best_full_version` de `media_upgrade.py` (57 pasos en total en `STEPS_UPG`; 21/21 mutaciones) y `asset_import.allowed_download_url` (`asset_import.hpp`, sobre `py_url`; 3/3 mutaciones).
- **Trampa:** `owner not in olds` de Python compara con la igualdad de Pydantic (mismos campos), no por identidad: el C++ compara el `to_json` de cada material.
- **MinGW:** 145/145 y **MSVC:** 180/180, 0 warnings y 0 avisos de clang-tidy.

**Quinta tanda: los efectos de vídeo animados (F4), como efecto suelto**

- `gc_render/vfx.hpp`: los 41 efectos de motor «shader» del runtime de video-yt (`frontend/src/fx`) traducidos a SkSL sin tocar su GLSL (`tools/gen/gen_vfx_shaders.mjs` lo extrae del registro real a `vfx_shaders.inc`). El preludio de `gl.js` se traduce de forma mecánica: si cambia, el render falla en vez de dibujar otra cosa. `render_vfx` hace lo de `renderVfx` (efectos activos, margen, una pasada por efecto) con lienzos raster de Skia.
- **Paridad con el Chromium real:** la fixture llama a `window.__fx.renderVfx` en la página de `fx_bake.FxBaker` (Playwright, la RTX 4060 por ANGLE/D3D11) sobre una imagen con alfa y guarda el PNG: 125 fotogramas en 7 s. Mediana de 64 dB; suelo de 35 dB (el del plan para el render). Lo peor son los efectos con ruido (`sin` y hashes, aproximados en la GPU) y bordes: sketch 37,8, pixel_pulse 39,8, tv_static y night_vision 42–45.
- **Trampas:** los `select` van al shader como el ÍNDICE de la opción (el catálogo trae objetos {id, label}); el SkSL de los runtime effects es ES 2 (sin `transpose`); la GPU filtra en bilineal con pesos de 8 bits y Skia con pesos exactos: imitándolo en el SkSL mejora 26 de 105 casos. `pixel_drift` elige la muestra con un umbral duro sobre la luma sin premultiplicar: un nivel de diferencia en el borde semitransparente cambia el píxel entero (0,8 % del cuadro); se mide por píxeles distintos.
- **MSVC:** 182/182; **MinGW:** 145/145 (gc_render solo se compila con vcpkg).

**Revisión de la tanda**

- `is_youtube_url` y `youtube_id_from_url` no volvían a pasar a minúsculas el host: `.hostname` deja la zona «%…» de un IPv6 como está y Python hace otro `.lower()` (`https://[::1%.YOUTUBE.COM]/x` es de YouTube). 4 URLs nuevas en la fixture; sin el arreglo, el test falla.
- `replace_media_with`: `owner not in olds` de Python mira primero la identidad; con un NaN en el material, la igualdad de los JSON no bastaba y el mismo dueño (pedido por su segmento y por él) salía dos veces. Caso nuevo con `start` NaN.
- **Aviso para F7** (copiado tal cual de Python): `allowed_download_url("http://[::1%.pexels.com]/x")` da true porque el hostname lleva la zona. Antes de descargar de verdad, hay que rechazar hosts con «[» o «%» en los dos lados.
- Cifras de los docs corregidas (48 pasos del historial, 57 de `STEPS_UPG`, 368 URLs).

**Sexta tanda: las calidades de YouTube (F7)**

- `youtube_video.hpp`: la parte sin red de `youtube_video.py`. La regex `(\d{3,4})p` de `format_note` va a mano con los dígitos de cualquier sistema (el `\d` de Python es Unicode e `int()` los lee: «١٠٨٠p» es 1080), y el mejor formato de cada calidad es el `max` de Python con la tupla de `_rank` (el primero de los iguales). 78 casos contra las funciones reales; 17/17 mutaciones (una más era equivalente: el `int()` de un infinito ya lo para el límite de 64 bits).
- **Divergencia que se queda:** con NaN en `fps`, `tbr` o `filesize`, Python compara las tuplas de `_rank` con la identidad de cada float; aquí un NaN nunca gana. yt-dlp no los da.
- **MinGW:** 146/146 y **MSVC:** 183/183, 0 warnings.

**Séptima tanda: lo que decide fx_bake antes de hornear (F4)**

- `gc_core/fx_bake.hpp`: `active_vfx` (Python normaliza distinto que el JS: solo quita `enabled` si es `False`, y `from`/`to` pasan por `float()`), `has_vfx`, `pad_frac`, `pad_allowed` y `baked_clip` con `_remap_reframe`. Es el primer paso para meter los efectos en el compuesto (plan en las notas de F4 del TODO). 14 clips; 15/15 mutaciones.
- **MinGW:** 147/147 y **MSVC:** 184/184, 0 warnings.

**Octava tanda: los efectos de vídeo en el compuesto (F4), en el test de render**

- `plan_vfx_bake` y compañía (`gc_render/vfx.hpp`): como `fx_bake`, el clip con efectos se cambia en la timeline por su `baked_clip`, su material mide lo del original recortado a par más el margen, y el fotograma que enseña en cada tick (el overlay con `setpts` truncando, y el trim que adelanta el final) es `render_vfx` del fotograma m del tramo original en t = m/F. Sin archivo intermedio.
- **Paridad:** 6 escenas nuevas en `RENDER_SCENES` (12 fotogramas) contra `compose.render_frame` con el horneado real en Chromium (la caché de `fx_bake` va a una carpeta temporal). El horneado del export pierde detalle (VP9 4:2:0) y **oscurece el clip 3–4 niveles** también sin efecto activo (BT.601 de rango TV y la ida y vuelta por el navegador): divergencia para Edu. El test mide ese sesgo en la zona del clip (tiene que estar entre 0 y 6) y compara las medias de 8×8 sin él: el vídeo con CRT da 55–57 dB, la sombra 47 y las texturas de color saturado 36,6–38 (el error depende del color; 36,6 es la escena sin efecto activo); suelo de 33, y ≥ 30 dB píxel a píxel.
- **Trampas de la fixture:** una sombra sobre un PNG con borde transparente no necesita el margen (cae dentro); con un material opaco, sí. Y un degradado suave no nota el zoom ni el desplazamiento de un terremoto: con ondas, la duración mal puesta (el progreso) falla.
- 5/5 mutaciones a mano con MSVC.

- **En el export:** `gc_export/vfx_bake.hpp` (`ensure_fx_catalog` con el catálogo de `<raíz>/app/fx_assets`, como `fx_catalog.CATALOG_PATH`, y `plan_timeline_vfx`, los clips de `bake_timeline`: activos, en una pista de vídeo visible y con material). `genclip render` lee el tramo original con `ClipFrames` en el fotograma m y `genclip frame` con `export_frame_at`. Probados con tres escenas de la fixture en un proyecto de prueba: lo mismo que el test (41,9 dB el CRT en `frame`, 42,2 en el MP4 de `render`).
- **Trampa:** `fx_catalog()` nunca es nulo (hasta que se fija es un catálogo vacío): para saber si hay que cargarlo hay que mirar si tiene efectos.

**Novena tanda: revisión y efectos con velocidad y al revés (F4)**

- **Revisión:** `plan_vfx_bake` rechaza antes `paper_cutout` (de Canvas 2D; antes abortaba `genclip render` a mitad y `--omitir` no lo saltaba), la velocidad va con la tolerancia de 1e-3 de `video_speed_filters`, `active_vfx` falla como el `int(v)` de Python y `from_chars` en vez de `strtod`.
- **Velocidad y revés:** en vez de imitar qué fotogramas elige el `fps=F` tras la velocidad, `gc_media` ejecuta el mismo grafo que `prepare_input`: la cadena del clip sin su `fps` final y después `reverse`, `setpts=PTS/sp` y `fps=F` (`bake_input_frame_at`, `ClipFrames::open_bake_input`); el fotograma n es el n-ésimo que sale. 6 escenas nuevas (velocidad 2 y 0,5, al revés, inicio a 0,215 s, hasta el final del archivo y 25 fps): 55–57 dB; 3/3 mutaciones a mano.
- **MinGW:** 147/147 y **MSVC:** 185/185, 0 warnings.

**Décima tanda: «Papel recortado» (paper_cutout, F4)**

- `gc_render/paper.cpp`: el único efecto de motor «canvas», paso a paso como `paper.js`. Chrome dibuja el Canvas 2D y el filtro SVG con Skia, así que las mismas primitivas dan casi lo mismo: `Dilate` (Skia redondea el radio, como en el navegador; la primera versión lo truncaba y la revisión lo encontró), `MakeFractalNoise` con la semilla de `TORN_SEEDS`, `DisplacementMap` R/G, el blanco con `SrcIn` y `Merge` con la imagen; las arrugas aclaradas con el redondeo al par de `Uint8ClampedArray` y su modo de fusión; la sombra en una capa sin la transformación (la de Canvas va en px del lienzo) con sigma = la mitad de `shadowBlur` (medido: 55,7 dB; con el difuminado entero, 46,4); el `rand` de Murmur3 de `fx/core/rand.js` con la aritmética de 32 bits de JS.
- **Paridad:** 13 fotogramas de `renderVfx` en Chromium con las texturas cargadas (`preloadFxAssets`): 37,7–61 dB. 11/11 mutaciones a mano.
- **Revisión:** radio de dilatación redondeado (el borde ancho pasa de 37,7 a 51,7 dB), fotogramas del horneado con el redondeo al par de Python, 9 casos más (22 en total, 42–61 dB, suelos de 50 y 40) y la caída a `resources/fx` si la raíz no trae `frontend/public`.
- Las texturas (16 WebP, 392 KB) van en `resources/fx/paper-animator/assets/texture`; `genclip` las lee de `frontend/public` de video-yt, como `fx_bake.PUBLIC_DIR`, y si no está, de `resources/fx` (de momento con la ruta del árbol de fuentes: en F9, relativa al ejecutable).
- **MinGW:** 147/147 y **MSVC:** 185/185, 0 warnings.

**Siguiente:**
- Con F4 casi entero, lo que queda sin Edu: la medida real del texto (FreeType/HarfBuzz en `set_text_width`), las transiciones y looks, y la caché de fotogramas de F3.
- `heatmap.analyze` y la descarga del vídeo completo, con yt-dlp como sidecar (F7).
- El «Detectar beats» del menú del clip y el Clip Editor, en el studio (con Qt, en Linux).
- En el render queda el matte automático (F6); los efectos de vídeo ya están todos (F4).

### 2026-10-24 — F4: la medida del texto exacta, como Pillow

- **Revisión de la tanda anterior** (`revisor-migracion` sobre `2e5370e..be0ec2a`): aprobada; MinGW 147/147 y MSVC 185/185, 0 warnings. Hallazgos menores, para después: la sombra de `paper_cutout` con desenfoque y desplazamiento 0 (Canvas no la dibuja y el `DropShadow` de Skia oscurece el borde semitransparente: 34,6 dB), sin test de `ClipFrames::open_bake_input` (la ruta de `genclip render` con efectos; a mano da los mismos píxeles) y `tidy.py`, que solo analiza la build de MinGW (con la de MSVC salen 9 avisos en `gc_render`, `gc_media` y `gc_export`).
- **La medida (`gc_render/pillow_font.cpp`):** el Pillow de video-yt (12.3) no tiene raqm y lleva FreeType 2.14.3, la misma versión que trae vcpkg. En vez de imitar el hinting, se llama a FreeType con las mismas llamadas que `_imagingft.c`: la petición NOMINAL de `size·64` (en `float` de C y truncada a 32 bits), un glifo por carácter (`FT_Get_Char_Index`, sin formas ni ligaduras), `FT_LOAD_DEFAULT` y el kerning de la tabla 'kern'. Sale bit a bit. La usan `TextFonts::text_width` (con caché: el render mide en cada fotograma) y la máscara de tipo texto, cuyo ancho y anclas pasan de ±1 px a exactos. Antes (HarfBuzz a tamaño fraccionario), el 24 % de las medidas se iba más de 1 px, hasta 64, y 2 de tus clips partían las líneas distinto (lo midió el revisor).
- **Trampas:** (1) FreeType mide con el ppem ENTERO (`head.flags` bit 3, que tienen todas las fuentes del export: 46,375 px se mide a 46), pero el kerning usa la escala fraccionaria. (2) Pillow suma el kerning como `PIXEL(delta.x)` en unidades de 26.6: un px de kerning mueve 1/64 de px («AVAVAVAV WAWAWA» en Arial a 46 px da 482,515625). (3) `ImageFont.truetype` no se rinde: si FreeType no abre el archivo o no admite el tamaño, busca uno con el mismo nombre en `WINDIR\fonts` (con `os.walk`); con una carpeta de fuentes vacía o rota mide con la Arial de Windows, y solo estima si tampoco la encuentra. (4) Por encima de 2^22 px, donde el `float` de C redondea el tamaño, FreeType ya falla; por eso la mutación «petición en double» es equivalente. (5) En FreeType 2.14 los programas de cada glifo escriben en una copia del CVT (`Modify_CVT_Check`): la medida no depende del orden y se puede guardar en caché.
- **Paridad** (`export_fixtures.py --solo anchos [--real]`, `test_text_width_parity.cpp`): 10 992 medidas de `font_metrics.text_width` en las 26 fuentes del export (rejilla de tamaños con el redondeo a 1/4 al par, kerning, ligaduras, tildes, controles, CJK, emoji, árabe…), los nombres de `FONT_NAMES`, tamaños raros (de 0 a 1e308: el ppem de 16 bits, los 32 bits de Pillow, lo que no cabe en `float`), más de un millón de caracteres y carpetas de fuentes de mentira; y tus 642 textos (reparto en líneas y cajas de Fondo con la medida real, 1684 medidas). También desde cuatro hilos a la vez. Las fuentes del sistema van con su CRC-32: en otro equipo con otras fuentes, el test lo avisa y se las salta. 11 mutaciones a mano con MSVC: las 9 que cambian algo, detectadas (sin kerning, kerning en px enteros, sin hinting, tamaño redondeado hacia arriba o sin redondear a 1/4, sin la búsqueda en `WINDIR\fonts`, sin el límite de un millón de caracteres, el ancla del centro y el ascendente de la máscara), y 2 equivalentes: la petición en `double` (por encima de 2^22 px, FreeType ya falla) y el kerning con el glifo 0 (ninguna tabla 'kern' de estas fuentes tiene pares con `.notdef`).
- **Trampa de las mutaciones:** en Debug, el STL de MSVC abre un diálogo de aserción (por ejemplo, con `back()` de un vector vacío) y el test se queda esperando; una mutación no debe meter comportamiento indefinido.
- **Divergencia que se queda:** con un em no finito (o em·4 = inf), Python lanza en el `round()`, fuera del `try`; el C++ estima.
- **MinGW:** 147/147 y **MSVC:** 189/189, 0 warnings.

**Revisión: los hallazgos del revisor**

- **Sombra de `paper_cutout`:** Canvas 2D no dibuja la sombra si su color es transparente o si no hay difuminado ni desplazamiento (`ShouldDrawShadows`), y el `DropShadow` de Skia oscurecía igual el borde semitransparente (28,6–34,6 dB). Ahora no se dibuja, y como sus setters un valor no finito o un difuminado negativo deja el 0. 4 casos nuevos en `vfx_cases.PAPER` (sin difuminado ni desplazamiento, con opacidad 100, sombra dura solo desplazada y opacidad 0): 58,5–59,1 dB; sin el arreglo, 3 fallan.
- **`ClipFrames::open_bake_input`** (la lectura en orden de `genclip render` con efectos) tiene test: los mismos fotogramas (MD5) que `bake_input_frame_at` en 11 configuraciones, también al revés y con el salto previo a un fotograma clave. Al revés no pasa por `export_frame_at`: `bake_input_chain` mete el `reverse` en el grafo y deja `reverse = false`. Leer la cadena del overlay (mutación) da 175 fallos.
- **MinGW:** 147/147 y **MSVC:** 190/190, 0 warnings.
- **clang-tidy en todo el código:** `tidy.py` analiza también lo que MinGW no compila (`gc_media`, `gc_render`, `gc_export` y sus tests: 35 archivos) con la base de compilación de la build de MSVC y el entorno de `vcvars64.bat`. Salieron 129 avisos, unos 50 de ellos en `gc_core` y sus tests (código del studio que no se había vuelto a pasar). Arreglados (reservas, `const`, `std::format`, `std::ranges`, `std::cmp_*`, `starts_with`…) salvo donde lo que pide clang-tidy cambia el comportamiento: `json == ""` no es `.empty()` (un `null` o un `[]` también están vacíos: se escribe la comparación de verdad), los bucles con `double` copian el acumulado de `rulerTicks` del JS, el `min` anidado de `clip_mask.py` no da lo mismo que una lista con un NaN, y con `std::array` el iterador de MSVC no es un puntero (un bucle). Desactivados con su porqué en `.clang-tidy`: `bugprone-signed-bitwise` (avisa hasta de `std::ios::binary | std::ios::trunc`) y `readability-redundant-member-init` (los `= {}` evitan el `-Wmissing-field-initializers` de GCC con inicializadores designados); y `(void)` vale para descartar un resultado. `rotl` de `paper.cpp` pasa a `std::rotl` (con r = 0, el desplazamiento de 32 bits era indefinido). 0 avisos en 160 archivos.

**Siguiente:**
- La caché de fotogramas de F3 (con el preview de F5) y el resto de F4: el alfa de «Eliminar fondo» desde la caché, el marco de TikTok/Shorts del preview y la salida de F4 (todo lo de `docs/` de video-yt sin libass ni Chromium).

### 2026-10-25 — F4: el matte de «Eliminar fondo» desde la caché

- **Revisión de lo anterior** (`revisor-migracion` sobre `be0ec2a..a77f17b`): aprobada, sin nada bloqueante. Hallazgos menores, apuntados en las notas de F4: image2 puede saltarse un hueco de un fotograma al buscar el último (sondas de +1, +2, +4…), un crudo que no sea gris de 8 bits falla (Python lo convierte), un `mask_fps` negativo llega tal cual al `-framerate` (igual en Python) y los casos de error del test no miran el tipo de la excepción.
- **Qué pide el export a la caché** (`gc_core/bg_matte.hpp`): `clip_matte_spec` es `build_clip_bg_mask` + `ensure_derived` de `bg/service.py` con el nivel 2 vacío (lo que pasa la primera vez): el tramo de fotogramas (`matte_frame_index` con el redondeo del filtro `fps`), el `-start_number`, el bucle de un GIF y, de cada fotograma derivado, de qué crudo sale (fuera de lo procesado, el del extremo; si falta el crudo, nada, y la secuencia de image2 se corta ahí). El nivel 2 no se escribe: el derivado de solo LUT (`matte_lut`, umbral, suavidad e invertir con la aritmética de doble del JS) se calcula al vuelo. Paridad: 31 clips sobre 16 cachés sintéticas (meta roto, rangos raros, huecos, fuera de la caché, GIF, velocidad, al revés…) y 102 LUT frente a las funciones reales (`export_fixtures.py --solo matte`).
- **La segunda entrada del grafo** (`gc_media`): `MatteFeed` lee la secuencia como image2 (`start_number_range` de 5, marcas de 1/mask_fps, `-stream_loop -1` en un GIF) con el decodificador PNG de FFmpeg y la LUT; el `Decoder` tiene un segundo `buffer` (`[matte]` en la cadena) y le da un fotograma cuando el grafo lo pide (`av_buffersrc_get_nb_failed_requests`) o cuando el material ya acabó, como el CLI. `core::matte_source_chain` es la rama del matte de `_bg_source_chain`: el matte subido al tamaño del material (detect.dims; si no, el del meta.json) con bicúbico, `alphamerge`, y con croma, su alfa multiplicado por el del matte (`blend=all_mode=multiply`); el contorno, detrás. `export_frame_at`, `bake_input_frame_at` y `ClipFrames` reciben el `MatteSpec*`; `genclip render` y `genclip frame` lo calculan con la caché en `data/bgcache` (`exporter::clip_matte`; si falla, un aviso y sin matte, como `build_timeline_bg_masks`), también para el tramo que se hornea con efectos de vídeo.
- **Trampa:** en el export, el `-t` de la salida corta la secuencia en bucle de un GIF; aquí no hay salida y no acabaría, así que se corta pasado el final del clip (+1 fotograma), que ya no se ve.
- **Render frente a `compose.render_frame`:** 10 escenas (17 fotogramas): imagen, umbral/suavidad/invertir, un matte más pequeño que el material, vídeo con el matte a 15 fps, más largo que la caché, velocidad 1,5 y al revés, timeline de 25 fps, un GIF que da la vuelta, croma y matte, y sin la caché: 51 dB de luma, como el croma. Con tus 3 clips con matte (todos de solo LUT), `genclip frame` frente al export real dibuja el clip recortado igual salvo 1 px de borde (el redondeo a pares de la geometría, pendiente de Edu). `ClipFrames` con el matte da los mismos fotogramas que `export_frame_at` (5 casos, también el GIF en bucle).
- **Mutaciones a mano con MSVC (6/6 detectadas):** sin matte (30 fallos), sin la LUT, sin dar la vuelta en el GIF, el croma sin multiplicar por el matte, el `start_number` desplazado y el matte escalado con el vecino más cercano en vez de bicúbico. Para que las dos últimas se notaran: suelo propio de 48 dB en estas escenas (medido 50,96–51,10; con el de 35 pasaban), el matte pequeño es ahora un disco de 80×45 (×4: con franjas suaves ×2 el escalado no se notaba) y la escena de croma y matte lleva el matte invertido sobre un degradado (antes el sujeto del croma era de la misma textura que el fondo y el matte no cambiaba nada).
- **Trampa de las mutaciones:** el script deshace el cambio en el código, pero el `gc_tests.exe` que queda es el de la última mutación. Una pasada «sin mutar» con ese binario dio 47,7 dB y pareció una divergencia del escalado; con la build limpia, el alfa de `export_frame_at` es idéntico (byte a byte) al que saca el comando real del export tras su `alphamerge`. Después de mutar, recompilar.
- **Falta:** el resto de `derive_matte` (pluma, expansión, opacidad y el pincel); hoy un matte así sale con solo la LUT y un aviso (`exporter::clip_matte`). Ninguno de tus clips lo usa.

**Revisión: los hallazgos del revisor** (`revisor-migracion` sobre `a77f17b..b357f4b`: aprobada; repitió MinGW 150/150 y MSVC 194/194)

- **El contorno con matte usa el alto de `detect.dims`:** Python le pasa `detect.dims(path)[1]` (0 si OpenCV no abre el material: sin contorno), aunque el matte se haya subido al tamaño del meta.json. `matte_source_chain` recibía ese alto del meta; ahora recibe aparte el alto sondeado.
- **La nota «en marcha» del TODO** daba como pendientes pasos ya hechos; queda solo lo que sigue vigente (huecos que image2 puede saltarse, crudos que no son gris, sin fixture del texto de la cadena ni escenas con contorno o efectos de vídeo con matte, sin test de matte con salto previo).
- `heatmap.analyze` y la descarga del vídeo completo, con yt-dlp como sidecar (F7).
- El «Detectar beats» del menú del clip y el Clip Editor, en el studio (con Qt, en Linux).

**El resto de `derive_matte`** (pluma, expansión, opacidad y el pincel)

- `gc_core/matte_derive.cpp`: el orden de clip_bg.py (LUT → expansión → pluma → conservar/borrar → opacidad → `clip(·255 + 0,5)`), en float32 como numpy (NEP 50: el escalar de Python no sube a doble). `GaussianBlur((0,0), σ)` como OpenCV: núcleo de `getGaussianKernel` en doble y luego float, tamaño `cvRound(σ·8 + 1) | 1` (al par: `lrint`), filas y columnas con BORDER_REFLECT_101 (también con un núcleo mayor que la imagen); con σ ≥ 32, `resize` INTER_AREA (`computeResizeAreaTab`) a 1/step, el desenfoque y vuelta con INTER_LINEAR (centros alineados, coeficientes en float). `MatteFeed` lo aplica a cada crudo si el clip no es solo LUT (`MatteSpec::settings`); `genclip render` ya no avisa.
- **Paridad** (`export_fixtures.py --solo matte`, 26 casos en `bg_cases.DERIVE_CASES` con el crudo y el derivado del `derive_matte` real en base64): pluma (también σ ≤ 0,3, la máxima y la reducción ×2 y ×3), expansión y contracción, opacidad 0 y 0,4, umbral/invertir con pluma SAM (los trazos no se pintan), reducciones no enteras sobre franjas y el pincel: bit a bit, salvo la contracción (44 de 5184 píxeles a 1 nivel).
- **El pincel, con el rasterizador de OpenCV:** `cv2.polylines` y `cv2.circle` con LINE_AA pintan más ancho que el grosor pedido y con su propio antialias; unas cápsulas ajustadas daban 31–42 dB (y ~20 en el trazo de 1 px), así que se portó lo que usan de `modules/imgproc/src/drawing.cpp` de OpenCV 5.0.0 (la versión del export) a `gc_core/cv_draw.cpp`: `PolyLine` → `ThickLine` (el rectángulo del tramo con `FillConvexPoly` y los extremos con `EllipseEx`, recortando con `clipLine` lo que se sale del lienzo), `LineAA` (su tabla de pendiente y su filtro, la mezcla aplicada dos veces), `FillConvexPoly` con los deltas de LINE_AA y `ellipse2Poly` con su `SinTable`, todo en punto fijo 16.16 y `cvRound` al par. Sale bit a bit en los 26 casos (con el trazo de 1 px, fuera del lienzo, trazos con «m», grosor par e impar y extremos de radio ≥ 15).
- **Mutaciones a mano con MinGW (10/10 detectadas):** núcleo de 6σ, borde repetido en vez de reflejado, reducir con lineal en vez de área, expansión sin el sesgo, `LineAA` que mezcla una vez, círculo con menos lados, relleno sin el delta de LINE_AA, grosor par como impar, puntos truncados y borrar como conservar. Dos sobrevivían hasta añadir casos: con un factor entero y un disco suave, área y lineal dan lo mismo (ahora, reducción no entera sobre franjas), y los lados del círculo solo cambian con radios ≥ 15 px (ahora, un pincel grueso).
- **Trampa de clang-tidy:** `<cfloat>` no se puede analizar con la configuración de `tidy.py` para MinGW (el `float.h` de GCC hace `#include_next <float.h>` y no lo encuentra): el archivo queda «sin analizar» y los avisos que salen antes son falsos (uno de `misc-use-internal-linkage` sobre una función declarada en su cabecera). Se usa `std::numeric_limits<double>::epsilon()` en vez de `DBL_EPSILON`.
- **Render:** 2 escenas más (pluma, expansión y opacidad en una imagen; pincel para conservar y borrar con un trazo «m» en un vídeo): 3 fotogramas frente a `compose.render_frame`.

**Revisión: los hallazgos del revisor** (`revisor-migracion` sobre `6d92aaf..1d9fc37`: aprobada; repitió MinGW 151/151 y los tests `[bg]`)

- **Divergencia aceptada, con su cifra:** el GaussianBlur float32 de cv2 5.0.0 (con IPP) suma en otro orden; en una meseta de 1,0 no da exactamente 1,0 y, con un sesgo o una opacidad que dejan el valor en el ,5, se voltea 1 nivel (la contracción de la fixture: 0,85 % de los píxeles; en un disco de 854×480, 0,16–0,19 %). El test admite 1 nivel en el 1 %.
- **Un punto del pincel enorme** (|x·ancho| ≥ 2³¹) ya no es comportamiento indefinido: se acota (Python falla al pasarlo a int32).
- **Docs:** el comentario de `derive_matte` decía que el pincel solo «se parecía» al de cv2; la nota del TODO decía que un matte que no es solo LUT salía con un aviso; la casilla del contorno y el croma seguía abierta con sus tres partes hechas.
- **Pendiente (siguiente tanda):** probar `derive_matte` con los mattes reales de la caché de video-yt (todas las fixtures son discos y franjas) y medir el desenfoque, que es O(n·w·h) sin SIMD: con un matte de 1080 de alto y pluma media puede pesar en el export.

**`derive_matte` con tus mattes reales y más rápido**

- **Paridad con la caché real** (`export_fixtures.py --solo matte --real`, `tests/fixtures/local/bg/`, sin subir: el JSON y los fotogramas en gris sin cabecera): el fotograma del medio de un conjunto por cada proveedor y tamaño de tu `data/bgcache/matte` (12: RVM mobilenet y resnet 1280×720, SAM 2.1 base+ y large 910×512, U²-Net y U²-Netp de 288×512 a 910×512) con 7 ajustes (pluma 0,02/0,08/0,15, expansión, contracción con opacidad, umbral y suavidad con pluma, y pincel con pluma): los 84, como mucho 1 nivel en casi ningún píxel, con el `derive_matte` real.
- **El desenfoque, ~8 veces más rápido y con los mismos bits:** cada píxel suma sus coeficientes en el mismo orden, pero por cada coeficiente se recorre la fila entera (acolchada una vez con el borde reflejado), que el compilador vectoriza; la huella de los 110 derivados (sintéticos y reales) es la misma. En un RVM de 1280×720, de 260 a 33 ms por fotograma en el peor caso (contracción y opacidad); mediana, 7 ms.

### 2026-10-25 — F3: el SSIM de las fixtures de render

- **Revisión de lo anterior** (`revisor-migracion` sobre `6d92aaf..1d9fc37`, ver la tanda de `derive_matte`): aprobada; su siguiente tarea propuesta (los mattes reales y el desenfoque) ya está hecha, y el SSIM era su alternativa corta.
- **El SSIM** (`tests/parity/ssim.hpp`, común al test del render y al del export entero): el de Wang et al. (2004) sobre la luma BT.709, con la ventana gaussiana de 11×11 y σ = 1,5, C1 = (0,01·255)² y C2 = (0,03·255)², la media en las ventanas enteras dentro de la imagen. Comparado con una versión aparte de `ssim_index.m` (`filter2` en modo `valid`) en numpy sobre 27 escenas: igual hasta 5·10⁻⁷ (lo que redondea el PNG). En el render no cuentan las ventanas que tocan la banda del borde (la de la divergencia del redondeo a pares, pendiente de Edu), como el PSNR.
- **Suelos:** el del plan, ≥ 0,98, en todos los fotogramas del render (184; medido 0,990–0,99999: lo más bajo, un círculo sin trazo, la fusión diferencia con fundido y la capa de ajuste en un tramo) y en cada fotograma de los dos exports enteros (peores 0,9906 y 0,9945). Los efectos de vídeo horneados, ≥ 0,97 (0,976–0,988): el export los pasa por un VP9 4:2:0 intermedio que pierde detalle fino (el glitch RGB, el terremoto), la misma divergencia que ya se anota para su luma. Las escenas con grano no lo miden: el grano es aleatorio (0,89) y se comparan por la fuerza del ruido.
- **MSVC:** 196/196, 0 warnings.
- **Revisión** (`revisor-migracion` sobre `241d8c5..4981bbf`): aprobada; repitió MinGW 152/152 y los tests de render y export de MSVC. Menores, arreglados: una línea en blanco de más al final del test del matte y la casilla del SSIM, marcada pero aún con «parcial». Anotado: el suelo de 0,97 de los horneados queda a 0,006 de lo medido (0,976).

### 2026-10-29 — F3: el benchmark con tus proyectos de 4K, y NVENC

- **Medido** (build Release de MSVC, `genclip render … --tiempos` frente a `compose.render` llamado desde un script, con el nivel 2 de la caché de fondo en una carpeta temporal para no escribir en video-yt): «l filo del mañana» (2160×3840, 48,2 s) y «passagers» (41,4 s). El C++ gana al export de hoy (x264): 59,6 s frente a 80,8–89,6 y 82,8 frente a 92,5. Con el mismo codificador queda ~6 % por detrás: NVENC en los dos, 59,6 frente a 56,3 y 82,8 frente a 77,4; OpenH264 frente a x264, 83,2 frente a 80,8 y 87,8 frente a 92,5. Tabla y detalle en las notas de F3 del TODO.
- **NVENC funcionaba y nadie lo usaba:** la sonda de `gpu.py` codifica 64×64, por debajo del mínimo de NVENC en H.264 (145×49; la RTX 4060 lo rechaza con «Frame Dimension less than the minimum supported value»), y el C++ la copiaba. Ahora la sonda codifica 256×256 (y rellena cada plano con su alto: la de antes escribía 64 filas en los planos de croma de 32, fuera del búfer). `Mp4Writer::open` prueba los codificadores en orden y al final OpenH264, por si el elegido no abre con el tamaño de la salida (test nuevo: una salida de 64×64 sale con otro codificador, y pedir NVENC a mano en ese tamaño falla). Divergencia: el C++ exporta con NVENC donde video-yt usa x264; arreglar la sonda de video-yt queda para Edu.
- **OpenH264 en 4K:** el hilo que codifica era el cuello (93 s con 4 hilos y 4 slices; 91 con 8 y 85 con 12, con el PC algo cargado): las *slices* crecen ahora con la raíz del área (4 en 720×1280, 12 en 2160×3840). Los hilos no: OpenH264 no pasa de 4 (lo vio la revisión de abajo).
- **Lo que falta es componer:** un perfil por capa (temporal) muestra que el bicúbico de Skia (`drawImageRect` con `SkCubicResampler`, 16 lecturas por píxel en coma flotante) cuesta ~20 ns por píxel con un hilo: 170 ms por una imagen a pantalla completa en 4K, 423 ms con 8 hilos a la vez. Más hilos lo empeoran (la máquina se satura) y el bilineal de Skia solo quita un 18 %. Siguiente tanda: un remuestreo separable propio para los dibujos de escala y traslación.

**El bicúbico separable** (componer en 4K más deprisa)

- `gc_render/resample.cpp` (`detail::draw_scaled_bicubic`): los dibujos de escala y traslación sin cortinilla (casi todos los overlays) ya no pasan por el `drawImageRect` de Skia, que lee 16 texels por píxel en coma flotante, sino por un bicúbico separable: cada fila de la fuente se filtra una vez en horizontal y cada píxel combina 4 filas, con SSE2 en x86-64 (un píxel RGBA por registro) y escalar en lo demás. Escribe sobre los píxeles de la capa de arriba (`accessTopLayerPixels`, con `notifyContentWillChange` antes por si hay una instantánea), también dentro de `begin_layer` y con la matriz de color de los filtros.
- **Lo mismo que Skia**, leído de su código (el árbol de vcpkg): qué píxeles se pintan (`SkScan::FillRect`: el recorte llevado al cuadro en float y redondeado con floor(v + 0,5) en doble; fuera de 16.16 lo deja a Skia, que lo rellena como camino), la inversa en float de `SkMatrix::invert` con la traslación de la subimagen de `setConcat`, el centro de cada píxel por ella con FMA (`matrix_scale_translate`), kStrict como la subimagen `roundOut(recorte)` a la que `ix_and_ptr` acota las muestras, los pesos de `CubicResamplerMatrix(0, 0,6)` en t = fract(x + 0,5) con FMA, y el orden de etapas `clamp_01` → `premul` → alfa de la pintura (`scale_1_float`) → matriz de color (`unpremul`, `matrix_4x5`, `clamp_01`, `premul`) → `srcover_rgba_8888` (d·(1 − a) + c·255, al par con cvtps). Solo cambia el orden de las sumas.
- **Test** (`test_render_resample.cpp`, con un interruptor interno que obliga a dibujar con Skia): 31 casos frente a Skia (escalas de 0,3 a 60, volteos, bordes justo en ,5, recortes enteros, con fracción, desde el origen y de una columna, opacidad 0, 0,01 y 0,5, capa con fusión, medio fuera del lienzo, 5 matrices de color, una que se sale de 0–1, y un dibujo tras la capa de ajuste): iguales salvo 1 canal a 1 nivel en 5 casos de 27 648. Y lo que no debe usarlo (giro, sesgo, escala nula o no finita) no toca nada.
- **Tiempo** de un dibujo a pantalla completa en 4K (`gc_tests "[bench]"`, oculto): 29 ms frente a 155 de Skia; con el filtro vintage, 65–69 frente a 187. De punta a punta, con NVENC: passagers 74,4 s (componer, de 608 a 490 s entre 8 hilos) y filo 47,8 s (de 417 a 209 s), pero con carga en el PC; la tanda limpia se paró porque había un juego abierto (FC26 con 6 núcleos: el export de Python pasó de 81 a 263 s) y queda por repetir con el PC tranquilo.

**Revisión: los hallazgos del revisor** (`revisor-migracion` sobre `9280fef..eeb30aa`: aprobada, sin bloqueantes; repitió MSVC 199/199 y MinGW 152/152 a prioridad baja)

- **`tools/dev/benchmark.ps1` se paraba en la primera línea de stderr:** con `$ErrorActionPreference = "Stop"`, PowerShell 5.1 convierte el `2>&1` de un ejecutable en un error. Ahora `Continue`, comprueba que existen genclip, el Python de video-yt y el script, y con `-Force` mide aunque el PC esté ocupado (para probarlo; probado de punta a punta con «app launcher»). `bench_export.py` deja también los horneados de los efectos de vídeo (`fx_bake.CACHE_DIR`) en una carpeta temporal: ya no escribe nada en video-yt aunque el proyecto tenga efectos.
- **OpenH264 no usa más de 4 hilos** (`MAX_THREADS_NUM`; acota `iMultipleThreadIdc`): lo que crece con el área son las *slices*. `openh264_threads()` vuelve a 4 como mucho y `openh264_slices(ancho, alto)` da 4–16; comentarios y docs corregidos.
- **El test del bicúbico era laxo:** admitía un 0,2 % de canales distintos (~1700) y se miden 5; ahora como mucho 30. Dos casos más: bordes justo en ,5 (un redondeo al par en vez de floor(v + 0,5) cambia los píxeles pintados) y un dibujo tras `apply_color_matrix`, que deja una instantánea del lienzo.
- **Cifras:** 199 tests con MSVC y fecha del TODO al día.
- **MSVC:** 199/199, 0 warnings; MinGW 152/152.

**Las capas quietas, sin redibujar (y el benchmark cerrado)**

- `render::LayerCache` (`compose_into`; uno por hilo que compone en `genclip render`): las primeras capas que son imágenes fijas dibujadas igual (mismos píxeles, comparados con `memcmp`, y mismo recorte, matriz, opacidad, cortinilla y filtros de color; sin máscaras, fusión ni efectos) no se redibujan: se restaura el lienzo guardado tras ellas. Si solo siguen igual las de más abajo (una imagen que se mueve sobre un fondo quieto), se guardan esas; tras 3 guardados seguidos que no sirvieron (un GIF) descansa 16 fotogramas. Solo sin textos en la cadena de vídeo: `compose_with_buried_texts` ya no pasa `between` cuando no los hay. El guardado y la restauración, en `CanvasAccess` (sin capas abiertas; `notifyContentWillChange(kDiscard)` antes de sobrescribir).
- **Test** (`test_render_compose.cpp`): 12 fotogramas con y sin caché, iguales byte a byte, con un fondo quieto y una imagen que se mueve encima, cambios de opacidad, de píxeles y de filtro, y un fondo que cambia en cada fotograma (nunca acierta).
- **Con tus proyectos:** passagers, el MP4 entero idéntico al de sin caché (PSNR infinito en los 1242 fotogramas), y de 74,4 a 25,8 s (componer: de 490 a 92 s entre 8 hilos); filo, de 47,8 a 26,5 s.
- **Benchmark de F3, cerrado** (una tras otra, con el PC al ~20 % de carga): filo, C++ con NVENC 26,5 s y con OpenH264 85,7, frente a 83,2 del export de hoy (x264) y 56,3 de Python con NVENC forzado; passagers, 25,8 y 66,9 frente a 97,6 y 77,4. Por software, lo que limita al C++ es OpenH264 (~17 fotogramas por segundo en 4K, un hilo). Queda repetirlo con el PC tranquilo (`tools\dev\benchmark.ps1`) para confirmar las cifras.
- clang-tidy: un aviso del analizador en `chain_texts` (`*first` podía ser nulo para él; no pasa, el propio texto está en la pista) se resuelve con una vuelta explícita al clip del texto.
- **MSVC:** 200/200, 0 warnings.

**Revisión: los hallazgos del revisor** (`revisor-migracion` sobre `74e2950..339730a`: aprobada, sin bloqueantes; repitió MSVC 200/200 y MinGW 152/152)

- **El test de la caché no detectaba una clave incompleta** (quitar la comparación de la cortinilla seguía en verde). Ahora son escenarios que, tras guardar, cambian una sola cosa de la clave (opacidad, filtro, recorte, zoom, desplazamiento, cortinilla, número de blits, píxeles, tamaño del lienzo, también 60×80 frente a 80×60, que tienen los mismos bytes), lo que no se debe guardar (máscara, fusión, efectos, textos entre capas) y el descanso tras un fondo que cambia. 9/9 mutaciones a mano detectadas (la del tamaño, con el lienzo de 60×80).
- **Sin prueba en el export entero:** `RenderOptions::layer_cache` (apagarla) y `RenderOptions::on_composed` (cada fotograma compuesto, antes del codificador), y un test con un fondo con filtro y un vídeo encima: el MD5 de cada fotograma, igual con y sin caché. No se comparan los MP4: OpenH264 con varios hilos no da los mismos bytes dos veces (51 de 60 fotogramas distintos entre dos exports sin caché). `RenderStats::layer_cache_hits`, también en `genclip render --tiempos` (passagers: 1220 de 1242).
- **Memoria:** el pico de `genclip render` de passagers con 8 hilos es de 1,5 GB.
- **Docs:** un `\b` de Python se había comido la barra de `tools\dev\benchmark.ps1`; el README decía «por software, igual o más rápido» (filo va un 3 % por detrás) y las cifras finales se midieron a mano (con ~20 % de carga, `benchmark.ps1` se niega).
- **Casillas de F3:** «Opacidad, modos de fusión, filtros y ajustes de color», cerrada (los efectos de vídeo ya están en F4); «gc_render v1», partida en overlay (hecho) y relleno con doble encuadre (sin escenas de render: espera la decisión de Edu sobre su semántica).
- **MSVC:** 201/201, 0 warnings; MinGW 152/152; clang-tidy, 0 avisos.

**Revisión: los hallazgos del revisor** (`revisor-migracion` sobre `339730a..2000819`: aprobada, sin bloqueantes)

- **La clave aún tenía huecos sin probar:** un mutante que solo compara `src.sx` del recorte y `a`, `e` y `f` de la matriz pasaba. Escenarios nuevos: escala vertical, giro (`b` y `c`) y cada borde del recorte por separado (el fondo ahora va recortado con margen para poder mover cada borde sin salirse de la imagen; un primer intento movía `sy` fuera y el test fallaba por eso, no por la caché). 11/11 mutaciones de la clave detectadas.
- **`on_composed`** ya no cuenta en el tiempo de «texto» de `--tiempos`, y el test del export comprueba el tamaño del fotograma (un lienzo vacío daría el mismo MD5 con y sin caché).
- **Docs:** F5 en la tabla del TODO, 🟡 (estaba en ⬜ con decenas de partes hechas), y el README ya no dice que el preview espera a `gc_render`, sino que falta unir Qt con el render.
- **MSVC:** 201/201 (el test de la caché, también en Debug), 0 warnings; MinGW 152/152; clang-tidy, 0 avisos.

### 2026-10-29 — F5: las barras de los clips de audio en la timeline (pseudoWaveform)

- `gc_core/timeline_view.hpp`: `pseudo_waveform(semilla, barras)`, el `pseudoWaveform` de `lib/panning.js` que dibuja la timeline del editor en cada clip de audio. No sale del audio: barras pseudoaleatorias con la semilla del `asset_id` (si es falsy en JS, «a»; `String()` de JS para números, con el `Number::toString` de ECMAScript), el hash en uint32 sobre las unidades UTF-16 (`charCodeAt`) y un generador lineal en dobles como JS: el producto pasa de 2⁵³ y se redondea antes del `& 0x7fffffff`, así que se emula en doble, no en enteros. Envolvente que imita la voz y acotado a 0,12–1.
- **Paridad** frente al JS real (`editor_ui_js.mjs`, caso «forma de onda» de `timeline_ui.json`: 89 llamadas con semillas falsy, números, Unicode y emoji, y de 0 a 400 barras). Hasta 1 ulp por `Math.sin`: se compara con la tolerancia de 1e-12 de `near`, que ahora también recorre listas. Al regenerar la fixture, 6 valores de `zoomByDrag` cambiaron 1 ulp (el `Math.pow` del Node instalado); el test ya los compara con 1e-15 relativo.
- Falta dibujarlas en la timeline de QML (F5).
- **MSVC:** 201/201, 0 warnings; MinGW 152/152; clang-tidy, 0 avisos.

### 2026-10-31 — F3/F5: el fotograma suelto en gc_export (FrameRenderer), base del preview

- **Estado comprobado al empezar:** MinGW 152/152 (Debug y Release), MSVC 201/201 (Debug y Release), 0 warnings. Se quitó el worktree `claude/blissful-ellis-fcde91`, ya entero en `main` y limpio.
- `src/export/src/prepare.hpp/.cpp` (interno de `gc_export`): lo que preparan el export y el fotograma suelto antes de dibujar, antes duplicado en `render.cpp` y `apps/cli/frame_cmd.cpp`: el material de cada clip (`_clip_path` + `media_exists`), la timeline sin los clips sin material ni los motion graphics (que se guardan aparte), los efectos de vídeo (`plan_timeline_vfx`) y el matte de «Eliminar fondo» de cada clip; y cómo se abre el lector de un clip (`open_reader`, el tramo original si se hornea) y sale su fotograma (`read_image`, con el horneado).
- `gc/export/frame.hpp`: `FrameRenderer::open(proyecto, opciones)` y `tick(k)`/`at(s)`, el fotograma de `compose.render_frame`. Los lectores (`ClipFrames`, que da los mismos fotogramas que `export_frame_at`) se quedan abiertos de un fotograma al siguiente y se cierran los de los clips que no salen; el lienzo y las fuentes, una vez. `genclip frame` lo usa (antes decodificaba cada clip desde el principio con `export_frame_at`). Cambia el orden de algunos avisos: los del matte salen de todos los clips, no solo de los que se ven en ese instante.
- **Test:** «el fotograma suelto es el mismo que compone el export»: en cada escena de `export/synthetic.json`, cada fotograma de `FrameRenderer` (en orden y luego hacia atrás de 7 en 7) tiene el mismo MD5 que el que compone `render_project` (`on_composed`). Y `genclip frame` con tus dos proyectos de 4K en 0, 7,3, 21,05 y 40 s (1,5–8 s por fotograma en Release, con el decode incluido).
- **Benchmark de 4K (repetido con el PC sin juegos):** `38b3e5bd` entero en la segunda pasada: C++ 26,0 s con NVENC y 64,2 s con OpenH264; Python 56,2 s con NVENC forzado y 89,7 s con x264 (el de hoy). Con el mismo codificador, el C++ tarda menos de la mitad (NVENC) o un 28 % menos (por software, OpenH264 frente a x264). `c1dcc884` no llegó a medirse: la carga de fondo (iCUE, NZXT CAM, el navegador) pasó del umbral del 15 % del script.
- **MSVC:** 202/202 (Debug y Release), 0 warnings; MinGW 152/152; clang-tidy, 0 avisos en `src/export`, `apps/cli` y el test.

### 2026-10-31 — F3: la caché LRU con tope en bytes (lruBytes.js)

- `gc/core/lru_bytes.hpp`: `LruBytes<K, V>`, espejo de `lib/lruBytes.js` (la caché de canvas, fotogramas y pósters del editor de video-yt): cada entrada declara su peso; al pasar el tope se descartan las usadas hace más tiempo, salvo la que se acaba de guardar o redimensionar (una entrada sola mayor que el tope se conserva); `get` la pasa a reciente y `peek` no; `resize` recorta sin reordenar; `purge(keep)` y `clear` avisan (`onEvict`) en orden de antigüedad; `set` sobre una clave avisa del valor anterior salvo que sea igual (el `===` de JS); un `onEvict` que lanza no rompe la caché. `pixel_bytes(ancho, alto)`.
- **Paridad** frente al JS real (`tools/fixtures/lru_js.mjs`, `lru_cases.py`, `export_fixtures.py --solo lru`): 1052 pasos en 14 casos (los de `lruBytes.test.mjs`, bordes con tope 0 y el de por defecto, y 4 secuencias al azar) comparando, tras cada paso, lo devuelto, las claves en orden, los bytes, el tamaño y lo descartado.
- **Divergencias:** bytes en `size_t` (JS acepta cualquier cosa y la pasa a `max(0, Number(b) || 0)`), `pixelBytes` solo con dimensiones y sin el registro `cacheStats`.
- **MSVC:** 204/204, 0 warnings; MinGW 154/154; clang-tidy, 0 avisos.

### 2026-10-31 — F3, revisión de `c3a7b8c..HEAD`: FrameRenderer en las paridades de render y texto

- **Revisor (`revisor-migracion`): APROBADO**, con un hueco de cobertura: el test byte a byte de `FrameRenderer` solo recorría las 2 escenas de `export/synthetic.json`, sin revés, efectos horneados, ajustes con color (`__adj`), «Eliminar fondo», texto ni figuras.
- **Cerrado:** las paridades de render (`render/synthetic.json`, 184 escenas) y de texto (`render/text.json`) componen cada escena también con `FrameRenderer::at` (con `tests/parity/render_project.hpp`: los clips pasan a colección en `render/files`, que el render no distingue, y `DATA_DIR` = `render/files` para la caché de mattes) y piden los mismos bytes que su tubería, que sigue comparándose con el PNG de `compose.render_frame`. Así el camino de `genclip frame` y del preview queda probado contra el export en todo lo que el render dibuja. Con un mutante (`FrameRenderer` lee el fotograma siguiente) los tres tests fallan. Los fallos ya no imprimen los vectores de píxeles enteros (Catch2 los convertía a texto y un fallo tardaba minutos).
- **Menores:** `genclip frame` comprueba el rango del instante antes de preparar nada (como antes del cambio); `FrameRenderer` no guarda referencia al proyecto (comentario corregido). Se queda, documentado: en `render_project` el plan de efectos de vídeo va antes de la comprobación de motion graphics, así que con un motion graphic y un efecto que el render no sabe hornear, sin `--omitir`, el error es el del efecto. Notas del TODO al día (`export_frame_at`, `genclip frame`, el benchmark y el revés en `ClipFrames`, que hace dos decodes enteros por fotograma y es lo siguiente que importa para el preview).
### 2026-10-31 — F3: la caché de fotogramas compuestos en FrameRenderer

- `FrameOptions::cache_bytes`: `FrameRenderer` guarda cada fotograma compuesto en una `core::LruBytes` por tick (el peso, sus bytes RGBA) y, si se vuelve a pedir, lo da sin evaluar, decodificar ni componer (`cache_hits()`, `cache_bytes()`). 0 = sin caché (la LRU con tope 0 se quedaría la última entrada, así que ni se usa). Los avisos de un fotograma salen la primera vez.
- **Test** (en «el fotograma suelto es el mismo que compone el export»): con caché para 10 fotogramas, tras recorrerlos todos, los 10 últimos salen de la caché con el mismo MD5 que compone el export, el tope se respeta y el primero, ya descartado, se recompone igual.
- `LruBytes::drop` sigue tomando el iterador por valor (clang-tidy pedía referencia, pero en `set` viene del índice que `drop` borra antes de usarlo).
- **MSVC:** 204/204, 0 warnings; MinGW 154/154; clang-tidy, 0 avisos.

### 2026-10-31 — F3: el revés en ClipFrames, por bloques

- `ClipFrames::at` con `reverse` ya no llama a `export_frame_at` en cada tick (dos decodes enteros del tramo por fotograma: contar y tomar uno). Ahora cuenta el tramo una vez y decodifica un bloque de fotogramas seguidos que TERMINA en el pedido (el m-ésimo que suelta el filtro reverse es el (N−1−m)-ésimo del tramo, como en `export_frame_at`), tantos como quepan en 256 MB (en 1080p, 32; en 4K, 7). Al reproducir, el índice del tramo baja, así que los siguientes ticks salen del bloque: N fotogramas al revés cuestan ~N²/(2·bloque) decodificados en vez de 2·N². Usa el salto previo (preroll) del lector.
- **Test:** 5 casos al revés en «ClipFrames da los mismos fotogramas que export_frame_at» (30 en 30, 25 en 30 empezando en 0,29, velocidades 1,5 y 0,75, y con salto previo), con ticks crecientes, repetidos y saltos atrás. Un mutante (índice del tramo desplazado uno) falla.
- Ninguno de tus clips va al revés: sin medida real.
- **MSVC:** 204/204 (Debug y Release), 0 warnings; MinGW 154/154; clang-tidy, 0 avisos.

### 2026-10-31 — F4: el marco de TikTok/Shorts del preview (platformChrome.js)

- `gc/render/platform.hpp` y `src/render/src/platform.cpp`: `kPlatformOverlays` (PLATFORM_OVERLAYS), `overlay_label` y `draw_platform_chrome(lienzo, x, y, w, h, marco)`, línea a línea con `features/editor/render/platformChrome.js` sobre una capa mínima del contexto 2D en Skia: el estado de save/restore (relleno o degradado, trazo, grosor, puntas y uniones redondas, sombra, modo de fusión, fuente, alineación y línea de base), el trazado actual (que no es parte del estado), `arc` como el navegador (línea desde el punto actual, barrido módulo 2π y la vuelta entera en dos medias vueltas), `roundRect`, el recorte al recuadro, la sombra de CADA dibujo (DropShadow sin desplazamiento con sigma = shadowBlur / 2) y el `destination-out` del agujero del disco.
- **Texto:** `system-ui` es Segoe UI en Windows (la de Chromium), con el peso por la regla de CSS (`matchStyle` de DirectWrite = `matchStyleCSS3`: 500 → 400, 800 → 900); la forma, con HarfBuzz sobre los bytes de la fuente (interletraje; posiciones a 1/65536 px), y las líneas de base «top» y «middle» por la caja del em de Chromium (`NormalizedTypoAscentAndDescent`: sTypoAscender y sTypoDescender de OS/2 llevados a que sumen el tamaño, a 1/64 px). Con el ascenso de la fuente, los textos «top» bajaban ~5 px (25–27 dB); sin HarfBuzz, las líneas largas derivaban (34–38 dB).
- **Paridad** frente al JS REAL en el Canvas 2D de Chromium por software (`export_fixtures.py --solo plataforma`, `render/platform.json` y `render/platform/*.png`): 12 escenas (TikTok y Shorts a 180×320, 360×640 y 720×1280, dentro de un recuadro de un lienzo mayor, sin fondo, y «none», un marco desconocido y un ancho 0, que no dibujan nada y se comparan exactos) y las etiquetas del selector. Luma de los colores premultiplicados de 35,2 a 40,5 dB y alfa ≥ 40,3 dB (lo que queda es el antialias de glifos y sombras). Sin la sombra fallan 7 de 9.
- Falta mostrarlo en el preview del studio (F5), con el selector del visor (EdViewerTools).
- **MSVC:** 205/205 (Debug y Release), 0 warnings; MinGW 154/154; clang-tidy, 0 avisos.


### 2026-11-04 — F3: el autogiro de los vídeos de móvil en gc_media

- `autorotate_filters` (`src/media/src/decode.cpp`), espejo de `configure_input_video_filter` (`fftools/ffmpeg_filter.c`) y `get_rotation` (`cmdutils.c`) de FFmpeg 9: con la matriz de visualización del primer fotograma, `transpose=clock`/`cclock`/`clock_flip`/`cclock_flip` (según el signo de la matriz), `hflip` y/o `vflip` a 180°, `vflip` a 0° con la matriz volteada y `rotate={θ:f}*PI/180` en otro ángulo, delante de todo lo demás (también del trim de `-ss` y `-t`). Como todo el decode pasa por `Decoder::build_graph`, lo usan el export, `frame_at`, `ClipFrames`, el horneado de efectos y las dimensiones del material del render (que salen del fotograma decodificado; las del export, de `detect.dims` con OpenCV 5, que también da las giradas: comprobado).
- **Sondas:** con datos laterales en el stream, ffprobe escribe `unknown,` y `_color_untagged` da «con etiqueta»: el C++ lo copia (`nb_coded_side_data == 0`) y queda en «Pendiente por tu parte» (recomendación: arreglarlo en los dos).
- **Fixtures** (`export_fixtures.py --solo decode` y `--solo render --conservar`, nuevas): 10 medios derivados con `-display_rotation`/`-display_hflip`/`-display_vflip` y `-c copy` (una rama del CLI cada uno; MP4, MOV, Matroska VP8 y WebM con libvpx-vp9), 10 casos de decode (30 en total) y 14 instantes de seek (94), con el ancho y el alto de lo que sale del CLI (`_cli_dims`, no los de ffprobe); y 7 fotogramas de render (191) frente a `compose.render_frame`: vídeo de móvil a 90° en tres instantes, con recorte parcial, volteado, a 45° y con croma. Las fixtures que ya había no cambian.
- **Trampa:** el CLI negocia los formatos en el orden en que crea los filtros (primero los de `-vf`, luego el autogiro), y con `format=rgba` justo detrás el `rotate` va en RGBA; en el export (con `{cropscale}` detrás) y en el C++, en YUV. La referencia del caso de 45° lleva `,scale=iw:ih` (comprobado: con un `scale` detrás, el autogiro del CLI da los mismos MD5 que el C++).
- **Encontrado de paso** (notas de F3): `_shifted_keyframes` tira los keyframes del encuadre fuera del tramo (decisión nueva para Edu; ninguno de sus clips) y el `int()` del recorte del export cae un píxel antes con `0,35·180` (el redondeo pendiente).
- **Mutaciones a mano:** 8/8 detectadas (cada rama, el signo de `get_rotation`, el ángulo de `rotate` y la regla de las sondas); poner el giro detrás de los trim es equivalente.
- **MSVC:** 205/205 (Debug y Release), 0 warnings; MinGW 154/154 (Debug y Release); clang-tidy, 0 avisos en `decode.cpp` y `probe.cpp`.

### 2026-11-04 — F3, revisión de `7202cf4..HEAD` (revisor-migracion: aprobado, sin bloqueantes)

- **Autogiro y sondas:** el revisor comprobó las ramas, los signos, el NaN sin matriz y el orden frente a `ffmpeg_filter.c` y `cmdutils.c`, y la regla de las sondas frente a `print_stream` de `ffprobe.c` y el ffprobe del PATH. Todo cuadra. La divergencia teórica que queda (el CLI reconfigura el grafo si la matriz cambia a mitad del stream) no aparece en vídeos de móvil.
- **`--conservar`** solo saltaba un PNG por su nombre: con una escena cambiada o un índice movido se quedaba el viejo. Ahora lo vuelve a componer si su caso (instante y timeline) no es el del `synthetic.json` que ya hay (con todo igual, 2,5 s y ningún PNG tocado).
- **`ClipFrames` al revés:** el fotograma del tick se busca por bisección (`pts_of` no decrece) en vez de recorrer el tramo en cada tick (O(n²) al reproducir un clip largo al revés). Dos mutaciones a mano detectadas por «ClipFrames da los mismos fotogramas que export_frame_at».
- **TODO:** quitadas dos frases viejas (el autogiro «sin portar» y la caché de fotogramas como pendiente). Las miniaturas, los proxies y las formas de onda no tienen módulo de Python que portar: en video-yt las miniaturas las saca el navegador y las barras del audio son `pseudoWaveform`, ya portado. Llegan con el preview de F5.
- **MSVC:** 205/205 (Debug y Release), 0 warnings; clang-tidy, 0 avisos en `decode.cpp`. MinGW no compila nada de lo cambiado (154/154 en la tanda anterior).

### 2026-11-04 — F3: el decode por hardware en gc_media, con los mismos fotogramas que por software

- `DecodeSpec::hwaccel` y `ClipFrames::open(…, hwaccel)` (y `open_bake_input`): D3D11VA, CUDA, VAAPI o VideoToolbox, el primero que el códec admita y que se pueda crear (un dispositivo por tipo para todo el proceso, `shared_hw_device`), con un `get_format` que pide el de la GPU y, si libavcodec no puede iniciarlo, el de software. Solo H.264, HEVC, VP8, VP9 y AV1, cuyo decode es exacto por la norma: en MPEG-4 Part 2 (los MOV de la fixture) NVDEC no da lo mismo que FFmpeg.
- **Los mismos píxeles:** la GPU da los mismos YUV que el software (comprobado con D3D11VA y CUDA en el CLI), pero swscale convierte NV12 a RGBA por otro camino que YUV420P. El fotograma bajado pasa sin pérdidas al formato del decodificador por software (`to_software_format`: NV12 → YUV420P, P010 → YUV420P10) antes del grafo, que así es el mismo que por software.
- **Datos rotos:** con un error de decode o un fotograma marcado como dañado, la GPU oculta el daño a su manera (en el H.264 con paquetes rotos cambia el fotograma dañado), así que se vuelve a empezar por software y se saltan los fotogramas que ya salieron, que hasta ahí eran iguales.
- **Test** «decode por hardware: los mismos fotogramas que por software»: los 31 casos de `decode.json` con `hwaccel`, contra el MD5 del CLI por software; 20 salen de la GPU de este PC (RTX 4060 por D3D11VA). `GC_REQUIRE_HWACCEL=1` exige que la GPU decodifique alguno; sin GPU, el test pasa por software. 6/6 mutaciones a mano (sin la conversión, P010 desplazado de más, U y V cambiados, sin volver a software, sin saltar lo ya salido y todos los códecs por la GPU).
- **Fixture nueva:** un HEVC de 10 bits (`v_hevc10.mp4`, 320×180). Al pasar 10 bits a RGBA, el CLI de Gyan usa MMX en ensamblador en línea que la build de MSVC no tiene y redondea distinto (hasta 7 niveles, 50,9 dB): su referencia lleva `-cpuflags` sin MMX (`DECODE_CPUFLAGS`), con lo que coincide exacto. Divergencia de build, en las notas de F3.
- Pedirlo desde `gc_export` (`genclip render`, `genclip frame` y `FrameRenderer` del preview): hecho en la entrada siguiente.
- **MSVC:** 206/206 (Debug y Release), 0 warnings; clang-tidy, 0 avisos en `decode.cpp` y su test.

### 2026-11-04 — F3: el decode por hardware en gc_export y genclip (`--hwaccel`), medido

- `RenderOptions::hwaccel` y `FrameOptions::hwaccel` llegan a `open_reader` (`ClipFrames::open` y `open_bake_input`); `genclip render` y `genclip frame` aceptan `--hwaccel`.
- **Test** (en «el fotograma suelto es el mismo que compone el export»): con `hwaccel`, `render_project` compone los mismos fotogramas (MD5 de cada uno) que por software, y `FrameRenderer` también, en orden y a saltos hacia atrás. Con tus proyectos: `genclip frame` con y sin `--hwaccel` da el mismo PNG en Snowpiercer, TAREA2, IRON MAN y los dos de 4K.
- **Medido** con el PC al 10–12 % (Release, NVENC): tus proyectos de 4K tardan más con `--hwaccel` (38b3e5bd, 25,4 → 29,9 s; c1dcc884, 23,9 → 26,7 s). Con un dispositivo por decodificador da lo mismo (30,8 s): lo que cuesta es bajar cada fotograma de la GPU, que comparte con NVENC, cuando el decode por software en varios hilos ya va sobrado. Va apagado por defecto. En el preview solo compensará sin bajar el fotograma (la textura de la GPU directa a Skia).
- **MSVC:** 206/206 (Debug y Release), 0 warnings.

### 2026-11-04 — F6: los modelos de IA en RAM (model_manager.py)

- `gc/core/model_manager.hpp` (`ModelManager`, `model_idle_minutes`, `process_rss_bytes`): el registro de los modelos cargados con la función que los suelta, solo un pesado a la vez, `in_use` como guarda RAII, la descarga por inactividad (un hilo que arranca con el primer registro, cada 60 s) y el informe de memoria. El orden es el del dict de Python: volver a registrar no mueve el modelo y conserva «en uso» y la hora de carga. Las descargas van fuera del cerrojo; si una lanza, se avisa y se sigue. Es lo primero de F6 que no necesita ONNX Runtime: lo usarán los motores (Eliminar fondo, SAM, Whisper, TTS).
- **Paridad** (`export_fixtures.py --solo modelos`, `tests/fixtures/ai/model_manager.json`): 17 secuencias sobre el `model_manager.py` REAL con su reloj falso (`_set_clock`), sin el hilo (`_ensure_reaper`) y con `settings.load` sustituido (132 pasos: lo que devuelve cada uno y qué se descargó), y `idle_minutes()` con 24 ajustes (texto con espacios y con «_», NaN, ±inf, bool, listas, `models` que no es un dict…). Tests propios: el hilo con un intervalo de 10 ms y la memoria del proceso. 8/8 mutaciones a mano con MinGW.
- **Divergencias:** `settings.load()` que lanza (Python da 5) no tiene equivalente: el C++ recibe una función con los minutos. En macOS, `process_rss_bytes` da 0 (Python usa `ru_maxrss`, que es el pico).
- **Verificado:** MinGW Debug 157/157; MSVC Debug y Release 270/270 (ya con el studio: la entrada siguiente), 0 warnings y 0 avisos de clang-tidy en `model_manager`.

### 2026-11-04 — F5: el studio con MSVC y el Qt 6.11.2 de vcpkg

- **Qt:** feature `studio` del manifiesto (qtbase sin features por defecto, con gui, network, testlib, concurrent, png, jpeg, freetype, harfbuzz…; y qtdeclarative), pedida por los presets `msvc-vcpkg-*`. 38 min la primera vez; después, de la caché binaria. Hasta ahora el studio solo se había compilado en Linux con el Qt 6.4 de apt.
- **Lo que pedía Windows:** (1) moc de 6.11 necesita los tipos completos: `#include <QUrl>` en `editor_model.hpp` por el `QList<QUrl>`. (2) `gc_qt_runtime` (`cmake/GcQtRuntime.cmake`): el Qt de vcpkg deja sus plugins y su QML en `vcpkg_installed/<triplete>[/debug]/Qt6`, y el ejecutable no los encuentra; se genera un `qt.conf` junto a `gc_tests` y `genclip-studio` que apunta allí y se copian las DLL de Qt que cargan esos plugins (no hay `windeployqt` en vcpkg). (3) offscreen en Windows busca las fuentes en `<prefijo>/lib/fonts`, que vcpkg no trae: sin fuentes, los textos no miden y la maqueta se descoloca (tres tests fallaban por eso); los tests ponen `QT_QPA_FONTDIR` en `%WINDIR%\Fonts`.
- **Sin ventanas en los tests:** sin el plugin, cada test del studio acababa en un qFatal que, en Debug, abría la ventana «Microsoft Visual C++ Runtime Library» y se quedaba esperando (ocho a la vez en el PC de Edu). `tests/support/no_crash_dialogs.cpp` manda por stderr los abort, los assert de la CRT de depuración y los qFatal, y quita la de «dejó de funcionar»: el test falla y sigue.
- **Un test que dependía de la velocidad:** «arrastrar hasta el borde desplaza la vista» esperaba 300 ms fijos a que la vista corriera 100 px. En Debug con MSVC cada golpe del Timer (16 ms) rehace la vista previa sin optimizar y tarda ~50 ms; con el PC ocupado, uno solo cabía. Ahora espera a que corra (`QTest::qWaitFor`, hasta 5 s).
- **clang-tidy**, por primera vez sobre el studio (solo existe con la build de MSVC): `removeIf` de QList, los `int64` del lienzo a `double` explícitos, un `Json` por referencia, los `const` de los tests y destructores de los tests que no lanzan. Más un NOLINT razonado en `History` (`session.hpp`): mover su `std::deque` reserva memoria con la STL de MSVC en Debug.
- **MSVC:** 270/270 (Debug y Release), 0 warnings, 0 avisos de clang-tidy en el studio.

### 2026-11-05 — F5: la vista previa del editor

- **Qué pinta:** el fotograma del cabezal con `gc::exporter::FrameRenderer`, es decir, lo mismo que el export en ese fotograma (la misma preparación: material, efectos de vídeo, matte). Con `omit_unsupported` (el editor enseña lo que sabe dibujar y avisa del resto) y 256 MB de caché de fotogramas. La timeline es la de la sesión (`EditSession::timeline()`, un snapshot inmutable que se comparte con el hilo sin copiar los clips, como pensaba `Cow`); el proyecto (material, ajustes, colecciones) sale de `load_project` sobre la raíz del almacén (`<raíz>/data/projects.json`).
- **`PreviewSource`** (`apps/studio/preview_source.hpp`): un hilo con su `FrameRenderer`. Se queda con la última petición, así que con el cabezal corriendo salta a la más nueva en vez de encolar. Una timeline nueva (otro puntero) vuelve a preparar el render y repite el último instante; la misma, no. Fuera de la timeline, el fotograma más cercano. Publica en el hilo de la interfaz (`invokeMethod` con el propio objeto como contexto: si ya no existe, no llega). Error y avisos como propiedades.
- **`FramePreview`** (`apps/studio/frame_preview.hpp`, QML `GenClip.Preview`): `QSGImageNode` con mipmaps (el de `QSGSimpleTextureNode` no los usa: su material pisa el filtro de la textura), encajado sin deformar; sin fotograma, sin nodo, como `QQuickImage`. Registrado a mano (`register_qml_types`) porque `gc_studio` no es un módulo QML y así pasa `gc_set_warnings`.
- **Sin gc_export** (Linux sin vcpkg) el studio se compila igual: `available` es false y el recuadro lo dice.
- **Probado** con una copia de «lucy» de video-yt (720 × 1280; vídeo, imágenes, GIF, textos y el fondo de la biblioteca), con `genclip-studio --at <s> --grab`, que ahora espera al fotograma.
- **Tests** sin pantalla: byte a byte igual al del export en 0 s (solo material que falta: negro y su aviso) y en 7 s (la imagen de la fixture, de un color), en pantalla (la captura de la ventana tiene ese color dentro del recuadro, y no antes), tras cambiar la timeline (multiplicar sobre negro la quita), pasado el final (el último tick) y al cerrar; y una timeline vacía dice por qué. Los destructores de los tests paran el hilo antes de borrar la carpeta temporal.
- **Verificado:** MSVC Debug y Release 272/272, MinGW Debug y Release 157/157 (sin el studio), 0 warnings, 0 avisos de clang-tidy en el studio y en gc_export.

### 2026-11-05 — F5: el reproductor con el reloj del audio

- **Salida de audio** (`gc/media/audio_out.hpp`): miniaudio 0.11.25 de vcpkg (feature `media`, una cabecera; su implementación en `gc_miniaudio`, sin nuestros avisos, porque es C de terceros). `PcmCursor` sirve un PCM estéreo en float desde una posición y la avanza con lo que pide la tarjeta; sin PCM o pasado su final, silencio con el reloj corriendo. `AudioOut` abre la salida por defecto (o el backend nulo con `GC_AUDIO_DEVICE=null`, el de los tests) y su posición es el reloj.
- **La mezcla** (`gc/export/audio.hpp`, `mix_timeline_audio`): el bloque de audio de `render_project` sale a `detail::mix_audio` (con `detail::material_paths`, el mismo bucle que usaba `prepare`), así que el export y el reproductor suenan con el mismo PCM. Test: en la paridad del export, la mezcla tiene la misma sonoridad por bloque de 100 ms que el MP4 de video-yt (±0,5 dB).
- **`Player`** (`apps/studio/player.hpp`): al reproducir abre la salida (la primera vez), arranca el reloj en el cabezal y pide la mezcla de la timeline de la sesión a su hilo; la mezcla entra sin moverse cuando llega. Sonando, un cambio de la timeline pide otra (suena la anterior hasta entonces); mover el cabezal salta el reloj; al final se para en la duración. Sin salida o sin gc_export, reloj de pared. El cabezal a 60 Hz y, sonando, `setPlayhead` no avisa al inspector en cada fotograma (al parar, una vez).
- **QML:** el Timer que avanzaba el cabezal por reloj de pared se va; Espacio y ▶ llaman al reproductor, que se para al salir del editor y al abrir Recortar.
- **Tests:** el cursor (exacto, sin tarjeta), el reloj con el backend nulo (corre, se para, salta), y en el studio sin pantalla: Espacio y ▶ (con el foco en el botón, Espacio cambia una vez), el salto del cabezal sonando, el final, el inspector quieto sonando y la mezcla que llega y se rehace al cambiar la timeline.
- **Falta:** el preview a ½ o ¼ (el compuesto trabaja en píxeles: hay que pasar una escala por la evaluación y el render), los proxies y restar la latencia de la tarjeta al reloj.
- **Verificado:** MSVC Debug y Release 277/277, MinGW Debug y Release 157/157, 0 warnings, 0 avisos de clang-tidy (dos NOLINT razonados por la API en C de miniaudio).

### 2026-11-05 — F5: la vista previa a ½ o ¼

- **Cómo:** no se toca la evaluación (sus capas son transformaciones afines en px del cuadro) sino el lienzo. `Canvas::make(w, h, scale)` reserva round(w·s) × round(h·s) y pone una escala de base en el SkCanvas; `width()`/`height()` siguen siendo las del cuadro, así que el compuesto, las figuras, los textos y las capas intermedias (efectos de clip, texto en capa propia: a 1:1) no cambian, y `snapshot()` da los píxeles reducidos. Es lo que pide la fila de gc_render de la tabla de módulos: el preview reducido y el export 1:1 con el mismo código.
- **Lo que trabajaba en píxeles a mano:** la capa de ajuste (`apply_color_matrix` redibujaba la instantánea con la escala: ahora en el espacio del dispositivo), el bicúbico separable (su camino rápido exigía la matriz identidad: ahora acepta la de base y lleva la escala dentro), `snapshot`/`opaque_pixels`/la caché de capas (el tamaño de los píxeles). Las máscaras A8 de los glifos, las capas de texto y la máscara de `end_layer` son del cuadro: en un lienzo reducido se muestrean bilineales (`device_sampling`); con la matriz identidad, el más cercano de antes. A 1:1 nada cambia: la paridad del render y del export sigue igual.
- **`FrameOptions::scale`** compone así; `PreviewSource` elige la escala con `scale_for` (la más pequeña de 1, ½ y ¼ que llena el recuadro en píxeles de la pantalla, que le dice `FramePreview`) y vuelve a preparar el render cuando cambia. La textura que sube `FramePreview` es también más pequeña.
- **Test:** en la paridad del render, cada una de las 191 escenas por `FrameRenderer` a ½ y ¼ frente a su 1:1 promediado por bloques: a ½ la peor 33,9 dB («desplazado a escala 0,8», mediana 55,7), a ¼ 25,9 («contraste 10», mediana 33,3); suelos de 32 y 24 dB. Lo que se dibujara fuera de sitio bajaría a 10–15. Y en el lienzo: tamaños, `fill_rect` en coordenadas del cuadro, la capa de ajuste en todos los píxeles, la máscara y un blit en su sitio; en el studio, la escala que elige.
- **Falta:** los proxies: el decode sigue a tamaño completo, que con material 4K es lo que más cuesta.
- **Verificado:** MSVC Debug y Release 282/282, MinGW Debug y Release 157/157, 0 warnings, 0 avisos de clang-tidy; y «lucy» capturada a ½ con el studio (igual que a 1:1).

### 2026-11-05 — F5: los proxies de la vista previa

- **Qué no es:** el `data/proxies` de video-yt son los tramos ≤480p que baja el editor de reencuadre (`reframe.py`) para seguir caras; nada que ver. Los proxies del editor no tienen Python que portar y van en la caché de la app, nunca en la carpeta del proyecto.
- **Cuáles** (`gc/export/proxy.hpp`): de los vídeos con el lado corto de más de 1200 px (1440p, 4K), H.264 con el lado corto a 960 (lo que pide la vista previa a ½ de un horizontal encuadrado en vertical). No de WebM (se leen con libvpx-vp9 por su alfa), de lo que tiene alfa en el primer fotograma ni de imágenes; tampoco se usan en un clip con «Eliminar fondo» o efectos de vídeo (el matte y el horneado van en píxeles del original).
- **El tiempo:** el export lee el vídeo con `fps=F,trim=a:b` (convierte a los fps de la timeline antes de recortar). El proxy se hace con ese mismo `fps=F` y `start_time=0` (y el `color_pre` si el original no declara matriz), así que su fotograma k es el fotograma k que el export saca del original: `ClipFrames` sobre el proxy da el mismo fotograma en cada tick. Una clave por fps.
- **La caché:** `<caché>/<clave>.mp4` y `<clave>.json`, con la clave FNV-1a de la ruta, el tamaño, la fecha, los fps y la versión; el JSON también marca «no hace falta». Se escribe en `.part.mp4` y se renombra; parar no deja nada.
- **En el render** (`FrameOptions::proxies`): el clip se lee del proxy, la evaluación ve el tamaño del original y los blits pasan a píxeles del proxy (el recorte por la escala y la transformación a la inversa), así que llega al mismo sitio.
- **En el studio** (`ProxyMaker`): al abrir el proyecto y con cada vídeo nuevo de la timeline, un hilo de baja prioridad coge los que ya hay y hace, uno a uno, los que faltan; la vista previa vuelve a preparar el render con cada uno. Progreso bajo la vista previa. La caché, la de la app (`QStandardPaths::CacheLocation`/proxies).
- **Tests:** tamaños; hacerlo, reconocerlo, otro fps, el que no hace falta, pararlo y caducar con el archivo; el fotograma k del proxy y del original (el gris de cada fotograma dice cuál es) en un clip que empieza dentro del vídeo; `FrameRenderer` con y sin proxy a ½ (un relleno con recorte y un superpuesto) a 30 dB o más; y en el studio, un vídeo de 2560 × 1440 que se hace proxy en la caché y la vista previa que lo usa (byte a byte lo de `FrameRenderer` con él).
- **Falta:** limpiar la caché (hoy solo crece) y hacerlos al importar, cuando el studio importe material.
- **Verificado:** MSVC Debug y Release 287/287, MinGW Debug y Release 157/157, 0 warnings, 0 avisos de clang-tidy.

### 2026-11-05 — F5: exportar desde el editor

- **Como `jobs._run_export`:** el botón ⤓ exporta la timeline de la sesión (que el editor ya guarda en cada cambio, el «persistir la timeline usada» del job) con `render_project` a `<carpeta del proyecto>/exports/final_<AAAAMMDD-HHMMSS en UTC>.mp4`, con los mensajes del job («Vídeo final exportado.», «Error exportando el vídeo final.», «No hay nada en la timeline para exportar.»). `ExportJob` (`apps/studio/export_job.hpp`) lo hace en un hilo y publica el progreso unas 5 veces por segundo.
- **Divergencia:** video-yt renderiza antes los motion graphics de la timeline (su motion service); el render C++ aún no, así que el export los omite con un aviso (`omit_unsupported`) y la tarjeta los enseña al acabar, en vez de fallar.
- **Cancelar:** `RenderOptions::cancelled` se mira antes de evaluar cada fotograma; si dice que sí, `render_project` para los hilos y devuelve las estadísticas con `cancelled` (no un error) y el editor borra el MP4 a medias. Cerrar la app con un export en marcha lo cancela.
- **Tests** sin pantalla: el botón, la tarjeta con su progreso, el MP4 en exports con la duración de la timeline (9 s) y el aviso de «a.mp4», cerrar la tarjeta; cancelar sin dejar nada; y una timeline vacía que no empieza.

### 2026-11-05 — F5: las miniaturas del inicio

- **Qué enseña Home.jsx:** cada tarjeta pone `<video src={p.clips[0].url} preload="metadata">` con `object-fit: cover`: el primer fotograma del primer clip del proyecto, recortado al centro. No hay Python que portar (lo pinta el navegador).
- **`Thumbnails`** (`apps/studio/thumbnails.hpp`): el fotograma de `gc::media::frame_at` en 0 s (con el autogiro), reducido y recortado a 440 × 248 (el recuadro de 220 × 124 al doble) y guardado en JPEG en la caché de la app, con la clave de la ruta, el tamaño, la fecha y la versión (como los proxies): un archivo cambiado da otra. Un hilo de baja prioridad mira la caché y hace las que faltan, una a una; una petición nueva sustituye a la anterior y una que falla no se reintenta hasta que el vídeo cambie.
- **En el inicio:** `HomeModel` resuelve el vídeo del primer clip como `storage.resolve_media` y publica `thumbs` (id → JPEG) aparte de `projects`, así que llegar una miniatura no recrea las tarjetas (no corta un renombrado ni un arrastre). `RoundedImage` (GenClip.Preview, un `QQuickPaintedItem`) la pinta con las esquinas de arriba redondeadas: recortar con esquinas en Qt Quick pide MultiEffect, de Qt 6.5, y el studio sigue compilando con Qt 6.4.
- **Divergencia:** video-yt no guarda nada y con el archivo perdido deja el recuadro vacío; aquí se queda el icono.
- **Tests:** el recorte de un vídeo vertical en tres franjas (sale la del medio), la caché que la reconoce y caduca con el archivo, un vídeo roto o que no existe sin restos; y en el studio sin pantalla, la tarjeta que la recibe, la pinta (la captura tiene el vídeo en el centro y el fondo en la esquina redondeada) y la conserva al renombrar.
- **Verificado:** MSVC Debug y Release 291/291 (con los 2 del export de la tanda anterior), MinGW Debug y Release 157/157 (sin el studio), 0 warnings, 0 avisos de clang-tidy en el studio.

### 2026-11-05 — F7: el núcleo sin red del servidor MCP

- **Qué es:** lo de `backend/app/mcp_server` que no habla con la red ni ejecuta tools: `help_content.py`, `registry.py` (sin el envoltorio del SDK), `dto.py`, `capabilities.py`, `resources.py` y `audit.py`. Va en `gc_core` (`gc/core/mcp_*.hpp`), sin dependencias nuevas, así que se prueba también con MinGW y en Linux. El transporte (JSON-RPC 2.0 por Streamable HTTP en `:8000/mcp`) y las tools llegan después, en `gc_agent`.
- **Decisión:** `gc_agent` y `gc_cloud` en C++, no en Rust (lo que quedaba abierto en §13): el núcleo ya está aquí con su paridad, las tools son las ops de `gc_core` y el export de `gc_export`, y un segundo lenguaje pediría FFI, cargo junto a CMake y otra toolchain en móvil.
- **La ayuda como datos:** TOOL_DOMAINS (100 tools: el servidor ha crecido desde las 50 del plan) y HELP (15 guías) salen del módulo real con `tools/gen/gen_mcp_help.py` a `src/core/src/mcp_help.inc`, en literales de menos de 2 KB (el límite de MSVC); el test compara cada guía byte a byte.
- **Lo que Python pregunta a la máquina** (motores de TTS, GPU, ffmpeg en el PATH, el proveedor de IA, las categorías de SFX) llega ya resuelto en `RuntimeEnv`: sus módulos son de F6 y F7.
- **Semántica de Python que importa:** `x or 0.0` (el −0,0 pasa a 0,0), `max(0.0, x)` con NaN, `len()` de unos beats que no son una lista (el TypeError sale como un fallo, no como un ValueError), `dict.update` que deja las claves en su sitio, `strip().lower()` de Unicode en `describe`, `progress or 0` (sale el entero 0), la hora con `isoformat(timespec="milliseconds")` (trunca), el registro escrito en modo texto (CRLF en Windows) y leído con `read_text` + `splitlines()` (corta también en U+0085, U+2028, \x1c…) + `strip()` y los errores de UTF-8 con el texto de CPython.
- **Arreglado de paso:** `py_json_loads` aceptaba un BOM al principio porque nlohmann se lo salta; `json.loads` no, y ahora tampoco el C++.
- **Paridad** (`export_fixtures.py --solo mcp`, `mcp_cases.py`, `test_mcp_parity.cpp`): 389 casos contra los módulos REALES con la máquina, el reloj, los uuid y los jobs sustituidos: 12 proyectos (con valores raros: velocidades 0, negativas, NaN e infinitas, ids y pistas repetidos, beats que no son listas), 27 proporciones, 9 jobs, 3 análisis, 3 entornos × 25 dominios, 126 lecturas de resources (el texto exacto de `json.dumps`) y 96 pasos de auditoría con el archivo tras cada uno; más `match_resource`, el error de UTF-8 y `splitlines()` con tests propios.
- **Mutaciones:** 30/30 (`tools/dev/mutantes.py mcp`). La primera pasada dejó dos: una equivalente (con velocidad 0, dividir entre 1 o no dividir da lo mismo; se cambió por la de dividir con velocidad negativa) y un hueco de verdad, el tope de 200 de `read_recent` (ningún paso tenía más de 200 entradas: ahora hay uno con 250).
- **Verificado:** MSVC Debug y Release 298/298, MinGW Debug y Release 164/164 (GCC 15.2: un `istreambuf_iterator` daba un falso positivo de `-Wnull-dereference`; se lee con el tamaño conocido, como `io.cpp`), 0 warnings, 0 avisos de clang-tidy.
