# Plan de migración: video-yt → GenClipPro (C++)

> Estado: **en marcha** desde 2026-10-06 · F0 hecha (salvo MSVC y CI) · F1 casi cerrada (faltan colecciones y SFX de disco) · F2 cerrada · F3 en curso (lo que no necesita MSVC). Ver §14, [ARQUITECTURA.md](ARQUITECTURA.md) y la lista de tareas [TODO.md](TODO.md).
> Origen: `E:\proyectos\video-yt` (FastAPI + React). Destino: `C:\Users\Edu\CLionProjects\GenClipPro`.

## 0. Resumen

- **Qué se construye:** un **motor C++ único** (modelo, edición, decodificación, render, IA local) que se ejecuta igual en todos los dispositivos, con una **UI Qt Quick (QML)** encima. Ya no hay servidor HTTP entre la UI y el motor: la UI llama directamente a C++.
- **La mayor ventaja no es la velocidad, sino tener un solo renderizador.** Hoy el preview (`canvas.js` + `lib/*.js`) y el export (`compose.py` → FFmpeg `filter_complex` + libass + Chromium) son dos motores con la lógica duplicada en JS y en Python. En C++ el **mismo compositor (Skia)** dibuja el preview y el export, así que la paridad entre ambos ya no hay que mantenerla a mano.
- **Cómo se migra:** de forma **incremental (strangler)**. La app Python sigue en uso hasta que la C++ la iguale, y cada fase entrega algo que se usa de verdad:
  - **M1 (~4–5 meses):** la app Python exporta con el motor C++ (`genclip render`).
  - **M2 (~8–10 meses):** editor C++ de escritorio usable; lo que aún no está portado (IA, Motion) se le pide al backend Python por su API REST actual.
  - **M3 (~12–15 meses):** se retira Python; instaladores para Windows, macOS y Linux.
  - **M4 (~15–19 meses):** Android e iOS.
- **Coste honesto:** ~92 k líneas de lógica (42,5 k de Python + 50 k de JS/JSX), lo que supone **≈ 56–81 semanas** de trabajo dedicado. Lo que protege el plan es que la paridad se **mide con fixtures generadas desde Python**: los 20,6 k de líneas de tests no se reescriben a mano.

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
| **T3** | Android (arm64), iOS | Editor, export e IA «ligera» (RVM mobilenet, U²-Netp, Whisper tiny/base, Kokoro). **Sin descarga de YouTube** (las políticas de Play Store y App Store la prohíben). Motion Studio en remoto o desactivado al principio. |
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
| **gc_render** | Compositor de capas: vídeo, imagen, GIF, texto, figuras, máscaras, modos de fusión, filtros y ajustes de color (SkSL), chroma key, capa de ajuste, barras de cine, transiciones, efectos animados, stick y paper. Dibuja sobre una superficie de cualquier tamaño: **el preview a ½ o ¼ de resolución y el export a resolución completa usan el mismo código.** | Skia (GPU: D3D/Metal/Vulkan/GL; CPU raster como alternativa), HarfBuzz + SkParagraph |
| **gc_ai** | Eliminar fondo (RVM, BiRefNet, U²-Net) con caché en 2 niveles, SAM 2.1 + seguimiento + pincel, Whisper (palabras con timestamps), TTS (Kokoro, Piper), YuNet + reframe, tracking LK + RANSAC, beats (FFT), `model_manager` (descarga en el primer uso y presupuesto de RAM) | ONNX Runtime C++ (CUDA, DirectML/WinML, CoreML, NNAPI/XNNPACK, CPU), whisper.cpp, OpenCV (core/imgproc/video/objdetect/dnn), espeak-ng |
| **gc_cloud** | Clientes REST y SSE: Gemini, OpenAI-compatible (openrouter/groq/cerebras/mistral/hf), Azure (TTS/STT/Vision/imagen), Foundry, Pexels/Giphy/Pixabay/Unsplash | libcurl (+ cpr), JSON |
| **gc_services** | Proyectos, carpetas del inicio, biblioteca y colecciones, ajustes y API keys, jobs (pool de hilos con progreso y cancelación), import, YouTube (sidecar `yt-dlp` solo en escritorio) | gc_core, gc_media |
| **gc_agent** | Agente de chat con tools en el mismo proceso y servidor MCP en `:8000/mcp` (Streamable HTTP) con el **mismo contrato** que hoy: los 50 tools, DTO semántico y errores estructurados. Un único registro de tools, como `registry.py`. | cpp-httplib o Boost.Beast, gc_cloud |
| **gc_platform** | Rutas de datos por SO, detección de GPU y sondas reales (como `gpu.py`), permisos móviles, almacenamiento | Código condicional por SO |
| **studio** | UI QML; la lógica vive en ViewModels `QObject`. El preview es un `QQuickItem` que muestra el frame de gc_render. | Qt 6 (Quick, QuickControls2) |
| **cli** | `genclip render|frame|probe|parity` | Todo lo anterior salvo Qt |

### 4.2 Decisiones técnicas y motivo

- **Skia como compositor.** El Canvas 2D de Chrome *es* Skia, así que `canvas.js`, `lib/*.js`, paper y stick se portan casi 1:1:

  | Canvas 2D (JS) | Skia (C++) |
  |---|---|
  | `drawImage` | `drawImageRect` |
  | `globalCompositeOperation` | `SkBlendMode` |
  | `ctx.filter` | `SkColorFilter` / `SkImageFilter` |
  | `Path2D` | `SkPath` |

  El preview se verá como hoy. Además, Skia trae Skottie (Lottie) y un módulo SVG para Motion (§6).
- **FFmpeg solo para lo que hace bien:** decodificar, codificar, audio y algún filtro puntual. Ya no compone la imagen: el `filter_complex` no sirve para un preview interactivo y es justo el origen de la duplicación.
- **Qt Quick para la UI.** Es el único toolkit C++ maduro que cubre Windows, macOS, Linux, Android e iOS con una sola base de código. QML es declarativo, mentalmente parecido a JSX, y CLion lo soporta.
- **Modelo de hilos:** el hilo de UI solo muestra; el hilo de render recibe **snapshots inmutables** de la timeline (con estructura compartida); hay hilos de decodificación por clip visible y el reloj maestro es el de audio. Las ediciones entran como **ops** (las mismas que usa el MCP), lo que da undo/redo y diffs en lugar de enviar la timeline entera, como pasa ahora con el `PUT` y las respuestas del MCP.
- **Errores:** `std::expected<T, gc::Error>` entre módulos; en el MCP se mapea al enum de 8 códigos actual.
- **Prompts y plantillas como datos.** Los prompts de `scene_direction.py`, `motion/generator.py`, «Generar escena» y «Generar recurso» se sacan a `resources/prompts/*.md` y `resources/templates/*.json`. Son contenido del producto, no código, y así ambas apps los comparten durante la transición.

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
| `motion/stick.js` + `stick.py` | gc_render/stick | M | Port directo a Skia. |
| `features/paper` | gc_render/paper + UI | M | |
| `motion/` (HTML/GSAP + Playwright) | ver §6 | XL | |
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
| UI React (editor 24 k, paper, motion, ai, audio, direction, projects) | studio (QML) | **XL** | Se porta por pantallas, en orden de uso (F5). |

---

## 6. Motion Studio y contenido HTML

El problema: la IA genera **HTML, CSS y GSAP**, y eso necesita un motor web.

| Opción | Ventajas | Inconvenientes |
|---|---|---|
| **A. Sidecar Chromium headless por CDP** (escritorio). Se descarga `chrome-headless-shell` al usarlo y se controla por DevTools Protocol desde C++ (`__seek(t)`, después `Page.captureScreenshot` con fondo transparente). Es el mismo protocolo que usa hoy Playwright. | Mantiene el generador, los prompts y el validador tal cual. Es determinista. | ~100 MB adicionales y solo funciona en escritorio. |
| **B. Formato nativo.** La IA genera un JSON declarativo (grafo de escena + tweens) que renderiza gc_render, o bien Lottie (Skottie). | Un solo motor, funciona en móvil y es rápido. | Hay que rehacer el generador y los prompts. Además, los motion graphics de *concepto* que buscas necesitan un vocabulario expresivo. |

**Recomendación:** A en F8 para no perder nada en escritorio, más un **spike de B** (2 semanas) para decidir el formato a largo plazo. En móvil, al principio, Motion Studio renderiza en el escritorio o en un servidor (T4).

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
| **F8** Motion, Stick, Paper | §6 | 6–10 | |
| **F9** Empaquetado escritorio | instaladores, modelos en el primer arranque, updates | 2–3 | **M3** |
| **F10** Móvil | Android + iOS | 8–12 | **M4** |
| | **Total** | **≈ 56–81** | |

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
- Texto completo (temas de subtítulos, karaoke, contorno, sombras, 3D, máscara en texto), appear/exit, looks, capa de ajuste, barras de cine, film, contorno del sujeto, chroma key, efectos animados (fx → SkSL), alfa de «Eliminar fondo» leído desde la caché existente y marco de plataforma (solo en preview).
- **Salida:** todo lo descrito en `docs/` de video-yt se exporta desde C++. libass y Chromium-fx dejan de usarse en el export.

### F5 — Editor de escritorio Qt (10–14 semanas) → **M2**
- **Orden de pantallas:**
  1. Inicio (proyectos y carpetas).
  2. Editor: preview + timeline (arrastre sin solapes, pista magnética, zoom, beats y marcadores).
  3. Inspector: transform, texto, filtros, máscaras, modal de recortar.
  4. Biblioteca y materiales.
  5. Export con progreso.
  6. Ajustes.
  7. Atajos de teclado.
- **Reproductor:** reloj de audio, preview a ½ o ¼ de resolución, proxies generados al importar, repintado solo cuando cambia algo y 60 fps en el cabezal sin re-render del árbol de UI (justo lo que hoy cuesta en React).
- **Puente transitorio:** lo que aún no está portado (eliminar fondo, Whisper, TTS, chat, Motion, YouTube) se pide al backend Python **por su API REST actual** (`:8000`), como cliente HTTP. Se retira en F6–F8.
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

### F8 — Motion, Stick, Paper (6–10 semanas)
- Stick y Paper nativos en Skia; Motion Studio con la opción A (§6) y el spike de B.

### F9 — Empaquetado escritorio (2–3 semanas) → **M3**
- CPack: Windows con MSIX o NSIS/WiX; macOS `.dmg` firmado y notarizado; Linux AppImage o Flatpak.
- Modelos y Chromium se descargan en el primer uso (sustituye a `setup/nucleo.py`). Actualizaciones e informes de fallos (Crashpad).
- **Se retira la app Python.**

### F10 — Móvil (8–12 semanas) → **M4**
- Qt for Android/iOS; UI táctil (layouts QML adaptativos y gestos en timeline y preview).
- Almacenamiento con *scoped storage* y selector de Fotos; codecs por hardware (MediaCodec, VideoToolbox); ORT con NNAPI/XNNPACK o CoreML; modelos «lite»; límites de RAM.
- Export en segundo plano: *foreground service* en Android; en iOS es limitado.
- Sin YouTube; Motion en remoto o desactivado.

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
| Motion Studio en móvil | Medio | §6: remoto al principio y spike de formato nativo. |
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
| 3 | Motion Studio | Sidecar Chromium (A) + spike de formato nativo (B) | Solo A, o solo B |
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
- **Un BOM de UTF-8 ya no se acepta donde Python lo rechaza.** `json.loads(path.read_text("utf-8"))` falla con un BOM; C++ lo saltaba sin decir nada. `read_json_file_py` falla igual, y la usan los ajustes, la biblioteca y el historial: con BOM, los ajustes de fábrica, la biblioteca vacía y el historial vacío, como en video-yt. `read_json_file` (proyectos y timelines, que Pydantic lee de otra forma) sigue igual.
- **`py_lower`: `str.lower()` de Python** con toda la tabla de Unicode 16 (incluidas las expansiones, como «İ» → «i̇») y la sigma final («ΟΔΟΣ» → «οδος»). La tabla y los rangos «cased» y «case-ignorable» se generan desde el Python de video-yt (`tools/gen/gen_py_lower.py` → `py_lower.inc`).
- **`py_path_name`, `py_path_suffix` y `py_path_stem`:** `Path(x).name`, `.suffix` y `.stem` de Python 3.14 en Windows (unidades, UNC, «x.» con sufijo «.»…). **`py_quote`** pasa de la biblioteca a `py.hpp`.

**Verificación:**
- **Casos:** 3 pasos nuevos con BOM (ajustes y biblioteca) contra el Python real, y un test del historial con BOM.
- **Tests:** 26 comprobaciones de `py_lower` y 90 de rutas y `quote`, con los valores medidos en Python.
- **Resultado:** 80 tests y 110 740 aserciones en verde, en Debug y en Release, con 0 warnings.
