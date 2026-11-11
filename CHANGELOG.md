# Changelog

Los cambios de GenClipPro, de la versión más nueva a la más vieja. El detalle de cada tanda (cómo se
verificó y con qué cifras) está en el §14 de [docs/PLAN_MIGRACION_CPP.md](docs/PLAN_MIGRACION_CPP.md); lo
pendiente, en [docs/TODO.md](docs/TODO.md).

La versión está en `CMakeLists.txt` (`project(... VERSION)`) y la dice `genclip --version`. Mientras dure la
migración (0.x):

- **menor** (0.**N**.0): cada tanda que añade algo que se puede usar;
- **parche** (0.N.**P**): arreglos, revisiones y cambios sin nada nuevo;
- **1.0.0**: cuando se retire la app Python (F9).

## [0.5.0] — 2026-11-11

### Añadido

- **Eliminación personalizada (SAM) en la pestaña «Fondo»:** pincel y borrador inteligentes y normales sobre la vista
  previa, con el clip sin recortar y la selección en cian encima (POST `/bg-segment` al soltar el trazo), análisis
  del clip en segundo plano (`/bg-analyze`), los fotogramas marcados para saltar a ellos, «Último trazo», «Limpiar»,
  calidad del modelo y «Aplicar a todo el clip».
- **Retocar máscara:** Conservar y Eliminar sobre el matte de la automática, que se ve en el recorte mientras se pinta.
- Con esto, «Eliminar fondo» queda entero en el studio.
- `gc/core/bg_ui.hpp`: toggleBgCustom, los trazos, las marcas y el paso del cuadro a la fuente (por la inversa del
  blit), con paridad contra el JS real, también contra `canvasToSourceNorm` y `bgBrushRadiusPx`, y sus mutaciones.
- Barra de desplazamiento en el inspector.

## [0.4.0] — 2026-11-11

### Añadido

- **«Exportar recorte»** en la pestaña «Fondo», para vídeos y GIF con matte listo o croma: hornea el clip a un WebM
  transparente en el backend (POST `/api/projects/{id}/bg-cutout`, cancelable) y aparece en el material.
  `bg_cutout_body` en `gc/core/bg_ui.hpp`, con paridad contra `exportBgCutout` real y sus mutaciones.

## [0.3.0] — 2026-11-10

### Añadido

- **Chroma key en la pestaña «Fondo»:** activar, color (muestra con el selector, #RRGGBB o los cuatro preestablecidos),
  **cuentagotas** (toma el píxel compuesto bajo el clic en la vista previa), tolerancia, suavizado, derrame, limpiar
  borde, expandir y restablecer.
- **Contorno del recorte:** activar, color, grosor, difuminado (halo) y opacidad.
- **Fondo de vista previa:** normal, cuadros, color, o una imagen o un vídeo de prueba (que se repite). Solo en la
  vista previa: `FrameOptions::backdrop` y `render::Backdrop`, que el export no usa.
- `bg_patch_chroma`, `bg_patch_outline` y `bg_reset_chroma` en `gc/core/bg_ui.hpp`, con paridad contra el
  VideoEditor.jsx real (500 llamadas en total en el caso de «Eliminar fondo») y sus mutaciones.

## [0.2.0] — 2026-11-10

### Añadido

- **«Eliminar fondo» en el studio (F5, por el puente con el backend de video-yt).** Pestaña «Fondo» del
  inspector para vídeos e imágenes, como la «Eliminación automática» de EdBgRemove.jsx: activarla (propone
  RVM en un vídeo, BiRefNet en una imagen y U²-Net en un GIF, con su alto de matte), elegir motor y detalle,
  estabilizar, **Aplicar / Recalcular** (POST `/api/projects/{id}/bg-removal`, sondeado cada 700 ms),
  **Cancelar** (DELETE `/api/job/{id}`) y el estado (calculando, error, aviso del motor o listo). Con el matte
  listo, **Ajustar recorte**: invertir, umbral, suavizado, pluma, expandir y opacidad, al instante y sin
  volver a ejecutar el modelo. Los motores y dónde se ejecutan salen de GET `/api/bg/providers`. Cada cambio
  es un paso de deshacer y la vista previa lee el matte de `data/bgcache`.
- `gc/core/bg_ui.hpp`: lo que hace el editor con `clip.bg_removal` (toggleBgAuto, changeBgAuto,
  startBgAutoFor con su cuerpo y el final del job), con paridad contra el VideoEditor.jsx real (270 llamadas
  en `--solo editor`) y sus mutaciones (`mutantes.py bg_ui`).
- `BackendJobs::cancel`.
- Este CHANGELOG y la regla de versiones.

### Sin hacer todavía

- La Eliminación personalizada (SAM y sus pinceles), retocar la máscara, «Exportar recorte» y el croma del
  inspector.
- Probarlo contra el backend real: el test sin pantalla usa uno falso.

## [0.1.0] — 2026-10-07 a 2026-11-09

Todo lo anterior a este archivo, por fases (el detalle, en el §14 del plan):

- **F0, cimientos:** CMake con presets (MinGW, MSVC, MSVC + vcpkg, Linux, macOS y CI), avisos estrictos,
  clang-tidy, spdlog y el CI escrito.
- **F1, modelo y formato:** el modelo de `schemas.py` con sus migraciones, ajustes y claves, biblioteca,
  SFX en disco, colecciones, personajes stick, almacén de proyectos, segmentos y JSON como Python.
- **F2, edición:** las 42 operaciones, sin solapes, keyframes, historial en disco y autoguardado.
- **F3 y F4, render y export:** `gc_media`, `gc_render` y `genclip render` de punta a punta, los 42 efectos
  de vídeo, el croma con contorno, el matte de «Eliminar fondo» desde la caché y la medida del texto como
  Pillow.
- **F5, el studio (Qt Quick):** inicio, timeline, material, inspector, ajustes, vista previa con el
  fotograma del export, reproductor con el reloj del audio, export con progreso, máscara y pincel, y por el
  puente con el backend: subtítulos, «Seguir cara» y «Cargar audio».
- **F6, IA local (sin ONNX Runtime):** beats, presupuesto de RAM de los modelos y lo de alrededor del
  detector de caras.
- **F7, nube y MCP:** las piezas sin red de YouTube y el servidor MCP por HTTP (`genclip mcp`) con 44 de sus
  100 tools.
