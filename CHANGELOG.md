# Changelog

Los cambios de GenClipPro, de la versión más nueva a la más vieja. El detalle de cada tanda (cómo se
verificó y con qué cifras) está en el §14 de [docs/PLAN_MIGRACION_CPP.md](docs/PLAN_MIGRACION_CPP.md); lo
pendiente, en [docs/TODO.md](docs/TODO.md).

La versión está en `CMakeLists.txt` (`project(... VERSION)`) y la dice `genclip --version`. Mientras dure la
migración (0.x):

- **menor** (0.**N**.0): cada tanda que añade algo que se puede usar;
- **parche** (0.N.**P**): arreglos, revisiones y cambios sin nada nuevo;
- **1.0.0**: cuando se retire la app Python (F9).

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
